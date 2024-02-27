#pragma once

#include "duckdb/common/compressed_sparse_row.hpp"
#include "duckdb/parser/column_list.hpp"
#include "duckdb/parser/parsed_data/create_property_graph_info.hpp"
#include "duckdb/parser/parsed_expression.hpp"
#include "duckdb/parser/query_node.hpp"
#include "duckdb/parser/simplified_token.hpp"
#include "duckdb/parser/sql_statement.hpp"
#include "duckdb/planner/operator/logical_comparison_join.hpp"
#include "duckpgq/common.hpp"
#include "duckpgq/operators/logical_path_finding_operator.hpp"

namespace duckdb {

class DuckpgqExtension : public Extension {
public:
  void Load(DuckDB &db) override;
  std::string Name() override;
};

class DuckpgqOptimizerExtension : public OptimizerExtension {
public:
  DuckpgqOptimizerExtension() {
    optimize_function = DuckpgqOptimizeFunction;
  }

  static bool HasBetweenExpression(LogicalOperator &op) {
//    if (op.type == LogicalOperatorType::LOGICAL_COMPARISON_JOIN) {
//      auto &get = op.Cast<LogicalComparisonJoin>();
//      for (auto &condition : get.conditions) {
//        if (condition.comparison == ExpressionType::COMPARE_GREATERTHANOREQUALTO) {
//          unique_ptr<LogicalPathFindingOperator> path_finding_operator = make_uniq<LogicalPathFindingOperator>(op.children);
//          op.children.push_back(path_finding_operator);
//          return true;
//        }
//      }
//    }
    for (auto &child : op.children) {
      if (child->type == LogicalOperatorType::LOGICAL_COMPARISON_JOIN) {
        auto &get = child->Cast<LogicalComparisonJoin>();
        auto path_finding_operator =
            make_uniq<LogicalPathFindingOperator>(get.children);
        op.children.clear();
        op.children.push_back(std::move(path_finding_operator));
        return true;
      }
    }
    for (auto &child : op.children) {
      if (HasBetweenExpression(*child)) {
        return true;
      }
    }
    return false;
  }

  static void DuckpgqOptimizeFunction(ClientContext &context, OptimizerExtensionInfo *info,
                                     duckdb::unique_ptr<LogicalOperator> &plan) {
    if (!HasBetweenExpression(*plan)) {
      return;
    }
    std::cout << "Between expression found";
//    plan = make_uniq<LogicalPathFindingOperator>(plan->children);

  }
};

struct DuckPGQParserExtensionInfo : public ParserExtensionInfo {
public:
  DuckPGQParserExtensionInfo() : ParserExtensionInfo(){};
  ~DuckPGQParserExtensionInfo() override = default;
};

BoundStatement duckpgq_bind(ClientContext &context, Binder &binder,
                            OperatorExtensionInfo *info,
                            SQLStatement &statement);

struct DuckPGQOperatorExtension : public OperatorExtension {
  DuckPGQOperatorExtension() : OperatorExtension() { Bind = duckpgq_bind; }

  std::string GetName() override { return "duckpgq_bind"; }

  unique_ptr<LogicalExtensionOperator>
  Deserialize(Deserializer &deserializer) override {
    throw InternalException("DuckPGQ operator should not be serialized");
  }
};

ParserExtensionParseResult duckpgq_parse(ParserExtensionInfo *info,
                                         const std::string &query);

ParserExtensionPlanResult duckpgq_plan(ParserExtensionInfo *info,
                                       ClientContext &,
                                       unique_ptr<ParserExtensionParseData>);

ParserExtensionPlanResult
duckpgq_handle_statement(unique_ptr<SQLStatement> &statement);

struct DuckPGQParserExtension : public ParserExtension {
  DuckPGQParserExtension() : ParserExtension() {
    parse_function = duckpgq_parse;
    plan_function = duckpgq_plan;
    parser_info = make_shared<DuckPGQParserExtensionInfo>();
  }
};

struct DuckPGQParseData : ParserExtensionParseData {
  unique_ptr<SQLStatement> statement;

  unique_ptr<ParserExtensionParseData> Copy() const override {
    return make_uniq_base<ParserExtensionParseData, DuckPGQParseData>(
        statement->Copy());
  }

  explicit DuckPGQParseData(unique_ptr<SQLStatement> statement)
      : statement(std::move(statement)) {}
};

class DuckPGQState : public ClientContextState {
public:
  explicit DuckPGQState(unique_ptr<ParserExtensionParseData> parse_data)
      : parse_data(std::move(parse_data)) {}

  void QueryEnd() override {
    parse_data.reset();
    for (const auto &csr_id : csr_to_delete) {
      csr_list.erase(csr_id);
    }
  }

  CreatePropertyGraphInfo *GetPropertyGraph(const string &pg_name) {
    auto pg_table_entry = registered_property_graphs.find(pg_name);
    if (pg_table_entry == registered_property_graphs.end()) {
      throw BinderException("Property graph %s does not exist", pg_name);
    }
    return reinterpret_cast<CreatePropertyGraphInfo *>(
        pg_table_entry->second.get());
  }

  CSR *GetCSR(int32_t id) {
    auto csr_entry = csr_list.find(id);
    if (csr_entry == csr_list.end()) {
      throw ConstraintException("CSR not found with ID %d", id);
    }
    return csr_entry->second.get();
  }

public:
  unique_ptr<ParserExtensionParseData> parse_data;

  unique_ptr<ParsedExpression> transform_expression;

  //! Property graphs that are registered
  std::unordered_map<string, unique_ptr<CreateInfo>> registered_property_graphs;

  //! Used to build the CSR data structures required for path-finding queries
  std::unordered_map<int32_t, unique_ptr<CSR>> csr_list;
  std::mutex csr_lock;
  std::unordered_set<int32_t> csr_to_delete;
};

} // namespace duckdb
