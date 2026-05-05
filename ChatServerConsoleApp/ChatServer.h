#pragma once

#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include <winsock2.h>
#include <ws2tcpip.h>
#include <string>
#include <map>

#include "ClientHandler.h"
#include "MessageHandler.h"
#include "UserRegistry.h"
#include "Logger.h"
#include "UDPBroadcaster.h"

class ChatServer
{
private:
    // --- sockets ---
    SOCKET      listenSocket;
    fd_set      masterSet;
    fd_set      readySet;

    // --- components ---
    UserRegistry    registry;
    Logger          logger;
    MessageHandler* handler;
    UDPBroadcaster* broadcaster;  // Phase 3 — UDP broadcast thread

    // --- settings ---
    int         port;
    int         capacity;
    char        cmdChar;
    std::string serverIP;

    // --- connected clients ---
    std::map<int, ClientHandler> clients;

public:
    ChatServer();
    ~ChatServer();

    void promptAdminSettings();
    void displayServerInfo();
    bool initWinsock();
    bool initSocket();
    void run();
    void stop();

    int sendMessage(SOCKET sock, const char* data, int length);
    int readMessage(SOCKET sock, char* buffer, int size);
};