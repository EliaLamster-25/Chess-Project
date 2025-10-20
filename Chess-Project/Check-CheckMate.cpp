#include <vector>
#include <algorithm>
#include <iostream>
#include <utility>
#include <cstddef>
#include <array>
#include <unordered_map>

#include "Check-CheckMate.hpp"
#include "ChessPiece.hpp"
#include "conversion.hpp"

// Helpers in anonymous namespace
namespace {

inline int colorSign(bool isWhite) { return isWhite ? 1 : -1; }

inline bool inBounds(int sq) { return sq >= 0 && sq < 64; }
inline int fileOf(int sq) { return sq % 8; }
inline int rankOf(int sq) { return sq / 8; }

// Build a dense map square -> piece* for quick lookups
inline std::array<ChessPiece*, 64> buildMap(const std::vector<ChessPiece*>& pieces) {
    std::array<ChessPiece*, 64> m{};
    m.fill(nullptr);
    for (auto* p : pieces) {
        if (!p) continue;
        int s = p->getSquare();
        if (s >= 0 && s < 64) m[s] = p;
    }
    return m;
}

// Check if 'targetSq' is attacked by 'byWhite' side, using only piece identities and board occupancy
bool isSquareAttackedByColor(int targetSq, bool byWhite, const std::array<ChessPiece*, 64>& map) {
    if (!inBounds(targetSq)) return false;
    const int tf = fileOf(targetSq);
    const int tr = rankOf(targetSq);

    // 1) Knights
    static const int knightOffsets[8] = { 15, 17, -15, -17, 6, -6, 10, -10 };
    for (int off : knightOffsets) {
        int sq = targetSq + off;
        if (!inBounds(sq)) continue;
        int df = std::abs(fileOf(sq) - tf);
        int dr = std::abs(rankOf(sq) - tr);
        if (!((df == 1 && dr == 2) || (df == 2 && dr == 1))) continue; // guard wrap-around
        if (auto* p = map[sq]) {
            if (p->isWhite() == byWhite && p->getType() == PieceType::Knight) return true;
        }
    }

    // 2) Sliding pieces (Bishop/Queen diagonals, Rook/Queen orthogonals)
    static const int diagDirs[4] = { 9, 7, -9, -7 };
    static const int orthoDirs[4] = { 8, -8, 1, -1 };

    // Diagonals
    for (int d : diagDirs) {
        int sq = targetSq;
        while (true) {
            int prevFile = fileOf(sq);
            sq += d;
            if (!inBounds(sq)) break;
            int curFile = fileOf(sq);
            // If we wrapped files incorrectly for horizontal step (7 or -7/9 or -9 crosses files too far)
            if (std::abs(curFile - prevFile) != 1) break;

            if (auto* p = map[sq]) {
                if (p->isWhite() == byWhite && (p->getType() == PieceType::Bishop || p->getType() == PieceType::Queen)) {
                    return true;
                }
                break; // blocked
            }
        }
    }

    // Orthogonals
    for (int d : orthoDirs) {
        int sq = targetSq;
        while (true) {
            int prevFile = fileOf(sq);
            sq += d;
            if (!inBounds(sq)) break;
            int curFile = fileOf(sq);
            // Horizontal wrap check for left/right
            if ((d == 1 || d == -1) && std::abs(curFile - prevFile) != 1) break;

            if (auto* p = map[sq]) {
                if (p->isWhite() == byWhite && (p->getType() == PieceType::Rook || p->getType() == PieceType::Queen)) {
                    return true;
                }
                break; // blocked
            }
        }
    }

    // 3) Pawns (note: attack directions only)
    if (byWhite) {
        // White pawns attack +7 (left diag) and +9 (right diag)
        if (tf > 0) {
            int sq = targetSq - 9; // reverse from attacker perspective: attacker at targetSq-9 attacks targetSq
            if (inBounds(sq)) {
                if (auto* p = map[sq]) if (p->isWhite() == byWhite && p->getType() == PieceType::Pawn) return true;
            }
        }
        if (tf < 7) {
            int sq = targetSq - 7;
            if (inBounds(sq)) {
                if (auto* p = map[sq]) if (p->isWhite() == byWhite && p->getType() == PieceType::Pawn) return true;
            }
        }
    } else {
        // Black pawns attack -7 and -9 (from higher ranks downwards)
        if (tf > 0) {
            int sq = targetSq + 7;
            if (inBounds(sq)) {
                if (auto* p = map[sq]) if (p->isWhite() == byWhite && p->getType() == PieceType::Pawn) return true;
            }
        }
        if (tf < 7) {
            int sq = targetSq + 9;
            if (inBounds(sq)) {
                if (auto* p = map[sq]) if (p->isWhite() == byWhite && p->getType() == PieceType::Pawn) return true;
            }
        }
    }

    // 4) Opposing king (adjacent squares). Important for king moves filtering.
    for (int dr = -1; dr <= 1; ++dr) {
        for (int df = -1; df <= 1; ++df) {
            if (dr == 0 && df == 0) continue;
            int nf = tf + df, nr = tr + dr;
            if (nf < 0 || nf > 7 || nr < 0 || nr > 7) continue;
            int nsq = nr * 8 + nf;
            if (auto* p = map[nsq]) {
                if (p->isWhite() == byWhite && p->getType() == PieceType::King) return true;
            }
        }
    }

    return false;
}

// Generate pseudo-legal moves for a given piece (ignores self-check).
// Uses board (1 for white, -1 for black, 0 empty) and square map.
std::vector<int> generatePseudoLegalMoves(const ChessPiece* p,
                                          const std::vector<int>& board,
                                          const std::array<ChessPiece*, 64>& map) {
    std::vector<int> out;
    const int from = p->getSquare();
    if (from < 0 || from > 63) return out;
    const bool isW = p->isWhite();
    const int sign = colorSign(isW);
    const int ff = fileOf(from);
    const int fr = rankOf(from);

    auto pushIfEmpty = [&](int sq) {
        if (inBounds(sq) && board[sq] == 0) out.push_back(sq);
    };
    auto pushIfCapture = [&](int sq) {
        if (inBounds(sq) && board[sq] == -sign) out.push_back(sq);
    };
    auto pushIfNotFriendly = [&](int sq) {
        if (!inBounds(sq)) return false;
        if (board[sq] == sign) return false;
        out.push_back(sq);
        return true;
    };

    switch (p->getType()) {
    case PieceType::Knight: {
        static const int kOff[8] = { 15, 17, -15, -17, 6, -6, 10, -10 };
        for (int d : kOff) {
            int to = from + d;
            if (!inBounds(to)) continue;
            int df = std::abs(fileOf(to) - ff);
            int dr = std::abs(rankOf(to) - fr);
            if (!((df == 1 && dr == 2) || (df == 2 && dr == 1))) continue;
            if (board[to] != sign) out.push_back(to);
        }
        break;
    }
    case PieceType::Bishop:
    case PieceType::Rook:
    case PieceType::Queen: {
        std::vector<int> dirs;
        if (p->getType() != PieceType::Rook) {
            dirs.insert(dirs.end(), { 9, 7, -9, -7 }); // diagonals
        }
        if (p->getType() != PieceType::Bishop) {
            dirs.insert(dirs.end(), { 8, -8, 1, -1 }); // orthogonals
        }
        for (int d : dirs) {
            int sq = from;
            while (true) {
                int prevF = fileOf(sq);
                sq += d;
                if (!inBounds(sq)) break;
                int curF = fileOf(sq);
                if ((d == 1 || d == -1 || d == 7 || d == -7 || d == 9 || d == -9) &&
                    std::abs(curF - prevF) != (d == 8 || d == -8 ? 0 : 1)) {
                    break; // wrapped horizontally
                }
                if (board[sq] == 0) {
                    out.push_back(sq);
                    continue;
                }
                if (board[sq] == -sign) out.push_back(sq);
                break; // blocked by any piece
            }
        }
        break;
    }
    case PieceType::King: {
        for (int dr = -1; dr <= 1; ++dr) {
            for (int df = -1; df <= 1; ++df) {
                if (dr == 0 && df == 0) continue;
                int nf = ff + df, nr = fr + dr;
                if (nf < 0 || nf > 7 || nr < 0 || nr > 7) continue;
                int to = nr * 8 + nf;
                if (board[to] != sign) out.push_back(to);
            }
        }
        // Castling not handled here
        break;
    }
    case PieceType::Pawn: {
        // Move forward
        int forward = isW ? from + 8 : from - 8;
        if (inBounds(forward) && board[forward] == 0) {
            out.push_back(forward);
            // Double move from start rank
            bool onStart = isW ? (fr == 1) : (fr == 6);
            int forward2 = isW ? from + 16 : from - 16;
            if (onStart && inBounds(forward2) && board[forward2] == 0) {
                out.push_back(forward2);
            }
        }
        // Captures
        int capL = isW ? from + 7 : from - 7;
        int capR = isW ? from + 9 : from - 9;
        if (ff > 0 && inBounds(capL) && board[capL] == -sign) out.push_back(capL);
        if (ff < 7 && inBounds(capR) && board[capR] == -sign) out.push_back(capR);

        // En passant (target square is empty; victim stands behind it)
        int ep = ChessPiece::enPassantTargetSquare;
        if (ep >= 0 && board[ep] == 0) {
            // left diag EP
            if (ff > 0 && ep == capL) {
                int victim = ep + (isW ? -8 : 8);
                if (inBounds(victim)) {
                    if (auto* v = map[victim]) {
                        if (v->getType() == PieceType::Pawn && v->isWhite() != isW)
                            out.push_back(ep);
                    }
                }
            }
            // right diag EP
            if (ff < 7 && ep == capR) {
                int victim = ep + (isW ? -8 : 8);
                if (inBounds(victim)) {
                    if (auto* v = map[victim]) {
                        if (v->getType() == PieceType::Pawn && v->isWhite() != isW)
                            out.push_back(ep);
                    }
                }
            }
        }
        break;
    }
    default:
        break;
    }

    return out;
}

} // namespace

