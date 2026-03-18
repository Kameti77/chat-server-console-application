#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include "ChatServer.h"
#include <iostream>
#include <string>
#include <cstring>
#include <vector>

// ─────────────────────────────────────────────────────────────
// Constructor / Destructor
// ─────────────────────────────────────────────────────────────

ChatServer::ChatServer()
{
    listenSocket = INVALID_SOCKET;
    port = 0;
    capacity = 0;
    cmdChar = '/';
    handler = nullptr;

    // Zero out both socket sets
    FD_ZERO(&masterSet);
    FD_ZERO(&readySet);
}

ChatServer::~ChatServer()
{
    stop();
    delete handler;
}

// ─────────────────────────────────────────────────────────────
// Step 1 — Ask the admin for startup settings
// ─────────────────────────────────────────────────────────────
void ChatServer::promptAdminSettings()
{
    std::cout << "========================================\n";
    std::cout << "       Chat Server - Setup\n";
    std::cout << "========================================\n\n";

    // --- Port ---
    while (true)
    {
        std::cout << "Enter TCP port number (1025 - 65535): ";
        std::cin >> port;
        if (port >= 1025 && port <= 65535)
            break;
        std::cout << "  Invalid port. Please enter a number between 1025 and 65535.\n";
    }

    // --- Capacity ---
    while (true)
    {
        std::cout << "Enter chat capacity (max registered users, 1 - 100): ";
        std::cin >> capacity;
        if (capacity >= 1 && capacity <= 100)
            break;
        std::cout << "  Invalid capacity. Please enter a number between 1 and 100.\n";
    }

    // Store the capacity in UserRegistry so it can enforce the limit
    registry.setCapacity(capacity);

    // --- Command character ---
    std::string cmdInput;
    while (true)
    {
        std::cout << "Enter command character (single character, default '/'): ";
        std::cin >> cmdInput;
        if (cmdInput.length() == 1)
        {
            cmdChar = cmdInput[0];
            break;
        }
        std::cout << "  Please enter exactly one character.\n";
    }

    handler = new MessageHandler(registry, logger, cmdChar, *this);

    std::cout << "\n";
}

// ─────────────────────────────────────────────────────────────
// Step 2 — Show the server's own IP address and port
// ─────────────────────────────────────────────────────────────
void ChatServer::displayServerInfo()
{
    char hostname[256];
    if (gethostname(hostname, sizeof(hostname)) == SOCKET_ERROR)
    {
        std::cout << "Could not retrieve hostname.\n";
        return;
    }

    std::cout << "========================================\n";
    std::cout << "  Server is running\n";
    std::cout << "  Hostname : " << hostname << "\n";

    addrinfo hints, * result;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;    // accept both IPv4 and IPv6
    hints.ai_socktype = SOCK_STREAM;

    if (getaddrinfo(hostname, nullptr, &hints, &result) == 0)
    {
        for (addrinfo* ptr = result; ptr != nullptr; ptr = ptr->ai_next)
        {
            char ipStr[INET6_ADDRSTRLEN];

            if (ptr->ai_family == AF_INET)  // IPv4
            {
                sockaddr_in* addr = (sockaddr_in*)ptr->ai_addr;
                inet_ntop(AF_INET, &addr->sin_addr, ipStr, sizeof(ipStr));
                std::cout << "  IPv4     : " << ipStr << "\n";

                // Save the first IPv4 address for the UDP broadcaster later
                if (serverIP.empty())
                    serverIP = ipStr;
            }
            else if (ptr->ai_family == AF_INET6)  // IPv6
            {
                sockaddr_in6* addr = (sockaddr_in6*)ptr->ai_addr;
                inet_ntop(AF_INET6, &addr->sin6_addr, ipStr, sizeof(ipStr));
                std::cout << "  IPv6     : " << ipStr << "\n";
            }
        }
        freeaddrinfo(result);
    }

    std::cout << "  Port     : " << port << "\n";
    std::cout << "  Capacity : " << capacity << " users\n";
    std::cout << "  Cmd char : '" << cmdChar << "'\n";
    std::cout << "========================================\n\n";
}

