#include "MessageHandler.h"
#include "ChatServer.h"       // full definition needed to call sendMessage()
#include "ClientHandler.h"
#include "UserRegistry.h"
#include "Logger.h"
#include <iostream>
#include <sstream>            // for std::istringstream (splitting words)
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

// reply — sends a message back to one client
void MessageHandler::reply(int sock, const std::string& message)
{
    if (message.empty())
        return;

    const int MAX = 200; // safe margin under 255-byte limit

    if ((int)message.length() <= MAX)
    {
        server.sendMessage((SOCKET)sock,
            message.c_str(),
            (int)message.length());
    }
    else
    {
        // Split into chunks and send each one separately
        int offset = 0;
        while (offset < (int)message.length())
        {
            std::string chunk = message.substr(offset, MAX);
            server.sendMessage((SOCKET)sock,
                chunk.c_str(),
                (int)chunk.length());
            offset += MAX;
        }
    }
}

// ─────────────────────────────────────────────────────────────
// handle — the main entry point
// ─────────────────────────────────────────────────────────────
bool MessageHandler::handle(int senderSocket,
    const std::string& message,
    std::map<int, ClientHandler>& clients)
{
    // Empty message — nothing to do
    if (message.empty())
        return true;

    // Check if the first character matches the command prefix.
    if (message[0] == cmdChar)
    {
        // ── Extract command word and arguments ────────────────
        std::istringstream stream(message);
        std::string cmdWord;
        stream >> cmdWord;  // read first word e.g. "/register"

        // Remove the command character from the front
        // so cmdWord becomes "register", "help", "login" etc.
        std::string cmdName = cmdWord.substr(1);


        std::string args;
        if (stream.peek() == ' ')
            stream.ignore();               // skip the space
        std::getline(stream, args);        // rest is arguments

        std::string cmdLower = cmdName;
        for (char& c : cmdLower)
            c = (char)tolower((unsigned char)c);

        if (cmdLower == "help")
        {
            handleHelp(senderSocket, args, clients);
        }
        else if (cmdLower == "register")
        {
            handleRegister(senderSocket, args, clients);
        }
        else if (cmdLower == "login")
        {
            handleLogin(senderSocket, args, clients);
        }
        else if (cmdLower == "logout")
        {
            bool shouldDisconnect = false;
            handleLogout(senderSocket, args, clients, shouldDisconnect);
            if (shouldDisconnect)
                return false; // tells run() to close this socket
        }
        else if (cmdLower == "getlist")
        {
            handleGetList(senderSocket, args, clients);
        }
        else if (cmdLower == "getlog")
        {
            handleGetLog(senderSocket, args, clients);
        }
        else if (cmdLower == "send")
        {
            handleSend(senderSocket, args, clients);
        }
        else
        {
            // Unknown command — tell the client
            reply(senderSocket,
                "Unknown command. Type " +
                std::string(1, cmdChar) +
                "help to see all available commands.");
        }
    }
    else
    {
        // ── Normal chat message ────────────────────────────────
        auto it = clients.find(senderSocket);
        if (it == clients.end())
            return true;

        if (!it->second.loggedIn)
        {
            reply(senderSocket,
                "Must be logged in to send messages. "
                "Use " + std::string(1, cmdChar) +
                "login username password.");
            return true;
        }

        // Relay to all other logged-in clients
        relayMessage(senderSocket, message, clients);
    }

    return true; // keep the connection alive
}

// ─────────────────────────────────────────────────────────────
// handleHelp — responds to /help
// ─────────────────────────────────────────────────────────────
void MessageHandler::handleHelp(int sock,
    const std::string& args,
    std::map<int, ClientHandler>& clients)
{
    // Build the help text listing every available command.
    // We use the actual cmdChar so if the admin chose '!'
    // the help text shows '!help', '!register' etc.
    std::string c(1, cmdChar);  

    std::string helpText =
        "Available commands:\n"
        "  " + c + "help                          - Show this list\n"
        "  " + c + "register <username> <password> - Create an account\n"
        "  " + c + "login <username> <password>    - Log into your account\n"
        "  " + c + "logout                         - Log out and disconnect\n"
        "  " + c + "send <username> <message>      - Send a private message\n"
        "  " + c + "getlist                        - Show online users\n"
        "  " + c + "getlog                         - Show public chat history";

    reply(sock, helpText);
}

