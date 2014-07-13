
Bijective, heapless and bignumless conversion of IEEE 754 double to string and vice versa

http://www.gurucoding.com/en/dconvstr/

Copyright (c) 2014 Mikhail Kupchik <Mikhail.Kupchik@prime-expert.com>


# About this project
dconvstr is an implementation of IEEE 754 double to string conversion and vice versa.
Much better implementation than found in most C runtime libraries these days.

## Quality criteria
String to IEEE 754 double and vice versa conversion routines should conform 
to the following limitations:
* Bijectivity: perform double-to-string-and-back-to-double conversion without loss of precision
  (if number of decimal digits is not artifically truncated by user) in the entire domain.
* Reasonable rounding: numbers like 0.1 should be printed as "0.1" and not as "0.09999999999999997"
* Robustness: don't allocate memory on heap, because malloc(3) can fail and cause heap 
  lock contentions in multi-threading environment
* Speed: perform as fast as possible, don't depend on big numbers library to do the job,
  manage to retain bijectivity while operating within dynamic range of machine registers.

## Overview of other implementations
### [Apache APR](https://svn.apache.org/repos/asf/apr/apr/trunk/strings/apr_snprintf.c)
Very simple implementation not aiming for bijectivity. Accumulates rounding error during conversion.
Does not perform reasonable rounding. Does not handle special cases (infinity and NaN).

### [gdtoa library by David M. Gay](http://svnweb.freebsd.org/base/head/contrib/gdtoa/)
Also available in the form of [single file](http://www.netlib.org/fp/dtoa.c).
Quite popular implementation. Used in BSD libc, Bionic/Android libc, by Python, and by 
Javascript implementation in Mozilla Firefox web browser. Available in Windows via libmingex.a library.
Supports VAX and IBM mainframe floating point formats. Depends on its own internal 
big numbers library for bijectivity. Allocates heap memory. Slow (see benchmark below).
Also older versions of this library [violated strict aliasing rules](http://patrakov.blogspot.com/2009/03/dont-use-old-dtoac.html)
and required adaptation. 

### [GNU glibc](http://www.gnu.org/software/libc/)
glibc implementation of double/string conversion (stdio-common/printf_fp.c, stdlib/strtod_l.c) depends on 
excerpt from GNU MP library for bijectivity (*__mpn_mul*, *__mpn_lshift*, *__mpn_rshift*, *__mpn_cmp*).
Allocates heap memory by default, but tries to avoid heap allocations by using alloca(3) if buffers 
are small enough. Slow (see benchmark below). GNU quadmath library (part of gcc) follows the same approach.

### uClibc, dietlibc and AVR libc
All of them use inexact algoritms not aiming for bijectivity akin to Apache APR, but handle special cases
(infinity and NaN). They avoid expensive computation in bignums at the cost of losing precision.
Also they don't perform reasonable rounding. AVR libc is notable for implementing 64-bit computations 
in manually optimized 8-bit assembly.

### [Solaris libc](https://github.com/joyent/illumos-joyent/blob/master/usr/src/lib/libc/port/fp/decimal_bin.c)
At the time of writing, this legacy of Sun Microsystems is known as OpenIndiana/Illumos.
Implements own bignums library. Overall code quality looks good. Calls malloc(3) in *__big_float_times_power*.

### Java and Go programming languages
Native implementation of double/string conversion which depends on natively implemented bignum library.
Hence bijective, but very slow.

## [C runtime library shipped with Go](http://golang.org/src/lib9/fmt/strtod.c), originally from Plan 9
Implements its own bignum library (lib9\fmt\stdtod.c), which always allocates on stack (up to 1500 bytes).
Aims for bijectivity: contains feedback and adjust code in lib9\fmt\fltfmt.c. Slow as hell because of that
(see benchmark below).

## Javascript V8 Engine (Google Chrome web browser and Node.js web server platform)
Independent implementation in C++. Using its own bignums library, based on its own vector template class.
These vectors may allocate their storage either on heap or on stack.

## Microsoft MSVCRT.DLL
Implemented inside conv.lib, source code for it is not shipped with compiler's runtime library, so it was 
studied as a black box. Bijective, not using heap, not doing deep stack allocations. But without 
reasonable rounding, e.g. printf( "%.17f", 0.6 ) yields "0.59999999999999998".

## Overview of this implementation
This implementation provides bijectivity which is confirmed by stress test.

Reasonable rounding in this library is performed without scanning for repeating 9s at the end 
of decimal mantissa and other reckless hackery.

Heap is never touched and alloca() is never called.

This implementation does not use big numbers of arbitrary precision, just 11 bits of extended precision
are added to mantissa, so it still fits to 64-bit machine register. Instead of burning cycles at runtime,
this library is using 42 kilobytes of precomputed data, which allows core operations to be performed
as a single table lookup followed by integer multiplication. Also this implementation makes use of 
modern hardware by utilizing 128-bit unsigned multiplication and bit scan instructions, if available.

dconvstr library is licensed under 2-clause BSD license, so it's legally compatible both with
commercial and open source code.

## Benchmark
All results are in favour of dconvstr.
Table below shows ratio between libc running time and dconvstr running time.

| libc             | Double-to-string | String-to-double |
| :--------------- |:----------------:|:----------------:|
| MSVCRT           |      1.48        |       4.18       |
| BSD libc (gdtoa) |      3.32        |       6.28       |
| GNU glibc        |      2.34        |       2.48       |
| golang/plan9     |     88.62        |      63.67       |
