
#Notes on Heimdal's ASN.1 compiler's "template" backend

```bash
size .libs/libasn1.dylib
size .libs/libasn1base.a | awk '{sum += $1} END {print sum}' | sed 's/^/TEXT baselib: /'
size .libs/asn1_*.o | awk '{sum += $1} END {print sum}' | sed 's/^/generated code stubs: /'
size *_asn1-template.o | awk '{sum += $1} END {print sum}' | sed 's/^/TEXT stubs: /'
```

Notes about the template parser:

 - assumption: code is large, tables smaller

 - size scales better as features as added:

    - adding encoding rules, textual value parsers, comparators, and so on, are
      just new template interpreter, and generally that means no change to
      templates.

    - so template sizing scales like `O(M + N)` where `M` is the size of the
      modules and `N` is the size of the interpreters

    - but codegen sizing scales like `O(M * N)`

    - as we add interpreters the size advantage of templates increases

 - smaller tables and code, more memory sharing, smaller cache footprint,
   should lead to better performance

    - templates are shared for encode/decode/free/copy/print interpreters,
      whereas none of those operations as generated by the codegen backend
      share any code

 - very compressible -- we waste a lot of space in `struct asn1_template` on
   64-bit systems, and still it's smaller than the code generated by the
   codegen backend

   Note that the template backend does currently dedup templates, though that
   is a quadratic operation that we may eventually have to make optional (right
   now it's not a problem).

   If we made the `ptr` field a `uint32_t` instead of a pointer, and wrote a
   linker for templates, and squeezed out some bits of `tt` and `offset` (we'll
   never need even 8 bits for tags, let alone 20!, and we'll never need 32 bits
   for struct sizes and field offsets either, maybe not even 16-bits), we could
   cut the size of `struct asn1_template` in half.

   Also, once we add OER/JER we could have an option to not support TLV ERs and
   then drop a lot of the tag-related parts of the minified AST that templates
   are, further shrinking the templates.

   The smaller the templates, the faster interpreting will be.

 - use explicit stack instead of recursion in template interpreter to reduce
   stack use and increase speed

   The code generated by the codegen backend is also recursive, though the
   compiler could inline some calls.  Using an explicit stack in an iterative
   interpreter would likely be a big win.

 - how to generate template based stubs

   (Note: it's now the default for Heimdal itself.)

   Use the `--template` option to `asn1_compile` to use the template backend,
   or leave it off to use the codegen backend.

 - the template backend now has more functionality than the codegen backend

 - much easier to extend!  adding new encoding rules is just adding a few
   functions to template.c, one set of length/encode/decode functions per ER,
   so we could add OER/PER/XDR/GSER/JER with very little work outside that one
   file and `gen_template.c` (to generate stub functions and possibly slight
   alterations to templates) and gen.c (to generate declarations of those stub
   functions).

 - template decoding has been fuzzed extensively with American Fuzzy Lop (AFL)

TODO:

 - Generate templates for enumerations, with their names and values, so that
   values of enumerated types can be printed.

 - Remove old fuzzer.  Rely on AFL only.

 - Fuzzing tests, always more fuzzing:

    - Instructions:

```
    $ git clone https://github.com/heimdal/heimdal
    $ cd heimdal
    $ srcdir=$PWD
    $ autoreconf -fi
    $ 
    $ mkdir build
    $ cd build
    $ 
    $ ../configure --srcdir=$srcdir ...
    $ make -j4
    $
    $ cd lib/asn1
    $ make clean
    $ AFL_HARDEN=1 make -j4 asn1_print check CC=afl-gcc # or CC=afl-clang
    $ 
    $ # $srcdir/lib/asn1/fuzz-inputs/ has at least one minimized DER value
    $ # produced by taking an EK certificate and truncating the signatureValue
    $ # and tbsCertificate.subjectPublicKeyInfo fields then re-encoding, thus
    $ # cutting down the size of the certificate by 45%.  AFL finds interesting
    $ # code paths much faster if the input corpus is minimized.
    $ 
    $ mkdir f
    $ ../../libtool --mode=execute afl-fuzz -i $srcdir/lib/asn1/fuzz-inputs -o $PWD/f ./asn1_print '@@' Certificate
    $ 
    $ # Or
    $ ../../libtool --mode=execute afl-fuzz -i $srcdir/lib/asn1/fuzz-inputs -o $PWD/f ./asn1_print -A '@@'
    $
    $ # Examine crash reports, if any.  Each crash report consists of an input
    $ # that caused a crash, so run valgrind on each such input:
    $ 
    $ for i in f/crashes/id*; do
    >   echo $i
    >   ../../libtool --mode=execute valgrind --num-callers=64 ./asn1_print $i \
    >        Certificate IOSCertificationRequest >/dev/null 2> f/crashes/vg-${i##*/}
    > done
    $ 
    $ # then review the valgrind output:
    $ $PAGER f/crashes/vg-*
```

    - Here's a screenshot of AFL running on the previous commit:

```
                     american fuzzy lop 2.52b (asn1_print)

?????? process timing ????????????????????????????????????????????????????????????????????????????????????????????????????????????????????? overall results ??????????????????
???        run time : 1 days, 22 hrs, 39 min, 51 sec     ???  cycles done : 18     ???
???   last new path : 0 days, 0 hrs, 38 min, 5 sec       ???  total paths : 2310   ???
??? last uniq crash : none seen yet                      ??? uniq crashes : 0      ???
???  last uniq hang : none seen yet                      ???   uniq hangs : 0      ???
?????? cycle progress ?????????????????????????????????????????????????????????????????? map coverage ??????????????????????????????????????????????????????????????????????????????
???  now processing : 997* (43.16%)     ???    map density : 2.19% / 8.74%         ???
??? paths timed out : 0 (0.00%)         ??? count coverage : 3.25 bits/tuple       ???
?????? stage progress ?????????????????????????????????????????????????????????????????? findings in depth ???????????????????????????????????????????????????????????????
???  now trying : interest 16/8         ??? favored paths : 319 (13.81%)           ???
??? stage execs : 13.1k/13.4k (98.18%)  ???  new edges on : 506 (21.90%)           ???
??? total execs : 91.9M                 ??? total crashes : 0 (0 unique)           ???
???  exec speed : 576.2/sec             ???  total tmouts : 2158 (180 unique)      ???
?????? fuzzing strategy yields ??????????????????????????????????????????????????????????????????????????????????????? path geometry ???????????????????????????
???   bit flips : 565/5.60M, 124/5.60M, 74/5.59M        ???    levels : 19         ???
???  byte flips : 4/699k, 17/375k, 15/385k              ???   pending : 552        ???
??? arithmetics : 323/20.7M, 8/10.6M, 1/517k            ???  pend fav : 0          ???
???  known ints : 85/1.76M, 148/9.98M, 175/16.8M        ??? own finds : 2308       ???
???  dictionary : 0/0, 0/0, 12/6.62M                    ???  imported : n/a        ???
???       havoc : 757/6.35M, 0/0                        ??? stability : 100.00%    ???
???        trim : 14.30%/336k, 46.60%                   ??????????????????????????????????????????????????????????????????????????????
?????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????          [cpu000:196%]
```

    - TODO: Make building with AFL a ./cofigure option.

    - TODO: Make fuzzing with AFL a make target.

    - Fuzz decode round-tripping (don't just decode, but also encoded the
      decoded).

 - Performance testing

 - `ASN1_MALLOC_ENCODE()` as a function, replaces `encode_` and `length_`

 - Fix SIZE constraits

 - Proper implementation of `SET { ... }`

 - Compact types that only contain on entry to not having a header.


SIZE - Futher down is later generations of the template parser

```
	code:
	==================
	__TEXT	__DATA	__OBJC	others	dec	hex
	462848	12288	0	323584	798720	c3000 (O2)

	trivial types:
	==================
	__TEXT	__DATA	__OBJC	others	dec	hex
	446464	12288	0	323584	782336	bf000 (O2)

	OPTIONAL
	==================
	__TEXT	__DATA	__OBJC	others	dec	hex
	425984	16384	0	323584	765952	bb000 (O2)

	SEQ OF
	==================
	__TEXT	__DATA	__OBJC	others	dec	hex
	368640	32768	0	327680	729088	b2000 (O2)
	348160	32768	0	327680	708608	ad000 (Os)

	BOOLEAN
	==================
	339968	32768	0	327680	700416	ab000 (Os)

	TYPE_EXTERNAL:
	==================
	331776	32768	0	327680	692224	a9000 (Os)

	SET OF
	==================
	327680	32768	0	327680	688128	a8000 (Os)

	TYPE_EXTERNAL everywhere
	==================
	__TEXT	__DATA	__OBJC	others	dec	hex
	167936	69632	0	327680	565248	8a000 (Os)

	TAG uses ->ptr (header and trailer)
	==================
	229376	102400	0	421888	753664	b8000 (O0)

	TAG uses ->ptr (header only)
	==================
	221184	77824	0	421888	720896	b0000 (O0)

	BER support for octet string (not working)
	==================
	180224	73728	0	417792	671744	a4000 (O2)

	CHOICE and BIT STRING missign
	==================
	__TEXT	__DATA	__OBJC	others	dec	hex
	172032	73728	0	417792	663552	a2000 (Os)

	No accessor functions to global variable
	==================
	__TEXT	__DATA	__OBJC	others	dec	hex
	159744	73728	0	393216	626688	99000 (Os)

	All types tables (except choice) (id still objects)
	==================
	__TEXT	__DATA	__OBJC	others	dec	hex
	167936	77824	0	421888	667648	a3000
	base lib: 22820

	__TEXT	__DATA	__OBJC	others	dec	hex
	==================
	167936	77824	0	421888	667648	a3000 (Os)
	baselib: 22820
	generated code stubs: 41472
	TEXT stubs: 112560

	All types, id still objects
	==================
	__TEXT	__DATA	__OBJC	others	dec	hex
	155648	81920	0	430080	667648	a3000 (Os)
	TEXT baselib: 23166
	generated code stubs: 20796
	TEXT stubs: 119891

	All types, id still objects, dup compression
	==================
	__TEXT	__DATA	__OBJC	others	dec	hex
	143360	65536	0	376832	585728	8f000 (Os)
	TEXT baselib: 23166
	generated code stubs: 20796
	TEXT stubs: 107147

	All types, dup compression, id vars
	==================
	__TEXT	__DATA	__OBJC	others	dec	hex
	131072	65536	0	352256	548864	86000
	TEXT baselib: 23166
	generated code stubs: 7536
	TEXT stubs: 107147
```
