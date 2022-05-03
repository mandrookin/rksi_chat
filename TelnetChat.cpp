//
// Хорошее описание ANSI-команд
// https://docs.microsoft.com/en-us/windows/console/console-virtual-terminal-sequences
// https://vt100.net/docs/vt510-rm/chapter4.html

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

// ---------------------------------------------
//  Эти переменные объявдены в другом месте
// ---------------------------------------------
extern CRITICAL_SECTION                 poks_protector;
extern clients_strorage_t               chat_clients;
extern std::list<std::string>           chat_messages;
extern FILE                        *    log_file;

// ---------------------------------------------
//  Конструктор класс TelnetChat
// ---------------------------------------------
TelnetChat::TelnetChat(TcpConnection * tcp, char * tcp_packet, int length) : TelnetIAC(tcp)
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
void TelnetChat::SetViewport(int top, int bottom)
{
    //ESC[<t>; <b> r
    unsigned char buff[80];
    int len = snprintf((char*) buff, 80, "\033[%d;%dr", top, bottom);
    SendTelnet( buff, len);
}

// ---------------------------------------------
//  Установка позиции курсора
// ---------------------------------------------
void TelnetChat::SetCursorPosition(int x, int y)
{
    unsigned char buff[80];
    int len = snprintf( (char*)buff, 80, "\033[%d;%dH", y, x);
    SendTelnet(buff, len);
    printf("Cursor position set to %d:%d\n", x, y);
}

// ---------------------------------------------
//  Выборк области ввода/вывода в терминале
// ---------------------------------------------
void TelnetChat::SwitchViewport(viewport_t window)
{
    int top, bottom, y, x;
    unsigned char buff[80];
    if (window == InputWindow)
    {
        if(current_viewport == OutputWindow)
            GetCursorPosition(&cur_x, &cur_y);
        bottom = terminal_height;
        x = user_x;
        top = terminal_height;
        y = bottom; // 1;
    }
    else
    {
        if (current_viewport == InputWindow)
            GetCursorPosition(&user_x, &user_y);
        top = 2;
        bottom = terminal_height - 1;
        x = cur_x;
        y = cur_y;
    }
    int len = snprintf((char*)buff, 80, "\033[%d;%dr\033[%d;%dH", top, bottom, y, x);
    SendTelnet(buff, len);
    current_viewport = window;
}

void TelnetChat::SendChatMessage(std::string msg)
{
    SwitchViewport(OutputWindow);
    SendData((const char*) msg.c_str(), (int) msg.size());
    SwitchViewport(InputWindow);
}

void TelnetChat::AppendMessage(char * message)
{
    send_queue.push_back(message);
    SetEvent(this->chat_events[0]);
}

// -------------------------------------------------
//  Рассылка всем участникам чата, в том числе себе
// -------------------------------------------------
void TelnetChat::SendBroadcastMessage(char * message, int len)
{
    EnterCriticalSection(&poks_protector);
    for (const auto& any : chat_clients)
    {
        ChatClient * peer = any.second;
        if (strcmp(peer->TransportName(), "Telnet") == 0)
        {
            ((TelnetChat*)peer)->AppendMessage(message);
        }
    }

    if (chat_messages.size() > 25)
        chat_messages.pop_front();
    chat_messages.push_back(message);

    LeaveCriticalSection(&poks_protector);
}

void TelnetChat::FlushQueue()
{
    std::string data;
    EnterCriticalSection(&poks_protector);
    for (const auto& msg : this->send_queue)
    {
        data += msg;
    }
    send_queue.clear();
    LeaveCriticalSection(&poks_protector);
    if (data.size() > 0)
        SendChatMessage(data);
}

