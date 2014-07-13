/*
 *  Test of bijective, heapless and bignumless conversion of IEEE 754 double to string and vice versa
 *  http://www.gurucoding.com/en/dconvstr/
 *
 *  Copyright (c) 2014 Mikhail Kupchik <Mikhail.Kupchik@prime-expert.com>
 *  All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without modification, are permitted 
 *  provided that the following conditions are met:
 *
 *  1. Redistributions of source code must retain the above copyright notice, this list of conditions
 *     and the following disclaimer.
 *
 *  2. Redistributions in binary form must reproduce the above copyright notice, this list of conditions
 *     and the following disclaimer in the documentation and/or other materials provided with the 
 *     distribution.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR 
 *  IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND 
 *  FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR 
 *  CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL 
 *  DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 *  DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 *  IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 *  OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include "dconvstr.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <math.h>
#ifndef _MSC_VER
#  include <stdint.h>
#else
#  define  strtoull              _strtoui64
#  define  __DBL_DENORM_MIN__    ((double)4.94065645841246544177e-324L)
#  define  __DBL_MAX__           ((double)1.79769313486231570815e+308L)
   typedef unsigned __int64      uint64_t;
#endif

#if defined(_WIN32) || defined(_WIN64)       // not only for MSVC compiler, MinGW too
#  define  UINT64_FORMAT_STRING  "%I64u"
#else
#  if __SIZEOF_LONG__ == 4
#     define  UINT64_FORMAT_STRING  "%llu"
#  else
#     define  UINT64_FORMAT_STRING  "%lu"
#  endif
#endif


//=====================================================================================================
//
// CONSTANTS AND GLOBAL VARIABLES
//

static uint64_t  local_rng_state_ = 0;

//=====================================================================================================
//
// FUNCTIONS
//
/**
 *
 *  Generate random 64-bit unsigned integer using local RNG with reproduceable outcome
 *
 *  RNG type: Linear congruential generator with period length 2^64
 *            (called twice, so period length of output sequence is 2^63).
 *
 */
static inline uint64_t  local_rng_get_random_uint64()
{
    local_rng_state_ = 6364136223846793005ULL * local_rng_state_ + 1442695040888963407ULL;
    uint64_t  a = (local_rng_state_ >> 16) & 0xFFFFFFFFULL;
    local_rng_state_ = 6364136223846793005ULL * local_rng_state_ + 1442695040888963407ULL;
    uint64_t  b = (local_rng_state_ >> 16) & 0xFFFFFFFFULL;
    return  (a << 32) | b;
}

/**
 *
 *  Parse printf(3)-like format flags
 *
 *  @returns  1  if parsed normally
 *            0  if there were syntax errors
 *
 */
static int  parse_printf_format_flags(
    const char*    printf_format,
    int*           format_char,
    unsigned int*  format_flags,
    int*           format_width,
    int*           format_precision
)
{
    if( *printf_format != '%' )
        return  0;

    int  state = 0;
    for( const char*  p = printf_format + 1; *p; ++p )
    {
        char  ch = *p;

        // 1. Parse flags in prefix
        if( state == 0 )
        {
            if( ch == '#' )
            {
                (*format_flags) |= DCONVSTR_FLAG_SHARP;
                continue;
            }
            else if( ch == '-' )
            {
                (*format_flags) |= DCONVSTR_FLAG_LEFT_JUSTIFY;
                continue;
            }
            else if( ch == '+' )
            {
                (*format_flags) |= DCONVSTR_FLAG_PRINT_PLUS;
                continue;
            }
            else if( ch == ' ' )
            {
                (*format_flags) |= DCONVSTR_FLAG_SPACE_IF_PLUS;
                continue;
            }
            else if( ch == '0' )
            {
                (*format_flags) |= DCONVSTR_FLAG_PAD_WITH_ZERO;
                state = 1;
                continue;
            }
        }

        // 2. Parse width
        if( state <= 1 )
        {
            if(( ch >= '0' )&&( ch <= '9' ))
            {
                (*format_flags) |= DCONVSTR_FLAG_HAVE_WIDTH;
                (*format_width) = 10 * (*format_width) + (ch - '0');
                state = 1;
                continue;
            }
            else if( ch == '.' )
            {
                (*format_precision) = 0;
                state = 2;
                continue;
            }
        }

        // 3. Parse precision
        if(( state == 2 )&&( ch >= '0' )&&( ch <= '9' ))
        {
            (*format_precision) = 10 * (*format_precision) + (ch - '0');
            continue;
        }

        // 4. Parse format char
        if( state <= 2 )
        {
            if(( ch == 'E' )||( ch == 'F' )||( ch == 'G' ))
            {
                (*format_flags) |= DCONVSTR_FLAG_UPPERCASE;
                *format_char = (ch - 'A') + 'a';
                state = 3;
                continue;
            }
            else if(( ch == 'e' )||( ch == 'f' )||( ch == 'g' ))
            {
                *format_char = ch;
                state = 3;
                continue;
            }
        }

        // 5. Handle syntax errors
        return  0;
    }
    return( state == 3 );
}

