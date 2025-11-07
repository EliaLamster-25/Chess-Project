#include "main.hpp"
#include "assets/chessPieces/blackBishop.h"
#include "Bishop.hpp"
#include "stockfish.hpp"
#include <limits>

// ADDED: overlay buffer helpers
#include <string>

enum class State { Menu, Game_Singleplayer, Game_Multiplayer, Game_BotMatch, Game_Bot_vs_Bot};

bool aifirstmove;

bool gameOver = false;

ChessPiece* selectedPiece = nullptr;

constexpr int kEngineMoveTimeMs = 50; // tweak between 50..200 for responsiveness

constexpr float kSlideAnimSeconds = 0.20f;

//json json_game;

State currentState;
namespace {
    std::vector<std::unique_ptr<ChessPiece>> pieces;
}

static json lastEngineStats;
static json compareLastEngineStats;

std::vector<std::unique_ptr<ChessPiece>>& getPieces()
{
    return pieces;
}

const std::vector<std::unique_ptr<ChessPiece>>& getPiecesConst()
{
    return pieces;
}

// Put near other helpers
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

// Put near other statics/top-level helpers
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


// Horizontal mirror helpers (A<->H), keep ranks unchanged.

// Map a 0..63 square to its horizontally mirrored square.
// This is rank*8 + (7 - file), which is equivalent to sq ^ 7.
static inline int flipSquareHoriz(int sq) {
    return (sq >= 0 && sq < 64) ? (sq ^ 7) : sq;
}

// Flip a boardState (vector<int> size 64) horizontally in-place.
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

// Flip all piece squares horizontally (off-board pieces stay off).
// Also flips ChessPiece::enPassantTargetSquare if set.
static void flipPiecesHoriz(std::vector<std::unique_ptr<ChessPiece>>& piecesRef) {
    for (auto& up : piecesRef) {
        if (!up) continue;
        const int sq = up->getSquare();
        if (sq >= 0 && sq < 64) {
            up->setSquare(flipSquareHoriz(sq));
        }
        // If you track a captured-square indicator for visuals, mirror that too.
        const int cap = up->getCapturedSquare();
        if (cap >= 0 && cap < 64) {
            up->setCapturedSquare(flipSquareHoriz(cap));
        }
    }
    if (ChessPiece::enPassantTargetSquare >= 0 && ChessPiece::enPassantTargetSquare < 64) {
        ChessPiece::enPassantTargetSquare = flipSquareHoriz(ChessPiece::enPassantTargetSquare);
    }
}

// Flip a UCI move horizontally (e.g., "a2a4" -> "h2h4"). Promotion letter is preserved.
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

// Flip a 64-bit bitboard horizontally (reverse bits inside each 8-bit rank).
// This mirrors files A<->H for the conventional a1=LSB layout.
static inline std::uint64_t flipBitboardHoriz(std::uint64_t bb) {
    const std::uint64_t k1 = 0x5555555555555555ULL; // swap adjacent bits
    const std::uint64_t k2 = 0x3333333333333333ULL; // swap pairs
    const std::uint64_t k4 = 0x0f0f0f0f0f0f0f0fULL; // swap nibbles
    bb = ((bb >> 1) & k1) | ((bb & k1) << 1);
    bb = ((bb >> 2) & k2) | ((bb & k2) << 2);
    bb = ((bb >> 4) & k4) | ((bb & k4) << 4);
    return bb;
}

static void flipPositionHoriz(std::vector<std::unique_ptr<ChessPiece>>& piecesRef,
    std::vector<int>& boardStateRef) {
    flipPiecesHoriz(piecesRef);
    flipBoardStateHoriz(boardStateRef);

    // Clear transient UI state so the next frame rebuilds it coherently.
    selectedPiece = nullptr;
    // If you keep other per-frame overlays, let the next frame recompute them.
}

// Find the first matching piece (optionally at a given square)
static ChessPiece* findPiece(const std::vector<ChessPiece*>& pieces, PieceType type, bool isWhite, std::optional<int> atSquare = std::nullopt) {
    for (ChessPiece* p : pieces) {
        if (!p) continue;
        if (p->getSquare() == -1) continue; // skip off-board
        if (p->getType() == type && p->isWhite() == isWhite) {
            if (!atSquare || p->getSquare() == *atSquare) {
                return p;
            }
        }
    }
    return nullptr;
}

