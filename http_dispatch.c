#include <sys/queue.h>

#include <event.h>
#include <evhttp.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>

#include "http_dispatch.h"
#include "user.h"
#include "connection.h"
#include "channel.h"
#include "server.h"

static void
send_reply(struct evhttp_request *ev, int error) {
	struct evbuffer *buffer = evbuffer_new();
	
	if(error == 200) {
		evbuffer_add_printf(buffer, "ok\n");
	} else {
		evbuffer_add_printf(buffer, "fail\n");
	}

	switch(error) {
		case 404:
			evhttp_send_reply(ev, 404, "Not found", buffer);
			break;

		case 403:
			evhttp_send_reply(ev, 403, "Forbidden", buffer);
			break;

		case 200:
			evhttp_send_reply(ev, 200, "OK", buffer);
			break;

	}
	evbuffer_free(buffer);
}



void
http_dispatch_meta_authenticate(struct evhttp_request *ev, void *data) {
	(void)data;

	struct evkeyvalq get;
	const char *uid;
	const char *sid;
	const char *uri;
	struct p_user *user;
	long l_uid;
	int success = 0;
	
	printf("\nMETA: %s\n", __FUNCTION__);

	/* get (uid, sid) parameters */
	uri = evhttp_request_uri(ev);
	TAILQ_INIT(&get);
	evhttp_parse_query (uri, &get);

	uid = evhttp_find_header (&get, "uid");
	sid = evhttp_find_header (&get, "sid");

	printf("uid=[%s], sid=[%s]\n", uid, sid);

	l_uid = atol(uid);
	if(NULL == user_find(l_uid)) {
		user = user_new(l_uid, sid);
		if(user) {
			user_save(user);
			success = 1;
		} 
	}

	evhttp_clear_headers(&get);

	// reply
	if(success) {
		send_reply(ev, 200);
	} else {
		send_reply(ev, 403);
	}
}

void
http_dispatch_meta_connect(struct evhttp_request *ev, void *arg) {

	struct thread_info *self = arg;
	struct evkeyvalq get;
	const char *uid;
	const char *sid;
	const char *uri;
	int success = 0;
	struct p_user *user;
	long l_uid;
	struct p_connection *connection = NULL;

	printf("\nMETA: %s (thread id=%d)\n", __FUNCTION__, self->id);

	/* get (uid, sid) parameters */
	uri = evhttp_request_uri(ev);
	TAILQ_INIT(&get);
	evhttp_parse_query (uri, &get);

	uid = evhttp_find_header (&get, "uid");
	sid = evhttp_find_header (&get, "sid");

	if(!uid || !sid) {
		success = 0;
	} else {
		l_uid = atol(uid);
		user = user_find(l_uid);
		/* if user by uid is found, check sid */
		if(user && !strncmp(sid, user->sid, strlen(sid))) { 
			success = 1;
			printf("uid=[%s], sid=[%s]\n", uid, sid);
		}
	}

	if(success) {
		/* register the EV with the user */
		/* TODO: lock user globally. */
		connection = connection_new(self->pipe[1], ev);
		connection->user = user;
		connection->next = user->connections;
		user->connections = connection;
		printf("user=%p, user->connections=%p\n", user, user->connections);
	} else {
		send_reply(ev, 403);
	}

	/* cleanup */ 
	evhttp_clear_headers(&get);
	if(success) {
		struct evbuffer *padding;
		padding = evbuffer_new();

		/* send whitespace padding */
		evhttp_send_reply_start(ev, 200, "OK");
		evbuffer_add_printf(padding, "\n");
		evhttp_send_reply_chunk(ev, padding);
		evbuffer_free(padding);

		/* set "connection closed" callback */
		evhttp_connection_set_closecb(ev->evcon, 
				http_dispatch_close, connection);
	}
}

/**
 * GET: name, data
 */
