#pragma once

#include <string>
#include <fstream>
#include <atomic>
#include <cstdio>

class DebugLogger {
public:
    static DebugLogger& getInstance();

    void setLogFile(const std::string& filename);
    void log(const std::string& category, const std::string& message);
    void logf(const std::string& category, const char* format, ...);

    bool isEnabled() const { return enabled; }
    void setEnabled(bool enable) { enabled = enable; }

    // Cycle counter management
    void incrementCycle() { global_cycle_count++; }
    uint64_t getCycleCount() const { return global_cycle_count; }
    void setCycleCount(uint64_t count) { global_cycle_count = count; }

    // External cycle counter support (for thread-safe access from Simulator)
    void setExternalCycleCounter(std::atomic<uint64_t>* external_counter) {
        external_cycle_counter = external_counter;
    }
    uint64_t getCurrentCycleCount() const {
        if (external_cycle_counter) {
            return external_cycle_counter->load();
        }
        return global_cycle_count.load();
    }

    // Alias for compatibility with existing macros
    uint64_t getCurrentCycle() const {
        return getCurrentCycleCount();
    }

    // Debug level control
    enum DebugLevel {
        OFF = 0,        // No debug output
        CONCISE = 1,    // Only important information (errors, security, key stats)
        NORMAL = 2,     // Most debug information but not verbose
        VERBOSE = 3     // All debug information including verbose details
    };

    void setDebugLevel(DebugLevel level) { debugLevel = level; }
    DebugLevel getDebugLevel() const { return debugLevel; }
    bool shouldLog(const std::string& category, int level) const;

private:
    DebugLogger() : enabled(false), global_cycle_count(0), external_cycle_counter(nullptr) {
        // Set debug level from compile-time definition
        #ifdef DEBUG_LEVEL_CONCISE
            debugLevel = CONCISE;
        #elif defined(DEBUG_LEVEL_VERBOSE)
            debugLevel = VERBOSE;
        #elif defined(DEBUG_LEVEL_OFF)
            debugLevel = OFF;
        #else
            debugLevel = NORMAL;
        #endif
    }
    ~DebugLogger();

    DebugLogger(const DebugLogger&) = delete;
    DebugLogger& operator=(const DebugLogger&) = delete;

    std::ofstream logFile;
    bool enabled;
    DebugLevel debugLevel;
    std::atomic<uint64_t> global_cycle_count;
    std::atomic<uint64_t>* external_cycle_counter; // Pointer to external cycle counter
};

// Debug macros that only logs when DEBUG is enabled and the logger is active
#ifdef DEBUG
    #define DEBUG_LOG_ENABLED 1
#else
    #define DEBUG_LOG_ENABLED 0
#endif

// Category importance levels (lower = more important)
#define CATEGORY_IMPORTANCE_CORE 1     // Core execution, important events
#define CATEGORY_IMPORTANCE_MMU 1      // MMU operations and page table walks
#define CATEGORY_IMPORTANCE_SECURITY 0 // Security violations and exceptions (highest priority)
#define CATEGORY_IMPORTANCE_SIM 1     // Simulator operations (important - show in concise)
#define CATEGORY_IMPORTANCE_CACHE 3    // Cache operations (verbose)
#define CATEGORY_IMPORTANCE_UART 2     // UART operations
#define CATEGORY_IMPORTANCE_DEBUG 2    // General debug info

// Category-specific debug macros with level control
#define CORE_LOG(msg) do { \
    if (DEBUG_LOG_ENABLED && DebugLogger::getInstance().shouldLog("[CORE]", CATEGORY_IMPORTANCE_CORE)) { \
        DebugLogger::getInstance().log("[CORE]", msg); \
    } \
} while(0)

#define CORE_LOGF(fmt, ...) do { \
    if (DEBUG_LOG_ENABLED && DebugLogger::getInstance().shouldLog("[CORE]", CATEGORY_IMPORTANCE_CORE)) { \
        DebugLogger::getInstance().logf("[CORE]", fmt, ##__VA_ARGS__); \
    } \
} while(0)

// Core-specific debug macros with core ID in category
#define CORE_ID_LOG(core_id, msg) do { \
    if (DEBUG_LOG_ENABLED && DebugLogger::getInstance().shouldLog("[CORE#" #core_id "]", CATEGORY_IMPORTANCE_CORE)) { \
        char category[32]; \
        snprintf(category, sizeof(category), "[CORE#%d]", core_id); \
        DebugLogger::getInstance().log(category, msg); \
    } \
} while(0)

#define CORE_ID_LOGF(core_id, fmt, ...) do { \
    if (DEBUG_LOG_ENABLED && DebugLogger::getInstance().shouldLog("[CORE#" #core_id "]", CATEGORY_IMPORTANCE_CORE)) { \
        char category[32]; \
        snprintf(category, sizeof(category), "[CORE#%d]", core_id); \
        char buffer[1024]; \
        snprintf(buffer, sizeof(buffer), fmt, ##__VA_ARGS__); \
        DebugLogger::getInstance().log(category, buffer); \
    } \
} while(0)

#define MMU_LOG(msg) do { \
    if (DEBUG_LOG_ENABLED && DebugLogger::getInstance().shouldLog("[MMU]", CATEGORY_IMPORTANCE_MMU)) { \
        DebugLogger::getInstance().log("[MMU]", msg); \
    } \
} while(0)

