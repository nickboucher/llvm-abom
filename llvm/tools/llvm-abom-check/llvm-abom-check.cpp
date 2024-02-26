#include "AbomCheckOpts.inc"
#include "llvm/BinaryFormat/ABOM.h"
#include "llvm/BinaryFormat/Magic.h"
#include "llvm/Object/ObjectFile.h"
#include "llvm/Option/ArgList.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/InitLLVM.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/WithColor.h"
#include <map>
#include <string>

using namespace llvm;
using namespace llvm::object;

namespace {
enum ID {
  OPT_INVALID = 0, // This is not an option ID.
#define OPTION(...) LLVM_MAKE_OPT_ID(__VA_ARGS__),
#include "AbomCheckOpts.inc"
#undef OPTION
};

#define PREFIX(NAME, VALUE)                                                    \
  static constexpr StringLiteral NAME##_init[] = VALUE;                        \
  static constexpr ArrayRef<StringLiteral> NAME(NAME##_init,                   \
                                                std::size(NAME##_init) - 1);
#include "AbomCheckOpts.inc"
#undef PREFIX

using namespace llvm::opt;
static constexpr opt::OptTable::Info InfoTable[] = {
#define OPTION(...) LLVM_CONSTRUCT_OPT_INFO(__VA_ARGS__),
#include "AbomCheckOpts.inc"
#undef OPTION
};

class StringsOptTable : public opt::GenericOptTable {
public:
  StringsOptTable() : GenericOptTable(InfoTable) {
    setGroupedShortOptions(true);
    setDashDashParsing(true);
  }
};
} // namespace

static StringRef ToolName;

static cl::list<std::string> InputFileNames(cl::Positional,
                                            cl::desc("<input object files>"));

[[noreturn]] static void error(const Twine &Message) {
  WithColor::error(errs(), ToolName) << Message << "\n";
  exit(1);
}

[[noreturn]] void reportError(Error Err, StringRef Input) {
  assert(Err);
  if (Input == "-")
    Input = "<stdin>";
  handleAllErrors(createFileError(Input, std::move(Err)),
                  [&](const ErrorInfoBase &EI) { error(EI.message()); });
  llvm_unreachable("error() call should never return");
}

template <class T> T unwrapOrError(StringRef Input, Expected<T> EO) {
  if (EO)
    return *EO;
  reportError(EO.takeError(), Input);
}


static std::vector<object::SectionRef>
getSectionRefsByNameOrIndex(const object::ObjectFile &Obj,
                            ArrayRef<std::string> Sections) {
  std::vector<object::SectionRef> Ret;
  std::map<std::string, bool> SecNames;
  std::map<unsigned, bool> SecIndices;
  unsigned SecIndex;
  for (StringRef Section : Sections) {
    if (!Section.getAsInteger(0, SecIndex))
      SecIndices.emplace(SecIndex, false);
    else
      SecNames.emplace(std::string(Section), false);
  }

  SecIndex = Obj.isELF() ? 0 : 1;
  for (object::SectionRef SecRef : Obj.sections()) {
    StringRef SecName = unwrapOrError(Obj.getFileName(), SecRef.getName());
    auto NameIt = SecNames.find(std::string(SecName));
    if (NameIt != SecNames.end())
      NameIt->second = true;
    auto IndexIt = SecIndices.find(SecIndex);
    if (IndexIt != SecIndices.end())
      IndexIt->second = true;
    if (NameIt != SecNames.end() || IndexIt != SecIndices.end())
      Ret.push_back(SecRef);
    SecIndex++;
  }

  for (const std::pair<const std::string, bool> &S : SecNames)
    if (!S.second)
      reportError(createError("could not find section " + S.first),
          Obj.getFileName());

  for (std::pair<unsigned, bool> S : SecIndices)
    if (!S.second)
      reportError(createError("could not find section " + std::to_string(S.first)),
          Obj.getFileName());

  return Ret;
}

static void abomCheck(raw_ostream &OS, StringRef FileName, StringRef Hash) {
  ErrorOr<std::unique_ptr<MemoryBuffer>> FileOrErr =
      MemoryBuffer::getFileOrSTDIN(FileName, /*IsText=*/false,
                                   /*RequiresNullTerminator=*/false);
  if (std::error_code EC = FileOrErr.getError())
    reportError(errorCodeToError(EC), FileName);

  std::unique_ptr<MemoryBuffer> &Buffer = FileOrErr.get();
  file_magic Type = identify_magic(Buffer->getBuffer());
  if (Type != file_magic::elf && Type != file_magic::elf_relocatable
      && Type != file_magic::elf_executable && Type != file_magic::elf_shared_object
      && Type != file_magic::elf_core) {
    error(FileName + ": unsupported file type");
  }
  Expected<std::unique_ptr<Binary>> BinaryOrErr = createBinary(
      Buffer->getMemBufferRef(), /*Context=*/nullptr, /*InitContent=*/false);
  if (!BinaryOrErr)
    reportError(BinaryOrErr.takeError(), FileName);

  std::unique_ptr<Binary> Bin = std::move(*BinaryOrErr);
  ObjectFile *Obj = dyn_cast<ObjectFile>(Bin.get());
  if (!Obj)
    error(FileName + ": unsupported file type");

  std::vector<object::SectionRef> Sections = getSectionRefsByNameOrIndex(
      *Obj, {".abom"});

  if (Sections.empty())
    error(FileName + ": no .abom section found");

  if (Sections.size() > 1)
    error(FileName + ": corrupted ABOM - multiple .abom sections found");

  StringRef Contents = unwrapOrError(FileName, Sections[0].getContents());

  ArrayRef<uint8_t> abomBytes(
      reinterpret_cast<const uint8_t *>(Contents.data()), Contents.size());

  auto abom = ABOM::ABOM::deserialize(abomBytes);
  hash_t digest;
  if (!ABOM::SHAKE128::fromHex(Hash.data(), digest)) return;

  if (abom.contains(digest)) {
    OS << "Present: " << FileName << " contains " << Hash << "\n";
  } else {
    OS << "Absent: " << FileName << " does not contain " << Hash << "\n";
  }
}

int main(int argc, char **argv) {
  InitLLVM X(argc, argv);
  BumpPtrAllocator A;
  StringSaver Saver(A);
  StringsOptTable Tbl;
  ToolName = argv[0];
  opt::InputArgList Args =
      Tbl.parseArgs(argc, argv, OPT_UNKNOWN, Saver,
                    [&](StringRef Msg) { error(Msg); });
  if (Args.hasArg(OPT_help)) {
    Tbl.printHelp(
        outs(),
        (Twine(ToolName) + " [options] <input object file> <hash>").str().c_str(),
        "llvm ABOM hash checker");
    // TODO Replace this with OptTable API once it adds extrahelp support.
    outs() << "\nPass @FILE and @HASH as arguments to check whether FILE contains HASH.\n";
    return 0;
  }
  if (Args.hasArg(OPT_version)) {
    outs() << ToolName << '\n';
    cl::PrintVersionMessage();
    return 0;
  }

  std::vector<std::string> Inputs = Args.getAllArgValues(OPT_INPUT);
  if (Inputs.size() != 2)
    error("exactly one input file and one hash must be specified");

  abomCheck(outs(), Inputs[0], Inputs[1]);

  return EXIT_SUCCESS;
}
