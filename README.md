# LLVM, with ABOM Support

This repository contains a fork of the [LLVM project](https://github.com/llvm/llvm-project)
with support for Automatic Bill of Materials, or ABOM. ABOM is a system
for automatically generating a novel form of bills of materials for software
projects. The ABOM system is described in the following paper:
[*Automatic Bill of Materials*](https://arxiv.org/abs/2310.09742).

## Getting Started

Consult the
[Getting Started with LLVM](https://llvm.org/docs/GettingStarted.html#getting-the-source-code-and-building-llvm)
page for information on building and running LLVM from the source contained
in this repository.

## Generating an ABOM

To generate an ABOM when compiling using clang, use the `-fabom` flag, e.g.:
```sh
clang -fabom -o hello hello.c
```
Note that you must also use a linker that supports ABOM, such as `lld` within this project.
If `lld` is not the default linker on your system, you can specify it using the `-fuse-ld` flag:
```sh
clang -fabom -fuse-ld=lld -o hello hello.c
```

## Calculating an ABOM Hash

ABOM hashes are simply SHAKE128(36) hashes of source code files.

For conveinence, ABOM hashes can be generated using the `llvm-abom-hash` tool:
```sh
llvm-abom-hash hello.c
```
This will output the hash as a hex string, e.g.:
```
d43f65e61
```

## Check Binary for an ABOM Hash

To query a binary for an ABOM hash, use the `llvm-abom-check` tool:
```sh
llvm-abom-check hello d43f65e61
```
This will output whether the binary contains the specified ABOM hash:
```
Present: hello contains d43f65e61
```

## Examining an ABOM

To examine an ABOM contained within a binary, leverage the `llvm-readobj` tool:
```sh
llvm-readobj -abom hello
```
This will output information about the ABOM header:
```
File: hello
Format: elf64-x86-64
Arch: x86_64
AddressSize: 64bit
LoadName: <Not found>

ABOM version: 1
ABOM num filters: 0
ABOM model: 0
ABOM blob size: 1
```

## Acknowledgements

Thank you to:
- The [LLVM Project](https://github.com/llvm/llvm-project) for providing the base compiler, linker, and toolchain.
- Ed Warnicke and Bharathi Seshadri for the understanding of LLVM internals provided by [llvm-omnibor](https://github.com/omnibor/llvm-omnibor).
- Tongda Xu, et al. for the Arithmetic Coding implementation provided by [YAECL](https://github.com/tongdaxu/YAECL-Yet-Another-Entropy-Coding-Library).

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