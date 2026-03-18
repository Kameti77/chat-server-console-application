#include "ClientHandler.h"

ClientHandler::ClientHandler()
{
    socket = -1;
    loggedIn = false;
    username = "";
}

void ClientHandler::reset()
{
    socket = -1;
    loggedIn = false;
    username = "";
}