#ifndef KSNPRINTF_H
#define KSNPRINTF_H
#include <stdarg.h>
#include <stddef.h>
int ksnprintf(char *buf, size_t size, const char *fmt, ...);
#endif