// ─────────────────────────────────────────────────────────────
// Step 3 — Initialize Winsock
// ─────────────────────────────────────────────────────────────
bool ChatServer::initWinsock()
{
    WSADATA wsaData;

    // MAKEWORD(2, 2) means "I want Winsock version 2.2"
    int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (result != 0)
    {
        std::cout << "WSAStartup failed with error: " << result << "\n";
        return false;
    }

    std::cout << "Winsock initialized successfully.\n";

    logger.open();

    return true;
}

// ─────────────────────────────────────────────────────────────
// Step 4 — Create socket, bind, listen
// ─────────────────────────────────────────────────────────────
bool ChatServer::initSocket()
{
    // ── 4a. Create the listening socket ──────────────────────
    listenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listenSocket == INVALID_SOCKET)
    {
        std::cout << "socket() failed. Error: "
            << WSAGetLastError() << "\n";
        return false;
    }
    std::cout << "Socket created successfully.\n";

    // ── 4b. Set SO_REUSEADDR ─────────────────────────────────
    int opt = 1;
    if (setsockopt(listenSocket, SOL_SOCKET, SO_REUSEADDR,
        (char*)&opt, sizeof(opt)) == SOCKET_ERROR)
    {
        std::cout << "setsockopt(SO_REUSEADDR) failed. Error: "
            << WSAGetLastError() << "\n";
        closesocket(listenSocket);
        listenSocket = INVALID_SOCKET;
        return false;
    }

    // ── 4c. Bind the socket to our port ──────────────────────
    sockaddr_in serverAddr;
    memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons((u_short)port);
    serverAddr.sin_addr.s_addr = INADDR_ANY;

    if (bind(listenSocket,
        (sockaddr*)&serverAddr,
        sizeof(serverAddr)) == SOCKET_ERROR)
    {
        std::cout << "bind() failed. Error: "
            << WSAGetLastError() << "\n";
        std::cout << "Port " << port << " may already be in use.\n";
        closesocket(listenSocket);
        listenSocket = INVALID_SOCKET;
        return false;
    }
    std::cout << "Socket bound to port " << port << ".\n";

    // ── 4d. Start listening ───────────────────────────────────
    if (listen(listenSocket, 10) == SOCKET_ERROR)
    {
        std::cout << "listen() failed. Error: "
            << WSAGetLastError() << "\n";
        closesocket(listenSocket);
        listenSocket = INVALID_SOCKET;
        return false;
    }
    std::cout << "Listening for connections on port " << port << ".\n";

    // ── 4e. Add the listening socket to masterSet ─────────────
    FD_SET(listenSocket, &masterSet);

    return true;
}

// ─────────────────────────────────────────────────────────────
// Step 5 — Main select() loop
// ─────────────────────────────────────────────────────────────
int ChatServer::sendMessage(SOCKET sock, const char* data, int length)
{
    // Validate: message must be 1-255 bytes
    // (1 byte length prefix means max value is 255)
    if (length <= 0 || length > 255)
    {
        std::cout << "sendMessage: invalid length " << length << "\n";
        return -1;
    }

    // ── Step 1: Send the 1-byte length prefix ─────────────────
    unsigned char len = (unsigned char)length;

    int result = send(sock, (char*)&len, 1, 0);
    if (result == 0)  return -2;  // clean disconnect
    if (result < 0)   return -3;  // error

    // ── Step 2: Send the actual message payload ────────────────
    int sent = 0;
    while (sent < length)
    {
        result = send(sock, data + sent, length - sent, 0);
        if (result == 0)  return -2;  // clean disconnect mid-send
        if (result < 0)   return -3;  // error mid-send
        sent += result;
    }

    return 0; // success
}

