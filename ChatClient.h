#include "TcpConnection.h"
#include "serv.h"

class ChatClient {
protected:
    TcpConnection   *   tcp;

public:
    ChatClient(class TcpConnection * client)
    {
        tcp = client;
    }

    int SendData(char * message, int length)
    {
        return tcp->SendData(message, length);
    }

    long long GetHash() { return tcp->hash.hash; }
    virtual int RunChat() = 0;
    virtual const char * TransportName() = 0;
};

class TelnetIAC : public ChatClient
{
    typedef union {
        unsigned int    all_bits_value;
        struct {
            int         echo : 1;
            int         sga : 1;
            int         naws;
        };
    } iac_sent_t;

    bool                line_mode;          // Режим терминала
    iac_sent_t          bits_sent;
protected:
    int                 terminal_width;     // Ширина терминала в символах
    int                 terminal_height;    // Количество строк терминала
    int                 cur_x;              // Сохранение горизонтальной позиции курсора
    int                 cur_y;              // Сохранение вериткальной позиции курсора
private:
    void ParseCursorPosition(unsigned char * ptr, int len, int * x, int * y);
protected:
    int ReceiveData(char * input_buffer, int buffer_size, int timeout_ms);
    int DebugIAC(unsigned char * data, int len);
    unsigned char * ParseProtocol(unsigned char * msg, int len);
    unsigned char * parse_WILL(unsigned char * ptr);
    unsigned char * parse_DO(unsigned char * ptr);
    unsigned char * parse_SB(unsigned char * ptr, unsigned char * finish);
    int Handshake(unsigned char * data, int len);
    void GetCursorPosition(int * x, int * y);
    void FindTerminalSize();
    void SendTelnet(unsigned char * msg, int len);

public:
    TelnetIAC(TcpConnection * tcp) : ChatClient(tcp) 
    {
        line_mode = true;
        bits_sent.all_bits_value = 0;
    }
};

class TelnetClient : public TelnetIAC
{
    typedef enum {
        HandshakeState,
        CommonChat,
        GoodBye
    } connection_state_t;
    
    typedef enum { 
        None,
        OutputWindow, 
        InputWindow 
    } viewport_t;

    viewport_t          current_viewport;
    connection_state_t  chat_state;
    int                 user_x;              // Сохранение пользовательской позиции курсора
    int                 user_y;              // Сохранение пользовательской позиции курсора

private:
    void SetCursorPosition(int x, int y);
    void SetViewport(int top, int bottom);
    void SwitchViewport(viewport_t window);
    bool CommandParser(char * command);
private:
    int RunChat();
    int DefaultRunChat();
    const char * TransportName() { return "Telnet"; };
public:
    TelnetClient(class TcpConnection *, char * data, int len);
    void SendBroadcastMessage(char * message, int len);
};

class BrowserClient : public ChatClient
{
    url_t          url;
private:
    int RunChat();
    const char * TransportName() { return "Web Client"; };
public:
    BrowserClient(class TcpConnection *, url_t url, char * header);
    void DebugURL();
};
