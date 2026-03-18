#pragma once
#include <string>
#include <map>

class ChatServer;
class ClientHandler;
class UserRegistry;
class Logger;

// MessageHandler reads an incoming message and decides what to do.
class MessageHandler
{
private:
    UserRegistry& registry;
    Logger& logger;
    char          cmdChar;   // command prefix character e.g. '/'
    ChatServer& server;    // reference to server so we can sendMessage()

    // ── One function per command ──────────
    void handleHelp(int sock,
        const std::string& args,
        std::map<int, ClientHandler>& clients);

    void handleRegister(int sock,
        const std::string& args,
        std::map<int, ClientHandler>& clients);

    void handleLogin(int sock,
        const std::string& args,
        std::map<int, ClientHandler>& clients);

    void handleLogout(int sock,
        const std::string& args,
        std::map<int, ClientHandler>& clients,
        bool& shouldDisconnect);

    void handleGetList(int sock,
        const std::string& args,
        std::map<int, ClientHandler>& clients);

    void handleGetLog(int sock,
        const std::string& args,
        std::map<int, ClientHandler>& clients);

    void handleSend(int sock,
        const std::string& args,
        std::map<int, ClientHandler>& clients);

    void relayMessage(int senderSocket,
        const std::string& message,
        std::map<int, ClientHandler>& clients);

    // sends a reply to one specific client
    void reply(int sock, const std::string& message);

public:
    MessageHandler(UserRegistry& reg, Logger& log,
        char cmdCharacter, ChatServer& srv);

    // Called by run() every time a complete message arrives.
    // it returns false if the client should be disconnected.
    bool handle(int senderSocket,
        const std::string& message,
        std::map<int, ClientHandler>& clients);
};