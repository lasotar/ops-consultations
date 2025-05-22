#include "common.h"

#define MAX_MESSAGE_SIZE 64
#define MAX_USERNAME_SIZE 32

void usage(char *name)
{
    fprintf(stderr, "USAGE: %s [server_address] [port]\n", name);
    exit(EXIT_FAILURE);
}

int main(int argc, char **argv)
{
    if (argc != 3)
        usage(argv[0]);

    signal(SIGPIPE, SIG_IGN);

    int sockfd = connect_tcp_socket(argv[1], argv[2]);
    int epoll_fd = epoll_create1(0);
    if (epoll_fd < 0)
        ERR("epoll_create");

    struct epoll_event ev, events[2];

    // Add socket to epoll
    ev.events = EPOLLIN;
    ev.data.fd = sockfd;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, sockfd, &ev) < 0)
        ERR("epoll_ctl sockfd");

    // Add stdin to epoll
    ev.events = EPOLLIN;
    ev.data.fd = STDIN_FILENO;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, STDIN_FILENO, &ev) < 0)
        ERR("epoll_ctl stdin");

    char buffer[MAX_MESSAGE_SIZE];
    printf("Connected to server. Waiting for prompt...\n");

    while (1)
    {
        int ready = epoll_wait(epoll_fd, events, 2, -1);
        if (ready < 0)
            ERR("epoll_wait");

        for (int i = 0; i < ready; i++)
        {
            if (events[i].data.fd == STDIN_FILENO)
            {
                // Read from user and send to server
                memset(buffer, 0, sizeof(buffer));
                ssize_t bytes = bulk_read(STDIN_FILENO, buffer, MAX_MESSAGE_SIZE - 1);
                if (bytes <= 0)
                {
                    printf("Input closed.\n");
                    goto cleanup;
                }

                if (bulk_write(sockfd, buffer, bytes) < 0)
                {
                    fprintf(stderr, "Disconnected while writing.\n");
                    goto cleanup;
                }
            }
            else if (events[i].data.fd == sockfd)
            {
                // Read from server and print
                memset(buffer, 0, sizeof(buffer));
                ssize_t bytes = bulk_read(sockfd, buffer, MAX_MESSAGE_SIZE - 1);
                if (bytes <= 0)
                {
                    printf("Server closed connection.\n");
                    goto cleanup;
                }

                set_color(STDOUT_FILENO, SOP_LIGHTGRAY);
                bulk_write(STDOUT_FILENO, buffer, bytes);
                reset_color(STDOUT_FILENO);
            }
        }
    }

cleanup:
    close(sockfd);
    close(epoll_fd);
    return 0;
}

