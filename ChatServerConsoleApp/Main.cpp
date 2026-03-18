#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include "ChatServer.h"
#include <iostream>

// main() is where the program starts.
// It creates one ChatServer object
int main()
{
    ChatServer server;

    // Step 1: Ask for port, capacity, command character
    server.promptAdminSettings();

    // Step 2: Start Winsock 
    if (!server.initWinsock())
    {
        std::cout << "Failed to initialize Winsock. Exiting.\n";
        return 1;
    }

    // Step 3: Create socket, bind to port, start listening
    if (!server.initSocket())
    {
        std::cout << "Failed to initialize server socket. Exiting.\n";
        return 1;
    }

    // Step 4: Show the server IP and port on screen
    server.displayServerInfo();

    // Step 5: Enter the main loop — wait for clients
    server.run();

    return 0;
}