#include <stdio.h>
#include <stdlib.h>

#include "net.h"

void on_timer(net_timer_t *t)
{
    static int n = 0;

    loginfo("repeat timer: %d\n", ++n);

    if (n == 6)
    {
        loginfo("repeat timer is good.\n");
        net_timer_reset(t, 0, 0);
        return;
    }
}

void on_normal_timer(net_timer_t *timer)
{
    static int n = 0;

    loginfo("normal timer trigger: %d\n", ++n);

    if (n == 5)
    {
        loginfo("reset timer is good.\n");
        net_timer_reset(timer, 0, 0);
        net_loop_stop(timer->loop);
    }
}

void on_reset_timer(net_timer_t * timer)
{
    net_timer_t *nt = net_timer_data(timer);
    net_timer_reset(nt, 2, 2);
}

int main(int argc, char *argv[])
{
    net_loop_t *loop;
    net_timer_t *timer;
    int init;
    int interval;

    if (argc == 1)
    {
        init = 2;
        interval = 1;
    }
    else if (argc == 2)
    {
        init = atoi(argv[1]);
        interval = 0;
    }
    else if (argc == 3)
    {
        init = atoi(argv[1]);
        interval = atoi(argv[2]);
    }
    else {
        printf("usage: %s [init] [interval]\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    net_log_level(LOG_INFO);
    loginfo("init: %d, interval: %d\n", init, interval);

    loop = net_loop_init(1024);

    timer = net_timer_init(loop, init, interval);
    if (timer == NULL)
    {
        logerr("net_timer_init failed.\n");
        exit(EXIT_FAILURE);
    }
    net_timer_start(timer, on_timer, NULL);

    net_timer_t *normal_timer = net_timer_init(loop, 12, 2);
    net_timer_t *reset_timer = net_timer_init(loop, 12 + 2 + 2 + 1, 0);
    if (normal_timer == NULL || reset_timer == NULL)
    {
        logerr("net_timer_init failed.\n");
        exit(EXIT_FAILURE);
    }
    net_timer_start(normal_timer, on_normal_timer, NULL);
    net_timer_start(reset_timer, on_reset_timer, normal_timer);

    net_loop_start(loop);

    return 0;
}
