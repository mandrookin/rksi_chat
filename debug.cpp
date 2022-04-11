#include "IEncoder.h"
#include <iostream>

using namespace std;

class ThirdEncoder : public IEncoder {
    int key_len;
    char * key;
    //public:
    virtual void Encrypt(char * ptr, int size);
    virtual void Decrypt(char * ptr, int size);
    virtual void WriteEncooderName();
public:
    ThirdEncoder(const char * key, int len);
    virtual ~ThirdEncoder();
};

void ThirdEncoder::Encrypt(char * ptr, int size)
{
    cout << "Fake ThirdEncoder::Encrypt\n";
}

void ThirdEncoder::Decrypt(char * ptr, int size)
{
    cout << "Fake ThirdEncoder::Decrypt\n";
}

void ThirdEncoder::WriteEncooderName()
{
    cout << "ThirdEncoder\n";
}

ThirdEncoder::ThirdEncoder(const char * key, int len)
{
    cout << "ThirdEncoder::ThirdEncoder\n";
}

ThirdEncoder::~ThirdEncoder()
{
    cout << "ThirdEncoder::~ThirdEncoder\n";
}

IEncoder * GetDebugEncoder(const char * key, int keylen)
{
    return new ThirdEncoder(key, keylen);
}
