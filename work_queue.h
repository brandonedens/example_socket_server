/** @file
 * Interface to software for queuing parallel work.
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
 * @date    2015-05-18
 * @details
 *
 */
#ifndef WORK_QUEUE_H_
#define WORK_QUEUE_H_

/*******************************************************************************
 * Global Types
 */

struct work_queue;

/*******************************************************************************
 * Global Functions
 */

void work_queue_add(struct work_queue *queue, void (*func)(void *), void *data);
struct work_queue *work_queue_alloc(void);
void work_queue_free(struct work_queue *queue);
int work_queue_init(struct work_queue *queue, int num_workers);
void work_queue_term(struct work_queue *queue);

#endif  // WORK_QUEUE_H_
