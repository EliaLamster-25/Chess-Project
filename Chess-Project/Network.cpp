#include <iostream>
#include <string>
#include <SFML/Network.hpp>
#include <SFML/Network/IpAddress.hpp>
#include <optional>
#include <cstdio>

#include "Network.hpp"

std::atomic<bool> isNetworkHost{ false };
std::atomic<bool> isBotMatch{ false };

namespace {
    inline std::string trim(const std::string& s) {
        const auto l = s.find_first_not_of(" \t\r\n");
        if (l == std::string::npos) return {};
        const auto r = s.find_last_not_of(" \t\r\n");
        return s.substr(l, r - l + 1);
    }

    // Accepts:
    // - "e2e4" or "e7e8q"
    // - "bestmove e2e4" (with optional "ponder ...")
    // Returns normalized lowercase promotion, e.g., "e7e8q".
    bool tryExtractUCIMove(const std::string& in, std::string& outMove) {
        std::string s = trim(in);

        // If it starts with "bestmove ", strip and take the first token after it
        if (s.rfind("bestmove ", 0) == 0 && s.size() > 9) {
            s = s.substr(9);
            auto space = s.find(' ');
            if (space != std::string::npos) s = s.substr(0, space);
        }

        // Raw UCI format: from(2) + to(2) + optional promo(1)
        if (s.size() == 4 || s.size() == 5) {
            auto file = [](char c) { return c >= 'a' && c <= 'h'; };
            auto rank = [](char c) { return c >= '1' && c <= '8'; };
            if (file(s[0]) && rank(s[1]) && file(s[2]) && rank(s[3])) {
                if (s.size() == 5) {
                    char p = s[4];
                    // normalize to lowercase
                    if (p >= 'A' && p <= 'Z') p = char(p - 'A' + 'a');
                    if (!(p == 'q' || p == 'r' || p == 'b' || p == 'n')) return false;
                    s[4] = p;
                }
                outMove = s;
                return true;
            }
        }
        return false;
    }

    std::optional<sf::IpAddress> parseIpv4(const std::string& s) {
        unsigned int a, b, c, d;
        char trailing;
        // Ensure only exactly a.b.c.d (no extra chars)
        if (sscanf_s(s.c_str(), "%u.%u.%u.%u%c", &a, &b, &c, &d, &trailing, 1) == 4) {
            if (a < 256 && b < 256 && c < 256 && d < 256) {
                return sf::IpAddress(a, b, c, d); // uses 4-integer ctor
            }
        }
        return std::nullopt;
    }
}

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
        searchTimeout = std::chrono::seconds(30); // 30 seconds search
    }
    else {
        searchTimeout = std::chrono::seconds(10); // 2 minute search
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
    } else if (st == sf::Socket::Status::Disconnected) {
        std::cout << "Client disconnected during send\n";
        clientConnected = false;
        if (!isBotMatch.load(std::memory_order_acquire)) {
            gameOver = true;
        }
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
    } else if (st == sf::Socket::Status::Disconnected) {
        std::cout << "Host disconnected during send\n";
        clientConnected = false;
        if (!isBotMatch.load(std::memory_order_acquire)) {
            gameOver = true;
        }
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
        if (!isBotMatch.load(std::memory_order_acquire)) {
            gameOver = true;
        }
        return -1;
    }
    return 0;
}

int networkManager::receiveFromHost(json& outMessage) {
    if (isNetworkHost || !clientConnected) return 0;

    for (;;) {
        sf::Packet packet;
        sf::Socket::Status status = clientSocket.receive(packet);

        if (status == sf::Socket::Status::Done) {
            std::string payload;
            if (!(packet >> payload)) {
                continue;
            }

            // 1) Try JSON first
            json parsed = json::parse(payload, nullptr, false);
            if (!parsed.is_discarded()) {
                // Normalize move if present in structured JSON
                std::string move;
                auto typeIt = parsed.find("type");
                if (typeIt != parsed.end() && typeIt->is_string()) {
                    const std::string t = *typeIt;
                    if (t == "move") {
                        if (parsed.contains("uci") && parsed["uci"].is_string() &&
                            tryExtractUCIMove(parsed["uci"].get<std::string>(), move)) {
                            outMessage = json{ {"type","move"}, {"uci", move} };
                            std::cout << "Received from host: " << outMessage.dump() << "\n";
                            return 1;
                        }
                    }
                    else if (t == "redirect") {
                        std::cout << "connection refused, forwarded to different server"<< "\n";
                        clientSocket.setBlocking(true);
                        const std::string hostStr = parsed["host"].get<std::string>();
                        auto ipOpt = parseIpv4(hostStr);
                        if (!ipOpt) {
                            std::cout << "Invalid IPv4 address in redirect: " << hostStr << "\n";
                            return 0;
                        }
                        sf::IpAddress ip = *ipOpt;
                        sf::Socket::Status connectStatus = clientSocket.connect(ip, parsed["port"], sf::milliseconds(2000));

                        if (connectStatus == sf::Socket::Status::Done) {
                            isNetworkHost.store(false, std::memory_order_release);
                            clientConnected = true;
                            clientSocket.setBlocking(false);
                            std::cout << "Successfully rerouted to server: " << ip << "port: " << parsed["port"] << "\n";
                            return true;
                        }
                        else {
                            std::cout << "Found server but failed to connect (status=" << static_cast<int>(connectStatus) << ")\n";
                        }
                    }
                    else if (t == "bestmove") {
                        // Accept {"type":"bestmove","uci":"e2e4"} or {"type":"bestmove","move":"e2e4"}
                        const std::string key = parsed.contains("uci") ? "uci" : (parsed.contains("move") ? "move" : "");
                        if (!key.empty() && parsed[key].is_string() &&
                            tryExtractUCIMove(parsed[key].get<std::string>(), move)) {
                            outMessage = json{ {"type","move"}, {"uci", move} };
                            std::cout << "Received from host: " << outMessage.dump() << "\n";
                            return 1;
                        }
                    }
                    else if (t == "engine_stats") {
                        // Some engines emit bestmove as a string line
                        if (parsed.contains("string") && parsed["string"].is_string() &&
                            tryExtractUCIMove(parsed["string"].get<std::string>(), move)) {
                            outMessage = json{ {"type","move"}, {"uci", move} };
                            std::cout << "Received from host: " << outMessage.dump() << "\n";
                            return 1;
                        }

                        // Ignore lightweight info strings with no PV/score/move
                        const bool isJustInfoString =
                            parsed.contains("string") && !parsed.contains("pv") && !parsed.contains("score");
                        if (isJustInfoString) {
                            continue;
                        }
                    }
                }

                // No move found but valid JSON: return it (keeps previous behavior)
                outMessage = parsed;
                std::cout << "Received from host: " << payload << "\n";
                return 1;
            }

            // 2) Not JSON: try to parse raw UCI or "bestmove e2e4"
            std::string move;
            if (tryExtractUCIMove(payload, move)) {
                outMessage = json{ {"type","move"}, {"uci", move} };
                std::cout << "Received from host: " << outMessage.dump() << "\n";
                return 1;
            }

            // Unknown non-JSON payload; ignore and keep draining
            continue;
        }
        else if (status == sf::Socket::Status::Disconnected) {
            std::cout << "Host disconnected\n";
            clientConnected = false;
            if (!isBotMatch.load(std::memory_order_acquire)) {
                gameOver = true;
            }
            return -1;
        }
        else {
            return 0; // NotReady/Error: nothing more right now
        }
    }
}