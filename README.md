# TrimBN
This repository contains the code for the paper "On Robust Trimming of Bayesian Network Classifiers", published in IJCAI 2018.

Run `make` to build the code.

To run a feature selection problem, you can run:
```
build/trim -c CNF_FILE -l LMAP_FILE -e PROBLEM_FILE
```
where CNF_FILE and LMAP_FILE are the Bayesian network encodings from ACE (you can get this here: http://reasoning.cs.ucla.edu/ace), and PROBLEM_FILE defines the search problem. The first line of the problem file is “$ [num_features] [decision_threshold] [budget]”, followed by “d [decision_node_name]” and “f [feature_node_name] [feature_cost]” for every candidate feature. Here, the node names are the ones defined in the original Bayesian network file.
Networks used for experiments in the paper can be found in the examples/ directory

To generate CNF and lmap files, you can use ACE. E.g.:
```
compile NETWORK_FILE -noEclause -encodeOnly -cd06
```
From the output files, the .cnf and .lmap files will be needed.

Please contact me at yjchoi@cs.ucla.edu for questions.
