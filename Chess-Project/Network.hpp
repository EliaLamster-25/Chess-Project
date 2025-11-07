#pragma once
#include <iostream>
#include <SFML/Network.hpp>
#include <thread>
#include <string>  // ADDED

#include "json.hpp"

using json = nlohmann::json;

extern std::atomic<bool> isNetworkHost;
extern std::atomic<bool> isBotMatch; // NEW

// ADDED: Global overlay buffer with the latest info received from the bot (client-side)
extern std::string g_BotInfoOverlay;

class networkManager {
public:
    // Establish connection (client) or host a listener (server fallback)
    void start(std::string mode);
    void hostNetwork();
    void update();

    int sendToClient(const json& message);
    int sendToHost(const json& message);

    int receiveFromClient(json& outMessage);
    int receiveFromHost(json& outMessage);

private:
    sf::TcpSocket      clientSocket;
    sf::TcpListener    hostListener;
    sf::UdpSocket      broadcastSocket;

    bool searchForServer(bool botMatch);
    void startBroadcasting();
    void stopBroadcasting();

    std::atomic<bool> shouldBroadcast{ false };
    std::thread broadcastThread;

    bool clientConnected = false;

    static constexpr unsigned short BROADCAST_PORT = 5554;
    static constexpr unsigned short TCP_PORT = 5555;
    std::string BROADCAST_MESSAGE;
};