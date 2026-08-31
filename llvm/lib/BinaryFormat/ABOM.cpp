//===- ABOM.cpp - Automatic Bill of Materials -----------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the Automatic Bill of Materials: source file hashing,
// the Bloom filters holding those hashes, and the binary protocol used to
// embed them in object files and images.
//
//===----------------------------------------------------------------------===//

#include "llvm/BinaryFormat/ABOM.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/bit.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/Parallel.h"
#include "llvm/Support/SHAKE128.h"
#include <cstring>

using namespace llvm;
using namespace llvm::abom;

//===----------------------------------------------------------------------===//
// Arithmetic coding
//===----------------------------------------------------------------------===//

namespace {

/// Largest value the coder's range can hold.
constexpr uint64_t FullRange = (uint64_t(1) << CodeBits) - 1;
constexpr uint64_t QuarterRange = (FullRange >> 2) + 1;
constexpr uint64_t HalfRange = 2 * QuarterRange;
constexpr uint64_t ThreeQuarterRange = 3 * QuarterRange;

/// Total weight of the coder's two-symbol cumulative distribution function.
constexpr uint64_t TotalWeight = uint64_t(1) << CDFBits;

/// Encodes the bit array \p Words, least significant bit of each word first,
/// and appends the result to \p Out. The coder is the one described by
/// Witten, Neal and Cleary, "Arithmetic Coding for Data Compression" (1987),
/// with the alphabet fixed to two symbols whose distribution is given by
/// \p ZeroWeight. Bits are written most significant bit first.
///
/// A bill of materials is a few hundred thousand symbols long, so the coder
/// state is kept in one frame rather than in an object the output could
/// alias, and the symbol of a clear bit is never tested for.
void encodeBits(ArrayRef<uint64_t> Words, uint32_t ZeroWeight,
                SmallVectorImpl<uint8_t> &Out) {
  assert(ZeroWeight != 0 && "model leaves no room for a clear bit");
  uint64_t Low = 0, High = FullRange, Pending = 0;
  uint8_t Byte = 0;
  unsigned BitsInByte = 0;

  auto putBit = [&](bool Bit) {
    Byte |= uint8_t(Bit) << (7 - BitsInByte);
    if (++BitsInByte == 8) {
      Out.push_back(Byte);
      Byte = 0;
      BitsInByte = 0;
    }
  };
  auto emit = [&](bool Bit) {
    putBit(Bit);
    for (; Pending != 0; --Pending)
      putBit(!Bit);
  };
  // Emits the bits of the range that are settled, and rescales.
  auto renormalize = [&]() {
    while (true) {
      if (High < HalfRange) {
        emit(false);
      } else if (Low >= HalfRange) {
        emit(true);
        Low -= HalfRange;
        High -= HalfRange;
      } else if (Low >= QuarterRange && High < ThreeQuarterRange) {
        // The range straddles the midpoint: defer the leading bit.
        ++Pending;
        Low -= QuarterRange;
        High -= QuarterRange;
      } else {
        return;
      }
      High = (High << 1) + 1;
      Low <<= 1;
    }
  };
  for (uint64_t Word : Words) {
    for (unsigned I = 0; I != 64; ++I) {
      bool Bit = (Word >> I) & 1;
      uint64_t Range = High - Low + 1;
      uint64_t Split = (ZeroWeight * Range) >> CDFBits;
      Low += Bit ? Split : 0;
      High = Low + (Bit ? Range - Split : Split) - 1;
      // Most bits settle nothing, so the rescaling stays off the hot path.
      if (High < HalfRange || Low >= HalfRange ||
          (Low >= QuarterRange && High < ThreeQuarterRange))
        renormalize();
    }
  }

  // Write the bits needed to disambiguate the final range.
  ++Pending;
  emit(Low >= QuarterRange);
  if (BitsInByte != 0)
    Out.push_back(Byte);
}

/// Decodes \p Words.size() words of bit array from \p Data, which
/// encodeBits() produced with the same \p ZeroWeight.
void decodeBits(ArrayRef<uint8_t> Data, uint32_t ZeroWeight,
                MutableArrayRef<uint64_t> Words) {
  uint64_t Low = 0, High = FullRange, Code = 0;
  size_t BitPos = 0;
  const size_t BitEnd = Data.size() * 8;

  // Returns the next bit of the payload, or zero once it is exhausted. The
  // encoder only emits the bits needed to identify the final range, so a well
  // formed payload runs out before the last symbols are decoded.
  auto nextBit = [&]() -> uint64_t {
    if (BitPos >= BitEnd)
      return 0;
    uint64_t Bit = (Data[BitPos / 8] >> (7 - BitPos % 8)) & 1;
    ++BitPos;
    return Bit;
  };

  for (unsigned I = 0; I != CodeBits; ++I)
    Code = (Code << 1) + nextBit();

  for (uint64_t &Word : Words) {
    uint64_t Value = 0;
    for (unsigned I = 0; I != 64; ++I) {
      uint64_t Range = High - Low + 1;
      // The scaled value is only ever compared against the model, and
      // floor(x / Range) >= ZeroWeight holds exactly when x >= ZeroWeight *
      // Range, which saves a division on every bit of every filter.
      uint64_t Weighted = uint64_t(ZeroWeight) * Range;
      bool Bit = ((Code - Low + 1) << CDFBits) - 1 >= Weighted;
      uint64_t Split = Weighted >> CDFBits;
      Low += Bit ? Split : 0;
      High = Low + (Bit ? Range - Split : Split) - 1;
      Value |= uint64_t(Bit) << I;

      while (true) {
        if (High < HalfRange) {
          // The leading bit is settled; nothing to discard.
        } else if (Low >= HalfRange) {
          Code -= HalfRange;
          Low -= HalfRange;
          High -= HalfRange;
        } else if (Low >= QuarterRange && High < ThreeQuarterRange) {
          Code -= QuarterRange;
          Low -= QuarterRange;
          High -= QuarterRange;
        } else {
          break;
        }
        High = (High << 1) + 1;
        Low <<= 1;
        Code = (Code << 1) + nextBit();
      }
    }
    Word = Value;
  }
}

/// Returns the weight of a zero bit, scaled to CDFBits, for \p Model. The
/// encoder and the decoder both derive the model from the value stored in the
/// record so that they agree on the distribution bit for bit.
uint32_t getZeroWeight(uint32_t Model) {
  return uint32_t((1.0 - double(Model) / UINT32_MAX) * TotalWeight);
}

/// Appends the \p Size low order bytes of \p Value to \p Out, little end
/// first.
void appendInt(SmallVectorImpl<uint8_t> &Out, uint64_t Value, unsigned Size) {
  for (unsigned I = 0; I != Size; ++I)
    Out.push_back(uint8_t(Value >> (8 * I)));
}

/// Appends an ABOM record header to \p Out.
void appendHeader(SmallVectorImpl<uint8_t> &Out, uint16_t NumFilters,
                  uint32_t Model, uint32_t PayloadSize) {
  Out.append(Magic.begin(), Magic.end());
  appendInt(Out, FormatVersion, 1);
  appendInt(Out, NumFilters, 2);
  appendInt(Out, Model, 4);
  appendInt(Out, PayloadSize, 4);
}

/// Returns the Bloom filter indices addressed by \p H.
std::array<uint32_t, NumHashFunctions> getIndices(const Hash &H) {
  // The hash is consumed most significant bit first, in Log2FilterBits sized
  // slices.
  uint64_t Bits = 0;
  for (uint8_t Byte : H)
    Bits = (Bits << 8) | Byte;

  std::array<uint32_t, NumHashFunctions> Indices;
  for (unsigned I = 0; I != NumHashFunctions; ++I) {
    unsigned Shift = 8 * HashSize - (I + 1) * Log2FilterBits;
    Indices[I] = (Bits >> Shift) & (FilterBits - 1);
  }
  return Indices;
}

} // end anonymous namespace

