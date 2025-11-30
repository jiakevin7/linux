###############################################################################
# Paper: Mitosis - Mitosis: Transparently Self-Replicating Page-Tables
#                  for Large-Memory Machines
# Authors: Reto Achermann, Jayneel Gandhi, Timothy Roscoe,
#          Abhishek Bhattacharjee, and Ashish Panwar
###############################################################################

CC  = gcc
CXX = g++

# cflags used
CFLAGS   = -O3 -march=native -std=gnu11 -Wall -Wextra
CXXFLAGS = -O3 -march=native

# common libraries used
COMMON_LIBS = -lrt -ldl -lnuma -lpthread -lm

# common main functions for multithreaded, single threaded
SRC_M = src/main.c src/btree1.c
SRC_S = src/main.c src/btree1.c
DEPS  = src/config.h

# the targets
all: bin/bench_btree_st bin/bench_btree_mt

###############################################################################
# BTree benchmark
###############################################################################

bin/bench_btree_st: $(SRC_S) $(DEPS) | bin
	$(CC) $(INCLUDES) $(CFLAGS) $(SRC_S) -o $@ $(COMMON_LIBS)

bin/bench_btree_mt: $(SRC_M) $(DEPS) | bin
	$(CC) $(INCLUDES) $(CFLAGS) -fopenmp $(SRC_M) -o $@ $(COMMON_LIBS)

###############################################################################
# Helper + Clean
###############################################################################

bin:
	mkdir -p bin

clean:
	rm -rf bin/*