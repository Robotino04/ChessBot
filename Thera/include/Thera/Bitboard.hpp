#pragma once

#include "Thera/Move.hpp"

#include "Thera/Utils/BuildType.hpp"
#include "Thera/Utils/Math.hpp"
#include "Thera/Utils/Coordinates.hpp"

#include "Thera/TemporyryCoordinateTypes.hpp"

#include <array>
#include <stdint.h>
#include <stdexcept>
#include <bit>

namespace Thera{

class Board;

/**
 * @brief A bitboard able to lookup in both ways.
 * 
 * Using any bitwise math operators (&, |, ^, etc.) will result in
 * a "broken" bitboard. Only further math operators and isOccupied
 * will work correctly. The use of a "broken" bitboard is safe but
 * functions like applyMove won't have the desired effect. "Broken"
 * bitboards also can't be checked for desyncs at runtime. 
 * 
 * @tparam N max number of pieces in this bitboard.
 */
template<int N>
class Bitboard{
    static_assert(Utils::isInRange(N, 0, 64), "Bitboards can only store up to 64 pieces.");
    friend class Board;
    
    public:
        struct Reference{
            friend Bitboard;
            public:
                constexpr bool operator = (bool value){
                    parent.bits = Utils::setBit<decltype(parent.bits)>(parent.bits, bitIndex, value);
                    return value;
                }

                constexpr operator bool () const{
                    return Utils::getBit(parent.bits, bitIndex);
                }

            private:
                constexpr Reference(Bitboard<N>& parent, size_t bitIndex): parent(parent), bitIndex(bitIndex){}

                Bitboard<N>& parent;
                const size_t bitIndex = 0;
        };
        friend Reference;

        constexpr Bitboard(){
            clear();
        }

        constexpr Bitboard(__uint128_t raw){
            if constexpr (Utils::BuildType::Current == Utils::BuildType::Debug){

                numPieces = std::popcount(raw);
                bits = raw;
                const auto pieces = getPieces();

                clear();
                for (int i=0; i<std::popcount(raw); i++){
                    placePiece(Coordinate8x8(pieces.at(i)));
                }
                if (std::popcount(bits) != numPieces)
                    throw std::runtime_error("Desync between bitboard and numPieces detected.");
            }

            bits = raw;
        }


        constexpr void clear(){
            bits = __uint128_t(0);
            numPieces = 0;
            occupiedSquares.fill(-1);
        }

        /**
         * @brief Test if square is occupied. Only does bounds checking in debug builds.
         * 
         * @param square 
         * @return true 
         * @return false 
         */
        constexpr bool isOccupied(Coordinate8x8 square) const{
            if constexpr (Utils::BuildType::Current == Utils::BuildType::Debug)
                if (!Utils::isInRange<int8_t>(square.pos, 0, 127))
                    throw std::out_of_range("Square index is outside the board");
            return Utils::getBit(bits, square.pos);
        }

        /**
         * @brief Blindly place the piece on the board without any sort of test.
         * 
         * In debug builds there are some tests to ensure that numPiece doesn't get out of sync.
         * 
         * @param square
         */
        constexpr void placePiece(Coordinate8x8 square){
            if constexpr (Utils::BuildType::Current == Utils::BuildType::Debug){
                if (isOccupied(square))
                    throw std::invalid_argument("Tried to place piece on already occupied square.");

                occupiedSquares[numPieces] = square.pos;
                reverseOccupiedSquares[square.pos] = numPieces;
                numPieces++;
            }
            
            (*this)[square] = true;
        }

        /**
         * @brief Blindly remove the piece from the board without any sort of test.
         * 
         * In debug builds there are some tests to ensure that numPiece doesn't get out of sync.
         * 
         * @param square 
         */
        constexpr void removePiece(Coordinate8x8 square){
            if constexpr (Utils::BuildType::Current == Utils::BuildType::Debug){
                if (!isOccupied(square))
                    throw std::invalid_argument("Tried to remove piece from empty square.");
                if (!Utils::isOnBoard(square))
                    throw std::invalid_argument("Tried to remove piece from outside the board.");

                const int pieceIndex = reverseOccupiedSquares.at(square.pos);

                // move last entry in occupiedSquares to the empty spot
                occupiedSquares.at(pieceIndex) = occupiedSquares.at(numPieces-1);
                reverseOccupiedSquares.at(occupiedSquares.at(pieceIndex)) = pieceIndex;

                reverseOccupiedSquares.at(square.pos) = -1;
                numPieces--;
            }
            
            (*this)[square] = false;
        }

