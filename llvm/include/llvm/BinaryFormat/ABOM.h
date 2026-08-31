//===- ABOM.h - Automatic Bill of Materials ---------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file
/// This file implements the Automatic Bill of Materials (ABOM): a compact,
/// queryable record of the source files that were compiled into a binary.
///
/// An ABOM stores the SHAKE128 hash of every source file consumed by the
/// compiler in a set of compressed Bloom filters, which are embedded in a
/// dedicated section of the objects and images emitted by the toolchain. When
/// a supply chain attack is discovered, the hash of the compromised source
/// file can be queried against a binary's ABOM to determine whether that
/// binary is affected, without shipping any additional metadata alongside it.
///
/// A section carries one or more records, each of which begins with a fixed
/// size header followed by the concatenated bit arrays of one or more Bloom
/// filters, compressed with an arithmetic coder. Every artifact the toolchain
/// emits carries a record of this form, relocatable objects included, so that
/// a reader that knows the specification and nothing else can read any of
/// them.
///
/// Merging is a union of sets, so an ABOM records the source files of a
/// binary's entire dependency tree, including those of any prebuilt library
/// that carries an ABOM of its own.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_BINARYFORMAT_ABOM_H
#define LLVM_BINARYFORMAT_ABOM_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/STLFunctionalExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Error.h"
#include <array>
#include <cstdint>
#include <optional>
#include <string>

namespace llvm {
namespace abom {

/// \name Specification parameters
///
/// These constants define the ABOM format. They are expected to change if the
/// specification is revised; nothing else in the implementation hardcodes
/// them.
/// @{

/// Base-2 logarithm of the number of bits in a Bloom filter (m).
constexpr unsigned Log2FilterBits = 18;

/// Number of hash functions used by each Bloom filter (k).
constexpr unsigned NumHashFunctions = 2;

/// Highest false positive rate tolerated for a single Bloom filter (f). Once a
/// filter's estimated false positive rate reaches this bound, further hashes
/// are inserted into an additional filter.
constexpr double MaxFalsePositiveRate = 0x1p-14;

/// Number of bits that may be set in a Bloom filter before its false positive
/// rate, (x/m)^k, reaches MaxFalsePositiveRate. This is m * f^(1/k); the
/// static assertions below check it against the parameters above. A filter is
/// checked for room before a hash is inserted rather than after, so it can end
/// up with as many as NumHashFunctions - 1 bits more than this.
constexpr unsigned MaxSetBits = 2048;

/// Magic word introducing every ABOM record.
constexpr StringRef Magic = "ABOM";

/// Version of the ABOM binary protocol emitted by this implementation.
constexpr uint8_t FormatVersion = 1;

/// Largest number of Bloom filters in a record, which is what both a reader
/// and a writer will handle. The format allows for 65535 of them, but this
/// many already describe more than a million source files, and decoding a
/// record that claims far more would cost more than reading any real bill of
/// materials.
constexpr unsigned MaxFiltersPerRecord = 1024;

/// Fewest payload bytes a record must hold for each Bloom filter it declares,
/// beyond the first. A filter costs about FilterBits * H(p) bits to encode,
/// where p is the record's model, so a filter that holds any hashes at all
/// runs to a thousand bytes or more. Requiring a small fraction of that keeps
/// a short record from asking a reader to decode megabytes of Bloom filters.
constexpr unsigned MinPayloadBytesPerFilter = 64;

/// Precision of the arithmetic coder's range, in bits.
constexpr unsigned CodeBits = 32;

/// Number of bits used to represent the arithmetic coder's cumulative
/// distribution function.
constexpr unsigned CDFBits = 16;

/// Name of the section holding ABOM records in ELF and COFF files.
constexpr StringRef SectionName = ".abom";

/// Segment and section names holding ABOM records in Mach-O files.
constexpr StringRef MachOSegmentName = "__ABOM";
constexpr StringRef MachOSectionName = "__abom";

/// Name of the WebAssembly custom section holding ABOM records.
constexpr StringRef WasmSectionName = "abom";

/// Name of the module metadata holding the hashes of the source files that a
/// module was compiled from. Its operands each hold the concatenated hashes of
/// one translation unit.
constexpr StringRef MetadataName = "llvm.abom";

/// @}

/// Number of bits in a Bloom filter (m).
constexpr size_t FilterBits = size_t(1) << Log2FilterBits;

/// Number of 64-bit words holding the bit array of one Bloom filter.
constexpr size_t FilterWords = FilterBits / 64;

/// Largest fraction of a Bloom filter's bits that a well formed record can
/// report as set. Inserting a hash sets no more than NumHashFunctions bits,
/// and a filter is only used while it has room, so a filter that claims to be
/// denser than this was not produced by inserting hashes into it.
constexpr double MaxSetBitRate =
    double(MaxSetBits + NumHashFunctions) / FilterBits;

/// Number of bits of hash consumed by a single Bloom filter insertion, which
/// is also the length of an ABOM source file hash.
constexpr unsigned HashBits = Log2FilterBits * NumHashFunctions;

/// Number of bytes needed to store an ABOM source file hash. Any bits of the
/// last byte beyond HashBits are zero.
constexpr size_t HashSize = (HashBits + 7) / 8;

/// Number of bytes in the header preceding the payload of an ABOM record.
constexpr size_t HeaderSize = 15;

/// The hash of a single source file: the leading HashBits bits of its SHAKE128
/// digest.
using Hash = std::array<uint8_t, HashSize>;

/// Returns the ABOM hash of the source file contents \p Data.
LLVM_ABI Hash computeHash(ArrayRef<uint8_t> Data);
inline Hash computeHash(StringRef Data) {
  return computeHash(
      ArrayRef(reinterpret_cast<const uint8_t *>(Data.data()), Data.size()));
}

/// Returns \p H as the hexadecimal string used to exchange ABOM hashes.
LLVM_ABI std::string toString(const Hash &H);

/// Parses the hexadecimal representation of an ABOM hash, which may have a
/// "0x" prefix. Returns std::nullopt if \p Str is not a well formed hash.
LLVM_ABI std::optional<Hash> parseHash(StringRef Str);

/// The header preceding the payload of every ABOM record.
struct Header {
  /// Number of Bloom filters in the payload, at least one.
  uint16_t NumFilters;
  /// The arithmetic coder's model, the probability that a bit of the
  /// concatenated Bloom filters is set, scaled by UINT32_MAX.
  uint32_t Model;
  /// Length of the payload following the header, in bytes.
  uint32_t PayloadSize;

