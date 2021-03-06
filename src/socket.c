#include <arpa/inet.h>
#include <fcntl.h>
#include <syslog.h>
#include <string.h>
#include <unistd.h>
#include <event.h>

#include "socket.h"
#include "websocket.h"
#include "channel.h"
#include "mem.h"

int server_max_cx;
static int server_cur_cx = 0;

extern struct dispatcher_info di;

/**
 * Sets up a non-blocking socket
 */
int
socket_setup(const char *ip, short port) {

	int reuse = 1;
	struct sockaddr_in addr;
	int fd, ret;

	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);

	memset(&(addr.sin_addr), 0, sizeof(addr.sin_addr));
	addr.sin_addr.s_addr = inet_addr(ip);

	/* this sad list of tests could use a Maybe monad... */

	/* create socket */
	fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (-1 == fd) {
		syslog(LOG_ERR, "Socket error: %m\n");
		return -1;
	}

	/* reuse address if we've bound to it before. */
	if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse,
				sizeof(reuse)) < 0) {
		syslog(LOG_ERR, "setsockopt error: %m\n");
		return -1;
	}

	/* set socket as non-blocking. */
	ret = fcntl(fd, F_SETFD, O_NONBLOCK);
	if (0 != ret) {
		syslog(LOG_ERR, "fcntl error: %m\n");
		return -1;
	}

	/* bind */
	ret = bind(fd, (struct sockaddr*)&addr, sizeof(addr));
	if (0 != ret) {
		syslog(LOG_ERR, "Bind error: %m\n");
		return -1;
	}

	/* listen */
	ret = listen(fd, SOMAXCONN);
	if (0 != ret) {
		syslog(LOG_DEBUG, "Listen error: %m\n");
		return -1;
	}

	/* there you go, ready to accept! */
	return fd;
}

struct connection *
cx_new(int fd, struct event_base *base) {
	struct connection *cx;

	/* monitor number of connections */
	if(server_max_cx && server_cur_cx > server_max_cx) {
		return NULL;
	}
	server_cur_cx++;

	cx = rcalloc(sizeof(struct connection), 1);

	cx->fd = fd;
	cx->base = base;
	cx->ev = rmalloc(sizeof(struct event));
	memset(&cx->get, 0, sizeof(cx->get));

	return cx;
}

void
cx_remove(struct connection *cx) {
	close(cx->fd);
	server_cur_cx--;

	if(cx->cu) {
		channel_del_connection(cx->channel, cx->cu);
	}

	if(cx->ev) {
		event_del(cx->ev);
		rfree(cx->ev);
	}

	/* cleanup */
	rfree(cx->headers.host);
	rfree(cx->headers.origin);
	rfree(cx->path);

	rfree(cx->headers.ws1);
	rfree(cx->headers.ws2);
	rfree(cx->post);

	if(cx->wsc) {
		evbuffer_free(cx->wsc->buffer);
		rfree(cx->wsc);
	}

	rfree(cx->get.name);
	rfree(cx->get.data);
	rfree(cx->get.jsonp);
	rfree(cx->get.domain);

	rfree(cx);
}

