#pragma once
#include <SFML/Graphics.hpp>
#include <vector>
#include <memory>
#include <iostream>

enum class PieceType {
    King,
    Queen,
    Rook,
    Bishop,
    Knight,
    Pawn
};

class ChessPiece {
public:
    ChessPiece(bool isWhite, PieceType type, int square, const sf::Texture& texture);
    virtual ~ChessPiece();

    virtual void draw(sf::RenderWindow& window, sf::Vector2u boardSize, int offsetX);


    void setSquare(int sq);
    int getSquare() const;
    bool isWhite() const;
    PieceType getType() const { return type; }

    sf::Sprite& getSprite();
    const sf::Sprite& getSprite() const;

    bool containsPoint(const sf::Vector2f& point) const;

    std::string pieceTypeToString(PieceType type);

    virtual std::vector<int> getPossibleMoves(const std::vector<int>& boardState) const;

    int getCapturedSquare() const { return capturedSquare; }
    void setCapturedSquare(int sq) { capturedSquare = sq; }
    void resetCapturedSquare() { capturedSquare = -1; }

    void setScale(float x, float y) {
        sprite.setScale(sf::Vector2f(x, y));
    }

    static bool whiteTurn;
    static int enPassantTargetSquare;

    virtual bool isSlidingPiece() const { return false; }

    bool getHasMoved() const { return hasMoved; }
    void setHasMoved(bool moved) { hasMoved = moved; }

    void drawPieceWithGlow(sf::RenderWindow& window, const sf::Sprite& sprite, sf::Color glow, int glowSteps = 6, float maxScale = 1.4f, float minAlphaFraction = 0.1f);

protected:
    bool white;
    PieceType type;
    int square;
    sf::Sprite sprite;
    int capturedSquare = -1;
	bool hasMoved = false;
};
