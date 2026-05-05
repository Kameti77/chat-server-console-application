#pragma once

#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include <winsock2.h>
#include <ws2tcpip.h>
#include <string>
#include <thread>
#include <atomic>

// UDPBroadcaster runs on a separate thread.
// Every 5 seconds it sends a UDP broadcast containing
// the server's IP and port so clients can find it
// automatically on the local network.
//
// Uses SO_BROADCAST to allow sending to 255.255.255.255
// which delivers the packet to every device on the subnet.
class UDPBroadcaster
{
private:
    std::string  serverIP;      // server's IPv4 address
    int          serverPort;    // server's TCP port
    int          broadcastPort; // UDP port to broadcast on

    std::thread  broadcastThread;
    std::atomic<bool> running;  // atomic so the thread sees stop() immediately

    SOCKET       udpSocket;

    // The function that runs on the background thread
    void broadcastLoop();

public:
    UDPBroadcaster(const std::string& ip, int tcpPort, int udpPort = 9999);
    void start();   // spawns the background thread
    void stop();    // signals thread to stop and waits for it
};