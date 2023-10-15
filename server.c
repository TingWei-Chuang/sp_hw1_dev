#include "hw1.h"
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/select.h>
#include <assert.h>
#include <stdbool.h>

#define ERR_EXIT(a) do { perror(a); exit(1); } while(0)
#define BUFFER_SIZE 512
#define DATA_SIZE sizeof(record) * RECORD_NUM
#define SEL_WAIT_MS 2000
#define POST_REQU '0'
#define POST_AVAI '1'
#define POST_NAVAI '2'
#define PULL_REQU '3'
#define EXIT_REQU '4'

typedef struct {
    char hostname[512];  // server's hostname
    unsigned short port;  // port to listen
    int listen_fd;  // fd to wait for a new connection
} server;

typedef struct {
    char host[512];  // client's host
    int conn_fd;  // fd to talk with client
    char buf[BUFFER_SIZE];  // data sent by/to client
    size_t buf_len;  // bytes used by buf
    int id;
    int k_locked_post;
} request;

server svr;  // server
request* requestP = NULL;  // point to a list of requests
int maxfd;  // size of open file descriptor table, size of request list
int last = -1;
int fd_bulletinBoard;
bool locked_post[RECORD_NUM] = {};
char buffer[BUFFER_SIZE];
char data[DATA_SIZE];

// initailize a server, exit for error
static void init_server(unsigned short port);

// initailize a request instance
static void init_request(request* reqP);

// free resources used by a request instance
static void free_request(request* reqP);

void set_fl(int fd, int fl) {int val = fcntl(fd, F_GETFL); val |= fl; fcntl(fd, F_SETFL, fl);}

struct flock set_flock(short type, short whence, off_t start, off_t len) {struct flock ret; ret.l_type = type; ret.l_whence = whence; ret.l_start = start; ret.l_len = len; return ret;}

bool lock_post(int conn_fd) {
    for (int i = 0; i < RECORD_NUM; i++) {
        last = (last + 1) % RECORD_NUM;
        if (locked_post[last]) continue;
        struct flock check = set_flock(F_WRLCK, SEEK_SET, sizeof(record) * last, sizeof(record));
        if (fcntl(fd_bulletinBoard, F_SETLK, &check) < 0) continue;
        locked_post[last] = true;
        requestP[conn_fd].k_locked_post = last;
        return true;
    }
    return false;
}

void send_post(int conn_fd) {
    char *ptr = data;
    int n_post = 0;
    int n_locked_post = 0;
    for (int i = 0; i < RECORD_NUM; i++) {
        if (locked_post[i]) {n_locked_post++; continue;}
        struct flock check = set_flock(F_RDLCK, SEEK_SET, sizeof(record) * i, sizeof(record));
        if (fcntl(fd_bulletinBoard, F_SETLK, &check) < 0) {n_locked_post++; continue;}
        int nbytes = pread(fd_bulletinBoard, ptr, sizeof(record), sizeof(record) * i);
        if (!(nbytes != sizeof(record) || (ptr[0] == '\0' && ptr[FROM_LEN] == '\0'))) {
            ptr += sizeof(record);
            n_post++;
        }
        check = set_flock(F_UNLCK, SEEK_SET, sizeof(record) * i, sizeof(record));
        fcntl(fd_bulletinBoard, F_SETLK, &check);
    }
    write(conn_fd, &n_post, sizeof(int));
    write(conn_fd, data, sizeof(record) * n_post);
    if (n_locked_post > 0)
        printf("[Warning] Try to access locked post - %d\n", n_locked_post);
}

void write_post(int conn_fd) {
    record re;
    read(conn_fd, &re, sizeof(re));
    pwrite(fd_bulletinBoard, &re, sizeof(record), sizeof(record) * requestP[conn_fd].k_locked_post);
    struct flock check = set_flock(F_UNLCK, SEEK_SET, sizeof(record) * requestP[conn_fd].k_locked_post, sizeof(record));
    fcntl(fd_bulletinBoard, F_SETLK, &check);
    locked_post[requestP[conn_fd].k_locked_post] = false;
    printf("[Log] Receive post from %s\n", re.From);
}