// Convenience: get by label like "Queen, Black"
static ChessPiece* getPieceByLabel(const std::vector<ChessPiece*>& pieces, std::string_view label) {
    auto parsed = parsePieceLabel(label);
    if (!parsed) return nullptr;
    return findPiece(pieces, parsed->first, parsed->second);
}

std::vector<int> getPath(int from, int to) {
    std::vector<int> path;
    if (from < 0 || from > 63 || to < 0 || to > 63) return path; // bounds check

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
        if (sq < 0 || sq > 63) break; // bounds check
        path.push_back(sq);
        file += dFile;
        rank += dRank;
    }
    if (to >= 0 && to <= 63) path.push_back(to);

    return path;
}

// UCI helpers
static std::string squareToAlgebraic(int sq) {
    if (sq < 0 || sq > 63) return "";
    int file = sq % 8;
    int rank = sq / 8;
    char f = static_cast<char>('a' + file);
    char r = static_cast<char>('1' + rank);
    return std::string() + f + r;
}

static bool parseUciMove(const std::string& uci, int& fromSq, int& toSq, char& promo) {
    // UCI: e2e4 or e7e8q
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

#include <random>  // For std::random_device, std::mt19937, std::uniform_int_distribution

std::string getBotUciMove(const std::vector<std::unique_ptr<ChessPiece>>& pieces) {
    // Build current board state and piece pointers
    std::vector<int> boardState(64, 0);
    std::vector<ChessPiece*> piecePtrs;
    for (const auto& up : pieces) {
        int sq = up->getSquare();
        if (sq >= 0 && sq < 64) {
            boardState[sq] = up->isWhite() ? 1 : -1;
            piecePtrs.push_back(up.get());
        }
    }

    // Collect all possible legal moves for black (ChessPiece::whiteTurn == false)
    std::vector<std::pair<int, int>> allMoves;  // fromSq, toSq
    for (auto* piece : piecePtrs) {
        if (piece->isWhite()) continue;  // Only black pieces

        auto possibleMoves = piece->getPossibleMoves(boardState);

        // Filter moves that leave king in check
        for (int toSq : possibleMoves) {
            // Simulate move
            int fromSq = piece->getSquare();
            ChessPiece* captured = nullptr;
            int capOldSq = -1;
            bool isEnPassant = (piece->getType() == PieceType::Pawn && toSq == ChessPiece::enPassantTargetSquare && boardState[toSq] == 0);
            if (isEnPassant) {
                int victimSq = toSq + 8;  // Since black, en passant down
                for (auto* p : piecePtrs) {
                    if (p->getSquare() == victimSq && p->isWhite()) {
                        captured = p;
                        capOldSq = victimSq;
                        break;
                    }
                }
            }
            else if (boardState[toSq] != 0 && boardState[toSq] == 1) {  // Capture white
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

            // Rebuild sim board/ptrs
            std::vector<int> simBoard(64, 0);
            std::vector<ChessPiece*> simPtrs;
            for (const auto& up : pieces) {
                int s = up->getSquare();
                if (s >= 0 && s < 64) {
                    simBoard[s] = up->isWhite() ? 1 : -1;
                    simPtrs.push_back(up.get());
                }
            }

            bool inCheck = CheckCheckmate::isInCheck(simBoard, simPtrs, false);  // Check for black

            // Restore
            piece->setSquare(fromSq);
            if (captured) captured->setSquare(capOldSq);

            if (!inCheck) {
                allMoves.emplace_back(fromSq, toSq);
            }
        }
    }

    if (allMoves.empty()) {
        return "";  // No moves (stalemate/checkmate)
    }

    // Pick a random move
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, allMoves.size() - 1);
    auto [fromSq, toSq] = allMoves[dis(gen)];

    // Handle promotion (stub: always queen)
    std::string promo = "";
    if (toRank(toSq) == 7 && findPiece(piecePtrs, PieceType::Pawn, false, fromSq)) {  // Black pawn to rank 0? Wait, ranks: assuming 0=white home, 7=black home? Wait, adjust based on your coord (rank 0 is bottom?)
        // Assuming rank 0 is white's promotion (black pawns promote at rank 0)
        if (toRank(toSq) == 0) {
            promo = "q";  // Or random underpromotion if desired
        }
    }

    return squareToAlgebraic(fromSq) + squareToAlgebraic(toSq) + promo;
}

int received_startSquare = -1;
int received_endSquare = -1;
int sent_startSquare = -1;
int sent_endSquare = -1;

bool commandMove = false;

void setState(State state) {
    currentState = state;
}

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

    // Castling: king moves two files on same rank -> slide rook
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
                // animate rook as part of castling
                up->beginSlide(rookFrom, rookTo, kSlideAnimSeconds);
                up->setSquare(rookTo);
                up->setHasMoved(true);
                break;
            }
        }
    }

    // Capture: normal or en passant
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

    // Move the piece with animation
    mover->beginSlide(fromSq, toSq, kSlideAnimSeconds);
    mover->setSquare(toSq);
    mover->setHasMoved(true);

    // En passant target for next move
    ChessPiece::enPassantTargetSquare = -1;
    if (mover->getType() == PieceType::Pawn && std::abs(toSq - fromSq) == 16) {
        ChessPiece::enPassantTargetSquare = (fromSq + toSq) / 2;
    }

    ChessPiece::whiteTurn = !ChessPiece::whiteTurn;
    return true;
}

