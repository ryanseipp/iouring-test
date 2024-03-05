#include <liburing.h>

// separate

#include <arpa/inet.h>
#include <liburing/io_uring.h>
#include <netinet/in.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define ACCEPT 123
#define KERNEL_BACKLOG 1024
#define ENTRIES 1024
#define MAX_CONNECTIONS 2048
#define MAX_BUFFER 1024

#define OP_ACCEPT 1
#define OP_RECV 2
#define OP_SEND 3
#define OP_CLOSE 4

typedef struct sockaddr_in sockaddr_in;
typedef struct io_uring io_uring;
typedef struct io_uring_params io_uring_params;
typedef struct io_uring_sqe io_uring_sqe;
typedef struct io_uring_cqe io_uring_cqe;

union event {
    struct {
        int32_t fd;
        uint32_t op;
    } data;
    uint64_t data_as_u64;
};

const char STANDARD_RESPONSE[] =
    "HTTP/1.1 200 OK\r\nContent-type: text/html\r\nContent-length: "
    "17\r\n\r\nHave a nice day!\n";

int32_t listen_on_addr(uint32_t addr, uint16_t port) {
    int32_t err = 0;

    int32_t sockfd = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, IPPROTO_TCP);
    if (sockfd < 0) {
        return err;
    }

    err = setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &(int){1}, sizeof(int));
    if (err < 0) {
        goto errdefer;
    }

    sockaddr_in sockaddr = {.sin_family = AF_INET,
                            .sin_addr = {htonl(addr)},
                            .sin_port = htons(port),
                            .sin_zero = {0}};

    err = bind(sockfd, (struct sockaddr *)&sockaddr, sizeof(sockaddr_in));
    if (err < 0) {
        goto errdefer;
    }

    err = listen(sockfd, KERNEL_BACKLOG);
    if (err < 0) {
        goto errdefer;
    }

    fprintf(stdout, "Server listening on %s:%d...\n",
            inet_ntoa(sockaddr.sin_addr), port);

    return sockfd;

errdefer:
    close(sockfd);
    return err;
}

static int init_io_uring(io_uring *ring, uint32_t entries) {
    int err = 0;
    io_uring_params params;
    memset(&params, 0, sizeof(params));
    params.cq_entries = entries;
    params.flags = IORING_SETUP_CQSIZE | IORING_SETUP_SUBMIT_ALL;

    err = io_uring_queue_init_params(entries, ring, &params);

    return err;
}

static inline void prepare_accept(io_uring *ring, int32_t sockfd) {
    io_uring_sqe *sqe = io_uring_get_sqe(ring);
    // if (sqe == nullptr) {
    //     return false;
    // }

    io_uring_prep_multishot_accept(sqe, sockfd, NULL, NULL, 0);
    union event data = {.data = {.fd = -1, .op = OP_ACCEPT}};
    sqe->user_data = data.data_as_u64;

    // return true;
}

static inline void prepare_recv(io_uring *ring, int32_t connfd, uint8_t *buf) {
    io_uring_sqe *sqe = io_uring_get_sqe(ring);
    // if (sqe == nullptr) {
    //     return false;
    // }

    io_uring_prep_recv(sqe, connfd, buf, MAX_BUFFER, 0);
    union event data = {.data = {.fd = connfd, .op = OP_RECV}};
    sqe->user_data = data.data_as_u64;

    // return true;
}

static inline void prepare_send(io_uring *ring, int32_t connfd, const char *msg,
                                uint32_t msglen) {
    io_uring_sqe *sqe = io_uring_get_sqe(ring);
    // if (sqe == nullptr) {
    //     return false;
    // }

    io_uring_prep_send(sqe, connfd, msg, msglen, 0);
    union event data = {.data = {.fd = connfd, .op = OP_SEND}};
    sqe->user_data = data.data_as_u64;

    // return true;
}

