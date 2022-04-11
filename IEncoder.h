#pragma once

class IEncoder {
public:
    virtual void Encrypt(char * ptr, int size) = 0;
    virtual void Decrypt(char * ptr, int size) = 0;
    virtual void WriteEncooderName() = 0;
};

