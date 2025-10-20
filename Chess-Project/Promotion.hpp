// Pseudocode:
// - Call promotePawnIfNeeded right after executing a move.
// - If the moved piece is a Pawn and its destination rank is last rank (7 for white, 0 for black):
//   - Replace the Pawn object in your pieces container with a Queen object of the same color.
//   - Keep the piece on the same square (toSquare). Return the new Queen pointer.
// - Otherwise return nullptr.

#pragma once

#include <memory>
#include <algorithm>

#include "conversion.hpp"
#include "Pawn.hpp"
#include "Queen.hpp"

// Helper to promote a pawn to a queen after a move is applied.
// - movedPiece: the piece that was just moved (must already be at toSquare).
// - toSquare: destination square index (0..63).
// - pieces: container of std::unique_ptr<ChessPiece> holding all pieces.
// - whiteQueenTexture / blackQueenTexture: textures for newly created queen.
//
// Returns:
// - Pointer to the new Queen if promotion happened.
// - nullptr if no promotion was performed.
//
// Usage:
//   auto* maybeQueen = promotePawnIfNeeded(movedPiece, toSq, pieces, whiteQTex, blackQTex);
//   if (maybeQueen) { /* optional: update selection/UI, etc. */ }
template <typename PiecesContainer>
ChessPiece* promotePawnIfNeeded(
    ChessPiece* movedPiece,
    int toSquare,
    PiecesContainer& pieces,
    const sf::Texture& whiteQueenTexture,
    const sf::Texture& blackQueenTexture)
{
    if (!movedPiece) return nullptr;

    // Only pawns can be promoted
    if (movedPiece->getType() != PieceType::Pawn)
        return nullptr;

    const bool isWhite = movedPiece->isWhite();
    const int rank = toRank(toSquare);

    const bool shouldPromote = (isWhite && rank == 7) || (!isWhite && rank == 0);
    if (!shouldPromote)
        return nullptr;

    // Find the pawn in the container
    auto it = std::find_if(pieces.begin(), pieces.end(),
        [movedPiece](auto& up) { return up.get() == movedPiece; });

    if (it == pieces.end())
        return nullptr;

    // Replace the pawn with a queen at the same index to keep ordering stable
    auto queen = std::make_unique<Queen>(isWhite, toSquare, isWhite ? whiteQueenTexture : blackQueenTexture);
    ChessPiece* queenRaw = queen.get();
    *it = std::move(queen);

    return queenRaw;
}
