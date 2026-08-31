//===- llvm-abom-hash.cpp - ABOM source file hashing ----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This program prints the Automatic Bill of Materials hash of source files,
// which is the value that llvm-abom-check queries a binary for.
//
//===----------------------------------------------------------------------===//

#include "llvm/BinaryFormat/ABOM.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/InitLLVM.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/WithColor.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

static cl::OptionCategory AbomCategory("llvm-abom-hash options");

static cl::list<std::string> InputFilenames(cl::Positional,
                                            cl::desc("<input files>"),
                                            cl::cat(AbomCategory));

int main(int Argc, char **Argv) {
  InitLLVM X(Argc, Argv);
  cl::HideUnrelatedOptions(AbomCategory);
  cl::ParseCommandLineOptions(
      Argc, Argv,
      "Print the Automatic Bill of Materials hash of each source file.\n");

  if (InputFilenames.empty())
    InputFilenames.push_back("-");

  bool Failed = false;
  for (StringRef Filename : InputFilenames) {
    ErrorOr<std::unique_ptr<MemoryBuffer>> Buffer =
        MemoryBuffer::getFileOrSTDIN(Filename);
    if (!Buffer) {
      WithColor::error(errs(), Argv[0])
          << Filename << ": " << Buffer.getError().message() << "\n";
      Failed = true;
      continue;
    }
    outs() << abom::toString(abom::computeHash((*Buffer)->getBuffer())) << "  "
           << (Filename == "-" ? "<stdin>" : Filename) << "\n";
  }
  return Failed ? EXIT_FAILURE : EXIT_SUCCESS;
}
