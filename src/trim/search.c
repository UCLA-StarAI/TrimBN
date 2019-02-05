#include <float.h>
#include <math.h>
#include <string.h>
#include "sddapi.h"
#include "compiler.h"
#include "search.h"

// forward references
char* ppc(SddSize n); // pretty print
SddNode* fnf_to_sdd(Fnf* fnf, SddManager* manager);
SddNode* sdd_move_feature_to_pos(
  SddNode* node, SddManager* manager, SddLiteral* const feature_vars,
  const size_t num_vars, const int rl_pos, const int no_check);

// Helper function: update constrained node positions, assuming that the
// Y-constrained node is y'th, and XY-constrained node is xy'th node in
// the right-most path
void update_constrained_positions(
    const Vtree* vtree, const int y, const int xy,
    SddLiteral* y_vtree, SddLiteral* xy_vtree) {
  if (y == 0) { // update Y-constrained vtree position
    *y_vtree = sdd_vtree_position(vtree);
  } 
  if (xy == 0) { // update XY-constrained vtree position
    *xy_vtree = sdd_vtree_position(vtree);
    return;
  }
  update_constrained_positions(sdd_vtree_right(vtree), y-1, xy-1, y_vtree, xy_vtree);
}

void print_set(char* set, int n) {
  for (int i = 0; i < n; i++) {
    if (set[i] == 1) printf("%d,",i);
  }
}

static inline
int cmp_by_bound_inc(const void* n1, const void* n2) {
  const SddWmc s1 = (*((const SddWmc**) n1))[1];
  const SddWmc s2 = (*((const SddWmc**) n2))[1];
  SddWmc diff = s1 - s2;
  if (fabs(diff) < DBL_EPSILON) return 0;
  return (diff > 0) ? 1 : -1;
}

void compute_maa_with_sdd(SearchData* data, SearchResult* result, SddNode** node, SddManager* manager,
  char* subset, int feature_to_remove, int y_size, SddWmc* maa, SddWmc* bound, int move_only) {
  // Move feature_to_remove to left child of Y-constrained node
  Feature* feature = data->features[feature_to_remove];
  *node = sdd_move_feature_to_pos(*node, manager, feature->indicators,
                                  feature->num_indicators, y_size+1, 0); // need to move to y_size+1 since moving down

  Vtree* vtree = sdd_manager_vtree(manager);
  SddLiteral y_vtree, xy_vtree;   
  update_constrained_positions(vtree, y_size, data->num_features, &y_vtree, &xy_vtree);

  if (move_only) return;

  // Compute E-SDP
  EsdpManager* e_manager = esdp_manager_new(*node, manager, data->literal_weights);  
  *bound = compute_mpa(e_manager, data->decision, data->threshold, xy_vtree, y_vtree, maa);

  esdp_manager_free(e_manager);
}

