#ifdef _MSC_VER
/* Microsoft C/C++-compatible compiler */
#include <intrin.h>
#else
#include <immintrin.h>
#include <x86intrin.h>
#endif

#include <cassert>
#include <cstring>

#include "jsonparser/common_defs.h"
#include "jsonparser/simdjson_internal.h"

// they are { 0x7b } 0x7d : 0x3a [ 0x5b ] 0x5d , 0x2c
// these go into the first 3 buckets of the comparison (1/2/4)

// we are also interested in the four whitespace characters
// space 0x20, linefeed 0x0a, horizontal tab 0x09 and carriage return 0x0d

const u32 structural_or_whitespace_negated[256] = {
    1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1,

    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 0, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 0, 1, 1,

    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,

    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1};

// return non-zero if not a structural or whitespace char
// zero otherwise
really_inline u32 is_not_structural_or_whitespace(u8 c) {
  return structural_or_whitespace_negated[c];
}

// These chars yield themselves: " \ /
// b -> backspace, f -> formfeed, n -> newline, r -> cr, t -> horizontal tab
// u not handled in this table as it's complex
const u8 escape_map[256] = {
    0, 0, 0,    0, 0,    0, 0,    0, 0, 0, 0, 0, 0,    0, 0,    0, // 0x0.
    0, 0, 0,    0, 0,    0, 0,    0, 0, 0, 0, 0, 0,    0, 0,    0,
    0, 0, 0x22, 0, 0,    0, 0,    0, 0, 0, 0, 0, 0,    0, 0,    0x2f,
    0, 0, 0,    0, 0,    0, 0,    0, 0, 0, 0, 0, 0,    0, 0,    0,

    0, 0, 0,    0, 0,    0, 0,    0, 0, 0, 0, 0, 0,    0, 0,    0, // 0x4.
    0, 0, 0,    0, 0,    0, 0,    0, 0, 0, 0, 0, 0x5c, 0, 0,    0, // 0x5.
    0, 0, 0x08, 0, 0,    0, 0x12, 0, 0, 0, 0, 0, 0,    0, 0x0a, 0, // 0x6.
    0, 0, 0x0d, 0, 0x09, 0, 0,    0, 0, 0, 0, 0, 0,    0, 0,    0, // 0x7.

    0, 0, 0,    0, 0,    0, 0,    0, 0, 0, 0, 0, 0,    0, 0,    0,
    0, 0, 0,    0, 0,    0, 0,    0, 0, 0, 0, 0, 0,    0, 0,    0,
    0, 0, 0,    0, 0,    0, 0,    0, 0, 0, 0, 0, 0,    0, 0,    0,
    0, 0, 0,    0, 0,    0, 0,    0, 0, 0, 0, 0, 0,    0, 0,    0,

    0, 0, 0,    0, 0,    0, 0,    0, 0, 0, 0, 0, 0,    0, 0,    0,
    0, 0, 0,    0, 0,    0, 0,    0, 0, 0, 0, 0, 0,    0, 0,    0,
    0, 0, 0,    0, 0,    0, 0,    0, 0, 0, 0, 0, 0,    0, 0,    0,
    0, 0, 0,    0, 0,    0, 0,    0, 0, 0, 0, 0, 0,    0, 0,    0,
};

const u32 leading_zeros_to_utf_bytes[33] = {
    1, 1, 1, 1, 1, 1, 1, 1,           // 7 bits for first one
    2, 2, 2, 2,                       // 11 bits for next
    3, 3, 3, 3, 3,                    // 16 bits for next
    4, 4, 4, 4, 4,                    // 21 bits for next
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}; // error

const u32 UTF_PDEP_MASK[5] = {0x00, // error
                              0x7f, 0x1f3f, 0x0f3f3f, 0x073f3f3f};

const u32 UTF_OR_MASK[5] = {0x00, // error
                            0x00, 0xc080, 0xe08080, 0xf0808080};

bool is_hex_digit(u8 v) {
  if (v >= '0' && v <= '9')
    return true;
  v &= 0xdf;
  if (v >= 'A' && v <= 'F')
    return true;
  return false;
}

u8 digit_to_val(u8 v) {
  if (v >= '0' && v <= '9')
    return v - '0';
  v &= 0xdf;
  return v - 'A' + 10;
}

bool hex_to_u32(const u8 *src, u32 *res) {
  u8 v1 = src[0];
  u8 v2 = src[1];
  u8 v3 = src[2];
  u8 v4 = src[3];
  if (!is_hex_digit(v1) || !is_hex_digit(v2) || !is_hex_digit(v3) ||
      !is_hex_digit(v4)) {
    return false;
  }
  *res = digit_to_val(v1) << 24 | digit_to_val(v2) << 16 |
         digit_to_val(v3) << 8 | digit_to_val(v4);
  return true;
}

