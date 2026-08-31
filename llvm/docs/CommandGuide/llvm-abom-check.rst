llvm-abom-check - query a binary's ABOM
=======================================

.. program:: llvm-abom-check

SYNOPSIS
--------

:program:`llvm-abom-check` [*options*] *input* [*hash...*]

DESCRIPTION
-----------

:program:`llvm-abom-check` queries the Automatic Bill of Materials of a binary
for the hashes of source files, as printed by :program:`llvm-abom-hash`. It
answers whether the binary was compiled from those source files, which is how a
compromised source file is traced to the binaries it reached.

The ``input`` may be an object file or image of any format that can carry a
bill of materials, an archive or universal binary of such files, a module
compiled for link time optimization, or a record extracted with
:program:`llvm-objcopy` ``--dump-section``.

The hashes to query may be given on the command line, or read from a file with
the :option:`--hashes` option. A hash may have a ``0x`` prefix.

An answer of present is subject to the false positive rate of the underlying
Bloom filters, which the format bounds at one in 16384 for a single filter. An
answer of absent is exact.

EXAMPLE
-------

.. code-block:: console

 $ llvm-abom-check hello d43f65e61
 hello: d43f65e61 is present

OPTIONS
-------

.. option:: --hashes=<filename>

 Read the hashes to query from ``filename``, one per line, in the form
 :program:`llvm-abom-hash` writes them. Anything after the hash on a line is
 ignored, and lines beginning with ``#`` are skipped, so a published list of
 compromised source files can be used as it is.

.. option:: --help

 Display a summary of command line options.

.. option:: --quiet, -q

 Report the result through the exit status alone.

.. option:: --summary

 Print a summary of the bill of materials rather than querying it.

.. option:: --version

 Display the version of the program.

EXIT STATUS
-----------

:program:`llvm-abom-check` returns 0 if every hash queried is recorded in the
binary, and 1 if any of them is not or if the input could not be read. This
makes it usable directly in scripts:

.. code-block:: console

 $ llvm-abom-check --quiet --hashes compromised.txt hello || echo affected

SEE ALSO
--------

:manpage:`llvm-abom-hash(1)`
