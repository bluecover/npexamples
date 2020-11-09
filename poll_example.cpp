#include <iostream>
#include <set>
#include <algorithm>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <errno.h>

// Compile:
// g++ -std=c++11 poll_example.cpp -o poll_example

// Artifical limit for Poll since it is not limited to 1024 file descriptors like Select.
#define POLL_SIZE 2048

// Timeout for poll function.
#define POLL_TIMEOUT -1

// Port > 1024 because program will not work not as root.
#define PORTNUM 1500

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

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
    // Create listen(master) socket.
    int listen_socket_fd = -1;
    if ((listen_socket_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) == -1)
    {
        die("Error of calling socket");
    }

    // Set SO_REUSEADDR in socket options.
    // For UDP SO_REUSEADDR may mean some problems...
    int optval = 1;
    if (setsockopt(listen_socket_fd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(int)) == -1)
    {
        die("Error of calling setsockopt");
    }

    // Link socket with address.
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY); // 0.0.0.0
    server_addr.sin_port = htons((int)PORTNUM);
    if (bind(listen_socket_fd, (struct sockaddr *)(&server_addr), sizeof(server_addr)) == -1)
    {
        die("Error of calling bind");
    }

    // Set listen socket to unblocking mode.
    set_nonblock_mode(listen_socket_fd);

    // Server is ready to get SOMAXCONN connection requests (128).
    // This is *SHOULD* be enought to call accept and create child process.
    if (listen(listen_socket_fd, SOMAXCONN) == -1)
    {
        die("Error of calling listen");
    }

    std::cout << "Server is ready: " << inet_ntoa(server_addr.sin_addr) << std::endl;

    // Put listen socket at the beginning of all polling descriptors.
    struct pollfd descriptors[POLL_SIZE];
    descriptors[0].fd = listen_socket_fd;
    descriptors[0].events = POLLIN;

    // Socket set for connections(slave sockets).
    std::set<int> slave_sockets;

    while (true)
    {
        int index = 1;
        for (auto iter = slave_sockets.begin(); iter != slave_sockets.end(); iter++)
        {
            descriptors[index].fd = *iter;
            descriptors[index].events = POLLIN;
            ++index;
        }

        // Plus 1 for the listen socket.
        int poll_size = 1 + slave_sockets.size();

        int ready_fd = poll(descriptors, poll_size, POLL_TIMEOUT);
        if (ready_fd == -1)
        {
            die("Error of calling poll");
        }
        else
        {
            std::cout << "Number of ready descriptors: " << ready_fd << std::endl;
        }

        for (int i = 0; i < poll_size; i++)
        {
            if (descriptors[i].revents & POLLIN)
            {
                if (i > 0) // Slave socket
                {
                    static char buf[1024];
                    int len = recv(descriptors[i].fd, buf, 1024, MSG_NOSIGNAL);

                    if ((len == 0) && (errno != EAGAIN))
                    {
                        // If we got event TO READ, but actually CANNOT read, this means we should
                        // CLOSE connection. This is how POLL and EPOLL works.
                        shutdown(descriptors[i].fd, SHUT_RDWR);
                        close(descriptors[i].fd);
                        slave_sockets.erase(descriptors[i].fd);
                    }
                    else if (len != 0)
                    {
                        send(descriptors[i].fd, buf, len, MSG_NOSIGNAL);
                    }
                }
                else // Master socket
                {
                    struct sockaddr_in client_addr;
                    socklen_t slen = sizeof(client_addr);
                    memset(&client_addr, 0, sizeof(client_addr));
                    int client_fd = accept(listen_socket_fd, (struct sockaddr *)&client_addr, &slen);
                    if (client_fd == -1)
                    {
                        if (errno != EWOULDBLOCK || errno != EAGAIN)
                            die("Error of calling accept");
                    }
                    std::cout << "Client = " << inet_ntoa(client_addr.sin_addr) << std::endl;
                    set_nonblock_mode(client_fd);
                    slave_sockets.insert(client_fd);
                }
            }
        }
    }

    return 0;
}
