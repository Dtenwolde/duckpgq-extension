#pragma once

#include "duckpgq/common.hpp"

namespace duckpgq {

namespace core {

bool GetPathFindingOption(ClientContext &context);
int32_t GetPathFindingTaskSize(ClientContext &context);
int32_t GetLightPartitionMultiplier(ClientContext &context);
double_t GetHeavyPartitionFraction(ClientContext &context);
double_t GetBottomUpThreshold(ClientContext &context);
bool GetEnableBottomUpSearch(ClientContext &context);

struct CorePGQOptions {
  static void Register(DatabaseInstance &db) {
    RegisterExperimentalPathFindingOperator(db);
    RegisterPathFindingTaskSize(db);
    RegisterPathFindingLightPartitionMultiplier(db);
    RegisterPathFindingHeavyPartitionFraction(db);
    RegisterPathFindingBottomUpThreshold(db);
    RegisterPathFindingEnableBottomUpSearch(db);
  }

private:
  static void RegisterExperimentalPathFindingOperator(DatabaseInstance &db);
  static void RegisterPathFindingTaskSize(DatabaseInstance &db);
  static void RegisterPathFindingLightPartitionMultiplier(DatabaseInstance &db);
  static void RegisterPathFindingBottomUpThreshold(DatabaseInstance &db);
  static void RegisterPathFindingHeavyPartitionFraction(DatabaseInstance &db);
  static void RegisterPathFindingEnableBottomUpSearch(DatabaseInstance &db);



};;

} // namespace core

} // namespace duckpgq