  /// Total length of the record, in bytes.
  uint64_t getRecordSize() const { return HeaderSize + uint64_t(PayloadSize); }
};

/// Parses the ABOM record header at the start of \p Data.
LLVM_ABI Expected<Header> readHeader(ArrayRef<uint8_t> Data);

/// The set of source file hashes recorded for a binary.
class BillOfMaterials {
public:
  /// Adds the hash of a single source file.
  void insert(const Hash &H) { Hashes.push_back(H); }

  /// Adds the source file hashes in \p PackedHashes, the concatenation of
  /// HashSize byte hashes.
  LLVM_ABI void insert(ArrayRef<uint8_t> PackedHashes);

  /// Merges every ABOM record in \p Data, which is the contents of an ABOM
  /// section and may hold more than one record.
  LLVM_ABI Error merge(ArrayRef<uint8_t> Data);

  /// Merges the contents of every ABOM section in \p Sections. The records are
  /// decompressed in parallel, and merged in the order given so that the
  /// result does not depend on how the work was scheduled. This is how a
  /// linker should merge its inputs; \p Describe names the file a section came
  /// from, for diagnostics.
  LLVM_ABI Error mergeAll(ArrayRef<ArrayRef<uint8_t>> Sections,
                          function_ref<std::string(size_t)> Describe);

  /// Returns true if \p H was inserted into this bill of materials, up to the
  /// false positive rate of the underlying Bloom filters.
  LLVM_ABI bool contains(const Hash &H) const;

  /// Returns true if no hash has been recorded.
  bool empty() const { return Hashes.empty() && Filters.empty(); }

  /// Returns the number of Bloom filters merged so far, which is zero until
  /// the hashes added to this bill of materials are folded into filters by
  /// writeFilters().
  size_t getNumFilters() const { return SetBits.size(); }

  /// Appends an ABOM record holding the Bloom filters of this bill of
  /// materials to \p Out. Any hash that is not yet part of a filter is folded
  /// into one, so the result is a complete record in the form described by the
  /// ABOM specification. Fails if more filters are needed than a record can
  /// describe, which takes upwards of a million source files.
  LLVM_ABI Error writeFilters(SmallVectorImpl<uint8_t> &Out);

private:
  /// Returns the bit array of Bloom filter \p Idx.
  MutableArrayRef<uint64_t> getFilter(size_t Idx) {
    return MutableArrayRef(Filters).slice(Idx * FilterWords, FilterWords);
  }
  ArrayRef<uint64_t> getFilter(size_t Idx) const {
    return ArrayRef(Filters).slice(Idx * FilterWords, FilterWords);
  }

  /// Returns the index of the first filter with room for more hashes,
  /// appending a new filter if all of them are saturated.
  size_t getUnsaturatedFilter();

  /// Inserts every recorded hash into a Bloom filter.
  void foldHashes();

  /// Merges the bit array \p Filter into the filters of this bill of
  /// materials.
  void mergeFilter(ArrayRef<uint64_t> Filter);

  /// The concatenated bit arrays of the Bloom filters, in little-endian bit
  /// order, FilterWords words each.
  std::vector<uint64_t> Filters;

  /// The number of bits set in each Bloom filter.
  std::vector<unsigned> SetBits;

  /// The hashes that are not yet part of a Bloom filter.
  std::vector<Hash> Hashes;

  /// The first filter that may still have room for more hashes. Filters only
  /// ever gain bits, so no filter before this one can become unsaturated.
  size_t FirstUnsaturated = 0;
};

// The number of bits that may be set in a filter must be the point at which
// the false positive rate of the filter reaches the tolerated maximum.
namespace detail {
constexpr double pow(double Base, unsigned Exp) {
  return Exp == 0 ? 1.0 : Base * pow(Base, Exp - 1);
}
} // end namespace detail
static_assert(detail::pow(double(MaxSetBits) / FilterBits, NumHashFunctions) >=
                  MaxFalsePositiveRate,
              "MaxSetBits exceeds the tolerated false positive rate");
static_assert(detail::pow(double(MaxSetBits - 1) / FilterBits,
                          NumHashFunctions) < MaxFalsePositiveRate,
              "MaxSetBits is smaller than the tolerated false positive rate "
              "allows");
static_assert(FilterBits % 64 == 0, "Bloom filters must be a whole number of "
                                    "words long");
static_assert(HashBits <= 64, "ABOM hashes must fit in a 64-bit word");

} // end namespace abom
} // end namespace llvm

#endif // LLVM_BINARYFORMAT_ABOM_H
