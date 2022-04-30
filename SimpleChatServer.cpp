#define _WINSOCK_DEPRECATED_NO_WARNINGS 1
#define _CRT_SECURE_NO_WARNINGS 1
#define SINGLE_THREAD 0

#include <stdio.h> 
#include <winsock2.h>
#include <ws2tcpip.h> 
#include <string.h> 
#include <map>
#include <list>
#include <fcntl.h>
#include <io.h>
#include <time.h>
#include <malloc.h>

#include "ChatClient.h"

// Link with ws2_32.lib
#pragma comment(lib, "Ws2_32.lib")

#define EXIT_FAILURE -2

// ---------------------------------------------
// Keep all connected clients to a hashed object
// ----------------------------------------------
std::map<long long, ChatClient *>   chat_clients;
std::list<std::string>              chat_messages;
CRITICAL_SECTION                    poks_protector;
FILE                        *       log_file;

// -------------------------------------------------------
// Функция "фабрика объектов"
// анализиррует тип пакета и на создаёт подходящий объект
// ------------------------------------------------------
ChatClient * HandleConnection(TcpConnection * client)
{
    ChatClient * chat_client = nullptr;
    int length;
    const unsigned  int   buffer_size = 2048;
    const int TIMEOUT = 500;

    char  * tcp_data = (char*) _malloca(buffer_size);
    length = client->ReceiveData(tcp_data, buffer_size, TIMEOUT);
    if (length <= 0) {
        printf("Timeout %d ms. Will ignore raw socket or browser parallel connections\n", TIMEOUT);
    }
    else if (tcp_data[0] == (char) 0xff) {
        chat_client = new TelnetClient(client, tcp_data, length);
    }
    else {
        url_t       url;
        tcp_data[length] = 0;
        char    *   next = parse_http_request(tcp_data, &url);
        if (next != nullptr)
            chat_client = new BrowserClient(client, url, next);
    }
    _freea(tcp_data);
    return chat_client;
}

// ---------------------------------------------
//  Добавление клиента в хешированный список
// ---------------------------------------------
void AddClientToList(ChatClient * chat)
{
    EnterCriticalSection(&poks_protector);
    chat_clients.insert(std::pair<long long, ChatClient *>(chat->GetHash(), chat));
    LeaveCriticalSection(&poks_protector);

}

// ---------------------------------------------
//  Удаление клиента из хешированного списка
// ---------------------------------------------
void RemoveClientFromList(ChatClient * chat)
{
    EnterCriticalSection(&poks_protector);
    std::map<long long, ChatClient *>::iterator iter = chat_clients.find(chat->GetHash());
    if (iter != chat_clients.end())
        chat_clients.erase(iter);
    else
        fprintf(stderr, "Something went wrong\n");
    LeaveCriticalSection(&poks_protector);
}

// ---------------------------------------------
//  Основной поток обслуживающий сетевой трафик
//  Создаётся на каждое входящее соединение
// ---------------------------------------------
DWORD WINAPI ClientProcedure(CONST LPVOID lpParam)
{
    global_context_t    *   context = (global_context_t*)lpParam;
    TcpConnection       *   tcp;
    ChatClient          *   chat_client;
    char                    message[4096];

    tcp = new TcpConnection(context->client_socket);
    tcp->Accept();
    chat_client = HandleConnection(tcp);

    if (chat_client == nullptr) {
        tcp->Close();
        delete tcp;
        SetEvent(context->connection_ready);
        return 0;
    }

    AddClientToList(chat_client);

    if (!SetEvent(context->connection_ready)) {
        printf("SetEvent failed (%d)\n", GetLastError());
        return -1;
    }

    fprintf(log_file, "%s connected %s:\n", tcp->source_name, chat_client->TransportName());

    chat_client->RunChat();


    int len = snprintf(message, 4096, "TcpConnection %s connection closed\r\n", tcp->source_name);

    EnterCriticalSection(&poks_protector);
    for (const auto& any : chat_clients) {
        ChatClient * peer = any.second;
        peer->SendData(message, len);
        putc('\n', stdout);
        fflush(stdout);
    }
    LeaveCriticalSection(&poks_protector);

    RemoveClientFromList(chat_client);

    delete chat_client;
    delete tcp;
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
    if ((server_fd = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0, WSA_FLAG_OVERLAPPED)) == 0)
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

char * get_current_date()
{
    time_t curr_time;
    tm * curr_tm;
    static char date_time_string[256];

    time(&curr_time);
    curr_tm = localtime(&curr_time);

    strftime(date_time_string, 256, "Date: %d %B %Y  Time: %T", curr_tm);

    return date_time_string;
}

// ---------------------------------------------
// Programm entry point
// ---------------------------------------------
int main(int argc, char const *argv[])
{
    global_context_t    poks_context;
    WSADATA wsaData;
    HANDLE hThreadsService;
    struct sockaddr_in address;
    int addrlen = sizeof(address);

#if LOGS
    poks_context.log_file = fopen("poks_chat.log", "a+");
#else
    for (int i = 0; i < 8; i++) {
        char bf[80];
        snprintf(bf, 80, "\r\nLine %d", i);
        chat_messages.push_back(bf);
    }
    log_file = fopen("NUL", "a+");
#endif

    poks_context.connection_ready = CreateEvent(
        NULL,               // default security attributes
        TRUE,               // manual-reset event
        FALSE,              // initial state is nonsignaled
        TEXT("Processing incoming connection")  // object name
    );
    if (poks_context.connection_ready == NULL)
    {
        printf("CreateEvent failed (%d)\n", GetLastError());
        return -1;
    }
    // Initialize Winsock
    int iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (iResult < 0)
    {
        printf("WSAStartup failed: %d\n", iResult);
        return 1;
    }

    poks_context.server_socket = prepare_server_socket(&address);

    printf("Chat-Server ready to accept connections\n");
    fprintf(log_file, "Server started on %s\n", get_current_date());
    fflush(log_file);

    InitializeCriticalSection(&poks_protector);
    while (true) {

        poks_context.client_socket = WSAAccept(poks_context.server_socket, (sockaddr *)&address, &addrlen, NULL, NULL);
        //poks_context.client_socket = accept(poks_context.server_socket, (struct sockaddr *)&address, &addrlen)
        if (poks_context.client_socket < 0)
        {
            perror("accept");
            break;
        }
#if ! SINGLE_THREAD
        hThreadsService = CreateThread(NULL, 0, &ClientProcedure, (LPVOID)&poks_context, 0, NULL);
        WaitForSingleObject(poks_context.connection_ready, 100000);

        if (!ResetEvent(poks_context.connection_ready)) {
            printf("    ResetEvent failed (%d)\n", GetLastError());
            return -1;
        }

#else
        ClientProcedure((LPVOID)&poks_context);
#endif
    }
    CloseHandle(poks_context.connection_ready);
    DeleteCriticalSection(&poks_protector);
    WSACleanup();
    fclose(log_file);
    return 0;
}
