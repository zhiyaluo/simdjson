#include "simdjson/portability.h"
#include <cassert>
#include "simdjson/common_defs.h"
#include "simdjson/parsedjson.h"

//#ifndef SIMDJSON_SKIPUTF8VALIDATION
//#define SIMDJSON_UTF8VALIDATE
//#endif

//__AVX512BW__
//__AVX512F__


// It seems that many parsers do UTF-8 validation.
// RapidJSON does not do it by default, but a flag
// allows it.
#ifdef SIMDJSON_UTF8VALIDATE
#include "simdjson/simdutf8check.h"
#endif
using namespace std;

really_inline uint64_t cmp_mask_against_input(__m256i input_lo, __m256i input_hi,
                                         __m256i mask) {
  __m256i cmp_res_0 = _mm256_cmpeq_epi8(input_lo, mask);
  uint64_t res_0 = (uint32_t)_mm256_movemask_epi8(cmp_res_0);
  __m256i cmp_res_1 = _mm256_cmpeq_epi8(input_hi, mask);
  uint64_t res_1 = _mm256_movemask_epi8(cmp_res_1);
  return res_0 | (res_1 << 32);
}

__m512i _mm512_set_epi8 (char e63, char e62, char e61, char e60, char e59, char e58, char e57, char e56, char e55, char e54, char e53, char e52, char e51, char e50, char e49, char e48, char e47, char e46, char e45, char e44, char e43, char e42, char e41, char e40, char e39, char e38, char e37, char e36, char e35, char e34, char e33, char e32, char e31, char e30, char e29, char e28, char e27, char e26, char e25, char e24, char e23, char e22, char e21, char e20, char e19, char e18, char e17, char e16, char e15, char e14, char e13, char e12, char e11, char e10, char e9, char e8, char e7, char e6, char e5, char e4, char e3, char e2, char e1, char e0) {
 uint8_t buffer[64];
 buffer[0]=e0;
buffer[1]=e1;
buffer[2]=e2;
buffer[3]=e3;
buffer[4]=e4;
buffer[5]=e5;
buffer[6]=e6;
buffer[7]=e7;
buffer[8]=e8;
buffer[9]=e9;
buffer[10]=e10;
buffer[11]=e11;
buffer[12]=e12;
buffer[13]=e13;
buffer[14]=e14;
buffer[15]=e15;
buffer[16]=e16;
buffer[17]=e17;
buffer[18]=e18;
buffer[19]=e19;
buffer[20]=e20;
buffer[21]=e21;
buffer[22]=e22;
buffer[23]=e23;
buffer[24]=e24;
buffer[25]=e25;
buffer[26]=e26;
buffer[27]=e27;
buffer[28]=e28;
buffer[29]=e29;
buffer[30]=e30;
buffer[31]=e31;
buffer[32]=e32;
buffer[33]=e33;
buffer[34]=e34;
buffer[35]=e35;
buffer[36]=e36;
buffer[37]=e37;
buffer[38]=e38;
buffer[39]=e39;
buffer[40]=e40;
buffer[41]=e41;
buffer[42]=e42;
buffer[43]=e43;
buffer[44]=e44;
buffer[45]=e45;
buffer[46]=e46;
buffer[47]=e47;
buffer[48]=e48;
buffer[49]=e49;
buffer[50]=e50;
buffer[51]=e51;
buffer[52]=e52;
buffer[53]=e53;
buffer[54]=e54;
buffer[55]=e55;
buffer[56]=e56;
buffer[57]=e57;
buffer[58]=e58;
buffer[59]=e59;
buffer[60]=e60;
buffer[61]=e61;
buffer[62]=e62;
buffer[63]=e63;
return _mm512_loadu_si512(buffer);

}

