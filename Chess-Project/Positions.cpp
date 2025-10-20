#include "positions.hpp"

Positions::Positions() {}

void Positions::setPiece(const std::string& name, int square) {
    if (square >= 0 && square < 64) {
        pieces[name] |= (1ULL << square);
        printBoard(name);
    }
}

void Positions::clearPiece(const std::string& name, int square) {
    if (square >= 0 && square < 64) {
        pieces[name] &= ~(1ULL << square);
        printBoard(name);
    }
}

void Positions::printBoard(const std::string& name) const {
    auto it = pieces.find(name);
    if (it == pieces.end()) {
        std::cout << name << " not found.\n";
        return;
    }
    uint64_t piece = it->second;

    std::cout << "Board for " << name << ":\n";
    for (int rank = 7; rank >= 0; --rank) {
        for (int file = 0; file < 8; ++file) {
            int sq = rank * 8 + file;
            std::cout << ((piece >> sq) & 1ULL) << " ";
        }
        std::cout << "\n";
    }
    std::cout << "\n";
}
