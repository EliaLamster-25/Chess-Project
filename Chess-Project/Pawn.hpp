#pragma once
#include "ChessPiece.hpp"

class Pawn : public ChessPiece {
public:
    Pawn(bool isWhite, int square, const sf::Texture& texture);
    std::vector<int> getPossibleMoves(const std::vector<int>& boardState) const override;
    void draw(sf::RenderWindow& window, sf::Vector2u boardSize, int offsetX) override;
    bool isSlidingPiece() const override { return true; }

    void setEnPassant(bool value) { enPassant = value; }

private:
    bool isWhite;
    bool enPassant = false;
};
