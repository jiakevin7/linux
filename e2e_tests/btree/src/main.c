//BTREE_BASE_PAGES=1024 PT_MODE=prefetch BTREE_TIME_FIRST_K=16 ./btree-bench ...
//BTREE_BASE_PAGES=1024 PT_MODE=baseline BTREE_TIME_FIRST_K=16 ./btree-bench ...
/**
 * MIT License
 * Copyright (c) 2020 Mitosis-Project
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <sys/prctl.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <ctype.h>
#include <inttypes.h>
#include <limits.h>
#include <errno.h>

#include <string.h>
#include <fcntl.h>     /* open */
#include <unistd.h>    /* exit */
#include <sys/ioctl.h> /* ioctl */
#include <sys/mman.h>
#include <sys/time.h>
#include <numa.h>
#include <time.h>
#include <pthread.h>
#include <signal.h>

#define PR_SET_PT_PREFETCH  65
#define PR_GET_PT_PREFETCH  66

#define PR_SET_PTE_WARM     67
#define PR_GET_PTE_WARM     68

#ifdef _OPENMP
#    include <omp.h>
#endif

#include "config.h"

bool SYSCALL = false;

FILE *opt_file_out = NULL;  ///< standard outpu

extern int real_main(int argc, char *argv[]);

void signalhandler(int sig)
{
	FILE *fd3 = fopen(CONFIG_SHM_FILE_NAME ".done", "w");

	if (fd3 == NULL) {
		fprintf(stderr, "ERROR: could not create the shared memory file descriptor\n");
		exit(-1);
	}

	usleep(250);


	exit(0);
}


int main(int argc, char *argv[])
{

	{
		const char *pt_mode = getenv("PT_MODE");
		if (pt_mode) {
			if (!strcmp(pt_mode, "pt_warm")) {
				if (prctl(PR_SET_PTE_WARM, 1, 0, 0, 0) != 0) {
					perror("PR_SET_PTE_WARM failed");
				}
			} else if (!strcmp(pt_mode, "pte_prefetch")) {
				if (prctl(PR_SET_PT_PREFETCH, 1, 0, 0, 0) != 0) {
					perror("PR_SET_PT_PREFETCH failed");
				}
			} else if (!strcmp(pt_mode, "pte_prefetch_load")) {
				if (prctl(PR_SET_PT_PREFETCH, 1, 1, 0, 0) != 0) {
					perror("PR_SET_PT_PREFETCH failed");
				}
			}
		}
	}

	{
		const char *syscall = getenv("SYSCALL");
		if (syscall) {
			if (!strcmp(syscall, "1")) {
				SYSCALL = true;
			} else {
				SYSCALL = false;
			}
		}
	}

	struct timeval tstart, tend;
	gettimeofday(&tstart, NULL);

	/* check if NUMA is available, otherwise we don't know how to allocate memory */
	if (numa_available() == -1) {
		fprintf(stderr, "ERROR: Numa not available on this machine.\n");
		return -1;
	}

	opt_file_out = stdout;
	int c;
	while ((c = getopt(argc, argv, "o:h")) != -1) {
		switch (c) {
			case '-':
				break;
			case 'h':
				printf("usage: %s [-o FILE]\n", argv[0]);
				return 0;
			case 'o':
				opt_file_out = fopen(optarg, "a");
				if (opt_file_out == NULL) {
					fprintf(stderr, "Could not open the file '%s' switching to stdout\n", optarg);
					opt_file_out = stdout;
				}
				break;
			case '?':
				switch (optopt) {
					case 'o':
						fprintf(stderr, "Option -%c requires an argument.\n", optopt);
						return -1;
					default:
						fprintf(stderr, "Unknown option `-%c'.\n", optopt);
						return -1;
				}
		}
	}

	int prog_argc = 0;
	char **prog_argv = NULL;

	prog_argv = &argv[0];
	prog_argc = argc;

	optind = 1;

	for (int i = 0; i < argc; i++) {
		if (strcmp("--", argv[i]) == 0) {
			argv[i] = argv[0];
			prog_argv = &argv[i];
			prog_argc = argc - i;
			break;
		}
	}

	/* setting the bind policy */
	numa_set_strict(1);
	numa_set_bind_policy(1);

	struct sigaction sigact;
	sigset_t block_set;

	sigfillset(&block_set);
	sigdelset(&block_set, SIGUSR1);

	sigemptyset(&sigact.sa_mask);
	sigact.sa_flags = 0;
	sigact.sa_handler = signalhandler;
	sigaction(SIGUSR1, &sigact, NULL);

	real_main(prog_argc, prog_argv);
	gettimeofday(&tend, NULL);

	long sec  = tend.tv_sec  - tstart.tv_sec;
	long usec = tend.tv_usec - tstart.tv_usec;

	if (usec < 0) {
		sec  -= 1;
		usec += 1000000;  // borrow 1 second = 1,000,000 usec
	}

	double elapsed = (double)sec + (double)usec / 1e6;

	printf("Total run time: %.6f\n", elapsed);

	return 0;
}
