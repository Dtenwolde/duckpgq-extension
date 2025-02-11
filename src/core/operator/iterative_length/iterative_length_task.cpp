#include "duckpgq/core/operator/iterative_length/iterative_length_task.hpp"
#include <duckpgq/core/operator/iterative_length/iterative_length_state.hpp>
#include <duckpgq/core/operator/physical_path_finding_operator.hpp>

namespace duckpgq {
namespace core {

IterativeLengthTask::IterativeLengthTask(shared_ptr<Event> event_p,
                                   ClientContext &context,
                                   shared_ptr<IterativeLengthState> &state, idx_t worker_id,
                                   const PhysicalOperator &op_p,
                                   unique_ptr<LocalCSR> local_csr_)
: ExecutorTask(context, std::move(event_p), op_p), context(context),
  state(state), worker_id(worker_id), local_csr(std::move(local_csr_)) {
}

void IterativeLengthTask::CheckChange(vector<std::bitset<LANE_LIMIT>> &seen,
                                      vector<std::bitset<LANE_LIMIT>> &next,
                                      int64_t *v, vector<int64_t> &e) const {
  // Printer::PrintF("CheckChange: %d %d %d\n", worker_id, local_csr->v_offset,
  //                 local_csr->vsize);
  for (auto i = local_csr->v_offset; i < std::min(local_csr->v_offset + local_csr->vsize, seen.size()); i++) {
    if (next[i].any()) {
      next[i] &= ~seen[i];
      seen[i] |= next[i];
      if (next[i].any()) {
        state->change_atomic.store(true, std::memory_order_relaxed);
      }
    }
  }
}


  TaskExecutionResult IterativeLengthTask::ExecuteTask(TaskExecutionMode mode) {
  auto &barrier = state->barrier;
  while (state->started_searches < state->pairs->size()) {
    barrier->Wait();

    if (worker_id == 0) {
      state->InitializeLanes();
      // Printer::Print("Finished intialize lanes");
    }
    barrier->Wait();
    do {
      IterativeLength();
      barrier->Wait();

      if (worker_id == 0) {
        // Printer::Print("Starting reach detect");
        ReachDetect();
        // Printer::Print("Finished reach detect");
      }
      barrier->Wait();
    } while (state->change_atomic);
    if (worker_id == 0) {
      UnReachableSet();
    }

    // Final synchronization before finishing
    barrier->Wait();
    if (worker_id == 0) {
      state->Clear();
    }
    barrier->Wait();
  }

  event->FinishTask();
  return TaskExecutionResult::TASK_FINISHED;
}

void IterativeLengthTask::Explore(vector<std::bitset<LANE_LIMIT>> &visit,
                                  vector<std::bitset<LANE_LIMIT>> &next,
                                  int64_t *v, vector<int64_t> &e) {
  // Printer::PrintF("Explore - (worker %d): %d %d", worker_id, local_csr->v_offset,
                  // local_csr->vsize);
  // Printer::PrintF("(worker %d): %s", worker_id, local_csr->ToString());

  for (auto i = local_csr->v_offset; i < std::min(local_csr->v_offset + local_csr->vsize, visit.size()); i++) {
    // Printer::PrintF("All offsets - (worker %d): %d %d %d", worker_id, local_csr->v_offset, local_csr->vsize, i);
    // Printer::PrintF("Visit any - (worker %d): %d %d", worker_id, visit[i].any(), i);
    if (visit[i].any()) {  // If the vertex has been visited
      // Printer::PrintF("Offsets - (worker %d): %d %d %d", worker_id, v[i], v[i + 1], local_csr->v_offset);
      for (auto offset = v[i]; offset < v[i + 1]; offset++) {
        auto n = e[offset - v[local_csr->v_offset]];  // Use the local edge index directly
        // Printer::PrintF("Pre N - (worker %d): %d %d %d", worker_id, i, n, offset);
        next[n] |= visit[i]; // Propagate the visit bitset
        // Printer::PrintF("Post N - (worker %d): %d %d %d", worker_id, i, n, offset);
      }
      // Printer::PrintF("Finalized - (worker %d): %d %d", worker_id, i, v[i]);
    }
  }
  // Printer::PrintF("Loop complete - (worker %d)", worker_id);
}

void IterativeLengthTask::IterativeLength() {
    auto &seen = state->seen;
    auto &visit = state->iter & 1 ? state->visit1 : state->visit2;
    auto &next = state->iter & 1 ? state->visit2 : state->visit1;
    auto &barrier = state->barrier;
    int64_t *v = (int64_t *)local_csr->global_v;
    vector<int64_t> &e = local_csr->e;

    // Clear `next` array regardless of task availability
    if (worker_id == 0) {
      for (int64_t i = 0; i < state->v_size; i++) {
        next[i] = 0;
      }
      // Printer::PrintF("Visit size: %d", visit.size());
    }

    // Synchronize after clearing
    barrier->Wait();
    // Printer::PrintF("%d starting explore\n", worker_id);

    Explore(visit, next, v, e);
    // Printer::PrintF("%d finished explore\n", worker_id);

    // Check and process tasks for the next phase
    state->change_atomic.store(false, std::memory_order_relaxed);
    barrier->Wait();
    CheckChange(seen, next, v, e);

    barrier->Wait();
}

void IterativeLengthTask::ReachDetect() const {
  auto result_data = FlatVector::GetData<int64_t>(state->pf_results->data[0]);

  // detect lanes that finished
  for (int64_t lane = 0; lane < LANE_LIMIT; lane++) {
    int64_t search_num = state->lane_to_num[lane];
    if (search_num >= 0) { // active lane
      int64_t dst_pos = state->vdata_dst.sel->get_index(search_num);
      if (state->seen[state->dst[dst_pos]][lane]) {
        result_data[search_num] =
            state->iter; /* found at iter => iter = path length */
        state->lane_to_num[lane] = -1; // mark inactive
        state->active--;
      }
    }
  }
  if (state->active == 0) {
    state->change_atomic.store(false, std::memory_order_relaxed);
  }
  // into the next iteration
  state->iter++;
  // Printer::PrintF("Starting next iteration: %d\n", state->iter);
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