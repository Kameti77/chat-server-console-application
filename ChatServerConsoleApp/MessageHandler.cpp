#include "MessageHandler.h"
#include "ChatServer.h"
#include "ClientHandler.h"
#include "UserRegistry.h"
#include "Logger.h"
#include <iostream>
#include <sstream>
#include <string>
#include <map>

// ─────────────────────────────────────────────────────────────
// Constructor
// ─────────────────────────────────────────────────────────────
MessageHandler::MessageHandler(UserRegistry& reg, Logger& log,
    char cmdCharacter, ChatServer& srv)
    : registry(reg), logger(log), cmdChar(cmdCharacter), server(srv)
{
}

// ─────────────────────────────────────────────────────────────
// reply — sends one message back to a client
//
// MAX is 200 bytes — safely under the 255-byte frame limit.
// SpaghettiRelay's readMessage receives the 1-byte length first,
// then reads exactly that many bytes. If we send 255 as the
// length byte, SpaghettiRelay reads 255 bytes which is fine —
// but its internal buffer size is unknown so we stay at 200
// to guarantee nothing overflows on their side.
//
// If the message is longer than 200 bytes it is split into
// separate frames. Each frame arrives as a separate message
// in the SpaghettiRelay GUI.
//
// The null terminator is NOT sent over the wire — SpaghettiRelay
// adds it locally after recv(), just like we do in readMessage().
// ─────────────────────────────────────────────────────────────
void MessageHandler::reply(int sock, const std::string& message)
{
    if (message.empty())
        return;

    const int MAX = 200;

    int offset = 0;
    while (offset < (int)message.length())
    {
        int remaining = (int)message.length() - offset;
        int chunkLen = (remaining < MAX) ? remaining : MAX;

        server.sendMessage((SOCKET)sock,
            message.c_str() + offset,
            chunkLen);

        offset += chunkLen;
    }
}

// ─────────────────────────────────────────────────────────────
// handle — main entry point called by ChatServer::run()
// ─────────────────────────────────────────────────────────────
bool MessageHandler::handle(int senderSocket,
    const std::string& message,
    std::map<int, ClientHandler>& clients)
{
    if (message.empty())
        return true;

    if (message[0] == cmdChar)
    {
        std::istringstream stream(message);
        std::string cmdWord;
        stream >> cmdWord;

        // Strip the command character to get the name e.g. "help"
        std::string cmdName = cmdWord.substr(1);

        // Read remaining text as arguments
        std::string args;
        std::getline(stream >> std::ws, args);

        // Lowercase for case-insensitive matching
        std::string cmdLower = cmdName;
        for (char& c : cmdLower)
            c = std::tolower(static_cast<unsigned char>(c));

        if (cmdLower == "help")     handleHelp(senderSocket, args, clients);
        else if (cmdLower == "register") handleRegister(senderSocket, args, clients);
        else if (cmdLower == "login")    handleLogin(senderSocket, args, clients);
        else if (cmdLower == "logout")
        {
            bool shouldDisconnect = false;
            handleLogout(senderSocket, args, clients, shouldDisconnect);
            if (shouldDisconnect)
                return false;
        }
        else if (cmdLower == "getlist")  handleGetList(senderSocket, args, clients);
        else if (cmdLower == "getlog")   handleGetLog(senderSocket, args, clients);
        else if (cmdLower == "send")     handleSend(senderSocket, args, clients);
        else
        {
            reply(senderSocket,
                "Unknown command. Type " +
                std::string(1, cmdChar) +
                "help to see all commands.");
        }
    }
    else
    {
        auto it = clients.find(senderSocket);
        if (it == clients.end())
            return true;

        if (!it->second.loggedIn)
        {
            reply(senderSocket,
                "You must be logged in to send messages. "
                "Use " + std::string(1, cmdChar) + "login.");
            return true;
        }

        relayMessage(senderSocket, message, clients);
    }

    return true;
}

