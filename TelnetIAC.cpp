// Различная информация о протоколе Telnet
// https://www.ibm.com/docs/en/zos/2.3.0?topic=problems-telnet-commands-options
// https://datatracker.ietf.org/doc/html/rfc732
// https://stackoverflow.com/questions/30643457/telnet-linemode-suboption-trapsig-does-not-work
// https://stackoverflow.com/questions/10413963/telnet-iac-command-answering
// http://www.iana.org/assignments/telnet-options/telnet-options.xhtml
// https://www.omnisecu.com/tcpip/telnet-negotiation.php

#include "TelnetIAC.h"
#include "ChatClient.h"

#include <wchar.h>

// ---------------------------------------------
//  Передача данных удалённой клиенту
// ---------------------------------------------
void TelnetIAC::SendTelnet(unsigned char * msg, int len)
{
    SendData((char*)msg, len);

#if false // Отладка отправляемых даннных в консоль чат-сервера
    if (msg[0] == IAC) {
        printf("==> "); DebugIAC(msg, len);
    }
    else {
        unsigned char * bf = (unsigned char *) _malloca(len+1);
        for (int i = 0; i < len; i++) {
            bf[i] = msg[i] == 0x1b ? '\\' : msg[i];
        }
        bf[len] = 0;
        fprintf(stdout, "%s ==> %s\n", user_name, bf);
        _freea(bf);
    }
#endif
}

// ---------------------------------------------
//  Выводит осмысленное сообщени об ошибке
// ---------------------------------------------
void ErrorMessage(int err)
{
    wchar_t        msgbuf[2048];   // for a message up to 255 bytes.

    msgbuf[0] = '\0';    // Microsoft doesn't guarantee this on man page.

    FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,   // flags
        NULL,                // lpsource
        err,                 // message id
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),    // languageid
        (LPWSTR) msgbuf,              // output buffer
        sizeof(msgbuf),     // size of msgbuf, bytes
        NULL);               // va_list of arguments

    if (!*msgbuf)
        swprintf(msgbuf, sizeof(msgbuf), L"%d", err);  // provide error # if no string available

    fwprintf(stderr, msgbuf);
}

int TelnetIAC::ReceiveData(char * input_buffer, int buffer_size)
{
    int len = tcp->ReceiveData(input_buffer, buffer_size);
    if (len < 0)
        ErrorMessage(WSAGetLastError());

    if (len > 0) {
        input_buffer[len] = 0;
        {
            unsigned char first = (unsigned char)input_buffer[0];
            if ((first != 0xff && first != 0x1b) && input_buffer[len - 1] != 0xa)
                printf("Catch it!");
        }
        len = ParseInputStream((unsigned char*)input_buffer, len);
    }

    return  len;
}

// ---------------------------------------------
//  Чтение данных с таймаутом
// ---------------------------------------------
int TelnetIAC::ReceiveData(char * input_buffer, int buffer_size, int timeout_ms)
{
    int len = 0;
#if true
    int res = tcp->WaitDataReceive(timeout_ms);
    if (res > 0)
        len = tcp->ReceiveData(input_buffer, buffer_size);
    if(res < 0 || len < 0)
        ErrorMessage( WSAGetLastError() );
    // Это выражение кажется сложным, но гораздо сложнее было подобрать его
    // Суть в том, что мне понадобилось отличать таймаут приёма от ошибки сокета
    // В случае таймата возвращается значение 0, в случае закрытия сокета удалённой сторойной возвращается -2
    // Положительнное число - размер принятых данных, любоей другое число - какая-то другая ошибка
    // Вероятно при портировании на Linux эту часть кода приёдся переписать
    len = len ? len : res > 0 ? -2 : res;
#else
    DWORD rdy = WaitForSingleObject(this->chat_events[1], timeout_ms);
    if (rdy != WAIT_OBJECT_0) {
        printf("Got timeout on receive data\n");
        return 0;
    }
    len = tcp->ReceiveData(input_buffer, buffer_size);
#endif

    if (len > 0) {
        input_buffer[len] = 0;
        {
            unsigned char first = (unsigned char)input_buffer[0];
            if ((first != 0xff && first != 0x1b) && input_buffer[len - 1] != 0xa)
                printf("Catch it!");
        }
        len = ParseInputStream((unsigned char*)input_buffer, len);
    }

    ResetEvent(this->chat_events[1]);
    return  len;
}

