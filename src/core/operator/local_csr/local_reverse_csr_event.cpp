#include "duckpgq/core/operator/local_csr/local_reverse_csr_event.hpp"

#include <duckpgq/core/option/duckpgq_option.hpp>
#include <fstream>

namespace duckpgq {
namespace core {

LocalReverseCSREvent::LocalReverseCSREvent(shared_ptr<LocalReverseCSRState> local_csr_state_p,
                          Pipeline &pipeline_p, const PhysicalPathFinding &op_p, ClientContext &context_p)
    : BasePipelineEvent(pipeline_p), local_reverse_csr_state(std::move(local_csr_state_p)), op(op_p), context(context_p) {
}

void LocalReverseCSREvent::Schedule() {
  auto &context = pipeline->GetClientContext();
  vector<shared_ptr<Task>> csr_tasks;
  for (idx_t tnum = 0; tnum < local_reverse_csr_state->num_threads; tnum++) {
    csr_tasks.push_back(make_uniq<LocalReverseCSRTask>(
        shared_from_this(), context, local_reverse_csr_state, tnum, op));
    local_reverse_csr_state->tasks_scheduled++;
  }
  local_reverse_csr_state->barrier = make_uniq<Barrier>(local_reverse_csr_state->tasks_scheduled);
  SetTasks(std::move(csr_tasks));
}

void LocalReverseCSREvent::FinishEvent() {
  // Assume at least one partition exists
  D_ASSERT(!local_reverse_csr_state->partition_csrs.empty());

  std::sort(local_reverse_csr_state->partition_csrs.begin(), local_reverse_csr_state->partition_csrs.end(),
          [](const shared_ptr<LocalReverseCSR>& a, const shared_ptr<LocalReverseCSR>& b) {
              return a->GetEdgeSize() > b->GetEdgeSize();  // Sort by edge count
          });
}

} // namespace core
} // namespace duckpgq