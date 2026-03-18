#pragma once
#include <string>
#include <fstream>
#include <mutex>

// Logger writes two log files while the server runs:
//   commands.log  
//   messages.log  
//
// It is a singleton — only one instance should ever exist.
class Logger
{
private:
    std::ofstream commandsFile;
    std::ofstream messagesFile;
    std::mutex    logMutex;

    // Returns a timestamp string
    std::string getTimestamp();

public:
    Logger();
    ~Logger();

    bool open();  // opens both log files, returns false if it fails

    void logCommand(const std::string& username,
        const std::string& command);

    void logMessage(const std::string& username,
        const std::string& message);

    // Reads and returns the full contents of messages.log
    std::string readMessageLog();
};