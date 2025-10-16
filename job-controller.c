#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <errno.h>
#include <string.h>

static volatile sig_atomic_t terminate_requested = 0;
static volatile sig_atomic_t restart_requested = 0;
static volatile sig_atomic_t child_died = 0;

static void handle_sigterm(int signo) {
    (void)signo;
    terminate_requested = 1;
}
static void handle_sighup(int signo) {
    (void)signo;
    restart_requested = 1;
}
static void handle_sigchld(int signo) {
    (void)signo;
    child_died = 1;
}

static void worker_sigterm(int signo) {
    (void)signo;
    _exit(0);
}

static void worker_main_loop() {
    struct sigaction sa;
    sa.sa_handler = worker_sigterm;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGTERM, &sa, NULL);

    pid_t pid = getpid();
    int i = 0;
    while (1) {
        printf("[worker %d] heartbeat %d\n", pid, ++i);
        fflush(stdout);

        for (int j = 0; j < 3; ++j) {
            sleep(1);
        }
    }
}

static pid_t spawn_worker() {
    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        return -1;
    }
    if (pid == 0) {
        worker_main_loop();
        _exit(0);
    }
    return pid;
}

static pid_t *pids = NULL;
static int num_workers = 0;

static void allocate_pid_table(int n) {
    if (pids) free(pids);
    pids = calloc(n, sizeof(pid_t));
    if (!pids) {
        perror("calloc");
        exit(EXIT_FAILURE);
    }
    num_workers = n;
}

static void start_workers() {
    for (int i = 0; i < num_workers; ++i) {
        pid_t pid = spawn_worker();
        if (pid == -1) {
            fprintf(stderr, "Failed to spawn worker %d\n", i);
        } else {
            pids[i] = pid;
            printf("[parent] spawned worker %d -> pid %d\n", i, pid);
        }
    }
}

static void stop_workers() {
    for (int i = 0; i < num_workers; ++i) {
        if (pids[i] > 0) {
            printf("[parent] sending SIGTERM to pid %d\n", pids[i]);
            kill(pids[i], SIGTERM);
        }
    }
}

static void wait_for_all_workers() {
    int status;
    pid_t w;
    while ((w = waitpid(-1, &status, 0)) > 0) {
        printf("[parent] reaped pid %d (status %d)\n", w, status);
        for (int i = 0; i < num_workers; ++i) {
            if (pids[i] == w) pids[i] = 0;
        }
    }
    if (w == -1 && errno != ECHILD) {
        perror("waitpid");
    }
}

static int reap_children_nonblocking() {
    int reaped = 0;
    int status;
    pid_t w;
    while ((w = waitpid(-1, &status, WNOHANG)) > 0) {
        reaped++;
        printf("[parent] non-blocking reaped pid %d (status %d)\n", w, status);
        int idx = -1;
        for (int i = 0; i < num_workers; ++i) {
            if (pids[i] == w) { idx = i; pids[i] = 0; break; }
        }
        if (!terminate_requested && !restart_requested) {
            pid_t newpid = spawn_worker();
            if (newpid > 0) {
                if (idx != -1) {
                    pids[idx] = newpid;
                    printf("[parent] respawned worker at slot %d -> pid %d\n", idx, newpid);
                } else {
                    for (int i = 0; i < num_workers; ++i) {
                        if (pids[i] == 0) { pids[i] = newpid; printf("[parent] placed respawned pid %d in slot %d\n", newpid, i); break; }
                    }
                }
            } else {
                fprintf(stderr, "[parent] failed to respawn worker after pid %d died\n", w);
            }
        }
    }
    if (w == -1 && errno != ECHILD) {
        perror("waitpid");
    }
    return reaped;
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <num_workers>\n", argv[0]);
        return EXIT_FAILURE;
    }

    int n = atoi(argv[1]);
    if (n <= 0) {
        fprintf(stderr, "num_workers must be > 0\n");
        return EXIT_FAILURE;
    }

    struct sigaction sa_term, sa_hup, sa_chld;
    memset(&sa_term, 0, sizeof(sa_term));
    sa_term.sa_handler = handle_sigterm;
    sigemptyset(&sa_term.sa_mask);
    sa_term.sa_flags = 0; 

    memset(&sa_hup, 0, sizeof(sa_hup));
    sa_hup.sa_handler = handle_sighup;
    sigemptyset(&sa_hup.sa_mask);
    sa_hup.sa_flags = 0;

    memset(&sa_chld, 0, sizeof(sa_chld));
    sa_chld.sa_handler = handle_sigchld;
    sigemptyset(&sa_chld.sa_mask);
    sa_chld.sa_flags = SA_RESTART | SA_NOCLDSTOP;

    if (sigaction(SIGTERM, &sa_term, NULL) == -1) { perror("sigaction TERM"); return EXIT_FAILURE; }
    if (sigaction(SIGINT, &sa_term, NULL) == -1) { perror("sigaction INT"); return EXIT_FAILURE; }
    if (sigaction(SIGHUP, &sa_hup, NULL) == -1) { perror("sigaction HUP"); return EXIT_FAILURE; }
    if (sigaction(SIGCHLD, &sa_chld, NULL) == -1) { perror("sigaction CHLD"); return EXIT_FAILURE; }

    allocate_pid_table(n);
    start_workers();

    printf("[parent] controller pid %d running with %d workers\n", getpid(), n);

    while (!terminate_requested) {
        if (child_died) {
            child_died = 0;
            reap_children_nonblocking();
        }

        if (restart_requested) {
            restart_requested = 0;
            printf("[parent] restart requested: stopping workers...\n");
            stop_workers();
            wait_for_all_workers(); 
            printf("[parent] all workers stopped. restarting...\n");
            start_workers();
            continue; 
        }

        sleep(1);
    }

    printf("[parent] termination requested: shutting down workers...\n");
    stop_workers();
    wait_for_all_workers();

    printf("[parent] all workers stopped. exiting.\n");

    free(pids);
    return EXIT_SUCCESS;
}
