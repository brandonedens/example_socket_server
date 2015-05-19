/** @file
 * Implementation of a socket server.
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
 * @date    2015-05-11
 * @details
 *
 */

/*******************************************************************************
 * Include Files
 */

#include "server.h"

#include <assert.h>
#include <errno.h>
#include <err.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>

/*******************************************************************************
 * Constants
 */
/** Amount of milliseconds to wait before timing out waiting for an event. */
#define EVT_WAIT_TIMEOUT 1000

/** Amount of queued events. */
#define EVENT_QUEUE 30

/*******************************************************************************
 * Local Types
 */

/** Representation of a connection to the server. */
struct conn {
	int fd;
	struct conn *next;
	struct conn *prev;

	struct server *server;
};

/** Representation of a server socket. */
struct server {
	/** Socket file descriptor. */
	int fd;

	/** Epoll file descriptor. */
	int ep_fd;

	/** Flag that indicates the server is to shutdown. */
	bool shutdown;

	/** List of server connections. */
	struct conn *conns;
};

/*******************************************************************************
 * Macros
 */

#define ARRAY_LEN(x) (sizeof(x) / (sizeof((x)[0])))

/*******************************************************************************
 * Local Functions
 */
static struct conn *conn_alloc_init(int fd);
static void conn_close(struct conn *c);
static ssize_t conn_process_read(struct conn *c);
static ssize_t conn_process_write(struct conn *c);
static int do_accept(struct server *server);
static void *get_in_addr(struct sockaddr *sa);
static void process_events(struct server *server);
static void server_add_conn(struct server *server, struct conn *conn);
static void server_del_conn(struct server *server, struct conn *conn);
static int set_nonblock(int fd);

/******************************************************************************/

/** Allocate resources for a connection and initialize the connection. */
static struct conn *conn_alloc_init(int fd)
{
	struct conn *c = calloc(1, sizeof(struct conn));
	c->fd = fd;
	return c;
}

/** Close out a connection. */
static void conn_close(struct conn *c)
{
	close(c->fd);
}

/** Iterate over the list of connections executing the given function for each
 * in turn.
 */
static void conn_for_each(struct conn *conns,
                          void (*func)(struct conn *, void *), void *data)
{
	for (struct conn *c = conns; NULL != c; c = c->next) {
		func(c, data);
	}
}

/** Process any data available for read from a connection. */
static ssize_t conn_process_read(struct conn *c)
{
	uint8_t buf[512];
	size_t total_sz = 0;

	while (true) {
		ssize_t sz = read(c->fd, buf, sizeof(buf));
		printf("Received from %d: %zd bytes\n", c->fd, sz);
		if (sz > 0) {
			total_sz += sz;
		} else if (sz <= 0) {
			if (errno == EAGAIN || errno == EWOULDBLOCK) {
				// Done reading data for now.
				break;
			}

			// Connection closing due to clean shutdown or err.
			if (sz == 0) {
				printf("Closing connection.\n");
			} else {
				printf("Connection failed: %d\n", errno);
			}
			conn_close(c);
			server_del_conn(c->server, c);
			break;
		}
	}

	return total_sz;
}

/** Write any data necessary to the client. */
static ssize_t conn_process_write(struct conn *c)
{
	(void)c;
	// TODO implement write.
	return -1;
}

static void conn_start_polling(struct conn *c, void *data)
{
	int ep_fd = *((int *)data);

	struct epoll_event event = {
		.events = (EPOLLIN | EPOLLET),
		.data.ptr = c,
	};
	if (0 != epoll_ctl(ep_fd, EPOLL_CTL_ADD, c->fd, &event)) {
		perror("Failure to configure conn epoll event.");
	}
}

/** Accept client connections and act as an echo server. */
static int do_accept(struct server *server)
{
	// Accept a connection.
	struct sockaddr_storage addr;
	socklen_t sin_size = sizeof(addr);
	int fd = accept(server->fd, (struct sockaddr *)&addr, &sin_size);
	if (fd == -1) {
		if ((errno == EAGAIN) || (errno == EWOULDBLOCK)) {
			printf("Done processing connections.\n");
			// All connections processed.
			return EAGAIN;
		}

		// Some other failure occured.
		perror("Server failed to accept.");
		abort();
	}
	set_nonblock(fd);

	// Recover client address information.
	char s[INET6_ADDRSTRLEN];
	inet_ntop(addr.ss_family, get_in_addr((struct sockaddr *)&addr),
			s, sizeof(s));
	printf("Server: got connection from %s\n", s);

	// Setup connection.
	struct conn *conn = conn_alloc_init(fd);
	server_add_conn(server, conn);

	// Begin polling connection descriptor.
	struct epoll_event event = {
		.data = {.ptr = conn},
		.events = EPOLLIN | EPOLLET,
	};
	int ret = epoll_ctl(server->ep_fd, EPOLL_CTL_ADD, fd, &event);
	if (0 != ret) {
		perror("Failure to monitor conn fd.");
		abort();
	}

	return 0;
}

/** Get sockaddr, IPv4 or IPv6: */
static void *get_in_addr(struct sockaddr *sa)
{
	if (sa->sa_family == AF_INET) {
		return &(((struct sockaddr_in *)sa)->sin_addr);
	}

	return &(((struct sockaddr_in6 *)sa)->sin6_addr);
}

