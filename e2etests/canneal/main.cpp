// main.cpp
//
// Created by Daniel Schwartz-Narbonne on 13/04/07.
// Modified by Christian Bienia
//
// Copyright 2007-2008 Princeton University
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions
// are met:
// 1. Redistributions of source code must retain the above copyright
//    notice, this list of conditions and the following disclaimer.
// 2. Redistributions in binary form must reproduce the above copyright
//    notice, this list of conditions and the following disclaimer in the
//    documentation and/or other materials provided with the distribution.
//
// THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
// ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
// FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
// DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
// OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
// HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
// LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
// OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
// SUCH DAMAGE.


#include <iostream>
#include <math.h>
#include <stdlib.h>
#include <unistd.h>
#include <vector>
#include <numa.h>
#include <sys/prctl.h>
#include <pthread.h>
#include <stdint.h>
#include <inttypes.h>

#ifdef ENABLE_THREADS
#include <pthread.h>
#endif

#ifdef ENABLE_PARSEC_HOOKS
#include <hooks.h>
#endif

#include "annealer_types.h"
#include "annealer_thread.h"
#include "netlist.h"
#include "rng.h"
#include <stdarg.h>  // or #include <cstdarg>
#include <stdio.h>   // or #include <cstdio>

#define PR_SET_PT_PREFETCH 65
#define PR_GET_PT_PREFETCH 66
#define PR_SET_PTE_WARM    67
#define PR_GET_PTE_WARM    68


using namespace std;
int g_src_node;
int g_dst_node;
int g_pt_prefetch;
int g_pte_warm;
void* entry_pt(void*);

static void die(const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    vfprintf(stderr, fmt, ap); va_end(ap);
    fputc('\n', stderr);
    exit(1);
}

static int pin_to_any_cpu_on_node(int node) {
    struct bitmask *cpus = numa_allocate_cpumask();
    if (!cpus) return -1;
    if (numa_node_to_cpus(node, cpus) != 0) {
        numa_free_cpumask(cpus);
        return -1;
    }
    cpu_set_t set;
    CPU_ZERO(&set);
    int ok = -1;
    for (int i = 0; i < (int)cpus->size; i++) {
        if (numa_bitmask_isbitset(cpus, i)) {
            CPU_SET(i, &set);
            ok = 0;
            break;
        }
    }
    numa_free_cpumask(cpus);
    if (ok) return -1;

    return pthread_setaffinity_np(pthread_self(), sizeof(set), &set);
}

static void pick_two_nodes(int *src, int *dst) {
    int maxn = numa_max_node();
    if (maxn < 1) die("Need >= 2 NUMA nodes");

    int first = -1, second = -1;
    for (int n = 0; n <= maxn; n++) {
        long sz = numa_node_size64(n, NULL);
        if (sz <= 0) continue;
        if (first == -1) first = n;
        else { second = n; break; }
    }
    if (first == -1 || second == -1)
        die("Could not find two NUMA nodes");

    *src = first;
    *dst = second;
}

static inline uint64_t rdtscp_barrier(void) {
    unsigned aux;
    uint32_t lo, hi;
    asm volatile("lfence\n\trdtscp\n\t" :
                 "=a"(lo), "=d"(hi), "=c"(aux) :: "memory");
    asm volatile("lfence" ::: "memory");
    return (((uint64_t)hi << 32) | lo);
}