// ─────────────────────────────────────────────────────────────
// handleHelp
//
// Each command is sent as its own reply() call.
// This avoids chunking a large string and ensures each line
// arrives cleanly and displays correctly in SpaghettiRelay.
// ─────────────────────────────────────────────────────────────
void MessageHandler::handleHelp(int sock,
    const std::string& args,
    std::map<int, ClientHandler>& clients)
{
    std::string c(1, cmdChar);

    reply(sock, "=== Available Commands ===");
    reply(sock, c + "help - Show this list");
    reply(sock, c + "register <user> <pass> - Create account");
    reply(sock, c + "login <user> <pass> - Log in");
    reply(sock, c + "logout - Log out and disconnect");
    reply(sock, c + "send <user> <msg> - Private message");
    reply(sock, c + "getlist - Show online users");
    reply(sock, c + "getlog - Show public chat history");
    reply(sock, "==========================");
}

// ─────────────────────────────────────────────────────────────
// handleRegister
// ─────────────────────────────────────────────────────────────
void MessageHandler::handleRegister(int sock,
    const std::string& args,
    std::map<int, ClientHandler>& clients)
{
    std::istringstream stream(args);
    std::string username, password;
    stream >> username >> password;

    if (username.empty() || password.empty())
    {
        reply(sock, "Usage: " + std::string(1, cmdChar) +
            "register <username> <password>");
        return;
    }

    auto it = clients.find(sock);
    if (it != clients.end() && it->second.loggedIn)
    {
        reply(sock, "Already logged in. Use " +
            std::string(1, cmdChar) + "logout first.");
        return;
    }

    if (registry.isFull())
    {
        reply(sock, "Server is full. Registration not available.");
        return;
    }

    bool success = registry.registerUser(username, password);

    if (!success)
    {
        reply(sock, "Username '" + username + "' is already taken.");
        return;
    }

    reply(sock, "Registered successfully! Use " +
        std::string(1, cmdChar) + "login to log in.");

    std::cout << "New user registered: " << username << "\n";
    logger.logCommand(username, "register");
}

// ─────────────────────────────────────────────────────────────
// handleLogin
// ─────────────────────────────────────────────────────────────
void MessageHandler::handleLogin(int sock,
    const std::string& args,
    std::map<int, ClientHandler>& clients)
{
    auto it = clients.find(sock);
    if (it != clients.end() && it->second.loggedIn)
    {
        reply(sock, "Already logged in as '" +
            it->second.username + "'. Use " +
            std::string(1, cmdChar) + "logout first.");
        return;
    }

    std::istringstream stream(args);
    std::string username, password;
    stream >> username >> password;

    if (username.empty() || password.empty())
    {
        reply(sock, "Usage: " + std::string(1, cmdChar) +
            "login <username> <password>");
        return;
    }

    if (!registry.userExists(username))
    {
        reply(sock, "User '" + username + "' not found. "
            "Please register first.");
        return;
    }

    if (!registry.authenticate(username, password))
    {
        reply(sock, "Incorrect password. Please try again.");
        return;
    }

    if (it != clients.end())
    {
        it->second.loggedIn = true;
        it->second.username = username;
    }

    reply(sock, "Login successful! Welcome, " + username + ".");

    std::cout << username << " logged in on socket " << sock << "\n";
    logger.logCommand(username, "login");
}

// ─────────────────────────────────────────────────────────────
// handleLogout
// ─────────────────────────────────────────────────────────────
void MessageHandler::handleLogout(int sock,
    const std::string& args,
    std::map<int, ClientHandler>& clients,
    bool& shouldDisconnect)
{
    auto it = clients.find(sock);

    if (it == clients.end() || !it->second.loggedIn)
    {
        reply(sock, "Not logged in. Use " +
            std::string(1, cmdChar) + "login first.");
        return;
    }

    std::string username = it->second.username;

    reply(sock, "Goodbye, " + username + "! You have been logged out.");

    std::cout << username << " logged out.\n";
    logger.logCommand(username, "logout");

    shouldDisconnect = true;
}

