/*
 * this is a special http-client impl, which is meant to close connection
 * exactly after peer sleep on write-ready event, forcing peer generate
 * CLOSE-WAIT tcp conns. Given this,  we can have client actively close
 * connection, simulating 499 situation, usually observed on server peer.
 *
 * for this to come true, we need libnet core do some adjustments:
 *  1. set SO_RCVBUF to minimal value, say 4096
 *  2. set REQ_SIZE to just one byte
 *
 * so, this broken-client will stop receiving data from peer after reading
 * just one byte data, therefore server peer will block in writable event.
 * then client close connection, server peer should observer 'client broken'
 * or 'client write error' like errors.
 * */

#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <sys/time.h>

#include "net.h"
#include "util.h"

typedef struct {
    int in_flight;
    int max_flight;
    int req_cnt;
    int res_cnt;
    int req_max;
    struct timeval start_time;
    struct timeval stop_time;
} req_stats;

// const char *uri[4] = {"json", "json1", "json3", "json5"};
// const char *uri[4] = {"5K", "5K3", "5K4", "5K6"};
const char *uri[4] = {"1M", "1Mx", "1Mxxxx", "1Mxxxxx"};

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

        int i = random() % 4;
        net_buf_append(req, "GET /%s HTTP/1.1\r\n", uri[i]);

        net_buf_append(req, "Host: libnet\r\n");
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

    // if (util_strchr(start, '}', size))
    if (1)
    {
        stats->res_cnt++;

        // reach max, terminate whole loop.
        if (stats->res_cnt >= stats->req_max)
        {
            gettimeofday(&stats->stop_time, NULL);
            net_loop_stop(c->loop);
        }

        net_connection_set_close(c);
        stats->in_flight--;

        // reach max req
        if (stats->req_cnt >= stats->req_max) goto done;

        // reach max flight, take a break
        if (stats->in_flight >= stats->max_flight) goto done;

        // launch next client.
        net_client_t *next_client = net_client_init(client->loop,
                client->peer_host, client->peer_port);
        if (next_client == NULL) goto done;
        else stats->in_flight++;

        net_client_set_user_data(next_client, stats);
        net_client_set_connection_callback(next_client, send_req, stats);
        net_client_set_response_callback(next_client, process_res);
        net_client_set_done_callback(next_client, next_req);
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

    // reach max flight, take a break.
    if (stats->in_flight >= stats->max_flight) return;

    // launch next client.
    net_client_t *next_client = net_client_init(client->loop,
            client->peer_host, client->peer_port);
    if (next_client == NULL) return;
    else stats->in_flight++;

    net_client_set_user_data(next_client, stats);
    net_client_set_connection_callback(next_client, send_req, stats);
    net_client_set_response_callback(next_client, process_res);
    net_client_set_done_callback(next_client, next_req);
}


int main(int argc, char *argv[])
{
    net_loop_t *loop;
    net_client_t *client;
    char *host, *port;

    req_stats stats;
    bzero(&stats, sizeof(stats));
    gettimeofday(&stats.start_time, NULL);

    if (argc != 5)
    {
        printf("usage: %s host port max_req max_flight\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    stats.req_max = atoi(argv[3]);
    stats.max_flight = atoi(argv[4]);

    net_log_level(LOG_INFO);

    loop = net_loop_init(10000);
    if (!loop)
    {
        logerr("init loop failed.\n");
        exit(EXIT_FAILURE);
    }

    host = argv[1];
    port = argv[2];
    client = net_client_init(loop, host, atoi(port));
    stats.in_flight++;

    net_client_set_user_data(client, &stats);
    net_client_set_connection_callback(client, send_req, &stats);
    net_client_set_response_callback(client, process_res);
    net_client_set_done_callback(client, next_req);

    net_loop_set_stop_callback(loop, summary, &stats);
    net_loop_start(loop);
}
