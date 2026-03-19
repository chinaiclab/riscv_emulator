#ifndef UART_H
#define UART_H

#include <stdint.h>
#include <stddef.h>

// Type definition for ssize_t
typedef long ssize_t;

// UART mutex constants
#define UART_LOCKED 1
#define UART_UNLOCKED 0

// UART function declarations
void uart_lock(void);
void uart_unlock(void);
ssize_t uart_write(const void *ptr, size_t len);

#endif // UART_H