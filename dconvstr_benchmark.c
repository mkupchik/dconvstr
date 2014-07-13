/*
 *  Benchmark of bijective, heapless and bignumless conversion of IEEE 754 double to string and vice versa
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
#  include <intrin.h>
#  define  strtoull          _strtoui64
   typedef unsigned __int64  uint64_t;
#endif

#if defined(_WIN32) || defined(_WIN64)
#  include <windows.h>
#elif defined(__linux__)
#  define __USE_GNU
#  include <sched.h>
#  include <unistd.h>
#else
#  include <pthread.h>
#  include <pthread_np.h>
#endif

#ifdef USE_PLAN9_LIBC
#  include <fmt.h>
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
 *  Read timestamp counter (processor built-in)
 *
 */
static inline uint64_t  read_timestamp_counter()
{
#ifndef _MSC_VER
    uint32_t  a, d;
    __asm__ volatile( "rdtsc" : "=a"(a), "=d"(d) );
    return  ( ((uint64_t)d) << 32 )|( (uint64_t)a );
#else
    return  __rdtsc();
#endif
}

/**
 *
 *  Execute benchmark test
 *
 */
static void  benchmark()
{
    fprintf( stderr, "Running benchmark, please wait...\n" );

    uint64_t  t0, t1;
    uint64_t  dconvstr_time1 = 0, dconvstr_time2 = 0;
    uint64_t  libc_time1     = 0, libc_time2     = 0;

    uint64_t  loop_limit;
#ifndef USE_PLAN9_LIBC
    loop_limit = 0x1000000;
#else
    loop_limit = 0x100000;
#endif
    for( uint64_t  i = 0; i < loop_limit; ++i )
    {
        // 1. Generate random double-precision floating point value
        uint64_t  raw_random_value;
        do {
            raw_random_value = local_rng_get_random_uint64();     // skip NAN/INF because not every libc
        } while( ((raw_random_value >> 52) & 0x7FF) == 0x7FF );   // can parse its own output
        double  random_value = 0.0;
        memcpy( &random_value, &raw_random_value, sizeof(random_value) );

        // 2. Print number to string (using full precision) with dconvstr library
        char   str[128];
        int    str_size = sizeof(str) - 1;
        char*  str_end = str;
        t0 = read_timestamp_counter();
        int  dconvstr_print_status = dconvstr_print(
            &str_end, &str_size, random_value,
            'e', 0, 0, 20
        );
        t1 = read_timestamp_counter();
        dconvstr_time1 += (t1 - t0);
        if((! dconvstr_print_status )||( str_end + str_size != str + sizeof(str) - 1 ))
        {
            fprintf( stderr, "Failed 1\n" );
            exit(-1);
        }
        *str_end = 0;

        // 3. Convert string back to number using dconvstr library
        const char*  str_actual_end = NULL;
        double  alt_random_value = 0.0;
        int  erange_condition = 1;
        t0 = read_timestamp_counter();
        int  dconvstr_scan_status = dconvstr_scan(
            str, &str_actual_end, &alt_random_value, &erange_condition
        );
        t1 = read_timestamp_counter();
        dconvstr_time2 += (t1 - t0);
        if((! dconvstr_scan_status )||( erange_condition )||( str_actual_end != str_end )||
           ( 0 != memcmp( &random_value, &alt_random_value, sizeof(double) ) ))
        {
            fprintf( stderr, "Failed 2\n" );
            exit(-1);
        }

        // 4. Print number to string (using full precision) with libc
        memset( str, 0, sizeof(str) );
#ifndef USE_PLAN9_LIBC
        t0 = read_timestamp_counter();
        snprintf( str, sizeof(str)-1, "%.20e", random_value );
        t1 = read_timestamp_counter();
        libc_time1 += (t1 - t0);
#else
        t0 = read_timestamp_counter();
        snprint( str, sizeof(str)-1, "%.20e", random_value );
        t1 = read_timestamp_counter();
        libc_time1 += (t1 - t0);
#endif

        // 5. Convert string back to number using libc
        char*  str_actual_end2 = NULL;
        errno = 0;
        t0 = read_timestamp_counter();
#ifndef USE_PLAN9_LIBC
        alt_random_value = strtod( str, &str_actual_end2 );
#else
        alt_random_value = fmtstrtod( str, &str_actual_end2 );
#endif
        t1 = read_timestamp_counter();
        libc_time2 += (t1 - t0);
        if( str_actual_end2 != str + strlen( str ) )
        {
            fprintf( stderr, "Failed 3\n" );
            exit(-1);
        }

#ifdef USE_PLAN9_LIBC
        if( (i & 0xFFF) == 0xFFF )
            fprintf( stderr, "." );
#endif
    }

    if(( dconvstr_time1 != 0 )&&( dconvstr_time2 != 0 )&&( libc_time1 != 0 )&&( libc_time2 != 0 ))
    {
        double  r1 = ((double)libc_time1) / ((double)dconvstr_time1);
        double  r2 = ((double)libc_time2) / ((double)dconvstr_time2);
        fprintf( stderr, "Double-to-string conversion: time ratio = %g\n"
                         "String-to-double conversion: time ratio = %g\n",
                         r1, r2 );
    }
}

/**
 *
 *  Program entry point
 *
 */
#ifndef USE_PLAN9_LIBC
int  main( int  argc, const char* const*  argv )
#else
int  p9main( int  argc, const char* const*  argv )
#endif
{
    // 1. Check command line
    if( argc > 2 )
    {
        fprintf( stderr, "Usage: dconvstr_benchmark [initial_rng_state]\n" );
        exit(-1);
    }
    fprintf( stderr, "Running dconvstr_benchmark, build date " __DATE__ " " __TIME__ "\n" );

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

    // 3. Set thread affinity so read_timestamp_counter() won't return garbage
#if defined(_WIN32) || defined(_WIN64)
    SetThreadAffinityMask( GetCurrentThread(), 1 );
#elif defined(__linux__)
    cpu_set_t  cpuset;
    CPU_ZERO( &cpuset );
    CPU_SET( 0, &cpuset );
    sched_setaffinity( getpid(), sizeof(cpuset), &cpuset );
#else
    cpuset_t  cpuset;
    CPU_ZERO( &cpuset );
    CPU_SET( 0, &cpuset );
    pthread_setaffinity_np( pthread_self(), sizeof(cpuset), &cpuset );
#endif

    // 4. Run benchmark
    benchmark();
    return  0;
}
