#include "ChessBoard.hpp"

namespace {
    inline int logicalToDisplaySquare(int sq, bool blackPerspective) {
        // 180° rotation: file' = 7 - file, rank' = 7 - rank -> square' = 63 - sq
        return blackPerspective ? (63 - sq) : sq;
    }
    inline int logicalFile(int sq) { return sq % 8; }
    inline int logicalRank(int sq) { return sq / 8; }
}

namespace {
    struct BoardCache {
        int  rectW{0};
        int  rectH{0};
        int  offsetX{0};
        int  labelPad{0};
        bool blackPerspective{ false }; // rename for clarity (true = rotated 180°)

        sf::RectangleShape background;
        sf::VertexArray    squares{sf::PrimitiveType::Triangles};

        std::vector<sf::Text> fileLabelsTop;
        std::vector<sf::Text> fileLabelsBottom;
        std::vector<sf::Text> rankLabelsLeft;
        std::vector<sf::Text> rankLabelsRight;
    };

    inline bool paramsChanged(const BoardCache& c,
                              int w, int h, int ox, int pad, bool host) {
        return c.rectW != w || c.rectH != h || c.offsetX != ox ||
               c.labelPad != pad || c.blackPerspective != host;
    }

    inline void buildBoardCache(BoardCache& cache,
                                sf::Font& font,
                                int rectW, int rectH,
                                int offsetX, int labelPad,
                                bool blackPerspective) {
        cache.rectW = rectW;
        cache.rectH = rectH;
        cache.offsetX = offsetX;
        cache.labelPad = labelPad;
        cache.blackPerspective = blackPerspective;

        const float boardW = static_cast<float>(rectW * 8);
        const float boardH = static_cast<float>(rectH * 8);
        const float bgW = boardW + 2.f * static_cast<float>(labelPad);
        const float bgH = boardH + 2.f * static_cast<float>(labelPad);

        cache.background = sf::RectangleShape(sf::Vector2f(bgW, bgH));
        cache.background.setPosition(sf::Vector2f(static_cast<float>(offsetX), 0.0f));
        cache.background.setFillColor(sf::Color(160, 123, 90));

        const float w = static_cast<float>(rectW);
        const float h = static_cast<float>(rectH);
        const float baseX = static_cast<float>(offsetX + labelPad);
        const float baseY = static_cast<float>(labelPad);

        cache.squares.setPrimitiveType(sf::PrimitiveType::Triangles);
        cache.squares.resize(8 * 8 * 6);

        const sf::Color light(240, 217, 181);
        const sf::Color dark(181, 136, 99);

        for (int logical = 0; logical < 64; ++logical) {
            int displaySq = logicalToDisplaySquare(logical, blackPerspective);
            int file = logicalFile(displaySq);
            int rank = logicalRank(displaySq);

            const float squareX = baseX + w * static_cast<float>(file);
            const float squareY = baseY + h * static_cast<float>(7 - rank);

            const size_t base = static_cast<size_t>(logical * 6); // keep vertex grouping stable
            sf::Vertex* v = &cache.squares[base];

            const sf::Vector2f tl{ squareX, squareY };
            const sf::Vector2f tr{ squareX + w, squareY };
            const sf::Vector2f br{ squareX + w, squareY + h };
            const sf::Vector2f bl{ squareX, squareY + h };

            // Color pattern must use display file/rank parity
            const sf::Color c = ((rank + file) % 2 == 0) ? light : dark;

            v[0].position = tl; v[0].color = c;
            v[1].position = tr; v[1].color = c;
            v[2].position = br; v[2].color = c;
            v[3].position = tl; v[3].color = c;
            v[4].position = br; v[4].color = c;
            v[5].position = bl; v[5].color = c;
        }

        const float cell = static_cast<float>(rectW);
        const float boardBottomY = baseY + 8.f * static_cast<float>(rectH);
        const float boardLeftX = baseX;
        const float boardRightX = baseX + 8.f * cell;
        const unsigned int charSize = 20;
        const sf::Color labelColor(200, 200, 200, 255);

        cache.fileLabelsTop.clear();
        cache.fileLabelsBottom.clear();
        cache.rankLabelsLeft.clear();
        cache.rankLabelsRight.clear();

        cache.fileLabelsTop.reserve(8);
        cache.fileLabelsBottom.reserve(8);
        cache.rankLabelsLeft.reserve(8);
        cache.rankLabelsRight.reserve(8);

        // Files
        for (int displayFile = 0; displayFile < 8; ++displayFile) {
            int fileLetterIndex = blackPerspective ? displayFile : (7 - displayFile);
            // If you want black to see a-h left->right (normal from their POV), invert logic:
            // int fileLetterIndex = blackPerspective ? (7 - displayFile) : displayFile;
            // Choose one; below makes bottom perspective always a-h left->right for the side at bottom.
            char fileChar = static_cast<char>('a' + fileLetterIndex);
            std::string fileStr(1, fileChar);

            sf::Text bottom(font, fileStr);
            bottom.setCharacterSize(charSize);
            bottom.setFillColor(labelColor);
            sf::Text top = bottom;

            float xCenter = boardLeftX + cell * static_cast<float>(displayFile) + cell / 2.f;

            {
                sf::FloatRect b = bottom.getLocalBounds();
                bottom.setOrigin(sf::Vector2f(b.position.x + b.size.x / 2.f, b.position.y));
                bottom.setPosition(sf::Vector2f(xCenter, boardBottomY + 4.f));
            }
            {
                sf::FloatRect b = top.getLocalBounds();
                top.setOrigin(sf::Vector2f(b.position.x + b.size.x / 2.f, b.position.y + b.size.y));
                top.setPosition(sf::Vector2f(xCenter, baseY - 4.f));
            }

            cache.fileLabelsBottom.push_back(std::move(bottom));
            cache.fileLabelsTop.push_back(std::move(top));
        }

        // Ranks
        for (int displayRank = 0; displayRank < 8; ++displayRank) {
            int rankNumberIndex = blackPerspective ? (7 - displayRank) : displayRank;
            int rankNum = rankNumberIndex + 1;
            std::string rankStr = std::to_string(rankNum);

            sf::Text left(font, rankStr);
            left.setCharacterSize(charSize);
            left.setFillColor(labelColor);
            sf::Text right = left;

            float yCenter = baseY + h * static_cast<float>(7 - displayRank) + h / 2.f;

            {
                sf::FloatRect b = left.getLocalBounds();
                left.setOrigin(sf::Vector2f(b.size.x / 2.f, b.size.y / 2.f));
                left.setPosition(sf::Vector2f(static_cast<float>(offsetX) + static_cast<float>(labelPad) / 2.f, yCenter));
            }
            {
                sf::FloatRect b = right.getLocalBounds();
                right.setOrigin(sf::Vector2f(b.size.x / 2.f, b.size.y / 2.f));
                right.setPosition(sf::Vector2f(boardRightX + static_cast<float>(labelPad) / 2.f, yCenter));
            }

            cache.rankLabelsLeft.push_back(std::move(left));
            cache.rankLabelsRight.push_back(std::move(right));
        }
    }

} // namespace

