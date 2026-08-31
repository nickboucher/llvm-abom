//===--- DependencyFile.cpp - Generate dependency file --------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This code generates dependency files.
//
//===----------------------------------------------------------------------===//

#include "clang/Basic/DiagnosticFrontend.h"
#include "clang/Basic/FileManager.h"
#include "clang/Basic/MakeSupport.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Frontend/DependencyOutputOptions.h"
#include "clang/Frontend/Utils.h"
#include "clang/Lex/DirectoryLookup.h"
#include "clang/Lex/ModuleMap.h"
#include "clang/Lex/PPCallbacks.h"
#include "clang/Lex/Preprocessor.h"
#include "clang/Serialization/ASTBitCodes.h"
#include "clang/Serialization/ASTReader.h"
#include "clang/Serialization/ModuleFileExtension.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/BinaryFormat/ABOM.h"
#include "llvm/Bitstream/BitstreamWriter.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/raw_ostream.h"
#include <optional>

using namespace clang;

namespace {
struct DepCollectorPPCallbacks : public PPCallbacks {
  DependencyCollector &DepCollector;
  Preprocessor &PP;
  DepCollectorPPCallbacks(DependencyCollector &L, Preprocessor &PP)
      : DepCollector(L), PP(PP) {}

  void LexedFileChanged(FileID FID, LexedFileChangeReason Reason,
                        SrcMgr::CharacteristicKind FileType, FileID PrevFID,
                        SourceLocation Loc) override {
    if (Reason != PPCallbacks::LexedFileChangeReason::EnterFile)
      return;

    // Dependency generation really does want to go all the way to the
    // file entry for a source location to find out what is depended on.
    // We do not want #line markers to affect dependency generation!
    if (std::optional<StringRef> Filename =
            PP.getSourceManager().getNonBuiltinFilenameForID(FID))
      DepCollector.maybeAddDependency(
          llvm::sys::path::remove_leading_dotslash(*Filename),
          /*FromModule*/ false, isSystem(FileType), /*IsModuleFile*/ false,
          /*IsDirectModuleImport*/ false, /*IsMissing*/ false);
  }

  void FileSkipped(const FileEntryRef &SkippedFile, const Token &FilenameTok,
                   SrcMgr::CharacteristicKind FileType) override {
    StringRef Filename =
        llvm::sys::path::remove_leading_dotslash(SkippedFile.getName());
    DepCollector.maybeAddDependency(Filename, /*FromModule=*/false,
                                    /*IsSystem=*/isSystem(FileType),
                                    /*IsModuleFile=*/false,
                                    /*IsDirectModuleImport=*/false,
                                    /*IsMissing=*/false);
  }

  void EmbedDirective(SourceLocation, StringRef, bool,
                      OptionalFileEntryRef File,
                      const LexEmbedParametersResult &) override {
    assert(File && "expected to only be called when the file is found");
    StringRef FileName =
        llvm::sys::path::remove_leading_dotslash(File->getName());
    DepCollector.maybeAddDependency(FileName,
                                    /*FromModule*/ false,
                                    /*IsSystem*/ false,
                                    /*IsModuleFile*/ false,
                                    /*IsDirectModuleImport*/ false,
                                    /*IsMissing*/ false);
  }

  bool EmbedFileNotFound(StringRef FileName) override {
    DepCollector.maybeAddDependency(
        llvm::sys::path::remove_leading_dotslash(FileName),
        /*FromModule=*/false,
        /*IsSystem=*/false,
        /*IsModuleFile=*/false,
        /*IsDirectModuleImport=*/false,
        /*IsMissing=*/true);
    // Return true to silence the file not found diagnostic.
    return true;
  }

  void InclusionDirective(SourceLocation HashLoc, const Token &IncludeTok,
                          StringRef FileName, bool IsAngled,
                          CharSourceRange FilenameRange,
                          OptionalFileEntryRef File, StringRef SearchPath,
                          StringRef RelativePath, const Module *SuggestedModule,
                          bool ModuleImported,
                          SrcMgr::CharacteristicKind FileType) override {
    if (!File)
      DepCollector.maybeAddDependency(FileName, /*FromModule*/ false,
                                      /*IsSystem*/ false,
                                      /*IsModuleFile*/ false,
                                      /*IsDirectModuleImport*/ false,
                                      /*IsMissing*/ true);
    // Files that actually exist are handled by FileChanged.
  }

