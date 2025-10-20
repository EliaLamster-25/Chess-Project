#include "Rook.hpp"
#include "ChessPiece.hpp"
#include "conversion.hpp"
#include "positions.hpp"

Rook::Rook(bool isWhite, int square, const sf::Texture& texture) : ChessPiece(isWhite, PieceType::Rook, square, texture) {}

void Rook::draw(sf::RenderWindow& window, sf::Vector2u boardSize, int offsetX) {
    ChessPiece::draw(window, boardSize, offsetX);
}
std::vector<int> Rook::getPossibleMoves(const std::vector<int>& boardState) const {
    std::vector<int> moves;

    int startRank = toRank(square);
    int startFile = toFile(square);

    const std::vector<std::pair<int, int>> directions = {
            {1, 0},    // right
            {-1, 0},   // left
            {0, -1},   // down
            {0, 1},  // up
    };

    for (auto [fileStep, rankStep] : directions) {
        int currentFile = startFile + fileStep;
        int currentRank = startRank + rankStep;

        while (currentFile >= 0 && currentFile < 8 &&
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
                break;
            }

            currentFile += fileStep;
            currentRank += rankStep;
        }
    }

    return moves;
}
