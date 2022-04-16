#define _WINSOCK_DEPRECATED_NO_WARNINGS 1

#include <stdio.h> 
#include <winsock2.h>
#include <ws2tcpip.h> 
#include <string.h> 
#include <map>

#define PORT 8080 

// Link with ws2_32.lib
#pragma comment(lib, "Ws2_32.lib")

typedef struct {
    int    ip_address;
    int    port;
} client_id_t;

typedef union {
    long long       hash;
    client_id_t     id;
} client_hash_t;

#define EXIT_FAILURE -2

class Client {
    int                 socket;
    int                 source_port;

public:
    client_hash_t       hash;
    char source_ip_address[180];

public:
    long long GetSourceHash() { return hash.hash; }
    Client(int socket)
    {
        this->socket = socket;
    }

    int RecivedData(char * input_buffer, int buffer_size)
    {
        int read_size = recv(socket, input_buffer, buffer_size, 0);
        if (read_size < 0) {
            perror("accept");
            closesocket(socket);
            return -1;
        }
        return read_size;
    }

    int SendData(const char * data, int size)
    {
        return send(socket, data, size, 0);
    }

    void Close()
    {
        closesocket(socket);
    }

    void ShowAcceptInfo()
    {
        struct sockaddr_storage addr;
        struct sockaddr_in name;
        socklen_t len = sizeof addr;
        socklen_t namelen = sizeof(name);
        char local_ip_name[80];

        getsockname(socket, (struct sockaddr*)&name, &namelen);
        inet_ntop(AF_INET, &name.sin_addr, local_ip_name, 80);
        
        getpeername(socket, (struct sockaddr*)&addr, &len);

        if (addr.ss_family == AF_INET) 
        {
            struct sockaddr_in *s = (struct sockaddr_in *)&addr;
            source_port = ntohs(s->sin_port);
            inet_ntop(AF_INET, &s->sin_addr, source_ip_address, sizeof(source_ip_address));
            
            this->hash.id.ip_address = s->sin_addr.S_un.S_addr;
            this->hash.id.port = source_port;

            fprintf(stdout, "TCP Connection from %s:%d to %s:%d established\n",
                source_ip_address,
                source_port,
                local_ip_name,
                PORT
            );
        }
        else { // AF_INET6
#if false
            struct sockaddr_in6 *s = (struct sockaddr_in6 *)&addr;
            source_port = ntohs(s->sin6_port);
            inet_ntop(AF_INET6, &s->sin6_addr, source_ip_address, sizeof(source_ip_address));
#else
            printf("This version of chat server supports only IPv4 protocol\n");
            closesocket(socket);
#endif
        }

    }
};

// ---------------------------------------------
// Keep all connected clients to a hashed object
// ----------------------------------------------
std::map<long long, Client *>       chat_clients;


// ---------------------------------------------
//  This is client thread
// ---------------------------------------------
DWORD WINAPI ClientProcedure(CONST LPVOID lpParam)
{
    const char * hello = "Welcome to simple chat\r\n";
    Client * client = (Client *) lpParam;
    char   message[4096];

    client->ShowAcceptInfo();  // Write to local console 

    chat_clients.insert(std::pair<long long, Client *>(client->GetSourceHash(), client));

    client->SendData(hello, strlen(hello)); // Write to client

    while (true)
    {
        char buff[4096];
        int read_size = client->RecivedData(buff, sizeof(buff));

        if (read_size < 0) {
            perror("socker error on receive: ");
            client->Close();
            break;
        }

        if (read_size == 0)
            break;

        buff[read_size] = '\0';

        if (strcmp(buff, "\r\n") == 0)
            continue;

        printf("%s: %s\n", client->source_ip_address, buff);

        if (strcmp(buff, "\\BYE") == 0)
        {
            client->Close();
            printf("Client gracefully exit\n");
            break;
        }

        int len = snprintf(message, 4096, "%s: %s\r\n", client->source_ip_address, buff);

        for (const auto& any : chat_clients) 
        {
            Client * peer = any.second;
            if (client->hash.hash != peer->hash.hash) // Send too all clients except source
            {
                peer->SendData(message, len);
            }
        }
    }

    std::map<long long, Client *>::iterator iter = chat_clients.find(client->GetSourceHash());
    
    if (iter != chat_clients.end())
        chat_clients.erase(iter);
    else 
        fprintf(stderr, "Something went wrong\n");

    printf("Client closed connection\n");

    int len = snprintf(message, 4096, "Client %s leave chat\n", client->source_ip_address);

    for (const auto& any : chat_clients) {
        Client * peer = any.second;
            peer->SendData(message, len);
            putc('\n', stdout);
    }

    delete client;
    return 0;
}

// ---------------------------------------------
// Prepare server to accept connections
// ---------------------------------------------
int prepare_server_socket(struct sockaddr_in * address)
{
    int server_fd;
    int opt = 1;

    // Creating socket file descriptor 
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0)
    {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }

    // Forcefully attaching socket to the port 8080 
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR /*| SO_REUSEPORT */,
        (const char*)&opt, sizeof(opt)))
    {
        perror("setsockopt");
        exit(EXIT_FAILURE);
    }

    address->sin_family = AF_INET;
    address->sin_addr.s_addr = INADDR_ANY;
    address->sin_port = htons(PORT);

    // Forcefully attaching socket to the network interfaces and port
    if (bind(server_fd, (struct sockaddr *) address, sizeof(struct sockaddr_in)) < 0)
    {
        perror("bind failed");
        exit(EXIT_FAILURE);
    }

    if (listen(server_fd, 3) < 0)
    {
        perror("listen");
        exit(EXIT_FAILURE);
    }
    return server_fd;
}

// ---------------------------------------------
// Programm entry point
// ---------------------------------------------
int main(int argc, char const *argv[])
{
    int server_fd;
    WSADATA wsaData;
    struct sockaddr_in address;
    int addrlen = sizeof(address);

    // Initialize Winsock
    int iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (iResult < 0)
    {
        printf("WSAStartup failed: %d\n", iResult);
        return 1;
    }

    server_fd = prepare_server_socket(&address);

    printf("Chat-Server ready accept connections\n");

    Client * client = nullptr;
    HANDLE hThreadsService;

    while (true)
    {
        int client_socket;

        if ((client_socket = accept(server_fd, (struct sockaddr *)&address, &addrlen)) < 0)
        {
            perror("accept");
            break;
        }

        client = new Client(client_socket);

        hThreadsService = CreateThread(NULL, 0, &ClientProcedure, (LPVOID)client, 0, NULL);
    }

    WSACleanup();

    return 0;
}
