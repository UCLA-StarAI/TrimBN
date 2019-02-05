#ifndef SEARCH_H_
#define SEARCH_H_

#include <stdio.h>
#include <stdlib.h>
#include "sddapi.h"

typedef struct {
  SddLiteral* indicators;
  SddSize num_indicators;
} Feature;

typedef struct {
  SddSize var_count;        // Number of CNF variables
  SddWmc* literal_weights;  // Weights of literals in the CNF encoding

  SddSize node_count;       // Number of network nodes
  SddLiteral decision;      // Decision literal
  SddSize num_features;     // Total number of features
  Feature** features;       // Candidate features

  SddWmc threshold;         // Decision threshold
  float budget;             // Budget for feature subset selection
  float* costs;             // Costs associated with features
} SearchData;

typedef struct{
  SddWmc best_score;
  char* best_subset;
  float cost;
} SearchResult;

/****************************************************************************************
 * forward references 
 ****************************************************************************************/

SearchData* read_search_data(const char* lmap_filename, const char* input_filename);
void free_search_data(SearchData* data);
void print_search_data(SearchData* data);

SearchResult* new_search_result(const SddSize num_features);
void free_search_result(SearchResult* result);
void update_search_result(SearchResult* result, const SddWmc new_score,
                          const float new_cost, const char* new_subset,
                          const SddSize num_features);

#endif // SEARCH_H_