#define MMU_LOGF(fmt, ...) do { \
    if (DEBUG_LOG_ENABLED && DebugLogger::getInstance().shouldLog("[MMU]", CATEGORY_IMPORTANCE_MMU)) { \
        DebugLogger::getInstance().logf("[MMU]", fmt, ##__VA_ARGS__); \
    } \
} while(0)

#define UART_LOG(msg) do { \
    if (DEBUG_LOG_ENABLED && DebugLogger::getInstance().shouldLog("[UART]", CATEGORY_IMPORTANCE_UART)) { \
        DebugLogger::getInstance().log("[UART]", msg); \
    } \
} while(0)

#define UART_LOGF(fmt, ...) do { \
    if (DEBUG_LOG_ENABLED && DebugLogger::getInstance().shouldLog("[UART]", CATEGORY_IMPORTANCE_UART)) { \
        DebugLogger::getInstance().logf("[UART]", fmt, ##__VA_ARGS__); \
    } \
} while(0)

// Cache macros - separate levels for different verbosity
#define CACHE_LOG(msg) do { \
    if (DEBUG_LOG_ENABLED && DebugLogger::getInstance().shouldLog("[CACHE]", CATEGORY_IMPORTANCE_CACHE)) { \
        DebugLogger::getInstance().log("[CACHE]", msg); \
    } \
} while(0)

#define CACHE_LOGF(fmt, ...) do { \
    if (DEBUG_LOG_ENABLED && DebugLogger::getInstance().shouldLog("[CACHE]", CATEGORY_IMPORTANCE_CACHE)) { \
        DebugLogger::getInstance().logf("[CACHE]", fmt, ##__VA_ARGS__); \
    } \
} while(0)

// Verbose cache macros for detailed initialization (only in VERBOSE mode)
#define CACHE_VERBOSE_LOG(msg) do { \
    if (DEBUG_LOG_ENABLED && DebugLogger::getInstance().getDebugLevel() >= DebugLogger::VERBOSE) { \
        DebugLogger::getInstance().log("[CACHE]", msg); \
    } \
} while(0)

#define CACHE_VERBOSE_LOGF(fmt, ...) do { \
    if (DEBUG_LOG_ENABLED && DebugLogger::getInstance().getDebugLevel() >= DebugLogger::VERBOSE) { \
        DebugLogger::getInstance().logf("[CACHE]", fmt, ##__VA_ARGS__); \
    } \
} while(0)

#define SIM_LOG(msg) do { \
    if (DEBUG_LOG_ENABLED && DebugLogger::getInstance().shouldLog("[SIM]", CATEGORY_IMPORTANCE_SIM)) { \
        DebugLogger::getInstance().logf("[SIM]", "[C#%lu] %s", \
            static_cast<unsigned long>(DebugLogger::getInstance().getCurrentCycle()), msg); \
    } \
} while(0)

#define SIM_LOGF(fmt, ...) do { \
    if (DEBUG_LOG_ENABLED && DebugLogger::getInstance().shouldLog("[SIM]", CATEGORY_IMPORTANCE_SIM)) { \
        DebugLogger::getInstance().logf("[SIM]", "[C#%lu] " fmt, \
            static_cast<unsigned long>(DebugLogger::getInstance().getCurrentCycle()), ##__VA_ARGS__); \
    } \
} while(0)

#define DEBUG_LOG(msg) do { \
    if (DEBUG_LOG_ENABLED && DebugLogger::getInstance().shouldLog("[DEBUG]", CATEGORY_IMPORTANCE_DEBUG)) { \
        DebugLogger::getInstance().log("[DEBUG]", msg); \
    } \
} while(0)

#define DEBUG_LOGF(fmt, ...) do { \
    if (DEBUG_LOG_ENABLED && DebugLogger::getInstance().shouldLog("[DEBUG]", CATEGORY_IMPORTANCE_DEBUG)) { \
        DebugLogger::getInstance().logf("[DEBUG]", fmt, ##__VA_ARGS__); \
    } \
} while(0)

// Security and exception macros - always log when DEBUG enabled
#define SECURITY_LOG(msg) do { \
    if (DEBUG_LOG_ENABLED && DebugLogger::getInstance().shouldLog("[SECURITY]", CATEGORY_IMPORTANCE_SECURITY)) { \
        DebugLogger::getInstance().log("[SECURITY]", msg); \
    } \
} while(0)

#define SECURITY_LOGF(fmt, ...) do { \
    if (DEBUG_LOG_ENABLED && DebugLogger::getInstance().shouldLog("[SECURITY]", CATEGORY_IMPORTANCE_SECURITY)) { \
        DebugLogger::getInstance().logf("[SECURITY]", fmt, ##__VA_ARGS__); \
    } \
} while(0)