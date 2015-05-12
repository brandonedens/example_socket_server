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

/*******************************************************************************
 * Local Types
 */

struct conn {
	int fd;
	struct conn *next;
};

struct server {
	int fd;

	struct conn *conns;
};

/*******************************************************************************
 * Macros
 */

/*******************************************************************************
 * Local Variables
 */

/*******************************************************************************
 * Global Variable Definitions
 */

/*******************************************************************************
 * Local Functions
 */
static void *get_in_addr(struct sockaddr *sa);
static void server_add_conn(struct server *server, struct conn *conn);
static int set_nonblock(int fd);

/******************************************************************************/

/** Allocate resources for a connection and initialize the connection. */
static struct conn *conn_alloc_init(int fd)
{
	struct conn *c = calloc(1, sizeof(struct conn));
	c->fd = fd;
	return c;
}

/** Get sockaddr, IPv4 or IPv6: */
static void *get_in_addr(struct sockaddr *sa)
{
	if (sa->sa_family == AF_INET) {
		return &(((struct sockaddr_in *)sa)->sin_addr);
	}

	return &(((struct sockaddr_in6 *)sa)->sin6_addr);
}

/** Accept client connections and act as an echo server. */
void server_accept(struct server *server)
{
	char s[INET6_ADDRSTRLEN];
	// Client's address information.
	struct sockaddr_storage addr;
	socklen_t sin_size = sizeof(addr);
	int fd = accept(server->fd, (struct sockaddr *)&addr, &sin_size);
	if (fd == -1) {
		perror("accept");
		return;
	}
	set_nonblock(fd);

	inet_ntop(addr.ss_family, get_in_addr((struct sockaddr *)&addr),
			s, sizeof(s));
	warn("Server: got connection from %s", s);

	// Setup connection.
	struct conn *conn = conn_alloc_init(fd);
	server_add_conn(server, conn);

	ssize_t len = 0;
	while (len >= 0) {
		uint8_t buf[512];
		len = recv(fd, buf, sizeof(buf), 0);
		if (len <= 0) {
			break;
		}
		len = send(fd, buf, len, 0);
	}
	close(fd);
}

/** Allocate memory for the server. */
struct server *server_alloc(void)
{
	return calloc(1, sizeof(struct server));
}

/** Add connection to server. */
static void server_add_conn(struct server *server, struct conn *conn)
{
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

/** Restore the state of the server from the given text string. This consists of
 * setting up the file descriptors for both the server and its connections.
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

/** Set a socket to non-blocking mode. */
static int set_nonblock(int fd)
{
#if 0
	int flags;

	flags = fcntl(fd, F_GETFL);
	if (flags < 0) {
		return flags;
	}

	flags |= O_NONBLOCK;
	if (fcntl(fd, F_SETFL, flags) < 0) {
		return -1;
	}
#else
	(void)fd;
#warning "Non-Blocking sockets disabled until epoll. "
#endif
	return 0;
}
