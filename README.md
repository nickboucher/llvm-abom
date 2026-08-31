# LLVM, with ABOM Support

This repository contains a fork of the [LLVM project](https://github.com/llvm/llvm-project)
with support for Automatic Bill of Materials, or ABOM. ABOM is a system for
automatically generating a novel form of bills of materials for software
projects. The ABOM system is described in the following paper:
[*Automatic Bill of Materials*](https://arxiv.org/abs/2310.09742).

The design of the implementation, the binary format it produces, and the
sections that carry it are documented in
[llvm/docs/ABOM.rst](llvm/docs/ABOM.rst).

## Generating an ABOM

To generate an ABOM when compiling using clang, use the `-fabom` flag, e.g.:
```sh
clang -fabom -o hello hello.c
```
Every artifact carries a bill of materials, relocatable objects included, and
`lld` merges the ones it links, for ELF, COFF, Mach-O and WebAssembly outputs.
If `lld` is not the default linker on your system, you can select it with the
`-fuse-ld` flag:
```sh
clang -fabom -fuse-ld=lld -o hello hello.c
```
Linkers without ABOM support concatenate the records of their inputs instead of
merging them, which the tools below still accept.

## Calculating an ABOM Hash

ABOM hashes are simply SHAKE128(36) hashes of source code files.

For convenience, ABOM hashes can be generated using the `llvm-abom-hash` tool:
```sh
llvm-abom-hash hello.c
```
This will output the hash as a hex string alongside the file name, e.g.:
```
d43f65e61  hello.c
```

## Checking a Binary for an ABOM Hash

To query a binary for an ABOM hash, use the `llvm-abom-check` tool:
```sh
llvm-abom-check hello d43f65e61
```
This will output whether the binary contains the specified ABOM hash:
```
hello: d43f65e61 is present
```
The exit status is zero if every hash given is recorded and one if any of them
is not, so the tool can be used directly in scripts. Hashes can also be read
from a file, in the form `llvm-abom-hash` writes them:
```sh
llvm-abom-check --quiet --hashes compromised-hashes.txt hello || echo "affected"
```
Object files, archives, and ABOM records extracted with
`llvm-objcopy --dump-section` can be queried in the same way.

## Examining an ABOM

To see what a binary's bill of materials is made of, pass `--summary`:
```sh
llvm-abom-check --summary hello
```
```
hello: 1 Bloom filter
```

## Acknowledgements

Thank you to:
- The [LLVM Project](https://github.com/llvm/llvm-project) for providing the base compiler, linker, and toolchain.
- Ed Warnicke and Bharathi Seshadri for the understanding of LLVM internals provided by [llvm-omnibor](https://github.com/omnibor/llvm-omnibor).
- Tongda Xu, et al. for the arithmetic coding implementation provided by [YAECL](https://github.com/tongdaxu/YAECL-Yet-Another-Entropy-Coding-Library), with which the coder in this project is bit compatible.

## Citation

If you use this software in your research, please cite the following paper:

```tex
@misc{boucher2023automatic,
      title={Automatic Bill of Materials},
      author={Nicholas Boucher and Ross Anderson},
      year={2023},
      eprint={2310.09742},
      archivePrefix={arXiv},
      primaryClass={cs.CR}
}
```

# The LLVM Compiler Infrastructure

[![OpenSSF Scorecard](https://api.securityscorecards.dev/projects/github.com/llvm/llvm-project/badge)](https://securityscorecards.dev/viewer/?uri=github.com/llvm/llvm-project)
[![OpenSSF Best Practices](https://www.bestpractices.dev/projects/8273/badge)](https://www.bestpractices.dev/projects/8273)
[![libc++](https://github.com/llvm/llvm-project/actions/workflows/libcxx-pr-conformance-tests.yaml/badge.svg?branch=main&event=schedule)](https://github.com/llvm/llvm-project/actions/workflows/libcxx-pr-conformance-tests.yaml?query=event%3Aschedule)

Welcome to the LLVM project!

This repository contains the source code for LLVM, a toolkit for the
construction of highly optimized compilers, optimizers, and run-time
environments.

The LLVM project has multiple components. The core of the project is
itself called "LLVM". This contains all of the tools, libraries, and header
files needed to process intermediate representations and convert them into
object files. Tools include an assembler, disassembler, bitcode analyzer, and
bitcode optimizer.

C-like languages use the [Clang](https://clang.llvm.org/) frontend. This
component compiles C, C++, Objective-C, and Objective-C++ code into LLVM bitcode
-- and from there into object files, using LLVM.

Other components include:
the [libc++ C++ standard library](https://libcxx.llvm.org),
the [LLD linker](https://lld.llvm.org), and more.

## Getting the Source Code and Building LLVM

Consult the
[Getting Started with LLVM](https://llvm.org/docs/GettingStarted.html#getting-the-source-code-and-building-llvm)
page for information on building and running LLVM.

For information on how to contribute to the LLVM project, please take a look at
the [Contributing to LLVM](https://llvm.org/docs/Contributing.html) guide.

## Getting in touch

Join the [LLVM Discourse forums](https://discourse.llvm.org/), [Discord
chat](https://discord.gg/xS7Z362),
[LLVM Office Hours](https://llvm.org/docs/GettingInvolved.html#office-hours) or
[Regular sync-ups](https://llvm.org/docs/GettingInvolved.html#online-sync-ups).

The LLVM project has adopted a [code of conduct](https://llvm.org/docs/CodeOfConduct.html) for
participants to all modes of communication within the project.