void
http_dispatch_meta_publish(struct evhttp_request *ev, void *nil) {
	(void)nil;

	struct evkeyvalq get;
	const char *uri;
	const char *name, *data;
	struct p_channel *channel;
	struct p_channel_user *cu;
	struct p_connection *connection;
	struct p_message *message;

	printf("\nMETA: %s\n", __FUNCTION__);

	/* get (uid, sid) parameters */
	uri = evhttp_request_uri(ev);
	TAILQ_INIT(&get);
	evhttp_parse_query (uri, &get);

	name = evhttp_find_header(&get, "name");
	data = evhttp_find_header(&get, "data");
	channel = channel_find(name);

	if(NULL == channel) {
		send_reply(ev, 403);
		return;
	}


	for(cu = channel->users; cu; cu = cu->next) {

		struct p_user *user = user_find(cu->uid);

		if(NULL == user) {
			continue;
		}

		/* locking user here */
		pthread_mutex_lock(&(user->lock));
			/*printf("publish locked user %ld\n", user->uid);*/
			for(connection = user->connections; connection; 
					connection = connection->next) {
				/*
				printf("Send reply %s in pipe %d\n", data, connection->fd);
				*/

				message = message_new(data, 1+strlen(data));
				if(connection->inbox_last) {
					connection->inbox_last->next = message;
					connection->inbox_last = message;
				} else {
					connection->inbox_first = message;
					connection->inbox_last = message;
				}

				write(connection->fd, &(user->uid), sizeof(user->uid));
			}
		pthread_mutex_unlock(&(user->lock));
		/*printf("publish unlocked user %ld\n", user->uid);*/
		
	}

	send_reply(ev, 200);
}

void
http_dispatch_meta_subscribe(struct evhttp_request *ev, void *data) {
	(void)data;

	struct evkeyvalq get;
	const char *uri;
	const char *name, *uid, *sid;
	struct p_channel *channel;


	printf("\nMETA: %s\n", __FUNCTION__);

	/* get (uid, sid) parameters */
	uri = evhttp_request_uri(ev);
	TAILQ_INIT(&get);
	evhttp_parse_query (uri, &get);

	name = evhttp_find_header(&get, "name");
	uid = evhttp_find_header(&get, "uid");
	sid = evhttp_find_header(&get, "sid");

	channel = channel_find(name);

	if(NULL != uid && NULL != sid) {
		long l_uid;
		struct p_user *user;

		l_uid = atol(uid);
		user = user_find(l_uid);
		/* if user by uid is found, check sid */
		if(user && !strncmp(sid, user->sid, strlen(sid))) { 
			if(NULL == channel) {
				send_reply(ev, 404);
			} else {
	
				/* locking user here */
				pthread_mutex_lock(&(user->lock));
				if(channel_has_user(channel, user)) {
					send_reply(ev, 403);
				} else {
					send_reply(ev, 200);
					printf("subscribe user %s (sid %s) to channel %s\n",
							uid, sid, name);
					channel_add_user(channel, user);
				}
				pthread_mutex_unlock(&(user->lock));

			}
		} else {
			send_reply(ev, 403);
		}
	} else {
		send_reply(ev, 403);
	}

	/* cleanup */ 
	evhttp_clear_headers(&get);

	
}

void
http_dispatch_meta_unsubscribe(struct evhttp_request *ev, void *data) {
	(void)ev;
	(void)data;

	printf("\nMETA: %s\n", __FUNCTION__);
}

/**
 * GET: name, key
 */
void
http_dispatch_meta_newchannel(struct evhttp_request *ev, void *data) {
	(void)data;

	struct evkeyvalq get;
	const char *uri;
	const char *name;
	const char *key;


	printf("\nMETA: %s\n", __FUNCTION__);

	/* get (uid, sid) parameters */
	uri = evhttp_request_uri(ev);
	TAILQ_INIT(&get);
	evhttp_parse_query (uri, &get);

	name = evhttp_find_header(&get, "name");
	key = evhttp_find_header(&get, "key");


	printf("name=[%s], key=[%s]\n", name, key);

	if(NULL == channel_find(name)) {
		channel_new(name);
		send_reply(ev, 200);
	} else {
		send_reply(ev, 403);
	}

	/* cleanup */ 
	evhttp_clear_headers(&get);
}


void
http_dispatch_close(struct evhttp_connection *evcon, void *data) {
	struct p_connection *cx_del = data, *cx_prev = NULL;
	struct p_user *user = cx_del->user;

	(void)evcon;

	printf("\nCLOSE: BYE user %ld\n", user->uid);

	pthread_mutex_lock(&user->lock);
		for(cx_prev = user->connections; cx_prev; cx_prev = cx_prev->next) {
			if(cx_prev && cx_prev->next == cx_del) {
				cx_prev->next = cx_prev->next->next;
				break;
			}
		}
		connection_free(cx_del);
	pthread_mutex_unlock(&user->lock);
}