// ─────────────────────────────────────────────────────────────
// readMessage — read one complete framed message from a client
// ─────────────────────────────────────────────────────────────
int ChatServer::readMessage(SOCKET sock, char* buffer, int size)
{
    memset(buffer, 0, size);

    // ── Step 1: Read the 1-byte length prefix ─────────────────
    unsigned char length = 0;

    int result = recv(sock, (char*)&length, 1, 0);
    if (result == 0)  return -2;
    if (result < 0)   return -3;

    if (length >= size)
    {
        std::cout << "readMessage: incoming message (" << (int)length
            << " bytes) exceeds buffer size (" << size << ")\n";
        return -1;
    }

    // ── Step 2: Read exactly 'length' bytes ───────────────────
    int received = 0;
    while (received < (int)length)
    {
        result = recv(sock,
            buffer + received,
            (int)length - received,
            0);
        if (result == 0)  return -2;
        if (result < 0)   return -3;
        received += result;
    }

    buffer[length] = '\0';

    return 0;
}

void ChatServer::run()
{
    std::cout << "Server is ready. Waiting for clients...\n\n";

    int maxFd = (int)listenSocket;

    while (true)
    {
        readySet = masterSet;

        int activity = select(maxFd + 1, &readySet, NULL, NULL, NULL);

        if (activity == SOCKET_ERROR)
        {
            std::cout << "select() failed. Error: "
                << WSAGetLastError() << "\n";
            break;
        }

        // ── New client connecting ─────────────────────────────
        if (FD_ISSET(listenSocket, &readySet))
        {
            SOCKET newClient = accept(listenSocket, NULL, NULL);

            if (newClient == INVALID_SOCKET)
            {
                std::cout << "accept() failed. Error: "
                    << WSAGetLastError() << "\n";
                continue;
            }

            FD_SET(newClient, &masterSet);
            if ((int)newClient > maxFd)
                maxFd = (int)newClient;

            ClientHandler ch;
            ch.socket = (int)newClient;
            ch.loggedIn = false;
            ch.username = "";
            clients[newClient] = ch;

            std::cout << "New client connected. Socket: "
                << newClient << "\n";

            std::string welcome =
                "Welcome to the Chat Server! "
                "Use '" + std::string(1, cmdChar) + "' to send commands. "
                "Type " + std::string(1, cmdChar) + "help to see all commands.";

            sendMessage(newClient, welcome.c_str(), (int)welcome.length());
        }

        // ── Data from existing clients ────────────────────────
        std::vector<SOCKET> toRemove;

        for (auto& pair : clients)
        {
            SOCKET clientSock = (SOCKET)pair.first;

            if (!FD_ISSET(clientSock, &readySet))
                continue;

            char buffer[256];
            memset(buffer, 0, sizeof(buffer));

            int result = readMessage(clientSock, buffer, sizeof(buffer));

            if (result == -2)
            {
                std::cout << "Client " << clientSock
                    << " disconnected.\n";
                toRemove.push_back(clientSock);
            }
            else if (result == -3)
            {
                std::cout << "Read error on socket " << clientSock
                    << ". Disconnecting.\n";
                toRemove.push_back(clientSock);
            }
            else
            {
                // buffer is clean and null-terminated — safe to use
                std::string msg(buffer);

                std::cout << "Message from socket " << clientSock
                    << ": " << msg << "\n";

                if (handler != nullptr)
                {
                    bool keepAlive = handler->handle(
                        (int)clientSock, msg, clients);
                    if (!keepAlive)
                        toRemove.push_back(clientSock);
                }
            }
        }

        // ── Clean up disconnected clients ─────────────────────
        for (SOCKET sock : toRemove)
        {
            FD_CLR(sock, &masterSet);
            closesocket(sock);
            clients.erase(sock);
            std::cout << "Socket " << sock << " cleaned up.\n";
        }
    }
}

// ─────────────────────────────────────────────────────────────
// Cleanup — close every socket and shut down Winsock
// ─────────────────────────────────────────────────────────────
void ChatServer::stop()
{
    // Close all connected client sockets first
    for (auto& pair : clients)
    {
        shutdown((SOCKET)pair.first, SD_BOTH);
        closesocket((SOCKET)pair.first);
    }
    clients.clear();

    // Close the listening socket
    if (listenSocket != INVALID_SOCKET)
    {
        closesocket(listenSocket);
        listenSocket = INVALID_SOCKET;
    }

    // WSACleanup tells Windows we are done using Winsock.
    // Opposite of WSAStartup — always call this at the end.
    WSACleanup();
    std::cout << "Server stopped.\n";
}