int main(int argc, char** argv) {

    // Parse args.
    if (argc != 2) {
        ERR_EXIT("usage: [port]");
        exit(1);
    }

    struct sockaddr_in cliaddr;  // used by accept()
    int clilen;

    int conn_fd;  // fd for a new connection with client
    int file_fd;  // fd for file that we open for reading

    // Initialize server
    init_server((unsigned short) atoi(argv[1]));

    // Loop for handling connections
    fprintf(stderr, "\nstarting on %.80s, port %d, fd %d, maxconn %d...\n", svr.hostname, svr.port, svr.listen_fd, maxfd);

    fd_bulletinBoard = open(RECORD_PATH, O_RDWR | O_CREAT, 0644);

    fd_set ms, cs;
    fd_set *master_set = &ms, *candidate_set = &cs;
    FD_ZERO(master_set);
    FD_SET(svr.listen_fd, master_set);
    int biggest_fd = svr.listen_fd;

    while (1) {
        // TODO: Add IO multiplexing
        memcpy(candidate_set, master_set, sizeof(fd_set));
        struct timeval tv = {0, SEL_WAIT_MS};
        select(biggest_fd + 1, candidate_set, NULL, NULL, &tv);
        if (FD_ISSET(svr.listen_fd, candidate_set)) {
            // Check new connection
            clilen = sizeof(cliaddr);
            conn_fd = accept(svr.listen_fd, (struct sockaddr*)&cliaddr, (socklen_t*)&clilen);
            if (conn_fd < 0) {
                if (errno == EINTR || errno == EAGAIN) continue;  // try again
                if (errno == ENFILE) {
                    (void) fprintf(stderr, "out of file descriptor table ... (maxconn %d)\n", maxfd);
                    continue;
                }
                ERR_EXIT("accept");
            }
            requestP[conn_fd].conn_fd = conn_fd;
            strcpy(requestP[conn_fd].host, inet_ntoa(cliaddr.sin_addr));
            FD_SET(conn_fd, master_set);
            if (conn_fd > biggest_fd) biggest_fd = conn_fd;
            fprintf(stderr, "getting a new request... fd %d from %s\n", conn_fd, requestP[conn_fd].host);
        }
        
        file_fd = -1;
        for (int i = 0; i <= biggest_fd; i++) {
            if (i != svr.listen_fd && FD_ISSET(i, candidate_set)) {
                file_fd = i;
                break;
            }
        }
        if (file_fd < 0) continue;

        char instru;
        read(file_fd, &instru, 1);
        switch (instru)
        {
        case POST_REQU:
            instru = lock_post(file_fd) ? POST_AVAI : POST_NAVAI;
            write(file_fd, &instru, 1);
            break;
        case PULL_REQU:
            send_post(file_fd);
            break;
        case POST_AVAI:
            write_post(file_fd);
            break;
        case EXIT_REQU:
            FD_CLR(file_fd, master_set);
            if (file_fd == biggest_fd) biggest_fd--;
            close(requestP[file_fd].conn_fd);
            free_request(&requestP[file_fd]);
            break;
        }
        fflush(stdout);
    }
    free(requestP);
    return 0;
}

// ======================================================================================================
// You don't need to know how the following codes are working

static void init_request(request* reqP) {
    reqP->conn_fd = -1;
    reqP->buf_len = 0;
    reqP->id = 0;
}

static void free_request(request* reqP) {
    init_request(reqP);
}

static void init_server(unsigned short port) {
    struct sockaddr_in servaddr;
    int tmp;

    gethostname(svr.hostname, sizeof(svr.hostname));
    svr.port = port;

    svr.listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (svr.listen_fd < 0) ERR_EXIT("socket");

    bzero(&servaddr, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servaddr.sin_port = htons(port);
    tmp = 1;
    if (setsockopt(svr.listen_fd, SOL_SOCKET, SO_REUSEADDR, (void*)&tmp, sizeof(tmp)) < 0) {
        ERR_EXIT("setsockopt");
    }
    if (bind(svr.listen_fd, (struct sockaddr*)&servaddr, sizeof(servaddr)) < 0) {
        ERR_EXIT("bind");
    }
    if (listen(svr.listen_fd, 1024) < 0) {
        ERR_EXIT("listen");
    }

    // Get file descripter table size and initialize request table
    maxfd = getdtablesize();
    requestP = (request*) malloc(sizeof(request) * maxfd);
    if (requestP == NULL) {
        ERR_EXIT("out of memory allocating all requests");
    }
    for (int i = 0; i < maxfd; i++) {
        init_request(&requestP[i]);
    }
    requestP[svr.listen_fd].conn_fd = svr.listen_fd;
    strcpy(requestP[svr.listen_fd].host, svr.hostname);

    return;
}
