#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "list.h"
#include "net.h"
#include "util.h"

// user-defined OnMessage callback.
// steps: decode -> process -> encode
int net_request_process(char *start, size_t size, net_connect_t *c)
{
    char *tail = NULL;
    int parsed_bytes = -1;

    if (size <= 0) return NET_AGAIN;

    // decode
    if ((tail = strstr(start, "\n")))
    {
        // process
        *tail = '\0';
        loginfo("recv data: '%s'\n", start);
        parsed_bytes = tail - start + 1;

        // encode
        net_buf_t *buf = net_buf_create(parsed_bytes + sizeof("== hello x =="));
        buf->pos += snprintf(buf->buf, buf->size, "== hello %s ==\n", start);
        list_append(&c->outbuf, &buf->node);
        net_connection_send(c);

        // return parsed bytes
        return parsed_bytes;
    }
    else {
        // partial data, need more for decode
        logerr("recv partial data.\n");
        return NET_AGAIN;
    }
}

// user-definded OnError callback.
void net_request_error(const char *err_msg)
{
    logerr("req error: %s\n", err_msg);
}

int main(int argc, char *argv[])
{
    net_loop_t *loop;
    net_server_t *server;
    char *host, *port;

    if (argc < 3)
    {
        printf("usage: %s host port\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    net_log_level(LOG_INFO);

    loop = net_loop_init(EPOLL_SIZE);
    if (!loop)
    {
        logerr("init loop failed.\n");
        exit(EXIT_FAILURE);
    }

    host = argv[1];
    port = argv[2];
    server = net_server_init(loop, host, atoi(port));
    if (!server)
    {
        logerr("init server failed.\n");
        exit(EXIT_FAILURE);
    }

    net_server_set_message_callback(server, net_request_process);
    net_server_set_error_callback(server, net_request_error);

    net_loop_start(loop);
}