static int ___main (int argc, char * argv[]) {
if (numa_available() < 0) {
	die("NUMA not available");
}

pick_two_nodes(&g_src_node, &g_dst_node);

// Start execution pinned to source NUMA node 
if (pin_to_any_cpu_on_node(g_src_node) != 0)
	die("Failed to pin to src node");

// Disable prefetcher during warmup
prctl(PR_SET_PT_PREFETCH, 0, 0, 0, 0);
prctl(PR_SET_PTE_WARM,    0, 0, 0, 0);

// Read environment variables for the measured phase
g_pt_prefetch = getenv("PT_PREFETCH") ? atoi(getenv("PT_PREFETCH")) : 0;
g_pte_warm    = getenv("PTE_WARM")    ? atoi(getenv("PTE_WARM"))    : 0;
#ifdef PARSEC_VERSION
#define __PARSEC_STRING(x) #x
#define __PARSEC_XSTRING(x) __PARSEC_STRING(x)
        cout << "PARSEC Benchmark Suite Version "__PARSEC_XSTRING(PARSEC_VERSION) << endl << flush;
#else
        cout << "PARSEC Benchmark Suite" << endl << flush;
#endif //PARSEC_VERSION
#ifdef ENABLE_PARSEC_HOOKS
	__parsec_bench_begin(__parsec_canneal);
#endif

	srandom(3);

	if(argc != 5 && argc != 6) {
		cout << "Usage: " << argv[0] << " NTHREADS NSWAPS TEMP NETLIST [NSTEPS]" << endl;
		exit(1);
	}	
	
	//argument 1 is numthreads
	int num_threads = atoi(argv[1]);
	cout << "Threadcount: " << num_threads << endl;
#ifndef ENABLE_THREADS
	if (num_threads != 1){
		cout << "NTHREADS must be 1 (serial version)" <<endl;
		exit(1);
	}
#endif
		
	//argument 2 is the num moves / temp
	int swaps_per_temp = atoi(argv[2]);
	cout << swaps_per_temp << " swaps per temperature step" << endl;

	//argument 3 is the start temp
	int start_temp =  atoi(argv[3]);
	cout << "start temperature: " << start_temp << endl;
	
	//argument 4 is the netlist filename
	string filename(argv[4]);
	cout << "netlist filename: " << filename << endl;
	
	//argument 5 (optional) is the number of temperature steps before termination
	int number_temp_steps = -1;
        if(argc == 6) {
		number_temp_steps = atoi(argv[5]);
		cout << "number of temperature steps: " << number_temp_steps << endl;
        }

	//now that we've read in the commandline, run the program
	netlist my_netlist(filename);
        #define CONFIG_SHM_FILE_NAME "/tmp/alloctest-bench"
        FILE *fd2 = fopen(CONFIG_SHM_FILE_NAME ".ready", "w");
        if (fd2 == NULL) {
            printf("unable to create tmp file\n");
            exit(1);
        }
	annealer_thread a_thread(&my_netlist,num_threads,swaps_per_temp,start_temp,number_temp_steps);
	
#ifdef ENABLE_PARSEC_HOOKS
	__parsec_roi_begin();
#endif
#ifdef ENABLE_THREADS
	std::vector<pthread_t> threads(num_threads);
	void* thread_in = static_cast<void*>(&a_thread);
	for(int i=0; i<num_threads; i++){
		pthread_create(&threads[i], NULL, entry_pt,thread_in);
	}
	for (int i=0; i<num_threads; i++){
		pthread_join(threads[i], NULL);
	}
#else
	a_thread.Run();
#endif
#ifdef ENABLE_PARSEC_HOOKS
	__parsec_roi_end();
#endif
	fd2 = fopen(CONFIG_SHM_FILE_NAME ".done", "w");
        if (fd2 == NULL) {
            printf("Unable to create tmp file");
            exit(1);
        }
	cout << "Final routing is: " << my_netlist.total_routing_cost() << endl;

#ifdef ENABLE_PARSEC_HOOKS
	__parsec_bench_end();
#endif

	return 0;
}

int real_main (int argc, char * argv[]);
int real_main (int argc, char * argv[])
{
	return ___main(argc, argv);
}



int main (int argc, char *argv[])__attribute__((weak));
int main (int argc, char * argv[])
{
	return ___main(argc, argv);
}


void* entry_pt(void* data)
{
	annealer_thread* ptr = static_cast<annealer_thread*>(data);
	ptr->Run();
}