//===----------------------------------------------------------------------===//
// Hashing
//===----------------------------------------------------------------------===//

Hash abom::computeHash(ArrayRef<uint8_t> Data) {
  Hash H;
  SHAKE128::hash(Data, H);
  // Clear the bits of the digest beyond HashBits so that every hash has a
  // single canonical representation.
  constexpr unsigned UnusedBits = 8 * HashSize - HashBits;
  if constexpr (UnusedBits != 0)
    H.back() &= uint8_t(0xff << UnusedBits);
  return H;
}

std::string abom::toString(const Hash &H) {
  std::string Str = llvm::toHex(H, /*LowerCase=*/true);
  // Drop the trailing nibble if it holds no bits of the hash.
  if (8 * HashSize - HashBits >= 4)
    Str.pop_back();
  return Str;
}

std::optional<Hash> abom::parseHash(StringRef Str) {
  Str.consume_front_insensitive("0x");
  if (Str.size() != divideCeil(HashBits, 4) || !all_of(Str, isHexDigit))
    return std::nullopt;

  Hash H = {};
  for (unsigned I = 0, E = Str.size(); I != E; ++I)
    H[I / 2] |= hexDigitValue(Str[I]) << (I % 2 ? 0 : 4);
  return H;
}

//===----------------------------------------------------------------------===//
// Records
//===----------------------------------------------------------------------===//