// ---------------------------------------------
//  Распознавани и обратка команд чата
// ---------------------------------------------
bool TelnetChat::CommandParser(char * command)
{
    bool done = false;
    if(strncmp(command, "help", 4) == 0)
    {
        const char * help =
            "\r\n\\name - change user name"
            "\r\n\\bye - leave chat"
            "\r\n";

        SendChatMessage(help);
        done = true;
    }
    else if (strncmp(command, "name ", 5) == 0)
    {
        if (strlen(command + 5) < 3) {
            const char * msg = "\r\nName must be at least 3 characters length";
            SendChatMessage(msg);
        }
        else {
            char buff[80];
            int len = snprintf(buff, 80, "\r\nUser \x1b[32m%s\x1b[0m renamed as \x1b[32m%s\x1b[0m", user_name, command + 5);
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
    ShowPrompt();
    return done;
}

// ------------------------------------------------
//  Запись лога чата в файл
// ------------------------------------------------
void TelnetChat::WriteLogMessage(char * message)
{
    time_t t = time(NULL);
    struct tm * tm = localtime(&t);
    fprintf(log_file, "%d.%02d.%02d#%02d:%02d:%02d %s %s",
        tm->tm_year, tm->tm_mon, tm->tm_mday, tm->tm_hour, tm->tm_min, tm->tm_sec,
        user_name, message);
}

// ------------------------------------------------
//  Показываем имя пользователя в строке ввода
// ------------------------------------------------
void TelnetChat::ShowPrompt()
{
    char buff[80];
    int len = snprintf(buff, sizeof(buff), "\033[2K\033[32m%s\033[0m> ", user_name);
    SendTelnet((unsigned char*) buff, len);
}

// ------------------------------------------------
//  Основной цикл ANSI чата. Вся обработка здесь
// ------------------------------------------------
int TelnetChat::RunChat()
{
    const char * hello = "\033cWelcome to\033[32m simple chat\033[0m! Use \\help command for help";
    int     len;
    int     wait_crlf = 0;
    char    message[4096];

    chat_events[0] = CreateEvent(NULL, false, false, L"Send data signal");
    chat_events[1] = tcp->CreateReadEvent();

    FindTerminalSize();
    SwitchViewport(OutputWindow);
    SendTelnet((unsigned char*)hello, (int) strlen(hello));

    if (chat_messages.size() > 0) {
        std::string     all_messages;
        for (std::string & str : chat_messages)
            all_messages += str;
        SendData(all_messages.c_str(), (int) all_messages.size());
    }

    len = snprintf(message, sizeof(message), "\r\n\033[33m%s\033[0m has joined the chat", user_name);
    GetCursorPosition(&cur_x, &cur_y);
    SendBroadcastMessage(message, len);
    SwitchViewport(InputWindow);
    ShowPrompt();

    while (chat_state == CommonChat)
    {
        unsigned char * ptr = nullptr;
        int socket_event;
        char buff[4096];
        const int timeout_ms = 60000;

        DWORD hit = WaitForMultipleObjects(2, chat_events, false, timeout_ms);

        switch (hit)
        {
            // Готовы данные для передачи Telnet клиенту
        case WAIT_OBJECT_0 + 0:
            FlushQueue();
            continue;

            // Телнет клиент прислал какие-то данные
        case WAIT_OBJECT_0 + 1:
            socket_event = tcp->GetEvent(chat_events[1]);
            if (socket_event & FD_CLOSE) {
                // Телнет клиент закрыл соединение
                chat_state = GoodBye;
                continue;
            }
            // Пришли данные от Телнет-клиента
            break;

        case WAIT_TIMEOUT:
            // Это событие можно использовать для чего-то полезного
            printf("Reserved timer hit.\n");
            continue;

            // Return value is invalid.
        default:
            printf("Wait error: %d\n", GetLastError());
            chat_state = GoodBye;
            continue;
        }

        int read_size = ReceiveData(buff + wait_crlf, sizeof(buff) - wait_crlf);

        if (read_size == 0) {
            // Сюда прилетает когда получены внутренние данные протокола Telnet 
            continue;
        }

        if (read_size < 0) {
            chat_state = GoodBye;
            if (read_size != -2)
                perror("socker error on receive: ");
            continue;
        }

        int total = wait_crlf + read_size;
        buff[total] = '\0';
        if ( buff[total-1] != 0xa || buff[total-2] != 0xd) {
            wait_crlf = total;
            fprintf(stdout, "partial message recied. Hold in buffer: %s\n", buff);
            continue;
        }
        buff[total - 2] = 0;
        len += wait_crlf;
        wait_crlf = 0;

        if (buff[0] == '\\' && CommandParser(buff + 1) == true)
                continue;

        len = snprintf(message, 4096, "\r\n\033[33m%s\033[0m: %s", user_name, buff);

        WriteLogMessage(message);
        SendBroadcastMessage(message, len);
        ShowPrompt();
    }
    len = snprintf(message, sizeof(message), "\r\n\033[33m%s\033[0m has left the chat", user_name);
    SendBroadcastMessage(message, len);
    fprintf(log_file, "TcpConnection disconnected %s:\n", user_name);
    fflush(log_file);

    CloseHandle(chat_events[0]);
    CloseHandle(chat_events[1]);
    CloseSocket();
    return 0;
}

