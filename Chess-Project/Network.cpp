#include <iostream>
#include <string>
#include <SFML/Network.hpp>
#include <SFML/Network/IpAddress.hpp>
#include <optional>

#include "Network.hpp"

std::atomic<bool> isNetworkHost{ false };
std::atomic<bool> isBotMatch{ false }; // NEW

void networkManager::start(std::string mode) {
    std::cout << "Network\n";

    if (mode == "multiplayer") {
        isBotMatch.store(false, std::memory_order_release); // NEW
        BROADCAST_MESSAGE = "ChessGame";
        // First, search for existing servers via broadcast
        if (searchForServer(false)) {
            std::cout << "Found existing server, connecting as client\n";
            return;
        }

        // No server found, become host
        std::cout << "No existing server detected. Hosting...\n";
        hostNetwork();
    }
    else if (mode == "botMatch") {
        isBotMatch.store(true, std::memory_order_release);
        BROADCAST_MESSAGE = "chessGameEngine";
        std::cout << "BotMatch\n";
        // Directly host so the listener is actually listening (prevents accept spam)
        searchForServer(true);
    }
    else {
        std::cout << "Invalid mode: " << mode << "\n";
    }
}

bool networkManager::searchForServer(bool botMatch) {
    sf::UdpSocket searchSocket;
    searchSocket.setBlocking(false);

    // Bind to receive broadcasts
    if (searchSocket.bind(BROADCAST_PORT) != sf::Socket::Status::Done) {
        std::cout << "Failed to bind UDP socket for server search\n";
        return false;
    }

    // Start search message (no newline), append a dot every 5s
    std::cout << "Searching for server...";
    std::cout.flush();
    bool searchLineActive = true;

    auto startTime = std::chrono::steady_clock::now();
    std::chrono::milliseconds searchTimeout;
    if (botMatch) {
        searchTimeout = std::chrono::seconds(60); // 1 second search
    }
    else {
        searchTimeout = std::chrono::milliseconds(1000); // 1 minute search
    }

    auto nextDotTime = startTime + std::chrono::seconds(5);

    while (std::chrono::steady_clock::now() - startTime < searchTimeout) {
        // Append a dot every 5 seconds while searching
        auto now = std::chrono::steady_clock::now();
        if (now >= nextDotTime) {
            std::cout << ".";
            std::cout.flush();
            nextDotTime += std::chrono::seconds(5);
        }

        std::vector<std::uint8_t> data(1024);
        std::size_t received = 0;
        std::optional<sf::IpAddress> sender = std::nullopt;
        unsigned short port;

        sf::Socket::Status status = searchSocket.receive(data.data(), data.size(), received, sender, port);

        if (status == sf::Socket::Status::Done) {
            // Finish the "Searching..." line before other output
            if (searchLineActive) {
                std::cout << "\n";
                searchLineActive = false;
            }

            std::string message(reinterpret_cast<char*>(data.data()), received);  // Safe cast assuming text data
            if (message == BROADCAST_MESSAGE) {
                std::cout << "Found server at: " << sender.value().toString() << "\n";

                // Try to connect to the server
                clientSocket.setBlocking(true);
                sf::Socket::Status connectStatus = clientSocket.connect(sender.value(), TCP_PORT, sf::milliseconds(2000));

                if (connectStatus == sf::Socket::Status::Done) {
                    isNetworkHost.store(false, std::memory_order_release);
                    clientConnected = true;
                    clientSocket.setBlocking(false);

                    // Try to receive greeting
                    sf::Packet packet;
                    sf::Socket::Status rcvStatus = clientSocket.receive(packet);
                    if (rcvStatus == sf::Socket::Status::Done) {
                        std::string payload;
                        if (packet >> payload) {
                            std::cout << "Received greeting from server: " << payload << "\n";
                        }
                    }

                    std::cout << "Successfully connected to server: " << sender.value().toString() << "\n";
                    return true;
                }
                else {
                    std::cout << "Found server but failed to connect (status=" << static_cast<int>(connectStatus) << ")\n";
                }
            }
        }

        // Small delay to prevent busy waiting
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    // Finish the "Searching..." line before reporting no servers
    if (searchLineActive) {
        std::cout << "\n";
        searchLineActive = false;
    }

    std::cout << "No servers found\n";
    return false;
}

void networkManager::startBroadcasting() {
    shouldBroadcast.store(true, std::memory_order_release);

    broadcastThread = std::thread([this]() {
        sf::UdpSocket socket;
        socket.setBlocking(false);

        std::cout << "Started broadcasting server\n";

        while (shouldBroadcast.load(std::memory_order_acquire)) {
            // Broadcast to local network
            sf::Socket::Status status = socket.send(
                BROADCAST_MESSAGE.c_str(),
                BROADCAST_MESSAGE.size(),
                sf::IpAddress::Broadcast,
                BROADCAST_PORT
            );

            if (status != sf::Socket::Status::Done && status != sf::Socket::Status::NotReady) {
                std::cout << "Broadcast failed (status=" << static_cast<int>(status) << ")\n";
            }

            // Broadcast every 500ms
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }

        std::cout << "Stopped broadcasting\n";
        });
}

void networkManager::stopBroadcasting() {
    shouldBroadcast.store(false, std::memory_order_release);
    if (broadcastThread.joinable()) {
        broadcastThread.join();
    }
}
void networkManager::hostNetwork() {
    isNetworkHost.store(true, std::memory_order_release);
    std::cout << "Starting as host\n";

    hostListener.setBlocking(false);
    if (hostListener.listen(5555) != sf::Socket::Status::Done) {
        std::cout << "Failed to bind to port 5555\n";
        return;
    }

    std::cout << "Waiting for client...\n";
    clientSocket.setBlocking(false);

    // Start broadcasting to allow discovery
    startBroadcasting();
}

void networkManager::update() {
    if (isNetworkHost.load(std::memory_order_acquire)
        && !clientConnected
        && hostListener.getLocalPort() != 0) // only accept if listen() succeeded
    {
        sf::TcpSocket temp;
        if (hostListener.accept(temp) == sf::Socket::Status::Done) {
            temp.setBlocking(false);
            clientSocket = std::move(temp);
            clientConnected = true;

            auto remote = clientSocket.getRemoteAddress();
            std::cout << "Client connected: "
                      << (remote.has_value() ? remote->toString() : "<unknown>")
                      << "\n";
        }
    }
}

int networkManager::sendToClient(const json& message) {
    if (!isNetworkHost || !clientConnected) return 0;

    sf::Packet packet;
    std::string payload = message.dump();
    packet << payload;
    sf::Socket::Status st = clientSocket.send(packet);
    if (st == sf::Socket::Status::Done) {
        std::cout << "Message sent to client: " << payload << "\n";
        return 1;
    } else if (st == sf::Socket::Status::NotReady) {
        std::cout << "Send to client blocked (NotReady)\n";
        return 0;
    }
    std::cout << "Failed sending to client (status=" << static_cast<int>(st) << ")\n";
    return 0;
}

int networkManager::sendToHost(const json& message) {
	std::cout << "sendToHost called\n";
    if (isNetworkHost || !clientConnected) return 0;

    sf::Packet packet;
    std::string payload = message.dump();
    packet << payload;
    sf::Socket::Status st = clientSocket.send(packet);
    if (st == sf::Socket::Status::Done) {
        std::cout << "Message sent to Host: " << payload << "\n";
        return 1;
    } else if (st == sf::Socket::Status::NotReady) {
        std::cout << "Send to host blocked (NotReady)\n";
        return 0;
    }
    std::cout << "Failed sending to host (status=" << static_cast<int>(st) << ")\n";
    return 0;
}

int networkManager::receiveFromClient(json& outMessage) {
    if (!isNetworkHost || !clientConnected) return 0;
    sf::Packet packet;
    sf::Socket::Status status = clientSocket.receive(packet);
    if (status == sf::Socket::Status::Done) {
        std::string payload;
        if (!(packet >> payload)) return -2;
        json parsed = json::parse(payload, nullptr, false);
        if (parsed.is_discarded()) return -2;
        outMessage = parsed;
        std::cout << "Received from client: " << payload << "\n";
        return 1;
    } else if (status == sf::Socket::Status::Disconnected) {
        std::cout << "Client disconnected\n";
        clientConnected = false;
        return -1;
    }
    return 0;
}

int networkManager::receiveFromHost(json& outMessage) {
    if (isNetworkHost || !clientConnected) return 0;

    sf::Packet packet;
    sf::Socket::Status status = clientSocket.receive(packet);
    if (status == sf::Socket::Status::Done) {
        std::string payload;
        if (!(packet >> payload)) return -2;
        json parsed = json::parse(payload, nullptr, false);
        if (parsed.is_discarded()) return -2;
        outMessage = parsed;
        std::cout << "Received from host: " << payload << "\n";
        return 1;
    } else if (status == sf::Socket::Status::Disconnected) {
        std::cout << "Host disconnected\n";
        clientConnected = false;
        return -1;
    }
    return 0;
}