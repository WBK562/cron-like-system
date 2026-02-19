#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <mqueue.h>
#include <sys/stat.h>
#include <time.h>
#include "../include/cron.h"

void print_help(char *prog_name) {
    printf("Użycie programu:\n");
    printf("%s -a <sek> <prog> [arg...]  : Dodaj zadanie jednorazowe (za X sekund)\n", prog_name);
    printf("%s -c <sek> <prog> [arg...]  : Dodaj zadanie CYKLICZNE (co X sekund)\n", prog_name);
    printf("%s -t <date> <prog> [arg...] : Dodaj zadanie na konkretny dzień \"RRRR-MM-DD HH:MM\" lub \"HH:MM\"\n", prog_name);
    printf("-ls                          : Wyświetl listę zaplanowanych zadań\n");
    printf("-rm <id>                     : Usuń zadanie o podanym ID\n");
    printf("-kill                        : Zatrzymaj serwer\n");
}

void run_client(int argc, char *argv[]) {
    if (argc < 2) {
        print_help(argv[0]);
        return;
    }

    mqd_t mq = mq_open(SERVER_QUEUE, O_WRONLY);
    if (mq == (mqd_t)-1) {
        printf("Błąd: Nie można połączyć się z serwerem.\n");
        exit(1);
    }

    struct request_t req;
    memset(&req, 0, sizeof(req));
    req.client_pid = getpid();

    if (strcmp(argv[1], "-ls") == 0) {
        req.type = CMD_LIST;
    }
    //===============================================================
    else if (strcmp(argv[1], "-rm") == 0) {
        if (argc < 3) {
            printf("Błąd: Podaj ID zadania do usunięcia.\n");
            mq_close(mq);
            return;
        }
        req.type = CMD_REMOVE;
        req.req_id = atoi(argv[2]);
    }
    //===============================================================
    else if (strcmp(argv[1], "-kill") == 0) {
        req.type = CMD_STOP;
    }
    //===============================================================
    else if (strcmp(argv[1], "-a") == 0 || strcmp(argv[1], "-c") == 0 || strcmp(argv[1], "-t") == 0) {
        if (argc < 4) {
            printf("Zbyt mało argumentów dla dodania zadania.\n");
            mq_close(mq);
            return;
        }
        //===============================================================
        req.type = CMD_ADD;
        //===============================================================
        if (strcmp(argv[1], "-c") == 0) {
            req.is_cyclic = 1;
        }
        else {
            req.is_cyclic = 0;
        }
        //===============================================================
        if (strcmp(argv[1], "-t") == 0) {
            req.is_absolute = 1;

            struct tm t;
            memset(&t, 0, sizeof(struct tm));
            t.tm_isdst = -1;

            int Y, M, D, h, m;
            int mode = 0;

            if (sscanf(argv[2], "%d-%d-%d %d:%d", &Y, &M, &D, &h, &m) == 5) {
                t.tm_year = Y - 1900;
                t.tm_mon = M - 1;
                t.tm_mday = D;
                t.tm_hour = h;
                t.tm_min = m;
                mode = 1;
            }
            else if (sscanf(argv[2], "%d:%d", &h, &m) == 2) {
                time_t now = time(NULL);
                struct tm *today = localtime(&now);

                t.tm_year = today->tm_year;
                t.tm_mon = today->tm_mon;
                t.tm_mday = today->tm_mday;
                t.tm_hour = h;
                t.tm_min = m;
            }
            else {
                printf("Błąd: Niepoprawny format czasu. Użyj \"RRRR-MM-DD HH:MM\" lub \"HH:MM\"\n");
                return;
            }

            req.time_sec = mktime(&t);
            if (req.time_sec == -1) {
                printf("Błąd: Nie udało się rozpoznać daty.\n");
                return;
            }

            time_t now = time(NULL);

            if (req.time_sec < now) {
                if (mode == 1) {
                    printf("Błąd: Podana data (%s) jest z przeszłości!\n", argv[2]);
                    return;
                }
                printf("Godzina %02d:%02d już minęła, planuję na jutro.\n", h, m);
                req.time_sec += 86400;
            }
        }
        else {
            req.is_absolute = 0;
            req.time_sec = atol(argv[2]);
        }
        strncpy(req.path, argv[3], 128);

        int arg_idx = 0;
        for (int i = 4; i < argc && arg_idx < 10; i++) {
            strncpy(req.args[arg_idx], argv[i], 64);
            arg_idx++;
        }
        if (argc > 14) printf("Podałeś więcej niż 10 argumentów, niektóre mogą zostać zignorowane!\n");
    }
    else {
        printf("Nieznana opcja: %s\n", argv[1]);
        print_help(argv[0]);
        mq_close(mq);
        return;
    }
    //===============================================================
    char my_mq_name[64];
    snprintf(my_mq_name, sizeof(my_mq_name), CLIENT_QUEUE, getpid());

    struct mq_attr attr = {0, 10, MSG_SIZE, 0};
    mqd_t my_mq = mq_open(my_mq_name, O_CREAT | O_RDONLY, 0666, &attr);

    if (my_mq == (mqd_t)-1) {
        printf("Błąd tworzenia kolejki klienta\n");
        mq_close(mq);
        return;
    }
    //===============================================================
    if (mq_send(mq, (char*)&req, sizeof(req), 0) == -1) {
        printf("Błąd wysyłania do serwera\n");
        mq_close(my_mq);
        mq_unlink(my_mq_name);
        mq_close(mq);
        return;
    }

    if (req.type == CMD_STOP) {
        printf("Wysłano sygnał zatrzymania serwera.\n");
    }
    else {
        char buffer[MSG_SIZE];

        ssize_t bytes_read = mq_receive(my_mq, buffer, MSG_SIZE, NULL);

        if (bytes_read > 0) {
            if (req.type == CMD_LIST) printf("\n--- ZAPLANOWANE ZADANIA ---\n");
            printf("%s\n", buffer);
            if (req.type == CMD_LIST) printf("---------------------------\n");
        } else {
            printf("Błąd odbierania odpowiedzi\n");
        }
    }
    mq_close(my_mq);
    mq_unlink(my_mq_name);
    mq_close(mq);
}