        /**
         * @brief Blindly apply the base move without any sort of test.
         * 
         * In debug builds there are some tests to ensure that numPiece doesn't get out of sync.
         * 
         * @param move 
         */
        constexpr void applyMove(Move const& move){
            if constexpr (Utils::BuildType::Current == Utils::BuildType::Debug){
                if (!isOccupied(move.startIndex)){
                    throw std::runtime_error("Tried to make move starting on an empty square.");
                }
                if (!Utils::isInRange<int8_t>(Coordinate8x8(move.startIndex).pos, 0, reverseOccupiedSquares.size()))
                    throw std::out_of_range("Move start index is outside the board");
                if (!Utils::isInRange<int8_t>(Coordinate8x8(move.endIndex).pos, 0, reverseOccupiedSquares.size()))
                    throw std::out_of_range("Move end index is outside the board");
            }

            clearBit(Coordinate8x8(move.startIndex).pos); // will remove the piece
            setBit(Coordinate8x8(move.endIndex).pos); // will place the piece

            if constexpr (Utils::BuildType::Current == Utils::BuildType::Debug){
                int pieceIndex = reverseOccupiedSquares.at(Coordinate8x8(move.startIndex).pos);
                reverseOccupiedSquares.at(Coordinate8x8(move.startIndex).pos) = -1;
                reverseOccupiedSquares.at(Coordinate8x8(move.endIndex).pos) = pieceIndex;
                occupiedSquares.at(pieceIndex) = Coordinate8x8(move.endIndex).pos;
            }
        }

        /**
         * @brief Get a list of pieces. Only does sanity checks in debug builds.
         * 
         * @return constexpr std::array<Coordinate8x8, N> list of 8x8 square indices with first numPieces pieces valid
         */
        constexpr std::array<Coordinate8x8, N> getPieces() const{
            auto x = bits;

            if constexpr (Utils::BuildType::Current == Utils::BuildType::Debug){
                if (std::popcount(x) != numPieces)
                    throw std::runtime_error("Desync between bitboard and numPieces detected.");
            }

            std::array<Coordinate8x8, N> result;
            auto list = result.begin();

            if (x) do {
                int8_t idx = bitScanForward(x); // square index from 0..63
                *list++ = Coordinate8x8(idx);
            } while (x &= x-1); // reset LS1B

            return result;
        }
        
        constexpr auto begin(){
            return occupiedSquares.begin();
        }
        constexpr auto begin() const{
            return occupiedSquares.begin();
        }
        constexpr auto end(){
            return std::next(occupiedSquares.begin(), numPieces);
        }
        constexpr auto end() const{
            return std::next(occupiedSquares.begin(), numPieces);
        }

        constexpr int getNumPieces() const{
            if constexpr (Utils::BuildType::Current == Utils::BuildType::Debug){
                if (std::popcount(bits) != numPieces)
                    throw std::runtime_error("Desync between bitboard and numPieces detected.");
            }
            return std::popcount(bits);
        }

        constexpr uint64_t getBoard8x8() const{
            uint64_t board = 0;
            for (int i=0; i<64; i++){
                board = Utils::setBit<uint64_t>(board, i, (*this)[Coordinate8x8(i)]);
            }
            return board;
        }



        constexpr bool operator[] (Coordinate8x8 bitIdx) const{
            return Utils::getBit(bits, bitIdx.pos);
        }
        constexpr Reference operator[] (Coordinate8x8 bitIdx){
            return Reference(*this, bitIdx.pos);
        }
        constexpr void flipBit(int8_t bitIndex){
            bits ^= __uint128_t(1) << bitIndex;
        }

        constexpr void setBit(int8_t bitIndex){
            bits |= __uint128_t(1) << bitIndex;
        }
        constexpr void clearBit(int8_t bitIndex){
            bits &= ~(__uint128_t(1) << bitIndex);
        }

        constexpr bool hasPieces() const{
            return bits;
        }

        template<int otherN>
        constexpr Bitboard<std::min(64, N + otherN)> operator | (Bitboard<otherN> const& other){
            return {this->bits | other.bits};
        }
        template<int otherN>
        constexpr Bitboard<std::min(64, std::min(N, otherN))> operator & (Bitboard<otherN> const& other){
            return {this->bits & other.bits};
        }
        template<int otherN>
        constexpr Bitboard<std::min(64, N + otherN)> operator ^ (Bitboard<otherN> const& other){
            return {this->bits ^ other.bits};
        }

    private:



        /**
         * bitScanForward
         * @author Matt Taylor (2003)
         * @param bb bitboard to scan
         * @precondition bb != 0
         * @return index (0..63) of least significant one bit
         */
        static constexpr int8_t bitScanForward(uint64_t bb) {
            constexpr std::array<int8_t, 64> lsb_64_table ={
                63, 30,  3, 32, 59, 14, 11, 33,
                60, 24, 50,  9, 55, 19, 21, 34,
                61, 29,  2, 53, 51, 23, 41, 18,
                56, 28,  1, 43, 46, 27,  0, 35,
                62, 31, 58,  4,  5, 49, 54,  6,
                15, 52, 12, 40,  7, 42, 45, 16,
                25, 57, 48, 13, 10, 39,  8, 44,
                20, 47, 38, 22, 17, 37, 36, 26
            };

            uint32_t folded = 0;
            bb ^= bb - 1;
            folded = (uint32_t) bb ^ (bb >> 32);
            return lsb_64_table[folded * 0x78291ACF >> 26];
        }
    
    private:
        static_assert(requires {typename __uint128_t;});
        __uint128_t bits;

        // these are only used in debug builds to aid in debugging
        std::array<int8_t, N> occupiedSquares;
        std::array<int, 10*12> reverseOccupiedSquares;
        int numPieces = 0;
};

}