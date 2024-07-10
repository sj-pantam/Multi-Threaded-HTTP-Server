#include "asgn2_helper_funcs.h"
#include "connection.h"
#include "debug.h"
#include "response.h"
#include "request.h"
#include "queue.h"
#include "rwlock.h"

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>
#include <assert.h>
#include <pthread.h>

#include <sys/stat.h>

void handle_connection(int);
void handle_get(conn_t *);
void handle_put(conn_t *);
void handle_unsupported(conn_t *);

pthread_mutex_t mutex;
queue_t *q;

struct Node {
    char *key;
    rwlock_t *rwlock;
    struct Node *next;
};

struct List {
    struct Node *head;
};

struct List L;

struct Node *newNode(char *key, rwlock_t *rwlock) {
    struct Node *newNode = (struct Node *) malloc(sizeof(struct Node));
    newNode->key = key;
    newNode->rwlock = rwlock;
    newNode->next = NULL;
    return newNode;
}

void insert(struct List *L, char *key, rwlock_t *rw) {
    char *key1 = malloc(strlen(key) + 1);
    strcpy(key1, key);
    struct Node *N = newNode(key1, rw);
    N->next = L->head;
    L->head = N;
}

rwlock_t *find(struct List *L, char *key) {
    struct Node *N = L->head;
    while (N != NULL) {
        if (strcmp(N->key, key) == 0) {
            return N->rwlock;
        }
        N = N->next;
    }

    return NULL;
}

void freeList(struct List *L) {
    struct Node *N = L->head;
    struct Node *next;
    while (N != NULL) {
        next = N->next;
        free(N);
        N = next;
    }
    L->head = NULL;
}

void worker_thread() {
    while (1) {
        uintptr_t connfd = 0;
        queue_pop(q, (void **) &connfd);
        handle_connection(connfd);
        close(connfd);
    }
}

int main(int argc, char **argv) {
    int opt;
    if (argc < 2) {
        warnx("wrong arguments: %s port_num", argv[0]);
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        return EXIT_FAILURE;
    }

    int t;
    while ((opt = getopt(argc, argv, "t:")) != -1) {
        switch (opt) {
        case 't': t = atoi(optarg); break;
        default: t = 4; break;
        }
    }
    char *endptr = NULL;
    size_t port = (size_t) strtoull(argv[optind], &endptr, 10);
    if (endptr && *endptr != '\0') {
        warnx("invalid port number: %s", argv[1]);
        return EXIT_FAILURE;
    }
    L.head = NULL;
    signal(SIGPIPE, SIG_IGN);
    Listener_Socket sock;
    listener_init(&sock, port);
    pthread_mutex_init(&mutex, NULL);

    q = queue_new(t);

    pthread_t threads[t];
    for (int i = 0; i < t; i++) {
        pthread_create(
            &(threads[i]), NULL, (void *(*) (void *) ) worker_thread, (void *) threads[i]);
    }
    while (1) {
        uintptr_t fd = listener_accept(&sock);
        queue_push(q, (void *) fd);
    }

    return EXIT_SUCCESS;
}

void handle_connection(int connfd) {

    conn_t *conn = conn_new(connfd);

    const Response_t *res = conn_parse(conn);
    if (res != NULL) {
        conn_send_response(conn, res);
    } else {
        const Request_t *req = conn_get_request(conn);
        if (req == &REQUEST_GET) {
            handle_get(conn);
        } else if (req == &REQUEST_PUT) {
            handle_put(conn);
        } else {
            handle_unsupported(conn);
        }
    }
    conn_delete(&conn);
}

void handle_get(conn_t *conn) {
    char *head = conn_get_header(conn, "Request-Id");
    if (!head) {
        head = "0";
    }
    const Response_t *res = NULL;
    char *uri = conn_get_uri(conn);
    pthread_mutex_lock(&mutex);
    rwlock_t *val = find(&L, uri);
    if (val == NULL) {
        val = rwlock_new(N_WAY, 1);
        insert(&L, uri, val);
    }
    pthread_mutex_unlock(&mutex);
    reader_lock(val);
    struct stat st;
    if (stat(uri, &st) == 0) {
        if (S_ISDIR(st.st_mode)) {
            res = &RESPONSE_FORBIDDEN;
            conn_send_response(conn, res);
            fprintf(stderr, "GET,/%s,403,%s\n", uri, head);
        }
    }
    int fd = open(uri, O_RDONLY);
    if (fd < 0) {
        if (errno == ENOENT) {
            res = &RESPONSE_NOT_FOUND;
            conn_send_response(conn, res);
            fprintf(stderr, "GET,/%s,404,%s\n", uri, head);
        } else if (errno == EACCES) {
            res = &RESPONSE_FORBIDDEN;
            conn_send_response(conn, res);
            fprintf(stderr, "GET,/%s,403,%s\n", uri, head);
        } else {
            res = &RESPONSE_INTERNAL_SERVER_ERROR;
            conn_send_response(conn, res);
            fprintf(stderr, "GET,/%s,500,%s\n", uri, head);
        }
    }
    struct stat s;
    fstat(fd, &s);
    off_t f_size = s.st_size;
    res = conn_send_file(conn, fd, f_size);
    fprintf(stderr, "GET,/%s,200,%s\n", uri, head);
    reader_unlock(val);
    close(fd);
}

void handle_unsupported(conn_t *conn) {
    // send responses
    conn_send_response(conn, &RESPONSE_NOT_IMPLEMENTED);
    char *uri = conn_get_uri(conn);
    char *head = conn_get_header(conn, "Request-Id");
    if (!head) {
        head = "0";
    }
    fprintf(stderr, "unsupported request,/%s,501,%s\n", uri, head);
}

void handle_put(conn_t *conn) {
    char *head = conn_get_header(conn, "Request-Id");
    if (!head) {
        head = "0";
    }
    char *uri = conn_get_uri(conn);
    const Response_t *res = NULL;
    pthread_mutex_lock(&mutex);
    rwlock_t *lock = find(&L, uri);
    if (lock == NULL) {
        lock = rwlock_new(N_WAY, 1);
        insert(&L, uri, lock);
    }
    pthread_mutex_unlock(&mutex);
    writer_lock(lock);
    bool existed = access(uri, F_OK) == 0;
    int fd = open(uri, O_CREAT | O_TRUNC | O_WRONLY, 0600);
    if (fd < 0) {
        if (errno == EACCES || errno == EISDIR || errno == ENOENT) {
            res = &RESPONSE_FORBIDDEN;
            conn_send_response(conn, res);
            fprintf(stderr, "PUT,/%s,403,%s\n", uri, head);
        } else {
            res = &RESPONSE_INTERNAL_SERVER_ERROR;
            conn_send_response(conn, res);
            fprintf(stderr, "PUT,/%s,500,%s\n", uri, head);
        }
    }
    res = conn_recv_file(conn, fd);
    if (res == NULL && existed) {
        res = &RESPONSE_OK;
        conn_send_response(conn, res);
        fprintf(stderr, "PUT,/%s,200,%s\n", uri, head);
    } else if (res == NULL && !existed) {
        res = &RESPONSE_CREATED;
        conn_send_response(conn, res);
        fprintf(stderr, "PUT,/%s,201,%s\n", uri, head);
    }
    writer_unlock(lock);
    close(fd);
}