int CheckCheckmate::findKingSquare(const std::vector<ChessPiece*>& pieces, bool whiteTurn) {
    for (const auto& piece : pieces) {
        if (piece && piece->getType() == PieceType::King && piece->isWhite() == whiteTurn) {
            return piece->getSquare();
        }
    }
    return -1; // King not found (should not happen in a valid game)
}

std::vector<int> CheckCheckmate::getAllAttacks(const std::vector<int>& boardState, const std::vector<ChessPiece*>& pieces, bool enemyColor) {
    // Not used by the new isInCheck; kept for compatibility if needed elsewhere
    std::vector<int> attacks;
    auto map = buildMap(pieces);
    for (auto* piece : pieces) {
        if (piece && piece->isWhite() == enemyColor) {
            auto moves = generatePseudoLegalMoves(piece, boardState, map);
            attacks.insert(attacks.end(), moves.begin(), moves.end());
        }
    }
    return attacks;
}

bool CheckCheckmate::isInCheck(const std::vector<int>& boardState,
    const std::vector<ChessPiece*>& pieces,
    bool whiteTurn) {
    // Find king square of the side to move
    int kingSquare = -1;
    for (auto* piece : pieces) {
        if (piece && piece->isWhite() == whiteTurn && piece->getType() == PieceType::King) {
            kingSquare = piece->getSquare();
            break;
        }
    }
    if (kingSquare < 0 || kingSquare > 63) return false;

    // Build occupancy map and test attacks by opponent
    auto map = buildMap(pieces);
    bool attacked = isSquareAttackedByColor(kingSquare, !whiteTurn, map);
    return attacked;
}

