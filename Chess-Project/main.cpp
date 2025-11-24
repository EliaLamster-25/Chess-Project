#include "main.hpp"
#include "assets/chessPieces/blackBishop.h"
#include "Bishop.hpp"
#include "stockfish.hpp"
#include "engineSelection.hpp"
#include <limits>
#include <string>
#include <random>

enum class State { Menu, engineSelection, Game_Singleplayer, Game_Multiplayer, Game_BotMatch, Game_Bot_vs_Bot };

bool stockfishActive = false;
bool aifirstmove;
bool gameOver = false; // use the global (removed local shadow)

ChessPiece* selectedPiece = nullptr;

constexpr int kEngineMoveTimeMs = 50;
constexpr float kSlideAnimSeconds = 0.20f;

State currentState;
namespace {
    std::vector<std::unique_ptr<ChessPiece>> pieces;
}

static json lastEngineStats;
static json compareLastEngineStats;

static sf::Clock inputSuppressClock;
static bool suppressMouseUntil = false;
static constexpr int kSuppressMs = 200; // milliseconds to ignore mouse clicks after returning to menu

std::vector<std::unique_ptr<ChessPiece>>& getPieces() { return pieces; }
const std::vector<std::unique_ptr<ChessPiece>>& getPiecesConst() { return pieces; }

static int mouseToLogicalSquare(const sf::Vector2f& mouseF,
    const sf::Vector2u& boardSize,
    int offsetX,
    bool flipPerspective)
{
    constexpr int kLabelPad = 28;
    const int rectH = static_cast<int>((static_cast<int>(boardSize.y) - 2 * kLabelPad) / 8);
    const int rectW = rectH;
    const float left = static_cast<float>(offsetX + kLabelPad);
    const float top = static_cast<float>(kLabelPad);
    const float right = left + 8.f * static_cast<float>(rectW);
    const float bottom = top + 8.f * static_cast<float>(rectH);
    if (mouseF.x < left || mouseF.x > right || mouseF.y < top || mouseF.y > bottom)
        return -1;
    const int fileFromLeft = std::clamp(static_cast<int>((mouseF.x - left) / rectW), 0, 7);
    const int rankFromTop = std::clamp(static_cast<int>((mouseF.y - top) / rectH), 0, 7);
    const int logicalFile = flipPerspective ? (7 - fileFromLeft) : fileFromLeft;
    const int logicalRank = flipPerspective ? rankFromTop : (7 - rankFromTop);
    return logicalRank * 8 + logicalFile;
}

static bool anyPieceSliding() {
    for (auto& up : pieces) {
        if (up && up->isSliding()) return true;
    }
    return false;
}

static std::optional<PieceType> pieceTypeFromString(std::string_view s) {
    std::string t(s);
    std::transform(t.begin(), t.end(), t.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (t == "pawn")  return PieceType::Pawn;
    if (t == "knight")return PieceType::Knight;
    if (t == "bishop")return PieceType::Bishop;
    if (t == "rook")  return PieceType::Rook;
    if (t == "queen") return PieceType::Queen;
    if (t == "king")  return PieceType::King;
    return std::nullopt;
}

static std::optional<std::pair<PieceType, bool>> parsePieceLabel(std::string_view label) {
    std::string cleaned(label);
    cleaned.erase(std::remove(cleaned.begin(), cleaned.end(), ','), cleaned.end());
    std::istringstream iss(cleaned);

    std::string typeStr, colorStr;
    if (!(iss >> typeStr >> colorStr)) return std::nullopt;

    auto pt = pieceTypeFromString(typeStr);
    if (!pt) return std::nullopt;

    std::transform(colorStr.begin(), colorStr.end(), colorStr.begin(), ::tolower);
    if (colorStr != "white" && colorStr != "black") return std::nullopt;

    bool isWhite = (colorStr == "white");
    return std::make_pair(*pt, isWhite);
}

static inline int flipSquareHoriz(int sq) { return (sq >= 0 && sq < 64) ? (sq ^ 7) : sq; }

static void flipBoardStateHoriz(std::vector<int>& board) {
    if (board.size() != 64) return;
    std::vector<int> flipped(64, 0);
    for (int sq = 0; sq < 64; ++sq) {
        const int fsq = flipSquareHoriz(sq);
        flipped[fsq] = board[sq];
    }
    board.swap(flipped);
}

static inline int flipSquareVert(int sq) {
    if (sq < 0 || sq >= 64) return sq;
    const int file = sq % 8;
    const int rank = sq / 8;
    return (7 - rank) * 8 + file;
}

static void flipPiecesHoriz(std::vector<std::unique_ptr<ChessPiece>>& piecesRef) {
    for (auto& up : piecesRef) {
        if (!up) continue;
        const int sq = up->getSquare();
        if (sq >= 0 && sq < 64) {
            up->setSquare(flipSquareHoriz(sq));
        }
        const int cap = up->getCapturedSquare();
        if (cap >= 0 && cap < 64) {
            up->setCapturedSquare(flipSquareHoriz(cap));
        }
    }
    if (ChessPiece::enPassantTargetSquare >= 0 && ChessPiece::enPassantTargetSquare < 64) {
        ChessPiece::enPassantTargetSquare = flipSquareHoriz(ChessPiece::enPassantTargetSquare);
    }
}

static std::string flipUciHoriz(const std::string& uci) {
    if (uci.size() < 4) return uci;
    auto flipFileChar = [](char f) -> char {
        if (f < 'a' || f > 'h') return f;
        return static_cast<char>('a' + (7 - (f - 'a')));
    };
    std::string r = uci;
    r[0] = flipFileChar(static_cast<char>(std::tolower(static_cast<unsigned char>(r[0]))));
    r[2] = flipFileChar(static_cast<char>(std::tolower(static_cast<unsigned char>(r[2]))));
    return r;
}

static inline std::uint64_t flipBitboardHoriz(std::uint64_t bb) {
    const std::uint64_t k1 = 0x5555555555555555ULL;
    const std::uint64_t k2 = 0x3333333333333333ULL;
    const std::uint64_t k4 = 0x0f0f0f0f0f0f0f0fULL;
    bb = ((bb >> 1) & k1) | ((bb & k1) << 1);
    bb = ((bb >> 2) & k2) | ((bb & k2) << 2);
    bb = ((bb >> 4) & k4) | ((bb & k4) << 4);
    return bb;
}

static void flipPositionHoriz(std::vector<std::unique_ptr<ChessPiece>>& piecesRef,
    std::vector<int>& boardStateRef) {
    flipPiecesHoriz(piecesRef);
    flipBoardStateHoriz(boardStateRef);
    selectedPiece = nullptr;
}

static ChessPiece* findPiece(const std::vector<ChessPiece*>& pieces, PieceType type, bool isWhite, std::optional<int> atSquare = std::nullopt) {
    for (ChessPiece* p : pieces) {
        if (!p) continue;
        if (p->getSquare() == -1) continue;
        if (p->getType() == type && p->isWhite() == isWhite) {
            if (!atSquare || p->getSquare() == *atSquare) {
                return p;
            }
        }
    }
    return nullptr;
}

static ChessPiece* getPieceByLabel(const std::vector<ChessPiece*>& pieces, std::string_view label) {
    auto parsed = parsePieceLabel(label);
    if (!parsed) return nullptr;
    return findPiece(pieces, parsed->first, parsed->second);
}

std::vector<int> getPath(int from, int to) {
    std::vector<int> path;
    if (from < 0 || from > 63 || to < 0 || to > 63) return path;
    int fromFile = from % 8;
    int fromRank = from / 8;
    int toFile = to % 8;
    int toRank = to / 8;
    int dFile = (toFile > fromFile) ? 1 : (toFile < fromFile) ? -1 : 0;
    int dRank = (toRank > fromRank) ? 1 : (toRank < fromRank) ? -1 : 0;
    int file = fromFile + dFile;
    int rank = fromRank + dRank;
    while (file != toFile || rank != toRank) {
        int sq = rank * 8 + file;
        if (sq < 0 || sq > 63) break;
        path.push_back(sq);
        file += dFile;
        rank += dRank;
    }
    if (to >= 0 && to <= 63) path.push_back(to);
    return path;
}

static std::string squareToAlgebraic(int sq) {
    if (sq < 0 || sq > 63) return "";
    int file = sq % 8;
    int rank = sq / 8;
    char f = static_cast<char>('a' + file);
    char r = static_cast<char>('1' + rank);
    return std::string() + f + r;
}

static bool parseUciMove(const std::string& uci, int& fromSq, int& toSq, char& promo) {
    if (uci.size() < 4) return false;
    auto toSqIndex = [](char f, char r) -> int {
        if (f < 'a' || f > 'h' || r < '1' || r > '8') return -1;
        int file = f - 'a';
        int rank = r - '1';
        return rank * 8 + file;
    };
    fromSq = toSqIndex(uci[0], uci[1]);
    toSq = toSqIndex(uci[2], uci[3]);
    promo = (uci.size() >= 5) ? static_cast<char>(std::tolower(static_cast<unsigned char>(uci[4]))) : 0;
    return fromSq >= 0 && toSq >= 0;
}

std::string getBotUciMove(const std::vector<std::unique_ptr<ChessPiece>>& pieces) {
    std::vector<int> boardState(64, 0);
    std::vector<ChessPiece*> piecePtrs;
    for (const auto& up : pieces) {
        int sq = up->getSquare();
        if (sq >= 0 && sq < 64) {
            boardState[sq] = up->isWhite() ? 1 : -1;
            piecePtrs.push_back(up.get());
        }
    }
    std::vector<std::pair<int, int>> allMoves;
    for (auto* piece : piecePtrs) {
        if (piece->isWhite()) continue;
        auto possibleMoves = piece->getPossibleMoves(boardState);
        for (int toSq : possibleMoves) {
            int fromSq = piece->getSquare();
            ChessPiece* captured = nullptr;
            int capOldSq = -1;
            bool isEnPassant = (piece->getType() == PieceType::Pawn && toSq == ChessPiece::enPassantTargetSquare && boardState[toSq] == 0);
            if (isEnPassant) {
                int victimSq = toSq + 8;
                for (auto* p : piecePtrs) {
                    if (p->getSquare() == victimSq && p->isWhite()) {
                        captured = p;
                        capOldSq = victimSq;
                        break;
                    }
                }
            }
            else if (boardState[toSq] != 0 && boardState[toSq] == 1) {
                for (auto* p : piecePtrs) {
                    if (p->getSquare() == toSq) {
                        captured = p;
                        capOldSq = toSq;
                        break;
                    }
                }
            }
            piece->setSquare(toSq);
            if (captured) captured->setSquare(-1);

            std::vector<int> simBoard(64, 0);
            std::vector<ChessPiece*> simPtrs;
            for (const auto& up : pieces) {
                int s = up->getSquare();
                if (s >= 0 && s < 64) {
                    simBoard[s] = up->isWhite() ? 1 : -1;
                    simPtrs.push_back(up.get());
                }
            }
            bool inCheck = CheckCheckmate::isInCheck(simBoard, simPtrs, false);
            piece->setSquare(fromSq);
            if (captured) captured->setSquare(capOldSq);
            if (!inCheck) {
                allMoves.emplace_back(fromSq, toSq);
            }
        }
    }
    if (allMoves.empty()) {
        return "";
    }
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, static_cast<int>(allMoves.size()) - 1);
    auto [fromSq, toSq] = allMoves[dis(gen)];
    std::string promo = "";
    if (toRank(toSq) == 7 && findPiece(piecePtrs, PieceType::Pawn, false, fromSq)) {
        if (toRank(toSq) == 0) {
            promo = "q";
        }
    }
    return squareToAlgebraic(fromSq) + squareToAlgebraic(toSq) + promo;
}

