#define _GNU_SOURCE
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

static volatile sig_atomic_t stop_requested;

static void stop_handler(int sig)
{
    (void)sig;
    stop_requested = 1;
}

static void sleep_us(unsigned int usec)
{
    struct timespec delay = {
        .tv_sec = usec / 1000000,
        .tv_nsec = (long)(usec % 1000000) * 1000L,
    };

    while (!stop_requested && nanosleep(&delay, &delay) < 0 && errno == EINTR)
        ;
}

int main(int argc, char **argv)
{
    pid_t pid;
    unsigned int run_us;
    unsigned int sleep_interval_us;

    if (argc != 4) {
        fprintf(stderr, "usage: %s PID RUN_US SLEEP_US\n", argv[0]);
        return 2;
    }

    pid = (pid_t)strtol(argv[1], NULL, 0);
    run_us = (unsigned int)strtoul(argv[2], NULL, 0);
    sleep_interval_us = (unsigned int)strtoul(argv[3], NULL, 0);
    if (pid <= 1 || !run_us || !sleep_interval_us)
        return 2;

    signal(SIGINT, stop_handler);
    signal(SIGTERM, stop_handler);
    while (!stop_requested) {
        if (kill(pid, SIGCONT) < 0)
            break;
        sleep_us(run_us);
        if (kill(pid, SIGSTOP) < 0)
            break;
        sleep_us(sleep_interval_us);
    }
    (void)kill(pid, SIGCONT);
    return 0;
}
