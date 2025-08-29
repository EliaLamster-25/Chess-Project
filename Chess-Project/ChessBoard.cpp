#include <iostream>
#include <SFML/Graphics.hpp>
#include "ChessBoard.hpp"

void ChessBoard::draw(sf::RenderWindow& surface, sf::Vector2u size) {
    int RectWidth = size.y / 8;
    int RectHeight = size.y / 8;
    int iterations_x = 0;
    int iterations_y = 0;
    int OffsetX = size.x / 4;

    for (int i = 0; i < 64; i++) {
        sf::RectangleShape rect(
            sf::Vector2f(static_cast<float>(RectWidth), static_cast<float>(RectHeight))
        );
        rect.setPosition(sf::Vector2f(static_cast<float>(RectWidth * iterations_x + OffsetX), static_cast<float>(RectHeight * iterations_y)));
        if ((i + iterations_y) % 2 == 0) {
            rect.setFillColor(sf::Color(240, 217, 181));
        }
        else {
            rect.setFillColor(sf::Color(181, 136, 99));
        }
        surface.draw(rect);
        iterations_x++;
        if (iterations_x >= 8) {
            iterations_x = 0;
            iterations_y++;
        }
	}
}