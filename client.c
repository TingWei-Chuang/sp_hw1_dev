#include "hw1.h"
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#define ERR_EXIT(a) do { perror(a); exit(1); } while(0)
#define BUFFER_SIZE 512

typedef struct {
    char* ip; // server's ip
    unsigned short port; // server's port
    int conn_fd; // fd to talk with server
    char buf[BUFFER_SIZE]; // data sent by/to server
    size_t buf_len; // bytes used by buf
} client;

client cli;
static void init_client(char** argv);

#define POST_REQU '0'
#define POST_AVAI '1'
#define POST_NAVAI '2'
#define PULL_REQU '3'
#define EXIT_REQU '4'

void print_welcome_msg(void) {printf("==============================\nWelcome to CSIE Bulletin board\n");}

void pull_content(void) {
    printf("==============================\n");
    record re;
    while (read(cli.conn_fd, &re, sizeof(record)) == sizeof(record))
        printf("FROM: %s\nCONTENT:\n%s\n", re.From, re.Content);
    printf("==============================\n");
}

int main(int argc, char** argv){
    
    // Parse args.
    if(argc!=3){
        ERR_EXIT("usage: [ip] [port]");
    }

    // Handling connection
    init_client(argv);
    fprintf(stderr, "connect to %s %d\n", cli.ip, cli.port);

    print_welcome_msg();
    char instru = PULL_REQU;
    write(cli.conn_fd, &instru, 1);
    pull_content();

    while(1){
        // TODO: handle user's input
        printf("Please enter your command (post/pull/exit): ");
        char cmd[5];
        scanf("%s", cmd);
        if (strcmp(cmd, "post") == 0) {
            char instru = POST_REQU;
            write(cli.conn_fd, &instru, 1);
            read(cli.conn_fd, &instru, 1);
            if (instru == POST_AVAI) {
                record buf = {};
                printf("FROM: ");
                scanf("%s", buf.From);
                printf("CONTENT:\n");
                scanf("%s", buf.Content);
                write(cli.conn_fd, &instru, 1);
                write(cli.conn_fd, &buf, sizeof(buf));
            }
            else if (instru == POST_NAVAI) printf("[Error] Maximum posting limit exceeded\n");
            else ERR_EXIT("Inter process instruction error\n");
        }
        else if (strcmp(cmd, "pull") == 0) {
            char instru = PULL_REQU;
            write(cli.conn_fd, &instru, 1);
            pull_content();
        }
        else if (strcmp(cmd, "exit") == 0) {
            char instru = EXIT_REQU;
            write(cli.conn_fd, &instru, 1);
            return 0;
        }
        else ERR_EXIT("Command should be one of [post/pull/exit]\n");
    }
}

static void init_client(char** argv){
    
    cli.ip = argv[1];

    if(atoi(argv[2])==0 || atoi(argv[2])>65536){
        ERR_EXIT("Invalid port");
    }
    cli.port=(unsigned short)atoi(argv[2]);

    struct sockaddr_in servaddr;
    cli.conn_fd = socket(AF_INET, SOCK_STREAM, 0);
    if(cli.conn_fd<0){
        ERR_EXIT("socket");
    }

    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(cli.port);

    if(inet_pton(AF_INET, cli.ip, &servaddr.sin_addr)<=0){
        ERR_EXIT("Invalid IP");
    }

    if(connect(cli.conn_fd, (struct sockaddr*)&servaddr, sizeof(servaddr))<0){
        ERR_EXIT("connect");
    }

    return;
}
