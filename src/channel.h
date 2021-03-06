#ifndef CHANNEL_H
#define CHANNEL_H

#include "http.h"

struct connection;

struct channel_user {

	/* int fd; */
	struct connection *cx;

	int keep_connected;
	int free_on_remove;
	char *jsonp;
	int jsonp_len;

	write_function wfun;

	struct channel_user *prev;
	struct channel_user *next;
};

struct channel_message {

	unsigned long long seq; /* sequence number */

	char *data; /* message contents */
	size_t data_len;
};

struct channel {
	char *name;
	size_t name_len;

	unsigned long long seq;

	struct channel_user *user_list;

	struct channel_message *log_buffer;
	int log_pos;
};

void
channel_init() ;

struct channel *
channel_new(const char *name);

void
channel_free(struct channel *);

struct channel *
channel_find(const char *name);

struct channel_user *
channel_new_connection(struct connection *cx, int keep_connected, const char *jsonp, write_function wfun);

void
channel_add_connection(struct channel *channel, struct channel_user *cu);

void
channel_del_connection(struct channel *channel, struct channel_user *cu);

void
channel_write(struct channel *channel, const char *data, size_t data_len);

http_action
channel_catchup_user(struct channel *channel, struct channel_user *cu, unsigned long long seq);

void
channel_clean_idle();

#endif /* CHANNEL_H */

