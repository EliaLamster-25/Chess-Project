#pragma once
#include "ChessPiece.hpp"

class Bishop : public ChessPiece {
public:
    Bishop(bool isWhite, int square, const sf::Texture& texture);
    std::vector<int> getPossibleMoves(const std::vector<int>& boardState) const override;
    void draw(sf::RenderWindow& window, sf::Vector2u boardSize, int offsetX) override;
    bool isSlidingPiece() const override { return true; }
};