botOverlay engineStats;

bool handleEngineStats(sf::RenderWindow& window, sf::Vector2u size, json data, networkManager& net) {
    (void)net;
    if (currentState == State::Game_BotMatch || currentState == State::Game_Bot_vs_Bot) {
        if (data.is_object()) {
			lastEngineStats = data;
   //         std::cout << "[Engine Stats] " << data.dump() << "\n";
			//std::cout << "[Last Engine Stats] " << lastEngineStats.dump() << "\n";
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
    //Game game(window);

    ChessBoard chess;

    std::vector<ChessPiece*> piecePtrs;
    sf::Vector2f dragOffset;

    textures textureManager;
    textureManager.loadTextures(pieces);

    StockfishEngine engine(L"C:/Users/elamster/Downloads/stockfish-windows-x86-64-avx2/stockfish/stockfish.exe");  // Note the L prefix for wide string literal
    aifirstmove = true;

    ChessPiece::whiteTurn = true;
    ChessPiece::enPassantTargetSquare = -1;

    int offsetX = desktopMode.size.x / 4;
    sf::Vector2u boardSize = desktopMode.size;
    float cellSize = static_cast<float>(boardSize.y) / 8.f;

    std::vector<int> moves;
    std::vector<int> captureMoves;

    bool gameOver = true;

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

    std::thread networkThread;          // default-constructed, not joinable yet
    bool networkThreadStarted = false;

    flipPiecesHoriz(pieces);

    while (window.isOpen())
    {
        while (true) {
            if (currentState == State::Menu) {
                while (auto eventOpt = window.pollEvent()) {
                    const sf::Event& event = *eventOpt;
                    if (event.is<sf::Event::Closed>()) {
                        window.close();
                    }

                    // Avoid calling menu.mouseEvent twice; store result
                    std::string action = menu.mouseEvent(event);
                    if (action == "singleplayer") {
                        setState(State::Game_Singleplayer);
                        isNetworkHost = true;
                        continue;
                    }
                    if (action == "multiplayer") {
                        setState(State::Game_Multiplayer);

                        // Start network thread only once
                        if (!networkThreadStarted) {
                            networkThreadStarted = true;
                            networkThread = std::thread([&]() {
                                networkManager.start("multiplayer");
                                });
                        }
                        continue;
                    }
                    if (action == "botMatch") {
                        setState(State::Game_BotMatch);

                        // Start network thread only once
                        if (!networkThreadStarted) {
                            networkThreadStarted = true;
                            networkThread = std::thread([&]() {
                                networkManager.start("botMatch");
                                });
                        }
                        isNetworkHost = true;
                        continue;
                    }
                    if (action == "botVsBot") {
                        setState(State::Game_Bot_vs_Bot);

                        // Start network thread only once
                        if (!networkThreadStarted) {
                            networkThreadStarted = true;
                            networkThread = std::thread([&]() {
                                networkManager.start("botMatch");
                                });
                        }
                        isNetworkHost = true;
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
            // Include Game_Bot_vs_Bot here so the game loop runs after the menu closes.
            else if (currentState == State::Game_Singleplayer
                  || currentState == State::Game_Multiplayer
                  || currentState == State::Game_BotMatch
                  || currentState == State::Game_Bot_vs_Bot) {
                window.clear();
                // Put this right before the first draw call in the render branch (before: chess.draw(window, boardSize);)
                const bool flipPerspective = (currentState == State::Game_Multiplayer) && !isNetworkHost;
                // Defer engine reply by one frame so we render between moves

                if (currentState == State::Game_Bot_vs_Bot && engineReplyScheduled && !gameOver && !anyPieceSliding()) {
                    std::string reply = engine.getNextMove(kEngineMoveTimeMs);
                    std::cout << "Stockfish reply (deferred): " << reply << "\n";
                    if (!reply.empty() && reply != "(none)") {
                        if (applyUciMoveLocal(reply, pieces)) {
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
                else if (currentState == State::Game_Multiplayer || currentState == State::Game_BotMatch || currentState == State::Game_Bot_vs_Bot) {
                    if (aifirstmove) {
                        // Only do this for BotVsBot client or Multiplayer host; here we care about BotVsBot client
                        if (!ChessPiece::whiteTurn) ChessPiece::whiteTurn = true;

                        if (currentState == State::Game_Bot_vs_Bot) {
                            // Start fresh for each new BotVsBot
                            engine.reset();


                            std::string aiMove = engine.getNextMove(kEngineMoveTimeMs);
                            std::cout << "Stockfish suggests move: " << aiMove << "\n";

                            bool applied = false;
                            if (!aiMove.empty() && aiMove != "(none)") {
                                applied = applyUciMoveLocal(aiMove, pieces);
                            }

                            if (applied) {
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
                            // Primary path: always expect a UCI string (e.g., "e2e4" or "e7e8q")
                            if (received_data.is_string()) {
                                const std::string uci = received_data.get<std::string>();

                                if (!applyUciMoveLocal(uci, pieces)) {
                                    std::cerr << "Failed to apply client move: \"" << uci << "\"\n";
                                } else {
                                    if (currentState == State::Game_Bot_vs_Bot) {
                                        engine.opponentMove(uci);
                                        engineReplyScheduled = true;
                                        engineReplyRoute = ReplyRoute::ToClient;
                                    }
                                }
                                selectedPiece = nullptr;
                            }
                            // Back-compat: only attempt if payload is an object
                            else if (received_data.is_object()) {
                                const auto itStart = received_data.find("start_square");
                                const auto itEnd   = received_data.find("end_square");
                                if (itStart != received_data.end() && itEnd != received_data.end()
                                    && itStart->is_number_integer() && itEnd->is_number_integer()) {
                                    const int fs = itStart->get<int>();
                                    const int ts = itEnd->get<int>();
                                    const std::string uci = squareToString(fs) + squareToString(ts);

                                    if (!applyUciMoveLocal(uci, pieces)) {
                                        std::cerr << "Failed to apply client move (legacy JSON): " << uci << "\n";
                                    } else if (currentState == State::Game_Bot_vs_Bot) {
                                        engine.opponentMove(uci);
                                        if (!gameOver) {
                                            engineReplyScheduled = true;
                                            engineReplyRoute = ReplyRoute::ToClient;
                                        }
                                    }
                                    selectedPiece = nullptr;
                                } else {
                                    std::cerr << "Malformed legacy move packet from client: " << received_data.dump() << "\n";
                                }
                            }
                            else {
                                std::cerr << "Malformed move packet from client (type=" << received_data.type_name() << "): " << received_data.dump() << "\n";
                            }
                        }
                    }
                    // Client path
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
                                        if (currentState == State::Game_Bot_vs_Bot) {
                                            engine.opponentMove(uci);
                                            if (!gameOver) {
                                                engineReplyScheduled = true;
                                                engineReplyRoute = ReplyRoute::ToHost;
                                            }
                                        }
                                    }
                                    selectedPiece = nullptr;
                                }
                                // Back-compat: only attempt if payload is an object
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
                                        else if (currentState == State::Game_Bot_vs_Bot) {
                                            engine.opponentMove(uci);
                                            if (!gameOver) {
                                                engineReplyScheduled = true;
                                                engineReplyRoute = ReplyRoute::ToHost;
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
                // --- Build current board state + raw pointer list for this frame ---
                std::vector<int> boardState(64, 0);
                // CRITICAL: avoid progressive growth causing lag while dragging
                piecePtrs.clear();
                piecePtrs.reserve(pieces.size());
                for (auto& up : pieces) {
                    int sq = up->getSquare();
                    if (sq >= 0 && sq < 64) boardState[sq] = up->isWhite() ? 1 : -1;
                    // only add on-board pieces to pointer list used by check functions
                    if (sq >= 0 && sq < 64) piecePtrs.push_back(up.get());
                }

                // --- Precompute check/escape moves & highlights for this frame ---
                bool inCheck = CheckCheckmate::isInCheck(boardState, piecePtrs, ChessPiece::whiteTurn);
                std::vector<std::pair<int, int>> escapeMoves;
                std::unordered_set<int> allowedPieces;
                int endangeredKingSquare = -1;

                if (inCheck) {
                    // find endangered king square (where the side-to-move's king stands)
                    for (auto* p : piecePtrs) {
                        if (p->isWhite() == ChessPiece::whiteTurn && p->getType() == PieceType::King) {
                            endangeredKingSquare = p->getSquare();
                            break;
                        }
                    }

                    // --- Optimization: Prepare board state and piece pointers once ---
                    std::vector<int> simBoard = boardState;
                    std::vector<ChessPiece*> simPtrs = piecePtrs;

                    // For each friendly piece, simulate each of its legal moves by mutating real pieces then restoring.
                    for (auto* piece : piecePtrs) {
                        if (piece->isWhite() != ChessPiece::whiteTurn) continue;

                        auto legalMoves = piece->getPossibleMoves(boardState);
                        int originalFrom = piece->getSquare();

                        for (int move : legalMoves) {
                            // find any captured opponent piece at 'move' among ALL pieces (including off-board ones)
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
                            // En passant capture: victim is behind the target square
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

                            // --- Optimization: Incrementally update simBoard and simPtrs ---
                            // Remove piece from original square
                            simBoard[originalFrom] = 0;
                            piece->setSquare(move);
                            simBoard[move] = piece->isWhite() ? 1 : -1;
                            if (capturedPiece) {
                                simBoard[capturedOrigSquare] = 0;
                                capturedPiece->setSquare(-1); // mark captured / off-board
                            }
                            // Remove captured piece from simPtrs if needed
                            if (capturedPiece) {
                                auto it = std::find(simPtrs.begin(), simPtrs.end(), capturedPiece);
                                if (it != simPtrs.end()) simPtrs.erase(it);
                            }

                            // if king is safe after this simulated move, record it
                            bool stillInCheck = CheckCheckmate::isInCheck(simBoard, simPtrs, ChessPiece::whiteTurn);
                            if (!stillInCheck) {
                                escapeMoves.emplace_back(originalFrom, move);
                                allowedPieces.insert(originalFrom);
                            }

                            // --- Restore simBoard and simPtrs ---
                            simBoard[move] = 0;
                            piece->setSquare(originalFrom);
                            simBoard[originalFrom] = piece->isWhite() ? 1 : -1;
                            if (capturedPiece) {
                                capturedPiece->setSquare(capturedOrigSquare);
                                simBoard[capturedOrigSquare] = capturedPiece->isWhite() ? 1 : -1;
                                // Re-add captured piece to simPtrs if needed
                                if (std::find(simPtrs.begin(), simPtrs.end(), capturedPiece) == std::end(simPtrs))
                                    simPtrs.push_back(capturedPiece);
                            }
                        }
                    }
                }

                // Local player side: white in bot matches, otherwise driven by host/client
                const bool isLocalSideWhite = (currentState == State::Game_BotMatch) ? true : isNetworkHost.load();
                // --- Event loop ---
                while (auto eventOpt = window.pollEvent()) {
                    const sf::Event& event = *eventOpt;

                    if (event.is<sf::Event::Closed>()) {
                        window.close();
                    }

                    if (gameOver) {
                        // still allow window close events handled above; skip other input
                        continue;
                    }

                    // Mouse pressed: select piece (but restrict selection if in check)
                    if (event.is<sf::Event::MouseButtonPressed>()) {
                        auto mouse = event.getIf<sf::Event::MouseButtonPressed>();
                        if (mouse->button == sf::Mouse::Button::Left) {
                            if (gameOver) continue; // no selection if game ended
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

                                    // Compute base scale and animate shrink to 88% for click feedback
                                    constexpr int kLabelPad = 28;
                                    const float rectW = static_cast<float>((boardSize.y - 2 * kLabelPad) / 8);
                                    float baseScale = (rectW * 0.6f) / 100.f;
                                    if (selectedPiece->getType() == PieceType::Queen || selectedPiece->getType() == PieceType::King) {
                                        baseScale = (rectW * 0.7f) / 100.f;
                                    }
                                    const float targetShrink = baseScale * 0.88f; // 12% smaller
                                    selectedPiece->startScaleAnimation(selectedPiece->currentScale, targetShrink, 0.05f);

                                    // choose moves (existing logic) ...
                                    if (!escapeMoves.empty()) {
                                        moves.clear();
                                        for (auto& em : escapeMoves) {
                                            if (em.first == selectedPiece->getSquare()) moves.push_back(em.second);
                                        }
                                    }
                                    else {
                                        moves = selectedPiece->getPossibleMoves(boardState);
                                    }

                                    // Filter out moves that would leave own king in check, so only legal moves are drawn
                                    {
                                        const int fromSq = selectedPiece->getSquare();
                                        std::vector<int> filteredMoves;
                                        filteredMoves.reserve(moves.size());

                                        for (int mv : moves) {
                                            if (mv < 0 || mv >= 64) continue;

                                            // Special handling for castling: ensure not currently in check and intermediate square not attacked
                                            if (selectedPiece->getType() == PieceType::King &&
                                                toRank(mv) == toRank(fromSq) &&
                                                std::abs(toFile(mv) - toFile(fromSq)) == 2) {

                                                // Must not be in check currently
                                                bool currentlyInCheck = CheckCheckmate::isInCheck(boardState, piecePtrs, selectedPiece->isWhite());
                                                if (currentlyInCheck) {
                                                    continue; // cannot castle out of check
                                                }

                                                // Rook must exist and be unmoved on proper corner
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

                                                // Simulate king stepping to the intermediate square (f-file on kingside, d-file on queenside)
                                                int stepFile = toFile(fromSq) + (kingSide ? 1 : -1);
                                                int stepSq = toSquare(stepFile, rnk);

                                                // Simulate step
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

                                                // Restore
                                                selectedPiece->setSquare(fromSq);

                                                if (stepInCheck) {
                                                    continue; // cannot pass through check
                                                }
                                                // fallthrough to standard final-square simulation below
                                            }

                                            // find potential captured opponent on mv
                                            ChessPiece* capPtr = nullptr;
                                            int capOldSq = -1;
                                            for (auto& up2 : pieces) {
                                                if (up2->getSquare() == mv && up2->isWhite() != selectedPiece->isWhite()) {
                                                    capPtr = up2.get();
                                                    capOldSq = capPtr->getSquare();
                                                    break;
                                                }
                                            }
                                            // En passant capture: victim is behind mv
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

                                            // Simulate move to final square
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

                                            // Restore
                                            selectedPiece->setSquare(fromSq);
                                            if (capPtr) capPtr->setSquare(capOldSq);

                                            if (!selfInCheck) filteredMoves.push_back(mv);
                                        }

                                        moves.swap(filteredMoves);
                                    }

                                    // capture moves for red highlighting (recompute after filtering)
                                    captureMoves.clear();
                                    for (int mv : moves) {
                                        bool isCap = false;
                                        if (mv >= 0 && mv < 64 && boardState[mv] != 0 &&
                                            boardState[mv] != (selectedPiece->isWhite() ? 1 : -1)) {
                                            isCap = true;
                                        }
                                        // En passant capture squares are empty but still a capture
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

                    // Mouse released: attempt to apply move (enforcing safety)
                    else if (event.is<sf::Event::MouseButtonReleased>() || commandMove) {
                        auto mouse = event.getIf<sf::Event::MouseButtonReleased>();
                        bool usingCommand = commandMove;

                        // If this is a console command, ensure we have a selectedPiece from input_startSquare
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
                            // Mouse path must have a left release and an active selection
                            if (!(mouse && mouse->button == sf::Mouse::Button::Left && selectedPiece)) {
                                continue;
                            }
                        }

                        sent_startSquare = selectedPiece->getSquare();

                        sf::Vector2f mouseF = mouse ? window.mapPixelToCoords({ mouse->position.x, mouse->position.y })
                            : sf::Vector2f();

                        // New board geometry based on your square placement
                        //float rectW = static_cast<float>(boardSize.y) / 8.f;
                        //float rectH = rectW;
                        //float stepX = rectW * 0.95f;
                        //float stepY = rectH * 0.95f;
                        //float left = offsetX * 1.06f;
                        //float top = rectH * 0.20f; // 0.2*RectHeight top margin
                        //float right = left + 8.f * stepX;
                        //float bottom = top + 8.f * stepY;

                        // Helper: snap sprite back to its current square center using new formula
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

                        // Compute legal moves (restricted if in check)
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

                        // Determine target square
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

                        // If move not legal, exit appropriately
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

                                // Animate scale back to base on release
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
                            if (usingCommand) {
                                commandMove = false;   // consume the incoming command
                            }
                            continue;
                        }

                        // ========== Capture handling ===========
                        ChessPiece* targetEnemy = nullptr;
                        bool isEnPassant = false;
                        int victimSq = -1;

                        if (selectedPiece->getType() == PieceType::Pawn &&
                            targetSquare == ChessPiece::enPassantTargetSquare &&
                            boardState[targetSquare] == 0) {
                            // En passant: victim is behind the target square
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
                            // Normal capture on the target square
                            for (auto& p : pieces) {
                                if (p->getSquare() == targetSquare && p->isWhite() != selectedPiece->isWhite()) {
                                    targetEnemy = p.get();
                                    break;
                                }
                            }
                        }

                        // ========== Self-check guard (disallow moves that leave own king in check) ==========
                        int fromSq = selectedPiece->getSquare();

                        // Special-case castling: ensure intermediate square is safe and not in check currently
                        if (selectedPiece->getType() == PieceType::King &&
                            toRank(targetSquare) == toRank(fromSq) &&
                            std::abs(toFile(targetSquare) - toFile(fromSq)) == 2) {
                            // must not be in check
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

                            // Simulate step
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

                            // Restore
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

                        // Simulate final position
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

                        // Restore
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
                        // ========== end self-check guard ==========

                        // Move the selected piece
                        if (targetEnemy) targetEnemy->setSquare(-1); // capture now

                        // Handle castling rook shift when king moves two files on same rank
                        if (selectedPiece->getType() == PieceType::King &&
                            toRank(targetSquare) == toRank(fromSq) &&
                            std::abs(toFile(targetSquare) - toFile(fromSq)) == 2) {

                            bool kingSide = toFile(targetSquare) > toFile(fromSq);
                            int rnk = toRank(fromSq);

                            // Find rook on the corner (h-file for kingside, a-file for queenside)
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

                        // Apply the move to the piece (this was commented out, causing snap-back)
                        selectedPiece->beginSlide(fromSq, targetSquare, kSlideAnimSeconds);
                        selectedPiece->setSquare(targetSquare);

                        // Mark piece as having moved (affects future castling rights)
                        selectedPiece->setHasMoved(true);

                        // En passant availability for the next player: clear by default,
                        // set only if this move was a double pawn push.
                        ChessPiece::enPassantTargetSquare = -1;
                        if (selectedPiece->getType() == PieceType::Pawn && std::abs(targetSquare - fromSq) == 16) {
                            ChessPiece::enPassantTargetSquare = (fromSq + targetSquare) / 2;
                        }
                        ChessPiece::whiteTurn = !ChessPiece::whiteTurn;


                        sent_endSquare = targetSquare;

                        // Was this move coming from a network/console command?
                        bool wasNetworkCommand = usingCommand; // usingCommand is true when this move came from received data

                        if (!wasNetworkCommand) {
                            const std::string uciSent = squareToString(sent_startSquare) + squareToString(sent_endSquare);
                            json sent_data = uciSent; // send as a plain string
                            if (isNetworkHost) {
                                networkManager.sendToClient(sent_data);
                            } else {
                                networkManager.sendToHost(sent_data);
                            }
                        } else {
                            commandMove = false; // clear the pending command so it won't be processed again
                        }

                        // ========== Update state ==========
                        std::vector<int> newBoard(64, 0);
                        std::vector<ChessPiece*> newPtrs;
                        for (auto& up : pieces) {
                            int sq = up->getSquare();
                            if (sq >= 0 && sq < 64) {
                                newBoard[sq] = up->isWhite() ? 1 : -1;
                                newPtrs.push_back(up.get());
                            }
                        }

                        // Update the global state BEFORE checks
                        boardState = newBoard;
                        piecePtrs = newPtrs;

                        escapeMoves.clear();
                        allowedPieces.clear();

                        // Now compute check / escapes once and act on it
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
                            // prepare highlights for viewer: allowedPieces, endangeredKingSquare
                            escapeMoves = escapes; // reuse your existing variable for rendering
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

                        // Cleanup selection and overlays. Ensure command flag is reset on command path.
                        selectedPiece = nullptr;
                        moves.clear();
                        captureMoves.clear();
                        if (usingCommand) commandMove = false;
                    }


                    // Dragging
                    else if (event.is<sf::Event::MouseMoved>()) {
                        if (selectedPiece) {
                            auto mouse = event.getIf<sf::Event::MouseMoved>();
                            sf::Vector2f mouseF = window.mapPixelToCoords({ mouse->position.x, mouse->position.y });
                            selectedPiece->getSprite().setPosition(mouseF - dragOffset);
                        }
                    }

                } // end event loop

				bool botInfo = currentState == State::Game_BotMatch || currentState == State::Game_Bot_vs_Bot;
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
                    //constexpr int kLabelPad = 28;
                    //const float rectW = static_cast<float>((boardSize.y - 2 * kLabelPad) / 8);
                    //float pieceScale = (rectW * 0.55f) / 100.f;  // Normal "pop" size for all
                    //if (selectedPiece->getType() == PieceType::Queen) {
                    //    pieceScale = (rectW * 0.7f) / 100.f;  // Extra large pop for Q/K
                    //}
                    //if (selectedPiece->getType() == PieceType::King) {
                    //    pieceScale = (rectW * 0.65f) / 100.f;  // Extra large pop for Q/K
                    //}
                    //if (selectedPiece->getType() == PieceType::Knight) {
                    //    pieceScale = (rectW * 0.65f) / 100.f;  // Extra large pop for Q/K
                    //}
                    //selectedPiece->getSprite().setScale(sf::Vector2f(pieceScale, pieceScale));
                    selectedPiece->tickScaleOnly();
                    window.draw(selectedPiece->getSprite());
                }

                // Draw pieces on-board (non-selected first)
                // Draw pieces on-board (non-selected first)
                {
                    // Draw endangered king square once (under pieces)
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
                            SavedState st{
                                up.get(),
                                up->getSquare(),
                                up->slideFromSquare,
                                up->slideToSquare,
                                up->isSliding()
                            };
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
                    // Show game over screen
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
                        static_cast<float>(boardSize.y) / 2.f));

                    sf::Text winnerText(font, winner + " won");
                    winnerText.setCharacterSize(40);
                    winnerText.setFillColor(sf::Color::White);
                    const sf::FloatRect winnerTextBounds = winnerText.getLocalBounds();
                    winnerText.setOrigin(sf::Vector2f(winnerTextBounds.position.x + winnerTextBounds.size.x / 2.f,
                        winnerTextBounds.position.y + winnerTextBounds.size.y / 2.f));
                    winnerText.setPosition(sf::Vector2f(static_cast<float>(boardSize.x) / 2.f + 63,
                        static_cast<float>(boardSize.y) / 2.f - 63));

                    window.draw(gameOverText);
                    window.draw(winnerText);
                }

                window.display();

                sf::sleep(sf::milliseconds(1));
            }
        }
        }
    }