// handle a unicode codepoint
// write appropriate values into dest
// src will always advance 6 bytes
// dest will advance a variable amount (return via pointer)
// return true if the unicode codepoint was valid
// We work in little-endian then swap at write time
really_inline bool handle_unicode_codepoint(const u8 **src_ptr, u8 **dst_ptr) {
  u32 code_point = 0; // read the hex, potentially reading another \u beyond if
                      // it's a // wacky one
  if (!hex_to_u32(*src_ptr + 2, &code_point)) {
    return false;
  }
  *src_ptr += 6;
  // check for the weirdo double-UTF-16 nonsense for things outside Basic
  // Multilingual Plane.
  if (code_point >= 0xd800 && code_point < 0xdc00) {
    // TODO: sanity check and clean up; snippeted from RapidJSON and poorly
    // understood at the moment
    if (((*src_ptr)[0] != '\\') || (*src_ptr)[1] != 'u') {
      return false;
    }
    u32 code_point_2 = 0;
    if (!hex_to_u32(*src_ptr + 2, &code_point_2)) {
      return false;
    }
    if (code_point_2 < 0xdc00 || code_point_2 > 0xdfff) {
      return false;
    }
    code_point =
        (((code_point - 0xd800) << 10) | (code_point_2 - 0xdc00)) + 0x10000;
    *src_ptr += 6;
  }
  // TODO: check to see whether the below code is nonsense (it's really only a
  // sketch at this point)
  u32 lz = __builtin_clz(code_point);
  u32 utf_bytes = leading_zeros_to_utf_bytes[lz];
  u32 tmp =
      _pdep_u32(code_point, UTF_PDEP_MASK[utf_bytes]) | UTF_OR_MASK[utf_bytes];
  // swap and move to the other side of the register
  tmp = __builtin_bswap32(tmp);
  tmp >>= ((4 - utf_bytes) * 8) & 31; // if utf_bytes, this could become a shift
                                      // by 32, hence the mask with 31
  // use memcpy to avoid undefined behavior:
  std::memcpy(*(u32 **)dst_ptr, &tmp, sizeof(u32)); //**(u32 **)dst_ptr = tmp;
  *dst_ptr += utf_bytes;
  return true;
}

really_inline bool parse_string(const u8 *buf, UNUSED size_t len,
                                ParsedJson &pj, u32 tape_loc) {
  u32 offset = pj.tape[tape_loc] & 0xffffff;
  const u8 *src = &buf[offset + 1]; // we know that buf at offset is a "
  u8 *dst = pj.current_string_buf_loc;
#ifdef DEBUG
  cout << "Entering parse string with offset " << offset << "\n";
#endif
  // basic non-sexy parsing code
  while (1) {
#ifdef DEBUG
    for (u32 j = 0; j < 32; j++) {
      char c = *(src + j);
      if (isprint(c)) {
        cout << c;
      } else {
        cout << '_';
      }
    }
    cout << "|  ... string handling input\n";
#endif
    m256 v = _mm256_loadu_si256((const m256 *)(src));
    u32 bs_bits =
        (u32)_mm256_movemask_epi8(_mm256_cmpeq_epi8(v, _mm256_set1_epi8('\\')));
    dumpbits32(bs_bits, "backslash bits 2");
    u32 quote_bits =
        (u32)_mm256_movemask_epi8(_mm256_cmpeq_epi8(v, _mm256_set1_epi8('"')));
    dumpbits32(quote_bits, "quote_bits");
    u32 quote_dist = __builtin_ctz(quote_bits);
    u32 bs_dist = __builtin_ctz(bs_bits);
    // store to dest unconditionally - we can overwrite the bits we don't like
    // later
    _mm256_storeu_si256((m256 *)(dst), v);
#ifdef DEBUG
    cout << "quote dist: " << quote_dist << " bs dist: " << bs_dist << "\n";
#endif

    if (quote_dist < bs_dist) {
#ifdef DEBUG
      cout << "Found end, leaving!\n";
#endif
      // we encountered quotes first. Move dst to point to quotes and exit
      dst[quote_dist] = 0; // null terminate and get out
      pj.current_string_buf_loc = dst + quote_dist + 1;
      pj.tape[tape_loc] =
          ((u32)'"') << 24 |
          (pj.current_string_buf_loc -
           pj.string_buf); // assume 2^24 will hold all strings for now
      return true;
    } else if (quote_dist > bs_dist) {
      u8 escape_char = src[bs_dist + 1];
#ifdef DEBUG
      cout << "Found escape char: " << escape_char << "\n";
#endif
      // we encountered backslash first. Handle backslash
      if (escape_char == 'u') {
        // move src/dst up to the start; they will be further adjusted
        // within the unicode codepoint handling code.
        src += bs_dist;
        dst += bs_dist;
        if (!handle_unicode_codepoint(&src, &dst)) {
          return false;
        }
        return true;
      } else {
        // simple 1:1 conversion. Will eat bs_dist+2 characters in input and
        // write bs_dist+1 characters to output
        // note this may reach beyond the part of the buffer we've actually
        // seen. I think this is ok
        u8 escape_result = escape_map[escape_char];
        if (!escape_result)
          return false; // bogus escape value is an error
        dst[bs_dist] = escape_result;
        src += bs_dist + 2;
        dst += bs_dist + 1;
      }
    } else {
      // they are the same. Since they can't co-occur, it means we encountered
      // neither.
      src += 32;
      dst += 32;
    }
    return true;
  }
  // later extensions -
  // if \\ we could detect whether it's a substantial run of \ or just eat 2
  // chars and write 1 handle anything short of \u or \\\ (as a prefix) with
  // clever PSHUFB stuff and don't leave SIMD
  return true;
}

