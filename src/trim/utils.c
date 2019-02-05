#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sddapi.h"
#include "search.h"

// Helper function: find a string in a sorted array of strings.
// Return the index if string is in the array, and return -1 otherwise.
int bsearch_locate(char** sorted_arr, const int n, const char* str) {
  int mid = n / 2;
  int cmp = strcmp(str, sorted_arr[mid]);
  
  if (n == 0 || (n == 1 && cmp != 0)) {
    return -1;
  }

  if (cmp == 0) {
    return mid;
  } else if (cmp < 0) {
    return bsearch_locate(sorted_arr, mid, str);
  } else {
    return mid + bsearch_locate(sorted_arr+mid, n-mid, str);
  }
}

// Parse the literal map file produced by ACE while encoding a Bayes net
// Return (via pointers) the following values:
//  - var_count: the number of CNF variables
//  - node_count: the number of BN nodes
//  - sorted_node_names: BN node names in alphabetical order
//  - sorted_node_indicators: a list of indicator variables for each BN node
//  - sorted_node_num_indicators: the number of possible values (= number of
//    indicator variables) for each BN node
//  - weights: the weight of each CNF literal. Weight for L is in weights[L-1]
// Assumes that lmap lists nodes in alphabetical order
void parse_lmap(const char* filename, size_t* var_count, size_t* node_count,
    char*** sorted_node_names, SddLiteral*** sorted_node_indicators, 
    SddSize** sorted_node_num_indicators, SddWmc** weights) {
  FILE* fp = fopen(filename, "rb");
  if (fp == NULL) {
    fprintf(stderr, "Could not open lmap file %s\n", filename);
    exit(1);
  }

  char* line = NULL;
  size_t len = 0;
  ssize_t read;
  int node = 0, var = 0;
  while((read = getline(&line, &len, fp)) != -1) {
    if (read < 5) continue;
    if (line[3] == 'N' && line[4] == '$') {
      // Number of CNF variables specified as: "cc$N$[var_count]"
      *var_count = strtoul(line+5,NULL,10);
      *weights = (SddWmc*) malloc(*var_count * sizeof(SddWmc));
    } else if (line[3] == 'v' && line[4] == '$') {
      // Number of network nodes specified as: "cc$v$[node_count]"
      *node_count = strtoul(line+5,NULL,10);
      *sorted_node_names = (char**) malloc(*node_count * sizeof(char*));
      *sorted_node_indicators =
          (SddLiteral**) malloc(*node_count * sizeof(SddLiteral*));      
      *sorted_node_num_indicators =
          (SddSize*) malloc(*node_count * sizeof(SddSize));
    } else if (line[3] == 'V' && line[4] == '$') {
      // Each BN node specified as: "cc$V$[node_name]$[node_num_indicators]"
      if (*sorted_node_names != NULL && *sorted_node_num_indicators != NULL) {
        (*sorted_node_names)[node] = (char*) calloc(read - 5, sizeof(char));
        int i = 5;
        while (i < read) {
          if (line[i] == '$') {
            (*sorted_node_names)[node][i-5] = '\0';
            i++;
            break;
          }
          (*sorted_node_names)[node][i-5] = line[i];
          i++;
        }
        (*sorted_node_num_indicators)[node] = strtol(line+i,NULL,10);
        (*sorted_node_indicators)[node] =
            (SddLiteral*) calloc((*sorted_node_num_indicators)[node],
                                 sizeof(SddLiteral));
        node++;
      }
    } else if (line[3] == 'I' && line[4] == '$') {
      // Each indicator variable for some value of BN node specified as:
      //  "cc$I$[var_id]$[weight]$+$[node_name]$[value]"
      int start = 5;
      while (line[start++] != '$');
      (*weights)[var++] = strtof(line+start,NULL);
      while (line[start++] != '+'); start++;
      int end = start;
      while (line[end++] != '$'); line[end-1] = '\0';
      int pos = bsearch_locate(*sorted_node_names, *node_count, line+start);
      start = 0;
      // Find the next empty slot for indicator of this node
      while ((*sorted_node_indicators)[pos][start] != 0) start++;
      (*sorted_node_indicators)[pos][start] = strtol(line+5,NULL,10);
    } else if (line[3] == 'C' && line[4] == '$') {
      // Each parameter variable specified as: "cc$C$[var_id]$[weight]$+$"
      if (line[5] == '-') continue; // Ignore negative literals
      int start = 5;
      while (line[start++] != '$');
      (*weights)[var++] = strtof(line+start,NULL);
    }
  }

  free(line);
  fclose(fp);
}

