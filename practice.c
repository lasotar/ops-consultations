#include "common.h"

#define MAX_CLIENTS 4

typedef struct shared
{
    int connections_active;
} shared_t;

void socket_write(int fd, char *buf, size_t count)
{
    if (write(fd, buf, count) < 0)
    {
        if (EPIPE != errno)
            ERR("write");
    }
}

void handle_server(int server_fd, int epoll_fd, shared_t* shared)
{
    if (shared->connections_active == 0)
    {
        int client_fd = add_new_client(server_fd);
        
        struct epoll_event client_ev;
        client_ev.events = EPOLLIN;
        client_ev.data.fd = client_fd;
        epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &client_ev);

        socket_write(client_fd, "Connected\n", 10);
        printf("[%d] Client connected", client_fd);
        shared->connections_active = 1;
    }
}

void handle_client(int client_fd, int epoll_fd, shared_t* shared)
{
    while (1)
    {

    }
}

int main()
{
    int port = 2137;

    signal(SIGPIPE, SIG_IGN);
    int server_fd = bind_tcp_socket(port, MAX_CLIENTS);
    int s_flags = fcntl(server_fd, F_GETFL, 0);
    fcntl(server_fd, F_SETFL, s_flags | O_NONBLOCK);

    printf("Server listening on port 2137...\n");

    int epoll_fd = epoll_create1(0);
    
    struct epoll_event ev, events[MAX_CLIENTS + 1];
    ev.events = EPOLLIN;
    ev.data.fd = server_fd;
    // Add server
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, server_fd, &ev))
    {
        ERR("epoll_ctl");
    }

    shared_t shared;
    shared.connections_active = 0;

    while (1)
    {
        int ready;
        if ((ready = epoll_wait(epoll_fd, events, MAX_CLIENTS + 1, -1)) < 0)
        {
            ERR("epoll_wait");
        }

        for (int i = 0; i < ready; i++)
        {
            int fd = events[i].data.fd;

            if (fd == server_fd)
            {
                handle_server(server_fd, epoll_fd, &shared);
            } else
            {
                handle_client(fd, epoll_fd, &shared);
            }
        }
    }

    if (TEMP_FAILURE_RETRY(close(server_fd)) < 0)
    {
        ERR("close");
    }

    exit(EXIT_SUCCESS);
}