#ifdef DOUBLECONV
#include "double-conversion/double-conversion.h"
#include "double-conversion/ieee.h"
using namespace double_conversion;
static StringToDoubleConverter
    converter(StringToDoubleConverter::ALLOW_TRAILING_JUNK, 2000000.0,
              Double::NaN(), NULL, NULL);
#endif


// does not validation whatsoever, assumes that all digit
// it should be quite fast
u64 naivestrtoll(const char *p, const char *end) {
    if(p == end) return 0; // should be an error?
    // this code could get a whole lot smarter if we have many long ints:
    // e.g., see http://0x80.pl/articles/simd-parsing-int-sequences.html
    u64 x = *p - '0';
    p++;
    for(;p < end;p++) {
      x = (x*10) + (*p - '0'); // this looks like a multiplication
      // but optimizing compilers produce code with relatively low
      // latency, so data dependency is small
    }
    return x;
}

static const double power_of_ten[] = {
    1e-308, 1e-307, 1e-306, 1e-305, 1e-304, 1e-303, 1e-302, 1e-301, 1e-300,
    1e-299, 1e-298, 1e-297, 1e-296, 1e-295, 1e-294, 1e-293, 1e-292, 1e-291,
    1e-290, 1e-289, 1e-288, 1e-287, 1e-286, 1e-285, 1e-284, 1e-283, 1e-282,
    1e-281, 1e-280, 1e-279, 1e-278, 1e-277, 1e-276, 1e-275, 1e-274, 1e-273,
    1e-272, 1e-271, 1e-270, 1e-269, 1e-268, 1e-267, 1e-266, 1e-265, 1e-264,
    1e-263, 1e-262, 1e-261, 1e-260, 1e-259, 1e-258, 1e-257, 1e-256, 1e-255,
    1e-254, 1e-253, 1e-252, 1e-251, 1e-250, 1e-249, 1e-248, 1e-247, 1e-246,
    1e-245, 1e-244, 1e-243, 1e-242, 1e-241, 1e-240, 1e-239, 1e-238, 1e-237,
    1e-236, 1e-235, 1e-234, 1e-233, 1e-232, 1e-231, 1e-230, 1e-229, 1e-228,
    1e-227, 1e-226, 1e-225, 1e-224, 1e-223, 1e-222, 1e-221, 1e-220, 1e-219,
    1e-218, 1e-217, 1e-216, 1e-215, 1e-214, 1e-213, 1e-212, 1e-211, 1e-210,
    1e-209, 1e-208, 1e-207, 1e-206, 1e-205, 1e-204, 1e-203, 1e-202, 1e-201,
    1e-200, 1e-199, 1e-198, 1e-197, 1e-196, 1e-195, 1e-194, 1e-193, 1e-192,
    1e-191, 1e-190, 1e-189, 1e-188, 1e-187, 1e-186, 1e-185, 1e-184, 1e-183,
    1e-182, 1e-181, 1e-180, 1e-179, 1e-178, 1e-177, 1e-176, 1e-175, 1e-174,
    1e-173, 1e-172, 1e-171, 1e-170, 1e-169, 1e-168, 1e-167, 1e-166, 1e-165,
    1e-164, 1e-163, 1e-162, 1e-161, 1e-160, 1e-159, 1e-158, 1e-157, 1e-156,
    1e-155, 1e-154, 1e-153, 1e-152, 1e-151, 1e-150, 1e-149, 1e-148, 1e-147,
    1e-146, 1e-145, 1e-144, 1e-143, 1e-142, 1e-141, 1e-140, 1e-139, 1e-138,
    1e-137, 1e-136, 1e-135, 1e-134, 1e-133, 1e-132, 1e-131, 1e-130, 1e-129,
    1e-128, 1e-127, 1e-126, 1e-125, 1e-124, 1e-123, 1e-122, 1e-121, 1e-120,
    1e-119, 1e-118, 1e-117, 1e-116, 1e-115, 1e-114, 1e-113, 1e-112, 1e-111,
    1e-110, 1e-109, 1e-108, 1e-107, 1e-106, 1e-105, 1e-104, 1e-103, 1e-102,
    1e-101, 1e-100, 1e-99,  1e-98,  1e-97,  1e-96,  1e-95,  1e-94,  1e-93,
    1e-92,  1e-91,  1e-90,  1e-89,  1e-88,  1e-87,  1e-86,  1e-85,  1e-84,
    1e-83,  1e-82,  1e-81,  1e-80,  1e-79,  1e-78,  1e-77,  1e-76,  1e-75,
    1e-74,  1e-73,  1e-72,  1e-71,  1e-70,  1e-69,  1e-68,  1e-67,  1e-66,
    1e-65,  1e-64,  1e-63,  1e-62,  1e-61,  1e-60,  1e-59,  1e-58,  1e-57,
    1e-56,  1e-55,  1e-54,  1e-53,  1e-52,  1e-51,  1e-50,  1e-49,  1e-48,
    1e-47,  1e-46,  1e-45,  1e-44,  1e-43,  1e-42,  1e-41,  1e-40,  1e-39,
    1e-38,  1e-37,  1e-36,  1e-35,  1e-34,  1e-33,  1e-32,  1e-31,  1e-30,
    1e-29,  1e-28,  1e-27,  1e-26,  1e-25,  1e-24,  1e-23,  1e-22,  1e-21,
    1e-20,  1e-19,  1e-18,  1e-17,  1e-16,  1e-15,  1e-14,  1e-13,  1e-12,
    1e-11,  1e-10,  1e-9,   1e-8,   1e-7,   1e-6,   1e-5,   1e-4,   1e-3,
    1e-2,   1e-1,   1e0,    1e1,    1e2,    1e3,    1e4,    1e5,    1e6,
    1e7,    1e8,    1e9,    1e10,   1e11,   1e12,   1e13,   1e14,   1e15,
    1e16,   1e17,   1e18,   1e19,   1e20,   1e21,   1e22,   1e23,   1e24,
    1e25,   1e26,   1e27,   1e28,   1e29,   1e30,   1e31,   1e32,   1e33,
    1e34,   1e35,   1e36,   1e37,   1e38,   1e39,   1e40,   1e41,   1e42,
    1e43,   1e44,   1e45,   1e46,   1e47,   1e48,   1e49,   1e50,   1e51,
    1e52,   1e53,   1e54,   1e55,   1e56,   1e57,   1e58,   1e59,   1e60,
    1e61,   1e62,   1e63,   1e64,   1e65,   1e66,   1e67,   1e68,   1e69,
    1e70,   1e71,   1e72,   1e73,   1e74,   1e75,   1e76,   1e77,   1e78,
    1e79,   1e80,   1e81,   1e82,   1e83,   1e84,   1e85,   1e86,   1e87,
    1e88,   1e89,   1e90,   1e91,   1e92,   1e93,   1e94,   1e95,   1e96,
    1e97,   1e98,   1e99,   1e100,  1e101,  1e102,  1e103,  1e104,  1e105,
    1e106,  1e107,  1e108,  1e109,  1e110,  1e111,  1e112,  1e113,  1e114,
    1e115,  1e116,  1e117,  1e118,  1e119,  1e120,  1e121,  1e122,  1e123,
    1e124,  1e125,  1e126,  1e127,  1e128,  1e129,  1e130,  1e131,  1e132,
    1e133,  1e134,  1e135,  1e136,  1e137,  1e138,  1e139,  1e140,  1e141,
    1e142,  1e143,  1e144,  1e145,  1e146,  1e147,  1e148,  1e149,  1e150,
    1e151,  1e152,  1e153,  1e154,  1e155,  1e156,  1e157,  1e158,  1e159,
    1e160,  1e161,  1e162,  1e163,  1e164,  1e165,  1e166,  1e167,  1e168,
    1e169,  1e170,  1e171,  1e172,  1e173,  1e174,  1e175,  1e176,  1e177,
    1e178,  1e179,  1e180,  1e181,  1e182,  1e183,  1e184,  1e185,  1e186,
    1e187,  1e188,  1e189,  1e190,  1e191,  1e192,  1e193,  1e194,  1e195,
    1e196,  1e197,  1e198,  1e199,  1e200,  1e201,  1e202,  1e203,  1e204,
    1e205,  1e206,  1e207,  1e208,  1e209,  1e210,  1e211,  1e212,  1e213,
    1e214,  1e215,  1e216,  1e217,  1e218,  1e219,  1e220,  1e221,  1e222,
    1e223,  1e224,  1e225,  1e226,  1e227,  1e228,  1e229,  1e230,  1e231,
    1e232,  1e233,  1e234,  1e235,  1e236,  1e237,  1e238,  1e239,  1e240,
    1e241,  1e242,  1e243,  1e244,  1e245,  1e246,  1e247,  1e248,  1e249,
    1e250,  1e251,  1e252,  1e253,  1e254,  1e255,  1e256,  1e257,  1e258,
    1e259,  1e260,  1e261,  1e262,  1e263,  1e264,  1e265,  1e266,  1e267,
    1e268,  1e269,  1e270,  1e271,  1e272,  1e273,  1e274,  1e275,  1e276,
    1e277,  1e278,  1e279,  1e280,  1e281,  1e282,  1e283,  1e284,  1e285,
    1e286,  1e287,  1e288,  1e289,  1e290,  1e291,  1e292,  1e293,  1e294,
    1e295,  1e296,  1e297,  1e298,  1e299,  1e300,  1e301,  1e302,  1e303,
    1e304,  1e305,  1e306,  1e307,  1e308};


