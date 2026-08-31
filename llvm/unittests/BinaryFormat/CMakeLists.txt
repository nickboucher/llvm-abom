set(LLVM_LINK_COMPONENTS
  BinaryFormat
  TargetParser
  TestingSupport
  )

add_llvm_unittest(BinaryFormatTests
  ABOMTest.cpp
  DwarfTest.cpp
  ELFTest.cpp
  MachOTest.cpp
  MsgPackDocumentTest.cpp
  MsgPackReaderTest.cpp
  MsgPackWriterTest.cpp
  SFrameTest.cpp
  TestFileMagic.cpp
  )

