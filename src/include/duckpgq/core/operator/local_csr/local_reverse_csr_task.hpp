#pragma once

#include "duckpgq/common.hpp"
#include "local_reverse_csr_state.hpp"

#include <duckpgq/core/operator/physical_path_finding_operator.hpp>

namespace duckpgq {
namespace core {

class LocalReverseCSRTask : public ExecutorTask {
public:
  LocalReverseCSRTask(shared_ptr<Event> event_p, ClientContext &context,
                           shared_ptr<LocalReverseCSRState> &state,
                           idx_t worker_id,
                           const PhysicalOperator &op_p);

  TaskExecutionResult ExecuteTask(TaskExecutionMode mode) override;

  void ComputeVertexPartitionsByEdgeCount() const;
  void FillLocalCSRPartition() const;

  shared_ptr<LocalReverseCSRState> &local_reverse_csr_state;
  idx_t worker_id;

};

} // namespace core
} // namespace duckpgq
