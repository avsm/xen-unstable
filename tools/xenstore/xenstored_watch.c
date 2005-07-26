/* 
    Watch code for Xen Store Daemon.
    Copyright (C) 2005 Rusty Russell IBM Corporation

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

#include <stdio.h>
#include <sys/types.h>
#include <stdarg.h>
#include <stdlib.h>
#include <sys/time.h>
#include <time.h>
#include <assert.h>
#include "talloc.h"
#include "list.h"
#include "xenstored_watch.h"
#include "xs_lib.h"
#include "utils.h"
#include "xenstored_test.h"
#include "xenstored_domain.h"

/* FIXME: time out unacked watches. */

/* We create this if anyone is interested "node", then we pass it from
 * watch to watch as each connection acks it.
 */
struct watch_event
{
	/* The watch we are firing for (watch->events) */
	struct list_head list;

	/* Watches we need to fire for (watches[0]->events == this). */
	struct watch **watches;
	unsigned int num_watches;

	struct timeval timeout;

	/* Name of node which changed. */
	char *node;

	/* For remove, we trigger on all the children of this node too. */
	bool recurse;
};

struct watch
{
	struct list_head list;
	unsigned int priority;

	/* Current outstanding events applying to this watch. */
	struct list_head events;

	/* Is this relative to connnection's implicit path? */
	bool relative;

	char *token;
	char *node;
	struct connection *conn;
};
static LIST_HEAD(watches);

static struct watch_event *get_first_event(struct connection *conn)
{
	struct watch *watch;
	struct watch_event *event;

	/* Find first watch with an event. */
	list_for_each_entry(watch, &watches, list) {
		if (watch->conn != conn)
			continue;

		event = list_top(&watch->events, struct watch_event, list);
		if (event)
			return event;
	}
	return NULL;
}

/* Look through our watches: if any of them have an event, queue it. */
void queue_next_event(struct connection *conn)
{
	struct watch_event *event;
	const char *node;
	char *buffer;
	unsigned int len;

	/* We had a reply queued already?  Send it: other end will
	 * discard watch. */
	if (conn->waiting_reply) {
		conn->out = conn->waiting_reply;
		conn->waiting_reply = NULL;
		conn->waiting_for_ack = NULL;
		return;
	}

	/* If we're already waiting for ack, don't queue more. */
	if (conn->waiting_for_ack)
		return;

	event = get_first_event(conn);
	if (!event)
		return;

	/* If we decide to cancel, we will reset this. */
	conn->waiting_for_ack = event->watches[0];

	/* If we deleted /foo and they're watching /foo/bar, that's what we
	 * tell them has changed. */
	if (!is_child(event->node, event->watches[0]->node)) {
		assert(event->recurse);
		node = event->watches[0]->node;
	} else
		node = event->node;

	/* If watch placed using relative path, give them relative answer. */
	if (event->watches[0]->relative) {
		node += strlen(get_implicit_path(conn));
		if (node[0] == '/') /* Could be "". */
			node++;
	}

	/* Create reply from path and token */
	len = strlen(node) + 1 + strlen(event->watches[0]->token) + 1;
	buffer = talloc_array(conn, char, len);
	strcpy(buffer, node);
	strcpy(buffer+strlen(node)+1, event->watches[0]->token);
	send_reply(conn, XS_WATCH_EVENT, buffer, len);
	talloc_free(buffer);
}

static struct watch **find_watches(const char *node, bool recurse,
				   unsigned int *num)
{
	struct watch *i;
	struct watch **ret = NULL;

	*num = 0;

	/* We include children too if this is an rm. */
	list_for_each_entry(i, &watches, list) {
		if (is_child(node, i->node) ||
		    (recurse && is_child(i->node, node))) {
			(*num)++;
			ret = talloc_realloc(node, ret, struct watch *, *num);
			ret[*num - 1] = i;
		}
	}
	return ret;
}

/* FIXME: we fail to fire on out of memory.  Should drop connections. */
void fire_watches(struct transaction *trans, const char *node, bool recurse)
{
	struct watch **watches;
	struct watch_event *event;
	unsigned int num_watches;

	/* During transactions, don't fire watches. */
	if (trans)
		return;

	watches = find_watches(node, recurse, &num_watches);
	if (!watches)
		return;

	/* Create and fill in info about event. */
	event = talloc(talloc_autofree_context(), struct watch_event);
	event->node = talloc_strdup(event, node);

	/* Tie event to this watch. */
	event->watches = watches;
	talloc_steal(event, watches);
	event->num_watches = num_watches;
	event->recurse = recurse;
	list_add_tail(&event->list, &watches[0]->events);

	/* Warn if not finished after thirty seconds. */
	gettimeofday(&event->timeout, NULL);
	event->timeout.tv_sec += 30;

	/* If connection not doing anything, queue this. */
	if (!watches[0]->conn->out)
		queue_next_event(watches[0]->conn);
}

/* We're done with this event: see if anyone else wants it. */
static void move_event_onwards(struct watch_event *event)
{
	list_del(&event->list);

	event->num_watches--;
	event->watches++;
	if (!event->num_watches) {
		talloc_free(event);
		return;
	}

	list_add_tail(&event->list, &event->watches[0]->events);

	/* If connection not doing anything, queue this. */
	if (!event->watches[0]->conn->out)
		queue_next_event(event->watches[0]->conn);
}

