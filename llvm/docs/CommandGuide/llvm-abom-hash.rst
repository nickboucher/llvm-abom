llvm-abom-hash - print ABOM source file hashes
==============================================

.. program:: llvm-abom-hash

SYNOPSIS
--------

:program:`llvm-abom-hash` [*options*] [*input...*]

DESCRIPTION
-----------

:program:`llvm-abom-hash` prints the Automatic Bill of Materials hash of each
``input`` file, which is the value :program:`llvm-abom-check` queries a binary
for. The hash of a source file is the leading 36 bits of its SHAKE128 digest,
printed as a hexadecimal string.

If "``-``" is specified as an ``input``, or no ``input`` is specified, the
program reads from the standard input stream.

The output has one line per file, holding the hash and the file name separated
by two spaces, in the form :program:`llvm-abom-check` reads with its
``--hashes`` option.

EXAMPLE
-------

.. code-block:: console

 $ llvm-abom-hash hello.c
 d43f65e61  hello.c

OPTIONS
-------

.. option:: --help

 Display a summary of command line options.

.. option:: --version

 Display the version of the program.

EXIT STATUS
-----------

:program:`llvm-abom-hash` returns 0 unless it could not read an input file.

SEE ALSO
--------

:manpage:`llvm-abom-check(1)`
