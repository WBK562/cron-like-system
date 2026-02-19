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

#define QUEUE_SIZE 128

static volatile sig_atomic_t logging_state = 1;
static volatile sig_atomic_t log_curr_level = MAX;
static volatile sig_atomic_t lib_runnig = 0;

static sem_t dump_sem;
static pthread_t dump_pthread;

static FILE *log_file = NULL;
static pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;

static int queue[QUEUE_SIZE];
static volatile sig_atomic_t head = 0;
static int tail = 0;


static void handler_dump(int sig, siginfo_t *info, void *ucontext) {
    queue[head] = info->si_value.sival_int;
    head = (head + 1) % QUEUE_SIZE;
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
    while(lib_runnig) {
        sem_wait(&dump_sem);

        while(tail != head) {
            int data = queue[tail];
            tail = (tail + 1) % QUEUE_SIZE;
            snprintf(filename, sizeof(filename), "dump_%ld_val%d.txt", time(NULL), data);
            FILE *f = fopen(filename, "w");
            if (f) {
                fprintf(f, "Czas: %ld\n", time(NULL));
                fprintf(f, "Otrzymana wartosc sygnalu: %d\n", data);
                fclose(f);
            }
        }
        if(!lib_runnig) break;
    }
    return NULL;
}

int logger_init(const char *filename, int sig_dump, int sig_toggle, int sig_level) {
    if(lib_runnig) return -1;

    log_file = fopen(filename,"a");
    if(log_file == NULL) {
        return -2;
    }
    lib_runnig = 1;

    sem_init(&dump_sem,0,0);
    pthread_create(&dump_pthread,NULL,&dump_thread_func,NULL);

    struct sigaction sa;
    sigemptyset(&sa.sa_mask);

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
    if(!lib_runnig) return;

    lib_runnig = 0;

    sem_post(&dump_sem);
    pthread_join(dump_pthread,NULL);

    sem_destroy(&dump_sem);
    pthread_mutex_destroy(&log_mutex);

    if (log_file) {
        fclose(log_file);
        log_file = NULL;
    }
}

void logger_write(enum log_level LogLevel, char *format, ...) {
    if(!lib_runnig || !logging_state) return;

    if (LogLevel > log_curr_level) return;

    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    char time_buf[30];
    strftime(time_buf, sizeof(time_buf), "%d-%m-%Y %H:%M:%S", t);

    const char *prefix_str = "UNK";
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
