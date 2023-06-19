#include <stdlib.h>
#include <stdio.h>

#include "http.h"
#include "util.h"


void http_request_foo(http_request_t *req, http_response_t *res)
{
    http_header_t *h;
    list_t *iter;
    net_buf_t *buf;

    // http header
    http_res_set_status(res, 200, "OK");
    http_res_add_header(res, "Server", "libnet/0.0.1");
    http_res_add_header(res, "Content-Type", "application/json");

    // http body
    buf = net_buf_create(0);

    net_buf_append(buf, "{");
    LIST_FOR_EACH(&req->headers, iter)
    {
        h = container_of(iter, http_header_t, node);
        net_buf_append(buf, "\"%s\": \"%s\"", h->header_name, h->header_value);
        if (!list_is_tail(&req->headers, iter)) net_buf_append(buf, ",");
    }
    net_buf_append(buf, "}");

    http_res_set_body(res, buf);
}


void http_request_bar(http_request_t *req, http_response_t *res)
{
    net_buf_t *buf;

    // http header - using default status code
    http_res_add_header(res, "Server", "bar/0.0.1");
    http_res_add_header(res, "Content-Type", "application/json");

    // http body
    buf = net_buf_create(0);

    net_buf_append(buf, "{");
    net_buf_append(buf, "\"bar\": \"foo\"");
    net_buf_append(buf, "}");

    http_res_set_body(res, buf);
}


void http_request_def(http_request_t *req, http_response_t *res)
{
    net_buf_t *buf;

    // http header - using default status code and Server header
    http_res_add_header(res, "Content-Type", "application/json");

    // http body
    buf = net_buf_create(0);

    net_buf_append(buf, "{");
    net_buf_append(buf, "\"bar\": \"foo\"");
    net_buf_append(buf, "}");

    http_res_set_body(res, buf);
}


int main(int argc, char *argv[])
{
    http_server_t *httpd;
    char *host, *port;

    if (argc < 3)
    {
        printf("usage: %s host port\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    host = argv[1];
    port = argv[2];

    net_log_level(LOG_INFO);

    httpd = http_server_init(host, atoi(port));

    http_add_route(httpd, "/foo", http_request_foo);
    http_add_route(httpd, "/bar", http_request_bar);
    http_add_route(httpd, "/def", http_request_def);

    http_server_start(httpd);
}