static inline void prepare_close(io_uring *ring, int32_t connfd) {
    io_uring_sqe *sqe = io_uring_get_sqe(ring);
    // if (sqe == nullptr) {
    //     return false;
    // }

    io_uring_prep_close(sqe, connfd);
    union event data = {.data = {.fd = connfd, .op = OP_CLOSE}};
    sqe->user_data = data.data_as_u64;

    // return true;
}

int main(int argc, char **argv) {
    const int response_len = strlen(STANDARD_RESPONSE);
    int err = 0;
    io_uring_cqe *cqes[ENTRIES];
    printf("Response length: %d\n", response_len);

    int32_t sockfd = listen_on_addr(INADDR_LOOPBACK, 8000);
    if (sockfd < 0) {
        err = sockfd;
        fprintf(
            stderr,
            "Couldn't create socket to listen for incoming connections: %s\n",
            strerror(err));
        goto defer_sock;
    }

    io_uring ring;
    err = init_io_uring(&ring, ENTRIES);
    if (err < 0) {
        fprintf(stderr, "Couldn't initialize io-uring: %s\n", strerror(-err));
        goto defer_sock;
    }

    err = io_uring_register_files(&ring, &sockfd, 1);
    if (err < 0) {
        fprintf(stderr, "Failed to register socket fd to io_uring: %s\n",
                strerror(-err));
        goto defer_sock;
    }

    err = io_uring_register_ring_fd(&ring);
    if (err < 0) {
        fprintf(stderr, "Failed to register ring fd: %s\n", strerror(-err));
    }

    uint8_t *buffer_mem = calloc(MAX_CONNECTIONS * MAX_BUFFER, sizeof(uint8_t));
    if (buffer_mem == NULL) {
        err = -1;
        fprintf(stderr, "Couldn't allocate buffers for incoming connections\n");
        goto defer;
    }

    prepare_accept(&ring, sockfd);
    uint32_t buf_idx = 0;

    while (true) {
        err = io_uring_submit_and_wait(&ring, 1);
        if (err == -EINTR) {
            continue;
        }
        if (err < 0) {
            goto defer;
        }

        int count = io_uring_peek_batch_cqe(&ring, &cqes[0], ENTRIES / 2);
        for (int i = 0; i < count; i++) {
            io_uring_cqe *cqe = cqes[i];
            union event event = {.data_as_u64 = cqe->user_data};

            if (cqe->res < 0) {
                int cqe_err = -cqe->res;

                if (cqe_err == EPIPE) {
                    fprintf(stdout, "WARN: EPIPE received\n");
                } else if (cqe_err == ECONNRESET) {
                    fprintf(stdout, "WARN: ECONNRESET received\n");
                } else {
                    fprintf(stderr,
                            "ERR(%d): %s for CQE {.op = %u, .fd = %d}\n",
                            cqe_err, strerror(cqe_err), event.data.op,
                            event.data.fd);
                    goto defer;
                }
            }

            switch (event.data.op) {
                case OP_ACCEPT:
                    if ((cqe->flags & IORING_CQE_F_MORE) == 0) {
                        prepare_accept(&ring, sockfd);
                    }
                    prepare_recv(&ring, cqe->res,
                                 &buffer_mem[buf_idx * MAX_BUFFER]);
                    buf_idx = (buf_idx + 1) % MAX_CONNECTIONS;
                    break;
                case OP_RECV:
                    if (cqe->res == 0) {
                        prepare_close(&ring, event.data.fd);
                    } else {
                        prepare_send(&ring, event.data.fd, STANDARD_RESPONSE,
                                     response_len);
                    }
                    break;
                case OP_SEND:
                    prepare_recv(&ring, event.data.fd,
                                 &buffer_mem[buf_idx * MAX_BUFFER]);
                    buf_idx = (buf_idx + 1) % MAX_CONNECTIONS;
                    break;
                case OP_CLOSE:
                default:
                    break;
            }
        }

        io_uring_cq_advance(&ring, count);
    }

defer:
    io_uring_queue_exit(&ring);
defer_sock:
    close(sockfd);

    return err;
}