int received_startSquare = -1;
int received_endSquare = -1;
int sent_startSquare = -1;
int sent_endSquare = -1;

bool commandMove = false;

void setState(State state) { currentState = state; }

static bool applyUciMoveLocal(const std::string& uci, std::vector<std::unique_ptr<ChessPiece>>& pieces) {
    int fromSq = -1, toSq = -1; char promo = 0;
    if (!parseUciMove(uci, fromSq, toSq, promo)) return false;
    ChessPiece* mover = nullptr;
    for (auto& up : pieces) {
        if (up->getSquare() == fromSq && up->isWhite() == ChessPiece::whiteTurn) {
            mover = up.get();
            break;
        }
    }
    if (!mover) {
        std::cerr << "[AI] No piece at fromSq=" << fromSq << " for side "
            << (ChessPiece::whiteTurn ? "white" : "black") << "\n";
        return false;
    }
    if (mover->getType() == PieceType::King &&
        toRank(toSq) == toRank(fromSq) &&
        std::abs(toFile(toSq) - toFile(fromSq)) == 2) {
        bool kingSide = toFile(toSq) > toFile(fromSq);
        int rnk = toRank(fromSq);
        int rookFrom = toSquare(kingSide ? 7 : 0, rnk);
        int rookTo = toSquare(kingSide ? 5 : 3, rnk);
        for (auto& up : pieces) {
            if (up->getSquare() == rookFrom &&
                up->isWhite() == mover->isWhite() &&
                up->getType() == PieceType::Rook) {
                up->beginSlide(rookFrom, rookTo, kSlideAnimSeconds);
                up->setSquare(rookTo);
                up->setHasMoved(true);
                break;
            }
        }
    }
    ChessPiece* targetEnemy = nullptr;
    if (mover->getType() == PieceType::Pawn &&
        toSq == ChessPiece::enPassantTargetSquare) {
        int victimSq = toSq + (mover->isWhite() ? -8 : 8);
        for (auto& up : pieces) {
            if (up->getSquare() == victimSq &&
                up->isWhite() != mover->isWhite() &&
                up->getType() == PieceType::Pawn) {
                targetEnemy = up.get();
                break;
            }
        }
    }
    else {
        for (auto& up : pieces) {
            if (up->getSquare() == toSq &&
                up->isWhite() != mover->isWhite()) {
                targetEnemy = up.get();
                break;
            }
        }
    }
    if (targetEnemy) targetEnemy->setSquare(-1);
    mover->beginSlide(fromSq, toSq, kSlideAnimSeconds);
    mover->setSquare(toSq);
    mover->setHasMoved(true);
    ChessPiece::enPassantTargetSquare = -1;
    if (mover->getType() == PieceType::Pawn && std::abs(toSq - fromSq) == 16) {
        ChessPiece::enPassantTargetSquare = (fromSq + toSq) / 2;
    }
    ChessPiece::whiteTurn = !ChessPiece::whiteTurn;
    return true;
}

botOverlay engineStats;

bool handleEngineStats(sf::RenderWindow& window, sf::Vector2u size, json data, networkManager& net) {
    (void)window; (void)size; (void)net;
    if (currentState == State::Game_BotMatch || stockfishActive) {
        if (data.is_object()) {
            lastEngineStats = data;
            return true;
        }
    }
    return false;
}

std::atomic<bool> running{ true };

enum class ReplyRoute { None, ToClient, ToHost };
static ReplyRoute engineReplyRoute = ReplyRoute::None;
static bool engineReplyScheduled = false;
bool stockfishTurn;

// Added helper: game-over button rectangle calculation
static sf::FloatRect computeGameOverButtonRect(const sf::Vector2u& boardSize) {
    float bw = 320.f;
    float bh = 80.f;
    float centerX = static_cast<float>(boardSize.x) / 2.f + 63.f;
    float centerY = static_cast<float>(boardSize.y) / 2.f + 63.f + 120.f; // below "Game Over"
    return sf::FloatRect(
        sf::Vector2f(centerX - bw / 2.f, centerY - bh / 2.f),
        sf::Vector2f(bw, bh)
    );
}

