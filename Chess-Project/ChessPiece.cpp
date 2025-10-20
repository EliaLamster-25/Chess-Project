#include <cmath>
#include <iostream>
#include <memory>
#include <string_view>
#include <SFML/Graphics.hpp>
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

// Helper: map logical square to display square when flipped
static inline int mapSquare(int sq, bool flip) {
    return flip ? (63 - sq) : sq;          // full 180° rotation
    // If you only wanted a horizontal mirror (files reversed):
    // int f = sq % 8, r = sq / 8;
    // return flip ? (r * 8 + (7 - f)) : sq;
}

ChessPiece::ChessPiece(bool isWhite, PieceType type, int square, const sf::Texture& texture)
    : white(isWhite), type(type), square(square), sprite(texture), capturedSquare(-1)
{
    sprite.setTexture(texture);
    sprite.setOrigin(sf::Vector2f(texture.getSize().x / 2.f, texture.getSize().y / 2.f));

    // Match board perspective: flip only for multiplayer client, never in botmatch
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

void ChessPiece::draw(sf::RenderWindow& window, sf::Vector2u boardSize, int offsetX)
{
    const float rectW = static_cast<float>(boardSize.y) / 8.f;
    const float rectH = rectW;
    const float stepX = rectW * 0.95f;
    const float stepY = rectH * 0.95f;
    const float left  = offsetX * 1.06f;
    const float top   = rectH * 0.20f;

    const int f = toFile(square);       // DO NOT flip: no (7 - f)
    const int r = toRank(square);
    const float x = left + f * stepX;
    const float y = top  + (7 - r) * stepY;

    const float cx = x + stepX / 2.f;
    const float cy = y + stepY / 2.f;

    // keep your scale logic if any
    sprite.setPosition({cx, cy});
    window.draw(sprite);
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
    // Pseudocode:
    // - Validate texture; prepare shader once (colorize to glowColor with source alpha)
    // - Compute on-screen sprite pixel size and a max glow radius (in pixels) from maxScale
    // - Create a render texture large enough to hold the sprite plus glow padding
    // - For ring from inner -> outer:
    //     - r = lerp(0, outerRadiusPx)
    //     - weight = minAlphaFraction + (1 - minAlphaFraction) * gaussian falloff
    //     - For multiple directions around the circle (staggered per ring):
    //         - offset = dir * r
    //         - draw the sprite at (center + offset) using additive blending and the glow shader
    // - Display the render texture and draw it to the window with alpha blending (halo)
    // - Draw the original sprite on top

    if (glowSteps <= 0) {
        window.draw(sprite);
        return;
    }

    static sf::Shader glowShader;
    static bool shaderLoaded = false;
    if (!shaderLoaded) {
        shaderLoaded = glowShader.loadFromMemory(std::string_view{}, std::string_view{}, whiteifyGlowFrag);
        // If shader fails, fall back to non-shader additive tinting
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

    // On-screen pixel size of the sprite
    const float scaleX = sprite.getScale().x;
    const float scaleY = sprite.getScale().y;
    const float spritePxW = static_cast<float>(texSize.x) * scaleX;
    const float spritePxH = static_cast<float>(texSize.y) * scaleY;

    // Interpret maxScale as how "thick" the glow can extend (relative to sprite size).
    // Default maxScale=1.4f -> ~20% of max dimension as outer radius.
    const float thicknessFactor = std::max(0.0f, maxScale - 1.0f);
    const float outerRadiusPx = 0.5f * std::max(spritePxW, spritePxH) * (thicknessFactor * 0.5f + 0.1f);

    // Pad around sprite to hold glow fully
    const float pad = std::ceil(outerRadiusPx + 8.0f);
    const unsigned rtW = static_cast<unsigned>(std::ceil(spritePxW + 2.0f * pad));
    const unsigned rtH = static_cast<unsigned>(std::ceil(spritePxH + 2.0f * pad));

    sf::RenderTexture rt(sf::Vector2u(rtW, rtH));
    rt.clear(sf::Color::Transparent);

    const sf::Vector2f rtCenter(rtW * 0.5f, rtH * 0.5f);

    // Prepare a sprite aligned to the RT center
    sf::Sprite s = sprite;
    s.setOrigin(sf::Vector2f(texSize.x * 0.5f, texSize.y * 0.5f));
    s.setScale(sf::Vector2f(scaleX, scaleY));
    s.setPosition(rtCenter);

    // Use additive blending for glow accumulation
    sf::RenderStates glowStates;
    glowStates.blendMode = sf::BlendAdd;
    if (shaderLoaded) {
        glowStates.shader = &glowShader;
    }

    // Softer falloff: Gaussian over radius, sampled with more directions and staggered angles
    constexpr float PI = 3.14159265358979323846f;
    constexpr int dirCount = 32; // more angular samples for smoothness
    const float twoPi = 2.0f * PI;

    // Gaussian sigma sized to produce a soft outer halo (tweakable)
    const float sigma = std::max(outerRadiusPx * 0.6f, 1.0f);
    minAlphaFraction = std::clamp(minAlphaFraction, 0.0f, 1.0f);

    for (int ring = 1; ring <= glowSteps; ++ring) {
        // Center the samples between 0..1 to avoid a hard outline near the outermost ring
        const float t = std::clamp((static_cast<float>(ring) - 0.5f) / static_cast<float>(glowSteps), 0.0f, 1.0f);
        const float r = t * outerRadiusPx;

        // Gaussian falloff by distance
        float g = std::exp(-(r * r) / (2.0f * sigma * sigma));
        float weight = minAlphaFraction + (1.0f - minAlphaFraction) * g;
        weight = std::clamp(weight, 0.0f, 1.0f);

        sf::Color stepColor = glow;
        stepColor.a = static_cast<std::uint8_t>(static_cast<float>(glow.a) * weight);

        if (shaderLoaded) {
            glowShader.setUniform("glowColor", sf::Glsl::Vec4(
                stepColor.r / 255.f, stepColor.g / 255.f, stepColor.b / 255.f, stepColor.a / 255.f
            ));
        } else {
            // Fallback: tint sprite color (still additive)
            s.setColor(stepColor);
        }

        // Stagger angles every other ring to break directional banding
        const float angleOffset = (ring & 1) ? (PI / static_cast<float>(dirCount)) : 0.0f;

        for (int d = 0; d < dirCount; ++d) {
            const float angle = angleOffset + (twoPi * (static_cast<float>(d) / static_cast<float>(dirCount)));
            const sf::Vector2f offset(std::cos(angle) * r, std::sin(angle) * r);
            s.setPosition(rtCenter + offset);
            rt.draw(s, glowStates);
        }
    }

    rt.display();

    // Draw halo (use alpha blending to preserve hue) then the original sprite on top
    sf::Sprite halo(rt.getTexture());
    halo.setOrigin(sf::Vector2f(rtW * 0.5f, rtH * 0.5f));
    halo.setPosition(sprite.getPosition());
    halo.setScale(sf::Vector2f(1.0f, 1.0f));

    sf::RenderStates haloStates;
    haloStates.blendMode = sf::BlendAlpha; // preserves piece colors
    window.draw(halo, haloStates);

    // Finally, draw the original piece
    window.draw(sprite);
}

ChessPiece::~ChessPiece() = default;