std::vector<std::pair<int, int>> CheckCheckmate::getEscapeMoves(
    const std::vector<int>& board,
    const std::vector<ChessPiece*>& pieces,
    bool whiteTurn)
{
    std::vector<std::pair<int, int>> escapes;
    auto map = buildMap(pieces);

    for (auto* p : pieces) {
        if (!p || p->isWhite() != whiteTurn) continue;
        int from = p->getSquare();
        if (from < 0) continue;

        auto moves = generatePseudoLegalMoves(p, board, map);

        for (int to : moves) {
            int oldFrom = p->getSquare();
            ChessPiece* capPtr = nullptr;
            int capOldSq = -1;
            // normal capture on 'to'
            for (auto* q : pieces) {
                if (q && q->getSquare() == to && q->isWhite() != p->isWhite()) { capPtr = q; capOldSq = q->getSquare(); break; }
            }
            // en passant: victim is behind 'to'
            if (!capPtr && p->getType() == PieceType::Pawn &&
                to == ChessPiece::enPassantTargetSquare) {
                int victimSq = to + (p->isWhite() ? -8 : 8);
                for (auto* q : pieces) {
                    if (q && q->getSquare() == victimSq &&
                        q->isWhite() != p->isWhite() &&
                        q->getType() == PieceType::Pawn) {
                        capPtr = q; capOldSq = q->getSquare();
                        break;
                    }
                }
            }

            // Apply
            p->setSquare(to);
            if (capPtr) capPtr->setSquare(-1);

            // Rebuild simulated board and ptrs
            std::vector<int> simBoard(64, 0);
            std::vector<ChessPiece*> simPtrs;
            for (auto* q : pieces) {
                if (!q) continue;
                int s = q->getSquare();
                if (s >= 0 && s < 64) {
                    simBoard[s] = q->isWhite() ? 1 : -1;
                    simPtrs.push_back(q);
                }
            }

            bool stillInCheck = CheckCheckmate::isInCheck(simBoard, simPtrs, whiteTurn);

            // Restore
            p->setSquare(oldFrom);
            if (capPtr) capPtr->setSquare(capOldSq);

            if (!stillInCheck) {
                escapes.emplace_back(from, to);
            }
        }
    }
    return escapes;
}

bool CheckCheckmate::isCheckmate(const std::vector<int>& boardState,
    const std::vector<ChessPiece*>& pieces,
    bool whiteTurn) {
    if (!isInCheck(boardState, pieces, whiteTurn)) return false;
    auto escapes = CheckCheckmate::getEscapeMoves(boardState, pieces, whiteTurn);
    return escapes.empty();
}