  void HasEmbed(SourceLocation, StringRef, bool,
                OptionalFileEntryRef File) override {
    if (!File)
      return;
    StringRef Filename =
        llvm::sys::path::remove_leading_dotslash(File->getName());
    DepCollector.maybeAddDependency(Filename,
                                    /*FromModule=*/false, false,
                                    /*IsModuleFile=*/false,
                                    /*IsDirectModuleImport=*/false,
                                    /*IsMissing=*/false);
  }

  void HasInclude(SourceLocation Loc, StringRef SpelledFilename, bool IsAngled,
                  OptionalFileEntryRef File,
                  SrcMgr::CharacteristicKind FileType) override {
    if (!File)
      return;
    StringRef Filename =
        llvm::sys::path::remove_leading_dotslash(File->getName());
    DepCollector.maybeAddDependency(Filename, /*FromModule=*/false,
                                    /*IsSystem=*/isSystem(FileType),
                                    /*IsModuleFile=*/false,
                                    /*IsDirectModuleImport=*/false,
                                    /*IsMissing=*/false);
  }

  void EndOfMainFile() override {
    DepCollector.finishedMainFile(PP.getDiagnostics());
  }
};

struct DepCollectorMMCallbacks : public ModuleMapCallbacks {
  DependencyCollector &DepCollector;
  DepCollectorMMCallbacks(DependencyCollector &DC) : DepCollector(DC) {}

  void moduleMapFileRead(SourceLocation Loc, FileEntryRef Entry,
                         bool IsSystem) override {
    StringRef Filename = Entry.getName();
    DepCollector.maybeAddDependency(Filename, /*FromModule*/ false,
                                    /*IsSystem*/ IsSystem,
                                    /*IsModuleFile*/ false,
                                    /*IsDirectModuleImport*/ false,
                                    /*IsMissing*/ false);
  }
};

struct DepCollectorASTListener : public ASTReaderListener {
  DependencyCollector &DepCollector;
  FileManager &FileMgr;
  DepCollectorASTListener(DependencyCollector &L, FileManager &FileMgr)
      : DepCollector(L), FileMgr(FileMgr) {}
  bool needsInputFileVisitation() override { return true; }
  bool needsSystemInputFileVisitation() override {
    return DepCollector.needSystemDependencies();
  }
  void visitModuleFile(ModuleFileName Filename, serialization::ModuleKind Kind,
                       bool DirectlyImported) override {
    DepCollector.maybeAddDependency(Filename, /*FromModule*/ true,
                                    /*IsSystem*/ false, /*IsModuleFile*/ true,
                                    /*IsDirectModuleImport*/ DirectlyImported,
                                    /*IsMissing*/ false);
  }
  bool visitInputFile(StringRef Filename, bool IsSystem,
                      bool IsOverridden, bool IsExplicitModule) override {
    if (IsOverridden ||
        (IsExplicitModule && !DepCollector.needExplicitModuleDependencies()))
      return true;

    // Run this through the FileManager in order to respect 'use-external-name'
    // in case we have a VFS overlay.
    if (auto FE = FileMgr.getOptionalFileRef(Filename))
      Filename = FE->getName();

    DepCollector.maybeAddDependency(Filename, /*FromModule*/ true, IsSystem,
                                    /*IsModuleFile*/ false,
                                    /*IsDirectModuleImport*/ false,
                                    /*IsMissing*/ false);
    return true;
  }
};
} // end anonymous namespace

void DependencyCollector::maybeAddDependency(StringRef Filename,
                                             bool FromModule, bool IsSystem,
                                             bool IsModuleFile,
                                             bool IsDirectModuleImport,
                                             bool IsMissing) {
  if (sawDependency(Filename, FromModule, IsSystem, IsModuleFile,
                    IsDirectModuleImport, IsMissing))
    addDependency(Filename);
}

bool DependencyCollector::addDependency(StringRef Filename) {
  StringRef SearchPath;
#ifdef _WIN32
  // Make the search insensitive to case and separators.
  llvm::SmallString<256> TmpPath = Filename;
  llvm::sys::path::native(TmpPath);
  std::transform(TmpPath.begin(), TmpPath.end(), TmpPath.begin(), ::tolower);
  SearchPath = TmpPath.str();
#else
  SearchPath = Filename;
#endif

  if (Seen.insert(SearchPath).second) {
    Dependencies.push_back(std::string(Filename));
    return true;
  }
  return false;
}

static bool isSpecialFilename(StringRef Filename) {
  return Filename == "<built-in>";
}

bool DependencyCollector::sawDependency(StringRef Filename, bool FromModule,
                                        bool IsSystem, bool IsModuleFile,
                                        bool IsDirectModuleImport,
                                        bool IsMissing) {
  return !isSpecialFilename(Filename) &&
         (needSystemDependencies() || !IsSystem);
}