void ChessBoard::draw(sf::RenderWindow& surface, sf::Vector2u size) {
    constexpr int LabelPad = 28;

    const int RectHeight = static_cast<int>((static_cast<int>(size.y) - 2 * LabelPad) / 8);
    const int RectWidth  = RectHeight;
    const int OffsetX    = static_cast<int>(size.x / 4);

    static sf::Font coordFont;
    static bool fontLoaded = false;
    //if (!fontLoaded) {
    //    if (!coordFont.openFromFile("assets/segoeuithibd.ttf")) {
    //        std::cerr << "Failed to load coordinate font\n";
    //    }
    //    fontLoaded = true;
    //}
    if (!coordFont.openFromMemory(segoeuithibd_ttf, segoeuithibd_ttf_len)) {
        std::cerr << "Failed to load Segoe UI font from memory\n";
        fontLoaded = true;
    }

    static BoardCache cache;

    // Only flip for multiplayer clients; never flip in bot matches
    bool blackPerspective = (!isNetworkHost.load(std::memory_order_acquire)
                             && !isBotMatch.load(std::memory_order_acquire)); // CHANGED

    if (paramsChanged(cache, RectWidth, RectHeight, OffsetX, LabelPad, blackPerspective)) {
        buildBoardCache(cache, coordFont, RectWidth, RectHeight, OffsetX, LabelPad, blackPerspective);
    }

    surface.draw(cache.background);
    surface.draw(cache.squares);

    for (auto& t : cache.fileLabelsBottom) surface.draw(t);
    for (auto& t : cache.fileLabelsTop)    surface.draw(t);
    for (auto& t : cache.rankLabelsLeft)   surface.draw(t);
    for (auto& t : cache.rankLabelsRight)  surface.draw(t);
}

