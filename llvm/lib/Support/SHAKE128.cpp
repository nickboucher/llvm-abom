//===- SHAKE128.cpp - SHAKE128 implementation -----------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the SHAKE128 extendable-output function specified by
// FIPS 202, "SHA-3 Standard: Permutation-Based Hash and Extendable-Output
// Functions".
//
//===----------------------------------------------------------------------===//

#include "llvm/Support/SHAKE128.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/MathExtras.h"
#include <cstring>

using namespace llvm;

namespace {

/// The number of rounds of the Keccak-p[1600, 24] permutation.
constexpr unsigned NumRounds = 24;

/// The round constants applied by the iota step.
constexpr uint64_t RoundConstants[NumRounds] = {
    0x0000000000000001ULL, 0x0000000000008082ULL, 0x800000000000808aULL,
    0x8000000080008000ULL, 0x000000000000808bULL, 0x0000000080000001ULL,
    0x8000000080008081ULL, 0x8000000000008009ULL, 0x000000000000008aULL,
    0x0000000000000088ULL, 0x0000000080008009ULL, 0x000000008000000aULL,
    0x000000008000808bULL, 0x800000000000008bULL, 0x8000000000008089ULL,
    0x8000000000008003ULL, 0x8000000000008002ULL, 0x8000000000000080ULL,
    0x000000000000800aULL, 0x800000008000000aULL, 0x8000000080008081ULL,
    0x8000000000008080ULL, 0x0000000080000001ULL, 0x8000000080008008ULL};

/// The rotation offsets applied by the rho step, indexed by lane.
constexpr unsigned RotationOffsets[25] = {0,  1, 62, 28, 27, 36, 44, 6,  55,
                                          20, 3, 10, 43, 25, 39, 41, 45, 15,
                                          21, 8, 18, 2,  61, 56, 14};

/// The lane permutation applied by the pi step: lane PiLanes[I] of the input
/// becomes lane I of the output.
constexpr unsigned PiLanes[25] = {0,  6,  12, 18, 24, 3,  9, 10, 16,
                                  22, 1,  7,  13, 19, 20, 4, 5,  11,
                                  17, 23, 2,  8,  14, 15, 21};

/// Apply the Keccak-p[1600, 24] permutation to \p A in place.
void keccakF1600(uint64_t A[25]) {
  for (uint64_t RC : RoundConstants) {
    // Theta: exclusive-or each lane with the parities of two neighboring
    // columns.
    uint64_t C[5], D[5];
    for (unsigned X = 0; X != 5; ++X)
      C[X] = A[X] ^ A[X + 5] ^ A[X + 10] ^ A[X + 15] ^ A[X + 20];
    for (unsigned X = 0; X != 5; ++X)
      D[X] = C[(X + 4) % 5] ^ rotl(C[(X + 1) % 5], 1);
    for (unsigned I = 0; I != 25; ++I)
      A[I] ^= D[I % 5];

    // Rho and pi: rotate each lane and move it to its permuted position.
    uint64_t B[25];
    for (unsigned I = 0; I != 25; ++I)
      B[I] = rotl(A[PiLanes[I]], RotationOffsets[PiLanes[I]]);

    // Chi: combine each lane with the two that follow it in its row.
    for (unsigned Y = 0; Y != 25; Y += 5)
      for (unsigned X = 0; X != 5; ++X)
        A[Y + X] = B[Y + X] ^ (~B[Y + (X + 1) % 5] & B[Y + (X + 2) % 5]);

    // Iota: break the symmetry of the round function.
    A[0] ^= RC;
  }
}

} // end anonymous namespace

void SHAKE128::init() {
  std::memset(State, 0, sizeof(State));
  BufferOffset = 0;
}

void SHAKE128::absorbBlock() {
  for (unsigned I = 0, E = BlockLength / sizeof(uint64_t); I != E; ++I)
    State[I] ^= support::endian::read64le(Buffer + I * sizeof(uint64_t));
  keccakF1600(State);
}

void SHAKE128::update(ArrayRef<uint8_t> Data) {
  while (!Data.empty()) {
    size_t Count = std::min(BlockLength - BufferOffset, Data.size());
    std::memcpy(Buffer + BufferOffset, Data.data(), Count);
    BufferOffset += Count;
    Data = Data.drop_front(Count);
    if (BufferOffset == BlockLength) {
      absorbBlock();
      BufferOffset = 0;
    }
  }
}

void SHAKE128::final(MutableArrayRef<uint8_t> Out) {
  // Pad the final block with the SHAKE domain separator and the pad10*1 rule.
  std::memset(Buffer + BufferOffset, 0, BlockLength - BufferOffset);
  Buffer[BufferOffset] = 0x1f;
  Buffer[BlockLength - 1] |= 0x80;
  absorbBlock();

  // Squeeze the requested number of output bytes.
  while (!Out.empty()) {
    uint8_t Block[BlockLength];
    for (unsigned I = 0, E = BlockLength / sizeof(uint64_t); I != E; ++I)
      support::endian::write64le(Block + I * sizeof(uint64_t), State[I]);
    size_t Count = std::min(BlockLength, Out.size());
    std::memcpy(Out.data(), Block, Count);
    Out = Out.drop_front(Count);
    if (!Out.empty())
      keccakF1600(State);
  }
}

void SHAKE128::hash(ArrayRef<uint8_t> Data, MutableArrayRef<uint8_t> Out) {
  SHAKE128 S;
  S.update(Data);
  S.final(Out);
}