DependencyCollector::~DependencyCollector() { }
void DependencyCollector::attachToPreprocessor(Preprocessor &PP) {
  PP.addPPCallbacks(std::make_unique<DepCollectorPPCallbacks>(*this, PP));
  PP.getHeaderSearchInfo().getModuleMap().addModuleMapCallbacks(
      std::make_unique<DepCollectorMMCallbacks>(*this));
}
void DependencyCollector::attachToASTReader(ASTReader &R) {
  R.addListener(
      std::make_unique<DepCollectorASTListener>(*this, R.getFileManager()));
}

bool AbomCollector::sawDependency(StringRef Filename, bool FromModule,
                                  bool IsSystem, bool IsModuleFile,
                                  bool IsDirectModuleImport, bool IsMissing) {
  // A missing file has no contents to hash, and a module file is a build
  // artifact whose own inputs are reported separately.
  if (IsMissing || IsModuleFile)
    return false;
  return DependencyCollector::sawDependency(Filename, FromModule, IsSystem,
                                            IsModuleFile, IsDirectModuleImport,
                                            IsMissing);
}

namespace {
/// Carries the ABOM hashes of the source files a precompiled header or module
/// was built from inside the file itself.
///
/// The source files of a module are compiled into whatever imports it, so they
/// belong in the bill of materials of the importing object. Hashing them from
/// disk when the module is imported would record whatever they hold then; the
/// hashes stored here are of the contents that were compiled.
class AbomModuleFileExtension
    : public llvm::RTTIExtends<AbomModuleFileExtension, ModuleFileExtension> {
public:
  static char ID;

  AbomModuleFileExtension(AbomCollector &Collector) : Collector(Collector) {}

  ModuleFileExtensionMetadata getExtensionMetadata() const override {
    return {std::string(llvm::abom::MetadataName), 1, 0, "ABOM source hashes"};
  }

  std::unique_ptr<ModuleFileExtensionWriter>
  createExtensionWriter(ASTWriter &Writer) override;

  std::unique_ptr<ModuleFileExtensionReader>
  createExtensionReader(const ModuleFileExtensionMetadata &Metadata,
                        ASTReader &Reader, serialization::ModuleFile &Mod,
                        const llvm::BitstreamCursor &Stream) override;

  AbomCollector &Collector;
};

char AbomModuleFileExtension::ID = 0;

class AbomExtensionWriter : public ModuleFileExtensionWriter {
public:
  AbomExtensionWriter(AbomModuleFileExtension *Ext)
      : ModuleFileExtensionWriter(Ext) {}

  void writeExtensionContents(Sema &SemaRef,
                              llvm::BitstreamWriter &Stream) override {
    auto Abv = std::make_shared<llvm::BitCodeAbbrev>();
    Abv->Add(llvm::BitCodeAbbrevOp(serialization::FIRST_EXTENSION_RECORD_ID));
    Abv->Add(llvm::BitCodeAbbrevOp(llvm::BitCodeAbbrevOp::Blob));
    unsigned Abbrev = Stream.EmitAbbrev(std::move(Abv));

    // The hashes of every source file this file was built from, including
    // those of the modules it imports, so that one record describes the whole
    // of what a reader compiles.
    std::string Hashes = cast<AbomModuleFileExtension>(getExtension())
                             ->Collector.computeHashes(
                                 SemaRef.getSourceManager());
    uint64_t Record[] = {serialization::FIRST_EXTENSION_RECORD_ID};
    Stream.EmitRecordWithBlob(Abbrev, Record, Hashes);
  }
};

class AbomExtensionReader : public ModuleFileExtensionReader {
public:
  AbomExtensionReader(AbomModuleFileExtension *Ext,
                      llvm::BitstreamCursor Stream)
      : ModuleFileExtensionReader(Ext) {
    while (true) {
      llvm::Expected<llvm::BitstreamEntry> Entry =
          Stream.advanceSkippingSubblocks();
      if (!Entry) {
        consumeError(Entry.takeError());
        return;
      }
      if (Entry->Kind != llvm::BitstreamEntry::Record)
        return;

      SmallVector<uint64_t, 1> Record;
      StringRef Blob;
      llvm::Expected<unsigned> Code =
          Stream.readRecord(Entry->ID, Record, &Blob);
      if (!Code) {
        consumeError(Code.takeError());
        return;
      }
      if (*Code == serialization::FIRST_EXTENSION_RECORD_ID &&
          Blob.size() % llvm::abom::HashSize == 0)
        Ext->Collector.addImportedHashes(Blob);
    }
  }
};
} // end anonymous namespace

