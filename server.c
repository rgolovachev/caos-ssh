#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <syslog.h>

enum { BUF_SZ = 512, MAX_ARGS_CNT = 64, ERROR_EXIT_CODE = 42, LEN_MSG_TYPE = 16 };

int create_main_listen_socket(char* service) {
    struct addrinfo* res = NULL;
    int gai_err;
    struct addrinfo hint = {
        .ai_family = AF_INET,
        .ai_socktype = SOCK_STREAM,
        .ai_flags = AI_PASSIVE,
    };
    if ((gai_err = getaddrinfo(NULL, service, &hint, &res))) {
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
        if (bind(sock, ai->ai_addr, ai->ai_addrlen) < 0) {
            perror("bind");
            close(sock);
            sock = -1;
            continue;
        }
        if (listen(sock, SOMAXCONN) < 0) {
            perror("listen");
            close(sock);
            sock = -1;
            continue;
        }
        break;
    }
    freeaddrinfo(res);
    return sock;
}

void on_program_end(int signo) {
    wait(NULL);
    _exit(0);
}

void run_session(int conn) {
    struct sigaction sa = {.sa_handler = on_program_end};
    sigaction(SIGCHLD, &sa, 0);

    // read command and arguments
    int conn2 = dup(conn);
    FILE *cmd_args = fdopen(conn2, "r");
    char buf[BUF_SZ] = {0};
    char *args[MAX_ARGS_CNT] = {NULL};
    int args_sz = 0;
    fscanf(cmd_args, "%d", &args_sz);
    for (int i = 0; i < args_sz; ++i) {
        fscanf(cmd_args, "%s", buf);
        args[i] = calloc(BUF_SZ, sizeof(char));
        memcpy(args[i], buf, BUF_SZ);
    }
    fclose(cmd_args);
    // redirect IO and execute command
    // subprocess read from first_pipe and write to pipe
    int fd[2];
    pipe(fd);
    pid_t pid = fork();
    if (pid == 0) {
        close(fd[1]);
        dup2(fd[0], STDIN_FILENO);
        dup2(conn, STDOUT_FILENO);
        dup2(conn, STDERR_FILENO);
        close(conn);
        close(fd[0]);
        execvp(args[0], args);
        _exit(ERROR_EXIT_CODE);
    } else {
        close(fd[0]);
        conn2 = dup(conn);
        FILE *user_input = fdopen(conn2, "r");
        FILE *program_stdin = fdopen(fd[1], "w");
        while (1) {
            char *res = fgets(buf, BUF_SZ, user_input);
            if (res == NULL) {
                break;
            }
            char *text_input = buf;
            char msg_type[LEN_MSG_TYPE] = {0};
            size_t msg_type_cur_len = 0;
            while (buf[msg_type_cur_len] != ' ') {
                msg_type[msg_type_cur_len] = buf[msg_type_cur_len];
                ++msg_type_cur_len;
                ++text_input;
            }
            ++text_input;
            if (strcmp(msg_type, "input") == 0) {
                fprintf(program_stdin, "%s", text_input);
                fflush(program_stdin);
            } else {
                break;
            }
        }
        
        fclose(program_stdin);
        fclose(user_input);
        
        close(conn);
        int wstatus;
        waitpid(pid, &wstatus, 0);
        
    }
    for (size_t i = 0; i < args_sz; ++i) {
        free(args[i]);
    }
    _exit(0);
}

void on_session_end(int signo) {
    while (waitpid(-1, NULL, WNOHANG) > 0) {
    }
}

int daemonize() {
    openlog("ssh_server_daemon", LOG_PID | LOG_CONS, LOG_DAEMON);
    if (daemon(0, 0) == -1) {
        return -1;
    }
}

int main(int argc, char *argv[]) {
    if (daemonize() == -1) {
        printf("failed to create daemon :(\n");
        return 1;
    }

    struct sigaction sa = {.sa_handler = on_session_end};
    sigaction(SIGCHLD, &sa, 0);

    int sock = create_main_listen_socket(argv[1]);
    if (sock < 0) {
        return 1;
    }
    while (1) {
        int conn = accept(sock, NULL, NULL);
        if (conn > 0) {
            pid_t pid = fork();
            if (pid == 0) {
                close(sock);
                run_session(conn);
            }
            close(conn);
        }
    }
    close(sock);
    return 0;
}