/**
 *
 *  Execute single static test
 *
 */
static void  single_static_test( const char*  fmt, const char*  str, double  val, int  flag_reverse_test )
{
    // 1. Parse format string
    int           format_char      = 0;
    unsigned int  format_flags     = 0;
    int           format_width     = 0;
    int           format_precision = DCONVSTR_DEFAULT_PRECISION;
    if(! parse_printf_format_flags( fmt, &format_char, &format_flags, &format_width, &format_precision ) )
    {
        fprintf(
            stderr,
            "Can't parse format string\n"
            "Test failed for fmt=%s str=%s val=%17.17g\n", fmt, str, val
        );
        exit(-1);
    }

    // 2. Check conversion val -> str
    char  alt_str[128];
    memset( alt_str, 0, sizeof(alt_str) );
    int  alt_str_size = sizeof(alt_str) - 1;
    char*  alt_str_end = alt_str;
    int  dconvstr_print_status = dconvstr_print(
        &alt_str_end, &alt_str_size, val,
        format_char, format_flags, format_width, format_precision
    );
    if(! dconvstr_print_status )
    {
        fprintf(
            stderr,
            "Can't convert double to string\n"
            "Test failed for fmt=%s str=%s val=%17.17g\n", fmt, str, val
        );
        exit(-1);
    }
    *alt_str_end = 0;
    if( 0 != strcmp( str, alt_str ) )
    {
        fprintf(
            stderr,
            "Formatting result not as expected:\n"
            "    expected \"%s\",\n"
            "    got      \"%s\"\n", str, alt_str
        );
        fprintf(
            stderr,
            "Test failed for fmt=%s str=%s val=%17.17g\n", fmt, str, val
        );
        exit(-1);
    }

    // 3. Check conversion str -> val
    if( flag_reverse_test )
    {
        const char*  str_expected_end = str + strlen( str );
        const char*  str_actual_end   = NULL;
        double  alt_val = 0.0;
        int  erange_condition = 1;
        int  dconvstr_scan_status = dconvstr_scan(
            str, &str_actual_end, &alt_val, &erange_condition
        );
        if(! dconvstr_scan_status )
        {
            fprintf(
                stderr,
                "Unexpected internal error in reverse test\n"
                "Test failed for fmt=%s str=%s val=%17.17g\n", fmt, str, val
            );
            exit(-1);
        }
        if( erange_condition )
        {
            fprintf(
                stderr,
                "Unexpected ERANGE condition in reverse test\n"
                "Test failed for fmt=%s str=%s val=%17.17g\n", fmt, str, val
            );
            exit(-1);
        }
        if( str_actual_end != str_expected_end )
        {
            fprintf(
                stderr,
                "Unexpected syntax error in reverse test\n"
                "Test failed for fmt=%s str=%s val=%17.17g\n", fmt, str, val
            );
            exit(-1);
        }
        if( 0 != memcmp( &alt_val, &val, sizeof(double) ) )
        {
            fprintf(
                stderr,
                "Scanning result not as expected:\n"
                "    expected %.17e,\n"
                "    got      %.17e\n", val, alt_val
            );
            fprintf(
                stderr,
                "Test failed for fmt=%s str=%s val=%17.17g\n", fmt, str, val
            );
            exit(-1);
        }
    }
}

/**
 *
 *  Execute all static tests
 *
 */
