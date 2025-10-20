#include "King.hpp"
#include "ChessPiece.hpp"
#include "conversion.hpp"
#include "positions.hpp"
#include "main.hpp" // for getPiecesConst()

King::King(bool isWhite, int square, const sf::Texture& texture) : ChessPiece(isWhite, PieceType::King, square, texture) {}

void King::draw(sf::RenderWindow& window, sf::Vector2u boardSize, int offsetX) {
    ChessPiece::draw(window, boardSize, offsetX);
}

std::vector<int> King::getPossibleMoves(const std::vector<int>& boardState) const {
    std::vector<int> moves;

    int startRank = toRank(square);
    int startFile = toFile(square);

    const std::vector<std::pair<int, int>> directions = {
            {1, 0},    // right
            {-1, 0},   // left
            {0, -1},   // down
            {0, 1},    // up
            {1, 1},    // up-right
            {-1, 1},   // up-left
            {1, -1},   // down-right
            {-1, -1}   // down-left
    };

    for (auto [fileStep, rankStep] : directions) {
        int currentFile = startFile + fileStep;
        int currentRank = startRank + rankStep;

        if (currentFile >= 0 && currentFile < 8 &&
            currentRank >= 0 && currentRank < 8) {

            int targetSquare = toSquare(currentFile, currentRank);
            int targetValue = boardState[targetSquare];

            if (targetValue == 0) {
                moves.push_back(targetSquare);
            }
            else {
                int targetColor = (targetValue == 1);
                if (targetColor != white) {
                    moves.push_back(targetSquare);
                }
            }
        }
    }

    // --- Castling candidates (final legality checked in main) ---
    // Requirements we can check here:
    // - King hasn't moved
    // - King is on original file (e-file, file 4)
    // - Rook of same color exists on the corner and hasn't moved
    // - Squares between king and rook are empty
    if (!getHasMoved()) {
        // King must be on starting file (e1/e8)
        if (startFile == 4) {
            const auto& pieces = getPiecesConst();

            auto rookAt = [&](int sq) -> const ChessPiece* {
                for (const auto& up : pieces) {
                    const ChessPiece* p = up.get();
                    if (p->getSquare() == sq && p->isWhite() == white && p->getType() == PieceType::Rook) {
                        return p;
                    }
                }
                return nullptr;
            };

            // Kingside: rook on h-file (file 7), empty f and g squares
            {
                int rookSq = toSquare(7, startRank);
                const ChessPiece* rook = rookAt(rookSq);
                if (rook && !rook->getHasMoved()) {
                    int fSq = toSquare(5, startRank);
                    int gSq = toSquare(6, startRank);
                    if (boardState[fSq] == 0 && boardState[gSq] == 0) {
                        // Do NOT check check/attacks here to avoid recursion; main validates
                        moves.push_back(gSq);
                    }
                }
            }

            // Queenside: rook on a-file (file 0), empty b, c, d squares
            {
                int rookSq = toSquare(0, startRank);
                const ChessPiece* rook = rookAt(rookSq);
                if (rook && !rook->getHasMoved()) {
                    int bSq = toSquare(1, startRank);
                    int cSq = toSquare(2, startRank);
                    int dSq = toSquare(3, startRank);
                    if (boardState[bSq] == 0 && boardState[cSq] == 0 && boardState[dSq] == 0) {
                        // Do NOT check check/attacks here to avoid recursion; main validates
                        moves.push_back(cSq);
                    }
                }
            }
        }
    }

    return moves;
}
