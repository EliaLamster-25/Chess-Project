#include "Knight.hpp"
#include "ChessPiece.hpp"
#include "conversion.hpp"
#include "positions.hpp"

Knight::Knight(bool isWhite, int square, const sf::Texture& texture) : ChessPiece(isWhite, PieceType::Knight, square, texture) {}

void Knight::draw(sf::RenderWindow& window, sf::Vector2u boardSize, int offsetX) {
    ChessPiece::draw(window, boardSize, offsetX);
}

std::vector<int> Knight::getPossibleMoves(const std::vector<int>& boardState) const {
    std::vector<int> moves;

    int startRank = toRank(square);
    int startFile = toFile(square);

    const std::vector<std::pair<int, int>> directions = {
            {2, -1},    // right-down
            {2, 1},   // right-up
            {-2, -1},   // left-down
            {-2, 1},  // left-up
            {1, 2},    // up-right
            {-1, 2},   // up-left
            {1, -2},   // down-right
            {-1, -2}   // down-left
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

    return moves;
}
