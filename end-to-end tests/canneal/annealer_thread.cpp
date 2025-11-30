// annealer_thread.cpp
//
// Created by Daniel Schwartz-Narbonne on 14/04/07.
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

#ifdef ENABLE_THREADS
#include <pthread.h>
#endif

#include <cassert>
#include <math.h>
#include <iostream>
#include <fstream>

#include "annealer_thread.h"
#include "location_t.h"
#include "annealer_types.h"
#include "netlist_elem.h"
#include "rng.h"

// --- NEW: system headers for NUMA + prctl + timing ---
#include <sched.h>
#include <numa.h>
#include <sys/prctl.h>
#include <stdint.h>
#include <inttypes.h>
#include <cstdlib>  // getenv, atol
#include <cstdio>   // printf, perror
// -----------------------------------------------------

// Globals defined in main.cpp
extern int g_src_node;
extern int g_dst_node;
extern int g_pt_prefetch;
extern int g_pte_warm;

// Local helpers (internal linkage; OK to duplicate from main.cpp)
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

static inline uint64_t rdtscp_barrier(void) {
    unsigned aux;
    uint32_t lo, hi;
    asm volatile("lfence\n\trdtscp\n\t"
                 : "=a"(lo), "=d"(hi), "=c"(aux)
                 :
                 : "memory");
    asm volatile("lfence" ::: "memory");
    return ((uint64_t)hi << 32) | lo;
}

using std::cout;
using std::endl;


//*****************************************************************************************
//
//*****************************************************************************************
void annealer_thread::Run()
{
	int accepted_good_moves=0;
	int accepted_bad_moves=-1;
	double T = _start_temp;
	Rng rng; //store of randomness

	// --- NEW: warmup + timing control ---
	// Default: if running fixed number of temperature steps, warm up for 25% of them.
	// If running until convergence (_number_temp_steps == -1), default warmup is 0,
	// but can be overridden via WARMUP_TEMPS.
	long warmup_steps = 0;
	if (_number_temp_steps > 0) {
		warmup_steps = _number_temp_steps / 4;
	}
	const char *env_ws = std::getenv("WARMUP_TEMPS");
	if (env_ws) {
		long tmp = std::atol(env_ws);
		if (tmp >= 0) {
			warmup_steps = tmp;
		}
	}
	bool timing_started = false;
	uint64_t t_start = 0;
	// --- END NEW ---

	long a_id;
	long b_id;
	netlist_elem* a = _netlist->get_random_element(&a_id, NO_MATCHING_ELEMENT, &rng);
	netlist_elem* b = _netlist->get_random_element(&b_id, NO_MATCHING_ELEMENT, &rng);

	int temp_steps_completed=0;
	while(keep_going(temp_steps_completed, accepted_good_moves, accepted_bad_moves)){
		T = T / 1.5;
		accepted_good_moves = 0;
		accepted_bad_moves = 0;

		for (int i = 0; i < _moves_per_thread_temp; i++){
			//get a new element. Only get one new element, so that reuse should help the cache
			a = b;
			a_id = b_id;
			b = _netlist->get_random_element(&b_id, a_id, &rng);

			routing_cost_t delta_cost = calculate_delta_routing_cost(a,b);
			move_decision_t is_good_move = accept_move(delta_cost, T, &rng);

			//make the move, and update stats:
			if (is_good_move == move_decision_accepted_bad){
				accepted_bad_moves++;
				_netlist->swap_locations(a,b);
			} else if (is_good_move == move_decision_accepted_good){
				accepted_good_moves++;
				_netlist->swap_locations(a,b);
			} else if (is_good_move == move_decision_rejected){
				//no need to do anything for a rejected move
			}
		}
		temp_steps_completed++;
#ifdef ENABLE_THREADS
		pthread_barrier_wait(&_barrier);
#endif

		// --- NEW: after warmup temperature steps, migrate + enable prefetch + start timing ---
		if (!timing_started && temp_steps_completed == warmup_steps) {
			// Move this thread's execution to destination NUMA node
			if (pin_to_any_cpu_on_node(g_dst_node) != 0) {
				std::perror("pin_to_any_cpu_on_node(dst_node)");
			}

			// Enable kernel knobs according to env-configured globals
			prctl(PR_SET_PT_PREFETCH, g_pt_prefetch, 0, 0, 0);
			prctl(PR_SET_PTE_WARM,    g_pte_warm,    0, 0, 0);

			// Begin timing the "measured" phase
			t_start = rdtscp_barrier();
			timing_started = true;
		}
	}

	// --- NEW: stop timing and report cycles for this thread ---
	if (timing_started) {
		uint64_t t_end = rdtscp_barrier();
		uint64_t cyc = t_end - t_start;
		std::printf("%" PRIu64 "\n", cyc);
	}
	// --- END NEW ---
}

//*****************************************************************************************
//
//*****************************************************************************************
annealer_thread::move_decision_t annealer_thread::accept_move(routing_cost_t delta_cost, double T, Rng* rng)
{
	//always accept moves that lower the cost function
	if (delta_cost < 0){
		return move_decision_accepted_good;
	} else {
		double random_value = rng->drand();
		double boltzman = exp(- delta_cost/T);
		if (boltzman > random_value){
			return move_decision_accepted_bad;
		} else {
			return move_decision_rejected;
		}
	}
}


//*****************************************************************************************
//  If get turns out to be expensive, I can reduce the # by passing it into the swap cost fcn
//*****************************************************************************************
routing_cost_t annealer_thread::calculate_delta_routing_cost(netlist_elem* a, netlist_elem* b)
{
	location_t* a_loc = a->present_loc.Get();
	location_t* b_loc = b->present_loc.Get();

	routing_cost_t delta_cost = a->swap_cost(a_loc, b_loc);
	delta_cost += b->swap_cost(b_loc, a_loc);

	return delta_cost;
}

//*****************************************************************************************
//  Check whether design has converged or maximum number of steps has reached
//*****************************************************************************************
bool annealer_thread::keep_going(int temp_steps_completed, int accepted_good_moves, int accepted_bad_moves)
{
	bool rv;

	if(_number_temp_steps == -1) {
		//run until design converges
		rv = _keep_going_global_flag && (accepted_good_moves > accepted_bad_moves);
		if(!rv) _keep_going_global_flag = false; // signal we have converged
	} else {
		//run a fixed amount of steps
		rv = temp_steps_completed < _number_temp_steps;
	}

	return rv;
}