static void  all_static_tests()
{
    fprintf( stderr, "Running static tests...\n" );

    single_static_test( "%17.17f",     "1.00000000000000000",            1.0, 1 );
    single_static_test( "%17.17f",    "-1.00000000000000000",           -1.0, 1 );
    single_static_test( "%17.17f",    "10.00000000000000000",           10.0, 1 );
    single_static_test( "%17.17f",   "-10.00000000000000000",          -10.0, 1 );
    single_static_test( "%17.17f",    "11.00000000000000000",           11.0, 1 );

    single_static_test( "%+#22.15e",  "+7.894561230000000e+08",  789456123.0, 1 );
    single_static_test( "%-#22.15e",   "7.894561230000000e+08 ", 789456123.0, 0 );
    single_static_test( "%#22.15e",   " 7.894561230000000e+08",  789456123.0, 1 );
    single_static_test( "%#1.1g",      "8.e+08",                 789456123.0, 0 );
    single_static_test( "%.0f",        "1",                              0.6, 0 );
    single_static_test( "%2.4e",       "8.6000e+00",                     8.6, 1 );
    single_static_test( "%2.4g",       "8.6",                            8.6, 1 );
    single_static_test( "%e",          "-inf",                     -HUGE_VAL, 1 );

    single_static_test( "%e",          "1.234000e+01",                 12.34, 1 );
    single_static_test( "%e",          "1.234000e-01",                0.1234, 1 );
    single_static_test( "%e",          "1.234000e-03",              0.001234, 1 );
    single_static_test( "%.60e",       "1.000000000000000000000000000000000000000000000000000000000000e+20",
                                                               1e20, 1  );
    single_static_test( "%e",          "1.000000e-01",                   0.1, 1 );
    single_static_test( "%f",          "12.340000",                    12.34, 1 );
    single_static_test( "%f",          "0.123400",                    0.1234, 1 );
    single_static_test( "%f",          "0.001234",                  0.001234, 1 );
    single_static_test( "%g",          "12.34",                        12.34, 1 );
    single_static_test( "%g",          "0.1234",                      0.1234, 1 );
    single_static_test( "%g",          "0.001234",                  0.001234, 1 );
    single_static_test( "%.60g",       "100000000000000000000",         1e20, 1 );

    single_static_test( "%6.5f",       "0.10000",       0.099999999860301614, 0 );
    single_static_test( "%6.5f",       "0.10000",                         .1, 1 );
    single_static_test( "%5.4f",       "0.5000",                          .5, 1 );
    single_static_test( "%15.5e",   "   4.94066e-324",    __DBL_DENORM_MIN__, 0 );
    single_static_test( "%15.5e",   "   1.79769e+308",           __DBL_MAX__, 0 );
    single_static_test( "%e",          "1.234568e+06",             1234567.8, 0 );
    single_static_test( "%f",          "1234567.800000",           1234567.8, 1 );
    single_static_test( "%g",          "1.23457e+06",              1234567.8, 0 );
    single_static_test( "%g",          "123.456",                    123.456, 1 );
    single_static_test( "%g",          "1e+06",                    1000000.0, 1 );
    single_static_test( "%g",          "10",                            10.0, 1 );
    single_static_test( "%g",          "0.02",                          0.02, 1 );
}

/**
 *
 *  Execute stress test of forward-backward conversion without loss of precision (bijectivity test)
 *
 */
