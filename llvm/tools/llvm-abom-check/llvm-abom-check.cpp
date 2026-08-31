//===- llvm-abom-check.cpp - ABOM queries ---------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This program queries the Automatic Bill of Materials of a binary for the
// hashes of source files, as printed by llvm-abom-hash. It accepts any object
// file format that can carry an ABOM, archives of such files, and ABOM records
// that have been extracted to a file of their own.
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/StringExtras.h"
#include "llvm/BinaryFormat/ABOM.h"
#include "llvm/BinaryFormat/Magic.h"
#include "llvm/Bitcode/BitcodeReader.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Module.h"
#include "llvm/Object/Archive.h"
#include "llvm/Object/Binary.h"
#include "llvm/Object/MachO.h"
#include "llvm/Object/MachOUniversal.h"
#include "llvm/Object/ObjectFile.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/InitLLVM.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/WithColor.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;
using namespace llvm::object;

static cl::OptionCategory AbomCategory("llvm-abom-check options");

static cl::opt<std::string> InputFilename(cl::Positional, cl::Required,
                                          cl::desc("<input file>"),
                                          cl::cat(AbomCategory));

static cl::list<std::string> Hashes(cl::Positional, cl::desc("[<hash>...]"),
                                    cl::cat(AbomCategory));

static cl::opt<std::string>
    HashFile("hashes",
             cl::desc("Read hashes to query from a file, one per line, as "
                      "llvm-abom-hash writes them"),
             cl::value_desc("filename"), cl::cat(AbomCategory));

static cl::opt<bool> Quiet("quiet",
                           cl::desc("Report the result through the exit status "
                                    "alone"),
                           cl::cat(AbomCategory));
static cl::alias QuietAlias("q", cl::desc("Alias for --quiet"),
                            cl::aliasopt(Quiet));

static cl::opt<bool> Summary("summary",
                             cl::desc("Print a summary of the bill of "
                                      "materials"),
                             cl::cat(AbomCategory));

static StringRef ToolName;

[[noreturn]] static void reportError(const Twine &Message) {
  WithColor::error(errs(), ToolName) << Message << "\n";
  exit(EXIT_FAILURE);
}

[[noreturn]] static void reportError(Error E, StringRef Filename) {
  handleAllErrors(std::move(E), [&](const ErrorInfoBase &EI) {
    reportError(Filename + ": " + EI.message());
  });
  llvm_unreachable("reportError does not return");
}

/// Returns true if \p Section is the section that carries ABOM records in the
/// format of the object file holding it.
static bool isAbomSection(SectionRef Section, StringRef Name) {
  if (const auto *MachO = dyn_cast<MachOObjectFile>(Section.getObject())) {
    StringRef Segment =
        MachO->getSectionFinalSegmentName(Section.getRawDataRefImpl());
    return Segment == abom::MachOSegmentName && Name == abom::MachOSectionName;
  }
  return Name == abom::SectionName || Name == abom::WasmSectionName;
}

/// Merges the ABOM of \p Buffer into \p BOM, whatever kind of file it holds.
/// \p Depth is how many archives deep the buffer was found.
static void mergeBuffer(abom::BillOfMaterials &BOM, MemoryBufferRef Buffer,
                        StringRef Filename, unsigned Depth = 0);

/// Merges the ABOM of every object of \p Bin into \p BOM. The binary may be an
/// object file, an archive, or a universal binary holding either.
static void mergeBinary(abom::BillOfMaterials &BOM, Binary &Bin,
                        StringRef Filename, unsigned Depth);

/// Merges the bill of materials a bitcode module carries into \p BOM.
///
/// A module compiled for link time optimization is not an object file and has
/// no sections, so it carries its record as metadata until the backend that
/// runs inside the linker writes it to the object file it produces.
static void mergeBitcode(abom::BillOfMaterials &BOM, MemoryBufferRef Buffer,
                         StringRef Filename) {
  LLVMContext Context;
  Expected<std::unique_ptr<Module>> M = parseBitcodeFile(Buffer, Context);
  if (!M)
    reportError(M.takeError(), Filename);

  const NamedMDNode *NMD = (*M)->getNamedMetadata(abom::MetadataName);
  if (!NMD)
    return;
  for (const MDNode *N : NMD->operands()) {
    if (N->getNumOperands() != 1)
      reportError(Filename + ": malformed " + abom::MetadataName + " metadata");
    const auto *S = dyn_cast<MDString>(N->getOperand(0));
    if (!S)
      reportError(Filename + ": malformed " + abom::MetadataName + " metadata");
    if (Error E = BOM.merge(arrayRefFromStringRef(S->getString())))
      reportError(std::move(E), Filename);
  }
}

/// Merges the ABOM records of every section of \p Obj into \p BOM.
/// Largest number of archives that may be nested inside one another.
static constexpr unsigned MaxArchiveDepth = 16;

static void mergeObject(abom::BillOfMaterials &BOM, const ObjectFile &Obj) {
  for (SectionRef Section : Obj.sections()) {
    Expected<StringRef> Name = Section.getName();
    if (!Name)
      reportError(Name.takeError(), Obj.getFileName());
    if (!isAbomSection(Section, *Name))
      continue;
    Expected<StringRef> Contents = Section.getContents();
    if (!Contents)
      reportError(Contents.takeError(), Obj.getFileName());
    if (Error E = BOM.merge(arrayRefFromStringRef(*Contents)))
      reportError(std::move(E), Obj.getFileName());
  }
}

