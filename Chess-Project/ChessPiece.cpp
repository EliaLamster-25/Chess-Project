#include "ChessPiece.hpp"
#include <cmath>
#include <iostream>

ChessPiece::ChessPiece(std::string type, sf::Vector2u boardSize, sf::Vector2u gridPos, const sf::Color& color)
    : gridPosition(gridPos)
{
	PieceType = type;
    float cellSize = static_cast<float>(boardSize.y) / 8.f;
    shape.setRadius(cellSize / 2.f * 0.8f); // slightly smaller than cell
    shape.setFillColor(color);
}

void ChessPiece::handleEvent(const sf::Event& event, sf::RenderWindow& window, sf::Vector2u boardSize, int offsetX)
{
    float cellSize = static_cast<float>(boardSize.y) / 8.f;
    
    if (event.is<sf::Event::MouseButtonPressed>()) {
        auto mouse = sf::Mouse::getPosition(window);
        sf::Vector2f mouseF(static_cast<float>(mouse.x), static_cast<float>(mouse.y));

        if (shape.getGlobalBounds().contains(mouseF)) {
            isDragging = true;
            dragOffset = mouseF - shape.getPosition();
        }
    }
    else if (event.is<sf::Event::MouseButtonReleased>()) {
        if (isDragging) {
            isDragging = false;

            auto pos = shape.getPosition();
            unsigned gx = static_cast<unsigned>((pos.x - offsetX + cellSize / 2.f) / cellSize);
            unsigned gy = static_cast<unsigned>((pos.y + cellSize / 2.f) / cellSize);

            int ix = static_cast<int>(gx);
            int iy = static_cast<int>(gy);

            if (ix < 0 || ix > 7 || iy < 0 || iy > 7) {
                if (ix < 0) ix = 0;
                if (ix > 7) ix = 7;
                if (iy < 0) iy = 0;
                if (iy > 7) iy = 7;

                int dxLeft = std::abs(ix - 0);
                int dxRight = std::abs(ix - 7);
                int dyTop = std::abs(iy - 0);
                int dyBottom = std::abs(iy - 7);

                int minDist = std::min({ dxLeft, dxRight, dyTop, dyBottom });

                if (minDist == dxLeft) ix = 0;
                else if (minDist == dxRight) ix = 7;
                else if (minDist == dyTop) iy = 0;
                else if (minDist == dyBottom) iy = 7;
            }

            gridPosition = { static_cast<unsigned>(ix), static_cast<unsigned>(iy) };
        }

    }
    else if (event.is<sf::Event::MouseMoved>()) {
        if (isDragging) {
            sf::Vector2i mouseI = sf::Mouse::getPosition(window);
            sf::Vector2f mouseF(static_cast<float>(mouseI.x), static_cast<float>(mouseI.y));

            shape.setPosition(mouseF - dragOffset);
        }
    }

}

void ChessPiece::draw(sf::RenderWindow& surface, sf::Vector2u boardSize, int offsetX)
{
    if (!isDragging) {
        float cellSize = static_cast<float>(boardSize.y) / 8.f;

        float x = static_cast<float>(offsetX)
            + static_cast<float>(gridPosition.x) * cellSize
            + cellSize * 0.1f;

        float y = static_cast<float>(gridPosition.y) * cellSize
            + cellSize * 0.1f;

        sf::Vector2f position(x, y);

        shape.setPosition(position);
    }

    surface.draw(shape);
}

