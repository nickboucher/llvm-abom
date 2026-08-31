//===- llvm/unittest/Support/SHAKE128.cpp - SHAKE128 tests ----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements unit tests for the SHAKE128 extendable-output function.
//
//===----------------------------------------------------------------------===//

#include "llvm/Support/SHAKE128.h"
#include "llvm/ADT/StringExtras.h"
#include "gtest/gtest.h"

using namespace llvm;

namespace {

std::string shake128(StringRef Input, size_t Length) {
  std::vector<uint8_t> Digest(Length);
  SHAKE128::hash(arrayRefFromStringRef(Input), Digest);
  return toHex(Digest, /*LowerCase=*/true);
}

// Test vectors from the NIST Cryptographic Algorithm Validation Program.
TEST(SHAKE128Test, KnownAnswers) {
  EXPECT_EQ("7f9c2ba4e88f827d616045507605853ed73b8093f6efbc88eb1a6eacfa66ef26",
            shake128("", 32));
  EXPECT_EQ("5881092dd818bf5cf8a3ddb793fbcba74097d5c526a6d35f97b83351940f2cc8",
            shake128("abc", 32));
  EXPECT_EQ("f4202e3c5852f9182a0430fd8144f0a7",
            shake128("The quick brown fox jumps over the lazy dog", 16));
}

// The digest may be of any length, and every digest is a prefix of the longer
// ones.
TEST(SHAKE128Test, DigestLength) {
  EXPECT_EQ("", shake128("abc", 0));
  EXPECT_EQ("58", shake128("abc", 1));
  EXPECT_EQ("5881092dd8", shake128("abc", 5));

  // Squeezing more than one block of output requires further permutations.
  std::string Long = shake128("abc", 512);
  EXPECT_EQ(1024u, Long.size());
  EXPECT_EQ(shake128("abc", 32), Long.substr(0, 64));
}

// Input of any size may be absorbed in arbitrarily sized pieces.
TEST(SHAKE128Test, IncrementalUpdate) {
  std::string Input(1000, 'a');
  for (size_t Chunk : {1, 7, 168, 169, 500}) {
    SHAKE128 Hash;
    for (size_t I = 0; I < Input.size(); I += Chunk)
      Hash.update(StringRef(Input).substr(I, Chunk));
    std::vector<uint8_t> Digest(32);
    Hash.final(Digest);
    EXPECT_EQ(shake128(Input, 32), toHex(Digest, /*LowerCase=*/true))
        << "chunk size " << Chunk;
  }
}

} // end anonymous namespace