// ─────────────────────────────────────────────────────────────
// handleRegister — responds to /register username password
// ─────────────────────────────────────────────────────────────
void MessageHandler::handleRegister(int sock,
    const std::string& args,
    std::map<int, ClientHandler>& clients)
{
    // ── Parse the two arguments: username and password ────────
    // args should be "john pass123"
    // If there are not exactly two words, the format is wrong.
    std::istringstream stream(args);
    std::string username, password;
    stream >> username >> password;

    if (username.empty() || password.empty())
    {
        reply(sock,
            "Usage: " + std::string(1, cmdChar) +
            "register <username> <password>");
        return;
    }

    // ── Check: is the client already logged in? ───────────────
    // A logged-in client cannot register a new account.
    auto it = clients.find(sock);
    if (it != clients.end() && it->second.loggedIn)
    {
        reply(sock, "Cannot register while already logged in. "
            "Use " + std::string(1, cmdChar) + "logout first.");
        return;
    }

    // ── Check: is the server at capacity? ────────────────────
    if (registry.isFull())
    {
        reply(sock, "Server is full. Registration is not available.");
        return;
    }

    // ── Try to register ───────────────────────────────────────
    // registerUser() returns false if the username already exists
    bool success = registry.registerUser(username, password);

    if (!success)
    {
        reply(sock, "Username '" + username + "' is already taken. "
            "Please choose a different username.");
        return;
    }

    // Success
    reply(sock, "Registration successful! Welcome, " + username +
        ". Use " + std::string(1, cmdChar) +
        "login " + username + " <password> to log in.");

    std::cout << "New user registered: " << username << "\n";

    // Log the registration event
    logger.logCommand(username, "register");
}

