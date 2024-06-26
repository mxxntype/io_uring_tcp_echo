#include <assert.h>
#include <liburing.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>

#define BACKLOG 512
#define MAX_CONNECTIONS 4096
#define MAX_MESSAGE_LEN 2048
#define IORING_FEAT_FAST_POLL (1U << 5)

void add_accept(struct io_uring* ring, int32_t fd, struct sockaddr* client_addr, socklen_t* client_len);
void add_socket_read(struct io_uring* ring, int32_t fd, size_t size);
void add_socket_write(struct io_uring* ring, int32_t fd, size_t size);

// A description of each connection our server will be handling.
typedef struct {
    int32_t fd;
    uint32_t type;
} connection_info;

enum {
    ACCEPT,
    READ,
    WRITE,
};

connection_info connections[MAX_CONNECTIONS];
char buffers[MAX_CONNECTIONS][MAX_MESSAGE_LEN];

int32_t main(int32_t argc, char* argv[]) {
    // Ensure we have a port provided to us via an argument.
    if (argc < 2) {
        printf("Please supply a port for the server");
        exit(EXIT_FAILURE);
    }

    int32_t portno = strtol(argv[1], NULL, 10);
    struct sockaddr_in serv_addr, client_addr;
    socklen_t client_len = sizeof(client_addr);

    int32_t sock_listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    const int32_t val = 1;
    setsockopt(sock_listen_fd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val));

    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(portno);
    serv_addr.sin_addr.s_addr = INADDR_ANY;

    assert(bind(sock_listen_fd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) >= 0);
    assert(listen(sock_listen_fd, BACKLOG) >= 0);

    struct io_uring_params params;
    struct io_uring ring;
    memset(&params, 0, sizeof(params));

    assert(io_uring_queue_init_params(4096, &ring, &params) >= 0);

    if (!(params.features & IORING_FEAT_FAST_POLL)) {
        printf("IORING_FEAT_FAST_POLL not available in the kernel, quiting...\n");
        exit(0);
    }

    add_accept(&ring, sock_listen_fd, (struct sockaddr*)&client_addr, &client_len);

    // Event loop.
    while (1) {
        struct io_uring_cqe* cqe;
        int32_t ret;

        io_uring_submit(&ring);

        ret = io_uring_wait_cqe(&ring, &cqe);
        assert(ret == 0);

        struct io_uring_cqe* cqes[BACKLOG];
        int32_t cqe_count = io_uring_peek_batch_cqe(&ring, cqes, sizeof(cqes) / sizeof(cqes[0]));

        for (int32_t i = 0; i < cqe_count; ++i) {
            cqe = cqes[i];

            connection_info* user_data = (connection_info*)io_uring_cqe_get_data(cqe);
            uint32_t type = user_data->type;

            if (type == ACCEPT) {
                int32_t sock_conn_fd = cqe->res;
                add_socket_read(&ring, sock_conn_fd, MAX_MESSAGE_LEN);
                add_accept(&ring, sock_listen_fd, (struct sockaddr*)&client_addr, &client_len);
            } else if (type == READ) {
                int32_t bytes_read = cqe->res;
                if (bytes_read <= 0)
                    shutdown(user_data->fd, SHUT_RDWR);
                else
                    add_socket_write(&ring, user_data->fd, bytes_read);
            } else if (type == WRITE)
                add_socket_read(&ring, user_data->fd, MAX_MESSAGE_LEN);

            io_uring_cqe_seen(&ring, cqe);
        }
    }
}

void add_accept(struct io_uring* ring, int32_t fd, struct sockaddr* client_addr, socklen_t* client_len) {
    struct io_uring_sqe* sqe = io_uring_get_sqe(ring);
    io_uring_prep_accept(sqe, fd, client_addr, client_len, 0);

    connection_info* conn_i = &connections[fd];
    conn_i->fd = fd;
    conn_i->type = ACCEPT;

    io_uring_sqe_set_data(sqe, conn_i);
}

void add_socket_read(struct io_uring* ring, int32_t fd, size_t size) {
    struct io_uring_sqe* sqe = io_uring_get_sqe(ring);
    io_uring_prep_recv(sqe, fd, &buffers[fd], size, 0);

    connection_info* conn_i = &connections[fd];
    conn_i->fd = fd;
    conn_i->type = READ;

    io_uring_sqe_set_data(sqe, conn_i);
}

void add_socket_write(struct io_uring* ring, int32_t fd, size_t size) {
    struct io_uring_sqe* sqe = io_uring_get_sqe(ring);
    io_uring_prep_send(sqe, fd, &buffers[fd], size, 0);

    connection_info* conn_i = &connections[fd];
    conn_i->fd = fd;
    conn_i->type = WRITE;

    io_uring_sqe_set_data(sqe, conn_i);
}
