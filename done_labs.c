#include "common.h"

#define MAX_CLIENTS 2
#define MAX_EVENTS (MAX_CLIENTS + 1)
#define MAX_MESSAGE_SIZE 64
#define MAX_USERNAME_SIZE 32

typedef struct shared
{
    int active_connections;
    int client_fds[MAX_CLIENTS];
    int logged_in[MAX_CLIENTS];
    char usernames[MAX_CLIENTS][MAX_USERNAME_SIZE];
} shared_t;

void client_arr_init(int* client_arr)
{
    for (int i = 0; i < MAX_CLIENTS; i++)
    {
        client_arr[i] = -1;
    }
}

int shared_getid(shared_t* shared, int fd) {
    for (int i = 0; i < shared->active_connections; i++) {
        if (shared->client_fds[i] == fd) {
            return i;
        }
    }
    return -1;
}

int insert_client(shared_t* shared, int client_fd)
{
    if (shared->active_connections >= MAX_CLIENTS)
        return -1;

    int index = shared->active_connections;
    shared->client_fds[index] = client_fd;
    shared->logged_in[index] = 0;
    memset(shared->usernames[index], 0, MAX_USERNAME_SIZE);
    shared->active_connections++;
    return index;
}

int remove_client(shared_t* shared, int client_fd)
{
    int index = shared_getid(shared, client_fd);
    if (index == -1)
        return -1;

    int last = shared->active_connections - 1;

    if (index != last)
    {
        shared->client_fds[index] = shared->client_fds[last];
        shared->logged_in[index] = shared->logged_in[last];
        memcpy(shared->usernames[index], shared->usernames[last], MAX_USERNAME_SIZE);
    }

    shared->client_fds[last] = -1;
    shared->logged_in[last] = 0;
    memset(shared->usernames[last], 0, MAX_USERNAME_SIZE);
    shared->active_connections--;

    return index;
}

int is_first_logged_in(shared_t* shared) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (shared->logged_in[i] == 1) {
            return 0;
        }
    }
    return 1;
}

void socket_write(int fd, char *buf, size_t count)
{
    if (write(fd, buf, count) < 0)
    {
        if (EPIPE != errno)
            ERR("write");
    }
}

void notify_all_clients_ex(int* client_arr, int except, char *buf, size_t count)
{
    for (int i = 0; i < MAX_CLIENTS; i++)
    {
        if (client_arr[i] != -1 && client_arr[i] != except)
        {
            socket_write(client_arr[i], buf, count);
        }
    }
}

void handle_server(int server_fd, int epoll_fd, shared_t* shared)
{
    int client_fd = add_new_client(server_fd);

    if ((shared->active_connections) >= MAX_CLIENTS)
    {
        socket_write(client_fd, "Server is full\n", 15);
        close(client_fd);
        return;
    }

    struct epoll_event client_ev;
    client_ev.events = EPOLLIN;
    client_ev.data.fd = client_fd;

    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &client_ev) < 0)
    {
        ERR("epoll_ctl");
    }
    
    insert_client(shared, client_fd);
    socket_write(client_fd, "Please enter your username\n", 27);
    notify_all_clients_ex(shared->client_fds, client_fd, "User logging in...\n", 19);
    printf("Client socket %d connected\n", client_fd);
}

void handle_client(int fd, int epoll_fd, shared_t* shared)
{
    char buffer[MAX_MESSAGE_SIZE];
    int client_open = 1;
    int buf_index = 0;

    int index = shared_getid(shared, fd);
    while (1)
    {
        char buf;
        int chars_read;
        chars_read = bulk_read(fd, &buf, 1);
        if (chars_read <= 0)
        {
            if (!shared->logged_in[index])
            {
                char notification[MAX_MESSAGE_SIZE];
                sprintf(notification, "[%d] failed to log in\n", fd);
                notify_all_clients_ex(shared->client_fds, fd, notification, strlen(notification));
            }
            else
            {
                char notification[MAX_MESSAGE_SIZE];
                sprintf(notification, "[%s] is gone!\n", shared->usernames[index]);
                notify_all_clients_ex(shared->client_fds, fd, notification, strlen(notification));
                printf("%d known as %s has disconnected\n", fd, shared->usernames[index]);
            }


            epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, NULL);
            close(fd);
            client_open = 0;
            remove_client(shared, fd);
            break;
        }

        buffer[buf_index++] = buf;

        if (buf == '\n')
        {
            buffer[buf_index - 1] = '\0';
            break;
        }
    }
    if (!client_open)
    {
        return;
    }
    
    if (shared->logged_in[index] == 0)
    {
        // Log in
        strcpy(shared->usernames[index], buffer);
        printf("[%d] logged in as %s\n", fd, shared->usernames[index]);

        char notification[MAX_MESSAGE_SIZE];
        if (is_first_logged_in(shared))
        {
            socket_write(fd, "You are the first one here!\n", 28);
        }
        else
        {
            socket_write(fd, "Current users:\n", 16);
            for (int i = 0; i < MAX_CLIENTS; i++)
            {
                if (shared->logged_in[i] == 1 && i != index)
                {
                    // Send username followed by newline
                    char line[MAX_USERNAME_SIZE + 1];
                    int len = snprintf(line, sizeof(line), "%s\n", shared->usernames[i]);
                    socket_write(fd, line, len);
                }
            }
        }
        shared->logged_in[index] = 1;
        snprintf(notification, MAX_MESSAGE_SIZE, "User %s logged in\n", shared->usernames[index]);
        notify_all_clients_ex(shared->client_fds, fd, notification, strlen(notification));
    }
    else
    {
        char message[MAX_MESSAGE_SIZE + MAX_USERNAME_SIZE + 3];
        int len = snprintf(message, sizeof(message), "%s: %s\n", shared->usernames[index], buffer);
        notify_all_clients_ex(shared->client_fds, fd, message, len);
        printf("[%d] %s\n", fd, message);
    }
}

int main(int argc, char** argv)
{
    if (argc > 2)
    {
        ERR("argc");
    }

    int port;
    if (argc == 1)
    {
        port = 12345;
    }
    else
    {
        port = atoi(argv[1]);
    }

    if (port < 1024 || port > 65535)
    {
        ERR("invalid argument");
    }

    signal(SIGPIPE, SIG_IGN); // Block SIGPIPE

    int server_fd = bind_tcp_socket(port, MAX_CLIENTS);
    // Set server to non-blocking
    int flags = fcntl(server_fd, F_GETFL, 0);
    fcntl(server_fd, F_SETFL, flags | O_NONBLOCK);

    printf("Server listening on port %d...\n", port);

    int epoll_fd;
    if ((epoll_fd = epoll_create1(0)) < 0)
    {
        ERR("epoll_create");
    }

    shared_t shared;
    client_arr_init(shared.client_fds);
    shared.active_connections = 0;

    struct epoll_event ev, events[MAX_EVENTS];
    ev.events = EPOLLIN;
    ev.data.fd = server_fd;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, server_fd, &ev) < 0)
    {
        ERR("epoll_ctl");
    }

    int ready;
    while (1)
    {
        if ((ready = epoll_wait(epoll_fd, events, MAX_EVENTS, -1)) < 0)
        {
            ERR("epoll_wait");
        }

        for (int i = 0; i < ready; i++)
        {
            int fd = events[i].data.fd;

            if (fd == server_fd)
            {
                handle_server(server_fd, epoll_fd, &shared);
            }
            else
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
