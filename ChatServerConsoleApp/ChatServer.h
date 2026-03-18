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

// ChatServer is the heart of the program.
class ChatServer
{
private:
    // --- sockets ---
    SOCKET      listenSocket;   
    fd_set      masterSet;      
    fd_set      readySet;

    // --- components ---
    UserRegistry  registry;
    Logger        logger;
    MessageHandler* handler;

    // --- settings entered by admin at startup ---
    int         port;
    int         capacity;
    char        cmdChar;
    std::string serverIP;

    // --- connected clients ---
    std::map<int, ClientHandler> clients;

public:
    ChatServer();
    ~ChatServer();

    void promptAdminSettings();   // Step 1: ask port, capacity, cmdChar
    void displayServerInfo();     // Step 2: show IP and port on console
    bool initWinsock();           // Step 3: start up Winsock
    bool initSocket();            // Step 4: create socket, bind, listen
    void run();                   // Step 5: the main select() loop
    void stop();                  // clean shutdown

    // ── Framing helpers (used by run() for every client) ──────
    int sendMessage(SOCKET sock, const char* data, int length);

    int readMessage(SOCKET sock, char* buffer, int size);
};