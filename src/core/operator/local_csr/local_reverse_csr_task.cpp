#include "duckpgq/core/operator/local_csr/local_reverse_csr_task.hpp"
#include <duckdb/parallel/event.hpp>
#include <duckpgq/core/operator/local_csr/local_csr_state.hpp>
#include <duckpgq/core/operator/physical_path_finding_operator.hpp>
#include <duckpgq/core/option/duckpgq_option.hpp>

namespace duckpgq {
namespace core {

LocalReverseCSRTask::LocalReverseCSRTask(shared_ptr<Event> event_p, ClientContext &context,
                           shared_ptr<LocalReverseCSRState> &state, idx_t worker_id_p,
                           const PhysicalOperator &op_p)
    : ExecutorTask(context, std::move(event_p), op_p), local_reverse_csr_state(state),
      worker_id(worker_id_p) {}

TaskExecutionResult LocalReverseCSRTask::ExecuteTask(TaskExecutionMode mode) {
  auto &barrier = local_reverse_csr_state->barrier;
  if (worker_id == 0) {
    ComputeVertexPartitionsByEdgeCount(); // Phase 2
  }
  barrier->Wait(worker_id);
  FillLocalCSRPartition();
  barrier->Wait(worker_id);

  event->FinishTask();
  return TaskExecutionResult::TASK_FINISHED;
}

void LocalReverseCSRTask::FillLocalCSRPartition() const {
  while (true) {
    idx_t partition = local_reverse_csr_state->partition_index.fetch_add(1);
    if (partition >= local_reverse_csr_state->partition_csrs.size()) {
      break; // all partitions claimed
    }

    auto &local_csr = *local_reverse_csr_state->partition_csrs[partition];
    idx_t start_vertex = local_csr.start_vertex;
    idx_t end_vertex = local_csr.end_vertex;

    auto &global_v = local_reverse_csr_state->global_csr->v;
    auto &global_e = local_reverse_csr_state->global_csr->e;

    auto *local_v = local_csr.GetVertexArray();
    auto &local_e = *local_csr.GetEdgeVector();

    idx_t global_edge_start = global_v[start_vertex];
    idx_t global_edge_end = global_v[end_vertex];

    // Resize local edge vector
    local_e.resize(global_edge_end - global_edge_start);

    // Fill local v array (adjusted to local offset)
    for (idx_t i = start_vertex; i <= end_vertex; ++i) {
      local_v[i - start_vertex] = global_v[i] - global_edge_start;
    }
    local_v[end_vertex - start_vertex + 1] = local_v[end_vertex - start_vertex].load();

    // Copy edges into local e array
    for (idx_t i = global_edge_start; i < global_edge_end; ++i) {
      local_e[i - global_edge_start] = global_e[i];
    }

    local_csr.initialized_e = true;
  }
}

void LocalReverseCSRTask::ComputeVertexPartitionsByEdgeCount() const {
  idx_t num_partitions = local_reverse_csr_state->num_threads * 4;
  auto &v = local_reverse_csr_state->global_csr->v;  // V array of reverse CSR
  idx_t total_vertices = local_reverse_csr_state->global_csr->vsize - 2;
  idx_t total_edges = local_reverse_csr_state->global_csr->e.size();

  idx_t edges_per_partition = (total_edges + num_partitions - 1) / num_partitions;

  idx_t current_start = 0;
  idx_t current_target = edges_per_partition;

  local_reverse_csr_state->partition_csrs.clear(); // std::vector<std::unique_ptr<LocalReverseCSR>>

  for (idx_t i = 1; i <= total_vertices; ++i) {
    if (v[i] >= current_target || i == total_vertices) {
      // Create LocalReverseCSR for this range
      auto local_csr = make_uniq<LocalReverseCSR>(current_start, i);
      local_reverse_csr_state->partition_csrs.emplace_back(std::move(local_csr));

      current_start = i;
      current_target += edges_per_partition;
    }
  }
}


} // namespace core
} // namespace duckpgq
