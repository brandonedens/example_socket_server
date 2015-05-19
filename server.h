/** @file
 * Interface to the socket server.
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
#ifndef SERVER_H_
#define SERVER_H_

/*******************************************************************************
 * Include Files
 */

#include <stddef.h>
#include <stdint.h>

/*******************************************************************************
 * Global Types
 */

struct server;

/*******************************************************************************
 * Global Functions
 */
void server_accept(struct server *server);
struct server *server_alloc(void);
int server_bind(struct server *server, char const *port);
int server_listen(struct server *server, int backlog);
int server_restore(struct server *server, char *txt);
int server_save(struct server *server, size_t len, char buf[len]);
void server_start(struct server *server);

#endif  // SERVER_H_
