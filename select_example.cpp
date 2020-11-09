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
#include <stdio.h>
#include <errno.h>

// Compile:
// g++ -std=c++11 select_example.cpp -o select_example

#define PORTNUM 1500 // Port > 1024 because program will not work not as root.

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

    while (true)
    {
        fd_set read_flags, write_flags;
        FD_ZERO(&read_flags);

        FD_SET(master_socket_fd, &read_flags);
        for (auto iter = slave_sockets.begin(); iter != slave_sockets.end(); iter++)
        {
            FD_SET(*iter, &read_flags);
        }

        int max = std::max(master_socket_fd,
                           *std::max_element(slave_sockets.begin(), slave_sockets.end()));

        int ready_desc = select(max + 1, &read_flags, &write_flags, (fd_set *)0, NULL);
        if (ready_desc == -1)
        {
            die("Error of calling select");
        }
        else
        {
            std::cout << "Number of ready descriptors: " << ready_desc << std::endl;
        }

        for (auto iter = slave_sockets.begin(); iter != slave_sockets.end(); iter++)
        {
            if (FD_ISSET(*iter, &read_flags))
            {
                static char buf[1024];
                int recv_size = recv(*iter, buf, 1024, MSG_NOSIGNAL);

                if ((recv_size == 0) && (errno != EAGAIN))
                {
                    shutdown(*iter, SHUT_RDWR);
                    close(*iter);
                    slave_sockets.erase(iter);
                }
                else if (recv_size != 0)
                {
                    send(*iter, buf, recv_size, MSG_NOSIGNAL);
                }
            }
        }

        if (FD_ISSET(master_socket_fd, &read_flags))
        {
            struct sockaddr_in client_addr;
            socklen_t slen = sizeof(client_addr);
            memset(&client_addr, 0, sizeof(client_addr));
            int client_fd = accept(master_socket_fd, (struct sockaddr *)&client_addr, &slen);
            if (client_fd == -1)
            {
                if (errno != EWOULDBLOCK || errno != EAGAIN)
                    die("Error of calling accept");
            }
            set_nonblock_mode(client_fd);
            slave_sockets.insert(client_fd);
        }
    }

    return 0;
}
