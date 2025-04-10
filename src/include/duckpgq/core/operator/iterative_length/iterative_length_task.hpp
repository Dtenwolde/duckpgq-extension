#pragma once

#include "duckpgq/common.hpp"
#include "iterative_length_state.hpp"

#include <duckpgq/core/operator/physical_path_finding_operator.hpp>

namespace duckpgq {
namespace core {

class IterativeLengthTask : public ExecutorTask {
public:
  IterativeLengthTask(shared_ptr<Event> event_p, ClientContext &context,
                           shared_ptr<IterativeLengthState> &state, idx_t worker_id,
                           const PhysicalOperator &op_p);

  TaskExecutionResult ExecuteTask(TaskExecutionMode mode) override;
private:
  void IterativeLength(std::vector<std::bitset<LANE_LIMIT>> &visit,
                       std::vector<std::bitset<LANE_LIMIT>> &next,
                       std::vector<std::bitset<LANE_LIMIT>> &seen,
                       std::vector<shared_ptr<LocalCSR>> &local_csrs);
  void ReachDetect(std::bitset<LANE_LIMIT> done) const;
  void CheckChange(std::vector<std::bitset<LANE_LIMIT>> &seen,
                   std::vector<std::bitset<LANE_LIMIT>> &next,
                   shared_ptr<LocalCSR> &local_csr) const;
  void UnReachableSet() const;

  std::bitset<LANE_LIMIT> IntersectFrontiers(int64_t v_size,
                                             vector<std::bitset<LANE_LIMIT>> &src_seen,
                                             vector<std::bitset<LANE_LIMIT>> &dst_seen);

  double ExploreTopDown(const std::vector<std::bitset<LANE_LIMIT>> &visit,
    std::vector<std::bitset<LANE_LIMIT>> &next,
    const std::atomic<uint32_t> *v,
    const std::vector<uint16_t> &e,
    size_t v_size, idx_t start_vertex);

  // void RunExplore(const std::vector<std::bitset<LANE_LIMIT>> &visit,
  //               std::vector<std::bitset<LANE_LIMIT>> &next,
  //               const std::atomic<uint32_t> *v,
  //               const std::vector<uint16_t> &e,
  //               size_t v_size, idx_t start_vertex);
private:
  ClientContext &context;
  shared_ptr<IterativeLengthState> &state;
  idx_t worker_id;
  bool explore_done; // Flag to indicate task completion
};


} // namespace core
} // namespace duckpgq