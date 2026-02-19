#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <mqueue.h>
#include <errno.h>
#include "cron.h"
#include "../include/logger.h"

int main(int argc, char *argv[]) {

    struct mq_attr attr = {0, 10, MSG_SIZE, 0};
    mqd_t check = mq_open(SERVER_QUEUE, O_CREAT | O_EXCL | O_RDWR, 0666, &attr);

    if (check != (mqd_t)-1) {
        mq_close(check);
        mq_unlink(SERVER_QUEUE);

        printf("Serwer uruchomiony. Czekam na komendy (użyj drugiego terminala)...\n");
        run_server();
    }
    else {
        if (errno == EEXIST) {
            if (argc > 1) {
                run_client(argc, argv);
            } else {
                printf("Serwer już działa. Użyj flag, aby wysłać polecenie.\n");
                char *help[] = {argv[0], NULL};
                run_client(1, help);
            }
        } else {
            printf("Błąd przy sprawdzaniu serwera");
            exit(1);
        }
    }

    return 0;
}
