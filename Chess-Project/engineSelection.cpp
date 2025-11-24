#include "engineSelection.hpp"
#include <iostream>
#include <SFML/Graphics.hpp>
#include <SFML/Graphics/RoundedRectangleShape.hpp>
#include <SFML/System/Clock.hpp>

engineSelection::engineSelection(sf::RenderWindow& window, sf::Vector2f size)
    : window(window),
    windowSize(size),
    font(),
    fontBold(),
    title(fontBold),
    botButton(sf::Vector2f(300.f, 90.f), 20.f, 12), // size, corner radius, points per corner
    stockfishButton(sf::Vector2f(300.f, 90.f), 20.f, 12), // size, corner radius, points per corner
    lc0Button(sf::Vector2f(300.f, 90.f), 20.f, 12), // size, corner radius, points per corner
    botText(font),
    stockfishText(font),
    lc0Text(font),
    backdrop()
{
    if (!font.openFromMemory(segoeuithis_ttf, segoeuithis_ttf_len)) {
        std::cerr << "Failed to load Segoe UI font from memory\n";
    }

    if (!fontBold.openFromMemory(segoeuithibd_ttf, segoeuithis_ttf_len)) {
        std::cerr << "Failed to load Segoe UI Bold font from memory\n";
    }

    backdrop.setSize(sf::Vector2f(windowSize.y, windowSize.y));
    backdrop.setPosition(sf::Vector2f(windowSize.x / 4.f, 0.f));
    backdrop.setFillColor(sf::Color(0, 0, 0, 150));

    title.setString("Select Engine");
    title.setCharacterSize(64);
    title.setFillColor(sf::Color::White);
    title.setPosition(sf::Vector2f(windowSize.x / 2.f - (title.getLocalBounds().size.x / 3.f), windowSize.y / 2.f - 150.f));

    botButton.setFillColor(sf::Color(60, 60, 60, 220));
    botButton.setOutlineThickness(4.f);
    botButton.setOutlineColor(sf::Color(200, 200, 200));
    botButton.setPosition(sf::Vector2f(
        windowSize.x / 2.f - botButton.getLocalBounds().size.x / 3.45f,
        windowSize.y / 2.f
    ));

    stockfishButton.setFillColor(sf::Color(60, 60, 60, 220));
    stockfishButton.setOutlineThickness(4.f);
    stockfishButton.setOutlineColor(sf::Color(200, 200, 200));
    stockfishButton.setPosition(sf::Vector2f(
        windowSize.x / 2.f - stockfishButton.getLocalBounds().size.x / 3.45f,
        windowSize.y / 2.f + 120.f
    ));

    lc0Button.setFillColor(sf::Color(60, 60, 60, 220));
    lc0Button.setOutlineThickness(4.f);
    lc0Button.setOutlineColor(sf::Color(200, 200, 200));
    lc0Button.setPosition(sf::Vector2f(
        windowSize.x / 2.f - lc0Button.getLocalBounds().size.x / 3.45f,
        windowSize.y / 2.f + 240.f
    ));

    botText.setString("Bot");
    botText.setCharacterSize(40);
    botText.setFillColor(sf::Color::White);

    sf::FloatRect botTextBounds = botText.getLocalBounds();
    botText.setOrigin(sf::Vector2f(botTextBounds.position.x + botTextBounds.size.x / 2.f,
        botTextBounds.position.y + botTextBounds.size.y / 2.f));
    botText.setPosition(sf::Vector2f(
        botButton.getPosition().x + botButton.getSize().x / 2.f,
        botButton.getPosition().y + botButton.getSize().y / 2.f - 4.f
    ));

    stockfishText.setString("Stockfish");
    stockfishText.setCharacterSize(40);
    stockfishText.setFillColor(sf::Color::White);

    sf::FloatRect stockfishTextBounds = stockfishText.getLocalBounds();
    stockfishText.setOrigin(sf::Vector2f(stockfishTextBounds.position.x + stockfishTextBounds.size.x / 2.f,
        stockfishTextBounds.position.y + stockfishTextBounds.size.y / 2.f));
    stockfishText.setPosition(sf::Vector2f(
        stockfishButton.getPosition().x + stockfishButton.getSize().x / 2.f,
        stockfishButton.getPosition().y + stockfishButton.getSize().y / 2.f - 4.f
    ));

    lc0Text.setString("LC0");
    lc0Text.setCharacterSize(40);
    lc0Text.setFillColor(sf::Color::White);

    sf::FloatRect lc0TextBounds = lc0Text.getLocalBounds();
    lc0Text.setOrigin(sf::Vector2f(lc0TextBounds.position.x + lc0TextBounds.size.x / 2.f,
        lc0TextBounds.position.y + lc0TextBounds.size.y / 2.f));
    lc0Text.setPosition(sf::Vector2f(
        lc0Button.getPosition().x + lc0Button.getSize().x / 2.f,
        lc0Button.getPosition().y + lc0Button.getSize().y / 2.f - 4.f
    ));
}

std::string engineSelection::mouseEvent(const sf::Event& event) {

    if (auto released = event.getIf<sf::Event::MouseButtonReleased>()) {
        if (released->button == sf::Mouse::Button::Left) {
            sf::Vector2f worldPos = window.mapPixelToCoords({ released->position.x, released->position.y });
            if (botButton.getGlobalBounds().contains(worldPos)) {
                return "bot";
            }
            if (stockfishButton.getGlobalBounds().contains(worldPos)) {
                return "stockfish";
            }
            if (lc0Button.getGlobalBounds().contains(worldPos)) {
				return "lc0";
                
            }
        }
    }

    if (auto hover = event.getIf<sf::Event::MouseMoved>()) {
        sf::Vector2f worldPos = window.mapPixelToCoords({ hover->position.x, hover->position.y });
        if (botButton.getGlobalBounds().contains(worldPos)) {
            botButton.setFillColor(sf::Color(20, 20, 20, 220));
        }
        else {
            botButton.setFillColor(sf::Color(60, 60, 60, 220));
        }
        if (stockfishButton.getGlobalBounds().contains(worldPos)) {
            stockfishButton.setFillColor(sf::Color(20, 20, 20, 220));
        }
        else {
            stockfishButton.setFillColor(sf::Color(60, 60, 60, 220));
        }
        if (lc0Button.getGlobalBounds().contains(worldPos)) {
            lc0Button.setFillColor(sf::Color(20, 20, 20, 220));
        }
        else {
            lc0Button.setFillColor(sf::Color(60, 60, 60, 220));
        }
    }

    return "";
}

void engineSelection::draw(sf::RenderWindow& window) {
    window.draw(backdrop);
    window.draw(title);
    window.draw(botButton);
    window.draw(stockfishButton);
    window.draw(lc0Button);
    window.draw(botText);
    window.draw(stockfishText);
    window.draw(lc0Text);
}