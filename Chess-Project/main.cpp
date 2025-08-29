#include <iostream>
#include <SFML/Graphics.hpp>
#include <SFML/Window.hpp>
#include <SFML/Window/Mouse.hpp>
#include "ChessBoard.hpp"
#include "ChessPiece.hpp"

int main()
{
    sf::VideoMode desktopMode = sf::VideoMode::getDesktopMode();
    sf::RenderWindow window(sf::VideoMode(desktopMode.size), "Chess", sf::Style::None);

    ChessBoard chess;
    ChessPiece piece("King", desktopMode.size, { 0, 0 }, sf::Color::Black);

    int offsetX = desktopMode.size.x / 4;

    while (window.isOpen())
    {
        while (const std::optional event = window.pollEvent())
        {
            if (event->is<sf::Event::Closed>())
                window.close();

            else if (event.has_value())
                piece.handleEvent(*event, window, desktopMode.size, offsetX);
        }

        window.clear();

        chess.draw(window, desktopMode.size);
        piece.draw(window, desktopMode.size, offsetX);

        window.display();
    }
}