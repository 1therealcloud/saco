#pragma once

// do not include precompiled header in this file
//#include <stdafx.h>

#include <cstdarg>
#include <cstdio>

inline void Log(const char* fmt, ...)
{
#if defined(_DEBUG)
    va_list args;
    va_start(args, fmt);

    vprintf(fmt, args);
    putchar('\n');

    va_end(args);
#endif
}
