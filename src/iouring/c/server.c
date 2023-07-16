#include <asm-generic/socket.h>
#include <liburing.h>
#include <liburing/io_uring.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <unistd.h>

#define ACCEPT 123

/*
 * This code is broken... probably by a lot.
 * I am not a C wizard. Anyways, on to zig!
 */

int ENTRIES = 1024;
struct io_uring ring;

const char *standard_response = "HTTP/1.0 200 OK\r\n"
                                "Content-type: text/html\r\n"
                                "Content-length: 17\r\n"
                                "\r\n"
                                "Have a nice day!\n";

int setup_listening_socket(int port, int *sock_fd) {
  struct sockaddr_in srv_addr;
  int enable = 1;

  *sock_fd = socket(PF_INET, SOCK_STREAM, 0);
  setsockopt(*sock_fd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int));

  memset(&srv_addr, 0, sizeof(srv_addr));
  srv_addr.sin_family = AF_INET;
  srv_addr.sin_port = htons(port);
  srv_addr.sin_addr.s_addr = htonl(INADDR_ANY);

  if (bind(*sock_fd, (const struct sockaddr *)&srv_addr, sizeof(srv_addr))) {
    close(*sock_fd);
    return -1;
  }
  listen(*sock_fd, 10);
  return 0;
}

void add_accept(int sock_fd, struct sockaddr_in *client_addr,
                socklen_t *client_addrlen) {
  struct io_uring_sqe *sqe = io_uring_get_sqe(&ring);
  io_uring_prep_accept(sqe, sock_fd, (struct sockaddr *)client_addr,
                       client_addrlen, 0);
  io_uring_sqe_set_data(sqe, (void *)ACCEPT);
  io_uring_submit(&ring);
}

void add_write_and_close(int fd) {
  struct io_uring_sqe *sqe;
  sqe = io_uring_get_sqe(&ring);
  io_uring_prep_write(sqe, fd, standard_response, strlen(standard_response), 0);
  sqe->flags |= IOSQE_IO_LINK;

  sqe = io_uring_get_sqe(&ring);
  io_uring_prep_close(sqe, fd);
  io_uring_submit(&ring);
}

void server_loop(int sock_fd) {
  struct io_uring_cqe *cqe;
  struct sockaddr_in client_addr;
  socklen_t client_addrlen = sizeof(client_addr);
  int peek_result = 0;

  add_accept(sock_fd, &client_addr, &client_addrlen);

  while (1) {
    peek_result = io_uring_peek_cqe(&ring, &cqe);

    if (!peek_result) {
      if (cqe->user_data == ACCEPT && cqe->res >= 0) {
        add_write_and_close(cqe->res);
        add_accept(sock_fd, &client_addr, &client_addrlen);
      } else {
        // nothing to do
      }

      io_uring_cqe_seen(&ring, cqe);
    }
  }
}

void sigint_handler(int signo) {
  printf("^C pressed, shutting down.\n");
  io_uring_queue_exit(&ring);
  exit(0);
}

int main(int argc, char *argv[]) {
  struct io_uring_params params;
  int sock_fd;

  memset(&params, 0, sizeof(params));
  params.flags |= IORING_SETUP_SQPOLL;
  params.sq_thread_idle = 120000; // 2 mins

  io_uring_queue_init_params(ENTRIES, &ring, &params);

  signal(SIGINT, sigint_handler);
  if (!setup_listening_socket(8000, &sock_fd)) {
    server_loop(sock_fd);
  }

  printf("Server shut down.\n");
}
