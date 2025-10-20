#include <SFML/Graphics.hpp>
#include <SFML/Graphics/RoundedRectangleShape.hpp>
#include "assets/segoeuithis_ttf.h"
#include "assets/segoeuithibd_ttf.h"

class Menu {
public:
    Menu(sf::RenderWindow& window, sf::Vector2f size);

    std::string mouseEvent(const sf::Event& event);

    void draw(sf::RenderWindow& window);

private:
    sf::RenderWindow& window;
    sf::Vector2f windowSize;
    sf::Font font;
    sf::Font fontBold;
    sf::Text title;
    sf::RoundedRectangleShape singleplayerButton;
    sf::RoundedRectangleShape multiplayerButton;
    sf::RoundedRectangleShape botGameButton;
    sf::Text singleplayerText;
    sf::Text multiplayerText;
    sf::Text botGameText;
    sf::RectangleShape backdrop;
};