Expected<Header> abom::readHeader(ArrayRef<uint8_t> Data) {
  if (Data.size() < HeaderSize)
    return createStringError(std::errc::illegal_byte_sequence,
                             "ABOM record is truncated");
  if (!std::equal(Magic.begin(), Magic.end(), Data.begin()))
    return createStringError(std::errc::illegal_byte_sequence,
                             "invalid ABOM magic word");
  if (Data[4] != FormatVersion)
    return createStringError(std::errc::illegal_byte_sequence,
                             "unsupported ABOM version %u", unsigned(Data[4]));

  Header H;
  H.NumFilters = support::endian::read16le(Data.data() + 5);
  H.Model = support::endian::read32le(Data.data() + 7);
  H.PayloadSize = support::endian::read32le(Data.data() + 11);
  if (H.getRecordSize() > Data.size())
    return createStringError(std::errc::illegal_byte_sequence,
                             "ABOM payload is truncated");

  if (H.NumFilters == 0)
    return createStringError(std::errc::illegal_byte_sequence,
                             "ABOM record has no Bloom filters");

  // Bound the work the record can ask for before any of it is done. Every
  // filter that holds a hash contributes bytes to the payload, so a record
  // claiming more filters than it has payload bytes is malformed.
  if (H.NumFilters > MaxFiltersPerRecord || H.NumFilters > H.PayloadSize)
    return createStringError(
        std::errc::illegal_byte_sequence,
        "ABOM record declares %u Bloom filters in %u bytes",
        unsigned(H.NumFilters), unsigned(H.PayloadSize));

  // A filter that holds any hashes costs far more than this to encode, so a
  // record that declares many filters in few bytes is asking a reader to
  // decode Bloom filters that cannot be there.
  if (H.NumFilters > 1 &&
      (H.NumFilters - 1) * uint64_t(MinPayloadBytesPerFilter) > H.PayloadSize)
    return createStringError(
        std::errc::illegal_byte_sequence,
        "ABOM record declares %u Bloom filters in %u bytes",
        unsigned(H.NumFilters), unsigned(H.PayloadSize));

  // A model denser than a filter can be leaves the coder with no room for a
  // clear bit, which no well formed record needs.
  if (double(H.Model) / UINT32_MAX > MaxSetBitRate)
    return createStringError(std::errc::illegal_byte_sequence,
                             "ABOM record declares an impossible bit density");
  return H;
}

void BillOfMaterials::insert(ArrayRef<uint8_t> PackedHashes) {
  assert(PackedHashes.size() % HashSize == 0 && "partial source file hash");
  Hashes.reserve(Hashes.size() + PackedHashes.size() / HashSize);
  for (size_t I = 0, E = PackedHashes.size(); I != E; I += HashSize) {
    Hash Value;
    std::memcpy(Value.data(), PackedHashes.data() + I, HashSize);
    Hashes.push_back(Value);
  }
}

