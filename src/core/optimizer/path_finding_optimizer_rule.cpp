#include "duckpgq/core/optimizer/path_finding_optimizer_rule.hpp"

#include "duckpgq/core/optimizer/duckpgq_optimizer.hpp"
#include <duckdb/catalog/catalog_entry/duck_table_entry.hpp>
#include <duckdb/planner/expression/bound_cast_expression.hpp>
#include <duckdb/planner/expression/bound_function_expression.hpp>
#include <duckdb/planner/operator/logical_aggregate.hpp>
#include <duckdb/planner/operator/logical_comparison_join.hpp>
#include <duckdb/planner/operator/logical_cross_product.hpp>
#include <duckdb/planner/operator/logical_filter.hpp>
#include <duckdb/planner/operator/logical_get.hpp>
#include <duckdb/planner/operator/logical_projection.hpp>
#include <duckpgq/core/functions/function_data/shortest_path_operator_function_data.hpp>
#include <duckpgq/core/operator/logical_path_finding_operator.hpp>
#include <duckpgq/core/option/duckpgq_option.hpp>

namespace duckpgq {
namespace core {

// Helper function to create the required BoundColumnRefExpression
unique_ptr<Expression> CreateReplacementExpression(const string &alias, const string &functionName, idx_t tableIndex, idx_t position) {
  if (functionName == "iterativelengthoperator") {
    return make_uniq<BoundColumnRefExpression>(alias, LogicalType::BIGINT, ColumnBinding(tableIndex, position));
  }
  if (functionName == "shortestpathoperator") {
    return make_uniq<BoundColumnRefExpression>(alias, LogicalType::LIST(LogicalType::BIGINT), ColumnBinding(tableIndex, position));
  }
  return nullptr;
}

void ReplaceExpressions(LogicalProjection &op, unique_ptr<Expression> &function_expression, string &mode, vector<idx_t> &offsets) {
    // Create a temporary vector to hold the new expressions
    vector<unique_ptr<Expression>> new_expressions;
    new_expressions.reserve(op.expressions.size());  // Reserve space to avoid multiple reallocations

    for (size_t offset = 0; offset < op.expressions.size(); ++offset) {
        const auto &expr = op.expressions[offset];
        if (expr->expression_class != ExpressionClass::BOUND_FUNCTION) {
            // Directly transfer the expression to the new vector if no replacement is needed
            new_expressions.push_back(std::move(op.expressions[offset]));
            continue;
        }

        auto &bound_function_expression = expr->Cast<BoundFunctionExpression>();
        const auto &function_name = bound_function_expression.function.name;
        if (function_name == "iterativelengthoperator" || function_name == "shortestpathoperator") {
            // Create the replacement expression
            auto replacement_expr = CreateReplacementExpression(expr->alias, function_name, op.table_index, offset);
            if (replacement_expr) {
                // store the offsets of the expressions that need to be replaced
                offsets.push_back(offset);
                // Push the replacement into the new vector
                new_expressions.push_back(std::move(replacement_expr));
                // Optionally, copy the original expression if it's needed elsewhere
                function_expression = expr->Copy();
                mode = function_name == "iterativelengthoperator" ? "iterativelength" : "shortestpath";
            } else {
                // If no replacement is created, throw an internal exception
                throw InternalException("Found a bound path-finding function that should be replaced but could not be replaced.");
            }
        } else {
            // If the expression is a bound function but not one we are interested in replacing
            new_expressions.push_back(std::move(op.expressions[offset]));
        }
    }

    // Replace the old expressions vector with the new one
    op.expressions = std::move(new_expressions);
}

unique_ptr<LogicalPathFindingOperator> DuckpgqOptimizerExtension::FindCSRAndPairs(
    unique_ptr<LogicalOperator>& first_child,
    unique_ptr<LogicalOperator>& second_child,
    LogicalProjection& op_proj,
    ClientContext &context) {
  bool csr_found = false;
  bool reverse_csr_found = false;
  vector<unique_ptr<Expression>> path_finding_expressions;
  vector<unique_ptr<LogicalOperator>> path_finding_children;
  LogicalProjection *csr_projection = nullptr;
  LogicalProjection *reverse_csr_projection = nullptr;
  if (first_child->type == LogicalOperatorType::LOGICAL_CROSS_PRODUCT) {
    auto &potential_csr_cross = first_child->Cast<LogicalCrossProduct>();
    for (const auto &cross_child : potential_csr_cross.children) {
      auto &proj = cross_child->Cast<LogicalProjection>();
      for (const auto &expr : proj.expressions) {
        if (expr->type != ExpressionType::BOUND_COLUMN_REF) {
          continue;
        }
        auto &bound_col_ref_expr = expr->Cast<BoundColumnRefExpression>();
        if (bound_col_ref_expr.GetName() == "csr_id") {
          csr_found = true;
          csr_projection = &proj;
          break;
        } if (bound_col_ref_expr.GetName() == "reverse_csr_id") {
          reverse_csr_found = true;
          reverse_csr_projection = &proj;
          break;
        }
      }
    }

  }

  if (csr_found && reverse_csr_found) {
    if (csr_projection == nullptr || reverse_csr_projection == nullptr) {
      throw InternalException("Found CSR & Reverse CSR but one of their nodes was not found.");
    }
    path_finding_children.push_back(std::move(second_child));
    path_finding_children.push_back(csr_projection->Copy(context));
    path_finding_children.push_back(reverse_csr_projection->Copy(context));
    if (path_finding_children.size() != 3) {
      throw InternalException("Path-finding operator should have 3 children");
    }
    unique_ptr<Expression> function_expression;
    string path_finding_mode;
    vector<idx_t> offsets;
    ReplaceExpressions(op_proj, function_expression, path_finding_mode, offsets);
    return make_uniq<LogicalPathFindingOperator>(
      path_finding_children, path_finding_expressions, path_finding_mode, op_proj.table_index, offsets);
  }
  // Didn't find the CSR
  return nullptr;
}

bool DuckpgqOptimizerExtension::InsertPathFindingOperator(
    LogicalOperator &op, ClientContext &context) {
  unique_ptr<Expression> function_expression;
  string mode;
  vector<idx_t> offsets;
  if (op.type != LogicalOperatorType::LOGICAL_PROJECTION) {
    for (auto &child : op.children) {
      if (InsertPathFindingOperator(*child, context)) {
        return true;
      }
    }
    return false;
  }
  auto &op_proj = op.Cast<LogicalProjection>();

  for (const auto &child : op_proj.children) {
    vector<unique_ptr<LogicalOperator>> path_finding_children;
    if (child->type != LogicalOperatorType::LOGICAL_CROSS_PRODUCT) {
      continue;
    }
    auto &get_join = child->Cast<LogicalCrossProduct>();
    //! For now we assume this is enough to detect we have found a
    //! path-finding query. Should be improved in the future
    if (get_join.children.size() != 2) {
      continue;
    }

    auto &left_child = get_join.children[0];
    auto &right_child = get_join.children[1];
    auto path_finding_operator = FindCSRAndPairs(left_child, right_child, op_proj, context);
    if (path_finding_operator == nullptr) {
      path_finding_operator = FindCSRAndPairs(right_child, left_child, op_proj, context);
    }
    if (path_finding_operator != nullptr) {
      op.children.clear();
      op.children.push_back(std::move(path_finding_operator));
      return true;
    }

    return false; // No path-finding operator found
  }
  for (auto &child : op.children) {
    if (InsertPathFindingOperator(*child, context)) {
      return true;
    }
  }
  return false;
}

void DuckpgqOptimizerExtension::DuckpgqOptimizeFunction(
    OptimizerExtensionInput &input, unique_ptr<LogicalOperator> &plan) {
  if (!GetPathFindingOption(input.context)) {
    return;
  }
  InsertPathFindingOperator(*plan, input.context);
}

//------------------------------------------------------------------------------
// Register optimizer
//------------------------------------------------------------------------------
void CorePGQOptimizer::RegisterPathFindingOptimizerRule(DatabaseInstance &db) {
  auto &config = DBConfig::GetConfig(db);

  config.optimizer_extensions.push_back(DuckpgqOptimizerExtension());
}

} // namespace core
} // namespace duckpgq