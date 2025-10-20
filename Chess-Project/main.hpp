#pragma once

#include <iostream>
#include <SFML/Graphics.hpp>
#include <SFML/Window.hpp>
#include <SFML/Window/Mouse.hpp>
#include <unordered_set>
#include <memory>
#include <vector>
#include <optional>
#include <algorithm>
#include <sstream>
#include <cctype>
#include <string>
#include <thread>
#include <atomic>
#include <mutex>
#include <queue>
#include <chrono>
#include <SFML/Network.hpp>

#include "ChessBoard.hpp"
#include "ChessPiece.hpp"
#include "positions.hpp"
#include "Pawn.hpp"
#include "conversion.hpp"
#include "textures.hpp"
#include "Check-CheckMate.hpp"
#include "mainMenu.hpp"
#include "json.hpp"
#include "Network.hpp"

using json = nlohmann::json;

std::vector<std::unique_ptr<ChessPiece>>& getPieces();
const std::vector<std::unique_ptr<ChessPiece>>& getPiecesConst();

std::vector<int> getPath(int from, int to);

int main();
