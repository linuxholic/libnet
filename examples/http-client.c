#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <sys/time.h>

#include "net.h"
#include "util.h"

#define CONCURRENT_CONS 10

typedef struct {
    int conns;
    int req_cnt;
    int res_cnt;
    int req_max;
    struct timeval start_time;
    struct timeval stop_time;
} req_stats;


/* refer to:
 * https://www.gnu.org/software/libc/manual/html_node/Elapsed-Time.html#Elapsed-Time
 *
 * Subtract the ‘struct timeval’ values X and Y,
   storing the result in RESULT.
   Return 1 if the difference is negative, otherwise 0. */

int timeval_subtract(struct timeval *result, struct timeval *x, struct timeval *y)
{
    /* Perform the carry for the later subtraction by updating y. */
    if (x->tv_usec < y->tv_usec) {
        int nsec = (y->tv_usec - x->tv_usec) / 1000000 + 1;
        y->tv_usec -= 1000000 * nsec;
        y->tv_sec += nsec;
    }
    if (x->tv_usec - y->tv_usec > 1000000) {
        int nsec = (x->tv_usec - y->tv_usec) / 1000000;
        y->tv_usec += 1000000 * nsec;
        y->tv_sec -= nsec;
    }

    /* Compute the time remaining to wait.
       tv_usec is certainly positive. */
    result->tv_sec = x->tv_sec - y->tv_sec;
    result->tv_usec = x->tv_usec - y->tv_usec;

    /* Return 1 if result is negative. */
    return x->tv_sec < y->tv_sec;
}


void summary(net_loop_t *loop, void *arg)
{
    req_stats *stats = arg;
    struct timeval diff;

    timeval_subtract(&diff, &stats->stop_time, &stats->start_time);
    float ms = diff.tv_usec / 1000.0;

    loginfo("========== benchmark statics ============\n");

    loginfo("req_cnt: %d\n", stats->req_cnt);
    loginfo("res_cnt: %d\n", stats->res_cnt);
    loginfo("time elapsed: %lds %.3fms\n", diff.tv_sec, ms);
}


void send_req(net_connect_t *c, void *arg)
{
    net_client_t *client = c->client;
    req_stats *stats = client->user_data;

    // reach max reqs, no more req.
    if (stats->req_cnt >= stats->req_max) return;

    stats->req_cnt++;

    if (!c->err)
    {
        net_buf_t *req = net_buf_create(0);
        net_buf_append(req, "GET /foo HTTP/1.1\r\n");
        net_buf_append(req, "\r\n");
        list_append(&c->outbuf, &req->node);
    }

    // send client req
    net_connection_send(c);
}


// forward declare
void next_req(net_connect_t *c, void *arg);

int process_res(char *start, size_t size, net_connect_t *c)
{
    net_client_t *client = c->client;
    req_stats *stats = client->user_data;

    loginfo("[conn: %p, fd: %d] recv response, size: %ld\n",
            c, c->io_watcher.fd, size);

    if (util_strchr(start, '}', size))
    {
        stats->res_cnt++;

        // reach max, terminate whole loop.
        if (stats->res_cnt >= stats->req_max)
        {
            gettimeofday(&stats->stop_time, NULL);
            net_loop_stop(c->loop);
            goto done;
        }

        if (client->keep_alive)
        {
            send_req(c, NULL);
            goto done;
        }

        // terminate current client.
        net_connection_set_close(c);
        stats->conns--;

        // reach max conns, no more req.
        if (stats->conns >= CONCURRENT_CONS) goto done;

        // launch next client.
        net_client_t *next_client = net_client_init(client->loop,
                client->peer_host, client->peer_port);

        net_client_set_keep_alive(next_client, client->keep_alive);
        net_client_set_user_data(next_client, stats);
        net_client_set_connection_callback(next_client, send_req, stats);
        net_client_set_response_callback(next_client, process_res);
        net_client_set_done_callback(next_client, next_req);

        stats->conns++;
    }

done:
    return size;
}


void next_req(net_connect_t *c, void *arg)
{
    net_client_t *client = c->client;
    req_stats *stats = client->user_data;

    // reach max reqs, no more req.
    if (stats->req_cnt >= stats->req_max) return;

    // reach max conns, no more req.
    if (stats->conns >= CONCURRENT_CONS) return;

    // launch next client.
    net_client_t *next_client = net_client_init(client->loop,
            client->peer_host, client->peer_port);

    net_client_set_keep_alive(next_client, client->keep_alive);
    net_client_set_user_data(next_client, stats);
    net_client_set_connection_callback(next_client, send_req, stats);
    net_client_set_response_callback(next_client, process_res);
    net_client_set_done_callback(next_client, next_req);

    stats->conns++;
}


int main(int argc, char *argv[])
{
    net_loop_t *loop;
    net_client_t *client;
    char *host, *port;
    int keep_alive;

    req_stats stats;
    bzero(&stats, sizeof(stats));
    gettimeofday(&stats.start_time, NULL);

    if (argc < 4)
    {
        printf("usage: %s host port max_req [-k]\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    if (argc > 4 && !strcmp(argv[4], "-k")) keep_alive = 1;
    else keep_alive = 0;

    stats.req_max = atoi(argv[3]);

    net_log_level(LOG_INFO);

    loop = net_loop_init(EPOLL_SIZE);
    if (!loop)
    {
        logerr("init loop failed.\n");
        exit(EXIT_FAILURE);
    }

    host = argv[1];
    port = argv[2];
    client = net_client_init(loop, host, atoi(port));

    net_client_set_keep_alive(client, keep_alive);
    net_client_set_user_data(client, &stats);
    net_client_set_connection_callback(client, send_req, &stats);
    net_client_set_response_callback(client, process_res);
    net_client_set_done_callback(client, next_req);

    stats.conns++;

    net_loop_set_stop_callback(loop, summary, &stats);
    net_loop_start(loop);
}
