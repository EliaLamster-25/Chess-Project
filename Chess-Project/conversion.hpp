#pragma once
#include <string>

// Convert file/rank to square index
inline int toSquare(int file, int rank) {
    return rank * 8 + file;
}

// Extract file (0–7) from square index
inline int toFile(int square) {
    return square % 8;
}

// Extract rank (0–7) from square index
inline int toRank(int square) {
    return square / 8;
}

// Convert square to algebraic notation ("e2", "h8")
inline std::string squareToString(int square) {
    char fileChar = 'a' + toFile(square);
    char rankChar = '1' + toRank(square);
    return std::string{ fileChar, rankChar };
}

// Flip a file character horizontally (a<->h, b<->g, ...). Case preserved.
inline char flipFileChar(char file) {
    if (file >= 'a' && file <= 'h') return static_cast<char>('h' - (file - 'a'));
    if (file >= 'A' && file <= 'H') return static_cast<char>('H' - (file - 'A'));
    return file;
}

// Flip a single algebraic square ("e2") horizontally.
inline std::string flipSquareHoriz(std::string_view sq2) {
    if (sq2.size() != 2) return std::string(sq2);
    char f = flipFileChar(sq2[0]);
    char r = sq2[1]; // rank unchanged
    return std::string{ f, r };
}

inline std::string flipMoveHoriz(std::string_view uci) {
    if (uci.size() < 4) return std::string(uci);

    std::string from = flipSquareHoriz(uci.substr(0, 2));
    std::string to = flipSquareHoriz(uci.substr(2, 2));

    if (uci.size() >= 5) {
        // Preserve promotion character as-is
        return from + to + std::string(1, uci[4]);
    }
    return from + to;
}