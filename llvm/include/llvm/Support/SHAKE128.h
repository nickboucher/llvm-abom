//===- SHAKE128.h - SHAKE128 implementation ---------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the SHAKE128 extendable-output function (XOF) specified by
// FIPS 202, built on the Keccak-p[1600, 24] permutation.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_SHAKE128_H
#define LLVM_SUPPORT_SHAKE128_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Compiler.h"
#include <cstdint>

namespace llvm {

/// The SHAKE128 extendable-output function, which produces a digest of any
/// requested length.
class SHAKE128 {
public:
  SHAKE128() { init(); }

  /// Reinitialize the internal state.
  LLVM_ABI void init();

  /// Digest more data.
  LLVM_ABI void update(ArrayRef<uint8_t> Data);

  /// Digest more data.
  void update(StringRef Str) {
    update(ArrayRef(reinterpret_cast<const uint8_t *>(Str.data()), Str.size()));
  }

  /// Fill \p Out with the digest of the data consumed since the last call to
  /// init(). The internal state is not usable for further updates afterwards.
  LLVM_ABI void final(MutableArrayRef<uint8_t> Out);

  /// Fill \p Out with the SHAKE128 digest of \p Data.
  LLVM_ABI static void hash(ArrayRef<uint8_t> Data,
                            MutableArrayRef<uint8_t> Out);

private:
  /// The rate of SHAKE128 in bytes, i.e. (1600 - 2 * 128) / 8.
  static constexpr size_t BlockLength = 168;

  void absorbBlock();

  /// The Keccak state, held as 25 lanes of 64 bits.
  uint64_t State[25];
  uint8_t Buffer[BlockLength];
  size_t BufferOffset;
};

} // end namespace llvm

#endif // LLVM_SUPPORT_SHAKE128_H
