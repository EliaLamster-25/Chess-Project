#pragma once
#include <SFML/Graphics.hpp>
#include <vector>
#include <memory>
#include <iostream>

enum class PieceType { Pawn, Knight, Bishop, Rook, Queen, King };

class ChessPiece {
public:
    sf::Vector2f getCenterFromSquare(int sq, sf::Vector2u boardSize, int offsetX);

    ChessPiece(bool isWhite, PieceType type, int square, const sf::Texture& texture);
    virtual ~ChessPiece();

    virtual void draw(sf::RenderWindow& window, sf::Vector2u boardSize, int offsetX);

    void setSquare(int sq);
    int getSquare() const;
    bool isWhite() const;
    PieceType getType() const { return type; }

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

    // Expose sprite (used by dragging path)
    sf::Sprite& getSprite();
    const sf::Sprite& getSprite() const;

    // Scale tween API
    void startScaleAnimation(float fromScale, float toScale, float duration);
    // For selected piece dragging: update only scale tween and apply to sprite (no position changes)
    void tickScaleOnly();

    // Slide tween API
    void beginSlide(int fromSquare, int toSquare, float durationSeconds = 0.18f);
    bool isSliding() const;

    // fields (public to match existing style)
    bool white;
    PieceType type;
    int square;
    sf::Sprite sprite;
    int capturedSquare = -1;
    bool hasMoved = false;

    // Tween state (per piece)
    sf::Clock scaleClock;   // scale animation clock
    sf::Clock slideClock;   // slide animation clock
    float startScale = 1.0f;
    float currentScale = 1.0f;
    float targetScale = 1.0f;
    float scaleDuration = 0.0f;

    bool sliding = false;
    int slideFromSquare = -1;
    int slideToSquare = -1;
    float slideDuration = 0.0f;

    // NEW: start slide from current pixel position to avoid jump on drop
    sf::Vector2f slideFromPos{};
    bool slideFromPixel = false;
};
