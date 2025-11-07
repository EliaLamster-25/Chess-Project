#include "botOverlay.hpp"
#include <vector>
#include <sstream>
#include <algorithm>
#include <cctype>

void botOverlay::draw(sf::RenderWindow& window, sf::Vector2u size, json botInfo) {

    static sf::Font coordFont;
    static bool fontReady = false;

    // Cache last known values so we don't fall back to "-" when info is missing
    static std::string lastDepth, lastSelDepth, lastNps, lastNodes, lastTime, lastHashfull, lastTbHits;
    static std::string lastScoreStr, lastPvStr;

    // Load the font once; do NOT reload every frame
    if (!fontReady) {
        if (!coordFont.openFromMemory(segoeuithibd_ttf, segoeuithibd_ttf_len)) {
            std::cerr << "Failed to load Segoe UI font from memory\n";
            fontReady = false;
        }
        else {
            fontReady = true;
        }
    }

    if (!fontReady) return;

    if (botInfo.empty()) {
        if (lastDepth.empty() && lastSelDepth.empty() && lastNps.empty() && lastNodes.empty() &&
            lastTime.empty() && lastHashfull.empty() && lastTbHits.empty() &&
            lastScoreStr.empty() && lastPvStr.empty()) {
            return; // nothing to draw yet
        }
    }

    // Ensure UI uses default view and draws on top
    auto oldView = window.getView();
    window.setView(window.getDefaultView());

    // Panel/layout constants
    const float panelX = 10.f;
    const float panelY = 10.f;
    const float panelW = 260.f;
    const float minPanelH = 200.f;
    const float innerLeftPad = 10.f;
    const float innerRightPad = 10.f;
    const float lineHeight = 20.f;

    const float x = panelX + innerLeftPad; // 20.f
    float y = 20.f;

    // Safe extractors
    auto toStr = [](const json& v) -> std::string {
        if (v.is_string()) return v.get<std::string>();
        if (v.is_number_integer()) return std::to_string(v.get<long long>());
        if (v.is_number_unsigned()) return std::to_string(v.get<unsigned long long>());
        if (v.is_number_float()) return std::to_string(v.get<double>());
        if (v.is_boolean()) return v.get<bool>() ? "true" : "false";
        return v.dump();
    };

    auto getStr = [&](const json& j, const char* key, const std::string& def = "-") -> std::string {
        auto it = j.find(key);
        if (it == j.end()) return def;
        return toStr(*it);
    };

    auto getOrKeep = [&](const char* key, std::string& cache) -> std::string {
        std::string v = getStr(botInfo, key, "");
        if (!v.empty()) cache = v;
        return cache;
    };

    // Compose values with caching
    const std::string depth = getOrKeep("depth", lastDepth);
    const std::string selDepth = getOrKeep("seldepth", lastSelDepth);
    const std::string nps = getOrKeep("nps", lastNps);
    const std::string nodes = getOrKeep("nodes", lastNodes);
    const std::string time = getOrKeep("time", lastTime);
    const std::string hashfull = getOrKeep("hashfull", lastHashfull);
    const std::string tbHits = getOrKeep("tbhits", lastTbHits);

    // Score caching
    if (botInfo.contains("score") && botInfo["score"].is_object()) {
        const auto& s = botInfo["score"];
        const std::string type = getStr(s, "type", "");
        const std::string val = getStr(s, "value", "");
        if (!type.empty() || !val.empty()) {
            lastScoreStr = type.empty() ? val : (val.empty() ? type : (type + ":" + val));
        }
    }
    const std::string scoreStr = lastScoreStr;

    // PV caching (keep ALL moves; do not overwrite with shorter 1-move lines after the move is done)
    if (botInfo.contains("pv") && botInfo["pv"].is_array() && !botInfo["pv"].empty()) {
        std::string joined;
        bool first = true;
        for (const auto& mv : botInfo["pv"]) {
            if (!first) joined += ' ';
            first = false;
            joined += toStr(mv);
        }
        if (!joined.empty()) {
            auto tokenCount = [](const std::string& s) -> size_t {
                size_t cnt = 0;
                bool inTok = false;
                for (unsigned char c : s) {
                    if (std::isspace(c)) {
                        if (inTok) inTok = false;
                    } else {
                        if (!inTok) { inTok = true; ++cnt; }
                    }
                }
                return cnt;
            };
            auto firstToken = [](const std::string& s) -> std::string {
                size_t i = 0;
                while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i;
                size_t j = i;
                while (j < s.size() && !std::isspace(static_cast<unsigned char>(s[j]))) ++j;
                return s.substr(i, j - i);
            };

            if (lastPvStr.empty()) {
                lastPvStr = joined;
            } else {
                const size_t newCnt = tokenCount(joined);
                const size_t oldCnt = tokenCount(lastPvStr);
                const std::string newFirst = firstToken(joined);
                const std::string oldFirst = firstToken(lastPvStr);

                // Update if:
                // - PV line changed (different first move), or
                // - new PV is at least as long as what we already have
                if (newFirst != oldFirst || newCnt >= oldCnt) {
                    lastPvStr = joined;
                }
                // else: keep the previous (longer) PV so it doesn't collapse to a single move
            }
        }
    }
    const std::string pvStr = lastPvStr;

    // Helpers
    auto makeText = [&](const std::string& s) {
        sf::Text t(coordFont, s);
        t.setCharacterSize(16);
        t.setFillColor(sf::Color(240, 240, 240, 255));
        t.setPosition(sf::Vector2f(x, y));
        y += lineHeight;
        return t;
    };

    auto formatThousandsDots = [](const std::string& s) -> std::string {
        if (s.empty()) return s;
        for (char c : s) {
            if (!std::isdigit(static_cast<unsigned char>(c))) {
                return s; // non-integer content; return as-is
            }
        }
        std::string out;
        out.reserve(s.size() + s.size() / 3);
        int count = 0;
        for (int i = static_cast<int>(s.size()) - 1; i >= 0; --i) {
            out.push_back(s[static_cast<size_t>(i)]);
            if (++count == 3 && i > 0) {
                out.push_back('.');
                count = 0;
            }
        }
        std::reverse(out.begin(), out.end());
        return out;
    };

    auto wrapPvLines = [&](const std::string& prefix, const std::string& content, float maxWidth) -> std::vector<std::string> {
        std::vector<std::string> lines;

        sf::Text measure(coordFont);
        measure.setCharacterSize(16);

        // If there is no content, just return the prefix
        if (content.empty()) {
            lines.push_back(prefix);
            return lines;
        }

        const std::string contPrefix = "       "; // indent continuation lines (increased by 2 spaces)

        std::string current = prefix;
        if (!current.empty() && current.back() != ' ') current += ' ';
        measure.setString(current);

        std::istringstream iss(content);
        std::string tok;
        while (iss >> tok) {
            std::string candidate = current + tok;
            measure.setString(candidate);
            if (measure.getLocalBounds().size.x <= maxWidth) {
                current = candidate + ' ';
            }
            else {
                // trim trailing space and push current line
                if (!current.empty() && current.back() == ' ') current.pop_back();
                lines.push_back(current);
                // start new line with indented token
                current = contPrefix + tok + ' ';
                measure.setString(current);
            }
        }

        if (!current.empty()) {
            if (current.back() == ' ') current.pop_back();
            lines.push_back(current);
        }

        return lines;
    };

    // Build base lines
    const std::string depthLine = "Depth: " + depth + " (sel " + selDepth + ")";
    const std::string timeLine = "Time: " + time + " ms";
    const std::string scoreLine = "Score: " + scoreStr;
    const std::string nodesLine = "Nodes: " + formatThousandsDots(nodes);
    const std::string hashTbLine = "Hashfull: " + hashfull + "  TB: " + tbHits;

    // Compute available text width inside the panel
    const float availableWidth = panelW - (x - panelX) - innerRightPad; // 260 - 10 - 10 = 240

    // Wrap PV correctly: prefix separate from content
    const auto pvLines = wrapPvLines("PV:", pvStr, availableWidth);

    // Prepare all lines for drawing
    std::vector<std::string> lines;
    lines.reserve(5 + pvLines.size());
    lines.push_back(depthLine);
    lines.push_back(timeLine);
    lines.push_back(nodesLine);
    lines.push_back(scoreLine);
    lines.push_back(hashTbLine);
    for (const auto& l : pvLines) lines.push_back(l);

    // Dynamic panel height (keep at least the original height)
    const float panelH = std::max(minPanelH, 20.f + static_cast<float>(lines.size()) * lineHeight + 20.f);

    // Panel
    sf::RectangleShape bg(sf::Vector2f(panelW, panelH));
    bg.setPosition(sf::Vector2f(panelX, panelY));
    bg.setFillColor(sf::Color(0, 0, 0, 220));
    bg.setOutlineThickness(2.f);
    bg.setOutlineColor(sf::Color(255, 255, 255, 200));
    window.draw(bg);

    // Draw all lines
    y = 20.f;
    for (const auto& s : lines) {
        window.draw(makeText(s));
    }

    // Restore previous view
    window.setView(oldView);
}