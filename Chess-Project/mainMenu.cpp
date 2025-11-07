#include "mainMenu.hpp"
#include <iostream>
#include <SFML/Graphics.hpp>
#include <SFML/Graphics/RoundedRectangleShape.hpp>
#include <SFML/System/Clock.hpp>

Menu::Menu(sf::RenderWindow& window, sf::Vector2f size)
    : window(window),
      windowSize(size),
      font(),
      fontBold(),
      title(fontBold),
      singleplayerButton(sf::Vector2f(300.f, 90.f), 20.f, 12), // size, corner radius, points per corner
      multiplayerButton(sf::Vector2f(300.f, 90.f), 20.f, 12), // size, corner radius, points per corner
      botGameButton(sf::Vector2f(300.f, 90.f), 20.f, 12), // size, corner radius, points per corner
      singleplayerText(font),
      multiplayerText(font),
      botGameText(font),
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

    title.setString("Chess Game");
    title.setCharacterSize(64);
    title.setFillColor(sf::Color::White);
    title.setPosition(sf::Vector2f(windowSize.x / 2.f - (title.getLocalBounds().size.x/3.f), windowSize.y / 2.f - 150.f));

    singleplayerButton.setFillColor(sf::Color(60, 60, 60, 220));
    singleplayerButton.setOutlineThickness(4.f);
    singleplayerButton.setOutlineColor(sf::Color(200, 200, 200));
    singleplayerButton.setPosition(sf::Vector2f(
        windowSize.x / 2.f - singleplayerButton.getLocalBounds().size.x / 3.45f,
        windowSize.y / 2.f
    ));

    multiplayerButton.setFillColor(sf::Color(60, 60, 60, 220));
    multiplayerButton.setOutlineThickness(4.f);
    multiplayerButton.setOutlineColor(sf::Color(200, 200, 200));
    multiplayerButton.setPosition(sf::Vector2f(
        windowSize.x / 2.f - multiplayerButton.getLocalBounds().size.x / 3.45f,
        windowSize.y / 2.f + 120.f
    ));

    botGameButton.setFillColor(sf::Color(60, 60, 60, 220));
    botGameButton.setOutlineThickness(4.f);
    botGameButton.setOutlineColor(sf::Color(200, 200, 200));
    botGameButton.setPosition(sf::Vector2f(
        windowSize.x / 2.f - botGameButton.getLocalBounds().size.x / 3.45f,
        windowSize.y / 2.f + 240.f
    ));

    singleplayerText.setString("Singleplayer");
    singleplayerText.setCharacterSize(40);
    singleplayerText.setFillColor(sf::Color::White);

    sf::FloatRect singleplayerTextBounds = singleplayerText.getLocalBounds();
    singleplayerText.setOrigin(sf::Vector2f(singleplayerTextBounds.position.x + singleplayerTextBounds.size.x / 2.f,
        singleplayerTextBounds.position.y + singleplayerTextBounds.size.y / 2.f));
    singleplayerText.setPosition(sf::Vector2f(
        singleplayerButton.getPosition().x + singleplayerButton.getSize().x / 2.f,
        singleplayerButton.getPosition().y + singleplayerButton.getSize().y / 2.f - 4.f
    ));

    multiplayerText.setString("Multiplayer");
    multiplayerText.setCharacterSize(40);
    multiplayerText.setFillColor(sf::Color::White);

    sf::FloatRect multiplayerTextBounds = multiplayerText.getLocalBounds();
    multiplayerText.setOrigin(sf::Vector2f(multiplayerTextBounds.position.x + multiplayerTextBounds.size.x / 2.f,
        multiplayerTextBounds.position.y + multiplayerTextBounds.size.y / 2.f));
    multiplayerText.setPosition(sf::Vector2f(
        multiplayerButton.getPosition().x + multiplayerButton.getSize().x / 2.f,
        multiplayerButton.getPosition().y + multiplayerButton.getSize().y / 2.f - 4.f
    ));

    botGameText.setString("Bot Match");
    botGameText.setCharacterSize(40);
    botGameText.setFillColor(sf::Color::White);

    sf::FloatRect botGameTextBounds = botGameText.getLocalBounds();
    botGameText.setOrigin(sf::Vector2f(botGameTextBounds.position.x + botGameTextBounds.size.x / 2.f,
        botGameTextBounds.position.y + botGameTextBounds.size.y / 2.f));
    botGameText.setPosition(sf::Vector2f(
        botGameButton.getPosition().x + botGameButton.getSize().x / 2.f,
        botGameButton.getPosition().y + botGameButton.getSize().y / 2.f - 4.f
    ));
}