// ---------------------------------------------
//  Разбор управляющих команд Telnet
// ---------------------------------------------
int TelnetIAC::DebugIAC(unsigned char * data, int len)
{
    int counter = 0;

    printf("%s ", user_name);

    for (int i = 0; i < len; i++) {
        if (counter > 2)
        {
            if (data[i] == 0xf0) {
                printf("} ");
                counter = 0;
            }
            else if (data[i] == 0xff)
                printf("IAC ");
            else
                printf("%02d ", data[i]);
        }
        else switch (data[i]) {
        case 0x01: printf("ECHO "); break;
        case 0x03: printf("SGA "); break;
        case 0x18: printf("-TT- "); break;
        case 0x1d: printf("-TR- "); break;
        case 0x1f: printf("NAWS "); break;
        case 0x20: printf("-TS- "); break;
        case 0x22: printf("LINEMODE "); break;
        case 0x24: printf("EO "); break;
        case 0x27: printf("NEO "); break;
        case 0xf0: printf("} "); counter = 0; break;
        case 0xfa: printf("{ "); counter = 1; break;
        case 0xfb: printf("WILL "); break;
        case 0xfc: printf("WONT "); break;
        case 0xfd: printf("DO "); break;
        case 0xfe: printf("DONT "); break;
        case 0xff: printf("IAC "); break;
        default: printf("%02d ", data[i]);
        }
        if (counter)
            counter++;
    }
    puts("");
    return 0;
}

// ------------------------------------------------
//  Запрос размера окна терминала через Telnet API
// ------------------------------------------------
void TelnetIAC::FindTerminalSize()
{
    // https://www.ietf.org/rfc/rfc1073.txt

    unsigned char buffer[80];
    // Значения по умолчанию, если что-то пойдёт не так
    terminal_width = 80, terminal_height = 24;
    const char request_window_size[3] = { IAC, 0xfd, 0x1f };
    SendTelnet((unsigned char*)request_window_size, 3);
    this->bits_sent.naws = true;

    int len = ReceiveData((char*)buffer, sizeof(buffer), 1500);
    if (len <= 0)
        return;

    //ParseProtocol(buffer, len);
}

// ---------------------------------------------
//  Разбор ANSI позиции курсора
// ---------------------------------------------
void TelnetIAC::ParseCursorPosition(unsigned char * ptr, int len, int * x, int * y)
{
    if (ptr[0] == 0x1b && ptr[1] == '[') {
        ptr[len] = 0;
        char * delimiter = strchr((char *)(ptr + 2), ';');
        if (delimiter) {
            *delimiter = 0;
            delimiter++;
            char * final = strchr(delimiter, 'R');
            if (final) {
                *final++ = 0;
                *y = atoi((char*)(ptr + 2));
                *x = atoi(delimiter);
                printf("Found cursor position %d,%d\n", *x, *y);
                if (final < (char*)(ptr + len) || !*x || !*y) {
                    printf(" !!!! Need DDTIONAL PARSING\n");
                }
            }
            else {
            }
        }
    }
}

// ---------------------------------------------
//  Чтение позиции курсора
// ---------------------------------------------
void TelnetIAC::GetCursorPosition(int * x, int * y)
{
    int len;
    unsigned char * ptr;
    unsigned char buffer[80];

    const unsigned char request_go_ahead[] = { IAC, WILL,3, IAC,WILL,1, IAC,WONT,34 };
    bits_sent.sga = true;
    bits_sent.echo = true;
    SendTelnet((unsigned char*)request_go_ahead, 6);

    len = ReceiveData((char*)buffer, sizeof(buffer), 500);
    if (len < 0)
        return;

    SendTelnet((unsigned char*) "\033[6n", 4);

    len = ReceiveData((char*)buffer, sizeof(buffer), 500);
    if (len < 0)
        return;

    for (int i = 0; i < 4 && len == 0; i++) {
        fprintf(stdout, "TODO: syncronization error\n");
        len = ReceiveData((char*)buffer, sizeof(buffer), 500);
    }

    ParseCursorPosition(buffer, len, x, y);

    const char request_go_canoniical[] = { IAC,WONT,3, IAC,WONT,1, IAC,WONT,34 };;
    SendTelnet((unsigned char*)request_go_canoniical, 6);

    puts("---------------------- restore state after Get cursor postion\n");
}