__m512i _mm512_setr_epi8 (char e63, char e62, char e61, char e60, char e59, char e58, char e57, char e56, char e55, char e54, char e53, char e52, char e51, char e50, char e49, char e48, char e47, char e46, char e45, char e44, char e43, char e42, char e41, char e40, char e39, char e38, char e37, char e36, char e35, char e34, char e33, char e32, char e31, char e30, char e29, char e28, char e27, char e26, char e25, char e24, char e23, char e22, char e21, char e20, char e19, char e18, char e17, char e16, char e15, char e14, char e13, char e12, char e11, char e10, char e9, char e8, char e7, char e6, char e5, char e4, char e3, char e2, char e1, char e0)
{
  return _mm512_set_epi8(e0,e1,e2,e3,e4,e5,e6,e7,e8,e9,e10,e11,e12,e13,e14,e15,e16,e17,e18,e19,e20,e21,e22,e23,e24,e25,e26,e27,e28,e29,e30,e31,e32,e33,e34,e35,e36,e37,e38,e39,e40,e41,e42,e43,e44,e45,e46,e47,e48,e49,e50,e51,e52,e53,e54,e55,e56,e57,e58,e59,e60,e61,e62,e63);
}


WARN_UNUSED
/*never_inline*/ bool find_structural_bits(const uint8_t *buf, size_t len,
                                           ParsedJson &pj) {
  if (len > pj.bytecapacity) {
    cerr << "Your ParsedJson object only supports documents up to "<< pj.bytecapacity << " bytes but you are trying to process " <<  len  << " bytes\n";
    return false;
  }
  uint32_t *base_ptr = pj.structural_indexes;
  uint32_t base = 0;
#ifdef SIMDJSON_UTF8VALIDATE
  __m512i has_error = _mm512_setzero_si512();
  struct avx_processed_utf_bytes previous;
  previous.rawbytes = _mm512_setzero_si512();
  previous.high_nibbles = _mm512_setzero_si512();
  previous.carried_continuations = _mm512_setzero_si512();
 #endif

  // Useful constant masks
  const uint64_t even_bits = 0x5555555555555555ULL;
  const uint64_t odd_bits = ~even_bits;

  // for now, just work in 64-byte chunks
  // we have padded the input out to 64 byte multiple with the remainder being
  // zeros

  // persistent state across loop
  uint64_t prev_iter_ends_odd_backslash = 0ULL; // either 0 or 1, but a 64-bit value
  uint64_t prev_iter_inside_quote = 0ULL;       // either all zeros or all ones

  // effectively the very first char is considered to follow "whitespace" for the
  // purposes of psuedo-structural character detection
  uint64_t prev_iter_ends_pseudo_pred = 1ULL;
  size_t lenminus64 = len < 64 ? 0 : len - 64;
  size_t idx = 0;
  uint64_t structurals = 0;
  for (; idx < lenminus64; idx += 64) {
#ifndef _MSC_VER
    __builtin_prefetch(buf + idx + 128);
#endif
	__m512i input = _mm512_loadu_si512((const __m512i *)(buf + idx + 0));
#ifdef SIMDJSON_UTF8VALIDATE
    __m512i highbit = _mm512_set1_epi8(0x80);
    if((_mm512_test_epi8_mask(input,highbit)) == 1) {
        // it is ascii, we just check continuation
        has_error = _mm512_or_si512(
          _mm512_cmpgt_epi8(previous.carried_continuations,
                          _mm512_setr_epi8(9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9,
                                           9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9,
                                           9, 9, 9, 9, 9, 9, 9, 1)),has_error);

    } else {
        // it is not ascii so we have to do heavy work
        previous = avxcheckUTF8Bytes(input_lo, &previous, &has_error);
        previous = avxcheckUTF8Bytes(input_hi, &previous, &has_error);
    }
#endif
    ////////////////////////////////////////////////////////////////////////////////////////////
    //     Step 1: detect odd sequences of backslashes
    ////////////////////////////////////////////////////////////////////////////////////////////

    uint64_t bs_bits =
        _cvtmask64_u64(_mm512_cmpeq_epi8_mask(input, _mm512_set1_epi8('\\')));
    uint64_t start_edges = bs_bits & ~(bs_bits << 1);
    // flip lowest if we have an odd-length run at the end of the prior
    // iteration
    uint64_t even_start_mask = even_bits ^ prev_iter_ends_odd_backslash;
    uint64_t even_starts = start_edges & even_start_mask;
    uint64_t odd_starts = start_edges & ~even_start_mask;
    uint64_t even_carries = bs_bits + even_starts;

    uint64_t odd_carries;
    // must record the carry-out of our odd-carries out of bit 63; this
    // indicates whether the sense of any edge going to the next iteration
    // should be flipped
    bool iter_ends_odd_backslash =
		add_overflow(bs_bits, odd_starts, &odd_carries);

    odd_carries |=
        prev_iter_ends_odd_backslash; // push in bit zero as a potential end
                                      // if we had an odd-numbered run at the
                                      // end of the previous iteration
    prev_iter_ends_odd_backslash = iter_ends_odd_backslash ? 0x1ULL : 0x0ULL;
    uint64_t even_carry_ends = even_carries & ~bs_bits;
    uint64_t odd_carry_ends = odd_carries & ~bs_bits;
    uint64_t even_start_odd_end = even_carry_ends & odd_bits;
    uint64_t odd_start_even_end = odd_carry_ends & even_bits;
    uint64_t odd_ends = even_start_odd_end | odd_start_even_end;

    ////////////////////////////////////////////////////////////////////////////////////////////
    //     Step 2: detect insides of quote pairs
    ////////////////////////////////////////////////////////////////////////////////////////////

    uint64_t quote_bits =
        _cvtmask64_u64(_mm512_cmpeq_epi8_mask(input, _mm512_set1_epi8('"')));
    quote_bits = quote_bits & ~odd_ends;
    uint64_t quote_mask = _mm_cvtsi128_si64(_mm_clmulepi64_si128(
        _mm_set_epi64x(0ULL, quote_bits), _mm_set1_epi8(0xFF), 0));



    uint32_t cnt = hamming(structurals);
    uint32_t next_base = base + cnt;
    while (structurals) {
      base_ptr[base + 0] = (uint32_t)idx - 64 + trailingzeroes(structurals);                          
      structurals = structurals & (structurals - 1);
      base_ptr[base + 1] = (uint32_t)idx - 64 + trailingzeroes(structurals);                          
      structurals = structurals & (structurals - 1);
      base_ptr[base + 2] = (uint32_t)idx - 64 + trailingzeroes(structurals);                          
      structurals = structurals & (structurals - 1);
      base_ptr[base + 3] = (uint32_t)idx - 64 + trailingzeroes(structurals);                          
      structurals = structurals & (structurals - 1);
      base_ptr[base + 4] = (uint32_t)idx - 64 + trailingzeroes(structurals);                          
      structurals = structurals & (structurals - 1);
      base_ptr[base + 5] = (uint32_t)idx - 64 + trailingzeroes(structurals);                          
      structurals = structurals & (structurals - 1);
      base_ptr[base + 6] = (uint32_t)idx - 64 + trailingzeroes(structurals);                          
      structurals = structurals & (structurals - 1);
      base_ptr[base + 7] = (uint32_t)idx - 64 + trailingzeroes(structurals);                          
      structurals = structurals & (structurals - 1);
      base += 8;
    }
    base = next_base;

    quote_mask ^= prev_iter_inside_quote;
    prev_iter_inside_quote = (uint64_t)((int64_t)quote_mask >> 63); // right shift of a signed value expected to be well-defined and standard compliant as of C++20, John Regher from Utah U. says this is fine code

    // How do we build up a user traversable data structure
    // first, do a 'shufti' to detect structural JSON characters
    // they are { 0x7b } 0x7d : 0x3a [ 0x5b ] 0x5d , 0x2c
    // these go into the first 3 buckets of the comparison (1/2/4)

    // we are also interested in the four whitespace characters
    // space 0x20, linefeed 0x0a, horizontal tab 0x09 and carriage return 0x0d
    // these go into the next 2 buckets of the comparison (8/16)
    const __m512i low_nibble_mask = _mm512_setr_epi8(
        //  0                           9  a   b  c  d
        16, 0, 0, 0, 0, 0, 0, 0, 0, 8, 12, 1, 2, 9, 0, 0, 16, 0, 0, 0, 0, 0, 0,
        0, 0, 8, 12, 1, 2, 9, 0, 0,
        16, 0, 0, 0, 0, 0, 0, 0, 0, 8, 12, 1, 2, 9, 0, 0, 16, 0, 0, 0, 0, 0, 0,
        0, 0, 8, 12, 1, 2, 9, 0, 0);
    const __m512i high_nibble_mask = _mm512_setr_epi8(
        //  0     2   3     5     7
        8, 0, 18, 4, 0, 1, 0, 1, 0, 0, 0, 3, 2, 1, 0, 0, 8, 0, 18, 4, 0, 1, 0,
        1, 0, 0, 0, 3, 2, 1, 0, 0,
        8, 0, 18, 4, 0, 1, 0, 1, 0, 0, 0, 3, 2, 1, 0, 0, 8, 0, 18, 4, 0, 1, 0,
        1, 0, 0, 0, 3, 2, 1, 0, 0);

    __m512i structural_shufti_mask = _mm512_set1_epi8(0x7);
    __m512i whitespace_shufti_mask = _mm512_set1_epi8(0x18);

    __m512i v = _mm512_and_si512(
        _mm512_shuffle_epi8(low_nibble_mask, input),
        _mm512_shuffle_epi8(high_nibble_mask,
                            _mm512_and_si512(_mm512_srli_epi32(input, 4),
                                             _mm512_set1_epi8(0x7f))));

    uint64_t tmp =  _cvtmask64_u64(_mm512_cmpeq_epi8_mask(
        _mm512_and_si512(v, structural_shufti_mask), _mm512_set1_epi8(0)));


    structurals = ~tmp;

    // this additional mask and transfer is non-trivially expensive,
    // unfortunately
    uint64_t ws_res = _cvtmask64_u64(_mm512_cmpeq_epi8_mask(
        _mm512_and_si512(v, whitespace_shufti_mask), _mm512_set1_epi8(0)));
    uint64_t whitespace = ~(ws_res);
    // mask off anything inside quotes
    structurals &= ~quote_mask;

    // add the real quote bits back into our bitmask as well, so we can
    // quickly traverse the strings we've spent all this trouble gathering
    structurals |= quote_bits;

    // Now, establish "pseudo-structural characters". These are non-whitespace
    // characters that are (a) outside quotes and (b) have a predecessor that's
    // either whitespace or a structural character. This means that subsequent
    // passes will get a chance to encounter the first character of every string
    // of non-whitespace and, if we're parsing an atom like true/false/null or a
    // number we can stop at the first whitespace or structural character
    // following it.

    // a qualified predecessor is something that can happen 1 position before an
    // psuedo-structural character
    uint64_t pseudo_pred = structurals | whitespace;
    uint64_t shifted_pseudo_pred = (pseudo_pred << 1) | prev_iter_ends_pseudo_pred;
    prev_iter_ends_pseudo_pred = pseudo_pred >> 63;
    uint64_t pseudo_structurals =
        shifted_pseudo_pred & (~whitespace) & (~quote_mask);
    structurals |= pseudo_structurals;

    // now, we've used our close quotes all we need to. So let's switch them off
    // they will be off in the quote mask and on in quote bits.
    structurals &= ~(quote_bits & ~quote_mask);

    //*(uint64_t *)(pj.structurals + idx / 8) = structurals;
  }

  ////////////////
  /// we use a giant copy-paste which is ugly.
  /// but otherwise the string needs to be properly padded or else we
  /// risk invalidating the UTF-8 checks.
  ////////////
  if (idx < len) {
    uint8_t tmpbuf[64];
    memset(tmpbuf,0x20,64);
    memcpy(tmpbuf,buf+idx,len - idx);
    __m256i input_lo = _mm256_loadu_si256((const __m256i *)(tmpbuf + 0));
    __m256i input_hi = _mm256_loadu_si256((const __m256i *)(tmpbuf + 32));
#ifdef SIMDJSON_UTF8VALIDATE
    __m256i highbit = _mm256_set1_epi8(0x80);
    if((_mm256_testz_si256(_mm256_or_si256(input_lo, input_hi),highbit)) == 1) {
        // it is ascii, we just check continuation
        has_error = _mm256_or_si256(
          _mm256_cmpgt_epi8(previous.carried_continuations,
                          _mm256_setr_epi8(9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9,
                                           9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9,
                                           9, 9, 9, 9, 9, 9, 9, 1)),has_error);

    } else {
        // it is not ascii so we have to do heavy work
        previous = avxcheckUTF8Bytes(input_lo, &previous, &has_error);
        previous = avxcheckUTF8Bytes(input_hi, &previous, &has_error);
    }
#endif
    ////////////////////////////////////////////////////////////////////////////////////////////
    //     Step 1: detect odd sequences of backslashes
    ////////////////////////////////////////////////////////////////////////////////////////////
    uint64_t bs_bits =
        cmp_mask_against_input(input_lo, input_hi, _mm256_set1_epi8('\\'));
    uint64_t start_edges = bs_bits & ~(bs_bits << 1);
    // flip lowest if we have an odd-length run at the end of the prior
    // iteration
    uint64_t even_start_mask = even_bits ^ prev_iter_ends_odd_backslash;
    uint64_t even_starts = start_edges & even_start_mask;
    uint64_t odd_starts = start_edges & ~even_start_mask;
    uint64_t even_carries = bs_bits + even_starts;

    uint64_t odd_carries;
    // must record the carry-out of our odd-carries out of bit 63; this
    // indicates whether the sense of any edge going to the next iteration
    // should be flipped
    //bool iter_ends_odd_backslash =
	add_overflow(bs_bits, odd_starts, &odd_carries);

    odd_carries |=
        prev_iter_ends_odd_backslash; // push in bit zero as a potential end
                                      // if we had an odd-numbered run at the
                                      // end of the previous iteration
    //prev_iter_ends_odd_backslash = iter_ends_odd_backslash ? 0x1ULL : 0x0ULL;
    uint64_t even_carry_ends = even_carries & ~bs_bits;
    uint64_t odd_carry_ends = odd_carries & ~bs_bits;
    uint64_t even_start_odd_end = even_carry_ends & odd_bits;
    uint64_t odd_start_even_end = odd_carry_ends & even_bits;
    uint64_t odd_ends = even_start_odd_end | odd_start_even_end;

    ////////////////////////////////////////////////////////////////////////////////////////////
    //     Step 2: detect insides of quote pairs
    ////////////////////////////////////////////////////////////////////////////////////////////
    uint64_t quote_bits =
        cmp_mask_against_input(input_lo, input_hi, _mm256_set1_epi8('"'));
    quote_bits = quote_bits & ~odd_ends;
    uint64_t quote_mask = _mm_cvtsi128_si64(_mm_clmulepi64_si128(
        _mm_set_epi64x(0ULL, quote_bits), _mm_set1_epi8(0xFF), 0));
    quote_mask ^= prev_iter_inside_quote;
    //prev_iter_inside_quote = (uint64_t)((int64_t)quote_mask >> 63); // right shift of a signed value expected to be well-defined and standard compliant as of C++20

    uint32_t cnt = hamming(structurals);
    uint32_t next_base = base + cnt;
    while (structurals) {
      base_ptr[base + 0] = (uint32_t)idx - 64 + trailingzeroes(structurals);                          
      structurals = structurals & (structurals - 1);
      base_ptr[base + 1] = (uint32_t)idx - 64 + trailingzeroes(structurals);                          
      structurals = structurals & (structurals - 1);
      base_ptr[base + 2] = (uint32_t)idx - 64 + trailingzeroes(structurals);                          
      structurals = structurals & (structurals - 1);
      base_ptr[base + 3] = (uint32_t)idx - 64 + trailingzeroes(structurals);                          
      structurals = structurals & (structurals - 1);
      base_ptr[base + 4] = (uint32_t)idx - 64 + trailingzeroes(structurals);                          
      structurals = structurals & (structurals - 1);
      base_ptr[base + 5] = (uint32_t)idx - 64 + trailingzeroes(structurals);                          
      structurals = structurals & (structurals - 1);
      base_ptr[base + 6] = (uint32_t)idx - 64 + trailingzeroes(structurals);                          
      structurals = structurals & (structurals - 1);
      base_ptr[base + 7] = (uint32_t)idx - 64 + trailingzeroes(structurals);                          
      structurals = structurals & (structurals - 1);
      base += 8;
    }
    base = next_base;
    // How do we build up a user traversable data structure
    // first, do a 'shufti' to detect structural JSON characters
    // they are { 0x7b } 0x7d : 0x3a [ 0x5b ] 0x5d , 0x2c
    // these go into the first 3 buckets of the comparison (1/2/4)

    // we are also interested in the four whitespace characters
    // space 0x20, linefeed 0x0a, horizontal tab 0x09 and carriage return 0x0d
    // these go into the next 2 buckets of the comparison (8/16)
    const __m256i low_nibble_mask = _mm256_setr_epi8(
        //  0                           9  a   b  c  d
        16, 0, 0, 0, 0, 0, 0, 0, 0, 8, 12, 1, 2, 9, 0, 0, 16, 0, 0, 0, 0, 0, 0,
        0, 0, 8, 12, 1, 2, 9, 0, 0);
    const __m256i high_nibble_mask = _mm256_setr_epi8(
        //  0     2   3     5     7
        8, 0, 18, 4, 0, 1, 0, 1, 0, 0, 0, 3, 2, 1, 0, 0, 8, 0, 18, 4, 0, 1, 0,
        1, 0, 0, 0, 3, 2, 1, 0, 0);

    __m256i structural_shufti_mask = _mm256_set1_epi8(0x7);
    __m256i whitespace_shufti_mask = _mm256_set1_epi8(0x18);

    __m256i v_lo = _mm256_and_si256(
        _mm256_shuffle_epi8(low_nibble_mask, input_lo),
        _mm256_shuffle_epi8(high_nibble_mask,
                            _mm256_and_si256(_mm256_srli_epi32(input_lo, 4),
                                             _mm256_set1_epi8(0x7f))));

    __m256i v_hi = _mm256_and_si256(
        _mm256_shuffle_epi8(low_nibble_mask, input_hi),
        _mm256_shuffle_epi8(high_nibble_mask,
                            _mm256_and_si256(_mm256_srli_epi32(input_hi, 4),
                                             _mm256_set1_epi8(0x7f))));
    __m256i tmp_lo = _mm256_cmpeq_epi8(
        _mm256_and_si256(v_lo, structural_shufti_mask), _mm256_set1_epi8(0));
    __m256i tmp_hi = _mm256_cmpeq_epi8(
        _mm256_and_si256(v_hi, structural_shufti_mask), _mm256_set1_epi8(0));

    uint64_t structural_res_0 = (uint32_t)_mm256_movemask_epi8(tmp_lo);
    uint64_t structural_res_1 = _mm256_movemask_epi8(tmp_hi);
    structurals = ~(structural_res_0 | (structural_res_1 << 32));

    // this additional mask and transfer is non-trivially expensive,
    // unfortunately
    __m256i tmp_ws_lo = _mm256_cmpeq_epi8(
        _mm256_and_si256(v_lo, whitespace_shufti_mask), _mm256_set1_epi8(0));
    __m256i tmp_ws_hi = _mm256_cmpeq_epi8(
        _mm256_and_si256(v_hi, whitespace_shufti_mask), _mm256_set1_epi8(0));

    uint64_t ws_res_0 = (uint32_t)_mm256_movemask_epi8(tmp_ws_lo);
    uint64_t ws_res_1 = _mm256_movemask_epi8(tmp_ws_hi);
    uint64_t whitespace = ~(ws_res_0 | (ws_res_1 << 32));


    // mask off anything inside quotes
    structurals &= ~quote_mask;

    // add the real quote bits back into our bitmask as well, so we can
    // quickly traverse the strings we've spent all this trouble gathering
    structurals |= quote_bits;

    // Now, establish "pseudo-structural characters". These are non-whitespace
    // characters that are (a) outside quotes and (b) have a predecessor that's
    // either whitespace or a structural character. This means that subsequent
    // passes will get a chance to encounter the first character of every string
    // of non-whitespace and, if we're parsing an atom like true/false/null or a
    // number we can stop at the first whitespace or structural character
    // following it.

    // a qualified predecessor is something that can happen 1 position before an
    // psuedo-structural character
    uint64_t pseudo_pred = structurals | whitespace;
    uint64_t shifted_pseudo_pred = (pseudo_pred << 1) | prev_iter_ends_pseudo_pred;
    //prev_iter_ends_pseudo_pred = pseudo_pred >> 63;
    uint64_t pseudo_structurals =
        shifted_pseudo_pred & (~whitespace) & (~quote_mask);
    structurals |= pseudo_structurals;

    // now, we've used our close quotes all we need to. So let's switch them off
    // they will be off in the quote mask and on in quote bits.
    structurals &= ~(quote_bits & ~quote_mask);
    //*(uint64_t *)(pj.structurals + idx / 8) = structurals;
    idx += 64;
  }
    uint32_t cnt = hamming(structurals);
    uint32_t next_base = base + cnt;
    while (structurals) {
      base_ptr[base + 0] = (uint32_t)idx - 64 + trailingzeroes(structurals);                          
      structurals = structurals & (structurals - 1);
      base_ptr[base + 1] = (uint32_t)idx - 64 + trailingzeroes(structurals);                          
      structurals = structurals & (structurals - 1);
      base_ptr[base + 2] = (uint32_t)idx - 64 + trailingzeroes(structurals);                          
      structurals = structurals & (structurals - 1);
      base_ptr[base + 3] = (uint32_t)idx - 64 + trailingzeroes(structurals);                          
      structurals = structurals & (structurals - 1);
      base_ptr[base + 4] = (uint32_t)idx - 64 + trailingzeroes(structurals);                          
      structurals = structurals & (structurals - 1);
      base_ptr[base + 5] = (uint32_t)idx - 64 + trailingzeroes(structurals);                          
      structurals = structurals & (structurals - 1);
      base_ptr[base + 6] = (uint32_t)idx - 64 + trailingzeroes(structurals);                          
      structurals = structurals & (structurals - 1);
      base_ptr[base + 7] = (uint32_t)idx - 64 + trailingzeroes(structurals);                          
      structurals = structurals & (structurals - 1);
      base += 8;
      base += 8;
    }
    base = next_base;

  pj.n_structural_indexes = base;
  if(base_ptr[pj.n_structural_indexes-1] > len) {
    fprintf( stderr,"Internal bug\n");
    return false;
  }
  if(len != base_ptr[pj.n_structural_indexes-1]) {
    // the string might not be NULL terminated, but we add a virtual NULL ending character. 
    base_ptr[pj.n_structural_indexes++] = len;
  }
  base_ptr[pj.n_structural_indexes] = 0; // make it safe to dereference one beyond this array

#ifdef SIMDJSON_UTF8VALIDATE
  return _mm256_testz_si256(has_error, has_error);
#else
  return true;
#endif
}
