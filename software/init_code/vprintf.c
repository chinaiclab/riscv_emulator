#include "vprintf.h"
#include "uart.h"

// Helper functions for division without M extension
static uint32_t divmod10(uint32_t n, uint32_t *rem) {
    // Division by 10 using subtraction only
    uint32_t q = 0;
    uint32_t r = n;

    // Subtract chunks to speed up the process
    // NOTE: When we subtract 1000, we add 100 to quotient (not 1000!)
    while (r >= 1000) {
        r -= 1000;
        q += 100;
    }

    // When we subtract 100, we add 10 to quotient
    while (r >= 100) {
        r -= 100;
        q += 10;
    }

    // When we subtract 10, we add 1 to quotient
    while (r >= 10) {
        r -= 10;
        q += 1;
    }

    // Now r < 10, so r is the remainder and q is complete
    *rem = r;
    return q;
}

// Simple printf implementation for bare-metal environment
int printf(const char *format, ...) {
    char buffer[1024];
    va_list args;
    int result;

    va_start(args, format);
    result = vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    // Write the formatted string to UART
    uart_write(buffer, result);

    return result;
}

// Clean vsnprintf implementation without M extension dependency
int vsnprintf(char *str, size_t size, const char *format, va_list ap) {
    size_t written = 0;

    while (*format && written < size - 1) {
        if (*format != '%') {
            str[written++] = *format++;
        } else {
            format++; // Skip '%'

            if (*format == '\0') break;

            // Parse width specifier (like %08X)
            int width = 0;
            int fill_zero = 0;

            // Parse width digits (including leading zero)
            while (*format >= '0' && *format <= '9') {
                width = width * 10 + (*format - '0');
                format++;
            }

            // For hex with width, enable zero padding
            if (width > 0) {
                fill_zero = 1;
            }

            // Handle format specifiers
            switch (*format) {
                case 'l':
                    format++;
                    if (*format == 'u') {
                        // %lu case
                        unsigned long val = va_arg(ap, unsigned long);
                        char num_buf[20];
                        int i = 0;
                        if (val == 0) {
                            num_buf[i++] = '0';
                        } else {
                            while (val > 0) {
                                uint32_t rem;
                                val = divmod10(val, &rem);
                                num_buf[i++] = '0' + rem;
                            }
                            // Reverse the number
                            for (int j = 0; j < i/2; j++) {
                                char temp = num_buf[j];
                                num_buf[j] = num_buf[i-1-j];
                                num_buf[i-1-j] = temp;
                            }
                        }
                        for (int j = 0; j < i && written < size - 1; j++) {
                            str[written++] = num_buf[j];
                        }
                    } else if (*format == 'X' || *format == 'x') {
                        // %lX or %lx case
                        unsigned long val = va_arg(ap, unsigned long);
                        const char hex_chars[] = "0123456789ABCDEF";
                        char num_buf[20];
                        int i = 0;
                        if (val == 0) {
                            num_buf[i++] = '0';
                        } else {
                            while (val > 0) {
                                num_buf[i++] = hex_chars[val & 0xF];
                                val >>= 4;
                            }
                            // Reverse the number
                            for (int j = 0; j < i/2; j++) {
                                char temp = num_buf[j];
                                num_buf[j] = num_buf[i-1-j];
                                num_buf[i-1-j] = temp;
                            }
                        }
                        // Apply width and padding
                        if (width > 0 && width > i) {
                            int padding = width - i;
                            if (fill_zero) {
                                while (padding > 0 && written < size - 1) {
                                    str[written++] = '0';
                                    padding--;
                                }
                            }
                        }
                        for (int j = 0; j < i && written < size - 1; j++) {
                            str[written++] = num_buf[j];
                        }
                    }
                    break;
                case 's': {
                    const char *s = va_arg(ap, const char*);
                    while (*s && written < size - 1) {
                        str[written++] = *s++;
                    }
                    break;
                }
                case 'c': {
                    char c = (char)va_arg(ap, int);
                    if (written < size - 1) {
                        str[written++] = c;
                    }
                    break;
                }
                case 'd': {
                    int val = va_arg(ap, int);
                    // Simple integer to string conversion
                    char num_buf[20];
                    int i = 0;
                    if (val == 0) {
                        num_buf[i++] = '0';
                    } else {
                        int negative = 0;
                        if (val < 0) {
                            negative = 1;
                            val = -val;
                        }
                        while (val > 0) {
                            uint32_t rem;
                            val = divmod10(val, &rem);
                            num_buf[i++] = '0' + rem;
                        }
                        if (negative) {
                            num_buf[i++] = '-';
                        }
                        // Reverse the number
                        for (int j = 0; j < i/2; j++) {
                            char temp = num_buf[j];
                            num_buf[j] = num_buf[i-1-j];
                            num_buf[i-1-j] = temp;
                        }
                    }
                    for (int j = 0; j < i && written < size - 1; j++) {
                        str[written++] = num_buf[j];
                    }
                    break;
                }
                case 'u': {
                    unsigned int val = va_arg(ap, unsigned int);
                    // Simple unsigned integer to string conversion
                    char num_buf[20];
                    int i = 0;
                    if (val == 0) {
                        num_buf[i++] = '0';
                    } else {
                        while (val > 0) {
                            uint32_t rem;
                            val = divmod10(val, &rem);
                            num_buf[i++] = '0' + rem;
                        }
                        // Reverse the number
                        for (int j = 0; j < i/2; j++) {
                            char temp = num_buf[j];
                            num_buf[j] = num_buf[i-1-j];
                            num_buf[i-1-j] = temp;
                        }
                    }
                    for (int j = 0; j < i && written < size - 1; j++) {
                        str[written++] = num_buf[j];
                    }
                    break;
                }
                case 'X':
                case 'x': {
                    unsigned int val = va_arg(ap, unsigned int);

                    // Convert to hexadecimal using bit shifts (no division needed)
                    const char hex_chars[] = "0123456789ABCDEF";
                    char num_buf[20];
                    int i = 0;
                    if (val == 0) {
                        num_buf[i++] = '0';
                    } else {
                        while (val > 0) {
                            num_buf[i++] = hex_chars[val & 0xF];
                            val >>= 4;
                        }
                        // Reverse the number
                        for (int j = 0; j < i/2; j++) {
                            char temp = num_buf[j];
                            num_buf[j] = num_buf[i-1-j];
                            num_buf[i-1-j] = temp;
                        }
                    }

                    // For hex format, default to 8-character zero padding
                    // This handles %08X and similar formats
                    int target_width = (width > 0) ? width : 8;
                    int target_fill_zero = fill_zero; // Use parsed fill_zero

                    // Apply width and padding
                    if (target_width > i) {
                        int padding = target_width - i;
                        if (target_fill_zero) {
                            // Zero padding
                            while (padding > 0 && written < size - 1) {
                                str[written++] = '0';
                                padding--;
                            }
                        }
                        // Note: space padding would go here if needed
                    }

                    // Output the actual number
                    for (int j = 0; j < i && written < size - 1; j++) {
                        str[written++] = num_buf[j];
                    }
                    break;
                }
                case '%': {
                    if (written < size - 1) {
                        str[written++] = '%';
                    }
                    break;
                }
                default:
                    // Unknown format specifier, just copy the character
                    if (written < size - 1) {
                        str[written++] = *format;
                    }
                    break;
            }
            format++;
        }
    }

    if (written < size) {
        str[written] = '\0';
    } else {
        str[size-1] = '\0';
    }

    return written;
}