// n choose m
void bnb_search_aux(SearchData* data, SearchResult* result, SddNode** node, SddManager* manager,
    int cur_level, char* cur_subset, char* avail_features, int num_avail, int is_nb) {
  int soft_depth = data->num_features - (int)data->budget;
  int hard_depth = (is_nb == 1) ? soft_depth : data->num_features - 1;
  if (!is_nb && cur_level >= soft_depth) soft_depth = cur_level + 1;

  if (cur_level >= hard_depth || num_avail <= 0) return;

  // sort available features in ascending order of MPA
  SddWmc** sorted_avail = (SddWmc**) malloc(num_avail * sizeof(SddWmc*));
  SddWmc bound, maa;
  int j = 0;
  for (int i = 0; i < data->num_features; i++) {
    if (avail_features[i] == 1) {
      sorted_avail[j] = (SddWmc*) malloc(2 * sizeof(SddWmc));
      sorted_avail[j][0] = i; // feature id
      cur_subset[i] = 0;
      compute_maa_with_sdd(data,result,node,manager,cur_subset,i,data->num_features-cur_level-1,NULL,&bound,0);
      sorted_avail[j][1] = bound;
      cur_subset[i] = 1;
      j++;
    }
  }
  qsort((void*)sorted_avail, num_avail-1, sizeof(SddWmc*), cmp_by_bound_inc);

  // num_successors = num_avail - (n - m - k - 1)
  // choose the first num_successors features among them (let these Qk)
  // Remove Qk from avail; remove num_successors from num_avail
  int num_successors = num_avail - (soft_depth - cur_level - 1);
  for (int i = 0; i < num_successors; i++) {
    avail_features[(int)sorted_avail[i][0]] = 0;
  }

  // From right-most descendant node (qk) to left:
  char* new_subset = (char*) malloc(data->num_features * sizeof(char));
  for (int i = num_successors-1; i >= 0; i--) {
    int feature_to_remove = (int) sorted_avail[i][0];
    SddWmc maa_decrease = sorted_avail[i][1];
    new_subset = (char*) memcpy(new_subset, cur_subset, data->num_features);
    new_subset[feature_to_remove] = 0;

    if (num_successors == 1 && cur_level+1 < soft_depth) {
      // move cur feature inside Y-constrained node in vtree
      compute_maa_with_sdd(data, result, node, manager, new_subset, feature_to_remove, data->num_features-cur_level-1, NULL, NULL, 1);
      bnb_search_aux(data, result, node, manager, cur_level+1, new_subset, avail_features, num_avail-i-1, is_nb);
    } else {
      if (is_nb) {
        maa = bound = maa_decrease;
        compute_maa_with_sdd(data, result, node, manager, new_subset, feature_to_remove, data->num_features-cur_level-1, NULL, NULL, 1);
        if (bound - result->best_score >= DBL_EPSILON) { // Prune subtree if bound < best_esdp
          if (cur_level + 1 == soft_depth) { // Leaf node. Update best ESDP
            if (maa > result->best_score) {
              result->best_score = maa;
              result->cost = data->num_features - cur_level - 1;
              if (result->best_subset == NULL) result->best_subset = (char*) malloc(data->num_features * sizeof(char));
              for (int j = 0; j < data->num_features; j++) result->best_subset[j] = new_subset[j];
            }
          } else { // Internal node. Search subtree
            bnb_search_aux(data, result, node, manager, cur_level+1, new_subset, avail_features, num_avail-i-1, is_nb);
          }
        }
      } else if (cur_level+1 < soft_depth) { // general network.
        bound = maa_decrease;
        compute_maa_with_sdd(data, result, node, manager, new_subset, feature_to_remove, data->num_features-cur_level-1, NULL, &bound, 1);
        if (bound - result->best_score >= DBL_EPSILON) { // Prune subtree if bound < best_esdp
          bnb_search_aux(data, result, node, manager, cur_level+1, new_subset, avail_features, num_avail-i-1, is_nb);
        }
      } else { // general network. need to compute actual eca
        bound = maa_decrease;
        if (bound - result->best_score >= DBL_EPSILON) { // Prune subtree if bound < best_esdp
          compute_maa_with_sdd(data, result, node, manager, new_subset, feature_to_remove, data->num_features-cur_level-1, &maa, &bound, 0);
          if (maa > result->best_score) {
            result->best_score = maa;
            result->cost = data->num_features - cur_level - 1;
            if (result->best_subset == NULL) result->best_subset = (char*) malloc(data->num_features * sizeof(char));
            for (int j = 0; j < data->num_features; j++) result->best_subset[j] = new_subset[j];            
          }
          bnb_search_aux(data, result, node, manager, cur_level+1, new_subset, avail_features, num_avail-i-1, is_nb);          
        }
      }
    }

    avail_features[feature_to_remove] = 1;
  }
  free(sorted_avail);
  free(new_subset);  
  // All descendants have been tested. Return
}