static void  stress_test()
{
    fprintf( stderr, "Running stress test...\n" );

    for( uint64_t  i = 0; ; ++i )
    {
        // 1. Remember initial state of RNG which would be very useful in case of errors
        uint64_t  initial_rng_state = local_rng_state_;

        // 2. Generate random double-precision floating point value
        uint64_t  raw_random_value = local_rng_get_random_uint64();
        if(( ((raw_random_value >> 52) & 0x7FF) == 0x7FF )&&
           ( raw_random_value & ((1ULL<<52)-1ULL)        ))
            raw_random_value = (0xFFF8ULL << 48);   // if NaN: set NaN sign and clear payload
        double  random_value = 0.0;
        memcpy( &random_value, &raw_random_value, sizeof(random_value) );

        // 3. Print random double-precision floating point value to string (using full precision)
        char   str[128];
        int    str_size = sizeof(str) - 1;
        char*  str_end = str;
        int  dconvstr_print_status = dconvstr_print(
            &str_end, &str_size, random_value,
            'e', 0, 0, 20
        );
        if(! dconvstr_print_status )
        {
            fprintf(
                stderr,
                "Can't convert double to string\n"
                "Stress test failed for val=%.20e\n"
                "RNG state to reproduce this condition: " UINT64_FORMAT_STRING "\n",
                random_value, initial_rng_state
            );
            exit(-1);
        }
        if( str_end + str_size != str + sizeof(str) - 1 )
        {
            fprintf(
                stderr,
                "Unexpected buffer state after converting double to string\n"
                "Stress test failed for val=%.20e\n"
                "RNG state to reproduce this condition: " UINT64_FORMAT_STRING "\n",
                random_value, initial_rng_state
            );
            exit(-1);
        }
        *str_end = 0;

        // 4. Convert string back to number
        const char*  str_actual_end = NULL;
        double  alt_random_value = 0.0;
        int  erange_condition = 1;
        int  dconvstr_scan_status = dconvstr_scan(
            str, &str_actual_end, &alt_random_value, &erange_condition
        );
        uint64_t  raw_alt_random_value = 0;
        memcpy( &raw_alt_random_value, &alt_random_value, sizeof(raw_alt_random_value) );
        if(! dconvstr_scan_status )
        {
            fprintf(
                stderr,
                "Unexpected internal error during string-to-number conversion\n"
                "Stress test failed for val=%.20e str=%s\n"
                "RNG state to reproduce this condition: " UINT64_FORMAT_STRING "\n",
                random_value, str, initial_rng_state
            );
            exit(-1);
        }
        if( erange_condition )
        {
            fprintf(
                stderr,
                "Unexpected ERANGE condition during string-to-number conversion\n"
                "Stress test failed for val=%.20e str=%s\n"
                "RNG state to reproduce this condition: " UINT64_FORMAT_STRING "\n",
                random_value, str, initial_rng_state
            );
            exit(-1);
        }
        if( str_actual_end != str_end )
        {
            fprintf(
                stderr,
                "Unexpected syntax error during string-to-number conversion\n"
                "Stress test failed for val=%.20e str=%s\n"
                "RNG state to reproduce this condition: " UINT64_FORMAT_STRING "\n",
                random_value, str, initial_rng_state
            );
            exit(-1);
        }

        // 5. Make sure initial and converted-back-and-forth floating point values have exact match
        if( 0 != memcmp( &random_value, &alt_random_value, sizeof(double) ) )
        {
            fprintf(
                stderr,
                "Strict equality check in stress test failed:\n"
                "    initial double value:     %.20e (raw: 0x" UINT64_FORMAT_STRING ")\n"
                "    converted to string:     `%s'\n"
                "    converted back to double: %.20e (raw: 0x" UINT64_FORMAT_STRING ")\n"
                "RNG state to reproduce this condition: " UINT64_FORMAT_STRING "\n",
                random_value, raw_random_value,
                str,
                alt_random_value, raw_alt_random_value,
                initial_rng_state
            );
            exit(-1);
        }
        
        // 6. Indication to user: stress test is running normally
        if( i == 0x100000 )
        {
            fprintf( stderr, "." );
            fflush( stderr );
            i = 0;
        }
    }
}

/**
 *
 *  Program entry point
 *
 */
int  main( int  argc, const char* const*  argv )
{
    // 1. Check command line
    if( argc > 2 )
    {
        fprintf( stderr, "Usage: dconvstr_test [initial_rng_state]\n" );
        exit(-1);
    }
    fprintf( stderr, "Running dconvstr_test, build date " __DATE__ " " __TIME__ "\n" );

    // 2. Initialize local RNG (optional)
    if( argc == 2 )
    {
        errno = 0;
        uint64_t  parse_result = strtoull( argv[1], 0, 10 );
        if( errno != 0 )
        {
            fprintf( stderr, "Can't parse command line (initial RNG state)\n" );
            exit(-1);
        }
        local_rng_state_ = parse_result;
    }

    // 3. Run tests
    all_static_tests();
    stress_test();
    return  0;    // not reached
}