// ─────────────────────────────────────────────────────────────
// handleGetList
// ─────────────────────────────────────────────────────────────
void MessageHandler::handleGetList(int sock,
    const std::string& args,
    std::map<int, ClientHandler>& clients)
{
    auto requester = clients.find(sock);
    if (requester == clients.end() || !requester->second.loggedIn)
    {
        reply(sock, "Must be logged in to use " +
            std::string(1, cmdChar) + "getlist.");
        return;
    }

    std::string list = "Online users: ";
    bool first = true;

    for (auto& pair : clients)
    {
        if (pair.second.loggedIn)
        {
            if (!first) list += ", ";
            list += pair.second.username;
            first = false;
        }
    }

    if (first)
        list = "No users currently online.";

    reply(sock, list);
    logger.logCommand(requester->second.username, "getlist");
}

// ─────────────────────────────────────────────────────────────
// handleGetLog
// ─────────────────────────────────────────────────────────────
void MessageHandler::handleGetLog(int sock,
    const std::string& args,
    std::map<int, ClientHandler>& clients)
{
    auto requester = clients.find(sock);
    if (requester == clients.end() || !requester->second.loggedIn)
    {
        reply(sock, "Must be logged in to use " +
            std::string(1, cmdChar) + "getlog.");
        return;
    }

    std::string logContents = logger.readMessageLog();

    reply(sock, "--- Chat Log Start ---");

    if (logContents.empty() ||
        logContents == "No message log found." ||
        logContents == "Message log is empty.")
    {
        reply(sock, "No public messages logged yet.");
    }
    else
    {
        // Send in 200-byte chunks via reply()
        // reply() already handles chunking so just pass the full string
        reply(sock, logContents);
    }

    reply(sock, "--- Chat Log End ---");

    logger.logCommand(requester->second.username, "getlog");
}

// ─────────────────────────────────────────────────────────────
// handleSend — private direct message
// ─────────────────────────────────────────────────────────────
void MessageHandler::handleSend(int sock,
    const std::string& args,
    std::map<int, ClientHandler>& clients)
{
    auto senderIt = clients.find(sock);
    if (senderIt == clients.end() || !senderIt->second.loggedIn)
    {
        reply(sock, "Must be logged in to send messages.");
        return;
    }

    std::istringstream stream(args);
    std::string targetName;
    stream >> targetName;

    std::string msgText;
    std::getline(stream >> std::ws, msgText);

    if (targetName.empty() || msgText.empty())
    {
        reply(sock, "Usage: " + std::string(1, cmdChar) +
            "send <username> <message>");
        return;
    }

    if (targetName == senderIt->second.username)
    {
        reply(sock, "Cannot send a message to yourself.");
        return;
    }

    int targetSock = -1;
    for (auto& pair : clients)
    {
        if (pair.second.loggedIn && pair.second.username == targetName)
        {
            targetSock = pair.first;
            break;
        }
    }

    if (targetSock == -1)
    {
        reply(sock, "User '" + targetName + "' not found or not logged in.");
        return;
    }

    std::string senderName = senderIt->second.username;

    reply(targetSock, "[DM from " + senderName + "]: " + msgText);
    reply(sock, "[DM to " + targetName + "]: " + msgText);

    std::cout << "DM: " << senderName << " -> " << targetName
        << ": " << msgText << "\n";

    logger.logCommand(senderName, "send -> " + targetName);
}

// ─────────────────────────────────────────────────────────────
// relayMessage — public message to all logged-in clients
// ─────────────────────────────────────────────────────────────
void MessageHandler::relayMessage(int senderSocket,
    const std::string& message,
    std::map<int, ClientHandler>& clients)
{
    auto senderIt = clients.find(senderSocket);
    if (senderIt == clients.end() || !senderIt->second.loggedIn)
        return;

    std::string senderName = senderIt->second.username;
    std::string formatted = "[" + senderName + "]: " + message;

    for (auto& pair : clients)
    {
        if (pair.first == senderSocket)  continue;
        if (!pair.second.loggedIn)       continue;

        reply(pair.first, formatted);
    }

    logger.logMessage(senderName, message);

    std::cout << "Relay [" << senderName << "]: " << message << "\n";
}