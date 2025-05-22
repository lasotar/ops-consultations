#include "common.h"

#define MAX_EVENTS 2
#define BUF_SIZE 128

int main(int argc, char **argv)
{
    if (argc != 3)
    {
        fprintf(stderr, "Usage: %s server_ip server_port\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    signal(SIGPIPE, SIG_IGN);

    int sockfd = connect_tcp_socket(argv[1], argv[2]);

    // Set socket non-blocking
    int flags = fcntl(sockfd, F_GETFL, 0);
    if (flags == -1)
        ERR("fcntl F_GETFL");
    if (fcntl(sockfd, F_SETFL, flags | O_NONBLOCK) == -1)
        ERR("fcntl F_SETFL");

    printf("Connected to server (fd = %d)\n", sockfd);

    int epfd = epoll_create1(0);
    if (epfd < 0)
        ERR("epoll_create");

    struct epoll_event ev, events[MAX_EVENTS];

    ev.events = EPOLLIN;
    ev.data.fd = sockfd;
    if (epoll_ctl(epfd, EPOLL_CTL_ADD, sockfd, &ev) < 0)
        ERR("epoll_ctl sockfd");

    ev.events = EPOLLIN;
    ev.data.fd = STDIN_FILENO;
    if (epoll_ctl(epfd, EPOLL_CTL_ADD, STDIN_FILENO, &ev) < 0)
        ERR("epoll_ctl stdin");

    char buf[BUF_SIZE];

    while (1)
    {
        int n = epoll_wait(epfd, events, MAX_EVENTS, -1);
        if (n < 0)
            ERR("epoll_wait");

        for (int i = 0; i < n; ++i)
        {
            int fd = events[i].data.fd;

            if (fd == STDIN_FILENO)
            {
                // Read one line from stdin (blocking), send to server
                ssize_t len = read(STDIN_FILENO, buf, BUF_SIZE - 1);
                if (len > 0)
                {
                    buf[len] = '\0';

                    ssize_t sent = 0;
                    while (sent < len)
                    {
                        ssize_t w = bulk_write(sockfd, buf + sent, len - sent);
                        if (w < 0)
                        {
                            fprintf(stderr, "Server closed the connection\n");
                            close(sockfd);
                            exit(EXIT_FAILURE);
                        }
                        sent += w;
                    }
                }
                else if (len == 0)
                {
                    fprintf(stderr, "EOF on stdin, exiting.\n");
                    close(sockfd);
                    exit(EXIT_SUCCESS);
                }
                else
                {
                    perror("read stdin");
                    close(sockfd);
                    exit(EXIT_FAILURE);
                }
            }
            else if (fd == sockfd)
            {
                // Read all available data from socket until EAGAIN
                while (1)
                {
                    ssize_t len = read(sockfd, buf, BUF_SIZE - 1);
                    if (len > 0)
                    {
                        buf[len] = '\0';
                        printf("%s", buf);
                        fflush(stdout);
                    }
                    else if (len == 0)
                    {
                        fprintf(stderr, "Disconnected from server.\n");
                        close(sockfd);
                        exit(EXIT_SUCCESS);
                    }
                    else
                    {
                        if (errno == EAGAIN || errno == EWOULDBLOCK)
                        {
                            // No more data right now
                            break;
                        }
                        else
                        {
                            perror("read socket");
                            close(sockfd);
                            exit(EXIT_FAILURE);
                        }
                    }
                }
            }
        }
    }
}

