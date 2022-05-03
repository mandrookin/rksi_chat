#pragma once

#include <stdio.h>
#include <windows.h>

// This is server port
#define PORT 8080 

typedef struct {
    int    ip_address;
    int    port;
} client_id_t;

typedef union {
    long long       hash;
    client_id_t     id;
} client_hash_t;

typedef struct {
    HANDLE          connection_ready;
    int             client_socket;
    int             server_socket;
//    FILE    *       log_file;
} global_context_t;

// Network client class
class TcpConnection {
    int                 socket;
    int                 source_port;

public:
    client_hash_t       hash;
    char                source_name[180];

public:
    long long GetSourceHash() { return hash.hash; }
    int WaitDataReceive(int timeout_ms);
    int ReceiveData(char * input_buffer, int buffer_size);
    int ReceiveData(char * input_buffer, int buffer_size, int timeout_ms);
    int SendData(const char * data, int size);
    int Close();
    void Accept();
    HANDLE CreateReadEvent();
    int GetEvent(HANDLE ev);

    TcpConnection(int socket);
    virtual ~TcpConnection();
};

