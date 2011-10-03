/*
	Onion HTTP server library
	Copyright (C) 2011 David Moreno Montero

	This library is free software; you can redistribute it and/or
	modify it under the terms of the GNU Lesser General Public
	License as published by the Free Software Foundation; either
	version 3.0 of the License, or (at your option) any later version.

	This library is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
	Lesser General Public License for more details.

	You should have received a copy of the GNU Lesser General Public
	License along with this library; if not see <http://www.gnu.org/licenses/>.
	*/

#include <malloc.h>
#include <errno.h>
#include <sys/epoll.h>
#include <string.h>

#include "log.h"
#include "types.h"
#include "poller.h"

struct onion_poller_t{
	int fd;
	int stop;
	int n;

	struct onion_poller_el_t *head;
};

typedef struct onion_poller_el_t onion_poller_el;

/// Each element of the poll
/// @private
struct onion_poller_el_t{
	int fd;
	void (*f)(void*);
	void *data;
	
	struct onion_poller_el_t *next;
};

/**
 * @short Returns a poller object that helps polling on sockets and files
 * @memberof onion_poller_t
 *
 * This poller is implemented through epoll, but other implementations are possible 
 */
onion_poller *onion_poller_new(int n){
	onion_poller *p=malloc(sizeof(onion_poller));
	p->fd=epoll_create(n);
	if (p->fd < 0){
		ONION_ERROR("Error creating the poller. %s", strerror(errno));
		free(p);
		return NULL;
	}
	p->stop=0;
	p->head=NULL;
	p->n=0;

	return p;
}

/// @memberof onion_poller_t
void onion_poller_free(onion_poller *p){
	ONION_WARNING("No onion_poller_free yet!");
}

/**
 * @short Adds a file descriptor to poll.
 * @memberof onion_poller_t
 *
 * When new data is available (read/write/event) the given function
 * is called with that data.
 */
int onion_poller_add(onion_poller *poller, int fd, void (*f)(void*), void *data){
	ONION_DEBUG0("Adding fd %d/%d for polling", fd, poller->n);

	struct epoll_event ev;
	ev.events=EPOLLIN | EPOLLOUT | EPOLLRDHUP;
	ev.data.fd=fd;
	if (epoll_ctl(poller->fd, EPOLL_CTL_ADD, fd, &ev) < 0){
		ONION_ERROR("Error add descriptor to listen to. %s", strerror(errno));
		return 1;
	}
	onion_poller_el *nel=malloc(sizeof(onion_poller_el));
	nel->fd=fd;
	nel->f=f;
	nel->data=data;
	nel->next=NULL;

	if (poller->head){
		onion_poller_el *next=poller->head;
		while (next->next)
			next=next->next;
		next->next=nel;
	}
	else
		poller->head=nel;

	poller->n++;

	return 0;
}

/**
 * @short Removes a file descriptor, and all related callbacks from the listening queue
 * @memberof onion_poller_t
 */
int onion_poller_remove(onion_poller *poller, int fd){
	ONION_DEBUG0("Trying to remove fd %d/%d", fd, poller->n);
	onion_poller_el *el=poller->head;
	if (el && el->fd==fd){
		ONION_DEBUG0("Removed from head");
		poller->head=el->next;
		free(el);
		poller->n--;
		return 0;
	}
	while (el->next){
		if (el->next->fd==fd){
			ONION_DEBUG0("Removed from tail");
				onion_poller_el *t=el->next;
			el->next=t->next;
			free(t);
			poller->n--;
			return 0;
		}
		el=el->next;
	}
	
	ONION_WARNING("Trying to remove unknown fd from poller %d", fd);
	return 0;
}

// Max of events per loop. If not al consumed for next, so no prob.  right number uses less memory, and makes less calls.
#define MAX_EVENTS 10

/**
 * @short Do the event polling.
 * @memberof onion_poller_t
 *
 * It loops over polling. To exit polling call onion_poller_stop().
 */
void onion_poller_poll(onion_poller *p){
	struct epoll_event event[MAX_EVENTS];
	ONION_DEBUG0("Start poll of fds");

	while (!p->stop){
		int nfds = epoll_wait(p->fd, event, MAX_EVENTS, -1);
		int i;
		for (i=0;i<nfds;i++){
			onion_poller_el *el=p->head;
			while (el && el->fd!=event[i].data.fd)
				el=el->next;
			if (!el){
				ONION_WARNING("Event on an unlistened file descriptor!");
				continue;
			}
			// Call the callback
			ONION_DEBUG0("Calling callback for fd %d", el->fd);
			el->f(el->data);
			ONION_DEBUG0("--");
		}
	}
	p->stop=0;
}

/**
 * @short Marks the poller to stop ASAP
 * @memberof onion_poller_t
 */
void onion_poller_stop(onion_poller *p){
	p->stop=1;
}