// ----------------------------------------------------------------------
// Процедура рукопожатия Telnet
// Текущая версия отклоняет все опции, но изменяет их в процессе работы
// ----------------------------------------------------------------------
int TelnetIAC::Handshake(unsigned char * data, int len)
{
    unsigned char input_buffer[2048];

    DebugIAC(data, len);

    unsigned char *p = data, *finish = data + len;
    while (p < finish) {
        if (*p++ != IAC) {
            fprintf(stderr, "No IAC found at handshake\n");
            return 0;
        }
        if (*p == WILL) {
            *p = DONT;
            p += 2;
            continue;
        }
        if (*p == DO) {
            *p = WONT;
            p += 2;
            continue;
        }
    }
    SendData((char*)data, len);
    printf("==> "); DebugIAC(data, len);

    return 0;
}

unsigned char * TelnetIAC::parse_WILL(unsigned char * ptr)
{
    switch (*ptr) {
    case EO:
        SendTelnet((unsigned char*) "\377\376\044", 3);
        break;
    case NAWS:
        //        SendTelnet((unsigned char*) "\377\376\x1f", 3);
        break;
    default:
        fprintf(stderr, "WILL unknown option: 0x%02x\n", *ptr);
        break;
    }
    return ptr;
}

unsigned char * parse_WONT(unsigned char * ptr)
{
    return ptr;
}

unsigned char * TelnetIAC::parse_DO(unsigned char * ptr)
{
    switch (*ptr) {
    case ECHO:
        if (this->bits_sent.echo)
            this->bits_sent.echo = false;
        break;
    case SGA:
        if (this->bits_sent.sga)
            this->bits_sent.sga = false;
        break;
    default:
        fprintf(stderr, "DO unknown option: 0x%02x\n", *ptr);
        break;
    }
    return ptr;
    return ptr;
}

unsigned char * parse_DONT(unsigned char * ptr)
{
    return ptr;
}

unsigned char * TelnetIAC::parse_SB(unsigned char * ptr, unsigned char * finish)
{
    switch (*ptr)
    {
    case NAWS:
        terminal_width = (unsigned int)ptr[2];
        terminal_height = (unsigned int)ptr[4];
        printf("Found screen size %dx%d\n", terminal_width, terminal_height);
        ptr += 6; // TODO: check final IAC + SE and ptr < finish
        break;
    default:
        fprintf(stderr, "TELNET unknown block type 0x%02x\n", *ptr);
        ptr = nullptr;
    }
    return ptr;
}

int TelnetIAC::ParseInputStream(unsigned char * msg, int len)
{
    unsigned char * ptr = msg, * dest = msg, * finish = msg + len;
    len = 0;
    while (ptr != nullptr && ptr < finish) {
        if (*ptr == IAC) {
            ptr = ParseProtocol(ptr, finish - ptr);
            continue;
        } 
        *dest++ = *ptr++;
        len++;
    }
    return len;
}

unsigned char * TelnetIAC::ParseProtocol(unsigned char * msg, int len)
{
    enum { SYNC, CMD } state = SYNC;
    unsigned char * ptr, *finish;
    unsigned char cmd = 0;
    DebugIAC(msg, len);

    for (ptr = msg, finish = msg + len; ptr != nullptr && ptr < finish; ptr++)
    {
        if (state == SYNC) {
            if(*ptr != IAC) 
                return ptr;
            state = CMD;
            continue;
        }
        else
        {
            switch (*ptr) {
            case WILL:
                ptr = parse_WILL(++ptr);
                break;

            case WONT:
                ptr = parse_WONT(++ptr);
                break;
            case DO:
                ptr = parse_DO(++ptr);
                break;
            case DONT:
                ptr = parse_DONT(++ptr);
                break;
            case SB:
                ptr = parse_SB(++ptr, finish);
                break;
            default:
                fprintf(stderr, "TELNET command error: 0x%03x\n", *ptr);
                ptr = finish;
            }
            state = SYNC;
        }
    }

    if (ptr == finish)
        ptr = nullptr;

    return ptr;
}