// ─────────────────────────────────────────────────────────────
// handleLogin — responds to /login username password
// ─────────────────────────────────────────────────────────────
void MessageHandler::handleLogin(int sock,
    const std::string& args,
    std::map<int, ClientHandler>& clients)
{
    // ── Check: already logged in? ────────────────────────────
    auto it = clients.find(sock);
    if (it != clients.end() && it->second.loggedIn)
    {
        reply(sock, "Already logged in as '" + it->second.username +
            "'. Use " + std::string(1, cmdChar) + "logout first.");
        return;
    }

    // ── Parse arguments ───────────────────────────────────────
    std::istringstream stream(args);
    std::string username, password;
    stream >> username >> password;

    if (username.empty() || password.empty())
    {
        reply(sock, "Usage: " + std::string(1, cmdChar) +
            "login <username> <password>");
        return;
    }

    // ── Check username exists ─────────────────────────────────
    if (!registry.userExists(username))
    {
        reply(sock, "User '" + username + "' not found. "
            "Use " + std::string(1, cmdChar) +
            "register to create an account.");
        return;
    }

    // ── Check password ────────────────────────────────────────
    if (!registry.authenticate(username, password))
    {
        reply(sock, "Incorrect password. Please try again.");
        return;
    }

    // ── Success — mark as logged in ───────────────────────────
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
// handleLogout — responds to /logout
// ─────────────────────────────────────────────────────────────
void MessageHandler::handleLogout(int sock,
    const std::string& args,
    std::map<int, ClientHandler>& clients,
    bool& shouldDisconnect)
{
    auto it = clients.find(sock);

    // Block logout if not logged in — nothing to log out from
    if (it == clients.end() || !it->second.loggedIn)
    {
        reply(sock, "Not logged in. Use " +
            std::string(1, cmdChar) + "login username password first.");
        return;
    }

    std::string username = it->second.username;

    reply(sock, "Goodbye, " + username + "! You have been logged out.");
    std::cout << username << " logged out from socket " << sock << "\n";
    logger.logCommand(username, "logout");

    shouldDisconnect = true;
}

// ─────────────────────────────────────────────────────────────
// handleGetList — responds to /getlist
// ─────────────────────────────────────────────────────────────
void MessageHandler::handleGetList(int sock,
    const std::string& args,
    std::map<int, ClientHandler>& clients)
{
    // Check the requester is logged in
    auto requester = clients.find(sock);
    if (requester == clients.end() || !requester->second.loggedIn)
    {
        reply(sock, "Must be logged in to use " +
            std::string(1, cmdChar) + "getlist.");
        return;
    }

    // Build the list of logged-in usernames
    std::string list = "Online users: ";
    int count = 0;

    for (auto& pair : clients)
    {
        if (pair.second.loggedIn)
        {
            if (count > 0)
                list += ", ";
            list += pair.second.username;
            count++;
        }
    }

    if (count == 0)
        list = "No users currently online.";

    reply(sock, list);
    logger.logCommand(requester->second.username, "getlist");
}

// ─────────────────────────────────────────────────────────────
// handleGetLog — responds to /getlog
// ─────────────────────────────────────────────────────────────
void MessageHandler::handleGetLog(int sock,
    const std::string& args,
    std::map<int, ClientHandler>& clients)
{
    // Check the requester is logged in
    auto requester = clients.find(sock);
    if (requester == clients.end() || !requester->second.loggedIn)
    {
        reply(sock, "Must be logged in to use " +
            std::string(1, cmdChar) + "getlog.");
        return;
    }

    std::string logContents = logger.readMessageLog();

    const int CHUNK_SIZE = 200;

    if ((int)logContents.size() <= CHUNK_SIZE)
    {
        reply(sock, logContents);
    }
    else
    {
        // Send in chunks so we never exceed the 255-byte frame limit
        int offset = 0;
        while (offset < (int)logContents.size())
        {
            std::string chunk = logContents.substr(offset, CHUNK_SIZE);
            reply(sock, chunk);
            offset += CHUNK_SIZE;
        }
    }

    logger.logCommand(requester->second.username, "getlog");
}

// ─────────────────────────────────────────────────────────────
// handleSend — responds to /send username message
//
// Sends a private message to one specific logged-in client.
// Nobody else receives it. Not saved to the public log.
// ─────────────────────────────────────────────────────────────
void MessageHandler::handleSend(int sock,
    const std::string& args,
    std::map<int, ClientHandler>& clients)
{
    // Must be logged in to send a direct message
    auto senderIt = clients.find(sock);
    if (senderIt == clients.end() || !senderIt->second.loggedIn)
    {
        reply(sock, "Must be logged in to send messages. Use " +
            std::string(1, cmdChar) + "login username password.");
        return;
    }

    std::istringstream stream(args);
    std::string targetName;
    stream >> targetName;

    if (targetName.empty())
    {
        reply(sock, "Usage: " + std::string(1, cmdChar) +
            "send <username> <message>");
        return;
    }

    // Read the rest as the message text
    std::string msgText;
    if (stream.peek() == ' ') stream.ignore();
    std::getline(stream, msgText);

    if (msgText.empty())
    {
        reply(sock, "Usage: " + std::string(1, cmdChar) +
            "send <username> <message>\n"
            "Message cannot be empty.");
        return;
    }

    // Cannot send to yourself
    if (targetName == senderIt->second.username)
    {
        reply(sock, "Cannot send a message to yourself.");
        return;
    }

    // Find the target client in the clients map
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

    // Format and deliver the private message
    std::string senderName = senderIt->second.username;
    std::string formatted = "[DM from " + senderName + "]: " + msgText;

    server.sendMessage((SOCKET)targetSock,
        formatted.c_str(),
        (int)formatted.length());

    // Confirm to the sender that it was delivered
    reply(sock, "[DM to " + targetName + "]: " + msgText);

    std::cout << "DM: " << senderName << " -> " << targetName
        << ": " << msgText << "\n";

    logger.logCommand(senderName, "send " + targetName);
}


void MessageHandler::relayMessage(int senderSocket,
    const std::string& message,
    std::map<int, ClientHandler>& clients)
{
    // Get the sender's username from their ClientHandler
    auto senderIt = clients.find(senderSocket);
    if (senderIt == clients.end() || !senderIt->second.loggedIn)
        return;

    std::string senderName = senderIt->second.username;

    // Format the message with the sender's name at the front
    // This is what all other clients will see
    std::string formatted = "[" + senderName + "]: " + message;

    // Loop through every connected client
    for (auto& pair : clients)
    {
        // Skip the sender — they don't receive their own message
        if (pair.first == senderSocket)
            continue;

        // Only send to clients that are logged in
        if (!pair.second.loggedIn)
            continue;

        // Send the formatted message to this client
        server.sendMessage((SOCKET)pair.first,
            formatted.c_str(),
            (int)formatted.length());
    }

    // Save to messages.log
    logger.logMessage(senderName, message);

    std::cout << "Relayed from " << senderName << ": " << message << "\n";
}