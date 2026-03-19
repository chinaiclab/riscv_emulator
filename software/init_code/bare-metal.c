#include "bare-metal.h"

// errno variable
int errno = 0;

// errno constants
#define ENOMEM 12

// Simple heap for _sbrk
static unsigned char heap[HEAP_SIZE];
static unsigned char *heap_ptr = heap;

// Minimal _sbrk implementation for malloc
void *_sbrk(ptrdiff_t incr) {
    if (incr < 0) {
        errno = ENOMEM;
        return (void *)-1;
    }

    unsigned char *prev = heap_ptr;
    if ((heap_ptr + incr) > (heap + HEAP_SIZE)) {
        errno = ENOMEM;
        return (void *)-1; // out of memory
    }
    heap_ptr += incr;
    return (void *)prev;
}

// sbrk wrapper for compatibility
void *sbrk(ptrdiff_t incr) {
    return _sbrk(incr);
}

// memset implementation
void *memset(void *ptr, int value, size_t num) {
    unsigned char *p = (unsigned char *)ptr;
    while (num--) {
        *p++ = (unsigned char)value;
    }
    return ptr;
}

// memcpy implementation
void *memcpy(void *dest, const void *src, size_t n) {
    unsigned char *d = (unsigned char *)dest;
    const unsigned char *s = (const unsigned char *)src;
    while (n--) {
        *d++ = *s++;
    }
    return dest;
}

// memcmp implementation
int memcmp(const void *ptr1, const void *ptr2, size_t num) {
    const unsigned char *p1 = (const unsigned char *)ptr1;
    const unsigned char *p2 = (const unsigned char *)ptr2;

    while (num--) {
        if (*p1 != *p2) {
            return *p1 - *p2;
        }
        p1++;
        p2++;
    }
    return 0;
}

// strlen implementation
size_t strlen(const char *str) {
    size_t len = 0;
    while (str[len]) {
        len++;
    }
    return len;
}

// strcmp implementation
int strcmp(const char *str1, const char *str2) {
    while (*str1 && (*str1 == *str2)) {
        str1++;
        str2++;
    }
    return *(unsigned char *)str1 - *(unsigned char *)str2;
}

// Function to get the core ID using mhartid CSR
uint32_t get_core_id(void) {
    uint32_t hart_id;
    // Use the mhartid CSR (address 0xF14) to get the hardware thread ID
    __asm__ volatile ("csrr %0, 0xF14" : "=r"(hart_id));
    return hart_id;
}

// Function to release all other cores for parallel execution
void release_all_cores(void) {
    // Write to the new MultiCoreMonitor release signal address (0x98000)
    // MultiCoreMonitor monitors this location to release HALTED cores
    *((volatile uint32_t*)0x98000) = 0xDEADBEEF;  // Magic value to signal core release
}

// Function for cores to report their state to MultiCoreMonitor
void report_core_state(uint32_t state_value) {
    uint32_t core_id = get_core_id();
    // Core state array starts at 0x90000, each core has 4 bytes
    *((volatile uint32_t*)(0x90000 + core_id * 4)) = state_value;
}

// Setup function for other cores
void setup_other_cores(void) {
    uint32_t core_id = get_core_id();

    if (core_id == 0) {
        // Core 0 sets up boot addresses for other cores
        // Set all cores to start at the main program entry point
        extern char __main_program_start;
        uint32_t main_entry_point = (uint32_t)&__main_program_start;

        // For Core#1, Core#2, Core#3, etc., set their entry points
        // In this implementation, all cores will start at the same entry point
        // The startup code (start.S) will handle core-specific initialization

        // Note: The actual PC setting is handled by the simulator
        // This function is for documentation and potential future extensions
        // where Core#0 could configure different entry points for different cores
    }
}

// Minimal system call implementations for bare-metal environment

// Read function - not implemented for UART (no input device)
ssize_t _read(int file, void *ptr, size_t len) {
    (void)file;
    (void)ptr;
    (void)len;
    errno = ENOSYS; // Function not implemented
    return -1;
}

// Close function - no files to close in bare-metal
int _close(int file) {
    (void)file;
    return 0; // Success
}

// Lseek function - not applicable for UART
off_t _lseek(int file, off_t ptr, int dir) {
    (void)file;
    (void)ptr;
    (void)dir;
    errno = ESPIPE; // Illegal seek
    return (off_t)-1;
}

// Fstat function - minimal implementation
int _fstat(int file, struct stat *st) {
    (void)file;
    st->st_mode = S_IFCHR; // Character device
    st->st_blksize = 1024;
    return 0;
}

// Isatty function - UART is a tty
int _isatty(int file) {
    (void)file;
    return 1; // UART acts like a terminal
}

// Exit function - halt the system
void _exit(int status) {
    (void)status;
    // In a real system, we might halt the processor
    // For now, just enter an infinite loop
    while (1) {
        __asm__ volatile("wfi"); // Wait for interrupt
    }
}

// Kill function - not applicable
int _kill(int pid, int sig) {
    (void)pid;
    (void)sig;
    errno = EINVAL;
    return -1;
}

// Getpid function - return a fake PID
int _getpid(void) {
    return 1; // Fake PID
}

// Environment variables - not supported
char *__env[1] = {0};
char **environ = __env;