/** Process incoming events for the server and its connections. */
static void process_events(struct server *server)
{
	struct epoll_event events[EVENT_QUEUE];
	int const len = epoll_wait(server->ep_fd, events, ARRAY_LEN(events),
	                           EVT_WAIT_TIMEOUT);
	if (-1 == len) {
		perror("Failure to epoll wait on server.");
	}

	for (int i = 0; i < len; i++) {
		struct server *ev_server = (struct server *)events[i].data.ptr;

		if ((events[i].events & EPOLLERR) ||
		    (events[i].events & EPOLLHUP) ||
		    (!(events[i].events & EPOLLIN))) {

			// An error has occured on this fd, or the socket is not
			// ready for reading (why were we notified then?)
			struct server *srv =
			    (struct server *)events[i].data.ptr;
			if (server == srv) {
				fprintf(stderr, "Failure in server.\n");
				abort();
			}

			fprintf(stderr, "epoll conn error\n");
			struct conn *c = (struct conn *)events[i].data.ptr;
			conn_close(c);
			server_del_conn(server, c);
			continue;

		} else if (ev_server == server) {

			// Accept all waiting connections.
			while (true) {
				int ret = do_accept(server);
				if (EAGAIN == ret) {
					break;
				}
			}

		} else {
			printf("Handling connection comms.\n");
			// Handle processing of connection descriptors.
			struct conn *c = events[i].data.ptr;
			conn_process_read(c);
			conn_process_write(c);
		}
	}
}

/** Allocate memory for the server. */
struct server *server_alloc(void)
{
	return calloc(1, sizeof(struct server));
}

/** Add connection to server. */
static void server_add_conn(struct server *server, struct conn *conn)
{
	conn->server = server;

	if (NULL != server->conns) {
		server->conns->prev = conn;
	}
	conn->next = server->conns;
	server->conns = conn;
}

/** Bind a server on the given port returning the created socket descriptor. */
int server_bind(struct server *server, char const *port)
{
	struct addrinfo hints;
	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE; // use my IP

	struct addrinfo *servinfo;
	int rv;
	if ((rv = getaddrinfo(NULL, port, &hints, &servinfo)) != 0) {
		err(errno, "getaddrinfo: %s", gai_strerror(rv));
	}

	// loop through all the results and bind to the first we can
	int yes = 1;
	struct addrinfo *p;
	for (p = servinfo; p != NULL; p = p->ai_next) {
		if ((server->fd = socket(p->ai_family, p->ai_socktype,
		                         p->ai_protocol)) == -1) {
			perror("server: socket");
			continue;
		}
		set_nonblock(server->fd);

		if (setsockopt(server->fd, SOL_SOCKET, SO_REUSEADDR, &yes,
		               sizeof(int)) == -1) {
			perror("setsockopt");
			exit(1);
		}

		if (bind(server->fd, p->ai_addr, p->ai_addrlen) == -1) {
			close(server->fd);
			perror("server: bind");
			continue;
		}

		break;
	}

	if (p == NULL) {
		perror("server: failed to bind");
		return -2;
	}

	freeaddrinfo(servinfo); // all done with this structure
	return server->fd;
}

/** Remove connection from the server. */
static void server_del_conn(struct server *server, struct conn *conn)
{
	struct conn *next = conn->next;
	struct conn *prev = conn->prev;
	if (NULL != next) {
		next->prev = prev;
	}
	if (NULL != prev) {
		prev->next = next;
	} else {
		server->conns = next;
	}
}

/** Free resources allocated for server. */
void server_free(struct server *server)
{
	struct conn *c_next;
	for (struct conn *c = server->conns; NULL != c; c = c_next) {
		c_next = c->next;
		free(c);
	}
	free(server);
}

/** Begin listening for connections. */
int server_listen(struct server *server, int backlog)
{
	if (listen(server->fd, backlog) == -1) {
		perror("listen");
		exit(1);
	}

	return 0;
}

/** Restore the state of the server from the given text string.
 *
 * This consists of setting up the file descriptors for both the server and its
 * connections.
 */
int server_restore(struct server *server, char *txt)
{
	int fd = strtol(txt, &txt, 10);
	if (0 == fd) {
		return EINVAL;
	}
	server->fd = fd;

	fd = strtol(txt, &txt, 10);
	while (0 != fd) {
		struct conn *c = conn_alloc_init(fd);
		server_add_conn(server, c);
		fd = strtol(txt, &txt, 10);
	}

	return 0;
}

/** Save server data to the given buffer. */
int server_save(struct server *server, size_t len, char buf[len])
{
	// Write out the server descriptor.
	size_t ret = snprintf(buf, len, "%d", server->fd);
	assert(ret > 0);
	len -= ret;
	buf += ret;

	// Write out the connection descriptors.
	// TODO this needs to reconnect connections to their respective data.
	for (struct conn *c = server->conns; NULL != c; c = c->next) {
		ret = snprintf(buf, len, " %d", c->fd);
		len -= ret;
		buf += ret;
	}
	return 0;
}

/** Start handling connections for the server. */
void server_start(struct server *server)
{
	if ((server->ep_fd = epoll_create1(0)) < 0) {
		perror("Failure to create event poll for server.");
	}

	// Setup server polling for accept.
	struct epoll_event event = {
		.events = (EPOLLIN | EPOLLET),
		.data.ptr = server,
	};
	if (0 != epoll_ctl(server->ep_fd, EPOLL_CTL_ADD, server->fd, &event)) {
		perror("Failure to configure server epoll event.");
	}

	// For all existing connections begin polling.
	conn_for_each(server->conns, conn_start_polling, &server->ep_fd);

	printf("server: waiting for connections...\n");
	while (!server->shutdown) {
		process_events(server);
	}
}

/** Set a socket to non-blocking mode. */
static int set_nonblock(int fd)
{
	int flags;

	flags = fcntl(fd, F_GETFL);
	if (flags < 0) {
		return flags;
	}

	flags |= O_NONBLOCK;
	if (fcntl(fd, F_SETFL, flags) < 0) {
		return -1;
	}
	return 0;
}
