#pragma once
#include <string>

// To be implemented in Phase 3.
class UDPBroadcaster
{
private:
    std::string serverIP;
    int         serverPort;

public:
    UDPBroadcaster(const std::string& ip, int port);
    void start();  
    void stop();
};