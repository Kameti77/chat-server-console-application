#include "Logger.h"
#include <iostream>
#include <sstream>
#include <ctime>


// ─────────────────────────────────────────────────────────────
// Constructor / Destructor
// ─────────────────────────────────────────────────────────────

Logger::Logger() {}

Logger::~Logger()
{
    if (commandsFile.is_open())
        commandsFile.close();
    if (messagesFile.is_open())
        messagesFile.close();
}

// ─────────────────────────────────────────────────────────────
// open() — opens both log files for writing
// ─────────────────────────────────────────────────────────────
bool Logger::open()
{
    commandsFile.open("commands.log", std::ios::app);
    if (!commandsFile.is_open())
    {
        std::cout << "Warning: could not open commands.log\n";
        return false;
    }

    messagesFile.open("messages.log", std::ios::app);
    if (!messagesFile.is_open())
    {
        std::cout << "Warning: could not open messages.log\n";
        return false;
    }

    std::cout << "Log files opened (commands.log, messages.log)\n";
    return true;
}

std::string Logger::getTimestamp()
{
    std::time_t now = std::time(nullptr);
    std::tm t;
    localtime_s(&t, &now);

    char buf[32];
    std::strftime(buf, sizeof(buf), "[%Y-%m-%d %H:%M:%S]", &t);
    return std::string(buf);
}


void Logger::logCommand(const std::string& username,
    const std::string& command)
{
    // std::lock_guard locks the mutex for this scope.
    // When this function returns the lock is released automatically.
    // This prevents two threads writing at the same time.
    std::lock_guard<std::mutex> lock(logMutex);

    if (commandsFile.is_open())
    {
        commandsFile << getTimestamp() << " "
            << username << ": "
            << command << "\n";
        commandsFile.flush(); // write to disk immediately
    }
}

void Logger::logMessage(const std::string& username,
    const std::string& message)
{
    std::lock_guard<std::mutex> lock(logMutex);

    if (messagesFile.is_open())
    {
        messagesFile << getTimestamp() << " "
            << username << ": "
            << message << "\n";
        messagesFile.flush();
    }
}

std::string Logger::readMessageLog()
{
    std::ifstream file("messages.log");
    if (!file.is_open())
        return "No message log found.";

    std::ostringstream contents;
    contents << file.rdbuf();

    std::string result = contents.str();
    if (result.empty())
        return "Message log is empty.";

    return result;
}