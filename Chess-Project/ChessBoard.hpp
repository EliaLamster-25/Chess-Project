#define CHESSBOARD_HPP

#include <SFML/Graphics.hpp>
#include <iostream>
#include <SFML/Graphics.hpp>
#include <cstdint>
#include <vector>
#include <array>
#include <cmath>

#include "assets/segoeuithibd_ttf.h"
#include "Network.hpp"


class ChessBoard {
public:
    void draw(sf::RenderWindow& surface, sf::Vector2u size, bool botInfo);
    void drawCircle(sf::RenderWindow& window, sf::Vector2u boardSize, float size, int offsetX, int squareIndex, sf::Color color);
    void drawRect(sf::RenderWindow& window, sf::Vector2u boardSize, float size, int offsetX, int squareIndex, sf::Color color);
};
