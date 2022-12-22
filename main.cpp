#include <stdlib.h>
#include <unistd.h>
#include <stdarg.h>
#include <string.h>

#include <sys/stat.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <arpa/inet.h>

#include <sstream>
#include <vector>
#include <string>
#include <thread>

using namespace std;

vector<string> split(const string &str, char delimiter)
{
    vector<string> result;
    stringstream ss(str);
    string item;

    while (getline(ss, item, delimiter))
        result.push_back(item);

    return result;
}

void demonize()
{
    int pid = fork();

    if (pid == -1)
    {
        exit(EXIT_FAILURE);
    }
    else if (pid == 0)
    {
        umask(0);
        setsid();

        close(STDIN_FILENO);
        close(STDOUT_FILENO);
        close(STDERR_FILENO);
    }
    else
    {
        exit(EXIT_SUCCESS);
    }
}

int open_server_socket(const string &host, unsigned int port)
{
    int socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_fd == -1)
        exit(EXIT_FAILURE);

    sockaddr_in server_address;
    memset(&server_address, 0, sizeof(server_address));
    server_address.sin_family = AF_INET;
    server_address.sin_port = htons(port);

    unsigned char buffer[sizeof(struct in6_addr)];
    inet_pton(AF_INET, host.c_str(), buffer);
    server_address.sin_addr.s_addr = *((uint32_t *)buffer);

    if (bind(socket_fd, (struct sockaddr *)&server_address, sizeof(server_address)) == -1)
        exit(EXIT_FAILURE);

    if (listen(socket_fd, 32) != 0)
        exit(EXIT_FAILURE);

    return socket_fd;
}

void process_request(const string &request, int socket_fd)
{
    vector<string> lines = split(request, '\n');
    vector<string> first_line = split(lines[0], ' ');

    if (strcasecmp(first_line[0].c_str(), "GET") == 0)
    {
        string file_name = first_line[1];

        if (file_name[0] == '/')
            file_name = string(&file_name[1]);
        file_name = file_name.substr(0, file_name.find('?', 0));

        auto f = fopen(file_name.c_str(), "rb");
        if (f)
        {
            fseek(f, 0, SEEK_END);
            auto size = ftell(f);
            fseek(f, 0, SEEK_SET);

            vector<char> buffer;
            buffer.resize(size);
            fread(buffer.data(), size, 1, f);
            fclose(f);

            string response = string("HTTP/1.0 200 OK\r\n") +
                            "Content-Type: text/html\r\n" +
                            "Content-Length: " + to_string(buffer.size()) + "\r\n" +
                            "\r\n";
            write(socket_fd, response.c_str(), response.size());
            write(socket_fd, buffer.data(), buffer.size());
        }
        else
        {
            char response[] = "HTTP/1.0 404 NOT FOUND\r\n"
                              "Content-Length: 0\r\n"
                              "Content-Type: text/html\r\n\r\n";
            write(socket_fd, response, sizeof(response));
        }
    }
}

void process_connection(int socket)
{
    auto id = this_thread::get_id();

    char buffer[10 * 1024];
    memset(buffer, 0, sizeof(buffer));

    while (true)
    {
        auto size = read(socket, &buffer, 10 * 1024);
        if (size <= 0)
            break;

        process_request(buffer, socket);
        break;
    }
    close(socket);
}

int main(int argc, char *argv[])
{
    string ip, port, directory;
    int opt;
    while ((opt = getopt(argc, argv, "h:p:d:")) != -1)
    {
        switch (opt)
        {
        case 'h':
            ip = optarg;
            break;
        case 'p':
            port = optarg;
            break;
        case 'd':
            directory = optarg;
            break;
        }
    }

    demonize();

    if (ip == "" or port == "" or directory == "")
        exit(EXIT_FAILURE);

    chdir(directory.c_str());

    auto server_socket = open_server_socket(ip, stoi(port));
    while (true)
    {
        int client_socket = -1;
        sockaddr_in client_address;
        socklen_t client_address_length;
        if (-1 == (client_socket = accept(server_socket, (struct sockaddr *)&client_address, &client_address_length)))
            exit(EXIT_FAILURE);

        thread thread(process_connection, client_socket);
        thread.detach();
    }

    close(server_socket);

    exit(EXIT_SUCCESS);
}
