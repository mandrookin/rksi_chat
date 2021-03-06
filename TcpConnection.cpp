#define _WINSOCK_DEPRECATED_NO_WARNINGS 1

#include <stdio.h> 
#include <winsock2.h>
#include <ws2tcpip.h> 
#include <string.h> 
#include <map>

#include "serv.h"
#include "ChatClient.h"

//int DebugTelnet(unsigned char * data, int len);

TcpConnection::TcpConnection(SOCKET socket)
{
    this->socket = socket;
}

TcpConnection::~TcpConnection()
{
}

//
// Возвращает отрицательное число при ошибке
// ноль при таймуауте
// Единицу еслм данные готовы для чтения
//
int TcpConnection::WaitDataReceive(int timeout_ms)
{
    int res;
    fd_set read_s;
    timeval time_out;

    FD_ZERO(&read_s);
    FD_SET(socket, &read_s);
    time_out.tv_sec = 0;
    time_out.tv_usec = timeout_ms * 1000;

    if (SOCKET_ERROR == (res = select(0, &read_s, NULL, NULL, &time_out))) return -1;

//    if ((res != 0) && (FD_ISSET(socket, &read_s)))
    if ((res > 0) && (FD_ISSET(socket, &read_s)))
    {
    res = 1;
    }
    else
    {
        res = 0;
    }
    return res;
}

HANDLE TcpConnection::CreateReadEvent()
{
    HANDLE h = WSACreateEvent();
    ::WSAEventSelect(socket, h, FD_READ | FD_CLOSE );
    return h;
}


int TcpConnection::GetEvent(HANDLE h)
{
    WSANETWORKEVENTS NetworkEvents;
    WSAEnumNetworkEvents( socket, h, &NetworkEvents); 
    return NetworkEvents.lNetworkEvents;
}

int TcpConnection::ReceiveData(char * input_buffer, int buffer_size)
{
#if true
    WSABUF DataBuf;
    DWORD RecvBytes, Flags;

    //u_long iMode = 0;
    //if (ioctlsocket(socket, FIONBIO, &iMode) != NO_ERROR)
    //    perror("ioctlsocket failed with error\n");

    DataBuf.len = buffer_size;
    DataBuf.buf = input_buffer;
    Flags = 0; // MSG_PARTIAL;

    int rc = WSARecv(socket, &DataBuf, 1, &RecvBytes, &Flags, NULL, NULL);
    if (rc != 0)
        RecvBytes = -1;
    return RecvBytes;
#else
    int read_size = recv(socket, input_buffer, buffer_size, 0);
    //if (read_size < 0) {
    //    perror("recv");
    //    closesocket(socket);
    //    return -1;
    //}
    return read_size;
#endif
}

int TcpConnection::ReceiveData(char * input_buffer, int buffer_size, int timeout_ms)
{
    int res = WaitDataReceive(timeout_ms);
    if (res > 0)
        res = ReceiveData(input_buffer, buffer_size);
    return res;
}

int TcpConnection::SendData(const char * data, int size)
{
    WSABUF DataBuf;
    DWORD SendBytes;
    DWORD Flags = 0;

    DataBuf.len = size;
    DataBuf.buf = (char*)data;

    int ret_val = WSASend(socket, &DataBuf, 1, &SendBytes, Flags, NULL, NULL);
    if (ret_val < 0) {
        printf("Unable send to socket. Errno %d\n", WSAGetLastError());
        return -1;
    }
    return SendBytes;
}

int TcpConnection::Close()
{
    return closesocket(socket);
}

void TcpConnection::Accept()
{
    struct sockaddr_storage addr;
    struct sockaddr_in name;
    socklen_t len = sizeof addr;
    socklen_t namelen = sizeof(name);
    char local_ip_name[80];

    getsockname(socket, (struct sockaddr*)&name, &namelen);
    inet_ntop(AF_INET, &name.sin_addr, local_ip_name, sizeof(local_ip_name));

    getpeername(socket, (struct sockaddr*)&addr, &len);

    if (addr.ss_family == AF_INET)
    {
        char  buff[80];
        struct sockaddr_in *s = (struct sockaddr_in *)&addr;
        source_port = ntohs(s->sin_port);
        inet_ntop(AF_INET, &s->sin_addr, buff, sizeof(buff));

        snprintf(source_name, sizeof(source_name), "%s:%d", buff, source_port);

        this->hash.id.ip_address = s->sin_addr.S_un.S_addr;
        this->hash.id.port = source_port;

        fprintf(stdout, "TCP: Connection from %s to %s:%d established\n",
            source_name,
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

