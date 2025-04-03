#include "duckpgq/core/option/duckpgq_option.hpp"
#include "duckpgq/common.hpp"

namespace duckpgq {
namespace core {

bool GetPathFindingOption(ClientContext &context) {
  Value value;
  context.TryGetCurrentSetting("experimental_path_finding_operator", value);
  return value.GetValue<bool>();
}

int32_t GetPathFindingTaskSize(ClientContext &context) {
  Value value;
  context.TryGetCurrentSetting("experimental_path_finding_operator_task_size", value);
  return value.GetValue<int32_t>();
}

int32_t GetLightPartitionMultiplier(ClientContext &context) {
  Value value;
  context.TryGetCurrentSetting("experimental_path_finding_operator_light_partition_multiplier", value);
  return value.GetValue<int32_t>();
}

double_t GetHeavyPartitionFraction(ClientContext &context) {
  Value value;
  context.TryGetCurrentSetting("experimental_path_finding_operator_heavy_partition_fraction", value);
  return value.GetValue<double_t>();
}

double_t GetBottomUpThreshold(ClientContext &context) {
  Value value;
  context.TryGetCurrentSetting("experimental_path_finding_operator_bottom_up_threshold", value);
  return value.GetValue<double_t>();
}

bool GetEnableBottomUpSearch(ClientContext &context) {
  Value value;
  context.TryGetCurrentSetting("experimental_path_finding_operator_enable_bottom_up", value);
  return value.GetValue<bool>();
}

void CorePGQOptions::RegisterExperimentalPathFindingOperator(
    DatabaseInstance &db) {
  auto &config = DBConfig::GetConfig(db);
  config.AddExtensionOption("experimental_path_finding_operator",
  "Enables the experimental path finding operator to be triggered",
  LogicalType::BOOLEAN, Value(false));
}

void CorePGQOptions::RegisterPathFindingTaskSize(
    DatabaseInstance &db) {
  auto &config = DBConfig::GetConfig(db);

  config.AddExtensionOption("experimental_path_finding_operator_task_size",
    "Number of vertices processed per thread at a time", LogicalType::INTEGER, Value(256));
}

void CorePGQOptions::RegisterPathFindingLightPartitionMultiplier(
    DatabaseInstance &db) {
  auto &config = DBConfig::GetConfig(db);

  config.AddExtensionOption("experimental_path_finding_operator_light_partition_multiplier",
    "Multiplier used for the light partitions of the local CSR partitioning", LogicalType::INTEGER, Value(1));
}

void CorePGQOptions::RegisterPathFindingHeavyPartitionFraction(
    DatabaseInstance &db) {
  auto &config = DBConfig::GetConfig(db);

  config.AddExtensionOption("experimental_path_finding_operator_heavy_partition_fraction",
    "Fraction of edges part of the heavy partitions for the local CSR partitioning", LogicalType::DOUBLE, Value(0.75));
}

void CorePGQOptions::RegisterPathFindingBottomUpThreshold(
    DatabaseInstance &db) {
  auto &config = DBConfig::GetConfig(db);

  config.AddExtensionOption("experimental_path_finding_operator_bottom_up_threshold",
    "Threshold for the frontier size to trigger the bottom up exploration", LogicalType::DOUBLE, Value(0.1));
}

void CorePGQOptions::RegisterPathFindingEnableBottomUpSearch(
    DatabaseInstance &db) {
  auto &config = DBConfig::GetConfig(db);

  config.AddExtensionOption("experimental_path_finding_operator_enable_bottom_up",
    "Enable or disable bottom-up search", LogicalType::BOOLEAN, Value(true));
}

} // namespace core
} // namespace duckpgq

