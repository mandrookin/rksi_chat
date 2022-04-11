#define _CRT_SECURE_NO_WARNINGS

#include <iostream>
#include <string.h>

#include "IEncoder.h"
#include "debug.h"

class BaseEnc : public IEncoder {
//public:
protected:
    virtual void Encrypt(char * ptr, int size);
    virtual void Decrypt(char * ptr, int size);
    virtual void WriteEncooderName();
    virtual ~BaseEnc() {};
};

class Gronsfeld : public BaseEnc {
    int key_len;
    char * key;
//public:
    virtual void Encrypt(char * ptr, int size);
    virtual void Decrypt(char * ptr, int size);
    virtual void WriteEncooderName();
public:
    Gronsfeld(const char * key, int len);
    virtual ~Gronsfeld();
};

void BaseEnc::WriteEncooderName()
{
    std::cout << "Base Encoder\n";
}

void Gronsfeld::WriteEncooderName()
{
    std::cout << "Gronsfeld Encoder\n";
}

void Gronsfeld::Encrypt(char * str, int size)
{
    int j = 0;
    for (int i = 0; i < size; i++) {
        if (j < key_len)
            str[i] += key[j++];
        else
            j = 0;
    }
    BaseEnc::Encrypt(str, size);
}

void Gronsfeld::Decrypt(char * str, int size)
{
    int j = 0;

    BaseEnc::Decrypt(str, size);

    for (int i = 0; i < size; i++) {
        if (j < key_len)
            str[i] -= key[j++];
        else
            j = 0;
    }
}

Gronsfeld::~Gronsfeld()
{
    delete key;
}


Gronsfeld::Gronsfeld(const char * key, int len)
{
    this->key_len = len;
    this->key = new char[len];
    memcpy(this->key, key, len);
}

void BaseEnc::Encrypt(char * ptr, int size)
{
    for (int i = 0; i < size; i++)
        ptr[i] ^= 0x55;
}

void BaseEnc::Decrypt(char * ptr, int size)
{
    for (int i = 0; i < size; i++)
        ptr[i] ^= 0x55;
}

IEncoder * GetEncoder(int encooder_type)
{
    IEncoder * rv;
    switch (encooder_type)
    {
    case 0: 
        rv = new BaseEnc();
        break;
    case 1:
        rv = new Gronsfeld("mykey", 5);
        break;
    case 2:
        rv = GetDebugEncoder("fakekey", 7);
        break;
    default:
        throw "Unsupported encoder type";
    }
    return rv;
}

int old_main()
{
    IEncoder  * shifr;
    int size;
    const char * text;
    char * buff;

    try 
    {
        shifr = GetEncoder(1);

        text = "My string";
        size = strlen(text) + 1;
        buff = new char[size];

        strcpy(buff, text);
        shifr->WriteEncooderName();
        shifr->Encrypt(buff, size);
        shifr->Decrypt(buff, size);

        std::cout << buff << std::endl;

        delete buff;
        delete shifr;
    }
    catch (const char * text)
    {
        std::cout << "Exception : " << text << std::endl;
    }

    return 0;
}
