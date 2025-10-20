#include "Bishop.hpp"
#include "conversion.hpp"
#include "positions.hpp"

Bishop::Bishop(bool isWhite, int square, const sf::Texture& texture)
    : ChessPiece(isWhite, PieceType::Bishop, square, texture) {
}

void Bishop::draw(sf::RenderWindow& window, sf::Vector2u boardSize, int offsetX) {
    ChessPiece::draw(window, boardSize, offsetX);
}

std::vector<int> Bishop::getPossibleMoves(const std::vector<int>& boardState) const {
    std::vector<int> moves;

    int startRank = toRank(getSquare());
    int startFile = toFile(getSquare());

    const std::vector<std::pair<int, int>> directions = {
        {1, 1},    // up-right
        {-1, 1},   // up-left
        {1, -1},   // down-right
        {-1, -1}   // down-left
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
                bool targetColor = (targetValue == 1);
                if (targetColor != isWhite()) {
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