std::string Menu::mouseEvent(const sf::Event& event) {
    // Long-press state for the Bot Match button
    static bool botHoldActive = false;
    static bool botHoldTriggered = false;
    static sf::Clock botHoldClock;

    auto tryTriggerBotHold = [&]() -> std::string {
        if (botHoldActive && !botHoldTriggered && sf::Mouse::isButtonPressed(sf::Mouse::Button::Left)) {
            sf::Vector2i pixel = sf::Mouse::getPosition(window);
            sf::Vector2f worldPos = window.mapPixelToCoords(pixel);
            // Must still be over the button
            if (botGameButton.getGlobalBounds().contains(worldPos)) {
                if (botHoldClock.getElapsedTime().asSeconds() >= 3.f) {
                    botHoldTriggered = true;
                    botHoldActive = false; // consume
                    return "botVsBot";     // trigger immediately without waiting for release
                }
            } else {
                // moved off the button — cancel hold
                botHoldActive = false;
                botHoldTriggered = false;
            }
        }
        return "";
    };

    if (auto pressed = event.getIf<sf::Event::MouseButtonPressed>()) {
        if (pressed->button == sf::Mouse::Button::Left) {
            sf::Vector2f worldPos = window.mapPixelToCoords({ pressed->position.x, pressed->position.y });
            if (botGameButton.getGlobalBounds().contains(worldPos)) {
                botHoldActive = true;
                botHoldTriggered = false;
                botHoldClock.restart();
            }
        }
    }

    if (auto released = event.getIf<sf::Event::MouseButtonReleased>()) {
        if (released->button == sf::Mouse::Button::Left) {
            sf::Vector2f worldPos = window.mapPixelToCoords({ released->position.x, released->position.y });
            if (singleplayerButton.getGlobalBounds().contains(worldPos)) {
                botHoldActive = false;
                botHoldTriggered = false;
                return "singleplayer";
            }
            if (multiplayerButton.getGlobalBounds().contains(worldPos)) {
                botHoldActive = false;
                botHoldTriggered = false;
                return "multiplayer";
            }
            if (botGameButton.getGlobalBounds().contains(worldPos)) {
                // If a long hold already fired, ignore the short click fallback
                if (!botHoldTriggered) {
                    const float heldSeconds = botHoldClock.getElapsedTime().asSeconds();
                    botHoldActive = false;
                    botHoldTriggered = false;
                    if (heldSeconds >= 3.f) {
                        return "botVsBot"; // safety: in case the threshold was hit exactly on release
                    }
                    return "botMatch"; // short click
                }
            }
            // release elsewhere cancels
            botHoldActive = false;
            botHoldTriggered = false;
        }
    }

    if (auto hover = event.getIf<sf::Event::MouseMoved>()) {
        sf::Vector2f worldPos = window.mapPixelToCoords({ hover->position.x, hover->position.y });
        if (singleplayerButton.getGlobalBounds().contains(worldPos)) {
            singleplayerButton.setFillColor(sf::Color(20, 20, 20, 220));
        }
        else {
            singleplayerButton.setFillColor(sf::Color(60, 60, 60, 220));
        }
        if (multiplayerButton.getGlobalBounds().contains(worldPos)) {
            multiplayerButton.setFillColor(sf::Color(20, 20, 20, 220));
        }
        else {
            multiplayerButton.setFillColor(sf::Color(60, 60, 60, 220));
        }
        if (botGameButton.getGlobalBounds().contains(worldPos)) {
            botGameButton.setFillColor(sf::Color(20, 20, 20, 220));
        }
        else {
            botGameButton.setFillColor(sf::Color(60, 60, 60, 220));
        }
        // Cancel hold if cursor leaves while holding
        if (botHoldActive && !botGameButton.getGlobalBounds().contains(worldPos)) {
            botHoldActive = false;
            botHoldTriggered = false;
        }
    }

    // Check long-press threshold on any event to trigger immediately
    if (auto action = tryTriggerBotHold(); !action.empty()) {
        return action; // "botVsBot"
    }

    return "";
}

void Menu::draw(sf::RenderWindow& window) {
    window.draw(backdrop);
    window.draw(title);
    window.draw(singleplayerButton);
    window.draw(multiplayerButton);
    window.draw(botGameButton);
    window.draw(singleplayerText);
    window.draw(multiplayerText);
    window.draw(botGameText);
}