#pragma once
#include <SFML/Graphics.hpp>
#include <unordered_map>
#include <vector>
#include <memory>
#include <string>

class ChessPiece;

class textures {
public:
    void loadTextures(std::vector<std::unique_ptr<ChessPiece>>& pieces);

    void externalPush(std::vector<std::unique_ptr<ChessPiece>>& pieces,
        const std::string& type,
        bool isWhite,
        int square, std::string key);

    template <typename T>
    void push(std::vector<std::unique_ptr<ChessPiece>>& pieces,
        bool isWhite,
        int square,
        const std::string& textureKey)
    {
        sf::VideoMode desktopMode = sf::VideoMode::getDesktopMode();
        float squareWidth = desktopMode.size.x / 8.0f;
        float squareHeight = desktopMode.size.y / 8.0f;

        sf::Vector2u imageSize = loadedTextures.at(textureKey).getSize();
        float imageScale = std::min(
            squareWidth / static_cast<float>(imageSize.x),
            squareHeight / static_cast<float>(imageSize.y)
        );

        // Avoid duplicate pieces on the same square
        for (auto& p : pieces) {
            if (p->getSquare() == square && p->isWhite() == isWhite) return;
        }

        // Construct the piece using the template type T
        auto piece = std::make_unique<T>(
            isWhite,
            square,
            loadedTextures.at(textureKey)
        );

        piece->setScale(imageScale, imageScale);
        pieces.push_back(std::move(piece));
    }

private:
    std::vector<std::string> piecesToLoad{ "Bishop", "Queen", "Pawn", "King", "Knight", "Rook"};
    std::unordered_map<std::string, sf::Texture> loadedTextures;
};

struct PiecePlacement {
    std::string type;
    bool isWhite;
    std::vector<int> squares;
    std::string textureKey;
};

inline std::vector<PiecePlacement> initialPlacements = {
    // White pieces
    {"Rook",   true,  {0, 7},   "whiteRook"},
    {"Knight", true,  {1, 6},   "whiteKnight"},
    {"Bishop", true,  {2, 5},   "whiteBishop"},
    {"Queen",  true,  {4},      "whiteQueen"},
    {"King",   true,  {3},      "whiteKing"},
    {"Pawn",   true,  {8, 9, 10, 11, 12, 13, 14, 15}, "whitePawn"},

    // Black pieces
    {"Rook",   false, {56, 63}, "blackRook"},
    {"Knight", false, {57, 62}, "blackKnight"},
    {"Bishop", false, {58, 61}, "blackBishop"},
    {"Queen",  false, {60},     "blackQueen"},
    {"King",   false, {59},     "blackKing"},
    {"Pawn",   false, {48, 49, 50, 51, 52, 53, 54, 55}, "blackPawn"}
};
