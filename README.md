Potato-16 C Compiler
=============

This is a ported compiler of 8cc (shown below) to target my processor project's ISA. It features a 16-bit, single cycle system with MMIO support. The new target is still a WIP, but can compile basic functions to assembly (.s) files. FP and L/D support is eventually planned in the ISA.

-----

8cc C Compiler
==============

Note: 8cc is no longer an active project. The successor is
[chibicc](https://github.com/rui314/chibicc).

8cc is a compiler for the C programming language.
It's intended to support all C11 language features
while keeping the code as small and simple as possible.

The compiler is able to compile itself.
You can see its code both as an implementation of the C language
and as an example of what this compiler is able to compile.

8cc's source code is carefully written to be as concise and easy-to-read
as possible, so that the source code becomes good study material
to learn about various techniques used in compilers.
You may find the lexer, the preprocessor and the parser are
already useful to learn how C source code is processed at each stage.

It's not an optimizing compiler.
Generated code is usually 2x or more slower than GCC.
I plan to implement a reasonable level of optimization in the future.

8cc supports x86-64 Linux only. I have no plan to make it portable until
I fix all known miscompilations and implement an optimization pass.
As of 2015, I'm using Ubuntu 14 as my development platform.
It should work on other x86-64 Linux distributions though.

Note: Do not have high expectations on this compiler.
If you try to compile a program other than the compiler itself,
there's a good chance to see compile errors or miscompilations.
This is basically a one-man project, and I have spent only a few
months of my spare time so far.

Build
-----

Run make to build:

    make

The default build uses the x86-64 backend. To build the EX_ISA backend,
select it explicitly:

    make clean
    make TARGET=ex-isa

The compiler target option must match the backend selected at build time.
Use `-m64` for the default build or `-mex-isa` for the EX_ISA build.
EX_ISA builds emit EX_ISA assembly and do not invoke the GNU assembler.

### Multi-source EX_ISA images

`compile-ex-isa.sh` and `compile-ex-isa.bat` are source-level linker wrappers
for Unix-like shells and Windows command prompt/PowerShell, respectively.
They perform the same workflow:

1. Compile every C source separately with the EX_ISA backend.
2. Assign each source a deterministic module ID, such as `m0_main`.
3. Merge the generated assembly in the order given on the command line.
4. Invoke the EX_ISA assembler once to produce a flat instruction/data image.

The entry source should be listed first because source order determines the
instruction and data layout:

    ./compile-ex-isa.sh -o program.txt main.c provider.c

On Windows, use the batch wrapper with the same options:

    compile-ex-isa.bat -o program.txt main.c provider.c

Both wrappers support `-o`/`--output`, `--data-out`, `--mif`, `--mif-out`,
`--data-mif-out`, `--asm-out`, and `--assembler`. `--asm-out` preserves the
merged assembly for inspection. Without explicit output paths, the instruction
output is based on the first source name. `--mif` requests instruction and data
MIF output from the external assembler.

Before running either wrapper, build the compiler with:

    make TARGET=ex-isa 8cc

The wrappers expect the resulting executable at the project root as `8cc` or
`8cc.exe`. They invoke the separate EX_ISA assembler project; provide its
executable with `--assembler FILE` or set `ASSEMBLER_EX_ISA`. When neither is
specified, the wrappers look for `assembler-EX_ISA` or `assembler-EX_ISA.exe`
beside the wrapper. The wrappers use temporary per-module assembly files and
remove them after the build. Use `--asm-out` to copy the merged assembly to a
persistent inspection path.

This compiler-side linker is a flat image builder, not a conventional
object-file linker. It does not produce relocations, archives, symbol files,
or dynamic links. Public C functions and objects retain their C names, while
compiler-generated private labels are module-qualified. Undefined references
and duplicate public definitions are passed to the external assembler for
diagnosis.

The focused linker fixtures can be assembled with the shell wrapper using:

    make TARGET=ex-isa ex-isa-link-test

8cc comes with unit tests. To run the tests, give "test" as an argument:

    make test

The following target builds 8cc three times to verify that
stage1 compiler can build stage2, and stage2 can build stage3.
It then compares stage2 and stage3 binaries byte-by-byte to verify
that we reach a fixed point.

    make fulltest

Author
------

Rui Ueyama <rui314@gmail.com>


Retargeting
------

Seth Amico


Links for C compiler development
--------------------------------

Besides popular books about compiler, such as the Dragon Book,
I found the following books/documents are very useful
to develop a C compiler.
Note that the standard draft versions are very close to the ratified versions.
You can practically use them as the standard documents.

-   LCC: A Retargetable C Compiler: Design and Implementation
    http://www.amazon.com/dp/0805316701,
    https://github.com/drh/lcc

-   TCC: Tiny C Compiler
    http://bellard.org/tcc/,
    http://repo.or.cz/w/tinycc.git/tree

-   C99 standard final draft
    http://www.open-std.org/jtc1/sc22/wg14/www/docs/n1124.pdf

-   C11 standard final draft
    http://www.open-std.org/jtc1/sc22/wg14/www/docs/n1570.pdf

-   Dave Prosser's C Preprocessing Algorithm
    http://www.spinellis.gr/blog/20060626/

-   The x86-64 ABI
    http://www.x86-64.org/documentation/abi.pdf
