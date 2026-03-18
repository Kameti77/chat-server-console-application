#pragma once
#pragma once
#include <string>

// ClientHandler holds all the information about one connected client.
class ClientHandler
{
public:
    int socket;             
    bool loggedIn;           // true if the client has successfully logged in
    std::string username;    // the username, once logged in

    ClientHandler();
    void reset();            // clears everything when a client disconnects
};

