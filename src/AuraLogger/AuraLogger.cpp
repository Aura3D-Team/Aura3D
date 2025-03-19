#include "AuraLogger.h"

#include <sstream>
#include <iostream>
#include <chrono>
#include <iomanip>
#include <cstring>

namespace aura3d {

Logger::Logger(const std::string& name)
    : m_Name(name), m_Level(LogLevel::INFO), m_UseColors(true), m_LogToFile(false)
{
}

Logger::~Logger() {
    if (m_FileStream.is_open()) {
        m_FileStream.close();
    }
}

void Logger::setLevel(LogLevel level) {
    m_Level = level;
}

bool Logger::isEnabled(LogLevel level) const {
    return level >= m_Level;
}

std::string Logger::getColorForLevel(LogLevel level) const {
    if (!m_UseColors) return "";

    switch (level) {
        case LogLevel::TRACE: return Colors::CYAN;
        case LogLevel::DEBUG: return Colors::BLUE;
        case LogLevel::INFO:  return Colors::GREEN;
        case LogLevel::WARN:  return Colors::YELLOW;
        case LogLevel::ERROR: return Colors::RED;
        case LogLevel::FATAL: return std::string(Colors::RED) + Colors::BOLD;
        default:              return "";
    }
}

std::string Logger::getLevelString(LogLevel level) const {
    switch (level) {
        case LogLevel::TRACE: return "TRACE";
        case LogLevel::DEBUG: return "DEBUG";
        case LogLevel::INFO:  return "INFO ";
        case LogLevel::WARN:  return "WARN ";
        case LogLevel::ERROR: return "ERROR";
        case LogLevel::FATAL: return "FATAL";
        default:              return "UNKNOWN";
    }
}

std::string Logger::getCurrentTimestamp() const {
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;

    std::stringstream ss;
    ss << std::put_time(std::localtime(&time), "%Y-%m-%d %H:%M:%S");
    ss << '.' << std::setfill('0') << std::setw(3) << ms.count();
    return ss.str();
}

void Logger::log(LogLevel level, const std::string& message, const char* file, int line) {
    // This check is redundant with the macro check, but kept for safety
    if (!isEnabled(level)) return;

    // Get just the filename from the path - avoid string allocation in hot path
    const char* filename = file;
    const char* lastSlash = strrchr(file, '/');
    const char* lastBackslash = strrchr(file, '\\');

    if (lastSlash != nullptr) {
        filename = lastSlash + 1;
    } else if (lastBackslash != nullptr) {
        filename = lastBackslash + 1;
    }

    // Use a buffer for formatting to avoid stringstream overhead
    thread_local char buffer[4096];
    std::lock_guard<std::mutex> lock(m_Mutex);

    std::string timestamp = getCurrentTimestamp();
    std::string levelStr = getLevelString(level);
    std::string color = getColorForLevel(level);
    std::string reset = m_UseColors ? Colors::RESET : "";

    // Format: [timestamp] [level] [name]: message (file:line)
    int len = snprintf(buffer, sizeof(buffer),
        "[%s] %s[%s]%s [%s]: %s (%s:%d)",
        timestamp.c_str(),
        color.c_str(),
        levelStr.c_str(),
        reset.c_str(),
        m_Name.c_str(),
        message.c_str(),
        filename,
        line);

    if (len > 0 && len < sizeof(buffer)) {
        // Output to console - use fwrite for better performance than cout
        fwrite(buffer, 1, len, stdout);
        fwrite("\n", 1, 1, stdout);
        fflush(stdout);

        // Output to file if enabled
        if (m_LogToFile && m_FileStream.is_open()) {
            // Write to file without color codes
            std::string fileMessage(buffer, len);
            size_t pos = 0;
            while ((pos = fileMessage.find("\033[")) != std::string::npos) {
                size_t endPos = fileMessage.find('m', pos);
                if (endPos != std::string::npos) {
                    fileMessage.erase(pos, endPos - pos + 1);
                } else {
                    break;
                }
            }

            m_FileStream << fileMessage << std::endl;
        }
    }
}

void Logger::setLogToFile(const std::string& filepath) {
    std::lock_guard<std::mutex> lock(m_Mutex);

    if (m_FileStream.is_open()) {
        m_FileStream.close();
    }

    m_LogToFile = !filepath.empty();

    if (m_LogToFile) {
        m_FileStream.open(filepath, std::ios::out | std::ios::app);
        if (!m_FileStream.is_open()) {
            std::cerr << "Failed to open log file: " << filepath << std::endl;
            m_LogToFile = false;
        }
    }
}

void Logger::setUseColors(bool useColors) {
    m_UseColors = useColors;
}

// LogManager implementation
std::shared_ptr<Logger> LogManager::getLogger(const std::string& name) {
    std::lock_guard<std::mutex> lock(m_Mutex);

    auto it = m_Loggers.find(name);
    if (it != m_Loggers.end()) {
        return it->second;
    }

    auto logger = std::make_shared<Logger>(name);
    logger->setLevel(m_GlobalLevel);

    if (!m_GlobalFilePath.empty()) {
        logger->setLogToFile(m_GlobalFilePath);
    }

    logger->setUseColors(m_GlobalUseColors);

    m_Loggers[name] = logger;
    return logger;
}

void LogManager::setGlobalLevel(LogLevel level) {
    std::lock_guard<std::mutex> lock(m_Mutex);

    m_GlobalLevel = level;

    for (auto& pair : m_Loggers) {
        pair.second->setLevel(level);
    }
}

void LogManager::setLogToFile(const std::string& filepath) {
    std::lock_guard<std::mutex> lock(m_Mutex);

    m_GlobalFilePath = filepath;

    for (auto& pair : m_Loggers) {
        pair.second->setLogToFile(filepath);
    }
}

void LogManager::setUseColors(bool useColors) {
    std::lock_guard<std::mutex> lock(m_Mutex);

    m_GlobalUseColors = useColors;

    for (auto& pair : m_Loggers) {
        pair.second->setUseColors(useColors);
    }
}

void InitializeLogger() {
    LogManager::getInstance().getLogger("AURA");
}

}
