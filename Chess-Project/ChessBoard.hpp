#define CHESSBOARD_HPP

#include <SFML/Graphics.hpp>


class ChessBoard {
public:
    void draw(sf::RenderWindow& surface, sf::Vector2u size);
};
