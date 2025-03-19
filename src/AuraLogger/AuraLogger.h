#pragma once
#include <string>
#include <sstream>
#include <fstream>
#include <mutex>
#include <unordered_map>
#include <memory>

// Define build configuration macros
#if defined(AURA_CONFIG_RELEASE)
#define AURA_DISABLE_DEBUG_LOGGING
#endif
#if defined(AURA_CONFIG_DIST)
#define AURA_DISABLE_LOGGING
#endif

namespace aura3d {

enum class LogLevel {
    OFF = 0,
    FATAL = 1,
    ERROR = 2,
    WARN = 3,
    INFO = 4,
    DEBUG = 5,
    TRACE = 6
};

class Logger {
public:
    struct LogMessage {
        LogLevel level;
        std::string message;
        std::string timestamp;
        std::string file;
        int line;
    };

    // ANSI color codes for terminal output
    struct Colors {
        static constexpr const char* RESET   = "\033[0m";
        static constexpr const char* BLACK   = "\033[30m";
        static constexpr const char* RED     = "\033[31m";
        static constexpr const char* GREEN   = "\033[32m";
        static constexpr const char* YELLOW  = "\033[33m";
        static constexpr const char* BLUE    = "\033[34m";
        static constexpr const char* MAGENTA = "\033[35m";
        static constexpr const char* CYAN    = "\033[36m";
        static constexpr const char* WHITE   = "\033[37m";
        static constexpr const char* BOLD    = "\033[1m";
        static constexpr const char* UNDERLINE = "\033[4m";
    };

    Logger(const std::string& name);
    ~Logger();

    void setLevel(LogLevel level);
    bool isEnabled(LogLevel level) const;
    void log(LogLevel level, const std::string& message, const char* file, int line);
    std::string getColorForLevel(LogLevel level) const;
    std::string getLevelString(LogLevel level) const;
    void setLogToFile(const std::string& filepath);
    void setUseColors(bool useColors);

private:
    std::string m_Name;
    LogLevel m_Level;
    bool m_UseColors;
    std::mutex m_Mutex;
    std::ofstream m_FileStream;
    bool m_LogToFile;
    std::string getCurrentTimestamp() const;
};

// Stream-style logging class
class LogStream {
public:
    LogStream(std::shared_ptr<Logger> logger, LogLevel level, const char* file, int line)
        : m_Logger(logger), m_Level(level), m_File(file), m_Line(line) {}

    ~LogStream() {
        m_Logger->log(m_Level, m_Stream.str(), m_File, m_Line);
    }

    template<typename T>
    LogStream& operator<<(const T& value) {
        m_Stream << value;
        return *this;
    }

private:
    std::shared_ptr<Logger> m_Logger;
    LogLevel m_Level;
    std::stringstream m_Stream;
    const char* m_File;
    int m_Line;
};

// Global logger manager
class LogManager {
public:
    static LogManager& getInstance() {
        static LogManager instance;
        return instance;
    }

    std::shared_ptr<Logger> getLogger(const std::string& name);
    void setGlobalLevel(LogLevel level);
    void setLogToFile(const std::string& filepath);
    void setUseColors(bool useColors);

private:
    LogManager() = default;
    ~LogManager() = default;
    std::mutex m_Mutex;
    std::unordered_map<std::string, std::shared_ptr<Logger>> m_Loggers;
    LogLevel m_GlobalLevel = LogLevel::INFO;
    std::string m_GlobalFilePath;
    bool m_GlobalUseColors = true;
};

// Helper macros for the logger
#ifdef AURA_DISABLE_LOGGING
#define AURA_LOG_TRACE(logger) ((void)0)
#define AURA_LOG_DEBUG(logger) ((void)0)
#define AURA_LOG_INFO(logger)  ((void)0)
#define AURA_LOG_WARN(logger)  ((void)0)
#define AURA_LOG_ERROR(logger) ((void)0)
#define AURA_LOG_FATAL(logger) ((void)0)
#else
#define AURA_LOG_TRACE(logger) \
aura3d::LogStream(logger, aura3d::LogLevel::TRACE, __FILE__, __LINE__)

#define AURA_LOG_DEBUG(logger) \
    aura3d::LogStream(logger, aura3d::LogLevel::DEBUG, __FILE__, __LINE__)

#define AURA_LOG_INFO(logger) \
    aura3d::LogStream(logger, aura3d::LogLevel::INFO, __FILE__, __LINE__)

#define AURA_LOG_WARN(logger) \
    aura3d::LogStream(logger, aura3d::LogLevel::WARN, __FILE__, __LINE__)

#define AURA_LOG_ERROR(logger) \
    aura3d::LogStream(logger, aura3d::LogLevel::ERROR, __FILE__, __LINE__)

#define AURA_LOG_FATAL(logger) \
    aura3d::LogStream(logger, aura3d::LogLevel::FATAL, __FILE__, __LINE__)
#endif

}

// Define a core logger for global access
#define AURA_CORE_LOGGER aura3d::LogManager::getInstance().getLogger("AURA")

// Convenience macros that use the core logger
#ifdef AURA_DISABLE_LOGGING
#define AURA_TRACE ((void)0)
#define AURA_DEBUG ((void)0)
#define AURA_INFO  ((void)0)
#define AURA_WARN  ((void)0)
#define AURA_ERROR ((void)0)
#define AURA_FATAL ((void)0)
#else
#define AURA_TRACE aura3d::LogStream(AURA_CORE_LOGGER, aura3d::LogLevel::TRACE, __FILE__, __LINE__)
#define AURA_DEBUG aura3d::LogStream(AURA_CORE_LOGGER, aura3d::LogLevel::DEBUG, __FILE__, __LINE__)
#define AURA_INFO  aura3d::LogStream(AURA_CORE_LOGGER, aura3d::LogLevel::INFO, __FILE__, __LINE__)
#define AURA_WARN  aura3d::LogStream(AURA_CORE_LOGGER, aura3d::LogLevel::WARN, __FILE__, __LINE__)
#define AURA_ERROR aura3d::LogStream(AURA_CORE_LOGGER, aura3d::LogLevel::ERROR, __FILE__, __LINE__)
#define AURA_FATAL aura3d::LogStream(AURA_CORE_LOGGER, aura3d::LogLevel::FATAL, __FILE__, __LINE__)
#endif

// Initialize the logger
void InitializeLogger();
