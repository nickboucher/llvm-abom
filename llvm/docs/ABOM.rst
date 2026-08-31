==============================
Automatic Bill of Materials
==============================

.. contents::
   :local:

Introduction
============

An Automatic Bill of Materials (ABOM) is a compact, queryable record of the
source files that a binary was compiled from. Every source file the compiler
reads is identified by its hash, and those hashes are stored in a section of
the object files and images the toolchain emits. Nothing is shipped alongside
the binary, and nothing has to be published by its author.

When a supply chain attack is discovered, the hash of the compromised source
file is all that is needed to determine whether a binary is affected:

.. code-block:: console

  $ clang -fabom -o hello hello.c
  $ llvm-abom-hash hello.c
  4f1c5ea0f  hello.c
  $ llvm-abom-check hello 4f1c5ea0f
  hello: 4f1c5ea0f is present

Because the bills of materials of a linker's inputs are merged into the bill of
materials of its output, a binary records the source files of its entire
dependency tree, including those of prebuilt libraries that carry an ABOM of
their own.

Using ABOM
==========

``clang -fabom`` records the source files of each translation unit, and the
linker merges them. The source files of a translation unit are the same set of
files that ``-MD`` reports, extended with system headers, since a header can
contain arbitrary code regardless of where it is found.

``llvm-abom-hash`` prints the hash of a source file, and ``llvm-abom-check``
queries a binary for one or more such hashes. The exit status of
``llvm-abom-check`` is zero if every hash it was given is recorded, so it can
be used directly in scripts:

.. code-block:: console

  $ llvm-abom-check --quiet --hashes compromised-hashes.txt libfoo.a || \
      echo "affected"

The file it reads holds one hash per line, in the form ``llvm-abom-hash``
writes them, so a published list of compromised hashes can be used as it is.

``llvm-abom-check`` accepts any object file format that can carry an ABOM,
archives and universal binaries of such files, modules compiled for link time
optimization, and ABOM records that have been extracted with
``llvm-objcopy --dump-section``.

Bloom filters answer membership queries without storing their elements, so a
query can report a hash as present that was never inserted. This happens with a
probability of about one in 16,000 per Bloom filter, and never in the opposite
direction: a source file that was compiled into a binary is always reported as
present.

Design
======

Source files are identified by the leading 36 bits of their SHAKE128 digest.
A hash of that size is small enough to index a Bloom filter directly, so
inserting a hash and querying for one need no further hashing.

The hashes are held in Bloom filters of 2\ :sup:`18` bits with two hash
functions, which are compressed with an arithmetic coder when written out.
Filters of that size and occupancy compress to a little over two bytes per
source file. Once a filter holds enough hashes for its false positive rate to
reach one in 16,384, further hashes are inserted into an additional filter, so
a bill of materials can describe any number of source files.

The parameters of the format, and the reasoning behind them, are described in
the ABOM specification. They are gathered at the top of
``llvm/include/llvm/BinaryFormat/ABOM.h``, and are the only values that need to
be revised if the specification changes.

Records
-------

A section holds one or more records. Each begins with a header of fifteen
bytes, in little-endian order:

.. code-block:: none

  +---------+----------------------------------------------------------------+
  |  0 -  3 | Magic word, "ABOM"                                              |
  |       4 | Format version, 1                                               |
  |  5 -  6 | Number of Bloom filters in the payload, at least one            |
  |  7 - 10 | Probability that a bit of the payload is set, scaled by 2^32-1  |
  | 11 - 14 | Length of the payload in bytes                                  |
  +---------+----------------------------------------------------------------+

The payload is the concatenated bit arrays of that many Bloom filters,
compressed with an arithmetic coder driven by the probability in the header.

Every artifact carries a record of this one form, relocatable objects
included, so that anything a compiler or linker emits can be read by anything
that knows the specification and nothing else.

A reader that encounters several records in a section, which happens when a
linker without ABOM support concatenates the sections of its inputs, merges
them all.

Reading a bill of materials
---------------------------

A record is rejected rather than merged if it is not the encoding this
implementation would have produced for the filters it decodes to. Arithmetic
coding is deterministic, so re-encoding the decoded filters is an exact test: a
payload that has been truncated or corrupted decodes to filters that have lost
hashes, and would answer that a source file is absent when it is not. Records
that describe implausible amounts of work, such as more Bloom filters than
their payload could hold, are rejected before any of that work is done.

Linkers treat a malformed record as an error. Merging past one would produce a
bill of materials that looks sound but has silently forgotten source files,
which is the one answer an ABOM must never give.

Sections
--------

+-------------+-------------------------+
| ELF         | ``.abom``               |
+-------------+-------------------------+
| COFF        | ``.abom``               |
+-------------+-------------------------+
| Mach-O      | ``__ABOM,__abom``       |
+-------------+-------------------------+
| WebAssembly | custom section ``abom`` |
+-------------+-------------------------+

Object file formats that have no section defined for it, such as XCOFF, emit
no bill of materials.

The section is not part of the load image: it is neither allocated in ELF nor
mapped in COFF, where it is marked discardable. It survives stripping, as a
bill of materials is only useful if it reaches whoever runs the binary.

Every artifact holds one bill of materials, whether it is a relocatable object,
an archive member, an executable or a shared library, and whether it came from
a compiler, from ``ld -r``, or from a full link.

A module compiled for link time optimization is the one thing that holds no
section, because it is not an object file: ``clang -flto -c`` writes bitcode,
which has no sections to put a record in. It carries the same record in its
``llvm.abom`` metadata instead, byte for byte what the section would hold, and
the backend that runs inside the linker writes it to the object file it
produces. ``llvm-abom-check`` reads the metadata, so such a module answers
queries like any other artifact, and ``-ffat-lto-objects`` produces a real
object file with a section as usual.

An image produced by a linker without ABOM support holds the records of its
inputs, concatenated rather than merged. Readers accept that, and merge them.

Implementation
==============

``clang -fabom`` collects the source files of a translation unit with the same
machinery that produces dependency files, and hashes them from the copies the
preprocessor has already read into memory. It builds the bill of materials from
those hashes and attaches it to the module as ``llvm.abom`` metadata, which the
AsmPrinter writes to the object file as it is. Carrying the record in the IR
means a module holds its bill of materials in exactly the form everything else
does, and that it survives optimization. The IR linker concatenates the
operands of module metadata, so a module linked from several others carries one
record per source module, which the AsmPrinter merges.

The source files that reach a translation unit through a precompiled header or
a module are recorded as well. A precompiled header or module carries the
hashes of its own source files, written into the file itself as a module file
extension, so that what is recorded is what was compiled into it rather than
whatever those files hold when it is imported. The hashes of the modules a
module imports are carried along with its own, so one record describes the
whole of what a translation unit compiles. A module built without ``-fabom``
carries no hashes, and its input files are hashed from disk instead. A
precompiled header is built with the option or without it, like the
compilation that uses it; a mismatch is reported.

Linkers merge the records of their inputs into one. This is a union of sets, so
the result does not depend on how a dependency was built. ``lld`` implements
this for ELF, COFF, Mach-O and WebAssembly; a linker without ABOM support
concatenates the sections of its inputs instead, which readers accept.

Decompressing a record is the expensive part of merging and is independent of
every other record, so linkers do it in parallel, in batches that bound how
many decoded filters are held at once. The filters are merged in the order the
inputs were given, so the output does not depend on how the work was
scheduled.

Assembly files that are handed to the assembler directly are not recorded: the
assembler finalizes its output while parsing, before the set of files it read
is known. Assembly generated by the compiler is recorded as usual, since the
record is emitted as part of the assembly itself.