std::unique_ptr<ModuleFileExtensionWriter>
AbomModuleFileExtension::createExtensionWriter(ASTWriter &Writer) {
  return std::make_unique<AbomExtensionWriter>(this);
}

std::unique_ptr<ModuleFileExtensionReader>
AbomModuleFileExtension::createExtensionReader(
    const ModuleFileExtensionMetadata &Metadata, ASTReader &Reader,
    serialization::ModuleFile &Mod, const llvm::BitstreamCursor &Stream) {
  return std::make_unique<AbomExtensionReader>(this, Stream);
}

std::shared_ptr<ModuleFileExtension>
clang::createAbomModuleFileExtension(AbomCollector &Collector) {
  return std::make_shared<AbomModuleFileExtension>(Collector);
}

std::string AbomCollector::computeHashes(SourceManager &SM) const {
  std::string Hashes = ImportedHashes;
  Hashes.reserve(Hashes.size() +
                 getDependencies().size() * llvm::abom::HashSize);

  // Distinct dependencies can name the same file, for instance through a
  // symbolic link, in which case they share a buffer.
  llvm::SmallPtrSet<const char *, 32> Seen;
  for (StringRef Filename : getDependencies()) {
    OptionalFileEntryRef File =
        SM.getFileManager().getOptionalFileRef(Filename);
    if (!File)
      continue;
    std::optional<llvm::MemoryBufferRef> Buffer =
        SM.getMemoryBufferForFileOrNone(*File);
    if (!Buffer || !Seen.insert(Buffer->getBufferStart()).second)
      continue;
    llvm::abom::Hash H = llvm::abom::computeHash(Buffer->getBuffer());
    Hashes.append(H.begin(), H.end());
  }
  return Hashes;
}

DependencyFileGenerator::DependencyFileGenerator(
    const DependencyOutputOptions &Opts)
    : OutputFile(Opts.OutputFile), Targets(Opts.Targets),
      IncludeSystemHeaders(Opts.IncludeSystemHeaders),
      PhonyTarget(Opts.UsePhonyTargets),
      AddMissingHeaderDeps(Opts.AddMissingHeaderDeps), SeenMissingHeader(false),
      IncludeModuleFiles(
          static_cast<ModuleFileDepsKind>(Opts.IncludeModuleFiles)),
      OutputFormat(Opts.OutputFormat), InputFileIndex(0) {
  for (const auto &ExtraDep : Opts.ExtraDeps) {
    if (addDependency(ExtraDep.first))
      ++InputFileIndex;
  }
}

void DependencyFileGenerator::attachToPreprocessor(Preprocessor &PP) {
  // Disable the "file not found" diagnostic if the -MG option was given.
  if (AddMissingHeaderDeps)
    PP.SetSuppressIncludeNotFoundError(true);

  DependencyCollector::attachToPreprocessor(PP);
}

bool DependencyFileGenerator::sawDependency(StringRef Filename, bool FromModule,
                                            bool IsSystem, bool IsModuleFile,
                                            bool IsDirectModuleImport,
                                            bool IsMissing) {
  if (IsMissing) {
    // Handle the case of missing file from an inclusion directive.
    if (AddMissingHeaderDeps)
      return true;
    SeenMissingHeader = true;
    return false;
  }
  if (IsModuleFile) {
    if (IncludeModuleFiles == MFDK_None)
      return false;
    if (IncludeModuleFiles == MFDK_Direct && !IsDirectModuleImport)
      return false;
  }

  if (isSpecialFilename(Filename))
    return false;

  if (IncludeSystemHeaders)
    return true;

  return !IsSystem;
}

void DependencyFileGenerator::finishedMainFile(DiagnosticsEngine &Diags) {
  outputDependencyFile(Diags);
}

void DependencyFileGenerator::outputDependencyFile(DiagnosticsEngine &Diags) {
  if (SeenMissingHeader) {
    llvm::sys::fs::remove(OutputFile);
    return;
  }

  std::error_code EC;
  llvm::raw_fd_ostream OS(OutputFile, EC, llvm::sys::fs::OF_TextWithCRLF);
  if (EC) {
    Diags.Report(diag::err_fe_error_opening) << OutputFile << EC.message();
    return;
  }

  outputDependencyFile(OS);
}

void DependencyFileGenerator::outputDependencyFile(llvm::raw_ostream &OS) {
  clang::printMakeDependencyFile(OS, Targets, getDependencies(), OutputFormat,
                                 PhonyTarget, InputFileIndex);
}
