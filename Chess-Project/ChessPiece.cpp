#include <cmath>
#include <iostream>
#include <memory>
#include <string_view>
#include <SFML/Graphics.hpp>
#include <SFML/System/Clock.hpp>
#include <algorithm>
#include <array>

#include "ChessPiece.hpp"
#include "positions.hpp"
#include "conversion.hpp"
#include "Network.hpp"   // already present

static const std::string_view whiteifyGlowFrag = std::string_view(R"(
#version 120
uniform sampler2D texture;
uniform vec4 glowColor;
void main()
{
    vec4 pixel = texture2D(texture, gl_TexCoord[0].xy);
    float alpha = pixel.a;
    vec3 finalColor = glowColor.rgb;
    gl_FragColor = vec4(finalColor, alpha * glowColor.a);
}
)");

bool ChessPiece::whiteTurn = false;
int ChessPiece::enPassantTargetSquare = -1;

// Global clock for simple animation timing
static sf::Clock gAnimClock;

// Helper: map logical square to display square when horizontally flipped (mirror files only)
static inline int mapSquare(int sq, bool flip) {
    if (!flip) return sq;
    int file = sq % 8;
    int rank = sq / 8;
    return rank * 8 + (7 - file);
}

sf::Vector2f ChessPiece::getCenterFromSquare(int sq, sf::Vector2u boardSize, int offsetX) {
    int file = sq % 8;
    int rank = sq / 8;
    constexpr int kLabelPad = 28;
    const float rectW = static_cast<float>((boardSize.y - 2 * kLabelPad) / 8);
    const float rectH = rectW;
    const float left = static_cast<float>(offsetX + kLabelPad);
    const float top = static_cast<float>(kLabelPad);
    const float squareX = left + rectW * file;
    const float squareY = top + rectH * (7 - rank);
    return { squareX + rectW / 2.f, squareY + rectH / 2.f };
}

ChessPiece::ChessPiece(bool isWhite, PieceType type, int square, const sf::Texture& texture)
    : white(isWhite), type(type), square(square), sprite(texture), capturedSquare(-1)
{
    sprite.setTexture(texture);
    sprite.setOrigin(sf::Vector2f(texture.getSize().x / 2.f, texture.getSize().y / 2.f));

    // Match board perspective: mirror horizontally for multiplayer client, never in botmatch
    bool flip = (!isNetworkHost.load(std::memory_order_acquire)
        && !isBotMatch.load(std::memory_order_acquire));
    int disp = mapSquare(square, flip);
    int file = disp % 8;
    int rank = disp / 8;

    float cellSize = 80.f;
    float x = file * cellSize + cellSize / 2.f;
    float y = (7 - rank) * cellSize + cellSize / 2.f;
    sprite.setPosition(sf::Vector2f(x, y));
    sprite.setScale(sf::Vector2f(0.5f, 0.5f));
}

void ChessPiece::draw(sf::RenderWindow& window, sf::Vector2u boardSize, int offsetX) {
    int sq = getSquare();
    if (sq < 0 || sq >= 64) return;

    constexpr int kLabelPad = 28;
    const float rectW = static_cast<float>((boardSize.y - 2 * kLabelPad) / 8);

    // Handle slide animation (interpolate position if sliding)
    sf::Vector2f pos;
    if (sliding) {
        float t = slideClock.getElapsedTime().asSeconds() / std::max(0.0001f, slideDuration);
        if (t >= 1.0f) {
            t = 1.0f;
            setSquare(slideToSquare);  // End slide
            sliding = false;
            slideDuration = 0.0f;
            slideFromPixel = false;
        }
        // From: either drop pixel or center of from-square
        const sf::Vector2f fromCenter = slideFromPixel
            ? slideFromPos
            : getCenterFromSquare(slideFromSquare, boardSize, offsetX);
        const sf::Vector2f toCenter = getCenterFromSquare(slideToSquare, boardSize, offsetX);
        pos = fromCenter + t * (toCenter - fromCenter);
    } else {
        pos = getCenterFromSquare(sq, boardSize, offsetX);  // Normal pos
    }

    // Compute base scale
    float baseScale = (rectW * 0.6f) / 100.f;
    if (getType() == PieceType::Queen || getType() == PieceType::King) {
        baseScale = (rectW * 0.7f) / 100.f;
    }

    // Scale tween
    if (scaleDuration > 0.0f) {
        float t = scaleClock.getElapsedTime().asSeconds() / std::max(0.0001f, scaleDuration);
        t = std::min(1.0f, t);
        currentScale = startScale + t * (targetScale - startScale);
        if (t >= 1.0f) scaleDuration = 0.0f;
    } else {
        currentScale = baseScale;
    }

    getSprite().setPosition(pos);
    getSprite().setScale(sf::Vector2f(currentScale, currentScale));
    window.draw(getSprite());
}

void ChessPiece::setSquare(int sq) { square = sq; }
int  ChessPiece::getSquare() const { return square; }
bool ChessPiece::isWhite() const { return white; }
sf::Sprite& ChessPiece::getSprite() { return sprite; }
const sf::Sprite& ChessPiece::getSprite() const { return sprite; }

bool ChessPiece::containsPoint(const sf::Vector2f& point) const {
    return sprite.getGlobalBounds().contains(point);
}

std::string ChessPiece::pieceTypeToString(PieceType type) {
    switch (type) {
    case PieceType::King:   return "King";
    case PieceType::Queen:  return "Queen";
    case PieceType::Rook:   return "Rook";
    case PieceType::Bishop: return "Bishop";
    case PieceType::Knight: return "Knight";
    case PieceType::Pawn:   return "Pawn";
    default:                return "Unknown";
    }
}

