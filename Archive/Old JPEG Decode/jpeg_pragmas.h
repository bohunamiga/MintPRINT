fd2pragma 2.197 (09.10.2016) by Dirk Stoecker <software@dstoecker.de>
 -h,--help
 -i,--infile   <input filename>
 -s,--special  <number>
 -m,--mode     <number>
 -t,--to       <destination directory>
 -a,--abi      <m68k|ppc|ppc0|ppc2>
 -c,--clib     <clib prototypes filename>
 -d,--header   <header file or "">
 -i,--libname  <name of library>
 -n,--hunkname <name of HUNK_NAME, default is 'text'>
 -b,--basename <name of library base without '_'>
 -l,--libtype  <name of base library type>
 -p,--priority <priority for auto open files>
 -r,--copyright<copyright text>
    --prefix   <MorphOS gate prefix>
    --subprefix<MorphOS gate sub prefix>
    --premacro <MorphOS gate file start macro>

Switches:
--autoheader    add the typical automatic generated header
--comment       copy comments found in input file
--externc       add a #ifdef __cplusplus ... statement to pragma file
--fpuonly       work only with functions using FPU register arguments
--newsyntax     uses new Motorola syntax for asm files
--nofpu         disable usage of FPU register arguments
--noppc         disable usage of PPC-ABI functions
--noppcregname  do not add 'r' to PPC register names
--nosymbol      prevents creation of SYMBOL hunks for link libraries
--onlycnames    do not create C++ or ASM names
--opt040        optimize for 68040, do not use MOVEM for stubs
--ppconly       only use PPC-ABI functions
--private       includes private declared functions
--section       add section statements to asm texts
--smallcode     generate small code link libraries or assembler text
--smalldata     generate small data link libraries or assembler text
--smalltypes    allow 8 and 16 bit types in registers
--sorted        sort generated files by name and not by bias value
--systemrelease special handling of comments for system includes
--usesyscall    uses syscall pragma instead of libcall SysBase
--voidbase      library bases are of type void *

special: 1 - Aztec compiler (xxx_lib.h,     MODE 2, AMICALL)
         2 - DICE compiler  (xxx_pragmas.h, MODE 3, LIBCALL)
         3 - SAS compiler   (xxx_pragmas.h, MODE 3, LIBCALL,LIBTAGS)
         4 - MAXON compiler (xxx_lib.h,     MODE 1, AMICALL)
         5 - STORM compiler (xxx_lib.h,     MODE 1, AMITAGS,AMICALL)
         6 - pragma for all compilers [default]
         7 - all compilers with pragma to inline redirect for GCC
        10 - stub-functions for C - C text
        11 - stub-functions for C - assembler text
        12 - stub-functions for C - link library
        13 - defines and link library for local library base (register call)
        14 - defines and link library for local library base (stack call)
        15 - stub-functions for Pascal - assembler text
        16 - stub-functions for Pascal - link library
        17 - BMAP file for AmigaBASIC and MaxonBASIC
        18 - module for AmigaE
        20 - assembler lvo _lvo.i file
        21 - assembler lvo _lib.i file
        22 - assembler lvo _lvo.i file no XDEF
        23 - assembler lvo _lib.i file no XDEF
        24 - assembler lvo link library
        30 - proto file with pragma/..._lib.h call
        31 - proto file with pragma/..._pragmas.h call
        32 - proto file with pragmas/..._lib.h call
        33 - proto file with pragmas/..._pragmas.h call
        34 - proto file with local/..._loc.h call
        35 - proto file for all compilers (VBCC stubs)
        36 - proto file for GNU-C compiler only
        37 - proto file without lib definitions
        38 - proto file for all compilers (VBCC inline)
        39 - proto file with special PPC related checks
        40 - GCC inline file (preprocessor based)
        41 - GCC inline file (old type - inline based)
        42 - GCC inline file (library stubs)
        43 - GCC inline file (new style - macro)
        44 - GCC inline file (new style - inline)
        45 - GCC inline file (new style - inline with include lines)
        46 - GCC inline file (preprocessor based, direct)
        47 - GCC inline file (new style, direct)
        48 - GCC inline file (preprocessor based, direct, StormGCC)
        50 - GCC inline files for PowerUP (preprocessor based)
        51 - GCC inline files for PowerUP (old type - inline based)
        52 - GCC inline files for PowerUP (library stubs)
        53 - SAS-C include file for PowerUP
        54 - Proto file for PowerUP
        60 - FPC pascal unit text
        70 - VBCC inline files
        71 - VBCC WOS stub-functions - assembler text
        72 - VBCC WOS stub-functions - assembler text (libbase)
        73 - VBCC WOS stub-functions - link library
        74 - VBCC WOS stub-functions - link library (libbase)
        75 - VBCC PowerUP stub-functions - assembler text
        76 - VBCC PowerUP stub-functions - link library
        77 - VBCC WOS inline files
        78 - VBCC MorphOS stub-functions - link library
        79 - VBCC old inline files
        80 - pragma/proto redirect (xxx_pragmas.h, SAS/Dice)
        81 - pragma/proto redirect (xxx_lib.h, Aztec/Maxon/Storm)
        82 - pragma/proto redirect (xxx.h, GCC)
        83 - pragma/proto redirect (xxx_protos.h, VBCC)
        90 - stub-functions for C - assembler text (multiple files)
        91 - VBCC PowerUP stub-functions - assembler text (multiple files)
        92 - VBCC WOS stub-functions - assembler text (multiple files)
        93 - VBCC MorphOS stub-functions - assembler text (multiple files)
       100 - PPC assembler lvo file
       101 - PPC assembler lvo file no XDEF
       102 - PPC assembler lvo ELF link library
       103 - PPC assembler lvo EHF link library
       104 - PPC V.4-ABI assembler file
       105 - PPC V.4-ABI assembler file no XDEF
       106 - PPC V.4-ABI assembler lvo ELF link library
       107 - PPC V.4-ABI assembler lvo EHF link library
       110 - FD file
       111 - CLIB file
       112 - SFD file
       120 - VBCC auto libopen files (C source)
       121 - VBCC auto libopen files (m68k link library)
       122 - VBCC MorphOS inline files
       123 - VBCC new MorphOS inline files
       130 - GCC inline files for MorphOS (preprocessor based)
       131 - GCC inline files for MorphOS (old type - inline based)
       132 - GCC inline files for MorphOS (library stubs)
       133 - GCC inline files for MorphOS (library stubs, direct varargs)
       134 - MorphOS gate stubs
       135 - MorphOS gate stubs (prelib)
       136 - MorphOS gate stubs (postlib)
       137 - MorphOS gate stubs (reglib, prelib)
       138 - MorphOS gate stubs (reglib, postlib)
       140 - OS4 XML file
       141 - OS4 PPC->M68K cross-call stubs
       142 - OS4 M68K->PPC cross-call stubs
       200 - FD file (source is a pragma file!)
mode:    special 1-7
         1 - _INCLUDE_PRAGMA_..._LIB_H definition method [default]
         2 - _PRAGMAS_..._LIB_H definition method
         3 - _PRAGMAS_..._PRAGMAS_H definition method
         4 - no definition
         special 11-14,40-45,50-53,70-76,78,90-93,111-112,122,
                 130-138,141:
         1 - all functions, normal interface
         2 - only tag-functions, tagcall interface
         3 - all functions, normal and tagcall interface [default]
