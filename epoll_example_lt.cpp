#include <iostream>
#include <set>
#include <map>
#include <algorithm>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <errno.h>
#include <stdio.h>

// Compile:
// g++ -std=c++11 2_epoll_server.cpp -o epoll_server

// Number of events which Epoll will return at once.
#define MAX_EVENTS 32

// Port > 1024 because program will not work not as root.
#define PORTNUM 1500

int set_nonblock_mode(int fd)
{
    int flags;
#if defined(O_NONBLOCK)
    if (-1 == (flags = fcntl(fd, F_GETFL, 0)))
    {
        flags = 0;
    }

    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
#else
    flags = 1;
    return ioctl(fd, FIONBIO, &flags);
#endif
}

void die(const char *msg)
{
    // Move latest errno to stderr with message msg.
    perror(msg);

    // perror - POSIX standard function.
    // or use strerror(errno);

    exit(1); // TODO EXIT_FAILURE
}

// Single-threaded echo server handles 1024 < clients in multiplexing mode with select.
// Use client: telnet 127.0.0.1 12345
// just type anything...
int main(int argc, char **argv)
{
    // Create socket
    int master_socket_fd = -1;
    if ((master_socket_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) == -1)
    {
        die("Error of calling socket");
    }

    // For UDP SO_REUSEADDR may mean some problems...
    int optval = 1;
    if (setsockopt(master_socket_fd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(int)) == -1)
    {
        die("Error of calling setsockopt");
    }

    std::set<int> slave_sockets;
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY); // 0.0.0.0
    server_addr.sin_port = htons((int)PORTNUM);

    // Link socket with address
    if (bind(master_socket_fd, (struct sockaddr *)(&server_addr), sizeof(server_addr)) == -1)
    {
        die("Error of calling bind");
    }

    set_nonblock_mode(master_socket_fd);

    // Server is ready to get SOMAXCONN connection requests (128).
    // This is *SHOULD* be enought to call accept and create child process.
    if (listen(master_socket_fd, SOMAXCONN) == -1)
    {
        die("Error of calling listen");
    }

    std::cout << "Server is ready: " << inet_ntoa(server_addr.sin_addr) << std::endl;

    int epoll = epoll_create1(0);
    struct epoll_event master_event;
    master_event.data.fd = master_socket_fd;
    master_event.events = EPOLLIN;

    // Register event
    epoll_ctl(epoll, EPOLL_CTL_ADD, master_socket_fd, &master_event);

    std::map<int, sockaddr_in> clients;

    while (true)
    {
        struct epoll_event events[MAX_EVENTS];
        int ready_desc = epoll_wait(epoll, events, MAX_EVENTS, -1);

        if (ready_desc == -1)
        {
            die("Error of calling poll");
        }
        else
        {
            std::cout << "Number of ready descriptors: " << ready_desc << std::endl;
        }

        for (int i = 0; i < ready_desc; i++)
        {
            if (events[i].data.fd == master_socket_fd)
            {
                struct sockaddr_in client_addr;
                socklen_t slen = sizeof(client_addr);
                memset(&client_addr, 0, sizeof(client_addr));
                int slave_socket = accept(master_socket_fd, (struct sockaddr *)&client_addr, &slen);
                if (slave_socket == -1)
                {
                    if (errno != EWOULDBLOCK || errno != EAGAIN)
                        die("Error of calling accept");
                }

                set_nonblock_mode(slave_socket);

                struct epoll_event event;
                event.data.fd = slave_socket;
                event.events = EPOLLIN;
                epoll_ctl(epoll, EPOLL_CTL_ADD, slave_socket, &event);

                clients[slave_socket] = client_addr;
                std::cout << "Client = " << inet_ntoa(client_addr.sin_addr) << std::endl;
            }
            else
            {
                static char buf[1024];
                int recv_size = recv(events[i].data.fd, buf, 1024, MSG_NOSIGNAL);

                std::cout << "Received " << recv_size << " bytes from client "
                          << inet_ntoa(clients[events[i].data.fd].sin_addr) << std::endl;
                if ((recv_size == 0) && (errno != EAGAIN))
                {
                    // If we got event TO READ, but actually CANNOT read, this means we should CLOSE
                    // connection. This is how POLL and EPOLL works.
                    std::cout << "Shut down connection with client "
                              << inet_ntoa(clients[events[i].data.fd].sin_addr) << std::endl;

                    // A file descriptor is removed from an interest list after all the file
                    // descriptors referring to the underlying open file have been closed.
                    shutdown(events[i].data.fd, SHUT_RDWR);
                    close(events[i].data.fd);
                }
                else if (recv_size != 0)
                {
                    send(events[i].data.fd, buf, recv_size, MSG_NOSIGNAL);
                }
            }
        }
    }

    return 0;
}
