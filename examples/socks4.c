#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "list.h"
#include "util.h"
#include "net.h"

#define DEFAULT_PORT 1080

struct end_point {
    char host[INET_ADDRSTRLEN];
    int port;
};


int on_server_msg(char *start, size_t size, net_connect_t *c)
{
    net_buf_t *to_client;
    net_client_t *client = c->client;
    net_connect_t *server_conn = client->user_data;

    if (size <= 0) return NET_AGAIN;

    // server -> socks4 -> client
    to_client = net_buf_create(0);
    net_buf_copy(to_client, start, size);
    list_append(&server_conn->outbuf, &to_client->node);

    logdebug("upstream -> client, size: %ld\n", size);
    net_connection_send(server_conn);
    if (server_conn->err) return NET_ERR;

    return size;
}


void on_server_close(net_connect_t *c, void *arg)
{
    logdebug("server side closed\n");
    net_connect_t *client_conn = arg;
    net_connection_close(client_conn);
}


void on_client_close(net_connect_t *c, void *arg)
{
    logdebug("client side closed\n");

    // due to socks protocol parse failed, server_conn may be null.
    net_connect_t *server_conn = c->data;
    if (server_conn) net_connection_close(server_conn);
}


int socks4_parse(char *start, size_t size, struct end_point *peer)
{
    char *end, ver, cmd;
    struct in_addr addr;
    in_port_t port;
    int ret;

    end = util_strchr(start, '\0', size);
    if (end)
    {
        ver = start[0];
        cmd = start[1];

        if (ver == 4 && cmd == 1)
        {
            memcpy(&port,        start+2, 2);
            memcpy(&addr.s_addr, start+4, 4);

            inet_ntop(AF_INET, &addr, peer->host, sizeof(peer->host));
            peer->port = ntohs(port);

            ret = NET_OK;
        }
        else {
            logerr("unknown socks4 request, ver:%c, cmd:%c\n", ver, cmd);
            ret = NET_ERR;
        }
    }
    else {
        ret = NET_AGAIN;
    }

    return ret;
}


void socks4_reply(net_connect_t *c, void *arg)
{
    char buf[8];
    in_port_t port;
    struct in_addr addr;
    net_buf_t *reply;
    net_client_t *local_client = c->client;
    net_connect_t *peer_client = local_client->user_data;

    buf[0] = '\000';
    buf[1] = '\x5a';

    port = htons(local_client->peer_port);
    memcpy(buf+2, &port, 2);

    inet_pton(AF_INET, local_client->peer_host, &addr);
    memcpy(buf+4, &addr.s_addr, 4);

    // socks4 reply to peer-client
    reply = net_buf_create(0);
    net_buf_copy(reply, buf, sizeof(buf));
    list_append(&peer_client->outbuf, &reply->node);
    net_connection_send(peer_client);
}

int on_client_msg(char *start, size_t size, net_connect_t *c)
{
    int ret, parsed_bytes;
    struct end_point peer_server;
    net_buf_t *to_server;
    net_client_t *client;
    net_connect_t *client_conn = c->data;

    if (size <= 0) return NET_AGAIN;
    parsed_bytes = size;

    if (!client_conn)
    {
        ret = socks4_parse(start, size, &peer_server);
        if (ret == NET_OK)
        {
            client = net_client_init(c->loop,
                    peer_server.host, peer_server.port);

            // bind client with server
            net_client_set_user_data(client, c);

            net_client_set_connection_callback(client, socks4_reply, NULL);
            net_client_set_response_callback(client, on_server_msg);
            net_client_set_close_callback(client, on_server_close, c);

            // bind server with client
            c->data = client->conn;
        }
        else if (ret == NET_AGAIN) {
            parsed_bytes = 0;
        }
        else {
            logerr("socks4_parse: unreachable!\n");
            parsed_bytes = NET_ERR;
        }
    }
    else {
        // client -> socks4 -> server
        to_server = net_buf_create(0);
        net_buf_copy(to_server, start, size);
        list_append(&client_conn->outbuf, &to_server->node);

        logdebug("client -> upstream, size: %ld\n", size);
        net_connection_send(client_conn);
    }

    return parsed_bytes;
}


int main(int argc, char *argv[])
{
    net_server_t *server;
    net_loop_t *loop;
    int port;

    if (argc < 1 || argc > 2)
    {
        printf("usage: %s [port]\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    if (argc == 2) port = atoi(argv[1]);
    else port = DEFAULT_PORT;

    net_log_level(LOG_INFO);

    loop = net_loop_init(EPOLL_SIZE);
    if (!loop)
    {
        logerr("init loop failed.\n");
        exit(EXIT_FAILURE);
    }

    // TODO: tcp-nodelay flag for both sides connection.
    server = net_server_init(loop, "0.0.0.0", port);
    if (!server)
    {
        logerr("init server failed.\n");
        exit(EXIT_FAILURE);
    }

    net_server_set_message_callback(server, on_client_msg);
    net_server_set_close_callback(server, on_client_close, NULL);

    net_loop_start(loop);
}
