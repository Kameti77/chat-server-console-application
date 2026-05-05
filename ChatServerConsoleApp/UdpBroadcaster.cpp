#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include "UDPBroadcaster.h"
#include <iostream>
#include <chrono>
#include <cstring>

// ─────────────────────────────────────────────────────────────
// Constructor
// ─────────────────────────────────────────────────────────────
UDPBroadcaster::UDPBroadcaster(const std::string& ip,
    int tcpPort,
    int udpPort)
    : serverIP(ip),
    serverPort(tcpPort),
    broadcastPort(udpPort),
    running(false),
    udpSocket(INVALID_SOCKET)
{
}

// ─────────────────────────────────────────────────────────────
// start — spawns the background broadcast thread
//
// Called once after the TCP server is ready.
// The thread runs broadcastLoop() independently of the
// main select() loop so broadcasting never blocks TCP.
// ─────────────────────────────────────────────────────────────
void UDPBroadcaster::start()
{
    running = true;
    broadcastThread = std::thread(&UDPBroadcaster::broadcastLoop, this);
    std::cout << "UDP broadcaster started on port "
        << broadcastPort << ".\n";
}

// ─────────────────────────────────────────────────────────────
// stop — signals the thread to stop and waits for it to finish
//
// Sets running = false. The broadcast loop checks this flag
// each iteration and exits cleanly when it is false.
// joinable() check prevents crash if stop() is called before
// start() or called twice.
// ─────────────────────────────────────────────────────────────
void UDPBroadcaster::stop()
{
    running = false;

    if (broadcastThread.joinable())
        broadcastThread.join();

    if (udpSocket != INVALID_SOCKET)
    {
        closesocket(udpSocket);
        udpSocket = INVALID_SOCKET;
    }

    std::cout << "UDP broadcaster stopped.\n";
}

// ─────────────────────────────────────────────────────────────
// broadcastLoop — runs on the background thread
//
// Steps:
//   1. Create a UDP socket
//   2. Enable SO_BROADCAST so we can send to 255.255.255.255
//   3. Bind to INADDR_ANY (required for UDP broadcast sockets)
//   4. Build the broadcast message: "IP:PORT"
//   5. Loop every 5 seconds — call sendto() then sleep
// ─────────────────────────────────────────────────────────────
void UDPBroadcaster::broadcastLoop()
{
    // ── Step 1: Create UDP socket ─────────────────────────────
    // SOCK_DGRAM = UDP (datagram, no connection, no guarantee)
    // Unlike TCP there is no connect() or accept() —
    // just create, configure, and sendto().
    udpSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (udpSocket == INVALID_SOCKET)
    {
        std::cout << "UDP socket creation failed. Error: "
            << WSAGetLastError() << "\n";
        return;
    }

    // ── Step 2: Enable SO_BROADCAST ──────────────────────────
    // By default Windows blocks sending to broadcast addresses.
    // SO_BROADCAST unlocks this. Without it, sendto() to
    // 255.255.255.255 returns WSAEACCES (permission denied).
    int broadcastEnable = 1;
    if (setsockopt(udpSocket, SOL_SOCKET, SO_BROADCAST,
        (char*)&broadcastEnable,
        sizeof(broadcastEnable)) == SOCKET_ERROR)
    {
        std::cout << "setsockopt(SO_BROADCAST) failed. Error: "
            << WSAGetLastError() << "\n";
        closesocket(udpSocket);
        udpSocket = INVALID_SOCKET;
        return;
    }

    // ── Step 3: Bind to INADDR_ANY ────────────────────────────
    // Binding a UDP socket to INADDR_ANY tells the OS which
    // local port this socket owns. This is required so the OS
    // knows which program should receive any UDP replies.
    // Port 0 means "let the OS pick a free port for us."
    sockaddr_in localAddr{};
    localAddr.sin_family = AF_INET;
    localAddr.sin_port = 0;           // OS picks the port
    localAddr.sin_addr.s_addr = INADDR_ANY;  // any network interface

    if (bind(udpSocket,
        (sockaddr*)&localAddr,
        sizeof(localAddr)) == SOCKET_ERROR)
    {
        std::cout << "UDP bind() failed. Error: "
            << WSAGetLastError() << "\n";
        closesocket(udpSocket);
        udpSocket = INVALID_SOCKET;
        return;
    }

    // ── Step 4: Build the broadcast destination address ───────
    // 255.255.255.255 is the limited broadcast address.
    // Every device on the local subnet receives packets sent here.
    // broadcastPort is the UDP port clients listen on to find us.
    sockaddr_in broadcastAddr{};
    broadcastAddr.sin_family = AF_INET;
    broadcastAddr.sin_port = htons((u_short)broadcastPort);
    broadcastAddr.sin_addr.s_addr = INADDR_BROADCAST; // 255.255.255.255

    // ── Step 5: Build the broadcast message ───────────────────
    // Format: "IP:PORT" e.g. "192.168.1.5:5000"
    // This is what SpaghettiRelay reads to find the server.
    std::string message = serverIP + ":" + std::to_string(serverPort);

    std::cout << "Broadcasting: " << message
        << " every 5 seconds.\n";

    // ── Step 6: Broadcast loop ────────────────────────────────
    // Send the message every 5 seconds until stop() is called.
    // std::this_thread::sleep_for sleeps without blocking
    // the main thread — only this background thread sleeps.
    while (running)
    {
        int result = sendto(
            udpSocket,
            message.c_str(),
            (int)message.length(),
            0,
            (sockaddr*)&broadcastAddr,
            sizeof(broadcastAddr)
        );

        if (result == SOCKET_ERROR)
        {
            std::cout << "UDP sendto() failed. Error: "
                << WSAGetLastError() << "\n";
            // Do not exit — just log and try again next cycle
        }
        else
        {
            std::cout << "UDP broadcast sent: " << message << "\n";
        }

        // Sleep 5 seconds between broadcasts.
        // We sleep in 500ms chunks so stop() responds quickly
        // instead of waiting up to 5 seconds to exit.
        for (int i = 0; i < 10 && running; i++)
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    // Clean up the socket when the loop exits
    closesocket(udpSocket);
    udpSocket = INVALID_SOCKET;
}