static inline sf::Vector2f squareCenter(sf::Vector2u boardSize,
    int offsetX,
    int squareIndex,
    bool blackPerspective) {
    constexpr int LabelPad = 28;
    const int rectH = static_cast<int>((static_cast<int>(boardSize.y) - 2 * LabelPad) / 8);
    const int rectW = rectH;

    int displaySq = logicalToDisplaySquare(squareIndex, blackPerspective);
    int file = logicalFile(displaySq);
    int rank = logicalRank(displaySq);

    float baseX = static_cast<float>(offsetX + LabelPad);
    float baseY = static_cast<float>(LabelPad);
    float squareX = baseX + static_cast<float>(rectW * file);
    float squareY = baseY + static_cast<float>(rectH * (7 - rank));

    return { squareX + rectW / 2.f, squareY + rectH / 2.f };
}

void ChessBoard::drawRect(sf::RenderWindow& window, sf::Vector2u boardSize, float size, int offsetX, int squareIndex, sf::Color color) {
    (void)size; // currently unused

    // Only flip for multiplayer clients; never flip in bot matches
    bool blackPerspective = (!isNetworkHost.load(std::memory_order_acquire)
                             && !isBotMatch.load(std::memory_order_acquire)); // CHANGED

    constexpr int LabelPad = 28;
    const int rectH = static_cast<int>((static_cast<int>(boardSize.y) - 2 * LabelPad) / 8);
    const int rectW = rectH;

    sf::Vector2f center = squareCenter(boardSize, offsetX, squareIndex, blackPerspective);

    static sf::RectangleShape rect;
    rect.setSize(sf::Vector2f(static_cast<float>(rectW), static_cast<float>(rectH)));
    rect.setFillColor(color);
    rect.setOrigin(rect.getSize() / 2.f);
    rect.setPosition(center);

    window.draw(rect);
}

void ChessBoard::drawCircle(sf::RenderWindow& window, sf::Vector2u boardSize, float size, int offsetX, int squareIndex, sf::Color color) {
    // Only flip for multiplayer clients; never flip in bot matches
    bool blackPerspective = (!isNetworkHost.load(std::memory_order_acquire)
                             && !isBotMatch.load(std::memory_order_acquire));

    constexpr int LabelPad = 28;
    const int rectH = static_cast<int>((static_cast<int>(boardSize.y) - 2 * LabelPad) / 8);
    const int rectW = rectH;

    sf::Vector2f center = squareCenter(boardSize, offsetX, squareIndex, blackPerspective);

    constexpr int glowSteps = 16;
    constexpr float exponent = 2.5f;
    const float baseRadius = static_cast<float>(rectW) * 0.05f;

    static std::array<float, glowSteps + 1> tPow{};
    static bool tPowInit = false;
    if (!tPowInit) {
        for (int i = 0; i <= glowSteps; ++i) {
            float t = i / static_cast<float>(glowSteps);
            tPow[i] = std::pow(t, exponent);
        }
        tPowInit = true;
    }

    static sf::CircleShape circle;
    static bool circleInit = false;
    if (!circleInit) {
        circle.setPointCount(64);
        circleInit = true;
    }

    constexpr std::uint8_t alphaMin = 60;
    constexpr std::uint8_t alphaMax = 70;

    for (int i = 0; i <= glowSteps; ++i) {
        float radius = baseRadius + (baseRadius * size) * (glowSteps - i);
        circle.setRadius(radius);
        circle.setOrigin(sf::Vector2f(radius, radius));
        circle.setPosition(center);

        std::uint8_t alpha = static_cast<std::uint8_t>(alphaMin + (alphaMax - alphaMin) * tPow[static_cast<size_t>(i)]);
        circle.setFillColor(sf::Color(color.r, color.g, color.b, alpha));

        window.draw(circle);
    }
}
