#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "sddapi.h"
#include "compiler.h"
#include "search.h"

// forward references
void free_fnf(Fnf* fnf);
SearchResult* search_best_subset(SearchData* data, Fnf* fnf, SddCompilerOptions* options);

SddCompilerOptions sdd_default_opt() {
  SddCompilerOptions options = 
    {
    NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL, //file names
    1,          // minimize cardinality
    "balanced", // initial vtree type
    -1,          // vtree search mode
    0           // verbose
    };
  return options;
}

/****************************************************************************************
 * start
 ****************************************************************************************/
 
int main(int argc, char** argv) {
  Fnf* fnf;
  SearchData* data;
  SddCompilerOptions options = sdd_default_opt(); // default options

  // Read input options
  char *cnf_filename = NULL, *lmap_filename = NULL, *input_filename = NULL;
  SddWmc threshold = -1.0;
  int option;
  while ((option = getopt(argc, argv, "c:l:e:t:")) != -1) {
    switch (option) {
      case 'c':
        cnf_filename = optarg;
        break;
      case 'l':
        lmap_filename = optarg;
        break;
      case 'e':
        input_filename = optarg;
        break;
      case 't':
        threshold = strtof(optarg, NULL);
        break;
      default:
        exit(1);
    }
  }
  if (cnf_filename == NULL || lmap_filename == NULL || input_filename == NULL) {
    fprintf(stderr,
      "Must provide names of CNF, lmap, and feature selection input files\n");
    exit(1);
  }

  printf("\nreading cnf...");
  fnf = read_cnf(cnf_filename);
  printf("vars=%"PRIlitS" clauses=%"PRIsS"\n",fnf->var_count,fnf->litset_count);

  printf("\nreading esdp search data...\n");
  data = read_search_data(lmap_filename, input_filename);

  // Overwrite threshold if explicitly given
  if (threshold > 0) {
    data->threshold = threshold;
  }

  print_search_data(data);
  
  SearchResult* result = search_best_subset(data, fnf, &options);
  
  printf("\nbest ECA: %f\nbest subset of features: ", result->best_score);
  for (int i = 0; i < data->num_features; i++) {
    printf("%d,", result->best_subset[i]);
  }

  printf("\nfreeing..."); fflush(stdout);
  free_fnf(fnf);
  free_search_data(data);
  free_search_result(result);
  printf("done\n"); 

  return 0;
}

/****************************************************************************************
 * end
 ***************************************************************************************/
