#include "textures.hpp"
#include "ChessPiece.hpp"
#include "Bishop.hpp"
#include "King.hpp"
#include "Knight.hpp"
#include "Pawn.hpp"
#include "Queen.hpp"
#include "Rook.hpp"
#include "conversion.hpp"
#include <iostream>
#include "assets/chessPieces/ChessPiecesUmbrella.hpp"

struct TextureData {
    const void* data;
    size_t size;
};

const std::unordered_map<std::string, TextureData> textureMap = {
    { "blackBishop", { blackBishop_png, blackBishop_png_len } },
    { "blackKing", { blackKing_png, blackKing_png_len } },
    { "blackKnight", { blackKnight_png, blackKnight_png_len } },
    { "blackPawn", { blackPawn_png, blackPawn_png_len } },
    { "blackQueen", { blackQueen_png, blackQueen_png_len } },
    { "blackRook", { blackRook_png, blackRook_png_len } },
    { "whiteBishop", { whiteBishop_png, whiteBishop_png_len } },
    { "whiteKing", { whiteKing_png, whiteKing_png_len } },
    { "whiteKnight", { whiteKnight_png, whiteKnight_png_len } },
    { "whitePawn", { whitePawn_png, whitePawn_png_len } },
    { "whiteQueen", { whiteQueen_png, whiteQueen_png_len } },
    { "whiteRook", { whiteRook_png, whiteRook_png_len } },

};

void textures::loadTextures(std::vector<std::unique_ptr<ChessPiece>>& pieces) {

    for (size_t i = 0; i < piecesToLoad.size(); ++i) {
        std::string pieceNameWhite = "white" + piecesToLoad[i];
        std::string pieceNameBlack = "black" + piecesToLoad[i];

        auto whiteTextureMap = textureMap.find(pieceNameWhite);
        auto blackTextureMap = textureMap.find(pieceNameBlack);

        if (whiteTextureMap == textureMap.end() || blackTextureMap == textureMap.end()) {
            std::cerr << "Missing texture data for " << piecesToLoad[i] << std::endl;
            continue;
        }

        sf::Texture whiteTexture, blackTexture;

        if (!whiteTexture.loadFromMemory(whiteTextureMap->second.data, whiteTextureMap->second.size)) {
            std::cerr << "Failed to load texture: " << whiteTextureMap->second.data << std::endl;
            continue;
        }
        if (!blackTexture.loadFromMemory(blackTextureMap->second.data, blackTextureMap->second.size)) {
            std::cerr << "Failed to load texture: " << blackTextureMap->second.data << std::endl;
            continue;
        }

        whiteTexture.setSmooth(true);
        blackTexture.setSmooth(true);

        loadedTextures[pieceNameWhite] = whiteTexture;
        loadedTextures[pieceNameBlack] = blackTexture;
    }
    for (size_t i = 0; i < piecesToLoad.size(); ++i) {
        for (const auto& placement : initialPlacements) {

            if (loadedTextures.count(placement.textureKey) == 0) {continue;}

            if (placement.type == "Pawn") {
                for (int square : placement.squares) {
                    this->push<Pawn>(pieces, placement.isWhite, square, placement.textureKey);
                }
            }
            else if (placement.type == "Rook") {
                for (int square : placement.squares) {
                    this->push<Rook>(pieces, placement.isWhite, square, placement.textureKey);
                }
            }
            else if (placement.type == "Knight") {
                for (int square : placement.squares) {
                    this->push<Knight>(pieces, placement.isWhite, square, placement.textureKey);
                }
            }
            else if (placement.type == "Bishop") {
                for (int square : placement.squares) {
                    this->push<Bishop>(pieces, placement.isWhite, square, placement.textureKey);
                }
            }
            else if (placement.type == "Queen") {
                this->push<Queen>(pieces, placement.isWhite, placement.squares[0], placement.textureKey);
            }
            else if (placement.type == "King") {
                this->push<King>(pieces, placement.isWhite, placement.squares[0], placement.textureKey);
            }
        }
    }

}

void textures::externalPush(std::vector<std::unique_ptr<ChessPiece>>& pieces,
    const std::string& type,
    bool isWhite,
    int square, std::string key)
{
    //std::string key = (isWhite ? "white" : "black") + type;
    //if (loadedTextures.find(key) == loadedTextures.end()) {
    //    throw std::runtime_error("Texture not loaded: " + key);
    //}

    if (type == "Pawn") {
        push<Pawn>(pieces, isWhite, square, key);
    }
    else if (type == "Rook") {
        push<Rook>(pieces, isWhite, square, key);
    }
    else if (type == "Knight") {
        push<Knight>(pieces, isWhite, square, key);
    }
    else if (type == "Bishop") {
        push<Bishop>(pieces, isWhite, square, key);
    }
    else if (type == "Queen") {
        push<Queen>(pieces, isWhite, square, key);
    }
    else if (type == "King") {
        push<King>(pieces, isWhite, square, key);
    }
    else {
        throw std::runtime_error("Unknown piece type: " + type);
    }
}