// put a parsed version of number (either as a double or a signed long) into the
// number buffer, put a 'tag' indicating which type and where it is back onto
// the tape at that location return false if we can't parse the number which
// means either (a) the number isn't valid, or (b) the number is followed by
// something that isn't whitespace, comma or a close }] character which are the
// only things that should follow a number at this stage bools to detect what we
// found in our initial character already here - we are already switching on 0
// vs 1-9 vs - so we may as well keep separate paths where that's useful

// TODO: see if we really need a separate number_buf or whether we should just
//       have a generic scratch - would need to align before using for this
really_inline bool parse_number(const u8 *buf, UNUSED size_t len,
                                UNUSED ParsedJson &pj, u32 tape_loc,
                                UNUSED bool found_zero, bool found_minus) {
  u32 offset = pj.tape[tape_loc] & 0xffffff;
////////////////
// This is temporary... but it illustrates how one could use Google's double
// conv.
///
#ifdef DOUBLECONV
  // Maybe surprisingly, StringToDouble does not parse according to the JSON
  // spec (e.g., it will happily parse 012 as 12).
  int processed_characters_count;
  double result_double_conv = converter.StringToDouble(
      (const char *)(buf + offset), 10, &processed_characters_count);
  *((double *)pj.current_number_buf_loc) = result_double_conv;
  pj.tape[tape_loc] =
        ((u32)'d') << 24 |
        (pj.current_number_buf_loc -
         pj.number_buf); // assume 2^24 will hold all numbers for now
  pj.current_number_buf_loc += 8;
  return result_double_conv == result_double_conv;
#endif
  ////////////////
  // end of double conv temporary stuff.
  ////////////////
  if (found_minus) {
    offset++;
  }
  const u8 *src = &buf[offset];
  m256 v = _mm256_loadu_si256((const m256 *)(src));
  u64 error_sump = 0;
#ifdef DEBUG
  for (u32 j = 0; j < 32; j++) {
    char c = *(src + j);
    if (isprint(c)) {
      cout << c;
    } else {
      cout << '_';
    }
  }
  cout << "|  ... number handling input\n";
#endif

  // categories to extract
  // Digits:
  // 0 (0x30) - bucket 0
  // 1-9 (never any distinction except if we didn't get the free kick at 0 due
  // to the leading minus) (0x31-0x39) - bucket 1
  // . (0x2e) - bucket 2
  // E or e - no distinction (0x45/0x65) - bucket 3
  // + (0x2b) - bucket 4
  // - (0x2d) - bucket 4
  // Terminators
  // Whitespace: 0x20, 0x09, 0x0a, 0x0d - bucket 5+6
  // Comma and the closes: 0x2c is comma, } is 0x5d, ] is 0x7d - bucket 5+7

  // Another shufti - also a bit hand-hacked. Need to make a better construction
  const m256 low_nibble_mask = _mm256_setr_epi8(
      //  0   1   2   3   4   5   6   7   8   9   a   b   c   d   e   f
      33, 2, 2, 2, 2, 10, 2, 2, 2, 66, 64, 16, 32, 0xd0, 4, 0, 33, 2, 2, 2, 2,
      10, 2, 2, 2, 66, 64, 16, 32, 0xd0, 4, 0);
  const m256 high_nibble_mask = _mm256_setr_epi8(
      //  0   1   2   3   4   5   6   7   8   9   a   b   c   d   e   f
      64, 0, 52, 3, 8, -128, 8, 0x80, 0, 0, 0, 0, 0, 0, 0, 0, 64, 0, 52, 3, 8,
      -128, 8, 0x80, 0, 0, 0, 0, 0, 0, 0, 0);

  m256 tmp = _mm256_and_si256(
      _mm256_shuffle_epi8(low_nibble_mask, v),
      _mm256_shuffle_epi8(
          high_nibble_mask,
          _mm256_and_si256(_mm256_srli_epi32(v, 4), _mm256_set1_epi8(0x7f))));
#ifdef DEBUG
  // let us print out the magic:
  uint8_t buffer[32];
  _mm256_storeu_si256((__m256i *)buffer,tmp);
  for(int k = 0; k < 32; k++)
  printf("%.2x ",buffer[k]);
  printf("\n");
#endif
  m256 enders_mask = _mm256_set1_epi8(0xe0);
  m256 tmp_enders = _mm256_cmpeq_epi8(_mm256_and_si256(tmp, enders_mask),
                                      _mm256_set1_epi8(0));
  u32 enders = ~(u32)_mm256_movemask_epi8(tmp_enders);
  dumpbits32(enders, "ender characters");
  //dumpbits32_always(enders, "ender characters");

  if (enders == 0) {
    error_sump = 1;
    //  if enders == 0  we have
    // a heroically long number string or some garbage
  }
  u32 number_mask = ~enders & (enders - 1);
  dumpbits32(number_mask, "number mask");
  //dumpbits32_always(number_mask, "number mask");
  m256 n_mask = _mm256_set1_epi8(0x1f);
  m256 tmp_n =
      _mm256_cmpeq_epi8(_mm256_and_si256(tmp, n_mask), _mm256_set1_epi8(0));
  u32 number_characters = ~(u32)_mm256_movemask_epi8(tmp_n);

  // put something into our error sump if we have something
  // before our ending characters that isn't a valid character
  // for the inside of our JSON
  number_characters &= number_mask;
  error_sump |= number_characters ^ number_mask;
  dumpbits32(number_characters, "number characters");

  m256 d_mask = _mm256_set1_epi8(0x03);
  m256 tmp_d =
      _mm256_cmpeq_epi8(_mm256_and_si256(tmp, d_mask), _mm256_set1_epi8(0));
  u32 digit_characters = ~(u32)_mm256_movemask_epi8(tmp_d);
  digit_characters &= number_mask;
  dumpbits32(digit_characters, "digit characters");

  // the last component of our number should be a digit
  ((number_mask >> 1) & digit_characters)
  //  dumpbits32_always(digit_characters, "digit characters");



  m256 p_mask = _mm256_set1_epi8(0x04);
  m256 tmp_p =
      _mm256_cmpeq_epi8(_mm256_and_si256(tmp, p_mask), _mm256_set1_epi8(0));
  u32 decimal_characters = ~(u32)_mm256_movemask_epi8(tmp_p);
  decimal_characters &= number_mask;
  dumpbits32(decimal_characters, "decimal characters");


  // the decimal character must be unique or absent
  error_sump |= ((decimal_characters) & (decimal_characters - 1));

  m256 e_mask = _mm256_set1_epi8(0x08);
  m256 tmp_e =
      _mm256_cmpeq_epi8(_mm256_and_si256(tmp, e_mask), _mm256_set1_epi8(0));
  u32 exponent_characters = ~(u32)_mm256_movemask_epi8(tmp_e);
  exponent_characters &= number_mask;
  dumpbits32(exponent_characters, "exponent characters");

  // the exponent character must be unique or absent
  error_sump |= ((exponent_characters) & (exponent_characters - 1));

  // if they exist the exponent character must follow the decimal_characters character
  error_sump |= ((exponent_characters - 1) & decimal_characters) ^ decimal_characters;


  m256 zero_mask = _mm256_set1_epi8(0x1);
  m256 tmp_zero =
      _mm256_cmpeq_epi8(tmp, zero_mask);
  u32 zero_characters = (u32)_mm256_movemask_epi8(tmp_zero);
  dumpbits32(zero_characters, "zero characters");

  // if the  zero character is in first position, it
  // needs to be followed by decimal or exponent or ender (note: we
  // handle found_minus separately)
  u32 expo_or_decimal_or_ender = exponent_characters | decimal_characters | enders;
  error_sump |= zero_characters & 0x01 & (~(expo_or_decimal_or_ender >> 1));

  m256 s_mask = _mm256_set1_epi8(0x10);
  m256 tmp_s =
      _mm256_cmpeq_epi8(_mm256_and_si256(tmp, s_mask), _mm256_set1_epi8(0));


  u32 sign_characters = ~(u32)_mm256_movemask_epi8(tmp_s);
  sign_characters &= number_mask;
  dumpbits32(sign_characters, "sign characters");

  // any sign character must be followed by a digit
  error_sump |= (~(digit_characters >> 1)) & sign_characters;

  // there is at most one sign character
  error_sump |= ((sign_characters) & (sign_characters - 1));

  // the exponent must be followed by either a sign character or a digit
  error_sump |= (~((digit_characters|sign_characters) >> 1)) & exponent_characters;

  u32 digit_edges = ~(digit_characters << 1) & digit_characters;
  dumpbits32(digit_edges, "digit_edges");

  // check that we have 1-3 'edges' only
  u32 t = digit_edges;
  t &= t - 1;
  t &= t - 1;
  t &= t - 1;
  error_sump |= t;

  // check that we start with a digit
  error_sump |= ~digit_characters & 0x1;

  // having done some checks, get lazy and fall back
  // to strtoll or strtod
  // TODO: handle the easy cases ourselves; these are
  // expensive and we've done a lot of the prepwork.
  // return errors if strto* fail, otherwise fill in a code on the tape
  // 'd' for floating point and 'l' for long and put a pointer to the
  // spot in the buffer.
  if ( digit_edges == 1) {
  //if (__builtin_popcount(digit_edges) == 1) { // DANIEL :  shouldn't we have digit_edges == 1
#define NAIVEINTPARSING // naive means "faster" in this case
#ifdef NAIVEINTPARSING
    // this is faster, maybe, because we use a naive strtoll
    // should be all digits?
    error_sump |= number_characters ^ digit_characters;
    int stringlength = __builtin_ctz(~digit_characters);
    const char *end = (const char *)src + stringlength;
    u64 result = naivestrtoll((const char *)src,end);
    if (found_minus) { // unfortunate that it is a branch?
      result = -result;
    }
#else
    // try a strtoll (this is likely slower because it revalidates)
    char *end;
    u64 result = strtoll((const char *)src, &end, 10);
    if ((errno != 0) || (end == (const char *)src)) {
      error_sump |= 1;
    }
    error_sump |= is_not_structural_or_whitespace(*end);
    if (found_minus) {
      result = -result;
    }
#endif
#ifdef DEBUG
    cout << "Found number " << result << "\n";
#endif
    *((u64 *)pj.current_number_buf_loc) = result;
    pj.tape[tape_loc] =
        ((u32)'l') << 24 |
        (pj.current_number_buf_loc -
         pj.number_buf); // assume 2^24 will hold all numbers for now
    pj.current_number_buf_loc += 8;
  } else {
//#define FASTSTRTOD
#ifdef FASTSTRTOD
   //dumpbits32_always(digit_edges, "digit_edges");



  // In an ideal world, this would be branchless, but... hey...
  // many of these branches ought to be predictable!

  const char *p = (const char *)src;
  // we start with digits followed by "." or "e" or "E".
  // scan them
  int justdigitlength = __builtin_ctz(exponent_characters | decimal_characters);
  const char *endjustdigit = p + justdigitlength;
  uint64_t integerpart = *p - '0';// there must be at least one digit
  p++;
  for(;p !=endjustdigit;p++) {
    integerpart = (integerpart*10) + (*p - '0');
  }
  double result = integerpart;
  if(decimal_characters != 0) {
        p++;
        justdigitlength = __builtin_ctz(exponent_characters | enders);
        const char *end = (const char *)src + justdigitlength;
        int fracdigitcount = end - p;
        uint64_t fractionalpart = 0;
        for(;p !=end;p++) {
          fractionalpart = (fractionalpart*10) + (*p - '0');
        }
        result += fractionalpart * power_of_ten[308 - fracdigitcount];
  }
  if(exponent_characters != 0) {
    p++;// skip exponent

    if(p[0] == '+') p++;
    if(p[0] == '-') p++;
    int stringlength = __builtin_ctz(exponent_characters | enders);
  }
#else
    // try a strtod
    char *end;
    double result = strtod((const char *)src, &end);
    if ((errno != 0) || (end == (const char *)src)) {
      error_sump |= 1;
    }
    error_sump |= is_not_structural_or_whitespace(*end);
#endif // FASTSTRTOD
    if (found_minus) {
      result = -result;
    }
#ifdef DEBUG
    cout << "Found number " << result << "\n";
#endif
    *((double *)pj.current_number_buf_loc) = result;
    pj.tape[tape_loc] =
        ((u32)'d') << 24 |
        (pj.current_number_buf_loc -
         pj.number_buf); // assume 2^24 will hold all numbers for now
    pj.current_number_buf_loc += 8;
  }


  // TODO: if it exists,
  // Decimal point is after the first cluster of numbers only
  // and before the second cluster of numbers only. It must
  // be digit_or_zero . digit_or_zero strictly

  // TODO: eE mark and +- construct are adjacent with eE first
  // eE mark preceeds final cluster of numbers only
  // and immediately follows second-last cluster of numbers only (not
  // necessarily second, as we may have 4e10).
  // it may suffice to insist that eE is preceeded immediately
  // by a digit of any kind and that it's followed locally by
  // a digit immediately or a +- construct then a digit.


  if (error_sump)
    return false;
  return true;
}