// Parse files with literal map and E-SDP search problem definition
SearchData* read_search_data(const char* lmap_filename, const char* input_filename) {
  FILE* input_fp = fopen(input_filename, "rb");
  if (input_fp == NULL) {
    printf("Could not open input file %s\n", input_filename);
    exit(1);
  }

  size_t var_count, node_count;
  char** sorted_node_names;
  SddLiteral** sorted_node_indicators;
  SddSize* sorted_node_num_indicators;
  SddWmc *weights;
  parse_lmap(lmap_filename, &var_count, &node_count, &sorted_node_names,
             &sorted_node_indicators, &sorted_node_num_indicators, &weights);

  SearchData* data = (SearchData*) malloc(sizeof(SearchData));
  data->var_count = var_count;
  data->node_count = node_count;

  char* line = NULL;
  size_t len = 0;
  ssize_t read;
  int n = 0;
  while((read = getline(&line, &len, input_fp)) != -1) {
    if (read < 3) continue;
    if (line[0] == '$' && line[1] == ' ') {
      // Search metadata specified as: "$ [num_features] [threshold] [budget]"
      data->num_features = strtoul(strtok(line+2," \n"),NULL,10);
      data->threshold = strtof(strtok(NULL," \n"),NULL);
      data->budget = strtof(strtok(NULL," \n"),NULL);
      data->costs = (float*) malloc(data->num_features * sizeof(float));
      data->features = (Feature**) malloc(data->num_features * sizeof(Feature*));      
    } else if (line[0] == 'd' && line[1] == ' ') {
      // Decision variable specified as: "d [decision_node_name]"
      int decision_index =
          bsearch_locate(sorted_node_names, node_count, strtok(line+2," \n"));
      data->decision = sorted_node_indicators[decision_index][0];
    } else if (line[0] == 'f' && line[1] == ' ') {
      // Features are specified as: "f [feature_node_name]"
      int index =
          bsearch_locate(sorted_node_names, node_count, strtok(line+2," \n"));
      SddSize num_indicators = sorted_node_num_indicators[index];
      data->features[n] = (Feature*) malloc(sizeof(Feature));
      data->features[n]->num_indicators = num_indicators;
      data->features[n]->indicators =
          (SddLiteral*) malloc(num_indicators * sizeof(SddLiteral));
      for (int i = 0; i < num_indicators; i++) {
        data->features[n]->indicators[i] = sorted_node_indicators[index][i];
      }
      data->costs[n] = strtof(strtok(NULL," \n"),NULL);
      n++;
    }
  }

  // Make literal_weights easier to index (1.0 weight to all negative literals)
  SddLiteral literal_count = 2 * data->var_count + 1;
  data->literal_weights = (SddWmc*) malloc(literal_count * sizeof(SddWmc));
  data->literal_weights += data->var_count;
  for (int i = 0; i < data->var_count; i++) {
    data->literal_weights[i+1] = weights[i];
    data->literal_weights[-(i+1)] = 1.0;
  }

  // Free all auxiliary variables
  for (int i = 0; i < node_count; i++) {
    free(sorted_node_names[i]);
    free(sorted_node_indicators[i]);
  }
  free(sorted_node_names);
  free(sorted_node_indicators);
  free(sorted_node_num_indicators);  
  free(weights);
  free(line);
  fclose(input_fp);

  return data;
}

void free_search_data(SearchData* data) {
  for (int i = 0; i < data->num_features; i++) {
    free(data->features[i]->indicators);
    free(data->features[i]);
  }
  free(data->features);
  free(data->literal_weights - data->var_count);
  free(data->costs);
  free(data);
}

void print_search_data(SearchData* data) {
  printf("num_features: %"PRIsS", threshold: %.2f, budget: %.2f\n",
         data->num_features, data->threshold, data->budget);
  for (int i = 0; i < data->num_features; i++) {
    printf("Feature %d: %d indicators ", i, data->features[i]->num_indicators);
    for (int j = 0; j < data->features[i]->num_indicators; j++) {
      printf("%d,", data->features[i]->indicators[j]);
    }
    printf("\n");
  }
}

SearchResult* new_search_result(const SddSize num_features) {
  SearchResult* result =
      (SearchResult*) malloc(sizeof(SearchResult));
  result->best_score = 0;
  result->best_subset = (char*) malloc(num_features * sizeof(char));
  result->cost = 0;
  return result;
}

void free_search_result(SearchResult* result) {
  if (result->best_subset != NULL) free(result->best_subset);
  free(result);
}

void update_search_result(SearchResult* result, const SddWmc new_score,
    const float new_cost, const char* new_subset, const SddSize num_features) {
  result->best_score = new_score;
  result->cost = new_cost;
  for (int i = 0; i < num_features; i++) {
    result->best_subset[i] = new_subset[i];
  }
}
