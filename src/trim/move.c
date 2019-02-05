#include "sddapi.h"

void move_var_in_vtree(SddLiteral var, char var_location, Vtree* new_sibling, SddManager* manager);

// Helper function: check if feature variables appear in the left vtree child
int is_feature_var_in_vtree(SddLiteral* const feature_vars,
    const size_t num_vars, Vtree* vtree) {
  if (sdd_vtree_is_leaf(vtree)) {
    SddLiteral var = sdd_vtree_var(vtree);
    for (int i = 0; i < num_vars; i++) { // enough to check 1 feature var
      if (feature_vars[i] == var) return 1;
    }
    return 0;
  }
  return is_feature_var_in_vtree(feature_vars, num_vars, sdd_vtree_left(vtree));
}

// Helper function: move a variable to the right of new_sibling vtree node (or
// to the left if to_left is set to 1).
// Note: node gets garbage collected
SddNode* sdd_move_var_to_pos(SddLiteral var, int to_left, Vtree* new_sibling,
    SddNode* node, SddManager* manager) {
  if (to_left) {
    Vtree* left = sdd_vtree_left(new_sibling);
    if (left != NULL && sdd_vtree_var(left) == var) {
      return node;
    }
  } else {
    Vtree* right = sdd_vtree_right(new_sibling);
    if (right != NULL && sdd_vtree_var(right) == var) {
      return node;
    }
  }

  // Condition the SDD node on positive and negative literals of var.
  // Resulting SDD nodes do not contain var, and now we can move it freely.
  SddNode* node1 = sdd_condition(var, node, manager);
  SddNode* node0 = sdd_condition(-var, node, manager);

  // Garbage collect node
  sdd_ref(node1, manager);
  sdd_ref(node0, manager);
  sdd_deref(node, manager);
  sdd_manager_garbage_collect(manager);

  // Move variable to left/right of new_sibling and join the SDDs
  SddNode* tmp1;
  SddNode* tmp0;
  move_var_in_vtree(var, to_left ? 'l' : 'r', new_sibling, manager);
  node1 = sdd_conjoin(sdd_manager_literal(var,manager), tmp1=node1, manager);
  node0 = sdd_conjoin(sdd_manager_literal(-var,manager), tmp0=node0, manager);
  SddNode* return_node = sdd_disjoin(node1, node0, manager);

  // Garbage collect node1 and node0
  sdd_ref(return_node, manager);
  sdd_deref(tmp1, manager);
  sdd_deref(tmp0, manager);
  sdd_manager_garbage_collect(manager);

  return return_node;
}

// Move indicator variables of a feature in SDD node such that they appear
// in the left child of rl_pos'th right-linear vtree node from the top.
// If no_check is set to 1, perform the operation even if some variables
// appear in the right vtree.
//        root
//     /        \
//   ...  ...(rl_pos-1) nodes...
//         /                 \
//    feature_vars           rest
SddNode* sdd_move_feature_to_pos(SddNode* node, SddManager* manager,
    SddLiteral* const feature_vars, const size_t num_vars, const int rl_pos,
    const int no_check) {
  SddNode* return_node = node;

  // new_sibling should point to rl_pos'th node in the rightmost path
  Vtree* new_sibling = sdd_vtree_of(node);  
  for (int i = 0; i < rl_pos; i++) new_sibling = sdd_vtree_right(new_sibling);

  // Return if already in position
  if (!no_check && is_feature_var_in_vtree(feature_vars, num_vars,
                                           sdd_vtree_left(new_sibling))) {
    return return_node;
  }

  for (int i = 0; i < num_vars; i++) {
    // Move the first indicator to the left of new_sibling, and the rest to
    // the right of new_sibling
    return_node = sdd_move_var_to_pos(feature_vars[i], (i==0), new_sibling,
                                      return_node, manager);
    new_sibling = sdd_vtree_of(sdd_manager_literal(feature_vars[i],manager));
  }

  return return_node;
}
