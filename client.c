#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>

enum { MIN_ARGS_CNT = 5, MAX_NUM_LEN = 16, BUF_SZ = 512 };

int create_connection(char* node, char* service) {
    struct addrinfo* res = NULL;
    int gai_err;
    struct addrinfo hint = {
        .ai_family = AF_INET,
        .ai_socktype = SOCK_STREAM,
    };
    if ((gai_err = getaddrinfo(node, service, &hint, &res))) {
        fprintf(stderr, "gai error: %s\n", gai_strerror(gai_err));
        return -1;
    }
    int sock = -1;
    for (struct addrinfo *ai = res; ai; ai = ai->ai_next) {
        sock = socket(ai->ai_family, ai->ai_socktype, 0);
        if (sock < 0) {
            perror("socket");
            continue;
        }
        if (connect(sock, ai->ai_addr, ai->ai_addrlen) < 0) {
            perror("connect");
            close(sock);
            sock = -1;
            continue;
        }
        break;
    }
    freeaddrinfo(res);
    return sock;
}

void run_cmd(int sock, int argc, char **argv) {
    int cmd_args_cnt = argc - 4;
    int output_cmd = dup(sock);
    FILE *out = fdopen(output_cmd, "w");
    fprintf(out, "%d ", cmd_args_cnt);
    for (int i = 4; i < argc; ++i) {
        fprintf(out, "%s ", argv[i]);
    }
    fclose(out);
}

void *get_output(void *arg) {
    int sock = *((int *) arg);
    int sock2 = dup(sock);
    FILE *output = fdopen(sock2, "r");
    char *line = NULL;
    size_t len = 0;
    ssize_t nread;
    char buf[BUF_SZ] = {0};
    char c;
    while ((c = fgetc(output)) >= 0) {
        printf("%c", c);
    }
    fclose(output);
    _exit(0);
    return NULL;
}

int main(int argc, char *argv[]) {
    // ./client address port spawn cmd args[optional]
    if (argc < MIN_ARGS_CNT) {
        printf("error! u must give at least %d arguments: ./client address port spawn cmd\n", MIN_ARGS_CNT);
        return 1;
    }
    const char* spawn_str = "spawn";
    if (strcmp(argv[3], spawn_str) != 0) {
        printf("error! 4th argument must be 'spawn'\n");
        return 1;
    }
    int sock = create_connection(argv[1], argv[2]);
    if (sock < 0) {
        printf("error! can't create connection with the server\n");
        return 1;
    }
    run_cmd(sock, argc, argv);
    pthread_t tid;
    pthread_create(&tid, NULL, get_output, &sock);
    char buf[BUF_SZ] = {0};
    int sock2 = dup(sock);
    FILE *input = fdopen(sock2, "w");
    while (fgets(buf, BUF_SZ, stdin) != NULL) {
        fprintf(input, "input %s", buf);
        fflush(input);
    }
    fprintf(input, "end \n");
    fflush(input);
    fclose(input);
    close(sock);
    pthread_join(tid, NULL);
    return 0;
}