Error BillOfMaterials::mergeAll(ArrayRef<ArrayRef<uint8_t>> Sections,
                                function_ref<std::string(size_t)> Describe) {
  // Decompressing a section is the expensive part and is independent of every
  // other section, so it is done in parallel. Merging is left to this thread,
  // in the order the sections were given, so that the result does not depend
  // on how the work was scheduled. Sections are taken in batches to bound the
  // memory held at once, counted in filters rather than in sections, since one
  // section may hold many and each costs FilterBits to decode.
  constexpr size_t BatchFilters = 64;
  std::vector<BillOfMaterials> Decoded;
  std::vector<std::string> Failures;

  for (size_t Begin = 0; Begin < Sections.size();) {
    size_t Count = 0, Budget = 0;
    while (Begin + Count != Sections.size() &&
           (Count == 0 || Budget < BatchFilters)) {
      // A malformed header is left for the decoding pass to report.
      if (Expected<Header> H = readHeader(Sections[Begin + Count]))
        Budget += H->NumFilters;
      else
        consumeError(H.takeError());
      ++Count;
    }
    if (Decoded.size() < Count) {
      Decoded.resize(Count);
      Failures.resize(Count);
    }

    parallelFor(0, Count, [&](size_t I) {
      Decoded[I] = BillOfMaterials();
      Failures[I].clear();
      if (Error E = Decoded[I].merge(Sections[Begin + I]))
        Failures[I] = toString(std::move(E));
    });

    for (size_t I = 0; I != Count; ++I) {
      if (!Failures[I].empty())
        return createStringError(std::errc::illegal_byte_sequence, "%s: %s",
                                 Describe(Begin + I).c_str(),
                                 Failures[I].c_str());
      for (size_t F = 0, FE = Decoded[I].SetBits.size(); F != FE; ++F)
        mergeFilter(Decoded[I].getFilter(F));
      Hashes.insert(Hashes.end(), Decoded[I].Hashes.begin(),
                    Decoded[I].Hashes.end());
      // Release the decoded filters before the next batch is read in.
      Decoded[I] = BillOfMaterials();
    }
    Begin += Count;
  }
  return Error::success();
}

Error BillOfMaterials::writeFilters(SmallVectorImpl<uint8_t> &Out) {
  foldHashes();
  if (Filters.empty())
    getUnsaturatedFilter();
  if (SetBits.size() > MaxFiltersPerRecord)
    return createStringError(std::errc::value_too_large,
                             "bill of materials needs %zu Bloom filters, more "
                             "than a record can describe",
                             SetBits.size());

  uint64_t TotalSetBits = 0;
  for (unsigned Bits : SetBits)
    TotalSetBits += Bits;
  double SetBitRate = double(TotalSetBits) / (FilterBits * SetBits.size());
  uint32_t Model = uint32_t(SetBitRate * UINT32_MAX);

  // A saturated filter compresses to a little over MaxSetBits bytes.
  SmallVector<uint8_t> Payload;
  Payload.reserve(SetBits.size() * MaxSetBits);
  encodeBits(Filters, getZeroWeight(Model), Payload);

  appendHeader(Out, SetBits.size(), Model, Payload.size());
  Out.append(Payload.begin(), Payload.end());
  return Error::success();
}

/// Decodes the Bloom filters of \p Payload into \p Filters.
///
/// The payload is only accepted if it is the encoding this implementation
/// would produce for the filters that came out of it. Arithmetic coding is
/// deterministic, so re-encoding is an exact test: it catches a truncated or
/// corrupted payload, which would otherwise decode to a filter that has lost
/// hashes, and so would answer that a source file is absent when it is not.
static Error decodeFilters(const Header &H, ArrayRef<uint8_t> Payload,
                           std::vector<uint64_t> &Filters) {
  uint32_t ZeroWeight = getZeroWeight(H.Model);
  Filters.assign(size_t(H.NumFilters) * FilterWords, 0);
  decodeBits(Payload, ZeroWeight, Filters);

  SmallVector<uint8_t> Reencoded;
  Reencoded.reserve(Payload.size());
  encodeBits(Filters, ZeroWeight, Reencoded);

  if (ArrayRef<uint8_t>(Reencoded) != Payload)
    return createStringError(std::errc::illegal_byte_sequence,
                             "ABOM payload is corrupt");

  // A filter denser than the format allows cannot have been produced by
  // inserting hashes into it, and would drive the arithmetic coder's model
  // into a degenerate state when the merged bill of materials is written.
  for (unsigned I = 0; I != H.NumFilters; ++I) {
    unsigned Bits = 0;
    for (uint64_t Word :
         ArrayRef(Filters).slice(size_t(I) * FilterWords, FilterWords))
      Bits += llvm::popcount(Word);
    if (Bits > MaxSetBits + NumHashFunctions)
      return createStringError(std::errc::illegal_byte_sequence,
                               "ABOM Bloom filter has %u bits set", Bits);
  }
  return Error::success();
}

