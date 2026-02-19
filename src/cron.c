#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <mqueue.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <pthread.h>
#include <time.h>
#include "../include/cron.h
#include "../include/logger.h"

struct dll_list *task_list = NULL;
mqd_t mq_server;
int running = 1;

struct dll_list* dll_create() {
    struct dll_list *list = malloc(sizeof(struct dll_list));
    if (!list) return NULL;
    list->head = NULL;
    list->tail = NULL;
    pthread_mutex_init(&list->lock, NULL);
    return list;
}

int dll_push_back(struct dll_list* dll, struct cron_data_t* value) {
    if (!dll) return 1;
    struct node_t *node = malloc(sizeof(struct node_t));
    if (!node) return 1;
    node->data = value;
    node->next = NULL;
    node->prev = dll->tail;

    if (dll->tail) dll->tail->next = node;
    dll->tail = node;
    if (!dll->head) dll->head = node;
    return 0;
}

int dll_remove_by_id(struct dll_list* dll, int id) {
    if (!dll) return 1;
    struct node_t *temp = dll->head;
    while (temp) {
        if (temp->data->id == id) {
            if (temp->prev) temp->prev->next = temp->next;
            else dll->head = temp->next;

            if (temp->next) temp->next->prev = temp->prev;
            else dll->tail = temp->prev;

            timer_delete(temp->data->timer_id);
            for(int i=0; temp->data->args[i] != NULL; i++) free(temp->data->args[i]);
            free(temp->data);
            free(temp);
            return 0;
        }
        temp = temp->next;
    }
    return 1;
}

void dll_destroy(struct dll_list* dll) {
    if (!dll) return;

    pthread_mutex_lock(&dll->lock);

    struct node_t *cur = dll->head;
    while (cur) {
        struct node_t *next = cur->next;
        timer_delete(cur->data->timer_id);
        for (int i = 0; cur->data->args[i] != NULL; i++) {
            free(cur->data->args[i]);
        }
        free(cur->data);
        free(cur);
        cur = next;
    }
    pthread_mutex_unlock(&dll->lock);
    pthread_mutex_destroy(&dll->lock);
    free(dll);
}

void timer_fun(union sigval sa) {
    struct cron_data_t *cron = sa.sival_ptr;

    pid_t pid = fork();
    if(pid == 0) {
        execv(cron->path,cron->args);
        printf("blad execv");
        exit(1);
    }
    if(pid > 0) {
        int temp;
        waitpid(pid,&temp,0);
    }

    if(!cron->is_cyclic) {
        pthread_mutex_lock(&task_list->lock);
        dll_remove_by_id(task_list,cron->id);
        pthread_mutex_unlock(&task_list->lock);
    }
}

int add_task(struct request_t *req) {
    struct cron_data_t *cron = calloc(1,sizeof(struct cron_data_t));
    if(!cron) return 1;
    int cron_idx = 1;

    req->req_id = rand() % 10000 + 1;
    cron->id = req->req_id;
    cron->is_cyclic = req->is_cyclic;
    strncpy(cron->path, req->path, sizeof(cron->path) - 1);

    cron->args[0] = strdup(req->path);
    if (!cron->args[0]){
        free(cron);
        return 1;
    }
    for(int i = 0; i < 10; i++) {
        if (cron_idx >= 11) break;
        if (req->args[i][0] != '\0') {
            req->args[i][63] = '\0';
            cron->args[cron_idx] = strdup(req->args[i]);
            if (!cron->args[cron_idx]) {
                break;
            }
            cron_idx++;
        }
        else {
            break;
        }
    }
    cron->args[cron_idx] = NULL;

    struct sigevent sev;
    sev.sigev_notify = SIGEV_THREAD;
    sev.sigev_notify_function = timer_fun;
    sev.sigev_value.sival_ptr = cron;
    sev.sigev_notify_attributes = NULL;

    if(timer_create(CLOCK_REALTIME,&sev,&cron->timer_id) == -1) {
        printf("Blad timer_create\n");
        for(int k=0; k < 12; k++) {
            if(cron->args[k]) free(cron->args[k]);
        }
        free(cron);
        return 1;
    }
    int abs = 0;
    if (req->is_absolute) {
        cron->timer_spec.it_value.tv_sec = req->time_sec;
        abs = TIMER_ABSTIME;
    }
    else {
        struct timespec now;
        clock_gettime(CLOCK_REALTIME, &now);
        cron->timer_spec.it_value.tv_sec = now.tv_sec + req->time_sec;
        abs = TIMER_ABSTIME;
    }
    if(req->is_cyclic) {
        cron->timer_spec.it_interval.tv_sec = req->is_absolute ? 60 : req->time_sec;
    }
    else {
        cron->timer_spec.it_interval.tv_sec = 0;
    }

    timer_settime(cron->timer_id,abs,&cron->timer_spec,NULL);
    pthread_mutex_lock(&task_list->lock);
    if(dll_push_back(task_list,cron)) {
        printf("Blad dodawania do listy dll_push_back!\n");
        timer_delete(cron->timer_id);
        for(int k=0; k < 12; k++) {
            if(cron->args[k]) free(cron->args[k]);
        }
        free(cron);
        pthread_mutex_unlock(&task_list->lock);
        return 1;
    }
    pthread_mutex_unlock(&task_list->lock);
    return 0;
}