/// Merges the ABOM of \p Filename into \p BOM. The file may be an object file,
/// an archive of object files, or an ABOM record of its own.
static void mergeFile(abom::BillOfMaterials &BOM, StringRef Filename) {
  ErrorOr<std::unique_ptr<MemoryBuffer>> Buffer =
      MemoryBuffer::getFileOrSTDIN(Filename, /*IsText=*/false,
                                   /*RequiresNullTerminator=*/false);
  if (!Buffer)
    reportError(Filename + ": " + Buffer.getError().message());
  MemoryBufferRef Ref = (*Buffer)->getMemBufferRef();

  // A record that has been extracted from a binary, for instance with
  // llvm-objcopy --dump-section.
  if (Ref.getBuffer().starts_with(abom::Magic)) {
    if (Error E = BOM.merge(arrayRefFromStringRef(Ref.getBuffer())))
      reportError(std::move(E), Filename);
    return;
  }

  mergeBuffer(BOM, Ref, Filename);
}

static void mergeBuffer(abom::BillOfMaterials &BOM, MemoryBufferRef Buffer,
                        StringRef Filename, unsigned Depth) {
  if (identify_magic(Buffer.getBuffer()) == file_magic::bitcode) {
    mergeBitcode(BOM, Buffer, Filename);
    return;
  }

  Expected<std::unique_ptr<Binary>> Bin = createBinary(Buffer);
  if (!Bin)
    reportError(Bin.takeError(), Filename);
  mergeBinary(BOM, **Bin, Filename, Depth);
}

static void mergeBinary(abom::BillOfMaterials &BOM, Binary &Bin,
                        StringRef Filename, unsigned Depth) {
  if (auto *Obj = dyn_cast<ObjectFile>(&Bin)) {
    mergeObject(BOM, *Obj);
  } else if (auto *Archive = dyn_cast<object::Archive>(&Bin)) {
    // An archive can hold archives, but a file that nests them without end is
    // not worth following.
    if (Depth == MaxArchiveDepth)
      reportError(Filename + ": archives are nested too deeply");
    Error E = Error::success();
    for (const object::Archive::Child &Child : Archive->children(E)) {
      Expected<MemoryBufferRef> Member = Child.getMemoryBufferRef();
      if (!Member)
        reportError(Member.takeError(), Filename);
      mergeBuffer(BOM, *Member, Filename, Depth + 1);
    }
    if (E)
      reportError(std::move(E), Filename);
  } else if (auto *Universal = dyn_cast<MachOUniversalBinary>(&Bin)) {
    // Query every slice of a universal binary: they are built from the same
    // sources, but a slice may well have been built with different options.
    for (const MachOUniversalBinary::ObjectForArch &Slice :
         Universal->objects()) {
      if (Expected<std::unique_ptr<MachOObjectFile>> Obj =
              Slice.getAsObjectFile())
        mergeObject(BOM, **Obj);
      else if (Expected<std::unique_ptr<object::Archive>> Ar =
                   (consumeError(Obj.takeError()), Slice.getAsArchive()))
        mergeBinary(BOM, **Ar, Filename, Depth);
      else
        reportError(Ar.takeError(), Filename);
    }
  } else {
    reportError(Filename + ": unsupported file format");
  }
}

int main(int Argc, char **Argv) {
  InitLLVM X(Argc, Argv);
  ToolName = Argv[0];
  cl::HideUnrelatedOptions(AbomCategory);
  cl::ParseCommandLineOptions(
      Argc, Argv,
      "Query the Automatic Bill of Materials of a binary for the hashes of "
      "source files.\n\nThe exit status is zero if every hash is recorded and "
      "one if any of them is not.\n");

  // Hashes may be given on the command line, in a file, or both. A file holds
  // one per line, and anything after the hash is ignored so that the output of
  // llvm-abom-hash can be used as it is.
  std::vector<std::string> Queries(Hashes.begin(), Hashes.end());
  if (!HashFile.empty()) {
    ErrorOr<std::unique_ptr<MemoryBuffer>> Buffer =
        MemoryBuffer::getFileOrSTDIN(HashFile);
    if (!Buffer)
      reportError(HashFile + ": " + Buffer.getError().message());
    for (StringRef Line : llvm::split((*Buffer)->getBuffer(), '\n')) {
      Line =
          Line.take_until([](char C) { return C == ' ' || C == '\t'; }).trim();
      if (!Line.empty() && !Line.starts_with("#"))
        Queries.push_back(Line.str());
    }
  }

  abom::BillOfMaterials BOM;
  mergeFile(BOM, InputFilename);

  if (BOM.empty() && !Quiet)
    WithColor::warning(errs(), ToolName)
        << InputFilename << ": no bill of materials found\n";

  if (Summary)
    outs() << InputFilename << ": " << BOM.getNumFilters() << " Bloom filter"
           << (BOM.getNumFilters() == 1 ? "" : "s") << "\n";

  bool AllPresent = true;
  for (StringRef Hash : Queries) {
    std::optional<abom::Hash> Value = abom::parseHash(Hash);
    if (!Value)
      reportError("'" + Hash + "' is not an ABOM hash");
    bool Present = BOM.contains(*Value);
    AllPresent &= Present;
    if (!Quiet)
      outs() << InputFilename << ": " << Hash << " is "
             << (Present ? "present" : "absent") << "\n";
  }
  return AllPresent ? EXIT_SUCCESS : EXIT_FAILURE;
}
