/** @file
 * Implementation of example program for testing hot-reload.
 *
 *==============================================================================
 * Copyright 2015 by Brandon Edens. All Rights Reserved
 *==============================================================================
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * @author  Brandon Edens
 * @date    2015-05-09
 * @details
 *
 * This software implements copyover or hot-reload of an existing socket server.
 * Sending the signal SIGUSR2 to the process will cause the socket server to
 * restart while running the latest version of the executable.
 */

/*******************************************************************************
 * Include Files
 */

#include <assert.h>
#include <errno.h>
#include <error.h>
#include <getopt.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <pthread.h>

#include "server.h"
#include "work_queue.h"

/*******************************************************************************
 * Constants
 */

/*******************************************************************************
 * Local Types
 */

/*******************************************************************************
 * Macros
 */

#define DBG(...)                                                               \
	do {                                                                   \
		if (debug) {                                                   \
			printf("DBG: ");                                       \
			printf(__VA_ARGS__);                                   \
		}                                                              \
	} while (0)

/*******************************************************************************
 * Local Variables
 */

static int ac;
static char **av;

static bool debug;

static bool start_reload;

struct server *server;

/*******************************************************************************
 * Global Variable Definitions
 */

/*******************************************************************************
 * Local Functions
 */

static void process_events(void *data);
static void sig_handler(int signum);

/******************************************************************************/

/** Function invoked in work queue that processes server events. */
static void process_events(void *data)
{
	struct server *s = (struct server *)data;
	while (server_is_running(s)) {
		server_process_events(s);
	}

#if 0
	printf("Thread %lu shutting down.\n", pthread_self());
#endif
}

/** Signal handler. */
static void sig_handler(int signum)
{
	if (SIGUSR2 == signum) {
		DBG("SIGUSR2 handler invoked.\n");
		start_reload = true;
	}
}

int main(int argc, char *argv[])
{
	ac = argc;
	av = argv;

	struct option long_options[] = {
		{"debug",  no_argument, 0, 0},
		{"reload", no_argument, 0, 0},
		{0,        0,           0, 0},
	};
	int c;
	int option_idx = 0;

	bool reload;
	while (true) {
		c = getopt_long(argc, argv, "dr", long_options, &option_idx);
		if (-1 == c) {
			break;
		}
		switch (c) {
		case 'd':
			debug = true;
			DBG("Debug enabled.\n");
			break;
		case 'r':
			reload = true;
			break;
		default:
			printf("Unhandled option: %o\n", c);
		}
	}

	// Begin creating the server. Note that server initialization proceeds
	// differently based upon whether or not this is a copyover of an
	// existing server execution.
	server = server_alloc();
	if (reload) {
		DBG("Reloading the server.\n");
#if 0
		printf("\tfrom data: %s\n", getenv("reload"));
#endif
		int ret = server_restore(server, getenv("reload"));
		assert(0 == ret);
		sleep(1);
	} else {
		int ret = server_bind(server, "3939");
		assert(ret >= 0);
		server_listen(server, 5);
	}

	server_setup_poll(server);

	// Setup the work queue.
	struct work_queue *queue = work_queue_alloc();
	work_queue_init(queue, 4);

	printf("Waiting for connections...\n");
	for (int i = 0; i < 4; i++) {
		work_queue_add(queue, process_events, server);
	}

	// Register signal handling.
	struct sigaction sa = {
		.sa_handler = sig_handler,
		.sa_flags = SA_RESTART,
	};
	sigemptyset(&sa.sa_mask);
	if (0 != sigaction(SIGUSR2, &sa, NULL)) {
		perror("Failure to set up signal.");
	}

	// Do nothing while we wait for server to shutdown.
	while (server_is_running(server)) {
		sleep(1);

		if (start_reload) {
			// We've been instructed to reload.
			server_stop(server);
		}
	}

	if (start_reload) {
		// Join all the threads.
		work_queue_term(queue);

		// Save server details to the reload environment variable.
		char buf[128];
		server_save(server, sizeof(buf), buf);
		int ret = setenv("reload", buf, true);
		assert(0 == ret);

		// Setup the program arguments to be passed to the next instance
		// of the server with the addition of the copyover option.
		char *args[ac + 2];
		bool existing_reload = false;
		for (int i = 0; i < ac; i++) {
			args[i] = av[i];
			if (0 == strcmp(av[i], "-r") ||
			    0 == strcmp(av[i], "--reload")) {
				existing_reload = true;
			}
		}
		if (!existing_reload) {
			args[ac] = "-r";
		}
		args[ac + 1] = NULL;

		// Unblock the signal before executing the new process.
		sigset_t sigs;
		sigprocmask(0, 0, &sigs);
		sigdelset(&sigs, SIGUSR2);
		sigprocmask(SIG_SETMASK, &sigs, NULL);

		// Execute the file.
		execv(args[0], args);
	}

	return EXIT_SUCCESS;
}
