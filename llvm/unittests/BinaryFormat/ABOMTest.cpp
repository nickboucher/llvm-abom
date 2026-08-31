//===- ABOMTest.cpp - Automatic Bill of Materials tests -------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/BinaryFormat/ABOM.h"
#include "llvm/Support/Endian.h"
#include "llvm/Testing/Support/Error.h"
#include "gtest/gtest.h"

using namespace llvm;
using namespace llvm::abom;

namespace {

/// Returns the hash of a source file whose contents are derived from \p Seed.
Hash makeHash(unsigned Seed) {
  return computeHash(StringRef("source file " + std::to_string(Seed)));
}

TEST(ABOMTest, HashesAreTruncatedDigests) {
  // The ABOM hash is the leading HashBits bits of the SHAKE128 digest, which
  // for "abc" starts with 0x5881092dd8.
  Hash H = computeHash(StringRef("abc"));
  EXPECT_EQ("5881092dd", toString(H));
  EXPECT_EQ(0x58u, H[0]);
  // Bits beyond HashBits are cleared.
  EXPECT_EQ(0xd0u, H[HashSize - 1]);
}

TEST(ABOMTest, ParseHash) {
  Hash H = computeHash(StringRef("abc"));
  EXPECT_EQ(H, parseHash("5881092dd"));
  EXPECT_EQ(H, parseHash("0x5881092dd"));
  EXPECT_EQ(H, parseHash("0X5881092DD"));
  EXPECT_EQ(std::nullopt, parseHash(""));
  EXPECT_EQ(std::nullopt, parseHash("5881092d"));
  EXPECT_EQ(std::nullopt, parseHash("5881092dd8"));
  EXPECT_EQ(std::nullopt, parseHash("5881092dg"));
}

// Hashes may be added from the packed form the compiler records them in.
TEST(ABOMTest, PackedHashes) {
  SmallVector<uint8_t> Packed;
  for (unsigned I = 0; I != 8; ++I) {
    Hash H = makeHash(I);
    Packed.append(H.begin(), H.end());
  }

  BillOfMaterials BOM;
  BOM.insert(Packed);
  SmallVector<uint8_t> Record;
  ASSERT_THAT_ERROR(BOM.writeFilters(Record), Succeeded());

  BillOfMaterials Merged;
  EXPECT_THAT_ERROR(Merged.merge(Record), Succeeded());
  for (unsigned I = 0; I != 8; ++I)
    EXPECT_TRUE(Merged.contains(makeHash(I)));
  EXPECT_FALSE(Merged.contains(makeHash(9)));
}

TEST(ABOMTest, FilterRecord) {
  BillOfMaterials BOM;
  for (unsigned I = 0; I != 100; ++I)
    BOM.insert(makeHash(I));

  SmallVector<uint8_t> Record;
  ASSERT_THAT_ERROR(BOM.writeFilters(Record), Succeeded());
  EXPECT_EQ(1u, BOM.getNumFilters());

  Expected<Header> H = readHeader(Record);
  ASSERT_THAT_EXPECTED(H, Succeeded());
  EXPECT_EQ(1u, H->NumFilters);
  EXPECT_EQ(Record.size(), H->getRecordSize());

  BillOfMaterials Merged;
  EXPECT_THAT_ERROR(Merged.merge(Record), Succeeded());
  for (unsigned I = 0; I != 100; ++I)
    EXPECT_TRUE(Merged.contains(makeHash(I)));
  EXPECT_FALSE(Merged.contains(makeHash(1000)));
}

TEST(ABOMTest, EmptyBillOfMaterials) {
  BillOfMaterials BOM;
  EXPECT_TRUE(BOM.empty());

  SmallVector<uint8_t> Record;
  ASSERT_THAT_ERROR(BOM.writeFilters(Record), Succeeded());
  EXPECT_EQ(1u, BOM.getNumFilters());

  BillOfMaterials Merged;
  EXPECT_THAT_ERROR(Merged.merge(Record), Succeeded());
  EXPECT_FALSE(Merged.contains(makeHash(0)));
}

// Hashes are spread over as many filters as are needed to keep the false
// positive rate of each one within the tolerated bound.
TEST(ABOMTest, Saturation) {
  BillOfMaterials BOM;
  for (unsigned I = 0; I != 3000; ++I)
    BOM.insert(makeHash(I));

  SmallVector<uint8_t> Record;
  ASSERT_THAT_ERROR(BOM.writeFilters(Record), Succeeded());
  EXPECT_EQ(3u, BOM.getNumFilters());

  BillOfMaterials Merged;
  EXPECT_THAT_ERROR(Merged.merge(Record), Succeeded());
  EXPECT_EQ(3u, Merged.getNumFilters());
  for (unsigned I = 0; I != 3000; ++I)
    EXPECT_TRUE(Merged.contains(makeHash(I)));
}

// A section may hold several records, for example when a linker without ABOM
// support concatenates the sections of its inputs.
TEST(ABOMTest, ConcatenatedRecords) {
  BillOfMaterials First;
  First.insert(makeHash(1));
  BillOfMaterials Second;
  Second.insert(makeHash(2));

  SmallVector<uint8_t> Records;
  ASSERT_THAT_ERROR(First.writeFilters(Records), Succeeded());
  ASSERT_THAT_ERROR(Second.writeFilters(Records), Succeeded());

  BillOfMaterials Merged;
  EXPECT_THAT_ERROR(Merged.merge(Records), Succeeded());
  EXPECT_TRUE(Merged.contains(makeHash(1)));
  EXPECT_TRUE(Merged.contains(makeHash(2)));
  EXPECT_FALSE(Merged.contains(makeHash(3)));
}

TEST(ABOMTest, MalformedRecords) {
  BillOfMaterials BOM;
  BOM.insert(makeHash(0));
  SmallVector<uint8_t> Record;
  ASSERT_THAT_ERROR(BOM.writeFilters(Record), Succeeded());

  SmallVector<uint8_t> Bad(Record);
  Bad[0] = 'X';
  EXPECT_THAT_ERROR(BillOfMaterials().merge(Bad), Failed());

  Bad = Record;
  Bad[4] = FormatVersion + 1;
  EXPECT_THAT_ERROR(BillOfMaterials().merge(Bad), Failed());

  Bad = Record;
  Bad.truncate(HeaderSize - 1);
  EXPECT_THAT_ERROR(BillOfMaterials().merge(Bad), Failed());

  // A payload length that runs past the end of the section.
  Bad = Record;
  support::endian::write32le(Bad.data() + 11, Bad.size());
  EXPECT_THAT_ERROR(BillOfMaterials().merge(Bad), Failed());

  // A record that declares no Bloom filters describes nothing.
  Bad = Record;
  support::endian::write16le(Bad.data() + 5, 0);
  EXPECT_THAT_ERROR(BillOfMaterials().merge(Bad), Failed());
}

// Sections are merged in the order given, whatever order they were decoded in.
TEST(ABOMTest, MergeAll) {
  SmallVector<SmallVector<uint8_t>, 0> Records;
  for (unsigned I = 0; I != 200; ++I) {
    BillOfMaterials BOM;
    for (unsigned J = 0; J != 20; ++J)
      BOM.insert(makeHash(I * 20 + J));
    Records.emplace_back();
    ASSERT_THAT_ERROR(BOM.writeFilters(Records.back()), Succeeded());
  }

  SmallVector<ArrayRef<uint8_t>, 0> Sections;
  for (const SmallVector<uint8_t> &R : Records)
    Sections.push_back(R);

  BillOfMaterials Merged;
  EXPECT_THAT_ERROR(
      Merged.mergeAll(Sections, [](size_t I) { return std::to_string(I); }),
      Succeeded());
  for (unsigned I = 0; I != 200 * 20; ++I)
    EXPECT_TRUE(Merged.contains(makeHash(I)));

  SmallVector<uint8_t> Record;
  ASSERT_THAT_ERROR(Merged.writeFilters(Record), Succeeded());
  EXPECT_EQ(4u, Merged.getNumFilters());

  // A malformed section is reported with the name it was given.
  SmallVector<uint8_t> Bad(Records[3]);
  Bad[0] = 'X';
  Sections[3] = Bad;
  std::string Message = toString(BillOfMaterials().mergeAll(
      Sections, [](size_t I) { return "input" + std::to_string(I); }));
  EXPECT_EQ("input3: invalid ABOM magic word", Message);
}

// A record must not be able to ask for more decoding than its payload can
// possibly describe.
TEST(ABOMTest, ImplausibleFilterCount) {
  SmallVector<uint8_t> Record(HeaderSize, 0);
  std::copy(Magic.begin(), Magic.end(), Record.begin());
  Record[4] = FormatVersion;
  support::endian::write16le(Record.data() + 5, UINT16_MAX);

  BillOfMaterials BOM;
  EXPECT_THAT_ERROR(BOM.merge(Record), Failed());
  EXPECT_TRUE(BOM.empty());

  // A filter count within the payload's reach but beyond what any bill of
  // materials holds is rejected too.
  Record.resize(HeaderSize + MaxFiltersPerRecord + 1, 0);
  support::endian::write16le(Record.data() + 5, MaxFiltersPerRecord + 1);
  support::endian::write32le(Record.data() + 11, MaxFiltersPerRecord + 1);
  EXPECT_THAT_ERROR(BillOfMaterials().merge(Record), Failed());
}

// A record can only describe so many filters, and a bill of materials that
// needs more says so rather than writing one that cannot be read back.
TEST(ABOMTest, TooManyFilters) {
  BillOfMaterials BOM;
  for (unsigned I = 0, E = (MaxFiltersPerRecord + 1) * 1028; I != E; ++I)
    BOM.insert(makeHash(I));

  SmallVector<uint8_t> Record;
  EXPECT_THAT_ERROR(BOM.writeFilters(Record), Failed());
  EXPECT_TRUE(Record.empty());
}

// Decoding is the expensive part of reading a record, so a record that claims
// more Bloom filters than its payload could hold is turned away before any of
// that work is done.
TEST(ABOMTest, ImplausiblePayload) {
  BillOfMaterials BOM;
  for (unsigned I = 0; I != 4000; ++I)
    BOM.insert(makeHash(I));

  SmallVector<uint8_t> Record;
  ASSERT_THAT_ERROR(BOM.writeFilters(Record), Succeeded());
  Header H = cantFail(readHeader(Record));
  ASSERT_EQ(4u, H.NumFilters);

  // The record itself is plausible, at about two kilobytes for each filter.
  EXPECT_GT(H.PayloadSize, (H.NumFilters - 1) * MinPayloadBytesPerFilter);

  // Claiming the largest number of filters the format allows is not.
  SmallVector<uint8_t> Bad(Record);
  support::endian::write16le(Bad.data() + 5, MaxFiltersPerRecord);
  EXPECT_THAT_ERROR(BillOfMaterials().merge(Bad), Failed());
}

// A model that claims a density no filter can reach leaves the coder with no
// room for a clear bit.
TEST(ABOMTest, ImpossibleModel) {
  BillOfMaterials BOM;
  BOM.insert(makeHash(0));
  SmallVector<uint8_t> Record;
  ASSERT_THAT_ERROR(BOM.writeFilters(Record), Succeeded());

  for (uint32_t Model : {UINT32_MAX, UINT32_MAX / 2, uint32_t(1) << 26}) {
    SmallVector<uint8_t> Bad(Record);
    support::endian::write32le(Bad.data() + 7, Model);
    EXPECT_THAT_ERROR(BillOfMaterials().merge(Bad), Failed())
        << "model " << Model;
  }
}

// A payload that has lost or gained bytes decodes to a filter that has lost
// hashes, which would answer that a source file is absent when it is not.
TEST(ABOMTest, CorruptPayload) {
  BillOfMaterials BOM;
  for (unsigned I = 0; I != 100; ++I)
    BOM.insert(makeHash(I));
  SmallVector<uint8_t> Record;
  ASSERT_THAT_ERROR(BOM.writeFilters(Record), Succeeded());

  // Truncated by one byte.
  SmallVector<uint8_t> Bad(Record);
  Bad.pop_back();
  support::endian::write32le(Bad.data() + 11, Bad.size() - HeaderSize);
  BillOfMaterials Merged;
  EXPECT_THAT_ERROR(Merged.merge(Bad), Failed());
  EXPECT_TRUE(Merged.empty());

  // A single flipped bit.
  Bad = Record;
  Bad[HeaderSize + Bad.size() / 2] ^= 1;
  EXPECT_THAT_ERROR(BillOfMaterials().merge(Bad), Failed());

  // A model that disagrees with the payload.
  Bad = Record;
  support::endian::write32le(Bad.data() + 7,
                             support::endian::read32le(Bad.data() + 7) * 2 + 1);
  EXPECT_THAT_ERROR(BillOfMaterials().merge(Bad), Failed());
}

// The false positive rate of a nearly saturated filter must stay within the
// bound the parameters were chosen for.
TEST(ABOMTest, FalsePositiveRate) {
  BillOfMaterials BOM;
  for (unsigned I = 0; I != 1000; ++I)
    BOM.insert(makeHash(I));
  SmallVector<uint8_t> Record;
  ASSERT_THAT_ERROR(BOM.writeFilters(Record), Succeeded());
  ASSERT_EQ(1u, BOM.getNumFilters());

  // The measured rate fluctuates around the expected one, so allow for a
  // generous margin rather than making the test flaky.
  unsigned FalsePositives = 0;
  constexpr unsigned Trials = 500000;
  for (unsigned I = 0; I != Trials; ++I)
    FalsePositives += BOM.contains(makeHash(1000000 + I));
  EXPECT_LT(double(FalsePositives) / Trials, 2 * MaxFalsePositiveRate);
}

} // end anonymous namespace
