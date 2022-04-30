//
// Хорошее описание ANSI-команд
// https://docs.microsoft.com/en-us/windows/console/console-virtual-terminal-sequences

#define _WINSOCK_DEPRECATED_NO_WARNINGS 1
#define _CRT_SECURE_NO_WARNINGS 1

#include <windows.h>
#include <stdio.h> 
#include <string.h> 
#include <map>
#include <time.h>
#include <list>

#include "ChatClient.h"
#include "TelnetIAC.h"

extern CRITICAL_SECTION                    poks_protector;
extern std::map<long long, ChatClient *>   chat_clients;
extern std::list<std::string>              chat_messages;
extern FILE                        *       log_file;


TelnetClient::TelnetClient(TcpConnection * tcp, char * tcp_packet, int length) : TelnetIAC(tcp)
{
    chat_state = CommonChat;
    current_viewport = None;
    user_x = 1;
    user_y = 1;
    cur_x = 1;
    cur_y = 1;
    TelnetIAC::Handshake( (unsigned char*) tcp_packet, length);
}


// ---------------------------------------------
//  Установка области прокрутки терминала
// ---------------------------------------------
void TelnetClient::SetViewport(int top, int bottom)
{
    //ESC[<t>; <b> r
    unsigned char buff[80];
    int len = snprintf((char*) buff, 80, "\033[%d;%dr", top, bottom);
    SendTelnet( buff, len);
}

// ---------------------------------------------
//  Установка позиции курсора
// ---------------------------------------------
void TelnetClient::SetCursorPosition(int x, int y)
{
    unsigned char buff[80];
    int len = snprintf( (char*)buff, 80, "\033[%d;%dH", y, x);
    SendTelnet(buff, len);
    printf("Cursor position set to %d:%d\n", x, y);
}

// ---------------------------------------------
//  Выборк области ввода/вывода в терминале
// ---------------------------------------------
void TelnetClient::SwitchViewport(viewport_t window)
{
    int top, bottom, y, x;
    unsigned char buff[80];
    if (window == InputWindow)
    {
        if(current_viewport == OutputWindow)
            GetCursorPosition(&cur_x, &cur_y);
        bottom = terminal_height;
        x = user_x;
#if false
        top = 1;
        y = user_y;
#else
        top = terminal_height;
        y = bottom; // 1;
#endif
    }
    else
    {
        if (current_viewport == InputWindow)
            GetCursorPosition(&user_x, &user_y);
        top = 1;
        bottom = terminal_height - 1;
        x = cur_x;
        y = cur_y;
    }
    int len = snprintf((char*)buff, 80, "\033[%d;%dr\033[%d;%dH", top, bottom, y, x);
    SendTelnet(buff, len);
    current_viewport = window;
}

// ---------------------------------------------
//  Рассылка всем участникам чата, // -- кроме источника
// ---------------------------------------------
void TelnetClient::SendBroadcastMessage(char * message, int len)
{
    EnterCriticalSection(&poks_protector);
    for (const auto& any : chat_clients)
    {
        ChatClient * peer = any.second;
        if (strcmp(peer->TransportName(), "Telnet") == 0)
        {
            TelnetClient * chat = (TelnetClient*) peer;
            // if (tcp->hash.hash != peer->GetHash()) // Send to all clients except source
            {
                chat->SwitchViewport(OutputWindow);
                chat->SendData(message, len);
                chat->SwitchViewport(InputWindow);
            }
        }
    }
    LeaveCriticalSection(&poks_protector);
}

// ---------------------------------------------
//  ANSI chat
// ---------------------------------------------

int TelnetClient::RunChat()
{
    int res;
    switch(chat_state)
    {
    case HandshakeState:
    {
        const int stack_size = 1024;
        unsigned char * buf = (unsigned char *)_malloca(stack_size);
        res = TelnetIAC::Handshake(buf, stack_size);
        _freea(buf);
        break;
    }
    default:
        res = DefaultRunChat();
    }
    return res;
}

bool TelnetClient::CommandParser(char * command)
{
    bool done = false;
    if(strncmp(command, "help", 4) == 0)
    {
        const char * help = 
            "\\name - change user name"
            "\\bye - leave chat";

        SendData( (char*)help, strlen(help));
        done = true;
    }
    else if (strncmp(command, "name ", 5) == 0)
    {
        if (strlen(command + 5) < 3) {
            const char * msg = "Name must be at least 3 characters length";
            SendData((char*)msg, strlen(msg));
        }
        else {
            char buff[80];
            int len = snprintf(buff, 80, "\r\nUser %s renamed as %s\n", user_name, command + 5);
            strcpy(user_name, command + 5);
            SendBroadcastMessage(buff, len);
        }
        done = true;
    }
    else if (strcmp(command, "bye") == 0)
    {
        chat_state = GoodBye;
        done = true;
    }
    return done;
}

int TelnetClient::DefaultRunChat()

{
    int     len;
    char    message[4096];

    this->FindTerminalSize();

    SwitchViewport(OutputWindow);

    const char * hello = "\033cWelcome to\033[32m simple chat\033[0m! Use \help command for help";
    SendTelnet((unsigned char*)hello, strlen(hello));
   
    for (std::string & str : chat_messages) {
        SendData(str.c_str(), str.size());
    }
    this->GetCursorPosition(&cur_x, &cur_y);

    len = snprintf(message, sizeof(message), "\r\nUser \033[33m%s\033[0m joined to chat", user_name);
    SendBroadcastMessage(message, len);

    SwitchViewport(InputWindow);

    while (chat_state == CommonChat)
    {
        unsigned char * ptr = nullptr;
        char buff[4096];
        const int timeout_ms = 5000;
        int read_size = ReceiveData(buff, sizeof(buff), timeout_ms);

        if (read_size == 0) {
            printf("--- debug --- receive timeout\n");
            continue;
        }

        if (read_size == -2) {
            chat_state = GoodBye;
            continue;
        }

        if (read_size < 0) {
            perror("socker error on receive: ");
            break;
        }

        buff[read_size] = '\0';

        if (buff[0] == (char)0xff) {
            ptr = (unsigned char *) ParseProtocol((unsigned char*)buff, read_size);
            if (ptr != nullptr)
                printf("DEBUG additional parsing reuired\n");
            continue;
        }

        if (strcmp(buff, "\r\n") == 0) {
            fprintf(stderr, "ping detected\n");
            continue;
        }
        if (buff[0] == '\\' && CommandParser(buff + 1) == true)
                continue;

        len = snprintf(message, 4096, "\r\n\033[33m%s\033[0m: %s", user_name, buff);

        if (chat_messages.size() > 25)
            chat_messages.pop_front();
        chat_messages.push_back(message);

        printf("%s", message);

        time_t t = time(NULL);
        struct tm * tm = localtime(&t);
        fprintf(log_file, "%d.%02d.%02d#%02d:%02d:%02d %s %s",
            tm->tm_year, tm->tm_mon, tm->tm_mday, tm->tm_hour, tm->tm_min, tm->tm_sec,
            user_name, message);

        SendBroadcastMessage(message, len);
        SendTelnet( (unsigned char*) "\033[2K", 4);
    }
    len = snprintf(message, sizeof(message), "\r\nUser \033[33m%s\033[0m leave chat", user_name);
    SendBroadcastMessage(message, len);
    fprintf(log_file, "TcpConnection disconnected %s:\n", user_name);
    fflush(log_file);
    CloseSocket();
}

