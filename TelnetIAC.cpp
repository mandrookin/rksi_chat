#include "TelnetIAC.h"
#include "ChatClient.h"

#include <wchar.h>

void TelnetIAC::SendTelnet(unsigned char * msg, int len)
{
    if (msg[0] == IAC) {
        printf("==> "); DebugIAC(msg, len);
    }
    else if (msg[0] == 0x1b)
        fprintf(stdout, "%s ==> \\0x1b%s\n", tcp->source_name, msg + 1);
    else
        fprintf(stdout, "%s ==> %s\n", tcp->source_name, msg);
    tcp->SendData((char*)msg, len);
}

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

int TelnetIAC::ReceiveData(char * input_buffer, int buffer_size, int timeout_ms)
{
    int len = 0;
    int res = tcp->WaitDataReceive(timeout_ms);
    if (res > 0)
        len = tcp->ReceiveData(input_buffer, buffer_size);
    if(res < 0 || len < 0)
        ErrorMessage( WSAGetLastError() );
    
    return  len ? len : res > 0 ? -2 : res;
}

// ---------------------------------------------
//  Разбор управляющих команд Telnet
// ---------------------------------------------
int TelnetIAC::DebugIAC(unsigned char * data, int len)
{
    int counter = 0;

    printf("%s ", tcp->source_name);

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
    terminal_width = 80, terminal_height = 25;
    const char request_window_size[3] = { IAC, 0xfd, 0x1f };
    SendTelnet((unsigned char*)request_window_size, 3);

    int len = tcp->ReceiveData((char*)buffer, sizeof(buffer), 500);
    if (len <= 0)
        return;

    ParseProtocol(buffer, len);
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
    if (len <= 0)
        return;

    ptr = ParseProtocol(buffer, len);

    SendTelnet((unsigned char*) "\033[6n", 4);

    len = ReceiveData((char*)buffer, sizeof(buffer), 500);
    if (len <= 0)
        return;

    ptr = buffer;

    if (buffer[0] == 0xff) {
        while (buffer[0] == 0xff) {
            ptr = ParseProtocol(buffer, len);
            if (ptr) {
                break;
            }
            len = ReceiveData((char*)buffer, sizeof(buffer), 500);
            if (len <= 0)
                return;
            ptr = buffer;
        }
    }

    if(ptr)
        ParseCursorPosition(ptr, len, x, y);

    const char request_go_canoniical[] = { IAC,WONT,3, IAC,WONT,1, IAC,WONT,34 };;
    SendTelnet((unsigned char*)request_go_canoniical, 6);

    puts("---------------------- restore state\n");
}


int TelnetIAC::Handshake(unsigned char * data, int len)
{
    unsigned char input_buffer[2048];

    DebugIAC(data, len);
#if false
    const unsigned char handshake_response[] = {
    IAC, DONT, 0x1f,
    IAC, DONT, 0x20,
    IAC, DONT, 0x18,
    IAC, DONT, 0x27,
    IAC, WONT, 0x01,
    IAC, DONT, 0x03,
    IAC, WONT, 0x03
    };
    tcp->SendData((char*)handshake_response, 21);
    printf("==> "); DebugIAC((unsigned char*)handshake_response, 21);
#else
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
    tcp->SendData((char*)data, len);
    printf("==> "); DebugIAC(data, len);
#endif

#if false
    len = tcp->ReceiveData((char*)input_buffer, sizeof(input_buffer), 500);
    if (len > 0) {
        DebugTelnet(input_buffer, len);

        const char second_response[] = { 0xff, DONT, 36 };
        tcp->SendData(second_response, 3);
        printf("==> ");     DebugTelnet((unsigned char*)second_response, 3);

        len = tcp->ReceiveData((char*)input_buffer, sizeof(input_buffer), 500);
        if (len > 0) {
            DebugTelnet(input_buffer, len);
        }
    }
#endif

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


unsigned char * TelnetIAC::ParseProtocol(unsigned char * msg, int len)
{
    enum { SYNC, CMD } state = SYNC;
    unsigned char * ptr, *finish;
    unsigned char cmd = 0;
    DebugIAC(msg, len);

    for (ptr = msg, finish = msg + len; ptr != nullptr && ptr < finish; ptr++)
    {
        if (state == SYNC) {
            if (*ptr == IAC)
                state = CMD;
            else if (*ptr == 0x1b) {
                printf("DEBUG: found escape\n");
                break;
            } 
            else if (len == 2 && ptr[0] == 0xd && ptr[1] == 0xa) {
                printf("DEBUG: ping?\n");
                break;
            }
            else {
                fprintf(stderr, "TELNET error: no IAC prefix\n");
                ptr = finish;
            }
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