// Helper function: recursively search for an optimal feature subset by E-SDP
// Invariant: subset at the termination of this function should look the same
// as what was passed into this function call.
void search_best_subset_aux(SddNode** node, SddManager* manager,
    SearchData* data, SearchResult* result, int cur_depth,
    char* subset, int num_included, float cur_cost) {
  // Backtrack if budget exceeded
  if (cur_cost >= data->budget || cur_depth >= data->num_features) {
    return;
  }

  SddLiteral y_vtree, xy_vtree;   
  if (result->best_score > 0) {
    // compute MPA, with size of Y being number of included and unassigned features
    EsdpManager* e_manager = esdp_manager_new(*node, manager, data->literal_weights);
    Vtree* vtree = sdd_manager_vtree(manager);
    update_constrained_positions(
        vtree, data->num_features-cur_depth+num_included, data->num_features, &y_vtree, &xy_vtree);
    SddWmc bound = compute_mpa(e_manager, data->decision, data->threshold, xy_vtree, y_vtree, NULL);
    if (bound < result->best_score) {
      return;
    }
  }

  Feature* feature = data->features[cur_depth];
  if (cur_cost + data->costs[cur_depth] <= data->budget) {
    subset[cur_depth] = 1;

    // Move vtree variables so that features appear in right order
    *node = sdd_move_feature_to_pos(*node, manager, feature->indicators,
                                    feature->num_indicators, num_included, 0);

    // Update constrained positions for Y to include cur feature
    Vtree* vtree = sdd_manager_vtree(manager);
    update_constrained_positions(
        vtree, num_included+1, data->num_features, &y_vtree, &xy_vtree);

    // Compute agreement score
    EsdpManager* e_manager =
        esdp_manager_new(*node, manager, data->literal_weights);
    SddWmc maa = 0;
    SddWmc mpa = compute_mpa(e_manager, data->decision, data->threshold, xy_vtree, y_vtree, &maa);

    // Update the current best subset. Tie-break by cost
    if (maa > result->best_score || (maa == result->best_score
        && cur_cost + data->costs[cur_depth] < result->cost)) {
      update_search_result(result, maa, cur_cost+data->costs[cur_depth],
                           subset, data->num_features);
    }
    if (e_manager != NULL) esdp_manager_free(e_manager);

    search_best_subset_aux(node, manager, data, result, cur_depth+1, subset,
                           num_included+1, cur_cost + data->costs[cur_depth]);
    subset[cur_depth] = 0;
  }

  // move next_feature to (num included+unassigned feature) pos in vtree
  *node = sdd_move_feature_to_pos(*node, manager, feature->indicators, feature->num_indicators,
                                  data->num_features-cur_depth+num_included, 0);

  // recursive run with next_feature excluded
  search_best_subset_aux(node, manager, data, result, cur_depth+1, subset,
                         num_included, cur_cost);
}

// Search optimal feature subset by E-SDP
//  - Runs inclusion/exclusion search on features
//  - First compiles an unconstrained SDD and makes it constrained by moving
//    feature variables to the top of SDD, with limited vtree minimization.
SearchResult* search_best_subset(SearchData* data, Fnf* fnf,
      SddCompilerOptions* options) {
  // Compile an unconstrained SDD
  printf("\ncreating manager..."); fflush(stdout);
  SddManager* manager = sdd_manager_create(fnf->var_count,0);
  sdd_manager_set_options(options,manager);
  printf("\ncompiling..."); fflush(stdout);
  SddNode* node = fnf_to_sdd(fnf,manager);
  char* s;  
  printf("\n sdd size               : %s \n",s=ppc(sdd_size(node))); free(s);
  printf(" sdd node count         : %s \n",s=ppc(sdd_count(node))); free(s);
  if(options->minimize_cardinality) {
    printf("\nminimizing cardinality...");
    node = sdd_minimize_cardinality(node,manager);
    printf("size = %zu / node count = %zu\n",sdd_size(node),sdd_count(node));
  }
  sdd_manager_auto_gc_and_minimize_off(manager);
  sdd_ref(node,manager);

  // Move feature variables to make a constrained SDD
  Feature* feature;
  Vtree* vtree = sdd_manager_vtree(manager);
  for (int i = data->num_features-1; i >= 0; i--) {
    feature = data->features[i];
    node = sdd_move_feature_to_pos(node, manager, feature->indicators,
                                   feature->num_indicators, 0, 1);
    // Minimize XY-constrained vtree node so far
    vtree = sdd_manager_vtree(manager);
    for (int j = 0; j < data->num_features - i; j++) {
      vtree = sdd_vtree_right(vtree);
    } // vtree now points to XY-constrained node
    sdd_ref(node,manager);
    sdd_vtree_minimize_limited(vtree,manager);
    sdd_deref(node,manager);
  }
  sdd_deref(node,manager);
  printf(" sdd size           : %s \n", s=ppc(sdd_size(node))); free(s);
  printf(" sdd node count     : %s \n", s=ppc(sdd_count(node))); free(s);

  // Search for an optimal subset using recursive helper func
  sdd_ref(node, manager);

  SearchResult* result = new_search_result(data->num_features);
  char* subset = (char*) calloc(data->num_features, sizeof(char));
  search_best_subset_aux(&node, manager, data, result, 0, subset, 0, 0);
  free(subset);

  sdd_deref(node, manager);

  sdd_manager_free(manager);
  return result;
}
