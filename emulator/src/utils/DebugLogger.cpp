#include "utils/DebugLogger.h"
#include <iostream>
#include <cstdarg>
#include <cstdio>
#include <iomanip>
#include <sstream>

DebugLogger& DebugLogger::getInstance() {
    static DebugLogger instance;
    return instance;
}

DebugLogger::~DebugLogger() {
    if (logFile.is_open()) {
        logFile.close();
    }
}

void DebugLogger::setLogFile(const std::string& filename) {
    if (logFile.is_open()) {
        logFile.close();
    }

    logFile.open(filename, std::ios::out | std::ios::trunc);
    enabled = logFile.is_open();

    if (!enabled) {
        std::cerr << "Warning: Could not open debug log file: " << filename << std::endl;
    }
}

bool DebugLogger::shouldLog(const std::string& category, int importance) const {
    if (!enabled) return false;

    // Always log security and exception related messages if DEBUG is enabled
    if (category == "[SECURITY]") return true;

    // Check if the importance level is within the current debug level
    switch (debugLevel) {
        case OFF:
            return false;
        case CONCISE:
            return importance <= 1;  // Only log most important messages
        case NORMAL:
            return importance <= 2;  // Log most messages, but skip verbose ones
        case VERBOSE:
            return true;             // Log everything
        default:
            return false;
    }
}

void DebugLogger::log(const std::string& category, const std::string& message) {
    if (enabled && logFile.is_open()) {
        logFile << "[C#" << getCurrentCycleCount() << "] " << category << ": " << message << std::endl;
    }
}

void DebugLogger::logf(const std::string& category, const char* format, ...) {
    if (enabled && logFile.is_open()) {
        char buffer[1024];
        va_list args;
        va_start(args, format);
        vsnprintf(buffer, sizeof(buffer), format, args);
        va_end(args);

        logFile << "[C#" << getCurrentCycleCount() << "] " << category << ": " << buffer << std::endl;
    }
}