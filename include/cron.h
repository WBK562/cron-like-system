#ifndef CRON_H
#define CRON_H

#include <time.h>
#include <signal.h>
#include <pthread.h>

#define SERVER_QUEUE   "/server_queue"
#define CLIENT_QUEUE    "/client_queue_%d"

#define MSG_SIZE 2048

typedef enum {
    CMD_ADD,
    CMD_LIST,
    CMD_REMOVE,
    CMD_STOP
} cmd_type_t;

struct request_t {
    int req_id;
    cmd_type_t type;
    pid_t client_pid;
    int is_cyclic;
    int is_absolute;
    time_t time_sec;
    char path[128];
    char args[10][64];
};

struct cron_data_t {
    int id;
    timer_t timer_id;
    struct itimerspec timer_spec;
    char path[128];
    char *args[12];
    int is_cyclic;
} ;

struct node_t {
    struct cron_data_t *data;
    struct node_t *next;
    struct node_t *prev;
};

struct dll_list {
    struct node_t *head;
    struct node_t *tail;
    pthread_mutex_t lock;
};

struct dll_list* dll_create();
int dll_push_back(struct dll_list* dll, struct cron_data_t* value);
int dll_remove_by_id(struct dll_list* dll, int id);
void dll_destroy(struct dll_list* dll);
void run_server();
int add_task(struct request_t *req);
void timer_fun(union sigval sa);
void run_client(int argc, char *argv[]);

#endif
