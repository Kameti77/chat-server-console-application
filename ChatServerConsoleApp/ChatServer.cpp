#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include "ChatServer.h"
#include <iostream>
#include <string>
#include <cstring>
#include <vector>

ChatServer::ChatServer()
{
    listenSocket = INVALID_SOCKET;
    port = 0;
    capacity = 0;
    cmdChar = '/';
    handler = nullptr;
    FD_ZERO(&masterSet);
    FD_ZERO(&readySet);
}

ChatServer::~ChatServer()
{
    stop();
    delete handler;
}

void ChatServer::promptAdminSettings()
{
    std::cout << "========================================\n";
    std::cout << "       Chat Server - Setup\n";
    std::cout << "========================================\n\n";

    while (true)
    {
        std::cout << "Enter TCP port number (1025 - 65535): ";
        std::cin >> port;
        if (port >= 1025 && port <= 65535)
            break;
        std::cout << "  Invalid port. Please enter a number between 1025 and 65535.\n";
    }

    while (true)
    {
        std::cout << "Enter chat capacity (max registered users, 1 - 100): ";
        std::cin >> capacity;
        if (capacity >= 1 && capacity <= 100)
            break;
        std::cout << "  Invalid capacity. Please enter a number between 1 and 100.\n";
    }

    registry.setCapacity(capacity);

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
    hints.ai_family = AF_UNSPEC;    // AF_UNSPEC = accept both IPv4 and IPv6
    hints.ai_socktype = SOCK_STREAM;

    if (getaddrinfo(hostname, nullptr, &hints, &result) == 0)
    {
        for (addrinfo* ptr = result; ptr != nullptr; ptr = ptr->ai_next)
        {
            char ipStr[INET6_ADDRSTRLEN];

            if (ptr->ai_family == AF_INET)
            {
                sockaddr_in* addr = (sockaddr_in*)ptr->ai_addr;
                inet_ntop(AF_INET, &addr->sin_addr, ipStr, sizeof(ipStr));
                std::cout << "  IPv4     : " << ipStr << "\n";
                if (serverIP.empty())
                    serverIP = ipStr;
            }
            else if (ptr->ai_family == AF_INET6)
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

bool ChatServer::initWinsock()
{
    WSADATA wsaData;
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

bool ChatServer::initSocket()
{
    listenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listenSocket == INVALID_SOCKET)
    {
        std::cout << "socket() failed. Error: " << WSAGetLastError() << "\n";
        return false;
    }
    std::cout << "Socket created successfully.\n";

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

    sockaddr_in serverAddr;
    memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons((u_short)port);
    serverAddr.sin_addr.s_addr = INADDR_ANY;

    if (bind(listenSocket, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR)
    {
        std::cout << "bind() failed. Error: " << WSAGetLastError() << "\n";
        std::cout << "Port " << port << " may already be in use.\n";
        closesocket(listenSocket);
        listenSocket = INVALID_SOCKET;
        return false;
    }
    std::cout << "Socket bound to port " << port << ".\n";

    if (listen(listenSocket, 10) == SOCKET_ERROR)
    {
        std::cout << "listen() failed. Error: " << WSAGetLastError() << "\n";
        closesocket(listenSocket);
        listenSocket = INVALID_SOCKET;
        return false;
    }
    std::cout << "Listening for connections on port " << port << ".\n";

    FD_SET(listenSocket, &masterSet);
    return true;
}

// ─────────────────────────────────────────────────────────────
// sendMessage — send a framed message to one client
// Format: [ 1 byte length ][ message bytes ]
// ─────────────────────────────────────────────────────────────
int ChatServer::sendMessage(SOCKET sock, const char* data, int length)
{
    if (length <= 0 || length > 255)
    {
        std::cout << "sendMessage: invalid length " << length << "\n";
        return -1;
    }

    unsigned char len = (unsigned char)length;
    int result = send(sock, (char*)&len, 1, 0);
    if (result == 0)  return -2;
    if (result < 0)   return -3;

    int sent = 0;
    while (sent < length)
    {
        result = send(sock, data + sent, length - sent, 0);
        if (result == 0)  return -2;
        if (result < 0)   return -3;
        sent += result;
    }
    return 0;
}

// ─────────────────────────────────────────────────────────────
// readMessage — read one complete framed message from a client
// ─────────────────────────────────────────────────────────────
int ChatServer::readMessage(SOCKET sock, char* buffer, int size)
{
    // Zero the buffer — prevents old bytes bleeding into new messages
    memset(buffer, 0, size);

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

    int received = 0;
    while (received < (int)length)
    {
        result = recv(sock, buffer + received, (int)length - received, 0);
        if (result == 0)  return -2;
        if (result < 0)   return -3;
        received += result;
    }

    buffer[length] = '\0';
    return 0;
}

// ─────────────────────────────────────────────────────────────
// run — main select() loop
// ─────────────────────────────────────────────────────────────
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
            std::cout << "select() failed. Error: " << WSAGetLastError() << "\n";
            break;
        }

        // ── New client connecting ─────────────────────────────
        if (FD_ISSET(listenSocket, &readySet))
        {
            SOCKET newClient = accept(listenSocket, NULL, NULL);
            if (newClient == INVALID_SOCKET)
            {
                std::cout << "accept() failed. Error: " << WSAGetLastError() << "\n";
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

            std::cout << "New client connected. Socket: " << newClient << "\n";

            std::string welcome =
                "Welcome to the Chat Server! "
                "Use '" + std::string(1, cmdChar) + "' to send commands. "
                "Type " + std::string(1, cmdChar) + "help to see all commands.";

            sendMessage(newClient, welcome.c_str(), (int)welcome.length());
        }

        // ── Data from existing clients ────────────────────────
        // Two separate removal lists:
        //   toRemoveGraceful — /logout was used, do proper TCP teardown
        //   toRemoveForced   — connection dropped, just close
        //
        // This matters for Wireshark — /logout must show the clean
        // four-way handshake (FIN/ACK/FIN/ACK), not a RST packet.
        std::vector<SOCKET> toRemoveGraceful;
        std::vector<SOCKET> toRemoveForced;

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
                std::cout << "Client " << clientSock << " disconnected.\n";
                toRemoveForced.push_back(clientSock);
            }
            else if (result == -3)
            {
                std::cout << "Read error on socket " << clientSock
                    << ". Disconnecting.\n";
                toRemoveForced.push_back(clientSock);
            }
            else
            {
                std::string msg(buffer);
                std::cout << "Message from socket " << clientSock
                    << ": " << msg << "\n";

                if (handler != nullptr)
                {
                    bool keepAlive = handler->handle(
                        (int)clientSock, msg, clients);

                    // handle() returns false only on /logout — graceful
                    if (!keepAlive)
                        toRemoveGraceful.push_back(clientSock);
                }
            }
        }

        // ── Graceful disconnects (/logout) ────────────────────
        // shutdown(SD_SEND) sends a FIN to the client.
        // This triggers the four-way TCP termination handshake:
        //   Server FIN  →  Client ACK
        //   Client FIN  →  Server ACK
        // Wireshark will show FIN/ACK/FIN/ACK as the rubric requires.
        for (SOCKET sock : toRemoveGraceful)
        {
            // Step 1: send FIN — we are done sending
            shutdown(sock, SD_SEND);

            // Step 2: drain any remaining bytes before the client's
            // FIN arrives. Without this the OS may send RST instead.
            char drain[64];
            fd_set drainSet;
            timeval timeout;
            timeout.tv_sec = 0;
            timeout.tv_usec = 100000; // 100ms

            FD_ZERO(&drainSet);
            FD_SET(sock, &drainSet);

            while (select(0, &drainSet, NULL, NULL, &timeout) > 0)
            {
                int r = recv(sock, drain, sizeof(drain), 0);
                if (r <= 0) break;
                FD_ZERO(&drainSet);
                FD_SET(sock, &drainSet);
            }

            // Step 3: fully close the socket
            FD_CLR(sock, &masterSet);
            closesocket(sock);
            clients.erase(sock);
            std::cout << "Socket " << sock
                << " closed gracefully (FIN/ACK/FIN/ACK).\n";
        }

        // ── Forced disconnects (error / unexpected drop) ──────
        // Connection is already dead — no shutdown() needed.
        for (SOCKET sock : toRemoveForced)
        {
            FD_CLR(sock, &masterSet);
            closesocket(sock);
            clients.erase(sock);
            std::cout << "Socket " << sock << " cleaned up.\n";
        }
    }
}

// ─────────────────────────────────────────────────────────────
// stop — close every socket and shut down Winsock
// ─────────────────────────────────────────────────────────────
void ChatServer::stop()
{
    for (auto& pair : clients)
    {
        SOCKET sock = (SOCKET)pair.first;
        shutdown(sock, SD_SEND);
        closesocket(sock);
    }
    clients.clear();

    if (listenSocket != INVALID_SOCKET)
    {
        closesocket(listenSocket);
        listenSocket = INVALID_SOCKET;
    }

    WSACleanup();
    std::cout << "Server stopped.\n";
}