#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <unistd.h>

#include "fastpub.h"

void publisher() {
    struct fastpub *fp = fastpub_pubopen("fastpub_test", 1024, 1);

    printf("publisher shm @ %p\n", fp->info);

    for (long int i = 0; i < 10000000L; i++) {
        int *buffer = fastpub_nextbuf(fp);
        buffer[0] = i;
        usleep(100);
        fastpub_publish(fp);
    }

    fastpub_close(fp);
}

void subscriber() {
    struct fastpub *fp = fastpub_subopen("fastpub_test");

    printf("subscriber shm @ %p\n", fp->info);

    for (int i = 0;; i++) {
        int *buffer = fastpub_next_update(fp);

        printf("got updated frame %d\n", buffer[0]);

        if (buffer[0] == 9999999) {
            fastpub_release(fp, buffer);
            break;
        }

        fastpub_release(fp, buffer);
    }

    fastpub_close(fp);
}

int main(int argc, char **argv) {

    if (fork()) {
        publisher();
    }
    else {
        subscriber();
    }

    return 0;
}
