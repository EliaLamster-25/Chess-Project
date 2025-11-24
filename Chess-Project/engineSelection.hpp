#include <SFML/Graphics.hpp>
#include <SFML/Graphics/RoundedRectangleShape.hpp>
#include "assets/segoeuithis_ttf.h"
#include "assets/segoeuithibd_ttf.h"

class engineSelection {
public:
    engineSelection(sf::RenderWindow& window, sf::Vector2f size);

    std::string mouseEvent(const sf::Event& event);

    void draw(sf::RenderWindow& window);

private:
    sf::RenderWindow& window;
    sf::Vector2f windowSize;
    sf::Font font;
    sf::Font fontBold;
    sf::Text title;
    sf::RoundedRectangleShape botButton;
    sf::RoundedRectangleShape stockfishButton;
    sf::RoundedRectangleShape lc0Button;
    sf::Text botText;
    sf::Text stockfishText;
    sf::Text lc0Text;
    sf::RectangleShape backdrop;
};