bool tape_disturbed(u32 i, ParsedJson &pj) {
  u32 start_loc = i * MAX_TAPE_ENTRIES;
  u32 end_loc = pj.tape_locs[i];
  return start_loc != end_loc;
}

bool shovel_machine(const u8 *buf, size_t len, ParsedJson &pj) {
  // fixup the mess made by the ape_machine
  // as such it does a bunch of miscellaneous things on the tapes
  u32 error_sump = 0;
  u64 tv = *(const u64 *)"true    ";
  u64 nv = *(const u64 *)"null    ";
  u64 fv = *(const u64 *)"false   ";
  u64 mask4 = 0x00000000ffffffff;
  u64 mask5 = 0x000000ffffffffff;

  // if the tape has been touched at all at the depths outside the safe
  // zone we need to quit. Note that our periodic checks to see that we're
  // inside our safe zone in stage 3 don't guarantee that the system did
  // not get into the danger area briefly.
  if (tape_disturbed(START_DEPTH - 1, pj) ||
      tape_disturbed(REDLINE_DEPTH, pj)) {
    return false;
  }

  // walk over each tape
  for (u32 i = START_DEPTH; i < MAX_DEPTH; i++) {
    u32 start_loc = i * MAX_TAPE_ENTRIES;
    u32 end_loc = pj.tape_locs[i];
    if (start_loc == end_loc) {
      break;
    }
    for (u32 j = start_loc; j < end_loc; j++) {
      switch (pj.tape[j] >> 56) {
      case '{':
      case '[': {
        // pivot our tapes
        // point the enclosing structural char (}]) to the head marker ({[) and
        // put the end of the sequence on the tape at the head marker
        // we start with head marker pointing at the enclosing structural char
        // and the enclosing structural char pointing at the end. Just swap
        // them. also check the balanced-{} or [] property here
        u8 head_marker_c = pj.tape[j] >> 56;
        u32 head_marker_loc = pj.tape[j] & 0xffffffffffffffULL;
        u64 tape_enclosing = pj.tape[head_marker_loc];
        u8 enclosing_c = tape_enclosing >> 56;
        pj.tape[head_marker_loc] = pj.tape[j];
        pj.tape[j] = tape_enclosing;
        error_sump |= (enclosing_c - head_marker_c -
                       2); // [] and {} only differ by 2 chars
        break;
      }
      case '"': {
        error_sump |= !parse_string(buf, len, pj, j);
        break;
      }
      case '1':
      case '2':
      case '3':
      case '4':
      case '5':
      case '6':
      case '7':
      case '8':
      case '9':
        error_sump |= !parse_number(buf, len, pj, j, false, false);
        break;
      case '0':
        error_sump |= !parse_number(buf, len, pj, j, true, false);
        break;
      case '-':
        error_sump |= !parse_number(buf, len, pj, j, false, true);
        break;
      case 't': {
        u32 offset = pj.tape[j] & 0xffffffffffffffULL;
        const u8 *loc = buf + offset;
        u64 locval; // we want to avoid unaligned 64-bit loads (undefined in
                    // C/C++)
        std::memcpy(&locval, loc, sizeof(u64));
        error_sump |= (locval & mask4) ^ tv;
        error_sump |= is_not_structural_or_whitespace(loc[4]);
        break;
      }
      case 'f': {
        u32 offset = pj.tape[j] & 0xffffffffffffffULL;
        const u8 *loc = buf + offset;
        u64 locval; // we want to avoid unaligned 64-bit loads (undefined in
                    // C/C++)
        std::memcpy(&locval, loc, sizeof(u64));
        error_sump |= (locval & mask5) ^ fv;
        error_sump |= is_not_structural_or_whitespace(loc[5]);
        break;
      }
      case 'n': {
        u32 offset = pj.tape[j] & 0xffffffffffffffULL;
        const u8 *loc = buf + offset;
        u64 locval; // we want to avoid unaligned 64-bit loads (undefined in
                    // C/C++)
        std::memcpy(&locval, loc, sizeof(u64));
        error_sump |= (locval & mask4) ^ nv;
        error_sump |= is_not_structural_or_whitespace(loc[4]);
        break;
      }
      default:
        break;
      }
    }
  }
  if (error_sump) {
    return false;
  }
  return true;
}