static void remove_watch_from_events(struct watch *dying_watch)
{
	struct watch *watch;
	struct watch_event *event;
	unsigned int i;

	list_for_each_entry(watch, &watches, list) {
		list_for_each_entry(event, &watch->events, list) {
			for (i = 0; i < event->num_watches; i++) {
				if (event->watches[i] != dying_watch)
					continue;

				assert(i != 0);
				memmove(event->watches+i,
					event->watches+i+1,
					(event->num_watches - (i+1))
					* sizeof(struct watch *));
				event->num_watches--;
			}
		}
	}
}

static int destroy_watch(void *_watch)
{
	struct watch *watch = _watch;
	struct watch_event *event;

	/* If we have pending events, pass them on to others. */
	while ((event = list_top(&watch->events, struct watch_event, list)))
		move_event_onwards(event);

	/* Remove from global list. */
	list_del(&watch->list);

	/* Other events which match this watch must be cleared. */
	remove_watch_from_events(watch);

	trace_destroy(watch, "watch");
	return 0;
}

/* We keep watches in priority order. */
static void insert_watch(struct watch *watch)
{
	struct watch *i;

	list_for_each_entry(i, &watches, list) {
		if (i->priority <= watch->priority) {
			list_add_tail(&watch->list, &i->list);
			return;
		}
	}

	list_add_tail(&watch->list, &watches);
}

void shortest_watch_ack_timeout(struct timeval *tv)
{
	struct watch *watch;

	list_for_each_entry(watch, &watches, list) {
		struct watch_event *i;
		list_for_each_entry(i, &watch->events, list) {
			if (!timerisset(&i->timeout))
				continue;
			if (!timerisset(tv) || timercmp(&i->timeout, tv, <))
				*tv = i->timeout;
		}
	}
}	

void check_watch_ack_timeout(void)
{
	struct watch *watch;
	struct timeval now;

	gettimeofday(&now, NULL);
	list_for_each_entry(watch, &watches, list) {
		struct watch_event *i, *tmp;
		list_for_each_entry_safe(i, tmp, &watch->events, list) {
			if (!timerisset(&i->timeout))
				continue;
			if (timercmp(&i->timeout, &now, <)) {
				xprintf("Warning: timeout on watch event %s"
					" token %s\n",
					i->node, watch->token);
				trace_watch_timeout(watch->conn, i->node,
						    watch->token);
				timerclear(&i->timeout);
			}
		}
	}
}

void do_watch(struct connection *conn, struct buffered_data *in)
{
	struct watch *watch;
	char *vec[3];
	bool relative;

	if (get_strings(in, vec, ARRAY_SIZE(vec)) != ARRAY_SIZE(vec)) {
		send_error(conn, EINVAL);
		return;
	}

	relative = !strstarts(vec[0], "/");
	vec[0] = canonicalize(conn, vec[0]);
	if (!check_node_perms(conn, vec[0], XS_PERM_READ)) {
		send_error(conn, errno);
		return;
	}

	watch = talloc(conn, struct watch);
	watch->node = talloc_strdup(watch, vec[0]);
	watch->token = talloc_strdup(watch, vec[1]);
	watch->conn = conn;
	watch->priority = strtoul(vec[2], NULL, 0);
	watch->relative = relative;
	INIT_LIST_HEAD(&watch->events);

	insert_watch(watch);
	talloc_set_destructor(watch, destroy_watch);
	trace_create(watch, "watch");
	send_ack(conn, XS_WATCH);
}

void do_watch_ack(struct connection *conn, const char *token)
{
	struct watch_event *event;

	if (!token) {
		send_error(conn, EINVAL);
		return;
	}

	if (!conn->waiting_for_ack) {
		send_error(conn, ENOENT);
		return;
	}

	event = list_top(&conn->waiting_for_ack->events,
			 struct watch_event, list);
	assert(event->watches[0] == conn->waiting_for_ack);
	if (!streq(conn->waiting_for_ack->token, token)) {
		/* They're confused: this will cause us to send event again */
		conn->waiting_for_ack = NULL;
		send_error(conn, EINVAL);
		return;
	}

	move_event_onwards(event);
	conn->waiting_for_ack = NULL;
	send_ack(conn, XS_WATCH_ACK);
}

void do_unwatch(struct connection *conn, struct buffered_data *in)
{
	struct watch *watch;
	char *node, *vec[2];

	if (get_strings(in, vec, ARRAY_SIZE(vec)) != ARRAY_SIZE(vec)) {
		send_error(conn, EINVAL);
		return;
	}

	/* We don't need to worry if we're waiting for an ack for the
	 * watch we're deleting: conn->waiting_for_ack was reset by
	 * this command in consider_message anyway. */
	node = canonicalize(conn, vec[0]);
	list_for_each_entry(watch, &watches, list) {
		if (watch->conn != conn)
			continue;

		if (streq(watch->node, node) && streq(watch->token, vec[1])) {
			talloc_free(watch);
			send_ack(conn, XS_UNWATCH);
			return;
		}
	}
	send_error(conn, ENOENT);
}

#ifdef TESTING
void dump_watches(struct connection *conn)
{
	struct watch *watch;
	struct watch_event *event;

	/* Find first watch with an event. */
	list_for_each_entry(watch, &watches, list) {
		if (watch->conn != conn)
			continue;

		printf("    watch on %s token %s prio %i\n",
		       watch->node, watch->token, watch->priority);
		list_for_each_entry(event, &watch->events, list)
			printf("        event: %s\n", event->node);
	}
}
#endif
