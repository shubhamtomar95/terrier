#pragma once

#include <memory>
#include <string>
#include <tuple>
#include <unordered_map>
#include <vector>

#include "catalog/catalog.h"
#include "catalog/catalog_defs.h"

namespace terrier {

namespace parser {
class TableRef;
}

namespace transaction {
class TransactionContext;
}

namespace catalog {
class Schema;
}

namespace binder {

/**
 * @brief Store the visible table alias and the corresponding <db_id, table_id>
 *  tuple. Also record the upper level context when traversing into the nested
 *  query. This context keep track of all the table alias that the column in the
 *  current level can bind to.
 */
class BinderContext {
 public:
  explicit BinderContext(std::shared_ptr<BinderContext> upper_context = nullptr) : upper_context_(upper_context) {
    if (upper_context != nullptr) depth_ = upper_context->depth_ + 1;
  }

  /**
   * @brief Update the table alias map given a table reference (in the from
   * clause)
   */
  void AddRegularTable(catalog::CatalogAccessor *accessor, parser::TableRef *table_ref);

  /**
   * @brief Update the table alias map given a table reference (in the from
   * clause)
   */
  void AddRegularTable(catalog::CatalogAccessor *accessor, const std::string &db_name, const std::string &table_name,
                       const std::string &table_alias);

  /**
   * @brief Update the nested table alias map
   */
  void AddNestedTable(const std::string &table_alias,
                      const std::vector<std::shared_ptr<parser::AbstractExpression>> &select_list);

  /**
   * @brief Check if the current context has any table
   */
  static bool HasTables(const std::shared_ptr<BinderContext> &current_context) {
    if (current_context == nullptr) return false;
    return (!current_context->regular_table_alias_map_.empty() || !current_context->nested_table_alias_map_.empty());
  }

  static bool ColumnInSchema(const catalog::Schema &schema, const std::string &col_name);

  static void GetColumnPosTuple(const std::string &col_name,
                                std::tuple<catalog::db_oid_t, catalog::table_oid_t, catalog::Schema> tuple,
                                parser::ColumnValueExpression *expr);

  static bool GetColumnPosTuple(std::shared_ptr<BinderContext> current_context, parser::ColumnValueExpression *expr);

  /**
   * @brief Construct the column position tuple given column name and the
   *  corresponding table obj. Also set the value type
   *  Note that this is just a helper function and it is independent of
   *  the context.
   *
   * @param col_name The column name
   * @param schema the table
   * @param col_pos_tuple The tuple of <db id, table id, column id>
   * @param value_type The value type of the column
   *
   * @return If the col_pos_tuple is retrieved successfully, return true,
   *  otherwise return false
   */
  //  static bool GetColumnPosTuple(const std::string &col_name, std::shared_ptr<catalog::Schema> schema,
  //      std::tuple<catalog::db_oid_t, catalog::table_oid_t, catalog::col_oid_t> &col_pos_tuple, type::TypeId
  //      &value_type);

  /**
   * @brief Construct the column position tuple given only the column name and
   *  the context. Also set the value type based on column type
   *  This function is used when the table alias is absent in the
   *  TupleValueExpression
   *
   * @param current_context The current context
   * @param col_name The current column name
   * @param col_pos_tuple the tuple of <db id, table id, column id>
   * @param table_alias The table alias
   * @param value_type The value type of the column
   * @param depth The depth of the context we find this column
   *
   * @return If the col_pos_tuple is retrieved successfully return true,
   * otherwise return false
   */
  //  static bool GetColumnPosTuple(std::shared_ptr<BinderContext> current_context,
  //                                const std::string &col_name,
  //                                std::tuple<catalog::db_oid_t, catalog::table_oid_t, catalog::col_oid_t>
  //                                &col_pos_tuple, std::string &table_alias, type::TypeId &value_type, int &depth);

  /**
   * @brief Construct the table obj given the table alias
   *
   * @param current_context The current context
   * @param alias The table alias
   * @param schema The retrieved table object
   * @param depth The depth of the context that found the table
   *
   * @return Return true on success, false otherwise
   */
  static bool GetRegularTableObj(std::shared_ptr<BinderContext> current_context, const std::string &alias,
                                 parser::ColumnValueExpression *expr,
                                 std::tuple<catalog::db_oid_t, catalog::table_oid_t, catalog::Schema> &tuple);

  static bool CheckNestedTableColumn(std::shared_ptr<BinderContext> current_context, const std::string &alias,
                                     std::string &col_name, parser::ColumnValueExpression *expr);

  std::shared_ptr<BinderContext> GetUpperContext() { return upper_context_; }

  void SetUpperContext(std::shared_ptr<BinderContext> upper_context) { upper_context_ = upper_context; }

  void inline SetDepth(int depth) { depth_ = depth; }

  int inline GetDepth() { return depth_; }

  void GenerateAllColumnExpressions(std::vector<std::shared_ptr<parser::AbstractExpression>> &exprs);

 private:
  /** @brief Map table alias to table obj */

  std::unordered_map<std::string, std::tuple<catalog::db_oid_t, catalog::table_oid_t, catalog::Schema>>
      regular_table_alias_map_;
  std::unordered_map<std::string, std::unordered_map<std::string, type::TypeId>> nested_table_alias_map_;
  std::shared_ptr<BinderContext> upper_context_;
  int depth_ = 0;
};

}  // namespace binder
}  // namespace terrier
