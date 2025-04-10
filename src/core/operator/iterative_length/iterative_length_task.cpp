#include "duckpgq/core/operator/iterative_length/iterative_length_task.hpp"
#include <duckdb/parallel/event.hpp>
#include <duckpgq/core/operator/iterative_length/iterative_length_state.hpp>
#include <duckpgq/core/operator/physical_path_finding_operator.hpp>
#include <duckpgq/core/option/duckpgq_option.hpp>
#include <fstream>

namespace duckpgq {
namespace core {

IterativeLengthTask::IterativeLengthTask(shared_ptr<Event> event_p,
                                   ClientContext &context,
                                   shared_ptr<IterativeLengthState> &state, idx_t worker_id,
                                   const PhysicalOperator &op_p)
: ExecutorTask(context, std::move(event_p), op_p), context(context),
  state(state), worker_id(worker_id) {
  explore_done = false;
}

void IterativeLengthTask::CheckChange(std::vector<std::bitset<LANE_LIMIT>> &seen,
                                      std::vector<std::bitset<LANE_LIMIT>> &next,
                                      shared_ptr<LocalCSR> &local_csr) const {
  for (auto i = local_csr->start_vertex; i < local_csr->end_vertex; i++) {
    if (next[i].any()) {
      next[i] &= ~seen[i];
      seen[i] |= next[i];
      if (!state->change && next[i].any()) {
        state->change = true;
      }
    }
  }
}

std::bitset<LANE_LIMIT> IterativeLengthTask::IntersectFrontiers(int64_t v_size,
  vector<std::bitset<LANE_LIMIT>> &src_seen,
  vector<std::bitset<LANE_LIMIT>> &dst_seen) {
  std::bitset<LANE_LIMIT> result;
  for (auto v = 0; v < v_size; v++) {
    result |= src_seen[v] & dst_seen[v];
  }
  return result;
}


TaskExecutionResult IterativeLengthTask::ExecuteTask(TaskExecutionMode mode) {
  auto &barrier = state->barrier;
  while (state->started_searches < state->pairs->size()) {
    barrier->Wait(worker_id);

    if (worker_id == 0) {
      state->InitializeLanes();
    }
    barrier->Wait(worker_id);
    do {
      bool is_backward = state->iter & 1;
      bool swap_buffers = state->iter & 2;
      auto& seen      = is_backward ? state->dst_seen : state->src_seen;
      auto& visit_from = swap_buffers
                         ? (is_backward ? state->dst_visit2 : state->src_visit2)
                         : (is_backward ? state->dst_visit1 : state->src_visit1);

      auto& visit_to   = swap_buffers
                         ? (is_backward ? state->dst_visit1 : state->src_visit1)
                         : (is_backward ? state->dst_visit2 : state->src_visit2);

      auto& local_csrs = is_backward ? state->local_reverse_csrs : state->local_csrs;
      IterativeLength(visit_from, visit_to, seen, local_csrs);
      barrier->Wait(worker_id);
      if (worker_id == 0) {
        std::bitset<LANE_LIMIT> done = IntersectFrontiers(state->v_size, state->src_seen, state->dst_seen);
        ReachDetect(done);
      }
      barrier->Wait(worker_id);
    } while (state->change);
    if (worker_id == 0) {
      UnReachableSet();
    }

    // Final synchronization before finishing
    barrier->Wait(worker_id);
    if (worker_id == 0) {
      state->Clear();
    }
    barrier->Wait(worker_id);
  }

  event->FinishTask();
  return TaskExecutionResult::TASK_FINISHED;
}

double IterativeLengthTask::ExploreTopDown(const std::vector<std::bitset<LANE_LIMIT>> &visit,
                                  std::vector<std::bitset<LANE_LIMIT>> &next,
                                  const std::atomic<uint32_t> *v, const std::vector<uint16_t> &e, size_t v_size, idx_t start_vertex) {
  auto start_time = std::chrono::high_resolution_clock::now();
  for (auto i = 0; i < v_size; i++) {
    if (visit[i].any()) {
      auto start_edges = v[i].load(std::memory_order_relaxed);
      auto end_edges = v[i+1].load(std::memory_order_relaxed);
      for (auto offset = start_edges; offset < end_edges; offset++) {
        auto n = e[offset] + start_vertex; // Use the local edge index directly
        next[n] |= visit[i]; // Propagate the visit bitset
      }
    }
  }
  // Capture end time
  auto end_time = std::chrono::high_resolution_clock::now();
  return std::chrono::duration<double, std::milli>(end_time - start_time).count(); // Return time in ms
}

// void IterativeLengthTask::RunExplore(const std::vector<std::bitset<LANE_LIMIT>> &visit,
//                                      std::vector<std::bitset<LANE_LIMIT>> &next,
//                                      const std::atomic<uint32_t> *v,
//                                      const std::vector<uint16_t> &e,
//                                     size_t v_size, idx_t start_vertex) {
//
//   // Decide traversal strategy based on frontier size
//   // You can tune this threshold! For now: bottom-up if > 5% of v_size
//   double time_taken;
//   time_taken = ExploreTopDown(visit, next, v, e, v_size, start_vertex);
//
//   // Get thread & core info *outside* Explore to reduce per-call overhead
//   std::thread::id thread_id = std::this_thread::get_id();
//   int core_id = -1; // Default if not available
// #ifdef __linux__
//   core_id = sched_getcpu();
// #elif defined(__APPLE__)
//   uint64_t tid;
//   pthread_threadid_np(NULL, &tid);
//   core_id = static_cast<int>(tid % std::thread::hardware_concurrency()); // Approximate core ID mapping
// #endif
//
//   // Store result safely
//   {
//     std::lock_guard<std::mutex> guard(state->log_mutex);
//     state->timing_data.emplace_back(thread_id, core_id, time_taken, state->num_threads, v_size, e.size(), state->local_csrs.size(), state->iter);
//   }
// }

void IterativeLengthTask::IterativeLength(std::vector<std::bitset<LANE_LIMIT>> &visit,
                                          std::vector<std::bitset<LANE_LIMIT>> &next,
                                          std::vector<std::bitset<LANE_LIMIT>> &seen,
                                          std::vector<shared_ptr<LocalCSR>> &local_csrs) {
    auto &barrier = state->barrier;
    size_t num_csr_partitions = local_csrs.size();

    // Clear `next` array
    while (state->partition_counter.load() < num_csr_partitions) {
      auto csr_index = state->partition_counter.fetch_add(1);
      if (csr_index >= num_csr_partitions) {
        break;
      }
      auto &local_csr = local_csrs[csr_index];
      for (auto i = local_csr->start_vertex; i < local_csr->end_vertex; i++) {
        next[i] = 0;
      }
    }
    // Finished clearing the next array

    barrier->Wait(worker_id);

    // Reset the partition counter and initialize some values
    state->partition_counter.store(0, std::memory_order_release);
    static std::atomic<int> finished_tasks(0);
    barrier->Wait(worker_id);

    while (state->partition_counter.load() < num_csr_partitions) {
      auto csr_index = state->partition_counter.fetch_add(1);
      if (csr_index >= num_csr_partitions) {
        break;
      }
      auto &local_csr = local_csrs[csr_index];
      ExploreTopDown(visit, next,
        local_csr->v, local_csr->e,
        local_csr->GetVertexSize(), local_csr->start_vertex);
    }
    state->change = false;
    // Mark this thread as finished
    finished_tasks.fetch_add(1);
    // Last thread reaching here should reset the counter for the next iteration
    if (finished_tasks.load() == state->tasks_scheduled) {
      finished_tasks.store(0); // Reset for the next phase
      state->partition_counter.store(0);
    }
    // Finished exploration phase
    barrier->Wait(worker_id);

    while (state->partition_counter.load() < num_csr_partitions) {
      auto csr_index = state->partition_counter.fetch_add(1);
      if (csr_index >= num_csr_partitions) {
        break;
      }
      auto &local_csr = local_csrs[csr_index];
      CheckChange(seen, next, local_csr);
    }
    // Finished setting the seen array and checking for changes
    barrier->Wait(worker_id);
    state->partition_counter = 0;
}

void IterativeLengthTask::ReachDetect(std::bitset<LANE_LIMIT> done) const {
  auto result_data = FlatVector::GetData<int64_t>(state->pf_results->data[0]);

  // detect lanes that finished
  for (int64_t lane = 0; lane < LANE_LIMIT; lane++) {
    int64_t search_num = state->lane_to_num[lane];
    if (search_num >= 0) { // active lane
      if (done[lane]) {
        result_data[search_num] =
            state->iter + 1; /* found at iter => iter = path length */
        state->lane_to_num[lane] = -1; // mark inactive
        state->active--;
      }
    }
  }
  if (state->active == 0) {
    state->change = false;
  }
  // into the next iteration
  state->iter++;
}

void IterativeLengthTask::UnReachableSet() const {
  auto result_data = FlatVector::GetData<int64_t>(state->pf_results->data[0]);
  auto &result_validity = FlatVector::Validity(state->pf_results->data[0]);

  for (int64_t lane = 0; lane < LANE_LIMIT; lane++) {
    int64_t search_num = state->lane_to_num[lane];
    if (search_num >= 0) { // active lane
      result_validity.SetInvalid(search_num);
      result_data[search_num] = (int64_t)-1; /* no path */
      state->lane_to_num[lane] = -1;     // mark inactive
    }
  }
}

} // namespace core
} // namespace duckpgq