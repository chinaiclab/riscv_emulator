#ifndef VPRINTF_H
#define VPRINTF_H

#include <stddef.h>

// va_list definitions for bare-metal environment
typedef __builtin_va_list va_list;
#define va_start(ap, last) __builtin_va_start(ap, last)
#define va_arg(ap, type) __builtin_va_arg(ap, type)
#define va_end(ap) __builtin_va_end(ap)

// printf function declarations
int printf(const char *format, ...);
int vsnprintf(char *str, size_t size, const char *format, va_list ap);

#endif // VPRINTF_H