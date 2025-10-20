#pragma once
#include <vector>
#include "ChessPiece.hpp"


struct EscapeMove {
    int from;
    int to;
};

class CheckCheckmate {
public:
    static bool isInCheck(const std::vector<int>& boardState, const std::vector<ChessPiece*>& pieces, bool whiteTurn);
    static std::vector<std::pair<int, int>> getEscapeMoves(const std::vector<int>& boardState, const std::vector<ChessPiece*>& pieces, bool whiteTurn);
    static bool isCheckmate(const std::vector<int>& boardState, const std::vector<ChessPiece*>& pieces, bool whiteTurn);

private:
    // Helpers
    static int findKingSquare(const std::vector<ChessPiece*>& pieces, bool whiteTurn);
    static std::vector<int> getAllAttacks(const std::vector<int>& boardState, const std::vector<ChessPiece*>& pieces, bool enemyColor);
};