std::vector<int> ChessPiece::getPossibleMoves(const std::vector<int>& boardState) const {
    std::vector<int> moves;
    for (int i = 0; i < 64; i++) {
        if (boardState[i] == 0) moves.push_back(i);
    }
    return moves;
}

void ChessPiece::drawPieceWithGlow(sf::RenderWindow& window,
    const sf::Sprite& sprite,
    sf::Color glow,
    int glowSteps,
    float maxScale,
    float minAlphaFraction)
{
    if (glowSteps <= 0) {
        window.draw(sprite);
        return;
    }

    static sf::Shader glowShader;
    static bool shaderLoaded = false;
    if (!shaderLoaded) {
        shaderLoaded = glowShader.loadFromMemory(std::string_view{}, std::string_view{}, whiteifyGlowFrag);
    }

    const sf::Texture* tex = &sprite.getTexture();
    if (!tex) {
        window.draw(sprite);
        return;
    }
    const sf::Vector2u texSize = tex->getSize();
    if (texSize.x == 0 || texSize.y == 0) {
        window.draw(sprite);
        return;
    }

    const float scaleX = sprite.getScale().x;
    const float scaleY = sprite.getScale().y;
    const float spritePxW = static_cast<float>(texSize.x) * scaleX;
    const float spritePxH = static_cast<float>(texSize.y) * scaleY;

    const float thicknessFactor = std::max(0.0f, maxScale - 1.0f);
    const float outerRadiusPx = 0.5f * std::max(spritePxW, spritePxH) * (thicknessFactor * 0.5f + 0.1f);

    const float pad = std::ceil(outerRadiusPx + 8.0f);
    const unsigned rtW = static_cast<unsigned>(std::ceil(spritePxW + 2.0f * pad));
    const unsigned rtH = static_cast<unsigned>(std::ceil(spritePxH + 2.0f * pad));

    sf::RenderTexture rt(sf::Vector2u(rtW, rtH));
    rt.clear(sf::Color::Transparent);
    rt.setSmooth(true);

    const sf::Vector2f rtCenter(rtW * 0.5f, rtH * 0.5f);

    sf::Sprite s = sprite;
    s.setOrigin(sf::Vector2f(texSize.x * 0.5f, texSize.y * 0.5f));
    s.setScale(sf::Vector2f(scaleX, scaleY));
    s.setPosition(rtCenter);

    sf::RenderStates glowStates;
    glowStates.blendMode = sf::BlendAlpha;
    if (shaderLoaded) {
        glowStates.shader = &glowShader;
    }

    constexpr float PI = 3.14159265358979323846f;
    constexpr int dirCount = 32;
    const float twoPi = 2.0f * PI;

    minAlphaFraction = std::clamp(minAlphaFraction, 0.0f, 1.0f);
    const float uniformWeight = (minAlphaFraction > 0.0f) ? minAlphaFraction : 1.0f;

    for (int ring = 1; ring <= glowSteps; ++ring) {
        const float t = std::clamp((static_cast<float>(ring) - 0.5f) / static_cast<float>(glowSteps), 0.0f, 1.0f);
        const float r = t * outerRadiusPx;

        sf::Color stepColor = glow;
        stepColor.a = static_cast<std::uint8_t>(static_cast<float>(glow.a) * uniformWeight);

        if (shaderLoaded) {
            glowShader.setUniform("glowColor", sf::Glsl::Vec4(
                stepColor.r / 255.f, stepColor.g / 255.f, stepColor.b / 255.f, stepColor.a / 255.f
            ));
        } else {
            s.setColor(stepColor);
        }

        const float angleOffset = (ring & 1) ? (PI / static_cast<float>(dirCount)) : 0.0f;

        for (int d = 0; d < dirCount; ++d) {
            const float angle = angleOffset + (twoPi * (static_cast<float>(d) / static_cast<float>(dirCount)));
            const sf::Vector2f offset(std::cos(angle) * r, std::sin(angle) * r);
            s.setPosition(rtCenter + offset);
            rt.draw(s, glowStates);
        }
    }

    rt.display();

    sf::Sprite halo(rt.getTexture());
    halo.setOrigin(sf::Vector2f(rtW * 0.5f, rtH * 0.5f));
    halo.setPosition(sprite.getPosition());
    halo.setScale(sf::Vector2f(1.0f, 1.0f));
   
    sf::RenderStates haloStates;
    haloStates.blendMode = sf::BlendAlpha;
    window.draw(halo, haloStates);

    window.draw(sprite);
}

void ChessPiece::startScaleAnimation(float fromScale, float toScale, float duration) {
    startScale   = fromScale;
    currentScale = fromScale;
    targetScale  = toScale;
    scaleDuration = duration;
    scaleClock.restart();
}

void ChessPiece::tickScaleOnly() {
    if (scaleDuration > 0.0f) {
        float t = scaleClock.getElapsedTime().asSeconds() / std::max(0.0001f, scaleDuration);
        if (t >= 1.0f) {
            t = 1.0f;
            currentScale = startScale + t * (targetScale - startScale);
            scaleDuration = 0.0f;
        } else {
            currentScale = startScale + t * (targetScale - startScale);
        }
    }
    sprite.setScale(sf::Vector2f(currentScale, currentScale));
}

void ChessPiece::beginSlide(int fromSq, int toSq, float duration) {
    slideFromSquare = fromSq;
    slideToSquare   = toSq;
    slideDuration   = duration;
    sliding         = true;

    // Start from the current pixel position (mouse drop), not the logical from-square center
    slideFromPos    = sprite.getPosition();
    slideFromPixel  = true;

    slideClock.restart();
}

bool ChessPiece::isSliding() const {
    return sliding;
}

ChessPiece::~ChessPiece() = default;