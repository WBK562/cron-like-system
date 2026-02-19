#include "../include/logger.h"
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>
#include <signal.h>
#include <pthread.h>
#include <semaphore.h>
#include <unistd.h>

// Shared variables modified by signal handlers must be volatile sig_atomic_t
static volatile sig_atomic_t logging_state = 1;
static volatile sig_atomic_t log_curr_level = STANDARD;
static volatile sig_atomic_t lib_running = 0;

// Variable to store the latest dump value instead of a queue
static volatile sig_atomic_t current_dump_val = 0;

static sem_t dump_sem;
static pthread_t dump_pthread;

static FILE *log_file = NULL;
static pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;

static void handler_dump(int sig, siginfo_t *info, void *ucontext) {
    // Overwrite with the latest signal value
    current_dump_val = info->si_value.sival_int;
    sem_post(&dump_sem);
}

static void handler_level(int sig, siginfo_t *info, void *ucontext) {
    log_curr_level++;
    if(log_curr_level > MAX) log_curr_level = MIN;
}

static void handler_log(int sig, siginfo_t *info, void *ucontext) {
    logging_state = !logging_state;
}

static void *dump_thread_func(void *arg) {
    char filename[128];
    while(lib_running) {
        sem_wait(&dump_sem);

        // Safety check: was the semaphore posted by logger_kill?
        if(!lib_running) break;

        // Copy value locally as quickly as possible to minimize overwrite risk
        int data = current_dump_val;

        snprintf(filename, sizeof(filename), "dump_%ld_val%d.txt", time(NULL), data);

        time_t now = time(NULL);
        struct tm *t = localtime(&now);
        char time_buf[30];
        strftime(time_buf, sizeof(time_buf), "%d-%m-%Y %H:%M:%S", t);

        FILE *f = fopen(filename, "w");
        if (f) {
            fprintf(f, "Time: %s\n", time_buf);
            fprintf(f, "Received signal value: %d\n", data);
            fclose(f);
        }
    }
    return NULL;
}

int logger_init(const char *filename, int sig_dump, int sig_toggle, int sig_level) {
    if(lib_running) return -1;

    log_file = fopen(filename, "a");
    if(log_file == NULL) {
        return -2;
    }
    lib_running = 1;

    sem_init(&dump_sem, 0, 0);
    pthread_create(&dump_pthread, NULL, &dump_thread_func, NULL);

    struct sigaction sa;
    sigemptyset(&sa.sa_mask);

    // SA_SIGINFO flag is required to read sival_int
    sa.sa_flags = SA_SIGINFO | SA_RESTART;

    sa.sa_sigaction = handler_dump;
    sigaction(sig_dump, &sa, NULL);

    sa.sa_sigaction = handler_level;
    sigaction(sig_level, &sa, NULL);

    sa.sa_sigaction = handler_log;
    sigaction(sig_toggle, &sa, NULL);

    return 0;
}

void logger_kill() {
    if(!lib_running) return;

    lib_running = 0;

    // Post semaphore to unblock the thread and allow it to exit cleanly
    sem_post(&dump_sem);
    pthread_join(dump_pthread, NULL);

    sem_destroy(&dump_sem);
    pthread_mutex_destroy(&log_mutex);

    if (log_file) {
        fclose(log_file);
        log_file = NULL;
    }
}

void logger_write(enum log_level LogLevel, char *format, ...) {
    if(!lib_running || !logging_state) return;

    if (LogLevel > log_curr_level) return;

    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    char time_buf[30];
    strftime(time_buf, sizeof(time_buf), "%d-%m-%Y %H:%M:%S", t);

    const char *prefix_str;
    if (LogLevel == MIN) prefix_str = "MIN";
    if (LogLevel == STANDARD) prefix_str = "STD";
    if (LogLevel == MAX) prefix_str = "MAX";

    pthread_mutex_lock(&log_mutex);

    fprintf(log_file, "[%s] [%s] ", time_buf, prefix_str);

    va_list args;
    va_start(args, format);
    vfprintf(log_file, format, args);
    va_end(args);

    fprintf(log_file, "\n");

    pthread_mutex_unlock(&log_mutex);
}
