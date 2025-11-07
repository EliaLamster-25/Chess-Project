#pragma once
#include <SFML/Graphics.hpp>
#include <vector>
#include <memory>
#include <iostream>
#include "json.hpp"
#include "assets/segoeuithibd_ttf.h"

using json = nlohmann::json;

class botOverlay {
	public:

	void draw(sf::RenderWindow& window, sf::Vector2u size, json botInfo);
};