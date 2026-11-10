#pragma once

#include "LocatorAPI_defs.h"

#include <cstddef>
#include <cstdint>

class IReader
{
public:
    IReader(const uint8_t* data, size_t size)
        : m_data(data), m_size(size)
    {
    }

    const void* begin() const { return m_data; }
    size_t length() const { return m_size; }

private:
    const uint8_t* m_data = nullptr;
    size_t m_size = 0;
};

struct FSShim
{
    IReader* r_open(fsgame::ESearchPath path, const char* name);
    void r_close(IReader* reader);
};

extern FSShim FS;
