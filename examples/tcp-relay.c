#include <stdio.h>
#include <stdlib.h>

#include "net.h"
#include "util.h"

struct end_point {
    char *host;
    int port;
};


int on_client_msg(char *start, size_t size, net_connect_t *c)
{
    net_buf_t *to_server;
    net_connect_t *client_conn = c->data;

    if (size <= 0) return NET_AGAIN;

    // client -> relay -> server
    to_server = net_buf_create(0);
    net_buf_copy(to_server, start, size);
    list_append(&client_conn->outbuf, &to_server->node);
    net_connection_send(client_conn);

    return size;
}


int on_server_msg(char *start, size_t size, net_connect_t *c)
{
    net_buf_t *to_client;
    net_client_t *client = c->client;
    net_connect_t *server_conn = client->user_data;

    if (size <= 0) return NET_AGAIN;

    // server -> relay -> client
    to_client = net_buf_create(0);
    net_buf_copy(to_client, start, size);
    list_append(&server_conn->outbuf, &to_client->node);
    net_connection_send(server_conn);

    return size;
}


void on_server_close(net_connect_t *c, void *arg)
{
    loginfo("[conn: %p, fd: %d] server side closed\n", c, c->io_watcher.fd);
    net_connect_t *client_conn = arg;
    net_connection_close(client_conn);
}


void on_client_close(net_connect_t *c, void *arg)
{
    loginfo("[conn: %p, fd: %d] client side closed\n", c, c->io_watcher.fd);
    net_connect_t *server_conn = c->data;
    net_connection_close(server_conn);
}


void relay_accept_cb(net_connect_t *c, void *arg)
{
    struct end_point *peer = arg;
    net_client_t *client;

    client = net_client_init(c->loop, peer->host, peer->port);
    net_client_set_user_data(client, c); // bind client with server
    net_client_set_response_callback(client, on_server_msg);
    net_client_set_close_callback(client, on_server_close, c);

    c->data = client->conn; // bind server with client
}


int main(int argc, char *argv[])
{
    struct end_point peer;
    net_server_t *server;
    net_loop_t *loop;

    if (argc != 3)
    {
        printf("usage: %s host port\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    peer.host = argv[1];
    peer.port = atoi(argv[2]);

    net_log_level(LOG_INFO);

    loop = net_loop_init(EPOLL_SIZE);
    if (!loop)
    {
        logerr("init loop failed.\n");
        exit(EXIT_FAILURE);
    }

    server = net_server_init(loop, "127.0.0.1", 8888);
    if (!server)
    {
        logerr("init server failed.\n");
        exit(EXIT_FAILURE);
    }

    net_server_set_message_callback(server, on_client_msg);
    net_server_set_accept_callback(server, relay_accept_cb, &peer);
    net_server_set_close_callback(server, on_client_close, NULL);

    net_loop_start(loop);
}