Error BillOfMaterials::merge(ArrayRef<uint8_t> Data) {
  // A section may hold several records, for instance when a linker without
  // ABOM support has concatenated the sections of its inputs.
  std::vector<uint64_t> Filters;
  while (!Data.empty()) {
    Expected<Header> H = readHeader(Data);
    if (!H)
      return H.takeError();
    ArrayRef<uint8_t> Payload = Data.slice(HeaderSize, H->PayloadSize);

    // Decode the whole record before merging any of it, so that a record that
    // turns out to be malformed leaves this bill of materials alone.
    if (Error E = decodeFilters(*H, Payload, Filters))
      return E;
    for (unsigned I = 0; I != H->NumFilters; ++I)
      mergeFilter(
          ArrayRef(Filters).slice(size_t(I) * FilterWords, FilterWords));
    Data = Data.drop_front(H->getRecordSize());
  }
  return Error::success();
}

bool BillOfMaterials::contains(const Hash &H) const {
  if (is_contained(Hashes, H))
    return true;

  std::array<uint32_t, NumHashFunctions> Indices = getIndices(H);
  for (size_t I = 0, E = SetBits.size(); I != E; ++I) {
    ArrayRef<uint64_t> Filter = getFilter(I);
    if (all_of(Indices, [&](uint32_t Index) {
          return (Filter[Index / 64] >> (Index % 64)) & 1;
        }))
      return true;
  }
  return false;
}

size_t BillOfMaterials::getUnsaturatedFilter() {
  while (FirstUnsaturated != SetBits.size() &&
         SetBits[FirstUnsaturated] >= MaxSetBits)
    ++FirstUnsaturated;
  if (FirstUnsaturated == SetBits.size()) {
    Filters.resize(Filters.size() + FilterWords);
    SetBits.push_back(0);
  }
  return FirstUnsaturated;
}

void BillOfMaterials::foldHashes() {
  for (const Hash &H : Hashes) {
    size_t Idx = getUnsaturatedFilter();
    MutableArrayRef<uint64_t> Filter = getFilter(Idx);
    for (uint32_t Index : getIndices(H)) {
      uint64_t Mask = uint64_t(1) << (Index % 64);
      uint64_t &Word = Filter[Index / 64];
      SetBits[Idx] += !(Word & Mask);
      Word |= Mask;
    }
  }
  Hashes.clear();
}

void BillOfMaterials::mergeFilter(ArrayRef<uint64_t> Filter) {
  unsigned Bits = 0;
  for (uint64_t Word : Filter)
    Bits += llvm::popcount(Word);

  // Merge into the last filter, which is where room is most likely to be
  // found: a filter is appended only when the one before it could not hold the
  // union. Trying every filter instead would cost a pass over every bit array
  // for each filter merged, which is quadratic in their number, and finds a
  // home little more often.
  bool Fits = !SetBits.empty() && SetBits.back() < MaxSetBits;
  // The union holds no more bits than the two filters together, which is often
  // enough to tell that it fits without forming it.
  if (Fits && SetBits.back() + Bits >= MaxSetBits) {
    unsigned Merged = 0;
    for (size_t Word = 0; Word != FilterWords; ++Word)
      Merged +=
          llvm::popcount(getFilter(SetBits.size() - 1)[Word] | Filter[Word]);
    Fits = Merged < MaxSetBits;
  }
  if (!Fits) {
    Filters.insert(Filters.end(), Filter.begin(), Filter.end());
    SetBits.push_back(Bits);
    return;
  }

  MutableArrayRef<uint64_t> Existing = getFilter(SetBits.size() - 1);
  unsigned Merged = 0;
  for (size_t Word = 0; Word != FilterWords; ++Word) {
    Existing[Word] |= Filter[Word];
    Merged += llvm::popcount(Existing[Word]);
  }
  SetBits.back() = Merged;
}
