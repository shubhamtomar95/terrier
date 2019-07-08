#include "common/hash_util.h"
#include "optimizer/group_expression.h"
#include "optimizer/group.h"
#include "optimizer/rule.h"

namespace terrier::optimizer {

void GroupExpression::SetLocalHashTable(PropertySet* output_properties,
                                        std::vector<PropertySet*> input_properties_list,
                                        double cost) {
  auto it = lowest_cost_table_.find(output_properties);
  if (it == lowest_cost_table_.end()) {
    // No other cost to compare against
    lowest_cost_table_.insert(std::make_pair(output_properties, std::make_tuple(cost, input_properties_list)));
  } else {
    // Only insert if the cost is lower than the existing cost
    std::vector<PropertySet *> &pending_deletion = input_properties_list;
    if (std::get<0>(it->second) > cost) {
      pending_deletion = std::get<1>(it->second);

      // Insert
      lowest_cost_table_[output_properties] = std::make_tuple(cost, input_properties_list);
    }

    // Cleanup any memory allocations by contract
    delete output_properties;
    for (auto prop : pending_deletion) { delete prop; }
  }
}

common::hash_t GroupExpression::Hash() const {
  size_t hash = op.Hash();

  for (size_t i = 0; i < child_groups.size(); ++i) {
    size_t child_hash = common::HashUtil::Hash<GroupID>(child_groups[i]);
    hash = common::HashUtil::CombineHashes(hash, child_hash);
  }

  return hash;
}

} // namespace terrier::optimizer