// New helper: evaluate board for check/checkmate and set gameOver
static void evaluateGameOver() {
    std::vector<int> boardState(64, 0);
    std::vector<ChessPiece*> piecePtrs;
    piecePtrs.reserve(pieces.size());
    for (const auto& up : pieces) {
        int s = up->getSquare();
        if (s >= 0 && s < 64) {
            boardState[s] = up->isWhite() ? 1 : -1;
            piecePtrs.push_back(up.get());
        }
    }
    bool sideInCheck = CheckCheckmate::isInCheck(boardState, piecePtrs, ChessPiece::whiteTurn);
    auto escapes = CheckCheckmate::getEscapeMoves(boardState, piecePtrs, ChessPiece::whiteTurn);
    if (sideInCheck) {
        std::cout << (ChessPiece::whiteTurn ? "White" : "Black") << " is in CHECK!\n";
    }
    if (sideInCheck && escapes.empty()) {
        std::cout << "CHECKMATE! " << (ChessPiece::whiteTurn ? "White" : "Black") << " loses.\n";
        gameOver = true;
    }
}

int main()
{
    setState(State::Menu);
    sf::VideoMode desktopMode = sf::VideoMode::getDesktopMode();
    sf::Vector2f windowSize(static_cast<float>(desktopMode.size.x), static_cast<float>(desktopMode.size.y));
    sf::ContextSettings contextSettings;
    contextSettings.antiAliasingLevel = 8;
    contextSettings.depthBits = 24;
    sf::RenderWindow window(sf::VideoMode(desktopMode.size), "Chess", sf::Style::None, sf::State::Fullscreen, contextSettings);

    Menu menu(window, sf::Vector2f(desktopMode.size));
    engineSelection engine_selection(window, sf::Vector2f(desktopMode.size));
    ChessBoard chess;

    std::vector<ChessPiece*> piecePtrs;
    sf::Vector2f dragOffset;

    textures textureManager;
    textureManager.loadTextures(pieces);

    StockfishEngine engine(L"C:/Users/elamster/Downloads/stockfish-windows-x86-64-avx2/stockfish/stockfish.exe");
    aifirstmove = true;
    ChessPiece::whiteTurn = true;
    ChessPiece::enPassantTargetSquare = -1;

    int offsetX = desktopMode.size.x / 4;
    sf::Vector2u boardSize = desktopMode.size;
    float cellSize = static_cast<float>(boardSize.y) / 8.f;

    std::vector<int> moves;
    std::vector<int> captureMoves;

    static sf::Font font;
    static bool fontReady = false;
    if (!fontReady) {
        if (!font.openFromMemory(segoeuithibd_ttf, segoeuithibd_ttf_len)) {
            std::cerr << "Failed to load Segoe UI font from memory\n";
            fontReady = false;
        }
        else {
            fontReady = true;
        }
    }

    networkManager networkManager;
    std::thread networkThread;
    bool networkThreadStarted = false;

    flipPiecesHoriz(pieces);

    sf::FloatRect btnRect = computeGameOverButtonRect(boardSize);
    sf::RoundedRectangleShape backToMenuButton(sf::Vector2f(btnRect.size.x, btnRect.size.y), 20.f, 12);

    // Lambda to reset game and go back to menu
    auto resetAndReturnToMenu = [&]() {
        // Clear active pieces and reload
        pieces.clear();
        textureManager.loadTextures(pieces);
        piecePtrs.clear();
        selectedPiece = nullptr;
        moves.clear();
        captureMoves.clear();
        ChessPiece::whiteTurn = true;
        ChessPiece::enPassantTargetSquare = -1;
        aifirstmove = true;
        lastEngineStats = json();
        compareLastEngineStats = json();
        engine.reset();
        gameOver = false;
        setState(State::Menu);
        };

    while (window.isOpen())
    {
        while (true) {
            if (currentState == State::Menu) {
                while (auto eventOpt = window.pollEvent()) {
                    const sf::Event& event = *eventOpt;
                    if (event.is<sf::Event::Closed>()) {
                        window.close();
                    }

                    if (suppressMouseUntil) {
                        if (inputSuppressClock.getElapsedTime().asMilliseconds() >= kSuppressMs) {
                            suppressMouseUntil = false;
                        }
                        else {
                            // drop mouse button events (press/release) but allow non-mouse events to still be processed
                            if (event.is<sf::Event::MouseButtonPressed>() || event.is<sf::Event::MouseButtonReleased>()) {
                                continue;
                            }
                        }
                    }

                    std::string action = menu.mouseEvent(event);
                    if (action == "singleplayer") {
                        setState(State::Game_Singleplayer);
                        isNetworkHost = true;
                        continue;
                    }
                    if (action == "multiplayer") {
                        setState(State::Game_Multiplayer);
                        if (!networkThreadStarted) {
                            networkThreadStarted = true;
                            networkThread = std::thread([&]() {
                                networkManager.start("multiplayer");
                                });
                        }
                        continue;
                    }
                    if (action == "botMatch") {
                        setState(State::engineSelection);
                        if (!networkThreadStarted) {
                            networkThreadStarted = true;
                            networkThread = std::thread([&]() {
                                networkManager.start("botMatch");
                                });
                        }
                        isNetworkHost = false;
                        continue;
                    }
                    if (action == "botVsBot") {
                        setState(State::engineSelection);
						stockfishActive = true;
                        if (!networkThreadStarted) {
                            networkThreadStarted = true;
                            networkThread = std::thread([&]() {
                                networkManager.start("botMatch");
                                });
                        }
                        isNetworkHost = false;
                        engine.initialize();
                        continue;
                    }
                }
                window.clear();
                chess.draw(window, boardSize, false);
                menu.draw(window);
                window.display();
                sf::sleep(sf::milliseconds(1));
                continue;
            }
            if (currentState == State::engineSelection) {
                json received_data;
                auto status = networkManager.receiveFromHost(received_data);
                if (status == 1) {
                    if (received_data.is_string()) {
						std::cout << "data: " << received_data.get<std::string>() << "\n"; 
                    }
                }


                while (auto eventOpt = window.pollEvent()) {
                    const sf::Event& event = *eventOpt;
                    if (event.is<sf::Event::Closed>()) {
                        window.close();
                    }
                    std::string action = engine_selection.mouseEvent(event);
                    if (action == "bot") {
                        if (!networkThreadStarted) {
                            networkThreadStarted = true;
                            networkThread = std::thread([&]() {
                                networkManager.start("botMatch");
                                });
                        }
                        isNetworkHost = false;
						json message;
						message["type"] = "engineSelection";
						message["engine"] = "bot";
                        networkManager.sendToHost(message);
                        if (networkManager.isConnected()) {
                            setState(State::Game_BotMatch);
						}
                        continue;
                    }
                    if (action == "stockfish") {
                        if (!networkThreadStarted) {
                            networkThreadStarted = true;
                            networkThread = std::thread([&]() {
                                networkManager.start("botMatch");
                                });
                        }
                        isNetworkHost = false;
                        json message;
                        message["type"] = "engineSelection";
                        message["engine"] = "stockfish";
                        networkManager.sendToHost(message);
                        if (networkManager.isConnected()) {
                            setState(State::Game_BotMatch);
                        }
                        continue;
                    }
                    if (action == "lc0") {
                        if (!networkThreadStarted) {
                            networkThreadStarted = true;
                            networkThread = std::thread([&]() {
                                networkManager.start("botMatch");
                                });
                        }
                        isNetworkHost = false;
                        json message;
                        message["type"] = "engineSelection";
                        message["engine"] = "lc0";
                        networkManager.sendToHost(message);
                        if (networkManager.isConnected()) {
                            setState(State::Game_BotMatch);
                        }
                        continue;
                    }
                }
                window.clear();
                chess.draw(window, boardSize, false);
                engine_selection.draw(window);
                window.display();
                sf::sleep(sf::milliseconds(1));
                continue;
            }
            else if (currentState == State::Game_Singleplayer
                || currentState == State::Game_Multiplayer
                || currentState == State::Game_BotMatch
                || stockfishActive) {

                window.clear();
                const bool flipPerspective = (currentState == State::Game_Multiplayer) && !isNetworkHost;

                if (stockfishActive && engineReplyScheduled && !gameOver && !anyPieceSliding()) {
                    std::string reply = engine.getNextMove(kEngineMoveTimeMs);
                    std::cout << "Stockfish reply (deferred): " << reply << "\n";
                    if (!reply.empty() && reply != "(none)") {
                        if (applyUciMoveLocal(reply, pieces)) {
                            // Evaluate game-over after engine move
                            evaluateGameOver();

                            if (engineReplyRoute == ReplyRoute::ToClient) {
                                networkManager.sendToClient(reply);
                            }
                            else if (engineReplyRoute == ReplyRoute::ToHost) {
                                networkManager.sendToHost(reply);
                            }
                        }
                        else {
                            std::cerr << "[AI] Failed to apply deferred reply: " << reply << "\n";
                        }
                    }
                    selectedPiece = nullptr;
                    engineReplyScheduled = false;
                    engineReplyRoute = ReplyRoute::None;
                }
                else if (currentState == State::Game_Multiplayer || currentState == State::Game_BotMatch || stockfishActive) {
                    if (aifirstmove) {
                        if (!ChessPiece::whiteTurn) ChessPiece::whiteTurn = true;
                        if (stockfishActive) {
                            engine.reset();
                            std::string aiMove = engine.getNextMove(kEngineMoveTimeMs);
                            std::cout << "Stockfish suggests move: " << aiMove << "\n";
                            bool applied = false;
                            if (!aiMove.empty() && aiMove != "(none)") {
                                applied = applyUciMoveLocal(aiMove, pieces);
                            }
                            if (applied) {
                                // Evaluate after engine opening move
                                evaluateGameOver();

                                networkManager.sendToHost(aiMove);
                                selectedPiece = nullptr;
                                aifirstmove = false;
                            }
                            else {
                                std::cerr << "[AI] Failed to apply opening move: " << aiMove << "\n";
                            }
                        }
                    }
                    json received_data;
                    networkManager.update();
                    if (isNetworkHost)
                    {
                        auto status = networkManager.receiveFromClient(received_data);
                        if (status == 1) {
                            if (received_data.is_string()) {
                                const std::string uci = received_data.get<std::string>();
                                if (!applyUciMoveLocal(uci, pieces)) {
                                    std::cerr << "Failed to apply client move: \"" << uci << "\"\n";
                                }
                                else {
                                    // Evaluate any game-end after applying client's move
                                    evaluateGameOver();

                                    if (stockfishActive) {
                                        engine.opponentMove(uci);
                                        if (!gameOver) {
                                            engineReplyScheduled = true;
                                            engineReplyRoute = ReplyRoute::ToClient;
                                        }
                                    }
                                }
                                selectedPiece = nullptr;
                            }
                            else if (received_data.is_object()) {
                                const auto itStart = received_data.find("start_square");
                                const auto itEnd = received_data.find("end_square");
                                if (itStart != received_data.end() && itEnd != received_data.end()
                                    && itStart->is_number_integer() && itEnd->is_number_integer()) {
                                    const int fs = itStart->get<int>();
                                    const int ts = itEnd->get<int>();
                                    const std::string uci = squareToString(fs) + squareToString(ts);
                                    if (!applyUciMoveLocal(uci, pieces)) {
                                        std::cerr << "Failed to apply client move (legacy JSON): " << uci << "\n";
                                    }
                                    else {
                                        // Evaluate after applying client legacy move
                                        evaluateGameOver();

                                        if (stockfishActive) {
                                            engine.opponentMove(uci);
                                            if (!gameOver) {
                                                engineReplyScheduled = true;
                                                engineReplyRoute = ReplyRoute::ToClient;
                                            }
                                        }
                                    }
                                    selectedPiece = nullptr;
                                }
                                else {
                                    std::cerr << "Malformed legacy move packet from client: " << received_data.dump() << "\n";
                                }
                            }
                            else {
                                std::cerr << "Malformed move packet from client (type=" << received_data.type_name() << "): " << received_data.dump() << "\n";
                            }
                        }
                    }
                    else
                    {
                        auto status = networkManager.receiveFromHost(received_data);
                        if (status == 1) {
                            if (!handleEngineStats(window, boardSize, received_data, networkManager)) {
                                if (received_data.is_string()) {
                                    const std::string uci = received_data.get<std::string>();
                                    std::cout << "[server->client] UCI: " << uci << "\n";
                                    if (!applyUciMoveLocal(uci, pieces)) {
                                        std::cerr << "Failed to apply host move: \"" << uci << "\"\n";
                                    }
                                    else {
                                        // Evaluate after applying host move
                                        evaluateGameOver();

                                        if (stockfishActive) {
                                            engine.opponentMove(uci);
                                            if (!gameOver) {
                                                engineReplyScheduled = true;
                                                engineReplyRoute = ReplyRoute::ToHost;
                                            }
                                        }
                                    }
                                    selectedPiece = nullptr;
                                }
                                else if (received_data.is_object()) {
                                    const auto itStart = received_data.find("start_square");
                                    const auto itEnd = received_data.find("end_square");
                                    if (itStart != received_data.end() && itEnd != received_data.end()
                                        && itStart->is_number_integer() && itEnd->is_number_integer()) {
                                        const int fs = itStart->get<int>();
                                        const int ts = itEnd->get<int>();
                                        const std::string uci = squareToString(fs) + squareToString(ts);
                                        if (!applyUciMoveLocal(uci, pieces)) {
                                            std::cerr << "Failed to apply host move (legacy JSON): " << uci << "\n";
                                        }
                                        else {
                                            // Evaluate after applying host legacy move
                                            evaluateGameOver();

                                            if (stockfishActive) {
                                                engine.opponentMove(uci);
                                                if (!gameOver) {
                                                    engineReplyScheduled = true;
                                                    engineReplyRoute = ReplyRoute::ToHost;
                                                }
                                            }
                                        }
                                        selectedPiece = nullptr;
                                    }
                                    else {
                                        std::cerr << "Malformed legacy move packet from host: " << received_data.dump() << "\n";
                                    }
                                }
                                else {
                                    std::cerr << "Malformed move packet from host (type=" << received_data.type_name() << "): " << received_data.dump() << "\n";
                                }
                            }
                        }
                    }
                }

                std::vector<int> boardState(64, 0);
                piecePtrs.clear();
                piecePtrs.reserve(pieces.size());
                for (auto& up : pieces) {
                    int sq = up->getSquare();
                    if (sq >= 0 && sq < 64) boardState[sq] = up->isWhite() ? 1 : -1;
                    if (sq >= 0 && sq < 64) piecePtrs.push_back(up.get());
                }

                bool inCheck = CheckCheckmate::isInCheck(boardState, piecePtrs, ChessPiece::whiteTurn);
                std::vector<std::pair<int, int>> escapeMoves;
                std::unordered_set<int> allowedPieces;
                int endangeredKingSquare = -1;

                if (inCheck) {
                    for (auto* p : piecePtrs) {
                        if (p->isWhite() == ChessPiece::whiteTurn && p->getType() == PieceType::King) {
                            endangeredKingSquare = p->getSquare();
                            break;
                        }
                    }
                    std::vector<int> simBoard = boardState;
                    std::vector<ChessPiece*> simPtrs = piecePtrs;
                    for (auto* piece : piecePtrs) {
                        if (piece->isWhite() != ChessPiece::whiteTurn) continue;
                        auto legalMoves = piece->getPossibleMoves(boardState);
                        int originalFrom = piece->getSquare();
                        for (int move : legalMoves) {
                            ChessPiece* capturedPiece = nullptr;
                            int capturedOrigSquare = -1;
                            for (auto& up : pieces) {
                                ChessPiece* maybe = up.get();
                                if (maybe->getSquare() == move && maybe->isWhite() != piece->isWhite()) {
                                    capturedPiece = maybe;
                                    capturedOrigSquare = maybe->getSquare();
                                    break;
                                }
                            }
                            if (!capturedPiece &&
                                piece->getType() == PieceType::Pawn &&
                                move == ChessPiece::enPassantTargetSquare &&
                                boardState[move] == 0) {
                                int victimSq = move + (piece->isWhite() ? -8 : 8);
                                for (auto& up : pieces) {
                                    ChessPiece* maybe = up.get();
                                    if (maybe->getSquare() == victimSq &&
                                        maybe->isWhite() != piece->isWhite() &&
                                        maybe->getType() == PieceType::Pawn) {
                                        capturedPiece = maybe;
                                        capturedOrigSquare = maybe->getSquare();
                                        break;
                                    }
                                }
                            }
                            simBoard[originalFrom] = 0;
                            piece->setSquare(move);
                            simBoard[move] = piece->isWhite() ? 1 : -1;
                            if (capturedPiece) {
                                simBoard[capturedOrigSquare] = 0;
                                capturedPiece->setSquare(-1);
                            }
                            if (capturedPiece) {
                                auto it = std::find(simPtrs.begin(), simPtrs.end(), capturedPiece);
                                if (it != simPtrs.end()) simPtrs.erase(it);
                            }
                            bool stillInCheck = CheckCheckmate::isInCheck(simBoard, simPtrs, ChessPiece::whiteTurn);
                            piece->setSquare(originalFrom);
                            if (capturedPiece) capturedPiece->setSquare(capturedOrigSquare);
                            if (!stillInCheck) {
                                escapeMoves.emplace_back(originalFrom, move);
                                allowedPieces.insert(originalFrom);
                            }
                            simBoard[move] = 0;
                            piece->setSquare(originalFrom);
                            simBoard[originalFrom] = piece->isWhite() ? 1 : -1;
                            if (capturedPiece) {
                                capturedPiece->setSquare(capturedOrigSquare);
                                simBoard[capturedOrigSquare] = capturedPiece->isWhite() ? 1 : -1;
                                if (std::find(simPtrs.begin(), simPtrs.end(), capturedPiece) == std::end(simPtrs))
                                    simPtrs.push_back(capturedPiece);
                            }
                        }
                    }
                }

                const bool isLocalSideWhite = (currentState == State::Game_BotMatch) ? true : isNetworkHost.load();

                while (auto eventOpt = window.pollEvent()) {
                    const sf::Event& event = *eventOpt;

                    if (event.is<sf::Event::Closed>()) {
                        window.close();
                    }

                    // Game over: allow button click
                    if (gameOver) {
                        if (event.is<sf::Event::MouseButtonPressed>()) {
                            auto mouse = event.getIf<sf::Event::MouseButtonPressed>();
                            if (mouse->button == sf::Mouse::Button::Left) {
                                sf::Vector2f mouseF = window.mapPixelToCoords({ mouse->position.x, mouse->position.y });
                                sf::FloatRect btnRect = computeGameOverButtonRect(boardSize);
                                if (btnRect.contains(mouseF)) {
									suppressMouseUntil = true;
                                    inputSuppressClock.restart();
                                    resetAndReturnToMenu();
                                    continue;
                                }
                            }
                        }
                        if (auto hover = event.getIf<sf::Event::MouseMoved>()) {
                            sf::Vector2f mouseF = window.mapPixelToCoords({ hover->position.x, hover->position.y });
                            sf::FloatRect btnRect = computeGameOverButtonRect(boardSize);
                            if (btnRect.contains(mouseF)) {
                                backToMenuButton.setFillColor(sf::Color(20, 20, 20, 220));
                            }
                        }
                        // Skip other input while game over
                        continue;
                    }

                    if (event.is<sf::Event::MouseButtonPressed>()) {
                        auto mouse = event.getIf<sf::Event::MouseButtonPressed>();
                        if (mouse->button == sf::Mouse::Button::Left) {
                            sf::Vector2f mouseF = window.mapPixelToCoords({ mouse->position.x, mouse->position.y });
                            moves.clear();
                            captureMoves.clear();
                            for (auto& up : pieces) {
                                if (up->isWhite() == ChessPiece::whiteTurn
                                    && up->containsPoint(mouseF)
                                    && ((currentState == State::Game_Singleplayer)
                                        ? true
                                        : (up->isWhite() == isLocalSideWhite))) {
                                    if (!allowedPieces.empty() && allowedPieces.count(up->getSquare()) == 0) {
                                        continue;
                                    }
                                    selectedPiece = up.get();
                                    dragOffset = mouseF - selectedPiece->getSprite().getPosition();
                                    constexpr int kLabelPad = 28;
                                    const float rectW = static_cast<float>((boardSize.y - 2 * kLabelPad) / 8);
                                    float baseScale = (rectW * 0.6f) / 100.f;
                                    if (selectedPiece->getType() == PieceType::Queen || selectedPiece->getType() == PieceType::King) {
                                        baseScale = (rectW * 0.7f) / 100.f;
                                    }
                                    const float targetShrink = baseScale * 0.88f;
                                    selectedPiece->startScaleAnimation(selectedPiece->currentScale, targetShrink, 0.05f);
                                    if (!escapeMoves.empty()) {
                                        moves.clear();
                                        for (auto& em : escapeMoves) {
                                            if (em.first == selectedPiece->getSquare()) moves.push_back(em.second);
                                        }
                                    }
                                    else {
                                        moves = selectedPiece->getPossibleMoves(boardState);
                                    }
                                    {
                                        const int fromSq = selectedPiece->getSquare();
                                        std::vector<int> filteredMoves;
                                        filteredMoves.reserve(moves.size());
                                        for (int mv : moves) {
                                            if (mv < 0 || mv >= 64) continue;
                                            if (selectedPiece->getType() == PieceType::King &&
                                                toRank(mv) == toRank(fromSq) &&
                                                std::abs(toFile(mv) - toFile(fromSq)) == 2) {
                                                bool currentlyInCheck = CheckCheckmate::isInCheck(boardState, piecePtrs, selectedPiece->isWhite());
                                                if (currentlyInCheck) {
                                                    continue;
                                                }
                                                bool kingSide = toFile(mv) > toFile(fromSq);
                                                int rnk = toRank(fromSq);
                                                int rookFrom = toSquare(kingSide ? 7 : 0, rnk);
                                                ChessPiece* rookPtr = nullptr;
                                                for (auto& up2 : pieces) {
                                                    if (up2->getSquare() == rookFrom &&
                                                        up2->isWhite() == selectedPiece->isWhite() &&
                                                        up2->getType() == PieceType::Rook) {
                                                        rookPtr = up2.get();
                                                        break;
                                                    }
                                                }
                                                if (!rookPtr || rookPtr->getHasMoved()) {
                                                    continue;
                                                }
                                                int stepFile = toFile(fromSq) + (kingSide ? 1 : -1);
                                                int stepSq = toSquare(stepFile, rnk);
                                                selectedPiece->setSquare(stepSq);
                                                std::vector<int> simBoardStep(64, 0);
                                                std::vector<ChessPiece*> simPtrsStep;
                                                for (auto& up2 : pieces) {
                                                    int s = up2->getSquare();
                                                    if (s >= 0 && s < 64) {
                                                        simBoardStep[s] = up2->isWhite() ? 1 : -1;
                                                        simPtrsStep.push_back(up2.get());
                                                    }
                                                }
                                                bool stepInCheck = CheckCheckmate::isInCheck(simBoardStep, simPtrsStep, selectedPiece->isWhite());
                                                selectedPiece->setSquare(fromSq);
                                                if (stepInCheck) {
                                                    continue;
                                                }
                                            }
                                            ChessPiece* capPtr = nullptr;
                                            int capOldSq = -1;
                                            for (auto& up2 : pieces) {
                                                if (up2->getSquare() == mv && up2->isWhite() != selectedPiece->isWhite()) {
                                                    capPtr = up2.get();
                                                    capOldSq = capPtr->getSquare();
                                                    break;
                                                }
                                            }
                                            if (!capPtr &&
                                                selectedPiece->getType() == PieceType::Pawn &&
                                                mv == ChessPiece::enPassantTargetSquare &&
                                                boardState[mv] == 0) {
                                                int victimSq = mv + (selectedPiece->isWhite() ? -8 : 8);
                                                for (auto& up2 : pieces) {
                                                    if (up2->getSquare() == victimSq &&
                                                        up2->isWhite() != selectedPiece->isWhite() &&
                                                        up2->getType() == PieceType::Pawn) {
                                                        capPtr = up2.get();
                                                        capOldSq = capPtr->getSquare();
                                                        break;
                                                    }
                                                }
                                            }
                                            selectedPiece->setSquare(mv);
                                            if (capPtr) capPtr->setSquare(-1);
                                            std::vector<int> simBoard(64, 0);
                                            std::vector<ChessPiece*> simPtrs;
                                            for (auto& up2 : pieces) {
                                                int s = up2->getSquare();
                                                if (s >= 0 && s < 64) {
                                                    simBoard[s] = up2->isWhite() ? 1 : -1;
                                                    simPtrs.push_back(up2.get());
                                                }
                                            }
                                            bool selfInCheck = CheckCheckmate::isInCheck(simBoard, simPtrs, selectedPiece->isWhite());
                                            selectedPiece->setSquare(fromSq);
                                            if (capPtr) capPtr->setSquare(capOldSq);
                                            if (!selfInCheck) filteredMoves.push_back(mv);
                                        }
                                        moves.swap(filteredMoves);
                                    }
                                    captureMoves.clear();
                                    for (int mv : moves) {
                                        bool isCap = false;
                                        if (mv >= 0 && mv < 64 && boardState[mv] != 0 &&
                                            boardState[mv] != (selectedPiece->isWhite() ? 1 : -1)) {
                                            isCap = true;
                                        }
                                        if (selectedPiece->getType() == PieceType::Pawn &&
                                            mv == ChessPiece::enPassantTargetSquare) {
                                            isCap = true;
                                        }
                                        if (isCap) captureMoves.push_back(mv);
                                    }
                                    break;
                                }
                            }
                        }
                    }
                    else if (event.is<sf::Event::MouseButtonReleased>() || commandMove) {
                        auto mouse = event.getIf<sf::Event::MouseButtonReleased>();
                        bool usingCommand = commandMove;
                        if (usingCommand) {
                            if (!selectedPiece) {
                                for (auto& up : pieces) {
                                    if (up->getSquare() == received_startSquare) {
                                        selectedPiece = up.get();
                                        break;
                                    }
                                }
                            }
                            if (!selectedPiece) {
                                std::cerr << "[network] incoming move: no piece found at start square " << received_startSquare << "\n";
                                commandMove = false;
                                moves.clear();
                                captureMoves.clear();
                                continue;
                            }
                        }
                        else {
                            if (!(mouse && mouse->button == sf::Mouse::Button::Left && selectedPiece)) {
                                continue;
                            }
                        }
                        sent_startSquare = selectedPiece->getSquare();
                        sf::Vector2f mouseF = mouse ? window.mapPixelToCoords({ mouse->position.x, mouse->position.y })
                            : sf::Vector2f();
                        auto snapBack = [&]() {
                            const int logicalSq = selectedPiece->getSquare();
                            const int dispSq = flipPerspective ? (63 - logicalSq) : logicalSq;
                            const int df = dispSq % 8;
                            const int dr = dispSq / 8;
                            constexpr int kLabelPad = 28;
                            const float rectW = static_cast<float>((boardSize.y - 2 * kLabelPad) / 8);
                            const float rectH = rectW;
                            const float left = static_cast<float>(offsetX + kLabelPad);
                            const float top = static_cast<float>(kLabelPad);
                            const float squareX = left + rectW * df;
                            const float squareY = top + rectH * (7 - dr);
                            const float cx = squareX + rectW / 2.f;
                            const float cy = squareY + rectH / 2.f;
                            selectedPiece->getSprite().setPosition(sf::Vector2f(cx, cy));
                            };
                        auto legalMoves = selectedPiece->getPossibleMoves(boardState);
                        if (!escapeMoves.empty()) {
                            std::vector<int> filtered;
                            for (auto& em : escapeMoves) {
                                if (em.first == selectedPiece->getSquare()) {
                                    filtered.push_back(em.second);
                                }
                            }
                            legalMoves = filtered;
                        }
                        int targetSquare = -1;
                        if (usingCommand) {
                            targetSquare = received_endSquare;
                        }
                        else {
                            targetSquare = mouseToLogicalSquare(mouseF, boardSize, offsetX, flipPerspective);
                            if (targetSquare == -1) {
                                snapBack();
                                selectedPiece = nullptr;
                                moves.clear();
                                captureMoves.clear();
                                continue;
                            }
                        }
                        if (std::find(legalMoves.begin(), legalMoves.end(), targetSquare) == legalMoves.end()) {
                            if (!usingCommand) {
                                const int logicalSq = selectedPiece->getSquare();
                                const int dispSq = flipPerspective ? (63 - logicalSq) : logicalSq;
                                const int df = dispSq % 8;
                                const int dr = dispSq / 8;
                                constexpr int kLabelPad = 28;
                                const float rectW = static_cast<float>((boardSize.y - 2 * kLabelPad) / 8);
                                const float rectH = rectW;
                                const float left = static_cast<float>(offsetX + kLabelPad);
                                const float top = static_cast<float>(kLabelPad);
                                const float squareX = left + rectW * df;
                                const float squareY = top + rectH * (7 - dr);
                                const float x = squareX + rectW / 2.f;
                                const float y = squareY + rectH / 2.f;
                                selectedPiece->getSprite().setPosition(sf::Vector2f(x, y));
                                float baseScale = (rectW * 0.6f) / 100.f;
                                if (selectedPiece->getType() == PieceType::Queen || selectedPiece->getType() == PieceType::King) {
                                    baseScale = (rectW * 0.7f) / 100.f;
                                }
                                selectedPiece->startScaleAnimation(selectedPiece->currentScale, baseScale, 0.08f);
                            }
                            std::cerr << "[apply] incoming move NOT legal: from=" << squareToString(sent_startSquare) << " to=" << squareToString(targetSquare) << "\n";
                            std::cerr << " legalMoves: ";
                            for (int m : legalMoves) std::cerr << squareToString(m) << " ";
                            selectedPiece = nullptr;
                            moves.clear();
                            captureMoves.clear();
                            if (usingCommand) { commandMove = false; }
                            continue;
                        }
                        ChessPiece* targetEnemy = nullptr;
                        bool isEnPassant = false;
                        int victimSq = -1;
                        if (selectedPiece->getType() == PieceType::Pawn &&
                            targetSquare == ChessPiece::enPassantTargetSquare &&
                            boardState[targetSquare] == 0) {
                            victimSq = targetSquare + (selectedPiece->isWhite() ? -8 : 8);
                            for (auto& p : pieces) {
                                if (p->getSquare() == victimSq && p->isWhite() != selectedPiece->isWhite()) {
                                    targetEnemy = p.get();
                                    isEnPassant = true;
                                    break;
                                }
                            }
                        }
                        else {
                            for (auto& p : pieces) {
                                if (p->getSquare() == targetSquare && p->isWhite() != selectedPiece->isWhite()) {
                                    targetEnemy = p.get();
                                    break;
                                }
                            }
                        }
                        int fromSq = selectedPiece->getSquare();
                        if (selectedPiece->getType() == PieceType::King &&
                            toRank(targetSquare) == toRank(fromSq) &&
                            std::abs(toFile(targetSquare) - toFile(fromSq)) == 2) {
                            bool currentlyInCheck = CheckCheckmate::isInCheck(boardState, piecePtrs, selectedPiece->isWhite());
                            if (currentlyInCheck) {
                                if (!usingCommand) snapBack();
                                selectedPiece = nullptr;
                                moves.clear();
                                captureMoves.clear();
                                if (usingCommand) commandMove = false;
                                continue;
                            }
                            bool kingSide = toFile(targetSquare) > toFile(fromSq);
                            int rnk = toRank(fromSq);
                            int stepFile = toFile(fromSq) + (kingSide ? 1 : -1);
                            int stepSq = toSquare(stepFile, rnk);
                            selectedPiece->setSquare(stepSq);
                            std::vector<int> simBoardStep(64, 0);
                            std::vector<ChessPiece*> simPtrsStep;
                            for (auto& up2 : pieces) {
                                int s = up2->getSquare();
                                if (s >= 0 && s < 64) {
                                    simBoardStep[s] = up2->isWhite() ? 1 : -1;
                                    simPtrsStep.push_back(up2.get());
                                }
                            }
                            bool stepInCheck = CheckCheckmate::isInCheck(simBoardStep, simPtrsStep, selectedPiece->isWhite());
                            selectedPiece->setSquare(fromSq);
                            if (stepInCheck) {
                                if (!usingCommand) snapBack();
                                selectedPiece = nullptr;
                                moves.clear();
                                captureMoves.clear();
                                if (usingCommand) commandMove = false;
                                continue;
                            }
                        }
                        selectedPiece->setSquare(targetSquare);
                        int enemyOldSq = -1;
                        if (targetEnemy) { enemyOldSq = targetEnemy->getSquare(); targetEnemy->setSquare(-1); }
                        std::vector<int> simBoard(64, 0);
                        std::vector<ChessPiece*> simPtrs;
                        for (auto& up : pieces) {
                            int s = up->getSquare();
                            if (s >= 0 && s < 64) {
                                simBoard[s] = up->isWhite() ? 1 : -1;
                                simPtrs.push_back(up.get());
                            }
                        }
                        bool selfInCheck = CheckCheckmate::isInCheck(simBoard, simPtrs, selectedPiece->isWhite());
                        selectedPiece->setSquare(fromSq);
                        if (targetEnemy) targetEnemy->setSquare(enemyOldSq);
                        if (selfInCheck) {
                            if (!usingCommand) snapBack();
                            selectedPiece = nullptr;
                            moves.clear();
                            captureMoves.clear();
                            if (usingCommand) commandMove = false;
                            continue;
                        }
                        if (targetEnemy) targetEnemy->setSquare(-1);
                        if (selectedPiece->getType() == PieceType::King &&
                            toRank(targetSquare) == toRank(fromSq) &&
                            std::abs(toFile(targetSquare) - toFile(fromSq)) == 2) {
                            bool kingSide = toFile(targetSquare) > toFile(fromSq);
                            int rnk = toRank(fromSq);
                            int rookFrom = toSquare(kingSide ? 7 : 0, rnk);
                            int rookTo = toSquare(kingSide ? 5 : 3, rnk);
                            for (auto& up : pieces) {
                                if (up->getSquare() == rookFrom &&
                                    up->isWhite() == selectedPiece->isWhite() &&
                                    up->getType() == PieceType::Rook) {
                                    up->setSquare(rookTo);
                                    up->setHasMoved(true);
                                    break;
                                }
                            }
                        }
                        selectedPiece->beginSlide(fromSq, targetSquare, kSlideAnimSeconds);
                        selectedPiece->setSquare(targetSquare);
                        selectedPiece->setHasMoved(true);
                        ChessPiece::enPassantTargetSquare = -1;
                        if (selectedPiece->getType() == PieceType::Pawn && std::abs(targetSquare - fromSq) == 16) {
                            ChessPiece::enPassantTargetSquare = (fromSq + targetSquare) / 2;
                        }
                        ChessPiece::whiteTurn = !ChessPiece::whiteTurn;
                        sent_endSquare = targetSquare;
                        bool wasNetworkCommand = usingCommand;
                        if (!wasNetworkCommand) {
                            const std::string uciSent = squareToString(sent_startSquare) + squareToString(sent_endSquare);
                            json sent_data = uciSent;
                            if (isNetworkHost) {
                                networkManager.sendToClient(sent_data);
                            }
                            else {
                                networkManager.sendToHost(sent_data);
                            }
                        }
                        else {
                            commandMove = false;
                        }
                        std::vector<int> newBoard(64, 0);
                        std::vector<ChessPiece*> newPtrs;
                        for (auto& up : pieces) {
                            int sq = up->getSquare();
                            if (sq >= 0 && sq < 64) {
                                newBoard[sq] = up->isWhite() ? 1 : -1;
                                newPtrs.push_back(up.get());
                            }
                        }
                        boardState = newBoard;
                        piecePtrs = newPtrs;
                        escapeMoves.clear();
                        allowedPieces.clear();
                        bool sideInCheck = CheckCheckmate::isInCheck(boardState, piecePtrs, ChessPiece::whiteTurn);
                        auto escapes = CheckCheckmate::getEscapeMoves(boardState, piecePtrs, ChessPiece::whiteTurn);
                        if (sideInCheck) {
                            std::cout << (ChessPiece::whiteTurn ? "White" : "Black") << " is in CHECK!\n";
                        }
                        if (sideInCheck && escapes.empty()) {
                            std::cout << "CHECKMATE! " << (ChessPiece::whiteTurn ? "White" : "Black") << " loses.\n";
                            gameOver = true;
                        }
                        else {
                            escapeMoves = escapes;
                            allowedPieces.clear();
                            for (auto& em : escapeMoves) allowedPieces.insert(em.first);
                            endangeredKingSquare = -1;
                            for (auto* p : piecePtrs) {
                                if (p->isWhite() == ChessPiece::whiteTurn && p->getType() == PieceType::King) {
                                    endangeredKingSquare = p->getSquare();
                                    break;
                                }
                            }
                        }
                        selectedPiece = nullptr;
                        moves.clear();
                        captureMoves.clear();
                        if (usingCommand) commandMove = false;
                    }
                    else if (event.is<sf::Event::MouseMoved>()) {
                        if (selectedPiece) {
                            auto mouse = event.getIf<sf::Event::MouseMoved>();
                            sf::Vector2f mouseF = window.mapPixelToCoords({ mouse->position.x, mouse->position.y });
                            selectedPiece->getSprite().setPosition(mouseF - dragOffset);
                        }
                    }
                }

                bool botInfo = currentState == State::Game_BotMatch || stockfishActive;
                chess.draw(window, boardSize, botInfo);

                // Draw legal move hints using ChessBoard::drawCircle
                if (selectedPiece && !moves.empty()) {
                    // Build fast lookup for legal squares
                    std::unordered_set<int> legalSet(moves.begin(), moves.end());

                    // Collect every legal square that lies on the path to any capture
                    std::unordered_set<int> redSquares;
                    if (!captureMoves.empty()) {
                        const int fromSq = selectedPiece->getSquare();
                        for (int capSq : captureMoves) {
                            // Ray/path from 'fromSq' to the capture square (includes intermediates and the capture square)
                            auto path = getPath(fromSq, capSq);
                            for (int sq : path) {
                                if (legalSet.find(sq) != legalSet.end()) {
                                    redSquares.insert(sq);
                                }
                            }
                        }
                    }

                    // Draw non-path legal moves in blue
                    for (int mv : moves) {
                        if (redSquares.find(mv) == redSquares.end()) {
                            chess.drawCircle(window, boardSize, 0.13f, offsetX, mv, sf::Color(70, 140, 245, 230));
                        }
                    }
                    // Draw capture-path legal moves (including the capture squares) in red
                    for (int sq : redSquares) {
                        chess.drawCircle(window, boardSize, 0.13f, offsetX, sq, sf::Color(220, 0, 0, 230));
                    }
                }

                // Draw selected piece last at its current sprite position (during drag)
                if (selectedPiece && selectedPiece->getSquare() >= 0 && selectedPiece->getSquare() < 64) {
                    // If the selected piece is the endangered king, draw a red rect behind it (before drawing the sprite)
                    if (inCheck && selectedPiece->getSquare() == endangeredKingSquare) {
                        chess.drawRect(window, boardSize, 0.95f, offsetX, endangeredKingSquare, sf::Color(220, 0, 0, 120));
                    }
                    selectedPiece->tickScaleOnly();
                    window.draw(selectedPiece->getSprite());
                }

                // Draw pieces on-board (non-selected first)
                {
                    if (inCheck && endangeredKingSquare >= 0) {
                        chess.drawRect(window, boardSize, 0.95f, offsetX, endangeredKingSquare, sf::Color(220, 0, 0, 120));
                    }
                    struct SavedState {
                        ChessPiece* p;
                        int origSq;
                        int slideFrom;
                        int slideTo;
                        bool sliding;
                    };
                    std::vector<SavedState> saved;
                    saved.reserve(pieces.size());

                    // Temporarily flip logical squares for rendering if we're a multiplayer client
                    if (flipPerspective) {
                        for (auto& up : pieces) {
                            if (!up) continue;
                            if (up->getSquare() < 0 || up->getSquare() >= 64) continue;
                            if (selectedPiece && up.get() == selectedPiece) continue;
                            SavedState st{ up.get(), up->getSquare(), up->slideFromSquare, up->slideToSquare, up->isSliding() };
                            saved.push_back(st);
                            up->setSquare(63 - st.origSq);
                            if (st.sliding) {
                                up->slideFromSquare = (st.slideFrom >= 0 && st.slideFrom < 64) ? (63 - st.slideFrom) : st.slideFrom;
                                up->slideToSquare = (st.slideTo >= 0 && st.slideTo < 64) ? (63 - st.slideTo) : st.slideTo;
                            }
                        }
                    }

                    for (auto& up : pieces) {
                        if (!up) continue;
                        if (up->getSquare() < 0 || up->getSquare() >= 64) continue;
                        if (selectedPiece && up.get() == selectedPiece) continue; // draw selected last at drag position

                        // Find original square for logic checks (needed because we may have flipped `square`)
                        int origSq = up->getSquare();
                        if (flipPerspective) {
                            auto it = std::find_if(saved.begin(), saved.end(), [&](const SavedState& st) { return st.p == up.get(); });
                            if (it != saved.end()) origSq = it->origSq;
                        }

                        // Draw piece (square may be flipped)
                        up->draw(window, boardSize, offsetX);

                        // Show blue glow only to the local player if their own king is in check (MP/BotMatch).
                        const bool restrictGlowToLocal = (currentState == State::Game_Multiplayer || currentState == State::Game_BotMatch);
                        const bool localSideIsInCheck = (isLocalSideWhite == ChessPiece::whiteTurn);
                        const bool showForLocal = (!restrictGlowToLocal) || (localSideIsInCheck && up->isWhite() == isLocalSideWhite);

                        if (inCheck
                            && up->isWhite() == ChessPiece::whiteTurn
                            && origSq != endangeredKingSquare
                            && allowedPieces.find(origSq) != allowedPieces.end()
                            && showForLocal) {
                            up->drawPieceWithGlow(window, up->getSprite(), sf::Color(70, 140, 245, 130), 12, 0.60f);
                        }
                    }

                    // Restore original logical squares after rendering
                    if (flipPerspective) {
                        for (const auto& st : saved) {
                            st.p->setSquare(st.origSq);
                            st.p->slideFromSquare = st.slideFrom;
                            st.p->slideToSquare = st.slideTo;
                        }
                    }
                }

                // Highlight capturable enemy pieces (including en passant victims) in red              
                if (selectedPiece && !captureMoves.empty()) {
                    for (int mv : captureMoves) {
                        int victimSq = mv;

                        // Handle en passant: target square is empty, victim is behind it
                        if (selectedPiece->getType() == PieceType::Pawn &&
                            mv == ChessPiece::enPassantTargetSquare &&
                            mv >= 0 && mv < 64 &&
                            std::vector<int>(boardState).at(mv) == 0) {
                            victimSq = mv + (selectedPiece->isWhite() ? -8 : 8);
                        }

                        // Find the enemy piece at victimSq and glow it
                        for (auto& up : pieces) {
                            if (!up) continue;
                            if (up->getSquare() != victimSq) continue;
                            if (up->isWhite() == selectedPiece->isWhite()) continue;
                            up->drawPieceWithGlow(window, up->getSprite(), sf::Color(220, 0, 0, 120), 12, 0.60f);
                            break;
                        }
                    }
                }

                if (aifirstmove) aifirstmove = false;
                engineStats.draw(window, boardSize, lastEngineStats);

                if (gameOver) {
                    std::string winner = ChessPiece::whiteTurn ? "Black" : "White";
                    sf::RectangleShape overlay(sf::Vector2f(static_cast<float>(boardSize.x), static_cast<float>(boardSize.y)));
                    overlay.setFillColor(sf::Color(0, 0, 0, 150));
                    window.draw(overlay);

                    sf::Text gameOverText(font, "Game Over");
                    gameOverText.setCharacterSize(64);
                    gameOverText.setFillColor(sf::Color::White);
                    const sf::FloatRect gameOverTextBounds = gameOverText.getLocalBounds();
                    gameOverText.setOrigin(sf::Vector2f(gameOverTextBounds.position.x + gameOverTextBounds.size.x / 2.f,
                        gameOverTextBounds.position.y + gameOverTextBounds.size.y / 2.f));
                    gameOverText.setPosition(sf::Vector2f(static_cast<float>(boardSize.x) / 2.f + 63,
                        static_cast<float>(boardSize.y) / 2.f - 38));

                    sf::Text winnerText(font, winner + " won");
                    winnerText.setCharacterSize(40);
                    winnerText.setFillColor(sf::Color::White);
                    const sf::FloatRect winnerTextBounds = winnerText.getLocalBounds();
                    winnerText.setOrigin(sf::Vector2f(winnerTextBounds.position.x + winnerTextBounds.size.x / 2.f,
                        winnerTextBounds.position.y + winnerTextBounds.size.y / 2.f));
                    winnerText.setPosition(sf::Vector2f(static_cast<float>(boardSize.x) / 2.f + 63,
                        static_cast<float>(boardSize.y) / 2.f + 28));

                    window.draw(gameOverText);
                    window.draw(winnerText);

                    // Draw "Return to Menu" button
                    backToMenuButton.setPosition(sf::Vector2f(btnRect.position.x, btnRect.position.y));
                    // Decide hover at draw-time so event-time color changes aren't overwritten
                    sf::Vector2i mousePixel = sf::Mouse::getPosition(window);
                    sf::Vector2f mouseF = window.mapPixelToCoords(mousePixel);
                    if (btnRect.contains(mouseF)) {
                        backToMenuButton.setFillColor(sf::Color(20, 20, 20, 220)); // hover
                    } else {
                        backToMenuButton.setFillColor(sf::Color(50, 50, 50, 200)); // default
                    }
                    backToMenuButton.setOutlineThickness(2.f);
                    backToMenuButton.setOutlineColor(sf::Color(180, 180, 180));
                    window.draw(backToMenuButton);

                    sf::Text btnText(font, "Return to Menu");
                    btnText.setCharacterSize(30);
                    btnText.setFillColor(sf::Color::White);
                    sf::FloatRect bt = btnText.getLocalBounds();
                    btnText.setOrigin(sf::Vector2f(bt.position.x + bt.size.x / 2.f, bt.position.y + bt.size.y / 2.f));
                    btnText.setPosition(sf::Vector2f(btnRect.position.x + btnRect.size.x / 2.f, btnRect.position.y + btnRect.size.y / 2.f));
                    window.draw(btnText);
                }

                window.display();
                sf::sleep(sf::milliseconds(1));
            }
        }
    }
}