void run_server() {
    printf("Serwer PID: %d\n", getpid());

    int sig_dump = SIGRTMIN;
    int sig_toggle = SIGRTMIN + 1;
    int sig_level = SIGRTMIN + 2;

    remove("logger_file.txt");
    logger_init("logger_file.txt", sig_dump, sig_toggle, sig_level);
    logger_write(STANDARD, "[SERVER START] Uruchamianie serwera...");

    struct mq_attr attr;
    attr.mq_flags = 0;
    attr.mq_maxmsg = 10;
    attr.mq_msgsize = MSG_SIZE;
    attr.mq_curmsgs = 0;

    mqd_t mq_server = mq_open(SERVER_QUEUE,O_CREAT | O_RDWR, 0666,&attr);
    if (mq_server == (mqd_t)-1) {
        printf("error server mq_open");
        logger_write(MIN, "[FATAL] Nie udało się otworzyć kolejki MQ");
    }
    task_list = dll_create();

    struct request_t req;
    while(running) {
        if(mq_receive(mq_server,(char*)&req,MSG_SIZE,NULL) == -1) {
            continue;
        }
        if(req.type == CMD_ADD) {
            int res = add_task(&req);

            char msg_to_client[MSG_SIZE];
            if(res != 0) {
                snprintf(msg_to_client, MSG_SIZE, "Błąd: Nie udało się dodać zadania.");
                logger_write(MIN, "[CMD ADD] Błąd dodawania zadania");
            }
            else {
                snprintf(msg_to_client, MSG_SIZE, "Sukces: Zadanie dodane (ID: %d).", req.req_id);
                logger_write(STANDARD, "[CMD ADD] Dodano nowe zadanie ID: %d", req.req_id);
            }
            char client[64];
            snprintf(client, sizeof(client), CLIENT_QUEUE, req.client_pid);

            mqd_t client_mq = mq_open(client, O_WRONLY);
            if (client_mq != (mqd_t)-1) {
                mq_send(client_mq, msg_to_client, MSG_SIZE, 0);
                mq_close(client_mq);
            }
            else {
                logger_write(MIN, "[ERROR] Nie można otworzyć kolejki klienta: %s", client);
            }
        }
        else if(req.type == CMD_LIST) {
            char buf[MSG_SIZE];
            strcpy(buf, "Lista zadan:\n");

            pthread_mutex_lock(&task_list->lock);
            struct node_t *cur = task_list->head;
            if(!cur){
                strcat(buf, "(brak zadań)\n");
            }
            while(cur) {
                char data_cron[MSG_SIZE];
                snprintf(data_cron, sizeof(data_cron), "ID: %d | path: %s | cykliczne: %d\n",
                         cur->data->id, cur->data->path, cur->data->is_cyclic);
                if (strlen(buf) + strlen(data_cron) < MSG_SIZE) {
                    strcat(buf, data_cron);
                }
                cur = cur->next;
            }
            pthread_mutex_unlock(&task_list->lock);

            char client[64];
            snprintf(client, sizeof(client), CLIENT_QUEUE, req.client_pid);

            mqd_t client_mq = mq_open(client, O_WRONLY);
            if (client_mq != (mqd_t)-1) {
                mq_send(client_mq, buf, MSG_SIZE, 0);
                mq_close(client_mq);
                logger_write(STANDARD, "[CMD LIST] Wysłano listę do klienta PID: %d", req.client_pid);
            } else {
                logger_write(MIN, "[ERROR] Nie można otworzyć kolejki klienta: %s", client);
            }
        }
        else if(req.type == CMD_REMOVE) {
            char msg_to_client[MSG_SIZE];
            pthread_mutex_lock(&task_list->lock);
            if(dll_remove_by_id(task_list, req.req_id) == 0) {
                logger_write(STANDARD, "[CMD REMOVE] Usunięto zadanie ID: %d", req.req_id);
                snprintf(msg_to_client,MSG_SIZE,"Poprawnie usunięto zadanie.");
            }
            else {
                logger_write(MIN, "[CMD REMOVE] Nie znaleziono ID: %d do usunięcia", req.req_id);
                snprintf(msg_to_client,MSG_SIZE,"Błąd: Nie udało się usunąć zadania.");
            }
            pthread_mutex_unlock(&task_list->lock);

            char client[64];
            snprintf(client,sizeof(client),CLIENT_QUEUE,req.client_pid);

            mqd_t client_mq = mq_open(client,O_WRONLY);
            if(client_mq != (mqd_t)-1) {
                mq_send(client_mq,msg_to_client,MSG_SIZE,0);
                mq_close(client_mq);
            }
            else {
                logger_write(MIN,"[ERROR] Nie można otworzyć kolejki do klienta: %s",client);
            }
        }
        else if(req.type == CMD_STOP) {
            logger_write(STANDARD, "[CMD STOP] Otrzymano sygnał zatrzymania.");
            running = 0;
        }
        else {
            logger_write(MIN, "Otrzymano nieznany typ komendy: %d", req.type);
        }
    }
    dll_destroy(task_list);
    mq_close(mq_server);
    mq_unlink(SERVER_QUEUE);
    logger_write(STANDARD, "[SERVER STOP] Koniec pracy.");
    logger_kill();
}
