#pragma once
#include <iostream>
#include <string>
#include <unordered_map>

class Positions {
public:
    Positions();

    void setPiece(const std::string& name, int square);
    void clearPiece(const std::string& name, int square);
    void printBoard(const std::string& name) const;

private:
    std::unordered_map<std::string, uint64_t> pieces;
};
