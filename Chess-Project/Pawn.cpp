#include "Pawn.hpp"
#include "ChessPiece.hpp"
#include "conversion.hpp"
#include "positions.hpp"
#include "textures.hpp"
#include "main.hpp"
#include "Queen.hpp"

Pawn::Pawn(bool isWhite, int square, const sf::Texture& texture) : ChessPiece(isWhite, PieceType::Pawn, square, texture) {
    this->isWhite = isWhite;
}

void Pawn::draw(sf::RenderWindow& window, sf::Vector2u boardSize, int offsetX) {
    ChessPiece::draw(window, boardSize, offsetX);
}

std::vector<int> Pawn::getPossibleMoves(const std::vector<int>& boardState) const {
    std::vector<int> moves;

    int startRank = toRank(square);
    int startFile = toFile(square);

    std::vector<std::pair<int, int>> directions;

    if (isWhite) {
        directions = {
            {0, 1},    // forward
            {1, 1},    // diagonal right
            {-1, 1}    // diagonal left
        };
    }
    else {
        directions = {
            {0, -1},   // forward
            {1, -1},   // diagonal right
            {-1, -1}   // diagonal left
        };
    }

    for (auto [fileStep, rankStep] : directions) {
        int currentFile = startFile + fileStep;
        int currentRank = startRank + rankStep;

        if (currentFile >= 0 && currentFile < 8 &&
            currentRank >= 0 && currentRank < 8) {

            int targetSquare = toSquare(currentFile, currentRank);
            int targetValue = boardState[targetSquare];

            if (fileStep == 0) {
                if (targetValue == 0) {
                    moves.push_back(targetSquare);

                    int startingRank = isWhite ? 1 : 6;
                    if (startRank == startingRank) {
                        int doubleForwardRank = startRank + 2 * rankStep;
                        if (doubleForwardRank >= 0 && doubleForwardRank < 8) {
                            int doubleForwardSquare = toSquare(startFile, doubleForwardRank);
                            if (boardState[doubleForwardSquare] == 0) {
                                moves.push_back(doubleForwardSquare);
                            }
                        }
                    }
                }
            }
            else {
                if (targetValue != 0) {
                    int targetColor = (targetValue == 1);
                    if (targetColor != isWhite) {
                        moves.push_back(targetSquare);
                    }
                }
                else {
                    // En passant capture to empty target square
                    if (ChessPiece::enPassantTargetSquare == targetSquare) {
                        moves.push_back(targetSquare);
                    }
                }
            }
        }
    }

    return moves;
}

