#ifndef CHESSPIECE_HPP
#define CHESSPIECE_HPP
#include <iostream>

#include <SFML/Graphics.hpp>

class ChessPiece {
public:
    ChessPiece(std::string type, sf::Vector2u boardSize, sf::Vector2u gridPos, const sf::Color& color);

    void handleEvent(const sf::Event& event, sf::RenderWindow& window, sf::Vector2u boardSize, int offsetX);
    void draw(sf::RenderWindow& surface, sf::Vector2u boardSize, int offsetX);

private:
    sf::Vector2u gridPosition;  // position on board (0–7, 0–7)
    sf::CircleShape shape;
    bool isDragging = false;
    sf::Vector2f dragOffset;    // offset between mouse and circle center
    std::string PieceType;
};

#endif