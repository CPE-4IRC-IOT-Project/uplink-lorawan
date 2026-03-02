#pragma once

#include <cstdint>

class DS1820 {
public:
    explicit DS1820(int)
        : _value(22000)
    {
    }

    bool begin()
    {
        return true;
    }

    void startConversion()
    {
    }

    int32_t read()
    {
        // Return a changing dummy value in milli-degrees.
        _value += 37;
        return _value;
    }

private:
    int32_t _value;
};
