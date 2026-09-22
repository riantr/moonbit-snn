#ifdef __cplusplus
extern "C" {
#endif

#include "moonbit.h"
#include "moonbit_runtime.h"
#include "moonbit_simd.h"

#ifdef _MSC_VER
#define _Noreturn __declspec(noreturn)
#endif

#if defined(__clang__)
#pragma clang diagnostic ignored "-Wshift-op-parentheses"
#pragma clang diagnostic ignored "-Wtautological-compare"
#endif

// TinyCC's in-memory run mode has no executable of its own; an OS lookup would
// report the host process instead.
#if defined(__TCC_RUN__)
#define moonbit_rt_get_current_exe moonbit_rt_get_current_exe_tcc
#endif

MOONBIT_EXPORT _Noreturn void moonbit_panic(void);
MOONBIT_EXPORT void *moonbit_malloc_array(enum moonbit_block_kind kind,
                                          int elem_size_shift, int32_t len);
int memcmp(const void *s1, const void *s2, size_t n);
MOONBIT_EXPORT int moonbit_val_array_equal_sized(const void *lhs,
                                                 const void *rhs,
                                                 int32_t elem_size);
MOONBIT_EXPORT moonbit_string_t moonbit_add_string(moonbit_string_t s1,
                                                   moonbit_string_t s2);
MOONBIT_EXPORT void moonbit_unsafe_bytes_blit(moonbit_bytes_t dst,
                                              int32_t dst_start,
                                              moonbit_bytes_t src,
                                              int32_t src_offset, int32_t len);
MOONBIT_EXPORT moonbit_string_t moonbit_unsafe_bytes_sub_string(
    moonbit_bytes_t bytes, int32_t start, int32_t len);
MOONBIT_EXPORT int32_t moonbit_unsafe_val_array_blit(void *dst,
                                                     int32_t dst_offset,
                                                     void *src,
                                                     int32_t src_offset,
                                                     int32_t len,
                                                     int32_t elem_size);
MOONBIT_EXPORT int32_t moonbit_unsafe_ref_array_blit(void *dst,
                                                     int32_t dst_offset,
                                                     void *src,
                                                     int32_t src_offset,
                                                     int32_t len);
MOONBIT_EXPORT void moonbit_println(moonbit_string_t str);
MOONBIT_EXPORT void moonbit_eprintln(moonbit_string_t str);
MOONBIT_EXPORT moonbit_bytes_t *moonbit_get_cli_args(void);
MOONBIT_EXPORT void moonbit_runtime_init(int argc, char **argv);
MOONBIT_EXPORT void moonbit_drop_object(void *);
// Slow paths of the inlined value-enum retain/release below (defined in
// runtime.c). Internal helpers, so declared here rather than in the public
// moonbit.h; reached only when a value's current variant carries references.
MOONBIT_EXPORT void moonbit_incref_value_enum_loop(void *p);
MOONBIT_EXPORT void moonbit_decref_value_enum_loop(void *p);
MOONBIT_EXPORT int32_t moonbit_utf16_len_from_utf8(moonbit_bytes_t src,
                                                   int32_t src_offset,
                                                   int32_t src_length);
MOONBIT_EXPORT int32_t moonbit_utf8_decode_into_utf16(
    moonbit_bytes_t src, int32_t src_offset, int32_t src_length,
    moonbit_string_t dst, int32_t dst_offset);
MOONBIT_EXPORT int32_t moonbit_utf8_decode_lossy_into_utf16(
    moonbit_bytes_t src, int32_t src_offset, int32_t src_length,
    moonbit_string_t dst, int32_t dst_offset);
MOONBIT_EXPORT int32_t moonbit_utf8_len_from_utf16(moonbit_string_t src,
                                                   int32_t src_offset,
                                                   int32_t src_length);
MOONBIT_EXPORT int32_t moonbit_utf8_encode_from_utf16(
    moonbit_string_t src, int32_t src_offset, int32_t src_length,
    moonbit_bytes_t dst, int32_t dst_offset);

#if !defined(_WIN64) && !defined(_WIN32)
void *malloc(size_t size);
void free(void *ptr);
#define libc_malloc malloc
#define libc_free free
#endif

// several important runtime functions are inlined
static void *moonbit_malloc_inlined(size_t size) {
  struct moonbit_object *ptr =
    (struct moonbit_object *)MOONBIT_MALLOC_RAW(sizeof(struct moonbit_object) + size);
  Moonbit_init_dynamic_rc(ptr, moonbit_BLOCK_KIND_REGULAR);
  return ptr + 1;
}

#define moonbit_malloc(obj) moonbit_malloc_inlined(obj)

#define MOONBIT_RC_COUNT_UNIT ((int32_t)(1u << MOONBIT_RC_COUNT_SHIFT))
#define raw_rc_is_dynamic(rc) ((int32_t)(rc) >= MOONBIT_RC_COUNT_UNIT)
#define raw_rc_is_shared(rc) ((int32_t)(rc) >= (MOONBIT_RC_COUNT_UNIT * 2))

static inline int32_t moonbit_unsafe_obj_cycle_capable(void *ptr) {
  return MOONBIT_IN_ROOT(ptr) ||
         !MOONBIT_CHECK_CYCLE_STATUS(
             ptr, moonbit_CYCLE_STATUS_ACYCLIC_OR_CANDIDATE);
}

extern const uint32_t *moonbit_layout_table;

static void moonbit_incref_inlined(void *ptr) {
  struct moonbit_object *header = Moonbit_object_header(ptr);
  int32_t const rc = header->rc;
  if (raw_rc_is_dynamic(rc)) {
    Moonbit_increase_rc_count(header);
  }
}

#define moonbit_incref moonbit_incref_inlined

static void moonbit_decref_inlined(void *ptr) {
  struct moonbit_object *header = Moonbit_object_header(ptr);
  int32_t const rc = header->rc;
  if (raw_rc_is_shared(rc)) {
    header->rc = rc - MOONBIT_RC_COUNT_UNIT;
  } else if (raw_rc_is_dynamic(rc)) {
    moonbit_drop_object(ptr);
  }
}

#define moonbit_decref moonbit_decref_inlined

// Value-enum retain/release: inline the cheap "does the current variant carry
// references?" test (a header read + class compare) so scalar-tag moves pay no
// call, and delegate the reference-walking loop to the out-of-line slow path in
// runtime.c. Mirrors the moonbit_incref/decref fast/slow split above.
static inline void moonbit_incref_value_enum_inlined(void *p) {
  if (Moonbit_header_layout_class(*(uint32_t *)p) ==
      MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED)
    moonbit_incref_value_enum_loop(p);
}

#define moonbit_incref_value_enum moonbit_incref_value_enum_inlined

static inline void moonbit_decref_value_enum_inlined(void *p) {
  if (Moonbit_header_layout_class(*(uint32_t *)p) ==
      MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED)
    moonbit_decref_value_enum_loop(p);
}

#define moonbit_decref_value_enum moonbit_decref_value_enum_inlined

#define moonbit_unsafe_make_string moonbit_make_string

#if defined(MOONBIT_V128_NEON)
#define Moonbit_v128_make(lo, hi)                                             \
  vreinterpretq_u8_u64(                                                       \
      vcombine_u64(vcreate_u64((uint64_t)(lo)), vcreate_u64((uint64_t)(hi))))
#define Moonbit_v128_lo(v) vgetq_lane_u64(vreinterpretq_u64_u8(v), 0)
#define Moonbit_v128_hi(v) vgetq_lane_u64(vreinterpretq_u64_u8(v), 1)
#define Moonbit_v128_load_storage(p) vld1q_u8((const uint8_t *)(p))
#define Moonbit_v128_store_storage(p, v) vst1q_u8((uint8_t *)(p), (v))
#elif defined(MOONBIT_V128_SSE2)
#define Moonbit_v128_make(lo, hi) _mm_set_epi64x((int64_t)(hi), (int64_t)(lo))
#define Moonbit_v128_lo(v) ((uint64_t)_mm_cvtsi128_si64(v))
#define Moonbit_v128_hi(v) ((uint64_t)_mm_cvtsi128_si64(_mm_srli_si128((v), 8)))
#define Moonbit_v128_load_storage(p) _mm_loadu_si128((const __m128i *)(p))
#define Moonbit_v128_store_storage(p, v) _mm_storeu_si128((__m128i *)(p), (v))
#else
#define Moonbit_v128_make(lo, hi) ((moonbit_v128_t){(lo), (hi)})
#define Moonbit_v128_lo(v) ((v).lo)
#define Moonbit_v128_hi(v) ((v).hi)
#define Moonbit_v128_load_storage(p) (*(p))
#define Moonbit_v128_store_storage(p, v) (*(p) = (v))
#endif

// detect whether compiler builtins exist for advanced bitwise operations
#ifdef __has_builtin

#if __has_builtin(__builtin_clz)
#define HAS_BUILTIN_CLZ
#endif

#if __has_builtin(__builtin_ctz)
#define HAS_BUILTIN_CTZ
#endif

#if __has_builtin(__builtin_popcount)
#define HAS_BUILTIN_POPCNT
#endif

#if __has_builtin(__builtin_sqrt)
#define HAS_BUILTIN_SQRT
#endif

#if __has_builtin(__builtin_sqrtf)
#define HAS_BUILTIN_SQRTF
#endif

#if __has_builtin(__builtin_fabs)
#define HAS_BUILTIN_FABS
#endif

#if __has_builtin(__builtin_fabsf)
#define HAS_BUILTIN_FABSF
#endif

#endif

// if there is no builtin operators, use software implementation
#ifdef HAS_BUILTIN_CLZ
static inline int32_t moonbit_clz32(int32_t x) {
  return x == 0 ? 32 : __builtin_clz(x);
}

static inline int32_t moonbit_clz64(int64_t x) {
  return x == 0 ? 64 : __builtin_clzll(x);
}

#undef HAS_BUILTIN_CLZ
#else
// table for [clz] value of 4bit integer.
static const uint8_t moonbit_clz4[] = {4, 3, 2, 2, 1, 1, 1, 1,
                                       0, 0, 0, 0, 0, 0, 0, 0};

int32_t moonbit_clz32(uint32_t x) {
  /* The ideas is to:

     1. narrow down the 4bit block where the most signficant "1" bit lies,
        using binary search
     2. find the number of leading zeros in that 4bit block via table lookup

     Different time/space tradeoff can be made here by enlarging the table
     and do less binary search.
     One benefit of the 4bit lookup table is that it can fit into a single cache
     line.
  */
  int32_t result = 0;
  if (x > 0xffff) {
    x >>= 16;
  } else {
    result += 16;
  }
  if (x > 0xff) {
    x >>= 8;
  } else {
    result += 8;
  }
  if (x > 0xf) {
    x >>= 4;
  } else {
    result += 4;
  }
  return result + moonbit_clz4[x];
}

int32_t moonbit_clz64(uint64_t x) {
  int32_t result = 0;
  if (x > 0xffffffff) {
    x >>= 32;
  } else {
    result += 32;
  }
  return result + moonbit_clz32((uint32_t)x);
}
#endif

#ifdef HAS_BUILTIN_CTZ
static inline int32_t moonbit_ctz32(int32_t x) {
  return x == 0 ? 32 : __builtin_ctz(x);
}

static inline int32_t moonbit_ctz64(int64_t x) {
  return x == 0 ? 64 : __builtin_ctzll(x);
}

#undef HAS_BUILTIN_CTZ
#else
int32_t moonbit_ctz32(int32_t x) {
  /* The algorithm comes from:

       Leiserson, Charles E. et al. “Using de Bruijn Sequences to Index a 1 in a
     Computer Word.” (1998).

     The ideas is:

     1. leave only the least significant "1" bit in the input,
        set all other bits to "0". This is achieved via [x & -x]
     2. now we have [x * n == n << ctz(x)], if [n] is a de bruijn sequence
        (every 5bit pattern occurn exactly once when you cycle through the bit
     string), we can find [ctz(x)] from the most significant 5 bits of [x * n]
 */
  static const uint32_t de_bruijn_32 = 0x077CB531;
  static const uint8_t index32[] = {0,  1,  28, 2,  29, 14, 24, 3,  30, 22, 20,
                                    15, 25, 17, 4,  8,  31, 27, 13, 23, 21, 19,
                                    16, 7,  26, 12, 18, 6,  11, 5,  10, 9};
  return (x == 0) * 32 + index32[(de_bruijn_32 * (x & -x)) >> 27];
}

int32_t moonbit_ctz64(int64_t x) {
  static const uint64_t de_bruijn_64 = 0x0218A392CD3D5DBF;
  static const uint8_t index64[] = {
      0,  1,  2,  7,  3,  13, 8,  19, 4,  25, 14, 28, 9,  34, 20, 40,
      5,  17, 26, 38, 15, 46, 29, 48, 10, 31, 35, 54, 21, 50, 41, 57,
      63, 6,  12, 18, 24, 27, 33, 39, 16, 37, 45, 47, 30, 53, 49, 56,
      62, 11, 23, 32, 36, 44, 52, 55, 61, 22, 43, 51, 60, 42, 59, 58};
  return (x == 0) * 64 + index64[(de_bruijn_64 * (x & -x)) >> 58];
}
#endif

#ifdef HAS_BUILTIN_POPCNT

#define moonbit_popcnt32 __builtin_popcount
#define moonbit_popcnt64 __builtin_popcountll
#undef HAS_BUILTIN_POPCNT

#else
int32_t moonbit_popcnt32(uint32_t x) {
  /* The classic SIMD Within A Register algorithm.
     ref: [https://nimrod.blog/posts/algorithms-behind-popcount/]
 */
  x = x - ((x >> 1) & 0x55555555);
  x = (x & 0x33333333) + ((x >> 2) & 0x33333333);
  x = (x + (x >> 4)) & 0x0F0F0F0F;
  return (x * 0x01010101) >> 24;
}

int32_t moonbit_popcnt64(uint64_t x) {
  x = x - ((x >> 1) & 0x5555555555555555);
  x = (x & 0x3333333333333333) + ((x >> 2) & 0x3333333333333333);
  x = (x + (x >> 4)) & 0x0F0F0F0F0F0F0F0F;
  return (x * 0x0101010101010101) >> 56;
}
#endif

/* Full 64x64->128 multiplication. Write directly into the destination value
   struct so the generated code does not depend on a matching runtime struct
   layout. Compilers without native 128-bit integers (e.g. tcc, MSVC) use the
   32x32 schoolbook decomposition. */
static inline void moonbit_umul_wide(uint64_t *out_lo, uint64_t *out_hi,
                                     uint64_t a, uint64_t b) {
#if defined(__SIZEOF_INT128__)
  unsigned __int128 r = (unsigned __int128)a * (unsigned __int128)b;
  *out_lo = (uint64_t)r;
  *out_hi = (uint64_t)(r >> 64);
#else
  uint64_t alo = a & 0xffffffff, ahi = a >> 32;
  uint64_t blo = b & 0xffffffff, bhi = b >> 32;
  uint64_t ll = alo * blo;
  uint64_t lh = alo * bhi;
  uint64_t hl = ahi * blo;
  uint64_t hh = ahi * bhi;
  uint64_t mid = (ll >> 32) + (lh & 0xffffffff) + (hl & 0xffffffff);
  *out_lo = a * b;
  *out_hi = hh + (lh >> 32) + (hl >> 32) + (mid >> 32);
#endif
}

static inline void moonbit_smul_wide(uint64_t *out_lo, int64_t *out_hi,
                                     int64_t a, int64_t b) {
#if defined(__SIZEOF_INT128__)
  __int128 r = (__int128)a * (__int128)b;
  *out_lo = (uint64_t)r;
  *out_hi = (int64_t)(r >> 64);
#else
  uint64_t hi;
  moonbit_umul_wide(out_lo, &hi, (uint64_t)a, (uint64_t)b);
  uint64_t correction_a = a < 0 ? (uint64_t)b : 0;
  uint64_t correction_b = b < 0 ? (uint64_t)a : 0;
  *out_hi = (int64_t)(hi - correction_a - correction_b);
#endif
}

/* The following sqrt implementation comes from
   [musl](https://git.musl-libc.org/cgit/musl),
   with some helpers inlined to make it zero dependency.
 */
#ifdef MOONBIT_NATIVE_NO_SYS_HEADER
const uint16_t __rsqrt_tab[128] = {
    0xb451, 0xb2f0, 0xb196, 0xb044, 0xaef9, 0xadb6, 0xac79, 0xab43, 0xaa14,
    0xa8eb, 0xa7c8, 0xa6aa, 0xa592, 0xa480, 0xa373, 0xa26b, 0xa168, 0xa06a,
    0x9f70, 0x9e7b, 0x9d8a, 0x9c9d, 0x9bb5, 0x9ad1, 0x99f0, 0x9913, 0x983a,
    0x9765, 0x9693, 0x95c4, 0x94f8, 0x9430, 0x936b, 0x92a9, 0x91ea, 0x912e,
    0x9075, 0x8fbe, 0x8f0a, 0x8e59, 0x8daa, 0x8cfe, 0x8c54, 0x8bac, 0x8b07,
    0x8a64, 0x89c4, 0x8925, 0x8889, 0x87ee, 0x8756, 0x86c0, 0x862b, 0x8599,
    0x8508, 0x8479, 0x83ec, 0x8361, 0x82d8, 0x8250, 0x81c9, 0x8145, 0x80c2,
    0x8040, 0xff02, 0xfd0e, 0xfb25, 0xf947, 0xf773, 0xf5aa, 0xf3ea, 0xf234,
    0xf087, 0xeee3, 0xed47, 0xebb3, 0xea27, 0xe8a3, 0xe727, 0xe5b2, 0xe443,
    0xe2dc, 0xe17a, 0xe020, 0xdecb, 0xdd7d, 0xdc34, 0xdaf1, 0xd9b3, 0xd87b,
    0xd748, 0xd61a, 0xd4f1, 0xd3cd, 0xd2ad, 0xd192, 0xd07b, 0xcf69, 0xce5b,
    0xcd51, 0xcc4a, 0xcb48, 0xca4a, 0xc94f, 0xc858, 0xc764, 0xc674, 0xc587,
    0xc49d, 0xc3b7, 0xc2d4, 0xc1f4, 0xc116, 0xc03c, 0xbf65, 0xbe90, 0xbdbe,
    0xbcef, 0xbc23, 0xbb59, 0xba91, 0xb9cc, 0xb90a, 0xb84a, 0xb78c, 0xb6d0,
    0xb617, 0xb560,
};

/* returns a*b*2^-32 - e, with error 0 <= e < 1.  */
static inline uint32_t mul32(uint32_t a, uint32_t b) {
  return (uint64_t)a * b >> 32;
}
#endif

#ifdef MOONBIT_NATIVE_NO_SYS_HEADER
float sqrtf(float x) {
  uint32_t ix, m, m1, m0, even, ey;

  ix = *(uint32_t *)&x;
  if (ix - 0x00800000 >= 0x7f800000 - 0x00800000) {
    /* x < 0x1p-126 or inf or nan.  */
    if (ix * 2 == 0)
      return x;
    if (ix == 0x7f800000)
      return x;
    if (ix > 0x7f800000)
      return (x - x) / (x - x);
    /* x is subnormal, normalize it.  */
    x *= 0x1p23f;
    ix = *(uint32_t *)&x;
    ix -= 23 << 23;
  }

  /* x = 4^e m; with int e and m in [1, 4).  */
  even = ix & 0x00800000;
  m1 = (ix << 8) | 0x80000000;
  m0 = (ix << 7) & 0x7fffffff;
  m = even ? m0 : m1;

  /* 2^e is the exponent part of the return value.  */
  ey = ix >> 1;
  ey += 0x3f800000 >> 1;
  ey &= 0x7f800000;

  /* compute r ~ 1/sqrt(m), s ~ sqrt(m) with 2 goldschmidt iterations.  */
  static const uint32_t three = 0xc0000000;
  uint32_t r, s, d, u, i;
  i = (ix >> 17) % 128;
  r = (uint32_t)__rsqrt_tab[i] << 16;
  /* |r*sqrt(m) - 1| < 0x1p-8 */
  s = mul32(m, r);
  /* |s/sqrt(m) - 1| < 0x1p-8 */
  d = mul32(s, r);
  u = three - d;
  r = mul32(r, u) << 1;
  /* |r*sqrt(m) - 1| < 0x1.7bp-16 */
  s = mul32(s, u) << 1;
  /* |s/sqrt(m) - 1| < 0x1.7bp-16 */
  d = mul32(s, r);
  u = three - d;
  s = mul32(s, u);
  /* -0x1.03p-28 < s/sqrt(m) - 1 < 0x1.fp-31 */
  s = (s - 1) >> 6;
  /* s < sqrt(m) < s + 0x1.08p-23 */

  /* compute nearest rounded result.  */
  uint32_t d0, d1, d2;
  float y, t;
  d0 = (m << 16) - s * s;
  d1 = s - d0;
  d2 = d1 + s + 1;
  s += d1 >> 31;
  s &= 0x007fffff;
  s |= ey;
  y = *(float *)&s;
  /* handle rounding and inexact exception. */
  uint32_t tiny = d2 == 0 ? 0 : 0x01000000;
  tiny |= (d1 ^ d2) & 0x80000000;
  t = *(float *)&tiny;
  y = y + t;
  return y;
}
#endif

#ifdef MOONBIT_NATIVE_NO_SYS_HEADER
/* returns a*b*2^-64 - e, with error 0 <= e < 3.  */
static inline uint64_t mul64(uint64_t a, uint64_t b) {
  uint64_t ahi = a >> 32;
  uint64_t alo = a & 0xffffffff;
  uint64_t bhi = b >> 32;
  uint64_t blo = b & 0xffffffff;
  return ahi * bhi + (ahi * blo >> 32) + (alo * bhi >> 32);
}

double sqrt(double x) {
  uint64_t ix, top, m;

  /* special case handling.  */
  ix = *(uint64_t *)&x;
  top = ix >> 52;
  if (top - 0x001 >= 0x7ff - 0x001) {
    /* x < 0x1p-1022 or inf or nan.  */
    if (ix * 2 == 0)
      return x;
    if (ix == 0x7ff0000000000000)
      return x;
    if (ix > 0x7ff0000000000000)
      return (x - x) / (x - x);
    /* x is subnormal, normalize it.  */
    x *= 0x1p52;
    ix = *(uint64_t *)&x;
    top = ix >> 52;
    top -= 52;
  }

  /* argument reduction:
     x = 4^e m; with integer e, and m in [1, 4)
     m: fixed point representation [2.62]
     2^e is the exponent part of the result.  */
  int even = top & 1;
  m = (ix << 11) | 0x8000000000000000;
  if (even)
    m >>= 1;
  top = (top + 0x3ff) >> 1;

  /* approximate r ~ 1/sqrt(m) and s ~ sqrt(m) when m in [1,4)

     initial estimate:
     7bit table lookup (1bit exponent and 6bit significand).

     iterative approximation:
     using 2 goldschmidt iterations with 32bit int arithmetics
     and a final iteration with 64bit int arithmetics.

     details:

     the relative error (e = r0 sqrt(m)-1) of a linear estimate
     (r0 = a m + b) is |e| < 0.085955 ~ 0x1.6p-4 at best,
     a table lookup is faster and needs one less iteration
     6 bit lookup table (128b) gives |e| < 0x1.f9p-8
     7 bit lookup table (256b) gives |e| < 0x1.fdp-9
     for single and double prec 6bit is enough but for quad
     prec 7bit is needed (or modified iterations). to avoid
     one more iteration >=13bit table would be needed (16k).

     a newton-raphson iteration for r is
       w = r*r
       u = 3 - m*w
       r = r*u/2
     can use a goldschmidt iteration for s at the end or
       s = m*r

     first goldschmidt iteration is
       s = m*r
       u = 3 - s*r
       r = r*u/2
       s = s*u/2
     next goldschmidt iteration is
       u = 3 - s*r
       r = r*u/2
       s = s*u/2
     and at the end r is not computed only s.

     they use the same amount of operations and converge at the
     same quadratic rate, i.e. if
       r1 sqrt(m) - 1 = e, then
       r2 sqrt(m) - 1 = -3/2 e^2 - 1/2 e^3
     the advantage of goldschmidt is that the mul for s and r
     are independent (computed in parallel), however it is not
     "self synchronizing": it only uses the input m in the
     first iteration so rounding errors accumulate. at the end
     or when switching to larger precision arithmetics rounding
     errors dominate so the first iteration should be used.

     the fixed point representations are
       m: 2.30 r: 0.32, s: 2.30, d: 2.30, u: 2.30, three: 2.30
     and after switching to 64 bit
       m: 2.62 r: 0.64, s: 2.62, d: 2.62, u: 2.62, three: 2.62  */

  static const uint64_t three = 0xc0000000;
  uint64_t r, s, d, u, i;

  i = (ix >> 46) % 128;
  r = (uint32_t)__rsqrt_tab[i] << 16;
  /* |r sqrt(m) - 1| < 0x1.fdp-9 */
  s = mul32(m >> 32, r);
  /* |s/sqrt(m) - 1| < 0x1.fdp-9 */
  d = mul32(s, r);
  u = three - d;
  r = mul32(r, u) << 1;
  /* |r sqrt(m) - 1| < 0x1.7bp-16 */
  s = mul32(s, u) << 1;
  /* |s/sqrt(m) - 1| < 0x1.7bp-16 */
  d = mul32(s, r);
  u = three - d;
  r = mul32(r, u) << 1;
  /* |r sqrt(m) - 1| < 0x1.3704p-29 (measured worst-case) */
  r = r << 32;
  s = mul64(m, r);
  d = mul64(s, r);
  u = (three << 32) - d;
  s = mul64(s, u); /* repr: 3.61 */
  /* -0x1p-57 < s - sqrt(m) < 0x1.8001p-61 */
  s = (s - 2) >> 9; /* repr: 12.52 */
  /* -0x1.09p-52 < s - sqrt(m) < -0x1.fffcp-63 */

  /* s < sqrt(m) < s + 0x1.09p-52,
     compute nearest rounded result:
     the nearest result to 52 bits is either s or s+0x1p-52,
     we can decide by comparing (2^52 s + 0.5)^2 to 2^104 m.  */
  uint64_t d0, d1, d2;
  double y, t;
  d0 = (m << 42) - s * s;
  d1 = s - d0;
  d2 = d1 + s + 1;
  s += d1 >> 63;
  s &= 0x000fffffffffffff;
  s |= top << 52;
  y = *(double *)&s;
  return y;
}
#endif

#ifdef MOONBIT_NATIVE_NO_SYS_HEADER
double fabs(double x) {
  union {
    double f;
    uint64_t i;
  } u = {x};
  u.i &= 0x7fffffffffffffffULL;
  return u.f;
}
#endif

#ifdef MOONBIT_NATIVE_NO_SYS_HEADER
float fabsf(float x) {
  union {
    float f;
    uint32_t i;
  } u = {x};
  u.i &= 0x7fffffff;
  return u.f;
}
#endif

#ifdef _MSC_VER
/* MSVC treats syntactic division by zero as fatal error,
   even for float point numbers,
   so we have to use a constant variable to work around this */
static const int MOONBIT_ZERO = 0;
#else
#define MOONBIT_ZERO 0
#endif

#ifdef __cplusplus
}
#endif
struct _M0TPB8MutLocalGiE;

struct _M0TP26RiantR8snn__mbt14SpikingSynapse;

struct _M0TPB5ArrayGRP26RiantR8snn__mbt7MonitorE;

struct _M0TPB4Show;

struct _M0TPB8MutLocalGfE;

struct _M0TPB5ArrayGRP26RiantR8snn__mbt14SpikingSynapseE;

struct _M0TPB13StringBuilder;

struct _M0TPB5ArrayGfE;

struct _M0TP26RiantR8snn__mbt9PostSpike;

struct _M0TPB17FloatingDecimal64;

struct _M0TPB5ArrayGbE;

struct _M0BTPB6Logger;

struct _M0BTPB4Show;

struct _M0TP26RiantR8snn__mbt7Monitor;

struct _M0TPC16string10StringView;

struct _M0TP26RiantR8snn__mbt2IF;

struct _M0TPB8MutLocalGbE;

struct _M0TPB5ArrayGRP26RiantR8snn__mbt2IFE;

struct _M0KTPB6LoggerTPB13StringBuilder;

struct _M0TPB6Logger;

struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR;

struct _M0TP26RiantR8snn__mbt5Model;

struct _M0TP26RiantR8snn__mbt7Xoshiro;

struct _M0TUmmmmE;

struct _M0DTPC16option6OptionGfE4Some;

struct _M0TP26RiantR8snn__mbt11IFParameter;

struct _M0TPB5ArrayGiE;

struct _M0TPB19MulShiftAll64Result;

struct _M0TP26RiantR8snn__mbt4Time;

struct _M0TPB7Umul128;

struct _M0TPB8Pow5Pair;

struct _M0TPB8MutLocalGiE {
  int32_t $0;
  
};

struct _M0TP26RiantR8snn__mbt14SpikingSynapse {
  struct _M0TP26RiantR8snn__mbt2IF* $0;
  struct _M0TP26RiantR8snn__mbt2IF* $1;
  moonbit_string_t $2;
  moonbit_string_t $3;
  struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* $4;
  struct _M0TPB5ArrayGfE* $5;
  struct _M0TPB5ArrayGfE* $6;
  struct _M0TPB5ArrayGfE* $7;
  struct _M0TPB5ArrayGiE* $8;
  struct _M0TPB5ArrayGfE* $9;
  
};

struct _M0TPB5ArrayGRP26RiantR8snn__mbt7MonitorE {
  struct _M0TP26RiantR8snn__mbt7Monitor** $0;
  int32_t $1;
  
};

struct _M0TPB4Show {
  struct _M0BTPB4Show* $0;
  void* $1;
  
};

struct _M0TPB8MutLocalGfE {
  float $0;
  
};

struct _M0TPB5ArrayGRP26RiantR8snn__mbt14SpikingSynapseE {
  struct _M0TP26RiantR8snn__mbt14SpikingSynapse** $0;
  int32_t $1;
  
};

struct _M0TPB13StringBuilder {
  uint16_t* $0;
  int32_t $1;
  
};

struct _M0TPB5ArrayGfE {
  float* $0;
  int32_t $1;
  
};

struct _M0TP26RiantR8snn__mbt9PostSpike {
  float $0;
  
};

struct _M0TPB17FloatingDecimal64 {
  uint64_t $0;
  int32_t $1;
  
};

struct _M0TPB5ArrayGbE {
  uint8_t* $0;
  int32_t $1;
  
};

struct _M0BTPB6Logger {
  int32_t(* $method_0)(void*, moonbit_string_t);
  int32_t(* $method_1)(void*, moonbit_string_t, int32_t, int32_t);
  int32_t(* $method_2)(void*, struct _M0TPC16string10StringView);
  int32_t(* $method_3)(void*, int32_t);
  int32_t(* $method_4)(void*, struct _M0TPB4Show);
  int32_t(* $method_5)(void*, struct _M0TPB4Show);
  
};

struct _M0BTPB4Show {
  int32_t(* $method_0)(void*, struct _M0TPB6Logger);
  moonbit_string_t(* $method_1)(void*);
  
};

struct _M0TP26RiantR8snn__mbt7Monitor {
  struct _M0TP26RiantR8snn__mbt2IF* $0;
  moonbit_string_t $1;
  struct _M0TPB5ArrayGfE* $2;
  struct _M0TPB5ArrayGfE* $3;
  int32_t $4;
  int32_t $5;
  int32_t $6;
  
};

struct _M0TPC16string10StringView {
  moonbit_string_t $0;
  int32_t $1;
  int32_t $2;
  
};

struct _M0TP26RiantR8snn__mbt2IF {
  struct _M0TP26RiantR8snn__mbt11IFParameter* $0;
  struct _M0TP26RiantR8snn__mbt9PostSpike* $1;
  int32_t $2;
  struct _M0TPB5ArrayGfE* $3;
  struct _M0TPB5ArrayGfE* $4;
  struct _M0TPB5ArrayGbE* $5;
  struct _M0TPB5ArrayGiE* $6;
  struct _M0TPB5ArrayGfE* $7;
  struct _M0TPB5ArrayGfE* $8;
  struct _M0TPB5ArrayGfE* $9;
  struct _M0TPB5ArrayGfE* $10;
  struct _M0TPB5ArrayGfE* $11;
  struct _M0TPB5ArrayGfE* $12;
  struct _M0TPB5ArrayGfE* $13;
  struct _M0TPB5ArrayGfE* $14;
  struct _M0TPB5ArrayGfE* $15;
  struct _M0TPB5ArrayGfE* $16;
  float $17;
  float $18;
  float $19;
  float $20;
  float $21;
  float $22;
  
};

struct _M0TPB8MutLocalGbE {
  int32_t $0;
  
};

struct _M0TPB5ArrayGRP26RiantR8snn__mbt2IFE {
  struct _M0TP26RiantR8snn__mbt2IF** $0;
  int32_t $1;
  
};

struct _M0KTPB6LoggerTPB13StringBuilder {
  struct _M0BTPB6Logger* $0;
  void* $1;
  
};

struct _M0TPB6Logger {
  struct _M0BTPB6Logger* $0;
  void* $1;
  
};

struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR {
  int32_t $0;
  int32_t $1;
  struct _M0TPB5ArrayGiE* $2;
  struct _M0TPB5ArrayGiE* $3;
  struct _M0TPB5ArrayGfE* $4;
  
};

struct _M0TP26RiantR8snn__mbt5Model {
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt2IFE* $0;
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt14SpikingSynapseE* $1;
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt7MonitorE* $2;
  
};

struct _M0TP26RiantR8snn__mbt7Xoshiro {
  uint64_t $0;
  uint64_t $1;
  uint64_t $2;
  uint64_t $3;
  
};

struct _M0TUmmmmE {
  uint64_t $0;
  uint64_t $1;
  uint64_t $2;
  uint64_t $3;
  
};

struct _M0DTPC16option6OptionGfE4Some {
  float $0;
  
};

struct _M0TP26RiantR8snn__mbt11IFParameter {
  float $0;
  float $1;
  float $2;
  float $3;
  float $4;
  float $5;
  float $6;
  float $7;
  float $8;
  float $9;
  float $10;
  
};

struct _M0TPB5ArrayGiE {
  int32_t* $0;
  int32_t $1;
  
};

struct _M0TPB19MulShiftAll64Result {
  uint64_t $0;
  uint64_t $1;
  uint64_t $2;
  
};

struct _M0TP26RiantR8snn__mbt4Time {
  struct _M0TPB5ArrayGfE* $0;
  struct _M0TPB5ArrayGiE* $1;
  float $2;
  
};

struct _M0TPB7Umul128 {
  uint64_t $0;
  uint64_t $1;
  
};

struct _M0TPB8Pow5Pair {
  uint64_t $0;
  uint64_t $1;
  
};

int32_t _M0FP26RiantR8snn__mbt16spiking__connect(
  struct _M0TP26RiantR8snn__mbt14SpikingSynapse*,
  int32_t,
  int32_t,
  float
);

struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0MP26RiantR8snn__mbt14SpikingSynapse3new(
  struct _M0TP26RiantR8snn__mbt2IF*,
  struct _M0TP26RiantR8snn__mbt2IF*,
  moonbit_string_t
);

struct _M0TP26RiantR8snn__mbt11IFParameter* _M0MP26RiantR8snn__mbt11IFParameter3new(
  
);

struct _M0TP26RiantR8snn__mbt2IF* _M0MP26RiantR8snn__mbt2IF3new(
  int32_t,
  struct _M0TP26RiantR8snn__mbt11IFParameter*,
  struct _M0TP26RiantR8snn__mbt7Xoshiro*
);

struct _M0TP26RiantR8snn__mbt9PostSpike* _M0MP26RiantR8snn__mbt9PostSpike3new(
  
);

struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0MP26RiantR8snn__mbt7Xoshiro7default(
  
);

int32_t _M0FP26RiantR8snn__mbt8sim__for(
  struct _M0TP26RiantR8snn__mbt5Model*,
  float
);

int32_t _M0FP26RiantR8snn__mbt11step__model(
  struct _M0TP26RiantR8snn__mbt5Model*,
  struct _M0TP26RiantR8snn__mbt4Time*
);

int32_t _M0FP26RiantR8snn__mbt16forward__synapse(
  struct _M0TP26RiantR8snn__mbt14SpikingSynapse*,
  float
);

int32_t _M0FP26RiantR8snn__mbt25deliver__pending__synapse(
  struct _M0TP26RiantR8snn__mbt14SpikingSynapse*,
  float
);

int32_t _M0FP26RiantR8snn__mbt13apply__weight(
  struct _M0TP26RiantR8snn__mbt14SpikingSynapse*,
  int32_t,
  float
);

int32_t _M0FP26RiantR8snn__mbt12record__zero(
  struct _M0TP26RiantR8snn__mbt5Model*
);

int32_t _M0FP26RiantR8snn__mbt11record__one(
  struct _M0TP26RiantR8snn__mbt7Monitor*,
  float
);

struct _M0TP26RiantR8snn__mbt7Monitor* _M0MP26RiantR8snn__mbt7Monitor6new__v(
  struct _M0TP26RiantR8snn__mbt2IF*,
  int32_t
);

int32_t _M0FP26RiantR8snn__mbt17synaptic__current(
  struct _M0TP26RiantR8snn__mbt2IF*
);

int32_t _M0FP26RiantR8snn__mbt14step__synapses(
  struct _M0TP26RiantR8snn__mbt2IF*,
  float
);

int32_t _M0FP26RiantR8snn__mbt12step__neuron(
  struct _M0TP26RiantR8snn__mbt2IF*,
  float
);

int32_t _M0MP26RiantR8snn__mbt15SparseMatrixCSR7forward(
  struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR*,
  struct _M0TPB5ArrayGbE*,
  struct _M0TPB5ArrayGfE*
);

int32_t _M0MP26RiantR8snn__mbt15SparseMatrixCSR3set(
  struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR*,
  int32_t,
  int32_t,
  float
);

struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0MP26RiantR8snn__mbt15SparseMatrixCSR5empty(
  int32_t,
  int32_t
);

struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0MP26RiantR8snn__mbt7Xoshiro3new(
  uint64_t
);

struct _M0TUmmmmE* _M0FP26RiantR8snn__mbt10splitmix64(uint64_t);

uint64_t _M0FP26RiantR8snn__mbt5mix64(uint64_t);

int32_t _M0FP26RiantR8snn__mbt12update__time(
  struct _M0TP26RiantR8snn__mbt4Time*,
  float
);

int32_t _M0FP26RiantR8snn__mbt7set__dt(
  struct _M0TP26RiantR8snn__mbt4Time*,
  float
);

float _M0FP26RiantR8snn__mbt9get__time(struct _M0TP26RiantR8snn__mbt4Time*);

struct _M0TP26RiantR8snn__mbt4Time* _M0MP26RiantR8snn__mbt4Time3new();

float _M0FP26RiantR8snn__mbt9next__f32(
  struct _M0TP26RiantR8snn__mbt7Xoshiro*
);

uint32_t _M0FP26RiantR8snn__mbt9next__u32(
  struct _M0TP26RiantR8snn__mbt7Xoshiro*
);

uint64_t _M0FP26RiantR8snn__mbt9next__u64(
  struct _M0TP26RiantR8snn__mbt7Xoshiro*
);

uint64_t _M0FP26RiantR8snn__mbt4rotl(uint64_t, int32_t);

int32_t _M0MP26RiantR8snn__mbt7Monitor13dump__summary(
  struct _M0TP26RiantR8snn__mbt7Monitor*
);

moonbit_string_t _M0IPC15float5FloatPB4Show10to__string(float);

int32_t _M0MPC15float5Float7to__int(float);

struct _M0TPB5ArrayGfE* _M0MPC15array5Array4makeGfE(int32_t, float);

struct _M0TPB5ArrayGbE* _M0MPC15array5Array4makeGbE(int32_t, int32_t);

struct _M0TPB5ArrayGiE* _M0MPC15array5Array4makeGiE(int32_t, int32_t);

int32_t _M0MPC15array5Array3setGfE(struct _M0TPB5ArrayGfE*, int32_t, float);

int32_t _M0MPC15array5Array3setGiE(struct _M0TPB5ArrayGiE*, int32_t, int32_t);

int32_t _M0MPC15array5Array3setGbE(struct _M0TPB5ArrayGbE*, int32_t, int32_t);

int32_t _M0MPC15array5Array6insertGiE(
  struct _M0TPB5ArrayGiE*,
  int32_t,
  int32_t
);

int32_t _M0MPC15array5Array6insertGfE(
  struct _M0TPB5ArrayGfE*,
  int32_t,
  float
);

void* _M0MPC15array5Array3popGfE(struct _M0TPB5ArrayGfE*);

int64_t _M0MPC15array5Array3popGiE(struct _M0TPB5ArrayGiE*);

float _M0MPC15array5Array2atGfE(struct _M0TPB5ArrayGfE*, int32_t);

struct _M0TP26RiantR8snn__mbt7Monitor* _M0MPC15array5Array2atGRP26RiantR8snn__mbt7MonitorE(
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt7MonitorE*,
  int32_t
);

int32_t _M0MPC15array5Array2atGiE(struct _M0TPB5ArrayGiE*, int32_t);

int32_t _M0MPC15array5Array2atGbE(struct _M0TPB5ArrayGbE*, int32_t);

int32_t _M0FPB7printlnGsE(moonbit_string_t);

moonbit_string_t _M0MPC16double6Double10to__string(double);

moonbit_string_t _M0FPB15ryu__to__string(double);

struct _M0TPB17FloatingDecimal64* _M0FPB15d2d__small__int(uint64_t, int32_t);

moonbit_string_t _M0FPB9to__chars(struct _M0TPB17FloatingDecimal64*, int32_t);

struct _M0TPB17FloatingDecimal64* _M0FPB3d2d(uint64_t, uint32_t);

uint64_t _M0MPC14bool4Bool10to__uint64(int32_t);

int64_t _M0MPC14bool4Bool9to__int64(int32_t);

int32_t _M0MPC14bool4Bool7to__int(int32_t);

int32_t _M0FPB17decimal__length17(uint64_t);

struct _M0TPB8Pow5Pair _M0FPB22double__computeInvPow5(int32_t);

struct _M0TPB8Pow5Pair _M0FPB19double__computePow5(int32_t);

struct _M0TPB19MulShiftAll64Result _M0FPB13mulShiftAll64(
  uint64_t,
  struct _M0TPB8Pow5Pair,
  int32_t,
  int32_t
);

int32_t _M0FPB18multipleOfPowerOf2(uint64_t, int32_t);

int32_t _M0FPB18multipleOfPowerOf5(uint64_t, int32_t);

int32_t _M0FPB10pow5Factor(uint64_t);

uint64_t _M0FPB13shiftright128(uint64_t, uint64_t, int32_t);

struct _M0TPB7Umul128 _M0FPB7umul128(uint64_t, uint64_t);

moonbit_string_t _M0FPB19string__from__bytes(
  moonbit_bytes_t,
  int32_t,
  int32_t
);

int32_t _M0FPB9log10Pow2(int32_t);

int32_t _M0FPB9log10Pow5(int32_t);

moonbit_string_t _M0FPB18copy__special__str(int32_t, int32_t, int32_t);

int32_t _M0FPB8pow5bits(int32_t);

int32_t _M0MPC16double6Double7to__int(double);

int64_t _M0MPC16double6Double9to__int64(double);

struct _M0TPB5ArrayGfE* _M0MPC15array5Array20unsafe__make__uninitGfE(int32_t);

struct _M0TPB5ArrayGbE* _M0MPC15array5Array20unsafe__make__uninitGbE(int32_t);

struct _M0TPB5ArrayGiE* _M0MPC15array5Array20unsafe__make__uninitGiE(int32_t);

uint64_t _M0MPC15array13ReadOnlyArray2atGmE(uint64_t*, int32_t);

uint32_t _M0MPC15array13ReadOnlyArray2atGjE(uint32_t*, int32_t);

moonbit_string_t _M0IPC16uint646UInt64PB4Show10to__string(uint64_t);

moonbit_string_t _M0IPC13int3IntPB4Show10to__string(int32_t);

uint64_t _M0MPC14uint4UInt10to__uint64(uint32_t);

int32_t _M0MPC15array5Array4pushGfE(struct _M0TPB5ArrayGfE*, float);

int32_t _M0MPC15array5Array4pushGiE(struct _M0TPB5ArrayGiE*, int32_t);

int32_t _M0MPC15array5Array7reallocGiE(struct _M0TPB5ArrayGiE*, int32_t);

int32_t _M0MPC15array5Array7reallocGfE(struct _M0TPB5ArrayGfE*, int32_t);

int32_t _M0MPC15array5Array14resize__bufferGiE(
  struct _M0TPB5ArrayGiE*,
  int32_t
);

int32_t _M0MPC15array5Array14resize__bufferGfE(
  struct _M0TPB5ArrayGfE*,
  int32_t
);

int32_t _M0MPC15array5Array8capacityGiE(struct _M0TPB5ArrayGiE*);

int32_t _M0MPC15array5Array8capacityGfE(struct _M0TPB5ArrayGfE*);

int32_t _M0FPB23array__growth__capacity(int32_t, int32_t, int32_t);

int32_t _M0MPC15array5Array6lengthGfE(struct _M0TPB5ArrayGfE*);

int32_t _M0MPC15array5Array6lengthGbE(struct _M0TPB5ArrayGbE*);

float* _M0MPC15array5Array6bufferGfE(struct _M0TPB5ArrayGfE*);

struct _M0TP26RiantR8snn__mbt7Monitor** _M0MPC15array5Array6bufferGRP26RiantR8snn__mbt7MonitorE(
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt7MonitorE*
);

int32_t* _M0MPC15array5Array6bufferGiE(struct _M0TPB5ArrayGiE*);

uint8_t* _M0MPC15array5Array6bufferGbE(struct _M0TPB5ArrayGbE*);

moonbit_string_t _M0IPC16string6StringPB4Show10to__string(moonbit_string_t);

int32_t _M0IPB13StringBuilderPB6Logger11write__view(
  struct _M0TPB13StringBuilder*,
  struct _M0TPC16string10StringView
);

moonbit_string_t _M0MPC16uint646UInt6418to__string_2einner(uint64_t, int32_t);

moonbit_string_t _M0MPC15int645Int6418to__string_2einner(int64_t, int32_t);

int32_t _M0FPB22int64__to__string__dec(uint16_t*, uint64_t, int32_t, int32_t);

int32_t _M0FPB26int64__to__string__generic(
  uint16_t*,
  uint64_t,
  int32_t,
  int32_t,
  int32_t
);

int32_t _M0FPB22int64__to__string__hex(uint16_t*, uint64_t, int32_t, int32_t);

int32_t _M0FPB14radix__count64(uint64_t, int32_t);

int32_t _M0FPB12hex__count64(uint64_t);

int32_t _M0FPB12dec__count64(uint64_t);

moonbit_string_t _M0MPC13int3Int18to__string_2einner(int32_t, int32_t);

int32_t _M0FPB14radix__count32(uint32_t, int32_t);

int32_t _M0FPB12hex__count32(uint32_t);

int32_t _M0FPB12dec__count32(uint32_t);

int32_t _M0FPB20int__to__string__dec(uint16_t*, uint32_t, int32_t, int32_t);

int32_t _M0FPB24int__to__string__generic(
  uint16_t*,
  uint32_t,
  int32_t,
  int32_t,
  int32_t
);

int32_t _M0FPB20int__to__string__hex(uint16_t*, uint32_t, int32_t, int32_t);

int32_t _M0IP016_24default__implPB4Show6outputGsE(
  moonbit_string_t,
  struct _M0TPB6Logger
);

int32_t _M0IP016_24default__implPB4Show6outputGiE(
  int32_t,
  struct _M0TPB6Logger
);

int32_t _M0IP016_24default__implPB4Show6outputGmE(
  uint64_t,
  struct _M0TPB6Logger
);

int32_t _M0MPC16string10StringView13start__offset(
  struct _M0TPC16string10StringView
);

moonbit_string_t _M0MPC16string10StringView4data(
  struct _M0TPC16string10StringView
);

int32_t _M0IP016_24default__implPB6Logger16write__substringGRPB13StringBuilderE(
  struct _M0TPB13StringBuilder*,
  moonbit_string_t,
  int32_t,
  int32_t
);

struct _M0TPC16string10StringView _M0MPC16string6String11sub_2einner(
  moonbit_string_t,
  int32_t,
  int64_t
);

int32_t _M0IP016_24default__implPB6Logger5writeGRPB13StringBuilderE(
  struct _M0TPB13StringBuilder*,
  struct _M0TPB4Show
);

int32_t _M0IP016_24default__implPB6Logger28write__string__interpolationGRPB13StringBuilderE(
  struct _M0TPB13StringBuilder*,
  struct _M0TPB4Show
);

uint64_t _M0MPC13int3Int10to__uint64(int32_t);

int32_t _M0IPC16uint166UInt16PB7Default7default();

int32_t _M0MPC16uint166UInt1623is__trailing__surrogate(int32_t);

int32_t _M0IPB13StringBuilderPB6Logger13write__string(
  struct _M0TPB13StringBuilder*,
  moonbit_string_t
);

int32_t _M0MPC15array10FixedArray26unsafe__blit__from__string(
  uint16_t*,
  int32_t,
  moonbit_string_t,
  int32_t,
  int32_t
);

int32_t _M0IPB13StringBuilderPB6Logger11write__char(
  struct _M0TPB13StringBuilder*,
  int32_t
);

int32_t _M0MPB13StringBuilder4grow(struct _M0TPB13StringBuilder*, int32_t);

int32_t _M0FPB31stringbuilder__growth__capacity(int32_t, int32_t, int32_t);

int32_t _M0MPC14uint4UInt10to__uint16(uint32_t);

uint32_t _M0MPC14char4Char8to__uint(int32_t);

moonbit_string_t _M0MPB13StringBuilder10to__string(
  struct _M0TPB13StringBuilder*
);

uint16_t* _M0MPC15array10FixedArray23make__and__blit_2einnerGkE(
  uint16_t*,
  int32_t,
  int32_t,
  int32_t,
  int32_t,
  int32_t
);

uint16_t* _M0MPC15array10FixedArray23unsafe__make__and__blitGkE(
  uint16_t*,
  int32_t,
  int32_t,
  int32_t,
  int32_t,
  int32_t
);

struct _M0TPB13StringBuilder* _M0MPB13StringBuilder21StringBuilder_2einner(
  int32_t
);

int32_t* _M0MPB18UninitializedArray23make__and__blit_2einnerGiE(
  int32_t*,
  int32_t,
  int32_t,
  int32_t,
  int32_t
);

float* _M0MPB18UninitializedArray23make__and__blit_2einnerGfE(
  float*,
  int32_t,
  int32_t,
  int32_t,
  int32_t
);

int32_t _M0MPB13StringBuilder13write__objectGsE(
  struct _M0TPB13StringBuilder*,
  moonbit_string_t
);

int32_t _M0MPB13StringBuilder13write__objectGiE(
  struct _M0TPB13StringBuilder*,
  int32_t
);

int32_t _M0MPB13StringBuilder13write__objectGmE(
  struct _M0TPB13StringBuilder*,
  uint64_t
);

int32_t* _M0MPB18UninitializedArray23unsafe__make__and__blitGiE(
  int32_t*,
  int32_t,
  int32_t,
  int32_t,
  int32_t
);

float* _M0MPB18UninitializedArray23unsafe__make__and__blitGfE(
  float*,
  int32_t,
  int32_t,
  int32_t,
  int32_t
);

int32_t _M0MPB18UninitializedArray12unsafe__blitGiE(
  int32_t*,
  int32_t,
  int32_t*,
  int32_t,
  int32_t
);

int32_t _M0MPB18UninitializedArray12unsafe__blitGfE(
  float*,
  int32_t,
  float*,
  int32_t,
  int32_t
);

int32_t _M0MPC15array10FixedArray12unsafe__blitGRPB17UnsafeMaybeUninitGiEE(
  int32_t*,
  int32_t,
  int32_t*,
  int32_t,
  int32_t
);

int32_t _M0MPC15array10FixedArray12unsafe__blitGRPB17UnsafeMaybeUninitGfEE(
  float*,
  int32_t,
  float*,
  int32_t,
  int32_t
);

int32_t _M0MPC15array10FixedArray12unsafe__blitGkE(
  uint16_t*,
  int32_t,
  uint16_t*,
  int32_t,
  int32_t
);

int32_t _M0MPB18UninitializedArray6lengthGiE(int32_t*);

int32_t _M0MPB18UninitializedArray6lengthGfE(float*);

int32_t _M0FPC15abort5abortGuE(moonbit_string_t);

uint16_t* _M0FPC15abort5abortGAkE(moonbit_string_t);

int32_t* _M0FPC15abort5abortGRPB18UninitializedArrayGiEE(moonbit_string_t);

float* _M0FPC15abort5abortGRPB18UninitializedArrayGfEE(moonbit_string_t);

int32_t _M0FPC15abort5abortGiE(moonbit_string_t);

int32_t _M0IP016_24default__implPB6Logger61write_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLoggerGRPB13StringBuilderE(
  void*,
  struct _M0TPB4Show
);

int32_t _M0IP016_24default__implPB6Logger84write__string__interpolation_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLoggerGRPB13StringBuilderE(
  void*,
  struct _M0TPB4Show
);

int32_t _M0IPB13StringBuilderPB6Logger67write__char_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLogger(
  void*,
  int32_t
);

int32_t _M0IPB13StringBuilderPB6Logger67write__view_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLogger(
  void*,
  struct _M0TPC16string10StringView
);

int32_t _M0IP016_24default__implPB6Logger72write__substring_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLoggerGRPB13StringBuilderE(
  void*,
  moonbit_string_t,
  int32_t,
  int32_t
);

int32_t _M0IPB13StringBuilderPB6Logger69write__string_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLogger(
  void*,
  moonbit_string_t
);

struct { int32_t rc; uint32_t meta; uint16_t const data[1]; 
} const moonbit_string_literal_0 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 0, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[10]; 
} const moonbit_string_literal_31 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 9, 32, 32, 
    118, 91, 50, 93, 32, 61, 32, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[16]; 
} const moonbit_string_literal_25 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 15, 44, 32, 
    100, 115, 116, 95, 111, 102, 102, 115, 101, 116, 32, 61, 32, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[19]; 
} const moonbit_string_literal_21 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 18, 105, 110, 
    118, 97, 108, 105, 100, 32, 99, 111, 100, 101, 32, 112, 111, 105, 
    110, 116, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[2]; 
} const moonbit_string_literal_15 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 1, 45, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[27]; 
} const moonbit_string_literal_28 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 26, 99, 104, 
    97, 105, 110, 46, 109, 98, 116, 58, 32, 115, 105, 109, 117, 108, 
    97, 116, 105, 111, 110, 32, 100, 111, 110, 101, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[31]; 
} const moonbit_string_literal_19 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 30, 114, 97, 
    100, 105, 120, 32, 109, 117, 115, 116, 32, 98, 101, 32, 98, 101, 
    116, 119, 101, 101, 110, 32, 50, 32, 97, 110, 100, 32, 51, 54, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[9]; 
} const moonbit_string_literal_16 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 8, 73, 110, 
    102, 105, 110, 105, 116, 121, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[4]; 
} const moonbit_string_literal_14 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 3, 78, 97, 78, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[7]; 
} const moonbit_string_literal_9 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 6, 32, 109, 
    101, 97, 110, 61, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[10]; 
} const moonbit_string_literal_30 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 9, 32, 32, 
    118, 91, 49, 93, 32, 61, 32, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[2]; 
} const moonbit_string_literal_12 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 1, 48, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[19]; 
} const moonbit_string_literal_32 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 18, 32, 32, 
    114, 101, 99, 111, 114, 100, 101, 100, 32, 115, 116, 101, 112, 115, 
    58, 32, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[9]; 
} const moonbit_string_literal_26 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 8, 44, 32, 
    108, 101, 110, 32, 61, 32, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[10]; 
} const moonbit_string_literal_5 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 9, 93, 32, 
    40, 101, 109, 112, 116, 121, 41, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[37]; 
} const moonbit_string_literal_23 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 36, 98, 111, 
    117, 110, 100, 115, 32, 99, 104, 101, 99, 107, 32, 102, 97, 105, 
    108, 101, 100, 58, 32, 97, 108, 108, 111, 99, 97, 116, 101, 95, 108, 
    101, 110, 32, 61, 32, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[6]; 
} const moonbit_string_literal_7 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 5, 32, 109, 
    105, 110, 61, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[3]; 
} const moonbit_string_literal_1 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 2, 103, 101, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[6]; 
} const moonbit_string_literal_8 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 5, 32, 109, 
    97, 120, 61, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[37]; 
} const moonbit_string_literal_20 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 36, 48, 49, 
    50, 51, 52, 53, 54, 55, 56, 57, 97, 98, 99, 100, 101, 102, 103, 104, 
    105, 106, 107, 108, 109, 110, 111, 112, 113, 114, 115, 116, 117, 
    118, 119, 120, 121, 122, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[9]; 
} const moonbit_string_literal_4 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 8, 77, 111, 
    110, 105, 116, 111, 114, 91, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[43]; 
} const moonbit_string_literal_10 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 42, 105, 110, 
    100, 101, 120, 32, 111, 117, 116, 32, 111, 102, 32, 98, 111, 117, 
    110, 100, 115, 58, 32, 116, 104, 101, 32, 108, 101, 110, 32, 105, 
    115, 32, 102, 114, 111, 109, 32, 48, 32, 116, 111, 32, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[5]; 
} const moonbit_string_literal_3 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 4, 102, 105, 
    114, 101, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[16]; 
} const moonbit_string_literal_27 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 15, 44, 32, 
    115, 114, 99, 46, 108, 101, 110, 103, 116, 104, 32, 61, 32, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[16]; 
} const moonbit_string_literal_24 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 15, 44, 32, 
    115, 114, 99, 95, 111, 102, 102, 115, 101, 116, 32, 61, 32, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[2]; 
} const moonbit_string_literal_2 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 1, 118, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[24]; 
} const moonbit_string_literal_18 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 23, 65, 114, 
    114, 97, 121, 32, 99, 97, 112, 97, 99, 105, 116, 121, 32, 111, 118, 
    101, 114, 102, 108, 111, 119, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[26]; 
} const moonbit_string_literal_13 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 25, 73, 108, 
    108, 101, 103, 97, 108, 65, 114, 103, 117, 109, 101, 110, 116, 69, 
    120, 99, 101, 112, 116, 105, 111, 110, 32, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[19]; 
} const moonbit_string_literal_11 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 18, 32, 98, 
    117, 116, 32, 116, 104, 101, 32, 105, 110, 100, 101, 120, 32, 105, 
    115, 32, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[5]; 
} const moonbit_string_literal_6 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 4, 93, 32, 
    110, 61, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[10]; 
} const moonbit_string_literal_29 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 9, 32, 32, 
    118, 91, 48, 93, 32, 61, 32, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[32]; 
} const moonbit_string_literal_22 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 31, 83, 116, 
    114, 105, 110, 103, 66, 117, 105, 108, 100, 101, 114, 32, 99, 97, 
    112, 97, 99, 105, 116, 121, 32, 111, 118, 101, 114, 102, 108, 111, 
    119, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[4]; 
} const moonbit_string_literal_17 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 3, 48, 46, 48, 0};

struct moonbit_object const moonbit_constant_constructor_0 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_REGULAR),
    Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0)
  };

uint32_t const moonbit_layout_table_data[71] =
  {
    sizeof(struct _M0TPB5ArrayGfE) / 4, 1,
    offsetof(struct _M0TPB5ArrayGfE, $0) / 4 * 2,
    sizeof(struct _M0TPB5ArrayGiE) / 4, 1,
    offsetof(struct _M0TPB5ArrayGiE, $0) / 4 * 2,
    sizeof(struct _M0TP26RiantR8snn__mbt14SpikingSynapse) / 4, 10,
    offsetof(struct _M0TP26RiantR8snn__mbt14SpikingSynapse, $0) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt14SpikingSynapse, $1) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt14SpikingSynapse, $2) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt14SpikingSynapse, $3) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt14SpikingSynapse, $4) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt14SpikingSynapse, $5) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt14SpikingSynapse, $6) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt14SpikingSynapse, $7) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt14SpikingSynapse, $8) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt14SpikingSynapse, $9) / 4 * 2,
    sizeof(struct _M0TP26RiantR8snn__mbt2IF) / 4, 16,
    offsetof(struct _M0TP26RiantR8snn__mbt2IF, $0) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt2IF, $1) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt2IF, $3) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt2IF, $4) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt2IF, $5) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt2IF, $6) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt2IF, $7) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt2IF, $8) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt2IF, $9) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt2IF, $10) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt2IF, $11) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt2IF, $12) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt2IF, $13) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt2IF, $14) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt2IF, $15) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt2IF, $16) / 4 * 2,
    sizeof(struct _M0TP26RiantR8snn__mbt7Monitor) / 4, 4,
    offsetof(struct _M0TP26RiantR8snn__mbt7Monitor, $0) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt7Monitor, $1) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt7Monitor, $2) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt7Monitor, $3) / 4 * 2,
    sizeof(struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR) / 4, 3,
    offsetof(struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR, $2) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR, $3) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR, $4) / 4 * 2,
    sizeof(struct _M0TP26RiantR8snn__mbt4Time) / 4, 2,
    offsetof(struct _M0TP26RiantR8snn__mbt4Time, $0) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt4Time, $1) / 4 * 2,
    sizeof(struct _M0TPB5ArrayGbE) / 4, 1,
    offsetof(struct _M0TPB5ArrayGbE, $0) / 4 * 2,
    sizeof(struct _M0TPB13StringBuilder) / 4, 1,
    offsetof(struct _M0TPB13StringBuilder, $0) / 4 * 2,
    sizeof(struct _M0TPB5ArrayGRP26RiantR8snn__mbt2IFE) / 4, 1,
    offsetof(struct _M0TPB5ArrayGRP26RiantR8snn__mbt2IFE, $0) / 4 * 2,
    sizeof(struct _M0TPB5ArrayGRP26RiantR8snn__mbt14SpikingSynapseE) / 4, 
    1,
    offsetof(struct _M0TPB5ArrayGRP26RiantR8snn__mbt14SpikingSynapseE, $0)
    / 4
    * 2, sizeof(struct _M0TPB5ArrayGRP26RiantR8snn__mbt7MonitorE) / 4, 
    1,
    offsetof(struct _M0TPB5ArrayGRP26RiantR8snn__mbt7MonitorE, $0) / 4 * 2,
    sizeof(struct _M0TP26RiantR8snn__mbt5Model) / 4, 3,
    offsetof(struct _M0TP26RiantR8snn__mbt5Model, $0) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt5Model, $1) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt5Model, $2) / 4 * 2
  };

struct { int32_t rc; uint32_t meta; struct _M0BTPB6Logger data; 
} _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id$object =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_REGULAR),
    Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0),
    {.$method_0 = _M0IPB13StringBuilderPB6Logger69write__string_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLogger,
       .$method_1 = _M0IP016_24default__implPB6Logger72write__substring_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLoggerGRPB13StringBuilderE,
       .$method_2 = _M0IPB13StringBuilderPB6Logger67write__view_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLogger,
       .$method_3 = _M0IPB13StringBuilderPB6Logger67write__char_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLogger,
       .$method_4 = _M0IP016_24default__implPB6Logger84write__string__interpolation_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLoggerGRPB13StringBuilderE,
       .$method_5 = _M0IP016_24default__implPB6Logger61write_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLoggerGRPB13StringBuilderE}
  };

struct _M0BTPB6Logger* _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id =
  &_M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id$object.data;

struct { int32_t rc; uint32_t meta; uint64_t data[30]; 
} _M0FPB26gDOUBLE__POW5__INV__SPLIT2$object =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 30, 1ull,
    2305843009213693952ull, 5955668970331000884ull, 1784059615882449851ull,
    8982663654677661702ull, 1380349269358112757ull, 7286864317269821294ull,
    2135987035920910082ull, 7005857020398200553ull, 1652639921975621497ull,
    17965325103354776697ull, 1278668206209430417ull, 8928596168509315048ull,
    1978643211784836272ull, 10075671573058298858ull, 1530901034580419511ull,
    597001226353042382ull, 1184477304306571148ull, 1527430471115325346ull,
    1832889850782397517ull, 12533209867169019542ull, 1418129833677084982ull,
    5577825024675947042ull, 2194449627517475473ull, 11006974540203867551ull,
    1697873161311732311ull, 10313493231639821582ull, 1313665730009899186ull,
    12701016819766672773ull, 2032799256770390445ull
  };

uint64_t* _M0FPB26gDOUBLE__POW5__INV__SPLIT2 =
  _M0FPB26gDOUBLE__POW5__INV__SPLIT2$object.data;

struct { int32_t rc; uint32_t meta; uint32_t data[19]; 
} _M0FPB19gPOW5__INV__OFFSETS$object =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 19, 1414808916u,
    67458373u, 268701696u, 4195348u, 1073807360u, 1091917141u, 1108u, 
    65604u, 1073741824u, 1140850753u, 1346716752u, 1431634004u, 1365595476u,
    1073758208u, 16777217u, 66816u, 1364284433u, 89478484u, 0u
  };

uint32_t* _M0FPB19gPOW5__INV__OFFSETS =
  _M0FPB19gPOW5__INV__OFFSETS$object.data;

struct { int32_t rc; uint32_t meta; uint64_t data[26]; 
} _M0FPB21gDOUBLE__POW5__SPLIT2$object =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 26, 0ull,
    1152921504606846976ull, 0ull, 1490116119384765625ull,
    1032610780636961552ull, 1925929944387235853ull, 7910200175544436838ull,
    1244603055572228341ull, 16941905809032713930ull, 1608611746708759036ull,
    13024893955298202172ull, 2079081953128979843ull, 6607496772837067824ull,
    1343575221513417750ull, 17332926989895652603ull, 1736530273035216783ull,
    13037379183483547984ull, 2244412773384604712ull, 1605989338741628675ull,
    1450417759929778918ull, 9630225068416591280ull, 1874621017369538693ull,
    665883850346957067ull, 1211445438634777304ull, 14931890668723713708ull,
    1565756531257009982ull
  };

uint64_t* _M0FPB21gDOUBLE__POW5__SPLIT2 =
  _M0FPB21gDOUBLE__POW5__SPLIT2$object.data;

struct { int32_t rc; uint32_t meta; uint32_t data[21]; 
} _M0FPB14gPOW5__OFFSETS$object =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 21, 0u, 0u, 
    0u, 0u, 1073741824u, 1500076437u, 1431590229u, 1448432917u, 1091896580u,
    1079333904u, 1146442053u, 1146111296u, 1163220304u, 1073758208u,
    2521039936u, 1431721317u, 1413824581u, 1075134801u, 1431671125u,
    1363170645u, 261u
  };

uint32_t* _M0FPB14gPOW5__OFFSETS = _M0FPB14gPOW5__OFFSETS$object.data;

struct { int32_t rc; uint32_t meta; uint64_t data[26]; 
} _M0FPB20gDOUBLE__POW5__TABLE$object =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 26, 1ull, 5ull,
    25ull, 125ull, 625ull, 3125ull, 15625ull, 78125ull, 390625ull,
    1953125ull, 9765625ull, 48828125ull, 244140625ull, 1220703125ull,
    6103515625ull, 30517578125ull, 152587890625ull, 762939453125ull,
    3814697265625ull, 19073486328125ull, 95367431640625ull,
    476837158203125ull, 2384185791015625ull, 11920928955078125ull,
    59604644775390625ull, 298023223876953125ull
  };

uint64_t* _M0FPB20gDOUBLE__POW5__TABLE =
  _M0FPB20gDOUBLE__POW5__TABLE$object.data;

float _M0FP26RiantR8snn__mbt2ms = 0x1p+0f;

int32_t _M0FP26RiantR8snn__mbt16spiking__connect(
  struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0L1cS882,
  int32_t _M0L3preS879,
  int32_t _M0L4postS881,
  float _M0L1wS883
) {
  int32_t _M0L8pre__idxS878;
  int32_t _M0L9post__idxS880;
  struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS2045;
  #line 97 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
  _M0L8pre__idxS878 = _M0L3preS879 - 1;
  _M0L9post__idxS880 = _M0L4postS881 - 1;
  _M0L6matrixS2045 = _M0L1cS882->$4;
  #line 100 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
  _M0MP26RiantR8snn__mbt15SparseMatrixCSR3set(_M0L6matrixS2045, _M0L8pre__idxS878, _M0L9post__idxS880, _M0L1wS883);
  return 0;
}

struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0MP26RiantR8snn__mbt14SpikingSynapse3new(
  struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS875,
  struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS876,
  moonbit_string_t _M0L3symS877
) {
  int32_t _M0L1nS2043;
  int32_t _M0L1nS2044;
  struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS874;
  float* _M0L6_2atmpS2042;
  struct _M0TPB5ArrayGfE* _M0L6_2atmpS2033;
  float* _M0L6_2atmpS2041;
  struct _M0TPB5ArrayGfE* _M0L6_2atmpS2034;
  float* _M0L6_2atmpS2040;
  struct _M0TPB5ArrayGfE* _M0L6_2atmpS2035;
  int32_t* _M0L6_2atmpS2039;
  struct _M0TPB5ArrayGiE* _M0L6_2atmpS2036;
  float* _M0L6_2atmpS2038;
  struct _M0TPB5ArrayGfE* _M0L6_2atmpS2037;
  struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _block_2085;
  #line 55 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
  _M0L1nS2043 = _M0L3preS875->$2;
  _M0L1nS2044 = _M0L4postS876->$2;
  #line 56 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
  _M0L6matrixS874
  = _M0MP26RiantR8snn__mbt15SparseMatrixCSR5empty(_M0L1nS2043, _M0L1nS2044);
  _M0L6_2atmpS2042 = moonbit_empty_float_array;
  _M0L6_2atmpS2033
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_M0L6_2atmpS2033)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 0, 0);
  _M0L6_2atmpS2033->$0 = _M0L6_2atmpS2042;
  _M0L6_2atmpS2033->$1 = 0;
  _M0L6_2atmpS2041 = moonbit_empty_float_array;
  _M0L6_2atmpS2034
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_M0L6_2atmpS2034)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 0, 0);
  _M0L6_2atmpS2034->$0 = _M0L6_2atmpS2041;
  _M0L6_2atmpS2034->$1 = 0;
  _M0L6_2atmpS2040 = moonbit_empty_float_array;
  _M0L6_2atmpS2035
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_M0L6_2atmpS2035)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 0, 0);
  _M0L6_2atmpS2035->$0 = _M0L6_2atmpS2040;
  _M0L6_2atmpS2035->$1 = 0;
  _M0L6_2atmpS2039 = (int32_t*)moonbit_empty_int32_array;
  _M0L6_2atmpS2036
  = (struct _M0TPB5ArrayGiE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGiE));
  Moonbit_object_header(_M0L6_2atmpS2036)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 3, 0);
  _M0L6_2atmpS2036->$0 = _M0L6_2atmpS2039;
  _M0L6_2atmpS2036->$1 = 0;
  _M0L6_2atmpS2038 = moonbit_empty_float_array;
  _M0L6_2atmpS2037
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_M0L6_2atmpS2037)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 0, 0);
  _M0L6_2atmpS2037->$0 = _M0L6_2atmpS2038;
  _M0L6_2atmpS2037->$1 = 0;
  moonbit_incref(_M0L3preS875);
  moonbit_incref(_M0L4postS876);
  moonbit_incref(_M0L3symS877);
  _block_2085
  = (struct _M0TP26RiantR8snn__mbt14SpikingSynapse*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt14SpikingSynapse));
  Moonbit_object_header(_block_2085)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 6, 0);
  _block_2085->$0 = _M0L3preS875;
  _block_2085->$1 = _M0L4postS876;
  _block_2085->$2 = _M0L3symS877;
  _block_2085->$3 = (moonbit_string_t)moonbit_string_literal_0.data;
  _block_2085->$4 = _M0L6matrixS874;
  _block_2085->$5 = _M0L6_2atmpS2033;
  _block_2085->$6 = _M0L6_2atmpS2034;
  _block_2085->$7 = _M0L6_2atmpS2035;
  _block_2085->$8 = _M0L6_2atmpS2036;
  _block_2085->$9 = _M0L6_2atmpS2037;
  return _block_2085;
}

struct _M0TP26RiantR8snn__mbt11IFParameter* _M0MP26RiantR8snn__mbt11IFParameter3new(
  
) {
  float _M0L1cS872;
  float _M0L2glS873;
  struct _M0TP26RiantR8snn__mbt11IFParameter* _block_2086;
  #line 39 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L1cS872 = -0x1p+0f;
  _M0L2glS873 = -0x1p+0f;
  _block_2086
  = (struct _M0TP26RiantR8snn__mbt11IFParameter*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt11IFParameter));
  Moonbit_object_header(_block_2086)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_2086->$0 = _M0L1cS872;
  _block_2086->$1 = _M0L2glS873;
  _block_2086->$2 = 0x1.ep+3f;
  _block_2086->$3 = -0x1.9p+5f;
  _block_2086->$4 = -0x1.ep+5f;
  _block_2086->$5 = -0x1.18p+6f;
  _block_2086->$6 = 0x1.eb851eb851eb8p-5f;
  _block_2086->$7 = 0x1p+1f;
  _block_2086->$8 = 0x0p+0f;
  _block_2086->$9 = 0x0p+0f;
  _block_2086->$10 = 0x0p+0f;
  return _block_2086;
}

struct _M0TP26RiantR8snn__mbt2IF* _M0MP26RiantR8snn__mbt2IF3new(
  int32_t _M0L1nS846,
  struct _M0TP26RiantR8snn__mbt11IFParameter* _M0L5paramS848,
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS851
) {
  struct _M0TPB5ArrayGfE* _M0L1vS845;
  float _M0L2vtS2031;
  float _M0L2vrS2032;
  float _M0L6spreadS847;
  int32_t _M0L7_2abindS849;
  int32_t _M0L1kS850;
  struct _M0TPB5ArrayGfE* _M0L1wS853;
  struct _M0TPB5ArrayGbE* _M0L4fireS854;
  struct _M0TPB5ArrayGiE* _M0L4tabsS855;
  struct _M0TPB5ArrayGfE* _M0L1iS856;
  struct _M0TPB5ArrayGfE* _M0L9syn__currS857;
  struct _M0TPB5ArrayGfE* _M0L2geS858;
  struct _M0TPB5ArrayGfE* _M0L2giS859;
  struct _M0TPB5ArrayGfE* _M0L2heS860;
  struct _M0TPB5ArrayGfE* _M0L2hiS861;
  struct _M0TPB5ArrayGfE* _M0L3gluS862;
  struct _M0TPB5ArrayGfE* _M0L4gabaS863;
  struct _M0TPB5ArrayGfE* _M0L7gsyn__eS864;
  struct _M0TPB5ArrayGfE* _M0L7gsyn__iS865;
  float _M0L4e__eS866;
  float _M0L4e__iS867;
  float _M0L3treS868;
  float _M0L3tdeS869;
  float _M0L3triS870;
  float _M0L3tdiS871;
  struct _M0TP26RiantR8snn__mbt9PostSpike* _M0L6_2atmpS2030;
  struct _M0TP26RiantR8snn__mbt2IF* _block_2088;
  #line 113 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  #line 114 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L1vS845 = _M0MPC15array5Array4makeGfE(_M0L1nS846, 0x0p+0f);
  _M0L2vtS2031 = _M0L5paramS848->$3;
  _M0L2vrS2032 = _M0L5paramS848->$4;
  _M0L6spreadS847 = _M0L2vtS2031 - _M0L2vrS2032;
  _M0L7_2abindS849 = 0;
  _M0L1kS850 = _M0L7_2abindS849;
  while (1) {
    if (_M0L1kS850 < _M0L1nS846) {
      float _M0L2vrS2026 = _M0L5paramS848->$4;
      float _M0L6_2atmpS2028;
      float _M0L6_2atmpS2027;
      float _M0L6_2atmpS2025;
      int32_t _M0L6_2atmpS2029;
      #line 117 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2028 = _M0FP26RiantR8snn__mbt9next__f32(_M0L3rngS851);
      _M0L6_2atmpS2027 = _M0L6_2atmpS2028 * _M0L6spreadS847;
      _M0L6_2atmpS2025 = _M0L2vrS2026 + _M0L6_2atmpS2027;
      #line 117 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS845, _M0L1kS850, _M0L6_2atmpS2025);
      _M0L6_2atmpS2029 = _M0L1kS850 + 1;
      _M0L1kS850 = _M0L6_2atmpS2029;
      continue;
    }
    break;
  }
  #line 119 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L1wS853 = _M0MPC15array5Array4makeGfE(_M0L1nS846, 0x0p+0f);
  #line 120 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L4fireS854 = _M0MPC15array5Array4makeGbE(_M0L1nS846, 0);
  #line 121 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L4tabsS855 = _M0MPC15array5Array4makeGiE(_M0L1nS846, 0);
  #line 122 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L1iS856 = _M0MPC15array5Array4makeGfE(_M0L1nS846, 0x0p+0f);
  #line 123 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L9syn__currS857 = _M0MPC15array5Array4makeGfE(_M0L1nS846, 0x0p+0f);
  #line 124 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L2geS858 = _M0MPC15array5Array4makeGfE(_M0L1nS846, 0x0p+0f);
  #line 125 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L2giS859 = _M0MPC15array5Array4makeGfE(_M0L1nS846, 0x0p+0f);
  #line 126 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L2heS860 = _M0MPC15array5Array4makeGfE(_M0L1nS846, 0x0p+0f);
  #line 127 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L2hiS861 = _M0MPC15array5Array4makeGfE(_M0L1nS846, 0x0p+0f);
  #line 128 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L3gluS862 = _M0MPC15array5Array4makeGfE(_M0L1nS846, 0x0p+0f);
  #line 129 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L4gabaS863 = _M0MPC15array5Array4makeGfE(_M0L1nS846, 0x0p+0f);
  #line 130 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L7gsyn__eS864 = _M0MPC15array5Array4makeGfE(_M0L1nS846, 0x1p+0f);
  #line 131 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L7gsyn__iS865 = _M0MPC15array5Array4makeGfE(_M0L1nS846, 0x1p+0f);
  _M0L4e__eS866 = 0x0p+0f;
  _M0L4e__iS867 = -0x1.2cp+6f;
  _M0L3treS868 = 0x1p+0f;
  _M0L3tdeS869 = 0x1.8p+2f;
  _M0L3triS870 = 0x1p-1f;
  _M0L3tdiS871 = 0x1p+1f;
  #line 138 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L6_2atmpS2030 = _M0MP26RiantR8snn__mbt9PostSpike3new();
  moonbit_incref(_M0L5paramS848);
  _block_2088
  = (struct _M0TP26RiantR8snn__mbt2IF*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt2IF));
  Moonbit_object_header(_block_2088)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 18, 0);
  _block_2088->$0 = _M0L5paramS848;
  _block_2088->$1 = _M0L6_2atmpS2030;
  _block_2088->$2 = _M0L1nS846;
  _block_2088->$3 = _M0L1vS845;
  _block_2088->$4 = _M0L1wS853;
  _block_2088->$5 = _M0L4fireS854;
  _block_2088->$6 = _M0L4tabsS855;
  _block_2088->$7 = _M0L1iS856;
  _block_2088->$8 = _M0L9syn__currS857;
  _block_2088->$9 = _M0L2geS858;
  _block_2088->$10 = _M0L2giS859;
  _block_2088->$11 = _M0L2heS860;
  _block_2088->$12 = _M0L2hiS861;
  _block_2088->$13 = _M0L3gluS862;
  _block_2088->$14 = _M0L4gabaS863;
  _block_2088->$15 = _M0L7gsyn__eS864;
  _block_2088->$16 = _M0L7gsyn__iS865;
  _block_2088->$17 = _M0L4e__eS866;
  _block_2088->$18 = _M0L4e__iS867;
  _block_2088->$19 = _M0L3treS868;
  _block_2088->$20 = _M0L3tdeS869;
  _block_2088->$21 = _M0L3triS870;
  _block_2088->$22 = _M0L3tdiS871;
  return _block_2088;
}

struct _M0TP26RiantR8snn__mbt9PostSpike* _M0MP26RiantR8snn__mbt9PostSpike3new(
  
) {
  struct _M0TP26RiantR8snn__mbt9PostSpike* _block_2089;
  #line 75 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _block_2089
  = (struct _M0TP26RiantR8snn__mbt9PostSpike*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt9PostSpike));
  Moonbit_object_header(_block_2089)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_2089->$0 = 0x1p+1f;
  return _block_2089;
}

struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0MP26RiantR8snn__mbt7Xoshiro7default(
  
) {
  #line 56 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  #line 57 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  return _M0MP26RiantR8snn__mbt7Xoshiro3new(0ull);
}

int32_t _M0FP26RiantR8snn__mbt8sim__for(
  struct _M0TP26RiantR8snn__mbt5Model* _M0L5modelS841,
  float _M0L8durationS840
) {
  float _M0L2dtS837;
  struct _M0TP26RiantR8snn__mbt4Time* _M0L4timeS838;
  float _M0L6_2atmpS2024;
  int32_t _M0L5stepsS839;
  int32_t _M0L7_2abindS842;
  int32_t _M0L2__S843;
  #line 123 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
  _M0L2dtS837 = 0x1p-3f;
  #line 125 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
  _M0L4timeS838 = _M0MP26RiantR8snn__mbt4Time3new();
  #line 126 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
  _M0FP26RiantR8snn__mbt7set__dt(_M0L4timeS838, _M0L2dtS837);
  _M0L6_2atmpS2024 = _M0L8durationS840 / _M0L2dtS837;
  #line 127 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
  _M0L5stepsS839 = _M0MPC15float5Float7to__int(_M0L6_2atmpS2024);
  #line 128 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
  _M0FP26RiantR8snn__mbt12record__zero(_M0L5modelS841);
  _M0L7_2abindS842 = 0;
  _M0L2__S843 = _M0L7_2abindS842;
  while (1) {
    if (_M0L2__S843 < _M0L5stepsS839) {
      int32_t _M0L6_2atmpS2023;
      #line 130 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
      _M0FP26RiantR8snn__mbt11step__model(_M0L5modelS841, _M0L4timeS838);
      _M0L6_2atmpS2023 = _M0L2__S843 + 1;
      _M0L2__S843 = _M0L6_2atmpS2023;
      continue;
    } else {
      moonbit_decref(_M0L4timeS838);
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt11step__model(
  struct _M0TP26RiantR8snn__mbt5Model* _M0L5modelS812,
  struct _M0TP26RiantR8snn__mbt4Time* _M0L4timeS810
) {
  float _M0L6t__nowS809;
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt14SpikingSynapseE* _M0L7_2abindS811;
  int32_t _M0L7_2abindS813;
  struct _M0TP26RiantR8snn__mbt14SpikingSynapse** _M0L7_2abindS814;
  int32_t _M0L2__S815;
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt14SpikingSynapseE* _M0L7_2abindS818;
  int32_t _M0L7_2abindS819;
  struct _M0TP26RiantR8snn__mbt14SpikingSynapse** _M0L7_2abindS820;
  int32_t _M0L2__S821;
  float _M0L2dtS824;
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt2IFE* _M0L7_2abindS825;
  int32_t _M0L7_2abindS826;
  struct _M0TP26RiantR8snn__mbt2IF** _M0L7_2abindS827;
  int32_t _M0L2__S828;
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt7MonitorE* _M0L7_2abindS831;
  int32_t _M0L7_2abindS832;
  struct _M0TP26RiantR8snn__mbt7Monitor** _M0L7_2abindS833;
  int32_t _M0L2__S834;
  #line 95 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
  #line 97 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
  _M0L6t__nowS809 = _M0FP26RiantR8snn__mbt9get__time(_M0L4timeS810);
  _M0L7_2abindS811 = _M0L5modelS812->$1;
  _M0L7_2abindS813 = _M0L7_2abindS811->$1;
  _M0L7_2abindS814 = _M0L7_2abindS811->$0;
  moonbit_incref(_M0L7_2abindS814);
  _M0L2__S815 = 0;
  while (1) {
    if (_M0L2__S815 < _M0L7_2abindS813) {
      struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0L1cS816 =
        (struct _M0TP26RiantR8snn__mbt14SpikingSynapse*)_M0L7_2abindS814[
          _M0L2__S815
        ];
      int32_t _M0L6_2atmpS2017;
      moonbit_incref(_M0L1cS816);
      #line 99 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
      _M0FP26RiantR8snn__mbt25deliver__pending__synapse(_M0L1cS816, _M0L6t__nowS809);
      moonbit_decref(_M0L1cS816);
      _M0L6_2atmpS2017 = _M0L2__S815 + 1;
      _M0L2__S815 = _M0L6_2atmpS2017;
      continue;
    } else {
      moonbit_decref(_M0L7_2abindS814);
    }
    break;
  }
  _M0L7_2abindS818 = _M0L5modelS812->$1;
  _M0L7_2abindS819 = _M0L7_2abindS818->$1;
  _M0L7_2abindS820 = _M0L7_2abindS818->$0;
  moonbit_incref(_M0L7_2abindS820);
  _M0L2__S821 = 0;
  while (1) {
    if (_M0L2__S821 < _M0L7_2abindS819) {
      struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0L1cS822 =
        (struct _M0TP26RiantR8snn__mbt14SpikingSynapse*)_M0L7_2abindS820[
          _M0L2__S821
        ];
      int32_t _M0L6_2atmpS2018;
      moonbit_incref(_M0L1cS822);
      #line 104 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
      _M0FP26RiantR8snn__mbt16forward__synapse(_M0L1cS822, _M0L6t__nowS809);
      moonbit_decref(_M0L1cS822);
      _M0L6_2atmpS2018 = _M0L2__S821 + 1;
      _M0L2__S821 = _M0L6_2atmpS2018;
      continue;
    } else {
      moonbit_decref(_M0L7_2abindS820);
    }
    break;
  }
  _M0L2dtS824 = _M0L4timeS810->$2;
  _M0L7_2abindS825 = _M0L5modelS812->$0;
  _M0L7_2abindS826 = _M0L7_2abindS825->$1;
  _M0L7_2abindS827 = _M0L7_2abindS825->$0;
  moonbit_incref(_M0L7_2abindS827);
  _M0L2__S828 = 0;
  while (1) {
    if (_M0L2__S828 < _M0L7_2abindS826) {
      struct _M0TP26RiantR8snn__mbt2IF* _M0L1pS829 =
        (struct _M0TP26RiantR8snn__mbt2IF*)_M0L7_2abindS827[_M0L2__S828];
      int32_t _M0L6_2atmpS2019;
      moonbit_incref(_M0L1pS829);
      #line 109 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
      _M0FP26RiantR8snn__mbt14step__synapses(_M0L1pS829, _M0L2dtS824);
      #line 110 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
      _M0FP26RiantR8snn__mbt17synaptic__current(_M0L1pS829);
      #line 111 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
      _M0FP26RiantR8snn__mbt12step__neuron(_M0L1pS829, _M0L2dtS824);
      moonbit_decref(_M0L1pS829);
      _M0L6_2atmpS2019 = _M0L2__S828 + 1;
      _M0L2__S828 = _M0L6_2atmpS2019;
      continue;
    } else {
      moonbit_decref(_M0L7_2abindS827);
    }
    break;
  }
  _M0L7_2abindS831 = _M0L5modelS812->$2;
  _M0L7_2abindS832 = _M0L7_2abindS831->$1;
  _M0L7_2abindS833 = _M0L7_2abindS831->$0;
  moonbit_incref(_M0L7_2abindS833);
  _M0L2__S834 = 0;
  while (1) {
    if (_M0L2__S834 < _M0L7_2abindS832) {
      struct _M0TP26RiantR8snn__mbt7Monitor* _M0L1mS835 =
        (struct _M0TP26RiantR8snn__mbt7Monitor*)_M0L7_2abindS833[_M0L2__S834];
      struct _M0TPB5ArrayGfE* _M0L1tS2021 = _M0L4timeS810->$0;
      float _M0L6_2atmpS2020;
      int32_t _M0L6_2atmpS2022;
      moonbit_incref(_M0L1mS835);
      #line 115 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
      _M0L6_2atmpS2020 = _M0MPC15array5Array2atGfE(_M0L1tS2021, 0);
      #line 115 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
      _M0FP26RiantR8snn__mbt11record__one(_M0L1mS835, _M0L6_2atmpS2020);
      moonbit_decref(_M0L1mS835);
      _M0L6_2atmpS2022 = _M0L2__S834 + 1;
      _M0L2__S834 = _M0L6_2atmpS2022;
      continue;
    } else {
      moonbit_decref(_M0L7_2abindS833);
    }
    break;
  }
  #line 118 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
  _M0FP26RiantR8snn__mbt12update__time(_M0L4timeS810, _M0L2dtS824);
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt16forward__synapse(
  struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0L1cS785,
  float _M0L6t__nowS796
) {
  struct _M0TPB5ArrayGfE* _M0L6delaysS2016;
  int32_t _M0L6_2atmpS2015;
  int32_t _M0L10use__delayS784;
  struct _M0TPB5ArrayGfE* _M0L3rhoS2014;
  int32_t _M0L6_2atmpS2013;
  int32_t _M0L8use__rhoS786;
  #line 246 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
  _M0L6delaysS2016 = _M0L1cS785->$5;
  #line 247 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
  _M0L6_2atmpS2015 = _M0MPC15array5Array6lengthGfE(_M0L6delaysS2016);
  _M0L10use__delayS784 = _M0L6_2atmpS2015 > 0;
  _M0L3rhoS2014 = _M0L1cS785->$6;
  #line 248 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
  _M0L6_2atmpS2013 = _M0MPC15array5Array6lengthGfE(_M0L3rhoS2014);
  _M0L8use__rhoS786 = _M0L6_2atmpS2013 > 0;
  if (_M0L10use__delayS784) {
    struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS1976 = _M0L1cS785->$0;
    struct _M0TPB5ArrayGbE* _M0L4fireS1975 = _M0L3preS1976->$5;
    int32_t _M0L6n__preS787;
    struct _M0TPB8MutLocalGiE* _M0L1jS788;
    #line 251 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
    _M0L6n__preS787 = _M0MPC15array5Array6lengthGbE(_M0L4fireS1975);
    _M0L1jS788
    = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
    Moonbit_object_header(_M0L1jS788)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
    _M0L1jS788->$0 = 0;
    while (1) {
      int32_t _M0L3valS1944 = _M0L1jS788->$0;
      if (_M0L3valS1944 < _M0L6n__preS787) {
        struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS1947 = _M0L1cS785->$0;
        struct _M0TPB5ArrayGbE* _M0L4fireS1945 = _M0L3preS1947->$5;
        int32_t _M0L3valS1946 = _M0L1jS788->$0;
        int32_t _M0L3valS1974;
        int32_t _M0L6_2atmpS1973;
        #line 254 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
        if (_M0MPC15array5Array2atGbE(_M0L4fireS1945, _M0L3valS1946)) {
          struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS1972 =
            _M0L1cS785->$4;
          struct _M0TPB5ArrayGiE* _M0L6rowptrS1970 = _M0L6matrixS1972->$2;
          int32_t _M0L3valS1971 = _M0L1jS788->$0;
          int32_t _M0L5startS789;
          struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS1969;
          struct _M0TPB5ArrayGiE* _M0L6rowptrS1966;
          int32_t _M0L3valS1968;
          int32_t _M0L6_2atmpS1967;
          int32_t _M0L3endS790;
          struct _M0TPB8MutLocalGiE* _M0L1sS791;
          #line 255 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
          _M0L5startS789
          = _M0MPC15array5Array2atGiE(_M0L6rowptrS1970, _M0L3valS1971);
          _M0L6matrixS1969 = _M0L1cS785->$4;
          _M0L6rowptrS1966 = _M0L6matrixS1969->$2;
          _M0L3valS1968 = _M0L1jS788->$0;
          _M0L6_2atmpS1967 = _M0L3valS1968 + 1;
          #line 256 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
          _M0L3endS790
          = _M0MPC15array5Array2atGiE(_M0L6rowptrS1966, _M0L6_2atmpS1967);
          _M0L1sS791
          = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
          Moonbit_object_header(_M0L1sS791)->meta
          = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
          _M0L1sS791->$0 = _M0L5startS789;
          while (1) {
            int32_t _M0L3valS1948 = _M0L1sS791->$0;
            if (_M0L3valS1948 < _M0L3endS790) {
              struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS1965 =
                _M0L1cS785->$4;
              struct _M0TPB5ArrayGiE* _M0L6colptrS1963 = _M0L6matrixS1965->$3;
              int32_t _M0L3valS1964 = _M0L1sS791->$0;
              int32_t _M0L9post__idxS792;
              struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS1962;
              struct _M0TPB5ArrayGfE* _M0L4valsS1960;
              int32_t _M0L3valS1961;
              float _M0L1wS793;
              struct _M0TPB5ArrayGfE* _M0L6delaysS1958;
              int32_t _M0L3valS1959;
              float _M0L1dS794;
              float _M0L9w__scaledS795;
              int32_t _M0L3valS1954;
              int32_t _M0L6_2atmpS1953;
              #line 259 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
              _M0L9post__idxS792
              = _M0MPC15array5Array2atGiE(_M0L6colptrS1963, _M0L3valS1964);
              _M0L6matrixS1962 = _M0L1cS785->$4;
              _M0L4valsS1960 = _M0L6matrixS1962->$4;
              _M0L3valS1961 = _M0L1sS791->$0;
              #line 260 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
              _M0L1wS793
              = _M0MPC15array5Array2atGfE(_M0L4valsS1960, _M0L3valS1961);
              _M0L6delaysS1958 = _M0L1cS785->$5;
              _M0L3valS1959 = _M0L1sS791->$0;
              #line 261 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
              _M0L1dS794
              = _M0MPC15array5Array2atGfE(_M0L6delaysS1958, _M0L3valS1959);
              if (_M0L8use__rhoS786) {
                struct _M0TPB5ArrayGfE* _M0L3rhoS1956 = _M0L1cS785->$6;
                int32_t _M0L3valS1957 = _M0L1sS791->$0;
                float _M0L6_2atmpS1955;
                #line 263 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
                _M0L6_2atmpS1955
                = _M0MPC15array5Array2atGfE(_M0L3rhoS1956, _M0L3valS1957);
                _M0L9w__scaledS795 = _M0L1wS793 * _M0L6_2atmpS1955;
              } else {
                _M0L9w__scaledS795 = _M0L1wS793;
              }
              if (_M0L1dS794 == 0x0p+0f) {
                #line 266 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
                _M0FP26RiantR8snn__mbt13apply__weight(_M0L1cS785, _M0L9post__idxS792, _M0L9w__scaledS795);
              } else {
                struct _M0TPB5ArrayGfE* _M0L14pending__timesS1949 =
                  _M0L1cS785->$7;
                float _M0L6_2atmpS1950 = _M0L6t__nowS796 + _M0L1dS794;
                struct _M0TPB5ArrayGiE* _M0L14pending__postsS1951;
                struct _M0TPB5ArrayGfE* _M0L16pending__weightsS1952;
                #line 269 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
                _M0MPC15array5Array4pushGfE(_M0L14pending__timesS1949, _M0L6_2atmpS1950);
                _M0L14pending__postsS1951 = _M0L1cS785->$8;
                #line 270 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
                _M0MPC15array5Array4pushGiE(_M0L14pending__postsS1951, _M0L9post__idxS792);
                _M0L16pending__weightsS1952 = _M0L1cS785->$9;
                #line 271 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
                _M0MPC15array5Array4pushGfE(_M0L16pending__weightsS1952, _M0L9w__scaledS795);
              }
              _M0L3valS1954 = _M0L1sS791->$0;
              _M0L6_2atmpS1953 = _M0L3valS1954 + 1;
              _M0L1sS791->$0 = _M0L6_2atmpS1953;
              continue;
            } else {
              moonbit_decref(_M0L1sS791);
            }
            break;
          }
        }
        _M0L3valS1974 = _M0L1jS788->$0;
        _M0L6_2atmpS1973 = _M0L3valS1974 + 1;
        _M0L1jS788->$0 = _M0L6_2atmpS1973;
        continue;
      } else {
        moonbit_decref(_M0L1jS788);
      }
      break;
    }
  } else {
    moonbit_string_t _M0L3symS2010 = _M0L1cS785->$2;
    struct _M0TPB5ArrayGfE* _M0L6targetS799;
    #line 280 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
    if (
      _M0L3symS2010 == (moonbit_string_t)moonbit_string_literal_1.data
      || Moonbit_array_length(_M0L3symS2010)
         == Moonbit_array_length((moonbit_string_t)moonbit_string_literal_1.data)
         && 0
            == memcmp(_M0L3symS2010, (moonbit_string_t)moonbit_string_literal_1.data, Moonbit_array_length(_M0L3symS2010) * 2)
    ) {
      struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS2011 = _M0L1cS785->$1;
      struct _M0TPB5ArrayGfE* _M0L8_2afieldS2046 = _M0L4postS2011->$13;
      moonbit_incref(_M0L8_2afieldS2046);
      _M0L6targetS799 = _M0L8_2afieldS2046;
    } else {
      struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS2012 = _M0L1cS785->$1;
      struct _M0TPB5ArrayGfE* _M0L8_2afieldS2047 = _M0L4postS2012->$14;
      moonbit_incref(_M0L8_2afieldS2047);
      _M0L6targetS799 = _M0L8_2afieldS2047;
    }
    if (_M0L8use__rhoS786) {
      struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS2006 = _M0L1cS785->$0;
      struct _M0TPB5ArrayGbE* _M0L4fireS2005 = _M0L3preS2006->$5;
      int32_t _M0L6n__preS800;
      struct _M0TPB8MutLocalGiE* _M0L1jS801;
      #line 283 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
      _M0L6n__preS800 = _M0MPC15array5Array6lengthGbE(_M0L4fireS2005);
      _M0L1jS801
      = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
      Moonbit_object_header(_M0L1jS801)->meta
      = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
      _M0L1jS801->$0 = 0;
      while (1) {
        int32_t _M0L3valS1977 = _M0L1jS801->$0;
        if (_M0L3valS1977 < _M0L6n__preS800) {
          struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS1980 = _M0L1cS785->$0;
          struct _M0TPB5ArrayGbE* _M0L4fireS1978 = _M0L3preS1980->$5;
          int32_t _M0L3valS1979 = _M0L1jS801->$0;
          int32_t _M0L3valS2004;
          int32_t _M0L6_2atmpS2003;
          #line 286 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
          if (_M0MPC15array5Array2atGbE(_M0L4fireS1978, _M0L3valS1979)) {
            struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS2002 =
              _M0L1cS785->$4;
            struct _M0TPB5ArrayGiE* _M0L6rowptrS2000 = _M0L6matrixS2002->$2;
            int32_t _M0L3valS2001 = _M0L1jS801->$0;
            int32_t _M0L5startS802;
            struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS1999;
            struct _M0TPB5ArrayGiE* _M0L6rowptrS1996;
            int32_t _M0L3valS1998;
            int32_t _M0L6_2atmpS1997;
            int32_t _M0L3endS803;
            struct _M0TPB8MutLocalGiE* _M0L1sS804;
            #line 287 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
            _M0L5startS802
            = _M0MPC15array5Array2atGiE(_M0L6rowptrS2000, _M0L3valS2001);
            _M0L6matrixS1999 = _M0L1cS785->$4;
            _M0L6rowptrS1996 = _M0L6matrixS1999->$2;
            _M0L3valS1998 = _M0L1jS801->$0;
            _M0L6_2atmpS1997 = _M0L3valS1998 + 1;
            #line 288 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
            _M0L3endS803
            = _M0MPC15array5Array2atGiE(_M0L6rowptrS1996, _M0L6_2atmpS1997);
            _M0L1sS804
            = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
            Moonbit_object_header(_M0L1sS804)->meta
            = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
            _M0L1sS804->$0 = _M0L5startS802;
            while (1) {
              int32_t _M0L3valS1981 = _M0L1sS804->$0;
              if (_M0L3valS1981 < _M0L3endS803) {
                struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS1995 =
                  _M0L1cS785->$4;
                struct _M0TPB5ArrayGiE* _M0L6colptrS1993 =
                  _M0L6matrixS1995->$3;
                int32_t _M0L3valS1994 = _M0L1sS804->$0;
                int32_t _M0L9post__idxS805;
                struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS1992;
                struct _M0TPB5ArrayGfE* _M0L4valsS1990;
                int32_t _M0L3valS1991;
                float _M0L6_2atmpS1986;
                struct _M0TPB5ArrayGfE* _M0L3rhoS1988;
                int32_t _M0L3valS1989;
                float _M0L6_2atmpS1987;
                float _M0L9w__scaledS806;
                float _M0L6_2atmpS1983;
                float _M0L6_2atmpS1982;
                int32_t _M0L3valS1985;
                int32_t _M0L6_2atmpS1984;
                #line 291 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
                _M0L9post__idxS805
                = _M0MPC15array5Array2atGiE(_M0L6colptrS1993, _M0L3valS1994);
                _M0L6matrixS1992 = _M0L1cS785->$4;
                _M0L4valsS1990 = _M0L6matrixS1992->$4;
                _M0L3valS1991 = _M0L1sS804->$0;
                #line 292 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
                _M0L6_2atmpS1986
                = _M0MPC15array5Array2atGfE(_M0L4valsS1990, _M0L3valS1991);
                _M0L3rhoS1988 = _M0L1cS785->$6;
                _M0L3valS1989 = _M0L1sS804->$0;
                #line 292 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
                _M0L6_2atmpS1987
                = _M0MPC15array5Array2atGfE(_M0L3rhoS1988, _M0L3valS1989);
                _M0L9w__scaledS806 = _M0L6_2atmpS1986 * _M0L6_2atmpS1987;
                #line 293 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
                _M0L6_2atmpS1983
                = _M0MPC15array5Array2atGfE(_M0L6targetS799, _M0L9post__idxS805);
                _M0L6_2atmpS1982 = _M0L6_2atmpS1983 + _M0L9w__scaledS806;
                #line 293 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
                _M0MPC15array5Array3setGfE(_M0L6targetS799, _M0L9post__idxS805, _M0L6_2atmpS1982);
                _M0L3valS1985 = _M0L1sS804->$0;
                _M0L6_2atmpS1984 = _M0L3valS1985 + 1;
                _M0L1sS804->$0 = _M0L6_2atmpS1984;
                continue;
              } else {
                moonbit_decref(_M0L1sS804);
              }
              break;
            }
          }
          _M0L3valS2004 = _M0L1jS801->$0;
          _M0L6_2atmpS2003 = _M0L3valS2004 + 1;
          _M0L1jS801->$0 = _M0L6_2atmpS2003;
          continue;
        } else {
          moonbit_decref(_M0L1jS801);
          moonbit_decref(_M0L6targetS799);
        }
        break;
      }
    } else {
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS2007 =
        _M0L1cS785->$4;
      struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS2009 = _M0L1cS785->$0;
      struct _M0TPB5ArrayGbE* _M0L4fireS2008 = _M0L3preS2009->$5;
      #line 300 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
      _M0MP26RiantR8snn__mbt15SparseMatrixCSR7forward(_M0L6matrixS2007, _M0L4fireS2008, _M0L6targetS799);
      moonbit_decref(_M0L6targetS799);
    }
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt25deliver__pending__synapse(
  struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0L1cS777,
  float _M0L6t__nowS780
) {
  struct _M0TPB5ArrayGfE* _M0L14pending__timesS1943;
  int32_t _M0L1nS776;
  struct _M0TPB8MutLocalGiE* _M0L4keptS778;
  struct _M0TPB8MutLocalGiE* _M0L1kS779;
  int32_t _M0L3valS1942;
  int32_t _M0L6_2atmpS1941;
  struct _M0TPB8MutLocalGiE* _M0L4dropS782;
  #line 312 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
  _M0L14pending__timesS1943 = _M0L1cS777->$7;
  #line 313 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
  _M0L1nS776 = _M0MPC15array5Array6lengthGfE(_M0L14pending__timesS1943);
  if (_M0L1nS776 == 0) {
    return 0;
  }
  _M0L4keptS778
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L4keptS778)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L4keptS778->$0 = 0;
  _M0L1kS779
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1kS779)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1kS779->$0 = 0;
  while (1) {
    int32_t _M0L3valS1904 = _M0L1kS779->$0;
    if (_M0L3valS1904 < _M0L1nS776) {
      struct _M0TPB5ArrayGfE* _M0L14pending__timesS1906 = _M0L1cS777->$7;
      int32_t _M0L3valS1907 = _M0L1kS779->$0;
      float _M0L6_2atmpS1905;
      int32_t _M0L3valS1934;
      int32_t _M0L6_2atmpS1933;
      #line 320 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
      _M0L6_2atmpS1905
      = _M0MPC15array5Array2atGfE(_M0L14pending__timesS1906, _M0L3valS1907);
      if (_M0L6_2atmpS1905 <= _M0L6t__nowS780) {
        struct _M0TPB5ArrayGiE* _M0L14pending__postsS1912 = _M0L1cS777->$8;
        int32_t _M0L3valS1913 = _M0L1kS779->$0;
        int32_t _M0L6_2atmpS1908;
        struct _M0TPB5ArrayGfE* _M0L16pending__weightsS1910;
        int32_t _M0L3valS1911;
        float _M0L6_2atmpS1909;
        #line 322 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
        _M0L6_2atmpS1908
        = _M0MPC15array5Array2atGiE(_M0L14pending__postsS1912, _M0L3valS1913);
        _M0L16pending__weightsS1910 = _M0L1cS777->$9;
        _M0L3valS1911 = _M0L1kS779->$0;
        #line 322 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
        _M0L6_2atmpS1909
        = _M0MPC15array5Array2atGfE(_M0L16pending__weightsS1910, _M0L3valS1911);
        #line 322 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
        _M0FP26RiantR8snn__mbt13apply__weight(_M0L1cS777, _M0L6_2atmpS1908, _M0L6_2atmpS1909);
      } else {
        int32_t _M0L3valS1914 = _M0L4keptS778->$0;
        int32_t _M0L3valS1915 = _M0L1kS779->$0;
        int32_t _M0L3valS1932;
        int32_t _M0L6_2atmpS1931;
        if (_M0L3valS1914 != _M0L3valS1915) {
          struct _M0TPB5ArrayGfE* _M0L14pending__timesS1916 = _M0L1cS777->$7;
          int32_t _M0L3valS1917 = _M0L4keptS778->$0;
          struct _M0TPB5ArrayGfE* _M0L14pending__timesS1919 = _M0L1cS777->$7;
          int32_t _M0L3valS1920 = _M0L1kS779->$0;
          float _M0L6_2atmpS1918;
          struct _M0TPB5ArrayGiE* _M0L14pending__postsS1921;
          int32_t _M0L3valS1922;
          struct _M0TPB5ArrayGiE* _M0L14pending__postsS1924;
          int32_t _M0L3valS1925;
          int32_t _M0L6_2atmpS1923;
          struct _M0TPB5ArrayGfE* _M0L16pending__weightsS1926;
          int32_t _M0L3valS1927;
          struct _M0TPB5ArrayGfE* _M0L16pending__weightsS1929;
          int32_t _M0L3valS1930;
          float _M0L6_2atmpS1928;
          #line 326 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
          _M0L6_2atmpS1918
          = _M0MPC15array5Array2atGfE(_M0L14pending__timesS1919, _M0L3valS1920);
          #line 326 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
          _M0MPC15array5Array3setGfE(_M0L14pending__timesS1916, _M0L3valS1917, _M0L6_2atmpS1918);
          _M0L14pending__postsS1921 = _M0L1cS777->$8;
          _M0L3valS1922 = _M0L4keptS778->$0;
          _M0L14pending__postsS1924 = _M0L1cS777->$8;
          _M0L3valS1925 = _M0L1kS779->$0;
          #line 327 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
          _M0L6_2atmpS1923
          = _M0MPC15array5Array2atGiE(_M0L14pending__postsS1924, _M0L3valS1925);
          #line 327 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
          _M0MPC15array5Array3setGiE(_M0L14pending__postsS1921, _M0L3valS1922, _M0L6_2atmpS1923);
          _M0L16pending__weightsS1926 = _M0L1cS777->$9;
          _M0L3valS1927 = _M0L4keptS778->$0;
          _M0L16pending__weightsS1929 = _M0L1cS777->$9;
          _M0L3valS1930 = _M0L1kS779->$0;
          #line 328 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
          _M0L6_2atmpS1928
          = _M0MPC15array5Array2atGfE(_M0L16pending__weightsS1929, _M0L3valS1930);
          #line 328 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
          _M0MPC15array5Array3setGfE(_M0L16pending__weightsS1926, _M0L3valS1927, _M0L6_2atmpS1928);
        }
        _M0L3valS1932 = _M0L4keptS778->$0;
        _M0L6_2atmpS1931 = _M0L3valS1932 + 1;
        _M0L4keptS778->$0 = _M0L6_2atmpS1931;
      }
      _M0L3valS1934 = _M0L1kS779->$0;
      _M0L6_2atmpS1933 = _M0L3valS1934 + 1;
      _M0L1kS779->$0 = _M0L6_2atmpS1933;
      continue;
    } else {
      moonbit_decref(_M0L1kS779);
    }
    break;
  }
  _M0L3valS1942 = _M0L4keptS778->$0;
  moonbit_decref(_M0L4keptS778);
  _M0L6_2atmpS1941 = _M0L1nS776 - _M0L3valS1942;
  _M0L4dropS782
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L4dropS782)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L4dropS782->$0 = _M0L6_2atmpS1941;
  while (1) {
    int32_t _M0L3valS1935 = _M0L4dropS782->$0;
    if (_M0L3valS1935 > 0) {
      struct _M0TPB5ArrayGfE* _M0L14pending__timesS1936 = _M0L1cS777->$7;
      void* _M0L6_2atmpS2049;
      struct _M0TPB5ArrayGiE* _M0L14pending__postsS1937;
      struct _M0TPB5ArrayGfE* _M0L16pending__weightsS1938;
      void* _M0L6_2atmpS2048;
      int32_t _M0L3valS1940;
      int32_t _M0L6_2atmpS1939;
      #line 337 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
      _M0L6_2atmpS2049
      = _M0MPC15array5Array3popGfE(_M0L14pending__timesS1936);
      moonbit_decref(_M0L6_2atmpS2049);
      _M0L14pending__postsS1937 = _M0L1cS777->$8;
      #line 338 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
      _M0MPC15array5Array3popGiE(_M0L14pending__postsS1937);
      _M0L16pending__weightsS1938 = _M0L1cS777->$9;
      #line 339 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
      _M0L6_2atmpS2048
      = _M0MPC15array5Array3popGfE(_M0L16pending__weightsS1938);
      moonbit_decref(_M0L6_2atmpS2048);
      _M0L3valS1940 = _M0L4dropS782->$0;
      _M0L6_2atmpS1939 = _M0L3valS1940 - 1;
      _M0L4dropS782->$0 = _M0L6_2atmpS1939;
      continue;
    } else {
      moonbit_decref(_M0L4dropS782);
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt13apply__weight(
  struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0L1cS773,
  int32_t _M0L9post__idxS774,
  float _M0L1wS775
) {
  moonbit_string_t _M0L3symS1891;
  #line 346 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
  _M0L3symS1891 = _M0L1cS773->$2;
  #line 347 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
  if (
    _M0L3symS1891 == (moonbit_string_t)moonbit_string_literal_1.data
    || Moonbit_array_length(_M0L3symS1891)
       == Moonbit_array_length((moonbit_string_t)moonbit_string_literal_1.data)
       && 0
          == memcmp(_M0L3symS1891, (moonbit_string_t)moonbit_string_literal_1.data, Moonbit_array_length(_M0L3symS1891) * 2)
  ) {
    struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS1897 = _M0L1cS773->$1;
    struct _M0TPB5ArrayGfE* _M0L3gluS1892 = _M0L4postS1897->$13;
    struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS1896 = _M0L1cS773->$1;
    struct _M0TPB5ArrayGfE* _M0L3gluS1895 = _M0L4postS1896->$13;
    float _M0L6_2atmpS1894;
    float _M0L6_2atmpS1893;
    #line 348 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
    _M0L6_2atmpS1894
    = _M0MPC15array5Array2atGfE(_M0L3gluS1895, _M0L9post__idxS774);
    _M0L6_2atmpS1893 = _M0L6_2atmpS1894 + _M0L1wS775;
    #line 348 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
    _M0MPC15array5Array3setGfE(_M0L3gluS1892, _M0L9post__idxS774, _M0L6_2atmpS1893);
  } else {
    struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS1903 = _M0L1cS773->$1;
    struct _M0TPB5ArrayGfE* _M0L4gabaS1898 = _M0L4postS1903->$14;
    struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS1902 = _M0L1cS773->$1;
    struct _M0TPB5ArrayGfE* _M0L4gabaS1901 = _M0L4postS1902->$14;
    float _M0L6_2atmpS1900;
    float _M0L6_2atmpS1899;
    #line 350 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
    _M0L6_2atmpS1900
    = _M0MPC15array5Array2atGfE(_M0L4gabaS1901, _M0L9post__idxS774);
    _M0L6_2atmpS1899 = _M0L6_2atmpS1900 + _M0L1wS775;
    #line 350 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
    _M0MPC15array5Array3setGfE(_M0L4gabaS1898, _M0L9post__idxS774, _M0L6_2atmpS1899);
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt12record__zero(
  struct _M0TP26RiantR8snn__mbt5Model* _M0L5modelS767
) {
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt7MonitorE* _M0L7_2abindS766;
  int32_t _M0L7_2abindS768;
  struct _M0TP26RiantR8snn__mbt7Monitor** _M0L7_2abindS769;
  int32_t _M0L2__S770;
  #line 87 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
  _M0L7_2abindS766 = _M0L5modelS767->$2;
  _M0L7_2abindS768 = _M0L7_2abindS766->$1;
  _M0L7_2abindS769 = _M0L7_2abindS766->$0;
  moonbit_incref(_M0L7_2abindS769);
  _M0L2__S770 = 0;
  while (1) {
    if (_M0L2__S770 < _M0L7_2abindS768) {
      struct _M0TP26RiantR8snn__mbt7Monitor* _M0L1mS771 =
        (struct _M0TP26RiantR8snn__mbt7Monitor*)_M0L7_2abindS769[_M0L2__S770];
      int32_t _M0L6_2atmpS1890;
      moonbit_incref(_M0L1mS771);
      #line 89 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
      _M0FP26RiantR8snn__mbt11record__one(_M0L1mS771, 0x0p+0f);
      moonbit_decref(_M0L1mS771);
      _M0L6_2atmpS1890 = _M0L2__S770 + 1;
      _M0L2__S770 = _M0L6_2atmpS1890;
      continue;
    } else {
      moonbit_decref(_M0L7_2abindS769);
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt11record__one(
  struct _M0TP26RiantR8snn__mbt7Monitor* _M0L1mS763,
  float _M0L1tS765
) {
  int32_t _M0L11step__countS1876;
  int32_t _M0L6_2atmpS1875;
  int32_t _M0L11step__countS1878;
  int32_t _M0L9rec__stepS1879;
  int32_t _M0L6_2atmpS1877;
  moonbit_string_t _M0L3symS1882;
  float _M0L1vS764;
  struct _M0TPB5ArrayGfE* _M0L4dataS1880;
  struct _M0TPB5ArrayGfE* _M0L5timesS1881;
  #line 61 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
  _M0L11step__countS1876 = _M0L1mS763->$6;
  _M0L6_2atmpS1875 = _M0L11step__countS1876 + 1;
  _M0L1mS763->$6 = _M0L6_2atmpS1875;
  _M0L11step__countS1878 = _M0L1mS763->$6;
  _M0L9rec__stepS1879 = _M0L1mS763->$5;
  _M0L6_2atmpS1877 = _M0L11step__countS1878 % _M0L9rec__stepS1879;
  if (_M0L6_2atmpS1877 != 0) {
    return 0;
  }
  _M0L3symS1882 = _M0L1mS763->$1;
  #line 66 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
  if (
    _M0L3symS1882 == (moonbit_string_t)moonbit_string_literal_2.data
    || Moonbit_array_length(_M0L3symS1882)
       == Moonbit_array_length((moonbit_string_t)moonbit_string_literal_2.data)
       && 0
          == memcmp(_M0L3symS1882, (moonbit_string_t)moonbit_string_literal_2.data, Moonbit_array_length(_M0L3symS1882) * 2)
  ) {
    struct _M0TP26RiantR8snn__mbt2IF* _M0L3popS1885 = _M0L1mS763->$0;
    struct _M0TPB5ArrayGfE* _M0L1vS1883 = _M0L3popS1885->$3;
    int32_t _M0L6neuronS1884 = _M0L1mS763->$4;
    #line 67 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
    _M0L1vS764 = _M0MPC15array5Array2atGfE(_M0L1vS1883, _M0L6neuronS1884);
  } else {
    moonbit_string_t _M0L3symS1886 = _M0L1mS763->$1;
    #line 68 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
    if (
      _M0L3symS1886 == (moonbit_string_t)moonbit_string_literal_3.data
      || Moonbit_array_length(_M0L3symS1886)
         == Moonbit_array_length((moonbit_string_t)moonbit_string_literal_3.data)
         && 0
            == memcmp(_M0L3symS1886, (moonbit_string_t)moonbit_string_literal_3.data, Moonbit_array_length(_M0L3symS1886) * 2)
    ) {
      struct _M0TP26RiantR8snn__mbt2IF* _M0L3popS1889 = _M0L1mS763->$0;
      struct _M0TPB5ArrayGbE* _M0L4fireS1887 = _M0L3popS1889->$5;
      int32_t _M0L6neuronS1888 = _M0L1mS763->$4;
      #line 69 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS1887, _M0L6neuronS1888)) {
        _M0L1vS764 = 0x1p+0f;
      } else {
        _M0L1vS764 = 0x0p+0f;
      }
    } else {
      _M0L1vS764 = 0x0p+0f;
    }
  }
  _M0L4dataS1880 = _M0L1mS763->$2;
  #line 73 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
  _M0MPC15array5Array4pushGfE(_M0L4dataS1880, _M0L1vS764);
  _M0L5timesS1881 = _M0L1mS763->$3;
  #line 74 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
  _M0MPC15array5Array4pushGfE(_M0L5timesS1881, _M0L1tS765);
  return 0;
}

struct _M0TP26RiantR8snn__mbt7Monitor* _M0MP26RiantR8snn__mbt7Monitor6new__v(
  struct _M0TP26RiantR8snn__mbt2IF* _M0L3popS761,
  int32_t _M0L6neuronS762
) {
  float* _M0L6_2atmpS1874;
  struct _M0TPB5ArrayGfE* _M0L6_2atmpS1871;
  float* _M0L6_2atmpS1873;
  struct _M0TPB5ArrayGfE* _M0L6_2atmpS1872;
  struct _M0TP26RiantR8snn__mbt7Monitor* _block_2102;
  #line 33 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
  _M0L6_2atmpS1874 = moonbit_empty_float_array;
  _M0L6_2atmpS1871
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_M0L6_2atmpS1871)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 0, 0);
  _M0L6_2atmpS1871->$0 = _M0L6_2atmpS1874;
  _M0L6_2atmpS1871->$1 = 0;
  _M0L6_2atmpS1873 = moonbit_empty_float_array;
  _M0L6_2atmpS1872
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_M0L6_2atmpS1872)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 0, 0);
  _M0L6_2atmpS1872->$0 = _M0L6_2atmpS1873;
  _M0L6_2atmpS1872->$1 = 0;
  moonbit_incref(_M0L3popS761);
  _block_2102
  = (struct _M0TP26RiantR8snn__mbt7Monitor*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt7Monitor));
  Moonbit_object_header(_block_2102)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 36, 0);
  _block_2102->$0 = _M0L3popS761;
  _block_2102->$1 = (moonbit_string_t)moonbit_string_literal_2.data;
  _block_2102->$2 = _M0L6_2atmpS1871;
  _block_2102->$3 = _M0L6_2atmpS1872;
  _block_2102->$4 = _M0L6neuronS762;
  _block_2102->$5 = 1;
  _block_2102->$6 = 0;
  return _block_2102;
}

int32_t _M0FP26RiantR8snn__mbt17synaptic__current(
  struct _M0TP26RiantR8snn__mbt2IF* _M0L1pS757
) {
  int32_t _M0L1nS756;
  int32_t _M0L7_2abindS758;
  int32_t _M0L1iS759;
  #line 165 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L1nS756 = _M0L1pS757->$2;
  _M0L7_2abindS758 = 0;
  _M0L1iS759 = _M0L7_2abindS758;
  while (1) {
    if (_M0L1iS759 < _M0L1nS756) {
      struct _M0TPB5ArrayGfE* _M0L9syn__currS1848 = _M0L1pS757->$8;
      struct _M0TPB5ArrayGfE* _M0L2geS1869 = _M0L1pS757->$9;
      float _M0L6_2atmpS1864;
      struct _M0TPB5ArrayGfE* _M0L1vS1868;
      float _M0L6_2atmpS1866;
      float _M0L4e__eS1867;
      float _M0L6_2atmpS1865;
      float _M0L6_2atmpS1861;
      struct _M0TPB5ArrayGfE* _M0L7gsyn__eS1863;
      float _M0L6_2atmpS1862;
      float _M0L6_2atmpS1850;
      struct _M0TPB5ArrayGfE* _M0L2giS1860;
      float _M0L6_2atmpS1855;
      struct _M0TPB5ArrayGfE* _M0L1vS1859;
      float _M0L6_2atmpS1857;
      float _M0L4e__iS1858;
      float _M0L6_2atmpS1856;
      float _M0L6_2atmpS1852;
      struct _M0TPB5ArrayGfE* _M0L7gsyn__iS1854;
      float _M0L6_2atmpS1853;
      float _M0L6_2atmpS1851;
      float _M0L6_2atmpS1849;
      int32_t _M0L6_2atmpS1870;
      #line 168 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1864 = _M0MPC15array5Array2atGfE(_M0L2geS1869, _M0L1iS759);
      _M0L1vS1868 = _M0L1pS757->$3;
      #line 168 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1866 = _M0MPC15array5Array2atGfE(_M0L1vS1868, _M0L1iS759);
      _M0L4e__eS1867 = _M0L1pS757->$17;
      _M0L6_2atmpS1865 = _M0L6_2atmpS1866 - _M0L4e__eS1867;
      _M0L6_2atmpS1861 = _M0L6_2atmpS1864 * _M0L6_2atmpS1865;
      _M0L7gsyn__eS1863 = _M0L1pS757->$15;
      #line 168 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1862
      = _M0MPC15array5Array2atGfE(_M0L7gsyn__eS1863, _M0L1iS759);
      _M0L6_2atmpS1850 = _M0L6_2atmpS1861 * _M0L6_2atmpS1862;
      _M0L2giS1860 = _M0L1pS757->$10;
      #line 169 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1855 = _M0MPC15array5Array2atGfE(_M0L2giS1860, _M0L1iS759);
      _M0L1vS1859 = _M0L1pS757->$3;
      #line 169 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1857 = _M0MPC15array5Array2atGfE(_M0L1vS1859, _M0L1iS759);
      _M0L4e__iS1858 = _M0L1pS757->$18;
      _M0L6_2atmpS1856 = _M0L6_2atmpS1857 - _M0L4e__iS1858;
      _M0L6_2atmpS1852 = _M0L6_2atmpS1855 * _M0L6_2atmpS1856;
      _M0L7gsyn__iS1854 = _M0L1pS757->$16;
      #line 169 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1853
      = _M0MPC15array5Array2atGfE(_M0L7gsyn__iS1854, _M0L1iS759);
      _M0L6_2atmpS1851 = _M0L6_2atmpS1852 * _M0L6_2atmpS1853;
      _M0L6_2atmpS1849 = _M0L6_2atmpS1850 + _M0L6_2atmpS1851;
      #line 168 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L9syn__currS1848, _M0L1iS759, _M0L6_2atmpS1849);
      _M0L6_2atmpS1870 = _M0L1iS759 + 1;
      _M0L1iS759 = _M0L6_2atmpS1870;
      continue;
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt14step__synapses(
  struct _M0TP26RiantR8snn__mbt2IF* _M0L1pS748,
  float _M0L2dtS751
) {
  int32_t _M0L1nS747;
  int32_t _M0L7_2abindS749;
  int32_t _M0L1iS750;
  int32_t _M0L7_2abindS753;
  int32_t _M0L1iS754;
  #line 146 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L1nS747 = _M0L1pS748->$2;
  _M0L7_2abindS749 = 0;
  _M0L1iS750 = _M0L7_2abindS749;
  while (1) {
    if (_M0L1iS750 < _M0L1nS747) {
      struct _M0TPB5ArrayGfE* _M0L2heS1786 = _M0L1pS748->$11;
      struct _M0TPB5ArrayGfE* _M0L2heS1791 = _M0L1pS748->$11;
      float _M0L6_2atmpS1788;
      struct _M0TPB5ArrayGfE* _M0L3gluS1790;
      float _M0L6_2atmpS1789;
      float _M0L6_2atmpS1787;
      struct _M0TPB5ArrayGfE* _M0L2hiS1792;
      struct _M0TPB5ArrayGfE* _M0L2hiS1797;
      float _M0L6_2atmpS1794;
      struct _M0TPB5ArrayGfE* _M0L4gabaS1796;
      float _M0L6_2atmpS1795;
      float _M0L6_2atmpS1793;
      struct _M0TPB5ArrayGfE* _M0L2geS1798;
      struct _M0TPB5ArrayGfE* _M0L2geS1810;
      float _M0L6_2atmpS1800;
      struct _M0TPB5ArrayGfE* _M0L2geS1809;
      float _M0L6_2atmpS1808;
      float _M0L6_2atmpS1806;
      float _M0L3tdeS1807;
      float _M0L6_2atmpS1803;
      struct _M0TPB5ArrayGfE* _M0L2heS1805;
      float _M0L6_2atmpS1804;
      float _M0L6_2atmpS1802;
      float _M0L6_2atmpS1801;
      float _M0L6_2atmpS1799;
      struct _M0TPB5ArrayGfE* _M0L2heS1811;
      struct _M0TPB5ArrayGfE* _M0L2heS1820;
      float _M0L6_2atmpS1813;
      struct _M0TPB5ArrayGfE* _M0L2heS1819;
      float _M0L6_2atmpS1818;
      float _M0L6_2atmpS1816;
      float _M0L3treS1817;
      float _M0L6_2atmpS1815;
      float _M0L6_2atmpS1814;
      float _M0L6_2atmpS1812;
      struct _M0TPB5ArrayGfE* _M0L2giS1821;
      struct _M0TPB5ArrayGfE* _M0L2giS1833;
      float _M0L6_2atmpS1823;
      struct _M0TPB5ArrayGfE* _M0L2giS1832;
      float _M0L6_2atmpS1831;
      float _M0L6_2atmpS1829;
      float _M0L3tdiS1830;
      float _M0L6_2atmpS1826;
      struct _M0TPB5ArrayGfE* _M0L2hiS1828;
      float _M0L6_2atmpS1827;
      float _M0L6_2atmpS1825;
      float _M0L6_2atmpS1824;
      float _M0L6_2atmpS1822;
      struct _M0TPB5ArrayGfE* _M0L2hiS1834;
      struct _M0TPB5ArrayGfE* _M0L2hiS1843;
      float _M0L6_2atmpS1836;
      struct _M0TPB5ArrayGfE* _M0L2hiS1842;
      float _M0L6_2atmpS1841;
      float _M0L6_2atmpS1839;
      float _M0L3triS1840;
      float _M0L6_2atmpS1838;
      float _M0L6_2atmpS1837;
      float _M0L6_2atmpS1835;
      int32_t _M0L6_2atmpS1844;
      #line 149 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1788 = _M0MPC15array5Array2atGfE(_M0L2heS1791, _M0L1iS750);
      _M0L3gluS1790 = _M0L1pS748->$13;
      #line 149 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1789 = _M0MPC15array5Array2atGfE(_M0L3gluS1790, _M0L1iS750);
      _M0L6_2atmpS1787 = _M0L6_2atmpS1788 + _M0L6_2atmpS1789;
      #line 149 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L2heS1786, _M0L1iS750, _M0L6_2atmpS1787);
      _M0L2hiS1792 = _M0L1pS748->$12;
      _M0L2hiS1797 = _M0L1pS748->$12;
      #line 150 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1794 = _M0MPC15array5Array2atGfE(_M0L2hiS1797, _M0L1iS750);
      _M0L4gabaS1796 = _M0L1pS748->$14;
      #line 150 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1795
      = _M0MPC15array5Array2atGfE(_M0L4gabaS1796, _M0L1iS750);
      _M0L6_2atmpS1793 = _M0L6_2atmpS1794 + _M0L6_2atmpS1795;
      #line 150 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L2hiS1792, _M0L1iS750, _M0L6_2atmpS1793);
      _M0L2geS1798 = _M0L1pS748->$9;
      _M0L2geS1810 = _M0L1pS748->$9;
      #line 151 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1800 = _M0MPC15array5Array2atGfE(_M0L2geS1810, _M0L1iS750);
      _M0L2geS1809 = _M0L1pS748->$9;
      #line 151 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1808 = _M0MPC15array5Array2atGfE(_M0L2geS1809, _M0L1iS750);
      _M0L6_2atmpS1806 = -_M0L6_2atmpS1808;
      _M0L3tdeS1807 = _M0L1pS748->$20;
      _M0L6_2atmpS1803 = _M0L6_2atmpS1806 / _M0L3tdeS1807;
      _M0L2heS1805 = _M0L1pS748->$11;
      #line 151 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1804 = _M0MPC15array5Array2atGfE(_M0L2heS1805, _M0L1iS750);
      _M0L6_2atmpS1802 = _M0L6_2atmpS1803 + _M0L6_2atmpS1804;
      _M0L6_2atmpS1801 = _M0L2dtS751 * _M0L6_2atmpS1802;
      _M0L6_2atmpS1799 = _M0L6_2atmpS1800 + _M0L6_2atmpS1801;
      #line 151 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L2geS1798, _M0L1iS750, _M0L6_2atmpS1799);
      _M0L2heS1811 = _M0L1pS748->$11;
      _M0L2heS1820 = _M0L1pS748->$11;
      #line 152 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1813 = _M0MPC15array5Array2atGfE(_M0L2heS1820, _M0L1iS750);
      _M0L2heS1819 = _M0L1pS748->$11;
      #line 152 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1818 = _M0MPC15array5Array2atGfE(_M0L2heS1819, _M0L1iS750);
      _M0L6_2atmpS1816 = -_M0L6_2atmpS1818;
      _M0L3treS1817 = _M0L1pS748->$19;
      _M0L6_2atmpS1815 = _M0L6_2atmpS1816 / _M0L3treS1817;
      _M0L6_2atmpS1814 = _M0L2dtS751 * _M0L6_2atmpS1815;
      _M0L6_2atmpS1812 = _M0L6_2atmpS1813 + _M0L6_2atmpS1814;
      #line 152 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L2heS1811, _M0L1iS750, _M0L6_2atmpS1812);
      _M0L2giS1821 = _M0L1pS748->$10;
      _M0L2giS1833 = _M0L1pS748->$10;
      #line 153 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1823 = _M0MPC15array5Array2atGfE(_M0L2giS1833, _M0L1iS750);
      _M0L2giS1832 = _M0L1pS748->$10;
      #line 153 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1831 = _M0MPC15array5Array2atGfE(_M0L2giS1832, _M0L1iS750);
      _M0L6_2atmpS1829 = -_M0L6_2atmpS1831;
      _M0L3tdiS1830 = _M0L1pS748->$22;
      _M0L6_2atmpS1826 = _M0L6_2atmpS1829 / _M0L3tdiS1830;
      _M0L2hiS1828 = _M0L1pS748->$12;
      #line 153 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1827 = _M0MPC15array5Array2atGfE(_M0L2hiS1828, _M0L1iS750);
      _M0L6_2atmpS1825 = _M0L6_2atmpS1826 + _M0L6_2atmpS1827;
      _M0L6_2atmpS1824 = _M0L2dtS751 * _M0L6_2atmpS1825;
      _M0L6_2atmpS1822 = _M0L6_2atmpS1823 + _M0L6_2atmpS1824;
      #line 153 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L2giS1821, _M0L1iS750, _M0L6_2atmpS1822);
      _M0L2hiS1834 = _M0L1pS748->$12;
      _M0L2hiS1843 = _M0L1pS748->$12;
      #line 154 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1836 = _M0MPC15array5Array2atGfE(_M0L2hiS1843, _M0L1iS750);
      _M0L2hiS1842 = _M0L1pS748->$12;
      #line 154 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1841 = _M0MPC15array5Array2atGfE(_M0L2hiS1842, _M0L1iS750);
      _M0L6_2atmpS1839 = -_M0L6_2atmpS1841;
      _M0L3triS1840 = _M0L1pS748->$21;
      _M0L6_2atmpS1838 = _M0L6_2atmpS1839 / _M0L3triS1840;
      _M0L6_2atmpS1837 = _M0L2dtS751 * _M0L6_2atmpS1838;
      _M0L6_2atmpS1835 = _M0L6_2atmpS1836 + _M0L6_2atmpS1837;
      #line 154 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L2hiS1834, _M0L1iS750, _M0L6_2atmpS1835);
      _M0L6_2atmpS1844 = _M0L1iS750 + 1;
      _M0L1iS750 = _M0L6_2atmpS1844;
      continue;
    }
    break;
  }
  _M0L7_2abindS753 = 0;
  _M0L1iS754 = _M0L7_2abindS753;
  while (1) {
    if (_M0L1iS754 < _M0L1nS747) {
      struct _M0TPB5ArrayGfE* _M0L3gluS1845 = _M0L1pS748->$13;
      struct _M0TPB5ArrayGfE* _M0L4gabaS1846;
      int32_t _M0L6_2atmpS1847;
      #line 157 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L3gluS1845, _M0L1iS754, 0x0p+0f);
      _M0L4gabaS1846 = _M0L1pS748->$14;
      #line 158 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L4gabaS1846, _M0L1iS754, 0x0p+0f);
      _M0L6_2atmpS1847 = _M0L1iS754 + 1;
      _M0L1iS754 = _M0L6_2atmpS1847;
      continue;
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt12step__neuron(
  struct _M0TP26RiantR8snn__mbt2IF* _M0L1pS733,
  float _M0L2dtS742
) {
  int32_t _M0L1nS732;
  struct _M0TP26RiantR8snn__mbt11IFParameter* _M0L3p__S734;
  float _M0L2tmS735;
  float _M0L2elS736;
  float _M0L1rS737;
  float _M0L2vtS738;
  float _M0L2vrS739;
  struct _M0TP26RiantR8snn__mbt9PostSpike* _M0L5spikeS1785;
  float _M0L11tabs__constS740;
  float _M0L6_2atmpS1784;
  int32_t _M0L11tabs__stepsS741;
  int32_t _M0L7_2abindS743;
  int32_t _M0L1iS744;
  #line 176 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L1nS732 = _M0L1pS733->$2;
  _M0L3p__S734 = _M0L1pS733->$0;
  _M0L2tmS735 = _M0L3p__S734->$2;
  _M0L2elS736 = _M0L3p__S734->$5;
  _M0L1rS737 = _M0L3p__S734->$6;
  _M0L2vtS738 = _M0L3p__S734->$3;
  _M0L2vrS739 = _M0L3p__S734->$4;
  _M0L5spikeS1785 = _M0L1pS733->$1;
  _M0L11tabs__constS740 = _M0L5spikeS1785->$0;
  _M0L6_2atmpS1784 = _M0L11tabs__constS740 / _M0L2dtS742;
  #line 185 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L11tabs__stepsS741 = _M0MPC15float5Float7to__int(_M0L6_2atmpS1784);
  _M0L7_2abindS743 = 0;
  _M0L1iS744 = _M0L7_2abindS743;
  while (1) {
    if (_M0L1iS744 < _M0L1nS732) {
      struct _M0TPB5ArrayGiE* _M0L4tabsS1744 = _M0L1pS733->$6;
      int32_t _M0L6_2atmpS1743;
      struct _M0TPB5ArrayGfE* _M0L1vS1750;
      struct _M0TPB5ArrayGfE* _M0L1vS1771;
      float _M0L6_2atmpS1752;
      float _M0L6_2atmpS1754;
      struct _M0TPB5ArrayGfE* _M0L1vS1770;
      float _M0L6_2atmpS1769;
      float _M0L6_2atmpS1768;
      float _M0L6_2atmpS1760;
      struct _M0TPB5ArrayGfE* _M0L1wS1767;
      float _M0L6_2atmpS1766;
      float _M0L6_2atmpS1763;
      struct _M0TPB5ArrayGfE* _M0L1iS1765;
      float _M0L6_2atmpS1764;
      float _M0L6_2atmpS1762;
      float _M0L6_2atmpS1761;
      float _M0L6_2atmpS1756;
      struct _M0TPB5ArrayGfE* _M0L9syn__currS1759;
      float _M0L6_2atmpS1758;
      float _M0L6_2atmpS1757;
      float _M0L6_2atmpS1755;
      float _M0L6_2atmpS1753;
      float _M0L6_2atmpS1751;
      struct _M0TPB5ArrayGbE* _M0L4fireS1772;
      struct _M0TPB5ArrayGfE* _M0L1vS1775;
      float _M0L6_2atmpS1774;
      int32_t _M0L6_2atmpS1773;
      struct _M0TPB5ArrayGfE* _M0L1vS1776;
      struct _M0TPB5ArrayGbE* _M0L4fireS1778;
      float _M0L6_2atmpS1777;
      struct _M0TPB5ArrayGiE* _M0L4tabsS1780;
      struct _M0TPB5ArrayGbE* _M0L4fireS1782;
      int32_t _M0L6_2atmpS1781;
      int32_t _M0L6_2atmpS1742;
      #line 188 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1743
      = _M0MPC15array5Array2atGiE(_M0L4tabsS1744, _M0L1iS744);
      if (_M0L6_2atmpS1743 > 0) {
        struct _M0TPB5ArrayGbE* _M0L4fireS1745 = _M0L1pS733->$5;
        struct _M0TPB5ArrayGiE* _M0L4tabsS1746;
        struct _M0TPB5ArrayGiE* _M0L4tabsS1749;
        int32_t _M0L6_2atmpS1748;
        int32_t _M0L6_2atmpS1747;
        #line 189 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
        _M0MPC15array5Array3setGbE(_M0L4fireS1745, _M0L1iS744, 0);
        _M0L4tabsS1746 = _M0L1pS733->$6;
        _M0L4tabsS1749 = _M0L1pS733->$6;
        #line 190 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
        _M0L6_2atmpS1748
        = _M0MPC15array5Array2atGiE(_M0L4tabsS1749, _M0L1iS744);
        _M0L6_2atmpS1747 = _M0L6_2atmpS1748 - 1;
        #line 190 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
        _M0MPC15array5Array3setGiE(_M0L4tabsS1746, _M0L1iS744, _M0L6_2atmpS1747);
        goto join_745;
      }
      _M0L1vS1750 = _M0L1pS733->$3;
      _M0L1vS1771 = _M0L1pS733->$3;
      #line 193 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1752 = _M0MPC15array5Array2atGfE(_M0L1vS1771, _M0L1iS744);
      _M0L6_2atmpS1754 = _M0L2dtS742 / _M0L2tmS735;
      _M0L1vS1770 = _M0L1pS733->$3;
      #line 194 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1769 = _M0MPC15array5Array2atGfE(_M0L1vS1770, _M0L1iS744);
      _M0L6_2atmpS1768 = _M0L6_2atmpS1769 - _M0L2elS736;
      _M0L6_2atmpS1760 = -_M0L6_2atmpS1768;
      _M0L1wS1767 = _M0L1pS733->$4;
      #line 194 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1766 = _M0MPC15array5Array2atGfE(_M0L1wS1767, _M0L1iS744);
      _M0L6_2atmpS1763 = -_M0L6_2atmpS1766;
      _M0L1iS1765 = _M0L1pS733->$7;
      #line 194 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1764 = _M0MPC15array5Array2atGfE(_M0L1iS1765, _M0L1iS744);
      _M0L6_2atmpS1762 = _M0L6_2atmpS1763 + _M0L6_2atmpS1764;
      _M0L6_2atmpS1761 = _M0L1rS737 * _M0L6_2atmpS1762;
      _M0L6_2atmpS1756 = _M0L6_2atmpS1760 + _M0L6_2atmpS1761;
      _M0L9syn__currS1759 = _M0L1pS733->$8;
      #line 194 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1758
      = _M0MPC15array5Array2atGfE(_M0L9syn__currS1759, _M0L1iS744);
      _M0L6_2atmpS1757 = _M0L1rS737 * _M0L6_2atmpS1758;
      _M0L6_2atmpS1755 = _M0L6_2atmpS1756 - _M0L6_2atmpS1757;
      _M0L6_2atmpS1753 = _M0L6_2atmpS1754 * _M0L6_2atmpS1755;
      _M0L6_2atmpS1751 = _M0L6_2atmpS1752 + _M0L6_2atmpS1753;
      #line 193 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS1750, _M0L1iS744, _M0L6_2atmpS1751);
      _M0L4fireS1772 = _M0L1pS733->$5;
      _M0L1vS1775 = _M0L1pS733->$3;
      #line 195 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1774 = _M0MPC15array5Array2atGfE(_M0L1vS1775, _M0L1iS744);
      _M0L6_2atmpS1773 = _M0L6_2atmpS1774 > _M0L2vtS738;
      #line 195 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGbE(_M0L4fireS1772, _M0L1iS744, _M0L6_2atmpS1773);
      _M0L1vS1776 = _M0L1pS733->$3;
      _M0L4fireS1778 = _M0L1pS733->$5;
      #line 196 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS1778, _M0L1iS744)) {
        _M0L6_2atmpS1777 = _M0L2vrS739;
      } else {
        struct _M0TPB5ArrayGfE* _M0L1vS1779 = _M0L1pS733->$3;
        #line 196 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
        _M0L6_2atmpS1777 = _M0MPC15array5Array2atGfE(_M0L1vS1779, _M0L1iS744);
      }
      #line 196 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS1776, _M0L1iS744, _M0L6_2atmpS1777);
      _M0L4tabsS1780 = _M0L1pS733->$6;
      _M0L4fireS1782 = _M0L1pS733->$5;
      #line 197 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS1782, _M0L1iS744)) {
        _M0L6_2atmpS1781 = _M0L11tabs__stepsS741;
      } else {
        struct _M0TPB5ArrayGiE* _M0L4tabsS1783 = _M0L1pS733->$6;
        #line 197 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
        _M0L6_2atmpS1781
        = _M0MPC15array5Array2atGiE(_M0L4tabsS1783, _M0L1iS744);
      }
      #line 197 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGiE(_M0L4tabsS1780, _M0L1iS744, _M0L6_2atmpS1781);
      goto join_745;
      goto joinlet_2107;
      join_745:;
      _M0L6_2atmpS1742 = _M0L1iS744 + 1;
      _M0L1iS744 = _M0L6_2atmpS1742;
      continue;
      joinlet_2107:;
    }
    break;
  }
  return 0;
}

int32_t _M0MP26RiantR8snn__mbt15SparseMatrixCSR7forward(
  struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L1mS720,
  struct _M0TPB5ArrayGbE* _M0L9pre__fireS723,
  struct _M0TPB5ArrayGfE* _M0L7post__gS729
) {
  int32_t _M0L4rowsS719;
  int32_t _M0L7_2abindS721;
  int32_t _M0L1iS722;
  #line 287 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  _M0L4rowsS719 = _M0L1mS720->$0;
  _M0L7_2abindS721 = 0;
  _M0L1iS722 = _M0L7_2abindS721;
  while (1) {
    if (_M0L1iS722 < _M0L4rowsS719) {
      int32_t _M0L6_2atmpS1741;
      #line 294 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L9pre__fireS723, _M0L1iS722)) {
        struct _M0TPB5ArrayGiE* _M0L6rowptrS1740 = _M0L1mS720->$2;
        int32_t _M0L5startS724;
        struct _M0TPB5ArrayGiE* _M0L6rowptrS1738;
        int32_t _M0L6_2atmpS1739;
        int32_t _M0L3endS725;
        int32_t _M0L1kS726;
        #line 295 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
        _M0L5startS724
        = _M0MPC15array5Array2atGiE(_M0L6rowptrS1740, _M0L1iS722);
        _M0L6rowptrS1738 = _M0L1mS720->$2;
        _M0L6_2atmpS1739 = _M0L1iS722 + 1;
        #line 296 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
        _M0L3endS725
        = _M0MPC15array5Array2atGiE(_M0L6rowptrS1738, _M0L6_2atmpS1739);
        _M0L1kS726 = _M0L5startS724;
        while (1) {
          if (_M0L1kS726 < _M0L3endS725) {
            struct _M0TPB5ArrayGiE* _M0L6colptrS1736 = _M0L1mS720->$3;
            int32_t _M0L9post__idxS727;
            struct _M0TPB5ArrayGfE* _M0L4valsS1735;
            float _M0L1wS728;
            float _M0L6_2atmpS1734;
            float _M0L6_2atmpS1733;
            int32_t _M0L6_2atmpS1737;
            #line 298 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
            _M0L9post__idxS727
            = _M0MPC15array5Array2atGiE(_M0L6colptrS1736, _M0L1kS726);
            _M0L4valsS1735 = _M0L1mS720->$4;
            #line 299 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
            _M0L1wS728
            = _M0MPC15array5Array2atGfE(_M0L4valsS1735, _M0L1kS726);
            #line 300 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
            _M0L6_2atmpS1734
            = _M0MPC15array5Array2atGfE(_M0L7post__gS729, _M0L9post__idxS727);
            _M0L6_2atmpS1733 = _M0L6_2atmpS1734 + _M0L1wS728;
            #line 300 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
            _M0MPC15array5Array3setGfE(_M0L7post__gS729, _M0L9post__idxS727, _M0L6_2atmpS1733);
            _M0L6_2atmpS1737 = _M0L1kS726 + 1;
            _M0L1kS726 = _M0L6_2atmpS1737;
            continue;
          }
          break;
        }
      }
      _M0L6_2atmpS1741 = _M0L1iS722 + 1;
      _M0L1iS722 = _M0L6_2atmpS1741;
      continue;
    }
    break;
  }
  return 0;
}

int32_t _M0MP26RiantR8snn__mbt15SparseMatrixCSR3set(
  struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L1mS706,
  int32_t _M0L1iS705,
  int32_t _M0L1jS711,
  float _M0L1vS712
) {
  int32_t _if__result_2110;
  struct _M0TPB5ArrayGiE* _M0L6rowptrS1732;
  int32_t _M0L5startS707;
  struct _M0TPB5ArrayGiE* _M0L6rowptrS1730;
  int32_t _M0L6_2atmpS1731;
  int32_t _M0L3endS708;
  struct _M0TPB8MutLocalGbE* _M0L5foundS709;
  int32_t _M0L1kS710;
  int32_t _M0L3valS1722;
  #line 258 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  if (_M0L1iS705 < 0) {
    _if__result_2110 = 1;
  } else {
    int32_t _M0L4rowsS1717 = _M0L1mS706->$0;
    _if__result_2110 = _M0L1iS705 >= _M0L4rowsS1717;
  }
  if (_if__result_2110) {
    return 0;
  }
  _M0L6rowptrS1732 = _M0L1mS706->$2;
  #line 262 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  _M0L5startS707 = _M0MPC15array5Array2atGiE(_M0L6rowptrS1732, _M0L1iS705);
  _M0L6rowptrS1730 = _M0L1mS706->$2;
  _M0L6_2atmpS1731 = _M0L1iS705 + 1;
  #line 263 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  _M0L3endS708
  = _M0MPC15array5Array2atGiE(_M0L6rowptrS1730, _M0L6_2atmpS1731);
  _M0L5foundS709
  = (struct _M0TPB8MutLocalGbE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGbE));
  Moonbit_object_header(_M0L5foundS709)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L5foundS709->$0 = 0;
  _M0L1kS710 = _M0L5startS707;
  while (1) {
    if (_M0L1kS710 < _M0L3endS708) {
      struct _M0TPB5ArrayGiE* _M0L6colptrS1719 = _M0L1mS706->$3;
      int32_t _M0L6_2atmpS1718;
      int32_t _M0L6_2atmpS1721;
      #line 267 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
      _M0L6_2atmpS1718
      = _M0MPC15array5Array2atGiE(_M0L6colptrS1719, _M0L1kS710);
      if (_M0L6_2atmpS1718 == _M0L1jS711) {
        struct _M0TPB5ArrayGfE* _M0L4valsS1720 = _M0L1mS706->$4;
        #line 268 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
        _M0MPC15array5Array3setGfE(_M0L4valsS1720, _M0L1kS710, _M0L1vS712);
        _M0L5foundS709->$0 = 1;
        break;
      }
      _M0L6_2atmpS1721 = _M0L1kS710 + 1;
      _M0L1kS710 = _M0L6_2atmpS1721;
      continue;
    }
    break;
  }
  _M0L3valS1722 = _M0L5foundS709->$0;
  moonbit_decref(_M0L5foundS709);
  if (!_M0L3valS1722) {
    struct _M0TPB5ArrayGiE* _M0L6colptrS1723 = _M0L1mS706->$3;
    struct _M0TPB5ArrayGfE* _M0L4valsS1724;
    int32_t _M0L7n__rowsS714;
    int32_t _M0L7_2abindS715;
    int32_t _M0L7_2abindS716;
    int32_t _M0L1rS717;
    #line 275 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
    _M0MPC15array5Array6insertGiE(_M0L6colptrS1723, _M0L3endS708, _M0L1jS711);
    _M0L4valsS1724 = _M0L1mS706->$4;
    #line 276 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
    _M0MPC15array5Array6insertGfE(_M0L4valsS1724, _M0L3endS708, _M0L1vS712);
    _M0L7n__rowsS714 = _M0L1mS706->$0;
    _M0L7_2abindS715 = _M0L1iS705 + 1;
    _M0L7_2abindS716 = _M0L7n__rowsS714 + 1;
    _M0L1rS717 = _M0L7_2abindS715;
    while (1) {
      if (_M0L1rS717 < _M0L7_2abindS716) {
        struct _M0TPB5ArrayGiE* _M0L6rowptrS1725 = _M0L1mS706->$2;
        struct _M0TPB5ArrayGiE* _M0L6rowptrS1728 = _M0L1mS706->$2;
        int32_t _M0L6_2atmpS1727;
        int32_t _M0L6_2atmpS1726;
        int32_t _M0L6_2atmpS1729;
        #line 279 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
        _M0L6_2atmpS1727
        = _M0MPC15array5Array2atGiE(_M0L6rowptrS1728, _M0L1rS717);
        _M0L6_2atmpS1726 = _M0L6_2atmpS1727 + 1;
        #line 279 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
        _M0MPC15array5Array3setGiE(_M0L6rowptrS1725, _M0L1rS717, _M0L6_2atmpS1726);
        _M0L6_2atmpS1729 = _M0L1rS717 + 1;
        _M0L1rS717 = _M0L6_2atmpS1729;
        continue;
      }
      break;
    }
  }
  return 0;
}

struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0MP26RiantR8snn__mbt15SparseMatrixCSR5empty(
  int32_t _M0L4rowsS703,
  int32_t _M0L4colsS704
) {
  int32_t _M0L6_2atmpS1716;
  struct _M0TPB5ArrayGiE* _M0L6rowptrS702;
  int32_t* _M0L6_2atmpS1715;
  struct _M0TPB5ArrayGiE* _M0L6_2atmpS1712;
  float* _M0L6_2atmpS1714;
  struct _M0TPB5ArrayGfE* _M0L6_2atmpS1713;
  struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _block_2113;
  #line 40 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  _M0L6_2atmpS1716 = _M0L4rowsS703 + 1;
  #line 41 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  _M0L6rowptrS702 = _M0MPC15array5Array4makeGiE(_M0L6_2atmpS1716, 0);
  _M0L6_2atmpS1715 = (int32_t*)moonbit_empty_int32_array;
  _M0L6_2atmpS1712
  = (struct _M0TPB5ArrayGiE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGiE));
  Moonbit_object_header(_M0L6_2atmpS1712)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 3, 0);
  _M0L6_2atmpS1712->$0 = _M0L6_2atmpS1715;
  _M0L6_2atmpS1712->$1 = 0;
  _M0L6_2atmpS1714 = moonbit_empty_float_array;
  _M0L6_2atmpS1713
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_M0L6_2atmpS1713)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 0, 0);
  _M0L6_2atmpS1713->$0 = _M0L6_2atmpS1714;
  _M0L6_2atmpS1713->$1 = 0;
  _block_2113
  = (struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR));
  Moonbit_object_header(_block_2113)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 42, 0);
  _block_2113->$0 = _M0L4rowsS703;
  _block_2113->$1 = _M0L4colsS704;
  _block_2113->$2 = _M0L6rowptrS702;
  _block_2113->$3 = _M0L6_2atmpS1712;
  _block_2113->$4 = _M0L6_2atmpS1713;
  return _block_2113;
}

struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0MP26RiantR8snn__mbt7Xoshiro3new(
  uint64_t _M0L4seedS700
) {
  struct _M0TUmmmmE* _M0L1sS699;
  uint64_t _M0L6_2atmpS1711;
  struct _M0TUmmmmE* _M0L1tS701;
  uint64_t _M0L6_2atmpS1707;
  uint64_t _M0L6_2atmpS1708;
  uint64_t _M0L6_2atmpS1709;
  uint64_t _M0L6_2atmpS1710;
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _block_2114;
  #line 48 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  #line 49 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L1sS699 = _M0FP26RiantR8snn__mbt10splitmix64(_M0L4seedS700);
  _M0L6_2atmpS1711 = _M0L1sS699->$3;
  #line 50 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L1tS701 = _M0FP26RiantR8snn__mbt10splitmix64(_M0L6_2atmpS1711);
  _M0L6_2atmpS1707 = _M0L1sS699->$0;
  _M0L6_2atmpS1708 = _M0L1sS699->$1;
  _M0L6_2atmpS1709 = _M0L1sS699->$2;
  moonbit_decref(_M0L1sS699);
  _M0L6_2atmpS1710 = _M0L1tS701->$0;
  moonbit_decref(_M0L1tS701);
  _block_2114
  = (struct _M0TP26RiantR8snn__mbt7Xoshiro*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt7Xoshiro));
  Moonbit_object_header(_block_2114)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_2114->$0 = _M0L6_2atmpS1707;
  _block_2114->$1 = _M0L6_2atmpS1708;
  _block_2114->$2 = _M0L6_2atmpS1709;
  _block_2114->$3 = _M0L6_2atmpS1710;
  return _block_2114;
}

struct _M0TUmmmmE* _M0FP26RiantR8snn__mbt10splitmix64(uint64_t _M0L4seedS691) {
  uint64_t _M0L2s1S690;
  uint64_t _M0L2z1S692;
  uint64_t _M0L2s2S693;
  uint64_t _M0L2z2S694;
  uint64_t _M0L2s3S695;
  uint64_t _M0L2z3S696;
  uint64_t _M0L2s4S697;
  uint64_t _M0L2z4S698;
  struct _M0TUmmmmE* _block_2115;
  #line 62 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2s1S690 = _M0L4seedS691 + 11400714819323198485ull;
  #line 64 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2z1S692 = _M0FP26RiantR8snn__mbt5mix64(_M0L2s1S690);
  _M0L2s2S693 = _M0L2s1S690 + 11400714819323198485ull;
  #line 66 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2z2S694 = _M0FP26RiantR8snn__mbt5mix64(_M0L2s2S693);
  _M0L2s3S695 = _M0L2s2S693 + 11400714819323198485ull;
  #line 68 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2z3S696 = _M0FP26RiantR8snn__mbt5mix64(_M0L2s3S695);
  _M0L2s4S697 = _M0L2s3S695 + 11400714819323198485ull;
  #line 70 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2z4S698 = _M0FP26RiantR8snn__mbt5mix64(_M0L2s4S697);
  _block_2115 = (struct _M0TUmmmmE*)moonbit_malloc(sizeof(struct _M0TUmmmmE));
  Moonbit_object_header(_block_2115)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_2115->$0 = _M0L2z1S692;
  _block_2115->$1 = _M0L2z2S694;
  _block_2115->$2 = _M0L2z3S696;
  _block_2115->$3 = _M0L2z4S698;
  return _block_2115;
}

uint64_t _M0FP26RiantR8snn__mbt5mix64(uint64_t _M0L1zS688) {
  uint64_t _M0L6_2atmpS1706;
  uint64_t _M0L6_2atmpS1705;
  uint64_t _M0L1zS687;
  uint64_t _M0L6_2atmpS1704;
  uint64_t _M0L6_2atmpS1703;
  uint64_t _M0L1zS689;
  uint64_t _M0L6_2atmpS1702;
  #line 75 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L6_2atmpS1706 = _M0L1zS688 >> 30;
  _M0L6_2atmpS1705 = _M0L1zS688 ^ _M0L6_2atmpS1706;
  _M0L1zS687 = _M0L6_2atmpS1705 * 13787848793156543929ull;
  _M0L6_2atmpS1704 = _M0L1zS687 >> 27;
  _M0L6_2atmpS1703 = _M0L1zS687 ^ _M0L6_2atmpS1704;
  _M0L1zS689 = _M0L6_2atmpS1703 * 10723151780598845931ull;
  _M0L6_2atmpS1702 = _M0L1zS689 >> 31;
  return _M0L1zS689 ^ _M0L6_2atmpS1702;
}

int32_t _M0FP26RiantR8snn__mbt12update__time(
  struct _M0TP26RiantR8snn__mbt4Time* _M0L1tS685,
  float _M0L2dtS686
) {
  struct _M0TPB5ArrayGfE* _M0L1tS1694;
  struct _M0TPB5ArrayGfE* _M0L1tS1697;
  float _M0L6_2atmpS1696;
  float _M0L6_2atmpS1695;
  struct _M0TPB5ArrayGiE* _M0L2ttS1698;
  struct _M0TPB5ArrayGiE* _M0L2ttS1701;
  int32_t _M0L6_2atmpS1700;
  int32_t _M0L6_2atmpS1699;
  #line 89 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\time.mbt"
  _M0L1tS1694 = _M0L1tS685->$0;
  _M0L1tS1697 = _M0L1tS685->$0;
  #line 90 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\time.mbt"
  _M0L6_2atmpS1696 = _M0MPC15array5Array2atGfE(_M0L1tS1697, 0);
  _M0L6_2atmpS1695 = _M0L6_2atmpS1696 + _M0L2dtS686;
  #line 90 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\time.mbt"
  _M0MPC15array5Array3setGfE(_M0L1tS1694, 0, _M0L6_2atmpS1695);
  _M0L2ttS1698 = _M0L1tS685->$1;
  _M0L2ttS1701 = _M0L1tS685->$1;
  #line 91 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\time.mbt"
  _M0L6_2atmpS1700 = _M0MPC15array5Array2atGiE(_M0L2ttS1701, 0);
  _M0L6_2atmpS1699 = _M0L6_2atmpS1700 + 1;
  #line 91 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\time.mbt"
  _M0MPC15array5Array3setGiE(_M0L2ttS1698, 0, _M0L6_2atmpS1699);
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt7set__dt(
  struct _M0TP26RiantR8snn__mbt4Time* _M0L1tS683,
  float _M0L1vS684
) {
  #line 80 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\time.mbt"
  _M0L1tS683->$2 = _M0L1vS684;
  return 0;
}

float _M0FP26RiantR8snn__mbt9get__time(
  struct _M0TP26RiantR8snn__mbt4Time* _M0L1tS682
) {
  struct _M0TPB5ArrayGfE* _M0L1tS1693;
  #line 49 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\time.mbt"
  _M0L1tS1693 = _M0L1tS682->$0;
  #line 50 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\time.mbt"
  return _M0MPC15array5Array2atGfE(_M0L1tS1693, 0);
}

struct _M0TP26RiantR8snn__mbt4Time* _M0MP26RiantR8snn__mbt4Time3new() {
  float* _M0L6_2atmpS1692;
  struct _M0TPB5ArrayGfE* _M0L6_2atmpS1689;
  int32_t* _M0L6_2atmpS1691;
  struct _M0TPB5ArrayGiE* _M0L6_2atmpS1690;
  struct _M0TP26RiantR8snn__mbt4Time* _block_2116;
  #line 34 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\time.mbt"
  _M0L6_2atmpS1692 = (float*)moonbit_make_float_array_raw(1);
  _M0L6_2atmpS1692[0] = 0x0p+0f;
  _M0L6_2atmpS1689
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_M0L6_2atmpS1689)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 0, 0);
  _M0L6_2atmpS1689->$0 = _M0L6_2atmpS1692;
  _M0L6_2atmpS1689->$1 = 1;
  _M0L6_2atmpS1691 = (int32_t*)moonbit_make_int32_array_raw(1);
  _M0L6_2atmpS1691[0] = 0;
  _M0L6_2atmpS1690
  = (struct _M0TPB5ArrayGiE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGiE));
  Moonbit_object_header(_M0L6_2atmpS1690)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 3, 0);
  _M0L6_2atmpS1690->$0 = _M0L6_2atmpS1691;
  _M0L6_2atmpS1690->$1 = 1;
  _block_2116
  = (struct _M0TP26RiantR8snn__mbt4Time*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt4Time));
  Moonbit_object_header(_block_2116)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 47, 0);
  _block_2116->$0 = _M0L6_2atmpS1689;
  _block_2116->$1 = _M0L6_2atmpS1690;
  _block_2116->$2 = 0x1p-3f;
  return _block_2116;
}

float _M0FP26RiantR8snn__mbt9next__f32(
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L1rS680
) {
  uint32_t _M0L1uS679;
  uint32_t _M0L4bitsS681;
  double _M0L6_2atmpS1688;
  double _M0L6_2atmpS1687;
  #line 128 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  #line 129 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L1uS679 = _M0FP26RiantR8snn__mbt9next__u32(_M0L1rS680);
  _M0L4bitsS681 = _M0L1uS679 >> 8;
  _M0L6_2atmpS1688 = (double)_M0L4bitsS681;
  _M0L6_2atmpS1687 = _M0L6_2atmpS1688 * 0x1p-24;
  return (float)_M0L6_2atmpS1687;
}

uint32_t _M0FP26RiantR8snn__mbt9next__u32(
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L1rS678
) {
  uint64_t _M0L1uS677;
  uint64_t _M0L6_2atmpS1686;
  #line 110 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  #line 111 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L1uS677 = _M0FP26RiantR8snn__mbt9next__u64(_M0L1rS678);
  _M0L6_2atmpS1686 = _M0L1uS677 >> 32;
  return (uint32_t)_M0L6_2atmpS1686;
}

uint64_t _M0FP26RiantR8snn__mbt9next__u64(
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L1rS670
) {
  uint64_t _M0L2s0S669;
  uint64_t _M0L2s1S671;
  uint64_t _M0L2s2S672;
  uint64_t _M0L2s3S673;
  uint64_t _M0L3tmpS674;
  uint64_t _M0L6_2atmpS1685;
  uint64_t _M0L3resS675;
  uint64_t _M0L1tS676;
  uint64_t _M0L6_2atmpS1675;
  uint64_t _M0L6_2atmpS1676;
  uint64_t _M0L2s2S1678;
  uint64_t _M0L6_2atmpS1677;
  uint64_t _M0L2s3S1680;
  uint64_t _M0L6_2atmpS1679;
  uint64_t _M0L2s2S1682;
  uint64_t _M0L6_2atmpS1681;
  uint64_t _M0L2s3S1684;
  uint64_t _M0L6_2atmpS1683;
  #line 90 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2s0S669 = _M0L1rS670->$0;
  _M0L2s1S671 = _M0L1rS670->$1;
  _M0L2s2S672 = _M0L1rS670->$2;
  _M0L2s3S673 = _M0L1rS670->$3;
  _M0L3tmpS674 = _M0L2s0S669 + _M0L2s3S673;
  #line 96 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L6_2atmpS1685 = _M0FP26RiantR8snn__mbt4rotl(_M0L3tmpS674, 23);
  _M0L3resS675 = _M0L6_2atmpS1685 + _M0L2s0S669;
  _M0L1tS676 = _M0L2s1S671 << 17;
  _M0L6_2atmpS1675 = _M0L2s2S672 ^ _M0L2s0S669;
  _M0L1rS670->$2 = _M0L6_2atmpS1675;
  _M0L6_2atmpS1676 = _M0L2s3S673 ^ _M0L2s1S671;
  _M0L1rS670->$3 = _M0L6_2atmpS1676;
  _M0L2s2S1678 = _M0L1rS670->$2;
  _M0L6_2atmpS1677 = _M0L2s1S671 ^ _M0L2s2S1678;
  _M0L1rS670->$1 = _M0L6_2atmpS1677;
  _M0L2s3S1680 = _M0L1rS670->$3;
  _M0L6_2atmpS1679 = _M0L2s0S669 ^ _M0L2s3S1680;
  _M0L1rS670->$0 = _M0L6_2atmpS1679;
  _M0L2s2S1682 = _M0L1rS670->$2;
  _M0L6_2atmpS1681 = _M0L2s2S1682 ^ _M0L1tS676;
  _M0L1rS670->$2 = _M0L6_2atmpS1681;
  _M0L2s3S1684 = _M0L1rS670->$3;
  #line 103 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L6_2atmpS1683 = _M0FP26RiantR8snn__mbt4rotl(_M0L2s3S1684, 45);
  _M0L1rS670->$3 = _M0L6_2atmpS1683;
  return _M0L3resS675;
}

uint64_t _M0FP26RiantR8snn__mbt4rotl(uint64_t _M0L1xS667, int32_t _M0L1kS668) {
  uint64_t _M0L6_2atmpS1672;
  int32_t _M0L6_2atmpS1674;
  uint64_t _M0L6_2atmpS1673;
  #line 83 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L6_2atmpS1672 = _M0L1xS667 << (_M0L1kS668 & 63);
  _M0L6_2atmpS1674 = 64 - _M0L1kS668;
  _M0L6_2atmpS1673 = _M0L1xS667 >> (_M0L6_2atmpS1674 & 63);
  return _M0L6_2atmpS1672 | _M0L6_2atmpS1673;
}

int32_t _M0MP26RiantR8snn__mbt7Monitor13dump__summary(
  struct _M0TP26RiantR8snn__mbt7Monitor* _M0L1mS656
) {
  struct _M0TPB5ArrayGfE* _M0L4dataS1671;
  int32_t _M0L1nS655;
  struct _M0TPB5ArrayGfE* _M0L4dataS1670;
  float _M0L6_2atmpS1669;
  struct _M0TPB8MutLocalGfE* _M0L2mnS658;
  struct _M0TPB5ArrayGfE* _M0L4dataS1668;
  float _M0L6_2atmpS1667;
  struct _M0TPB8MutLocalGfE* _M0L2mxS659;
  struct _M0TPB8MutLocalGfE* _M0L3sumS660;
  int32_t _M0L7_2abindS661;
  int32_t _M0L1iS662;
  float _M0L3valS1665;
  float _M0L6_2atmpS1666;
  float _M0L4meanS665;
  struct _M0TPB13StringBuilder* _M0L18_2astring__builderS666;
  moonbit_string_t _M0L3symS1664;
  moonbit_string_t _M0L6_2atmpS1662;
  moonbit_string_t _M0L6_2atmpS1663;
  moonbit_string_t _M0L6_2atmpS1661;
  moonbit_string_t _M0L6_2atmpS1658;
  float _M0L3valS1660;
  moonbit_string_t _M0L6_2atmpS1659;
  moonbit_string_t _M0L6_2atmpS1657;
  moonbit_string_t _M0L6_2atmpS1654;
  float _M0L3valS1656;
  moonbit_string_t _M0L6_2atmpS1655;
  moonbit_string_t _M0L6_2atmpS1653;
  moonbit_string_t _M0L6_2atmpS1651;
  moonbit_string_t _M0L6_2atmpS1652;
  moonbit_string_t _M0L6_2atmpS1650;
  #line 175 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\vecplot.mbt"
  _M0L4dataS1671 = _M0L1mS656->$2;
  #line 176 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\vecplot.mbt"
  _M0L1nS655 = _M0MPC15array5Array6lengthGfE(_M0L4dataS1671);
  if (_M0L1nS655 == 0) {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS657;
    moonbit_string_t _M0L3symS1643;
    moonbit_string_t _M0L6_2atmpS1642;
    #line 178 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\vecplot.mbt"
    _M0L18_2astring__builderS657
    = _M0MPB13StringBuilder21StringBuilder_2einner(17);
    #line 178 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\vecplot.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS657, (moonbit_string_t)moonbit_string_literal_4.data);
    _M0L3symS1643 = _M0L1mS656->$1;
    #line 178 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\vecplot.mbt"
    _M0MPB13StringBuilder13write__objectGsE(_M0L18_2astring__builderS657, _M0L3symS1643);
    #line 178 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\vecplot.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS657, (moonbit_string_t)moonbit_string_literal_5.data);
    #line 178 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\vecplot.mbt"
    _M0L6_2atmpS1642
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS657);
    moonbit_decref(_M0L18_2astring__builderS657);
    #line 178 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\vecplot.mbt"
    _M0FPB7printlnGsE(_M0L6_2atmpS1642);
    moonbit_decref(_M0L6_2atmpS1642);
    return 0;
  }
  _M0L4dataS1670 = _M0L1mS656->$2;
  #line 181 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\vecplot.mbt"
  _M0L6_2atmpS1669 = _M0MPC15array5Array2atGfE(_M0L4dataS1670, 0);
  _M0L2mnS658
  = (struct _M0TPB8MutLocalGfE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGfE));
  Moonbit_object_header(_M0L2mnS658)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L2mnS658->$0 = _M0L6_2atmpS1669;
  _M0L4dataS1668 = _M0L1mS656->$2;
  #line 182 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\vecplot.mbt"
  _M0L6_2atmpS1667 = _M0MPC15array5Array2atGfE(_M0L4dataS1668, 0);
  _M0L2mxS659
  = (struct _M0TPB8MutLocalGfE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGfE));
  Moonbit_object_header(_M0L2mxS659)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L2mxS659->$0 = _M0L6_2atmpS1667;
  _M0L3sumS660
  = (struct _M0TPB8MutLocalGfE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGfE));
  Moonbit_object_header(_M0L3sumS660)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L3sumS660->$0 = 0x0p+0f;
  _M0L7_2abindS661 = 0;
  _M0L1iS662 = _M0L7_2abindS661;
  while (1) {
    if (_M0L1iS662 < _M0L1nS655) {
      struct _M0TPB5ArrayGfE* _M0L4dataS1648 = _M0L1mS656->$2;
      float _M0L1vS663;
      float _M0L3valS1644;
      float _M0L3valS1645;
      float _M0L3valS1647;
      float _M0L6_2atmpS1646;
      int32_t _M0L6_2atmpS1649;
      #line 185 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\vecplot.mbt"
      _M0L1vS663 = _M0MPC15array5Array2atGfE(_M0L4dataS1648, _M0L1iS662);
      _M0L3valS1644 = _M0L2mnS658->$0;
      if (_M0L1vS663 < _M0L3valS1644) {
        _M0L2mnS658->$0 = _M0L1vS663;
      }
      _M0L3valS1645 = _M0L2mxS659->$0;
      if (_M0L1vS663 > _M0L3valS1645) {
        _M0L2mxS659->$0 = _M0L1vS663;
      }
      _M0L3valS1647 = _M0L3sumS660->$0;
      _M0L6_2atmpS1646 = _M0L3valS1647 + _M0L1vS663;
      _M0L3sumS660->$0 = _M0L6_2atmpS1646;
      _M0L6_2atmpS1649 = _M0L1iS662 + 1;
      _M0L1iS662 = _M0L6_2atmpS1649;
      continue;
    }
    break;
  }
  _M0L3valS1665 = _M0L3sumS660->$0;
  moonbit_decref(_M0L3sumS660);
  _M0L6_2atmpS1666 = (float)_M0L1nS655;
  _M0L4meanS665 = _M0L3valS1665 / _M0L6_2atmpS1666;
  #line 191 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\vecplot.mbt"
  _M0L18_2astring__builderS666
  = _M0MPB13StringBuilder21StringBuilder_2einner(12);
  #line 191 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\vecplot.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS666, (moonbit_string_t)moonbit_string_literal_4.data);
  _M0L3symS1664 = _M0L1mS656->$1;
  #line 191 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\vecplot.mbt"
  _M0MPB13StringBuilder13write__objectGsE(_M0L18_2astring__builderS666, _M0L3symS1664);
  #line 191 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\vecplot.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS666, (moonbit_string_t)moonbit_string_literal_6.data);
  #line 191 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\vecplot.mbt"
  _M0L6_2atmpS1662
  = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS666);
  moonbit_decref(_M0L18_2astring__builderS666);
  #line 191 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\vecplot.mbt"
  _M0L6_2atmpS1663 = _M0MPC13int3Int18to__string_2einner(_M0L1nS655, 10);
  #line 191 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\vecplot.mbt"
  _M0L6_2atmpS1661 = moonbit_add_string(_M0L6_2atmpS1662, _M0L6_2atmpS1663);
  moonbit_decref(_M0L6_2atmpS1663);
  moonbit_decref(_M0L6_2atmpS1662);
  #line 191 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\vecplot.mbt"
  _M0L6_2atmpS1658
  = moonbit_add_string(_M0L6_2atmpS1661, (moonbit_string_t)moonbit_string_literal_7.data);
  moonbit_decref(_M0L6_2atmpS1661);
  _M0L3valS1660 = _M0L2mnS658->$0;
  moonbit_decref(_M0L2mnS658);
  #line 192 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\vecplot.mbt"
  _M0L6_2atmpS1659 = _M0IPC15float5FloatPB4Show10to__string(_M0L3valS1660);
  #line 191 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\vecplot.mbt"
  _M0L6_2atmpS1657 = moonbit_add_string(_M0L6_2atmpS1658, _M0L6_2atmpS1659);
  moonbit_decref(_M0L6_2atmpS1659);
  moonbit_decref(_M0L6_2atmpS1658);
  #line 191 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\vecplot.mbt"
  _M0L6_2atmpS1654
  = moonbit_add_string(_M0L6_2atmpS1657, (moonbit_string_t)moonbit_string_literal_8.data);
  moonbit_decref(_M0L6_2atmpS1657);
  _M0L3valS1656 = _M0L2mxS659->$0;
  moonbit_decref(_M0L2mxS659);
  #line 193 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\vecplot.mbt"
  _M0L6_2atmpS1655 = _M0IPC15float5FloatPB4Show10to__string(_M0L3valS1656);
  #line 191 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\vecplot.mbt"
  _M0L6_2atmpS1653 = moonbit_add_string(_M0L6_2atmpS1654, _M0L6_2atmpS1655);
  moonbit_decref(_M0L6_2atmpS1655);
  moonbit_decref(_M0L6_2atmpS1654);
  #line 191 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\vecplot.mbt"
  _M0L6_2atmpS1651
  = moonbit_add_string(_M0L6_2atmpS1653, (moonbit_string_t)moonbit_string_literal_9.data);
  moonbit_decref(_M0L6_2atmpS1653);
  #line 194 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\vecplot.mbt"
  _M0L6_2atmpS1652 = _M0IPC15float5FloatPB4Show10to__string(_M0L4meanS665);
  #line 191 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\vecplot.mbt"
  _M0L6_2atmpS1650 = moonbit_add_string(_M0L6_2atmpS1651, _M0L6_2atmpS1652);
  moonbit_decref(_M0L6_2atmpS1652);
  moonbit_decref(_M0L6_2atmpS1651);
  #line 191 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\vecplot.mbt"
  _M0FPB7printlnGsE(_M0L6_2atmpS1650);
  moonbit_decref(_M0L6_2atmpS1650);
  return 0;
}

moonbit_string_t _M0IPC15float5FloatPB4Show10to__string(float _M0L4selfS654) {
  double _M0L6_2atmpS1641;
  #line 16 "C:\\Users\\31379\\.moon\\lib\\core\\float\\methods.mbt"
  _M0L6_2atmpS1641 = (double)_M0L4selfS654;
  #line 17 "C:\\Users\\31379\\.moon\\lib\\core\\float\\methods.mbt"
  return _M0MPC16double6Double10to__string(_M0L6_2atmpS1641);
}

int32_t _M0MPC15float5Float7to__int(float _M0L4selfS653) {
  #line 43 "C:\\Users\\31379\\.moon\\lib\\core\\float\\to_int.mbt"
  if (_M0L4selfS653 != _M0L4selfS653) {
    return 0;
  } else if (_M0L4selfS653 >= 0x1.fffffffcp+30f) {
    return 2147483647;
  } else if (_M0L4selfS653 <= -0x1p+31f) {
    return (int32_t)0x80000000;
  } else {
    return (int32_t)_M0L4selfS653;
  }
}

struct _M0TPB5ArrayGfE* _M0MPC15array5Array4makeGfE(
  int32_t _M0L3lenS639,
  float _M0L4elemS641
) {
  struct _M0TPB5ArrayGfE* _M0L3arrS638;
  int32_t _M0L1iS640;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3arrS638 = _M0MPC15array5Array20unsafe__make__uninitGfE(_M0L3lenS639);
  _M0L1iS640 = 0;
  while (1) {
    if (_M0L1iS640 < _M0L3lenS639) {
      float* _M0L3bufS1635 = _M0L3arrS638->$0;
      int32_t _M0L6_2atmpS1636;
      _M0L3bufS1635[_M0L1iS640] = _M0L4elemS641;
      _M0L6_2atmpS1636 = _M0L1iS640 + 1;
      _M0L1iS640 = _M0L6_2atmpS1636;
      continue;
    }
    break;
  }
  return _M0L3arrS638;
}

struct _M0TPB5ArrayGbE* _M0MPC15array5Array4makeGbE(
  int32_t _M0L3lenS644,
  int32_t _M0L4elemS646
) {
  struct _M0TPB5ArrayGbE* _M0L3arrS643;
  int32_t _M0L1iS645;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3arrS643 = _M0MPC15array5Array20unsafe__make__uninitGbE(_M0L3lenS644);
  _M0L1iS645 = 0;
  while (1) {
    if (_M0L1iS645 < _M0L3lenS644) {
      uint8_t* _M0L3bufS1637 = _M0L3arrS643->$0;
      int32_t _M0L6_2atmpS1638;
      _M0L3bufS1637[_M0L1iS645] = _M0L4elemS646;
      _M0L6_2atmpS1638 = _M0L1iS645 + 1;
      _M0L1iS645 = _M0L6_2atmpS1638;
      continue;
    }
    break;
  }
  return _M0L3arrS643;
}

struct _M0TPB5ArrayGiE* _M0MPC15array5Array4makeGiE(
  int32_t _M0L3lenS649,
  int32_t _M0L4elemS651
) {
  struct _M0TPB5ArrayGiE* _M0L3arrS648;
  int32_t _M0L1iS650;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3arrS648 = _M0MPC15array5Array20unsafe__make__uninitGiE(_M0L3lenS649);
  _M0L1iS650 = 0;
  while (1) {
    if (_M0L1iS650 < _M0L3lenS649) {
      int32_t* _M0L3bufS1639 = _M0L3arrS648->$0;
      int32_t _M0L6_2atmpS1640;
      _M0L3bufS1639[_M0L1iS650] = _M0L4elemS651;
      _M0L6_2atmpS1640 = _M0L1iS650 + 1;
      _M0L1iS650 = _M0L6_2atmpS1640;
      continue;
    }
    break;
  }
  return _M0L3arrS648;
}

int32_t _M0MPC15array5Array3setGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS627,
  int32_t _M0L5indexS628,
  float _M0L5valueS629
) {
  int32_t _M0L3lenS626;
  #line 262 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS626 = _M0L4selfS627->$1;
  if (_M0L5indexS628 >= 0 && _M0L5indexS628 < _M0L3lenS626) {
    float* _M0L6_2atmpS1632;
    #line 268 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS1632 = _M0MPC15array5Array6bufferGfE(_M0L4selfS627);
    _M0L6_2atmpS1632[_M0L5indexS628] = _M0L5valueS629;
    moonbit_decref(_M0L6_2atmpS1632);
  } else {
    #line 267 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
  return 0;
}

int32_t _M0MPC15array5Array3setGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS631,
  int32_t _M0L5indexS632,
  int32_t _M0L5valueS633
) {
  int32_t _M0L3lenS630;
  #line 262 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS630 = _M0L4selfS631->$1;
  if (_M0L5indexS632 >= 0 && _M0L5indexS632 < _M0L3lenS630) {
    int32_t* _M0L6_2atmpS1633;
    #line 268 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS1633 = _M0MPC15array5Array6bufferGiE(_M0L4selfS631);
    _M0L6_2atmpS1633[_M0L5indexS632] = _M0L5valueS633;
    moonbit_decref(_M0L6_2atmpS1633);
  } else {
    #line 267 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
  return 0;
}

int32_t _M0MPC15array5Array3setGbE(
  struct _M0TPB5ArrayGbE* _M0L4selfS635,
  int32_t _M0L5indexS636,
  int32_t _M0L5valueS637
) {
  int32_t _M0L3lenS634;
  #line 262 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS634 = _M0L4selfS635->$1;
  if (_M0L5indexS636 >= 0 && _M0L5indexS636 < _M0L3lenS634) {
    uint8_t* _M0L6_2atmpS1634;
    #line 268 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS1634 = _M0MPC15array5Array6bufferGbE(_M0L4selfS635);
    _M0L6_2atmpS1634[_M0L5indexS636] = _M0L5valueS637;
    moonbit_decref(_M0L6_2atmpS1634);
  } else {
    #line 267 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
  return 0;
}

int32_t _M0MPC15array5Array6insertGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS617,
  int32_t _M0L5indexS616,
  int32_t _M0L5valueS619
) {
  int32_t _if__result_2121;
  #line 738 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  if (_M0L5indexS616 >= 0) {
    int32_t _M0L3lenS1602 = _M0L4selfS617->$1;
    _if__result_2121 = _M0L5indexS616 <= _M0L3lenS1602;
  } else {
    _if__result_2121 = 0;
  }
  if (_if__result_2121) {
    int32_t _M0L3lenS1603 = _M0L4selfS617->$1;
    int32_t* _M0L6_2atmpS1605;
    int32_t _M0L6_2atmpS1604;
    int32_t* _M0L6_2atmpS1608;
    int32_t _M0L6_2atmpS1609;
    int32_t* _M0L6_2atmpS1610;
    int32_t _M0L3lenS1612;
    int32_t _M0L6_2atmpS1611;
    int32_t _M0L6lengthS618;
    int32_t* _M0L3bufS1613;
    int32_t _M0L6_2atmpS1614;
    #line 745 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0L6_2atmpS1605 = _M0MPC15array5Array6bufferGiE(_M0L4selfS617);
    _M0L6_2atmpS1604 = Moonbit_array_length(_M0L6_2atmpS1605);
    moonbit_decref(_M0L6_2atmpS1605);
    if (_M0L3lenS1603 == _M0L6_2atmpS1604) {
      int32_t _M0L3lenS1607 = _M0L4selfS617->$1;
      int32_t _M0L6_2atmpS1606 = _M0L3lenS1607 + 1;
      #line 746 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
      _M0MPC15array5Array7reallocGiE(_M0L4selfS617, _M0L6_2atmpS1606);
    }
    #line 749 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0L6_2atmpS1608 = _M0MPC15array5Array6bufferGiE(_M0L4selfS617);
    _M0L6_2atmpS1609 = _M0L5indexS616 + 1;
    #line 751 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0L6_2atmpS1610 = _M0MPC15array5Array6bufferGiE(_M0L4selfS617);
    _M0L3lenS1612 = _M0L4selfS617->$1;
    _M0L6_2atmpS1611 = _M0L3lenS1612 - _M0L5indexS616;
    #line 748 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0MPB18UninitializedArray12unsafe__blitGiE(_M0L6_2atmpS1608, _M0L6_2atmpS1609, _M0L6_2atmpS1610, _M0L5indexS616, _M0L6_2atmpS1611);
    moonbit_decref(_M0L6_2atmpS1608);
    moonbit_decref(_M0L6_2atmpS1610);
    _M0L6lengthS618 = _M0L4selfS617->$1;
    _M0L3bufS1613 = _M0L4selfS617->$0;
    _M0L3bufS1613[_M0L5indexS616] = _M0L5valueS619;
    _M0L6_2atmpS1614 = _M0L6lengthS618 + 1;
    _M0L4selfS617->$1 = _M0L6_2atmpS1614;
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS620;
    int32_t _M0L3lenS1616;
    moonbit_string_t _M0L6_2atmpS1615;
    #line 742 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0L18_2astring__builderS620
    = _M0MPB13StringBuilder21StringBuilder_2einner(60);
    #line 742 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS620, (moonbit_string_t)moonbit_string_literal_10.data);
    _M0L3lenS1616 = _M0L4selfS617->$1;
    #line 742 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS620, _M0L3lenS1616);
    #line 742 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS620, (moonbit_string_t)moonbit_string_literal_11.data);
    #line 742 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS620, _M0L5indexS616);
    #line 742 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0L6_2atmpS1615
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS620);
    moonbit_decref(_M0L18_2astring__builderS620);
    #line 741 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0FPC15abort5abortGuE(_M0L6_2atmpS1615);
    moonbit_decref(_M0L6_2atmpS1615);
  }
  return 0;
}

int32_t _M0MPC15array5Array6insertGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS622,
  int32_t _M0L5indexS621,
  float _M0L5valueS624
) {
  int32_t _if__result_2122;
  #line 738 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  if (_M0L5indexS621 >= 0) {
    int32_t _M0L3lenS1617 = _M0L4selfS622->$1;
    _if__result_2122 = _M0L5indexS621 <= _M0L3lenS1617;
  } else {
    _if__result_2122 = 0;
  }
  if (_if__result_2122) {
    int32_t _M0L3lenS1618 = _M0L4selfS622->$1;
    float* _M0L6_2atmpS1620;
    int32_t _M0L6_2atmpS1619;
    float* _M0L6_2atmpS1623;
    int32_t _M0L6_2atmpS1624;
    float* _M0L6_2atmpS1625;
    int32_t _M0L3lenS1627;
    int32_t _M0L6_2atmpS1626;
    int32_t _M0L6lengthS623;
    float* _M0L3bufS1628;
    int32_t _M0L6_2atmpS1629;
    #line 745 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0L6_2atmpS1620 = _M0MPC15array5Array6bufferGfE(_M0L4selfS622);
    _M0L6_2atmpS1619 = Moonbit_array_length(_M0L6_2atmpS1620);
    moonbit_decref(_M0L6_2atmpS1620);
    if (_M0L3lenS1618 == _M0L6_2atmpS1619) {
      int32_t _M0L3lenS1622 = _M0L4selfS622->$1;
      int32_t _M0L6_2atmpS1621 = _M0L3lenS1622 + 1;
      #line 746 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
      _M0MPC15array5Array7reallocGfE(_M0L4selfS622, _M0L6_2atmpS1621);
    }
    #line 749 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0L6_2atmpS1623 = _M0MPC15array5Array6bufferGfE(_M0L4selfS622);
    _M0L6_2atmpS1624 = _M0L5indexS621 + 1;
    #line 751 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0L6_2atmpS1625 = _M0MPC15array5Array6bufferGfE(_M0L4selfS622);
    _M0L3lenS1627 = _M0L4selfS622->$1;
    _M0L6_2atmpS1626 = _M0L3lenS1627 - _M0L5indexS621;
    #line 748 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0MPB18UninitializedArray12unsafe__blitGfE(_M0L6_2atmpS1623, _M0L6_2atmpS1624, _M0L6_2atmpS1625, _M0L5indexS621, _M0L6_2atmpS1626);
    moonbit_decref(_M0L6_2atmpS1623);
    moonbit_decref(_M0L6_2atmpS1625);
    _M0L6lengthS623 = _M0L4selfS622->$1;
    _M0L3bufS1628 = _M0L4selfS622->$0;
    _M0L3bufS1628[_M0L5indexS621] = _M0L5valueS624;
    _M0L6_2atmpS1629 = _M0L6lengthS623 + 1;
    _M0L4selfS622->$1 = _M0L6_2atmpS1629;
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS625;
    int32_t _M0L3lenS1631;
    moonbit_string_t _M0L6_2atmpS1630;
    #line 742 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0L18_2astring__builderS625
    = _M0MPB13StringBuilder21StringBuilder_2einner(60);
    #line 742 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS625, (moonbit_string_t)moonbit_string_literal_10.data);
    _M0L3lenS1631 = _M0L4selfS622->$1;
    #line 742 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS625, _M0L3lenS1631);
    #line 742 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS625, (moonbit_string_t)moonbit_string_literal_11.data);
    #line 742 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS625, _M0L5indexS621);
    #line 742 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0L6_2atmpS1630
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS625);
    moonbit_decref(_M0L18_2astring__builderS625);
    #line 741 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0FPC15abort5abortGuE(_M0L6_2atmpS1630);
    moonbit_decref(_M0L6_2atmpS1630);
  }
  return 0;
}

void* _M0MPC15array5Array3popGfE(struct _M0TPB5ArrayGfE* _M0L4selfS609) {
  int32_t _M0L3lenS608;
  #line 592 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L3lenS608 = _M0L4selfS609->$1;
  if (_M0L3lenS608 == 0) {
    return (struct moonbit_object*)&moonbit_constant_constructor_0 + 1;
  } else {
    int32_t _M0L5indexS610 = _M0L3lenS608 - 1;
    float* _M0L3bufS1600 = _M0L4selfS609->$0;
    float _M0L1vS611 = (float)_M0L3bufS1600[_M0L5indexS610];
    void* _block_2123;
    _M0L4selfS609->$1 = _M0L5indexS610;
    _block_2123
    = (void*)moonbit_malloc(sizeof(struct _M0DTPC16option6OptionGfE4Some));
    Moonbit_object_header(_block_2123)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 1);
    ((struct _M0DTPC16option6OptionGfE4Some*)_block_2123)->$0 = _M0L1vS611;
    return _block_2123;
  }
}

int64_t _M0MPC15array5Array3popGiE(struct _M0TPB5ArrayGiE* _M0L4selfS613) {
  int32_t _M0L3lenS612;
  #line 592 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L3lenS612 = _M0L4selfS613->$1;
  if (_M0L3lenS612 == 0) {
    return 4294967296ll;
  } else {
    int32_t _M0L5indexS614 = _M0L3lenS612 - 1;
    int32_t* _M0L3bufS1601 = _M0L4selfS613->$0;
    int32_t _M0L1vS615 = (int32_t)_M0L3bufS1601[_M0L5indexS614];
    _M0L4selfS613->$1 = _M0L5indexS614;
    return (int64_t)_M0L1vS615;
  }
}

float _M0MPC15array5Array2atGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS597,
  int32_t _M0L5indexS598
) {
  int32_t _M0L3lenS596;
  #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS596 = _M0L4selfS597->$1;
  if (_M0L5indexS598 >= 0 && _M0L5indexS598 < _M0L3lenS596) {
    float* _M0L6_2atmpS1596;
    float _result_2124;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS1596 = _M0MPC15array5Array6bufferGfE(_M0L4selfS597);
    _result_2124 = (float)_M0L6_2atmpS1596[_M0L5indexS598];
    moonbit_decref(_M0L6_2atmpS1596);
    return _result_2124;
  } else {
    #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
}

struct _M0TP26RiantR8snn__mbt7Monitor* _M0MPC15array5Array2atGRP26RiantR8snn__mbt7MonitorE(
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt7MonitorE* _M0L4selfS600,
  int32_t _M0L5indexS601
) {
  int32_t _M0L3lenS599;
  #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS599 = _M0L4selfS600->$1;
  if (_M0L5indexS601 >= 0 && _M0L5indexS601 < _M0L3lenS599) {
    struct _M0TP26RiantR8snn__mbt7Monitor** _M0L6_2atmpS1597;
    struct _M0TP26RiantR8snn__mbt7Monitor* _M0L6_2atmpS2050;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS1597
    = _M0MPC15array5Array6bufferGRP26RiantR8snn__mbt7MonitorE(_M0L4selfS600);
    _M0L6_2atmpS2050
    = (struct _M0TP26RiantR8snn__mbt7Monitor*)_M0L6_2atmpS1597[
        _M0L5indexS601
      ];
    if (_M0L6_2atmpS2050) {
      moonbit_incref(_M0L6_2atmpS2050);
    }
    moonbit_decref(_M0L6_2atmpS1597);
    return _M0L6_2atmpS2050;
  } else {
    #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
}

int32_t _M0MPC15array5Array2atGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS603,
  int32_t _M0L5indexS604
) {
  int32_t _M0L3lenS602;
  #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS602 = _M0L4selfS603->$1;
  if (_M0L5indexS604 >= 0 && _M0L5indexS604 < _M0L3lenS602) {
    int32_t* _M0L6_2atmpS1598;
    int32_t _result_2125;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS1598 = _M0MPC15array5Array6bufferGiE(_M0L4selfS603);
    _result_2125 = (int32_t)_M0L6_2atmpS1598[_M0L5indexS604];
    moonbit_decref(_M0L6_2atmpS1598);
    return _result_2125;
  } else {
    #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
}

int32_t _M0MPC15array5Array2atGbE(
  struct _M0TPB5ArrayGbE* _M0L4selfS606,
  int32_t _M0L5indexS607
) {
  int32_t _M0L3lenS605;
  #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS605 = _M0L4selfS606->$1;
  if (_M0L5indexS607 >= 0 && _M0L5indexS607 < _M0L3lenS605) {
    uint8_t* _M0L6_2atmpS1599;
    int32_t _result_2126;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS1599 = _M0MPC15array5Array6bufferGbE(_M0L4selfS606);
    _result_2126 = (int32_t)_M0L6_2atmpS1599[_M0L5indexS607];
    moonbit_decref(_M0L6_2atmpS1599);
    return _result_2126;
  } else {
    #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
}

int32_t _M0FPB7printlnGsE(moonbit_string_t _M0L5inputS595) {
  moonbit_string_t _M0L6_2atmpS1595;
  #line 36 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\console.mbt"
  #line 37 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\console.mbt"
  _M0L6_2atmpS1595 = _M0IPC16string6StringPB4Show10to__string(_M0L5inputS595);
  #line 37 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\console.mbt"
  moonbit_println(_M0L6_2atmpS1595);
  moonbit_decref(_M0L6_2atmpS1595);
  return 0;
}

moonbit_string_t _M0MPC16double6Double10to__string(double _M0L4selfS594) {
  #line 296 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double.mbt"
  #line 298 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double.mbt"
  return _M0FPB15ryu__to__string(_M0L4selfS594);
}

moonbit_string_t _M0FPB15ryu__to__string(double _M0L3valS579) {
  uint64_t _M0L4bitsS582;
  uint64_t _M0L6_2atmpS1594;
  uint64_t _M0L6_2atmpS1593;
  int32_t _M0L8ieeeSignS583;
  uint64_t _M0L12ieeeMantissaS584;
  uint64_t _M0L6_2atmpS1592;
  uint64_t _M0L6_2atmpS1591;
  int32_t _M0L12ieeeExponentS585;
  struct _M0TPB17FloatingDecimal64* _M0L7_2abindS586;
  struct _M0TPB17FloatingDecimal64* _M0L1vS587;
  moonbit_string_t _result_2128;
  #line 668 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  if (_M0L3valS579 == 0x0p+0) {
    return (moonbit_string_t)moonbit_string_literal_12.data;
  }
  if (_M0L3valS579 >= -0x1p+53 && _M0L3valS579 <= 0x1p+53) {
    if (_M0L3valS579 >= -0x1p+31 && _M0L3valS579 <= 0x1.fffffffcp+30) {
      int32_t _M0L1iS580;
      double _M0L6_2atmpS1580;
      #line 683 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0L1iS580 = _M0MPC16double6Double7to__int(_M0L3valS579);
      _M0L6_2atmpS1580 = (double)_M0L1iS580;
      if (_M0L6_2atmpS1580 == _M0L3valS579) {
        #line 685 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        return _M0MPC13int3Int18to__string_2einner(_M0L1iS580, 10);
      }
    } else {
      int64_t _M0L1iS581;
      double _M0L6_2atmpS1581;
      #line 688 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0L1iS581 = _M0MPC16double6Double9to__int64(_M0L3valS579);
      _M0L6_2atmpS1581 = (double)_M0L1iS581;
      if (_M0L6_2atmpS1581 == _M0L3valS579) {
        #line 690 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        return _M0MPC15int645Int6418to__string_2einner(_M0L1iS581, 10);
      }
    }
  }
  _M0L4bitsS582 = *(int64_t*)&_M0L3valS579;
  _M0L6_2atmpS1594 = _M0L4bitsS582 >> 63;
  _M0L6_2atmpS1593 = _M0L6_2atmpS1594 & 1ull;
  _M0L8ieeeSignS583 = _M0L6_2atmpS1593 != 0ull;
  _M0L12ieeeMantissaS584 = _M0L4bitsS582 & 4503599627370495ull;
  _M0L6_2atmpS1592 = _M0L4bitsS582 >> 52;
  _M0L6_2atmpS1591 = _M0L6_2atmpS1592 & 2047ull;
  _M0L12ieeeExponentS585 = (int32_t)_M0L6_2atmpS1591;
  if (
    _M0L12ieeeExponentS585 == 2047
    || _M0L12ieeeExponentS585 == 0 && _M0L12ieeeMantissaS584 == 0ull
  ) {
    int32_t _M0L6_2atmpS1582 = _M0L12ieeeExponentS585 != 0;
    int32_t _M0L6_2atmpS1583 = _M0L12ieeeMantissaS584 != 0ull;
    #line 707 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    return _M0FPB18copy__special__str(_M0L8ieeeSignS583, _M0L6_2atmpS1582, _M0L6_2atmpS1583);
  }
  #line 709 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS586
  = _M0FPB15d2d__small__int(_M0L12ieeeMantissaS584, _M0L12ieeeExponentS585);
  if (_M0L7_2abindS586 == 0) {
    uint32_t _M0L6_2atmpS1584;
    if (_M0L7_2abindS586) {
      moonbit_decref(_M0L7_2abindS586);
    }
    _M0L6_2atmpS1584 = *(uint32_t*)&_M0L12ieeeExponentS585;
    #line 719 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L1vS587 = _M0FPB3d2d(_M0L12ieeeMantissaS584, _M0L6_2atmpS1584);
  } else {
    struct _M0TPB17FloatingDecimal64* _M0L7_2aSomeS588 = _M0L7_2abindS586;
    struct _M0TPB17FloatingDecimal64* _M0L4_2afS589 = _M0L7_2aSomeS588;
    struct _M0TPB17FloatingDecimal64* _M0L1xS590 = _M0L4_2afS589;
    while (1) {
      uint64_t _M0L8mantissaS1590 = _M0L1xS590->$0;
      uint64_t _M0L1qS591 = _M0L8mantissaS1590 / 10ull;
      uint64_t _M0L8mantissaS1588 = _M0L1xS590->$0;
      uint64_t _M0L6_2atmpS1589 = 10ull * _M0L1qS591;
      uint64_t _M0L1rS592 = _M0L8mantissaS1588 - _M0L6_2atmpS1589;
      int32_t _M0L8exponentS1587;
      int32_t _M0L6_2atmpS1586;
      struct _M0TPB17FloatingDecimal64* _M0L6_2atmpS1585;
      if (_M0L1rS592 != 0ull) {
        _M0L1vS587 = _M0L1xS590;
        break;
      }
      _M0L8exponentS1587 = _M0L1xS590->$1;
      moonbit_decref(_M0L1xS590);
      _M0L6_2atmpS1586 = _M0L8exponentS1587 + 1;
      _M0L6_2atmpS1585
      = (struct _M0TPB17FloatingDecimal64*)moonbit_malloc(sizeof(struct _M0TPB17FloatingDecimal64));
      Moonbit_object_header(_M0L6_2atmpS1585)->meta
      = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
      _M0L6_2atmpS1585->$0 = _M0L1qS591;
      _M0L6_2atmpS1585->$1 = _M0L6_2atmpS1586;
      _M0L1xS590 = _M0L6_2atmpS1585;
      continue;
      break;
    }
  }
  #line 721 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _result_2128 = _M0FPB9to__chars(_M0L1vS587, _M0L8ieeeSignS583);
  moonbit_decref(_M0L1vS587);
  return _result_2128;
}

struct _M0TPB17FloatingDecimal64* _M0FPB15d2d__small__int(
  uint64_t _M0L12ieeeMantissaS574,
  int32_t _M0L12ieeeExponentS576
) {
  uint64_t _M0L2m2S573;
  int32_t _M0L6_2atmpS1579;
  int32_t _M0L2e2S575;
  int32_t _M0L6_2atmpS1578;
  uint64_t _M0L6_2atmpS1577;
  uint64_t _M0L4maskS577;
  uint64_t _M0L8fractionS578;
  int32_t _M0L6_2atmpS1576;
  uint64_t _M0L6_2atmpS1575;
  struct _M0TPB17FloatingDecimal64* _M0L6_2atmpS1574;
  #line 637 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L2m2S573 = 4503599627370496ull | _M0L12ieeeMantissaS574;
  _M0L6_2atmpS1579 = _M0L12ieeeExponentS576 - 1023;
  _M0L2e2S575 = _M0L6_2atmpS1579 - 52;
  if (_M0L2e2S575 > 0) {
    return 0;
  }
  if (_M0L2e2S575 < -52) {
    return 0;
  }
  _M0L6_2atmpS1578 = -_M0L2e2S575;
  _M0L6_2atmpS1577 = 1ull << (_M0L6_2atmpS1578 & 63);
  _M0L4maskS577 = _M0L6_2atmpS1577 - 1ull;
  _M0L8fractionS578 = _M0L2m2S573 & _M0L4maskS577;
  if (_M0L8fractionS578 != 0ull) {
    return 0;
  }
  _M0L6_2atmpS1576 = -_M0L2e2S575;
  _M0L6_2atmpS1575 = _M0L2m2S573 >> (_M0L6_2atmpS1576 & 63);
  _M0L6_2atmpS1574
  = (struct _M0TPB17FloatingDecimal64*)moonbit_malloc(sizeof(struct _M0TPB17FloatingDecimal64));
  Moonbit_object_header(_M0L6_2atmpS1574)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L6_2atmpS1574->$0 = _M0L6_2atmpS1575;
  _M0L6_2atmpS1574->$1 = 0;
  return _M0L6_2atmpS1574;
}

moonbit_string_t _M0FPB9to__chars(
  struct _M0TPB17FloatingDecimal64* _M0L1vS541,
  int32_t _M0L4signS539
) {
  moonbit_bytes_t _M0L6resultS537;
  int32_t _M0Lm5indexS538;
  uint64_t _M0L6outputS540;
  int32_t _M0L7olengthS542;
  int32_t _M0L8exponentS1573;
  int32_t _M0L6_2atmpS1572;
  int32_t _M0Lm3expS543;
  int32_t _M0L6_2atmpS1571;
  int32_t _M0L6_2atmpS1569;
  int32_t _M0L18scientificNotationS544;
  #line 530 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6resultS537 = (moonbit_bytes_t)moonbit_make_bytes(25, 0);
  _M0Lm5indexS538 = 0;
  if (_M0L4signS539) {
    int32_t _M0L6_2atmpS1443 = _M0Lm5indexS538;
    int32_t _M0L6_2atmpS1444;
    if (
      _M0L6_2atmpS1443 < 0
      || _M0L6_2atmpS1443 >= Moonbit_array_length(_M0L6resultS537)
    ) {
      #line 535 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      moonbit_panic();
    }
    _M0L6resultS537[_M0L6_2atmpS1443] = 45;
    _M0L6_2atmpS1444 = _M0Lm5indexS538;
    _M0Lm5indexS538 = _M0L6_2atmpS1444 + 1;
  }
  _M0L6outputS540 = _M0L1vS541->$0;
  #line 539 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7olengthS542 = _M0FPB17decimal__length17(_M0L6outputS540);
  _M0L8exponentS1573 = _M0L1vS541->$1;
  _M0L6_2atmpS1572 = _M0L8exponentS1573 + _M0L7olengthS542;
  _M0Lm3expS543 = _M0L6_2atmpS1572 - 1;
  _M0L6_2atmpS1571 = _M0Lm3expS543;
  if (_M0L6_2atmpS1571 >= -6) {
    int32_t _M0L6_2atmpS1570 = _M0Lm3expS543;
    _M0L6_2atmpS1569 = _M0L6_2atmpS1570 < 21;
  } else {
    _M0L6_2atmpS1569 = 0;
  }
  _M0L18scientificNotationS544 = !_M0L6_2atmpS1569;
  if (_M0L18scientificNotationS544) {
    int32_t _M0L7_2abindS545 = _M0L7olengthS542 - 1;
    uint64_t _M0L6outputS546;
    int32_t _M0L1iS547 = 0;
    uint64_t _M0L6outputS548 = _M0L6outputS540;
    int32_t _M0L6_2atmpS1445;
    int32_t _M0L6_2atmpS1449;
    int32_t _M0L6_2atmpS1448;
    int32_t _M0L6_2atmpS1447;
    int32_t _M0L6_2atmpS1446;
    int32_t _M0L6_2atmpS1453;
    int32_t _M0L6_2atmpS1454;
    int32_t _M0L6_2atmpS1455;
    int32_t _M0L6_2atmpS1456;
    int32_t _M0L6_2atmpS1457;
    int32_t _M0L6_2atmpS1463;
    int32_t _M0L6_2atmpS1496;
    moonbit_string_t _result_2130;
    while (1) {
      if (_M0L1iS547 < _M0L7_2abindS545) {
        uint64_t _M0L1cS549 = _M0L6outputS548 % 10ull;
        int32_t _M0L6_2atmpS1502 = _M0Lm5indexS538;
        int32_t _M0L6_2atmpS1501 = _M0L6_2atmpS1502 + _M0L7olengthS542;
        int32_t _M0L6_2atmpS1497 = _M0L6_2atmpS1501 - _M0L1iS547;
        int32_t _M0L6_2atmpS1500 = (int32_t)_M0L1cS549;
        int32_t _M0L6_2atmpS1499 = 48 + _M0L6_2atmpS1500;
        int32_t _M0L6_2atmpS1498 = _M0L6_2atmpS1499 & 0xff;
        int32_t _M0L6_2atmpS1503;
        uint64_t _M0L6_2atmpS1504;
        if (
          _M0L6_2atmpS1497 < 0
          || _M0L6_2atmpS1497 >= Moonbit_array_length(_M0L6resultS537)
        ) {
          #line 547 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
          moonbit_panic();
        }
        _M0L6resultS537[_M0L6_2atmpS1497] = _M0L6_2atmpS1498;
        _M0L6_2atmpS1503 = _M0L1iS547 + 1;
        _M0L6_2atmpS1504 = _M0L6outputS548 / 10ull;
        _M0L1iS547 = _M0L6_2atmpS1503;
        _M0L6outputS548 = _M0L6_2atmpS1504;
        continue;
      } else {
        _M0L6outputS546 = _M0L6outputS548;
      }
      break;
    }
    _M0L6_2atmpS1445 = _M0Lm5indexS538;
    _M0L6_2atmpS1449 = (int32_t)_M0L6outputS546;
    _M0L6_2atmpS1448 = _M0L6_2atmpS1449 % 10;
    _M0L6_2atmpS1447 = 48 + _M0L6_2atmpS1448;
    _M0L6_2atmpS1446 = _M0L6_2atmpS1447 & 0xff;
    if (
      _M0L6_2atmpS1445 < 0
      || _M0L6_2atmpS1445 >= Moonbit_array_length(_M0L6resultS537)
    ) {
      #line 552 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      moonbit_panic();
    }
    _M0L6resultS537[_M0L6_2atmpS1445] = _M0L6_2atmpS1446;
    if (_M0L7olengthS542 > 1) {
      int32_t _M0L6_2atmpS1451 = _M0Lm5indexS538;
      int32_t _M0L6_2atmpS1450 = _M0L6_2atmpS1451 + 1;
      if (
        _M0L6_2atmpS1450 < 0
        || _M0L6_2atmpS1450 >= Moonbit_array_length(_M0L6resultS537)
      ) {
        #line 554 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS537[_M0L6_2atmpS1450] = 46;
    } else {
      int32_t _M0L6_2atmpS1452 = _M0Lm5indexS538;
      _M0Lm5indexS538 = _M0L6_2atmpS1452 - 1;
    }
    _M0L6_2atmpS1453 = _M0Lm5indexS538;
    _M0L6_2atmpS1454 = _M0L7olengthS542 + 1;
    _M0Lm5indexS538 = _M0L6_2atmpS1453 + _M0L6_2atmpS1454;
    _M0L6_2atmpS1455 = _M0Lm5indexS538;
    if (
      _M0L6_2atmpS1455 < 0
      || _M0L6_2atmpS1455 >= Moonbit_array_length(_M0L6resultS537)
    ) {
      #line 562 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      moonbit_panic();
    }
    _M0L6resultS537[_M0L6_2atmpS1455] = 101;
    _M0L6_2atmpS1456 = _M0Lm5indexS538;
    _M0Lm5indexS538 = _M0L6_2atmpS1456 + 1;
    _M0L6_2atmpS1457 = _M0Lm3expS543;
    if (_M0L6_2atmpS1457 < 0) {
      int32_t _M0L6_2atmpS1458 = _M0Lm5indexS538;
      int32_t _M0L6_2atmpS1459;
      int32_t _M0L6_2atmpS1460;
      if (
        _M0L6_2atmpS1458 < 0
        || _M0L6_2atmpS1458 >= Moonbit_array_length(_M0L6resultS537)
      ) {
        #line 565 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS537[_M0L6_2atmpS1458] = 45;
      _M0L6_2atmpS1459 = _M0Lm5indexS538;
      _M0Lm5indexS538 = _M0L6_2atmpS1459 + 1;
      _M0L6_2atmpS1460 = _M0Lm3expS543;
      _M0Lm3expS543 = -_M0L6_2atmpS1460;
    } else {
      int32_t _M0L6_2atmpS1461 = _M0Lm5indexS538;
      int32_t _M0L6_2atmpS1462;
      if (
        _M0L6_2atmpS1461 < 0
        || _M0L6_2atmpS1461 >= Moonbit_array_length(_M0L6resultS537)
      ) {
        #line 569 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS537[_M0L6_2atmpS1461] = 43;
      _M0L6_2atmpS1462 = _M0Lm5indexS538;
      _M0Lm5indexS538 = _M0L6_2atmpS1462 + 1;
    }
    _M0L6_2atmpS1463 = _M0Lm3expS543;
    if (_M0L6_2atmpS1463 >= 100) {
      int32_t _M0L6_2atmpS1479 = _M0Lm3expS543;
      int32_t _M0L1aS551 = _M0L6_2atmpS1479 / 100;
      int32_t _M0L6_2atmpS1478 = _M0Lm3expS543;
      int32_t _M0L6_2atmpS1477 = _M0L6_2atmpS1478 / 10;
      int32_t _M0L1bS552 = _M0L6_2atmpS1477 % 10;
      int32_t _M0L6_2atmpS1476 = _M0Lm3expS543;
      int32_t _M0L1cS553 = _M0L6_2atmpS1476 % 10;
      int32_t _M0L6_2atmpS1464 = _M0Lm5indexS538;
      int32_t _M0L6_2atmpS1466 = 48 + _M0L1aS551;
      int32_t _M0L6_2atmpS1465 = _M0L6_2atmpS1466 & 0xff;
      int32_t _M0L6_2atmpS1470;
      int32_t _M0L6_2atmpS1467;
      int32_t _M0L6_2atmpS1469;
      int32_t _M0L6_2atmpS1468;
      int32_t _M0L6_2atmpS1474;
      int32_t _M0L6_2atmpS1471;
      int32_t _M0L6_2atmpS1473;
      int32_t _M0L6_2atmpS1472;
      int32_t _M0L6_2atmpS1475;
      if (
        _M0L6_2atmpS1464 < 0
        || _M0L6_2atmpS1464 >= Moonbit_array_length(_M0L6resultS537)
      ) {
        #line 576 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS537[_M0L6_2atmpS1464] = _M0L6_2atmpS1465;
      _M0L6_2atmpS1470 = _M0Lm5indexS538;
      _M0L6_2atmpS1467 = _M0L6_2atmpS1470 + 1;
      _M0L6_2atmpS1469 = 48 + _M0L1bS552;
      _M0L6_2atmpS1468 = _M0L6_2atmpS1469 & 0xff;
      if (
        _M0L6_2atmpS1467 < 0
        || _M0L6_2atmpS1467 >= Moonbit_array_length(_M0L6resultS537)
      ) {
        #line 577 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS537[_M0L6_2atmpS1467] = _M0L6_2atmpS1468;
      _M0L6_2atmpS1474 = _M0Lm5indexS538;
      _M0L6_2atmpS1471 = _M0L6_2atmpS1474 + 2;
      _M0L6_2atmpS1473 = 48 + _M0L1cS553;
      _M0L6_2atmpS1472 = _M0L6_2atmpS1473 & 0xff;
      if (
        _M0L6_2atmpS1471 < 0
        || _M0L6_2atmpS1471 >= Moonbit_array_length(_M0L6resultS537)
      ) {
        #line 578 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS537[_M0L6_2atmpS1471] = _M0L6_2atmpS1472;
      _M0L6_2atmpS1475 = _M0Lm5indexS538;
      _M0Lm5indexS538 = _M0L6_2atmpS1475 + 3;
    } else {
      int32_t _M0L6_2atmpS1480 = _M0Lm3expS543;
      if (_M0L6_2atmpS1480 >= 10) {
        int32_t _M0L6_2atmpS1490 = _M0Lm3expS543;
        int32_t _M0L1aS554 = _M0L6_2atmpS1490 / 10;
        int32_t _M0L6_2atmpS1489 = _M0Lm3expS543;
        int32_t _M0L1bS555 = _M0L6_2atmpS1489 % 10;
        int32_t _M0L6_2atmpS1481 = _M0Lm5indexS538;
        int32_t _M0L6_2atmpS1483 = 48 + _M0L1aS554;
        int32_t _M0L6_2atmpS1482 = _M0L6_2atmpS1483 & 0xff;
        int32_t _M0L6_2atmpS1487;
        int32_t _M0L6_2atmpS1484;
        int32_t _M0L6_2atmpS1486;
        int32_t _M0L6_2atmpS1485;
        int32_t _M0L6_2atmpS1488;
        if (
          _M0L6_2atmpS1481 < 0
          || _M0L6_2atmpS1481 >= Moonbit_array_length(_M0L6resultS537)
        ) {
          #line 583 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
          moonbit_panic();
        }
        _M0L6resultS537[_M0L6_2atmpS1481] = _M0L6_2atmpS1482;
        _M0L6_2atmpS1487 = _M0Lm5indexS538;
        _M0L6_2atmpS1484 = _M0L6_2atmpS1487 + 1;
        _M0L6_2atmpS1486 = 48 + _M0L1bS555;
        _M0L6_2atmpS1485 = _M0L6_2atmpS1486 & 0xff;
        if (
          _M0L6_2atmpS1484 < 0
          || _M0L6_2atmpS1484 >= Moonbit_array_length(_M0L6resultS537)
        ) {
          #line 584 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
          moonbit_panic();
        }
        _M0L6resultS537[_M0L6_2atmpS1484] = _M0L6_2atmpS1485;
        _M0L6_2atmpS1488 = _M0Lm5indexS538;
        _M0Lm5indexS538 = _M0L6_2atmpS1488 + 2;
      } else {
        int32_t _M0L6_2atmpS1491 = _M0Lm5indexS538;
        int32_t _M0L6_2atmpS1494 = _M0Lm3expS543;
        int32_t _M0L6_2atmpS1493 = 48 + _M0L6_2atmpS1494;
        int32_t _M0L6_2atmpS1492 = _M0L6_2atmpS1493 & 0xff;
        int32_t _M0L6_2atmpS1495;
        if (
          _M0L6_2atmpS1491 < 0
          || _M0L6_2atmpS1491 >= Moonbit_array_length(_M0L6resultS537)
        ) {
          #line 587 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
          moonbit_panic();
        }
        _M0L6resultS537[_M0L6_2atmpS1491] = _M0L6_2atmpS1492;
        _M0L6_2atmpS1495 = _M0Lm5indexS538;
        _M0Lm5indexS538 = _M0L6_2atmpS1495 + 1;
      }
    }
    _M0L6_2atmpS1496 = _M0Lm5indexS538;
    #line 590 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _result_2130
    = _M0FPB19string__from__bytes(_M0L6resultS537, 0, _M0L6_2atmpS1496);
    moonbit_decref(_M0L6resultS537);
    return _result_2130;
  } else {
    int32_t _M0L6_2atmpS1505 = _M0Lm3expS543;
    int32_t _M0L6_2atmpS1568;
    moonbit_string_t _result_2136;
    if (_M0L6_2atmpS1505 < 0) {
      int32_t _M0L6_2atmpS1506 = _M0Lm5indexS538;
      int32_t _M0L6_2atmpS1508;
      int32_t _M0L6_2atmpS1507;
      int32_t _M0L6_2atmpS1509;
      int32_t _M0L1iS556;
      int32_t _M0L6_2atmpS1524;
      int32_t _M0L6_2atmpS1526;
      int32_t _M0L6_2atmpS1525;
      int32_t _M0L7currentS558;
      int32_t _M0L1iS559;
      uint64_t _M0L6outputS560;
      if (
        _M0L6_2atmpS1506 < 0
        || _M0L6_2atmpS1506 >= Moonbit_array_length(_M0L6resultS537)
      ) {
        #line 595 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS537[_M0L6_2atmpS1506] = 48;
      _M0L6_2atmpS1508 = _M0Lm5indexS538;
      _M0L6_2atmpS1507 = _M0L6_2atmpS1508 + 1;
      if (
        _M0L6_2atmpS1507 < 0
        || _M0L6_2atmpS1507 >= Moonbit_array_length(_M0L6resultS537)
      ) {
        #line 596 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS537[_M0L6_2atmpS1507] = 46;
      _M0L6_2atmpS1509 = _M0Lm5indexS538;
      _M0Lm5indexS538 = _M0L6_2atmpS1509 + 2;
      _M0L1iS556 = -1;
      while (1) {
        int32_t _M0L6_2atmpS1510 = _M0Lm3expS543;
        if (_M0L1iS556 > _M0L6_2atmpS1510) {
          int32_t _M0L6_2atmpS1513 = _M0Lm5indexS538;
          int32_t _M0L6_2atmpS1512 = _M0L6_2atmpS1513 - _M0L1iS556;
          int32_t _M0L6_2atmpS1511 = _M0L6_2atmpS1512 - 1;
          int32_t _M0L6_2atmpS1514;
          if (
            _M0L6_2atmpS1511 < 0
            || _M0L6_2atmpS1511 >= Moonbit_array_length(_M0L6resultS537)
          ) {
            #line 599 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
            moonbit_panic();
          }
          _M0L6resultS537[_M0L6_2atmpS1511] = 48;
          _M0L6_2atmpS1514 = _M0L1iS556 - 1;
          _M0L1iS556 = _M0L6_2atmpS1514;
          continue;
        }
        break;
      }
      _M0L6_2atmpS1524 = _M0Lm5indexS538;
      _M0L6_2atmpS1526 = _M0Lm3expS543;
      _M0L6_2atmpS1525 = -1 - _M0L6_2atmpS1526;
      _M0L7currentS558 = _M0L6_2atmpS1524 + _M0L6_2atmpS1525;
      _M0L1iS559 = 0;
      _M0L6outputS560 = _M0L6outputS540;
      while (1) {
        if (_M0L1iS559 < _M0L7olengthS542) {
          int32_t _M0L6_2atmpS1521 = _M0L7currentS558 + _M0L7olengthS542;
          int32_t _M0L6_2atmpS1520 = _M0L6_2atmpS1521 - _M0L1iS559;
          int32_t _M0L6_2atmpS1515 = _M0L6_2atmpS1520 - 1;
          uint64_t _M0L6_2atmpS1519 = _M0L6outputS560 % 10ull;
          int32_t _M0L6_2atmpS1518 = (int32_t)_M0L6_2atmpS1519;
          int32_t _M0L6_2atmpS1517 = 48 + _M0L6_2atmpS1518;
          int32_t _M0L6_2atmpS1516 = _M0L6_2atmpS1517 & 0xff;
          int32_t _M0L6_2atmpS1522;
          uint64_t _M0L6_2atmpS1523;
          if (
            _M0L6_2atmpS1515 < 0
            || _M0L6_2atmpS1515 >= Moonbit_array_length(_M0L6resultS537)
          ) {
            #line 603 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
            moonbit_panic();
          }
          _M0L6resultS537[_M0L6_2atmpS1515] = _M0L6_2atmpS1516;
          _M0L6_2atmpS1522 = _M0L1iS559 + 1;
          _M0L6_2atmpS1523 = _M0L6outputS560 / 10ull;
          _M0L1iS559 = _M0L6_2atmpS1522;
          _M0L6outputS560 = _M0L6_2atmpS1523;
          continue;
        }
        break;
      }
      _M0Lm5indexS538 = _M0L7currentS558 + _M0L7olengthS542;
    } else {
      int32_t _M0L6_2atmpS1528 = _M0Lm3expS543;
      int32_t _M0L6_2atmpS1527 = _M0L6_2atmpS1528 + 1;
      if (_M0L6_2atmpS1527 >= _M0L7olengthS542) {
        int32_t _M0L1iS562 = 0;
        uint64_t _M0L6outputS563 = _M0L6outputS540;
        int32_t _M0L6_2atmpS1539;
        int32_t _M0L6_2atmpS1544;
        int32_t _M0L7_2abindS565;
        int32_t _M0L1iS566;
        int32_t _M0L6_2atmpS1545;
        int32_t _M0L6_2atmpS1548;
        int32_t _M0L6_2atmpS1547;
        int32_t _M0L6_2atmpS1546;
        while (1) {
          if (_M0L1iS562 < _M0L7olengthS542) {
            int32_t _M0L6_2atmpS1536 = _M0Lm5indexS538;
            int32_t _M0L6_2atmpS1535 = _M0L6_2atmpS1536 + _M0L7olengthS542;
            int32_t _M0L6_2atmpS1534 = _M0L6_2atmpS1535 - _M0L1iS562;
            int32_t _M0L6_2atmpS1529 = _M0L6_2atmpS1534 - 1;
            uint64_t _M0L6_2atmpS1533 = _M0L6outputS563 % 10ull;
            int32_t _M0L6_2atmpS1532 = (int32_t)_M0L6_2atmpS1533;
            int32_t _M0L6_2atmpS1531 = 48 + _M0L6_2atmpS1532;
            int32_t _M0L6_2atmpS1530 = _M0L6_2atmpS1531 & 0xff;
            int32_t _M0L6_2atmpS1537;
            uint64_t _M0L6_2atmpS1538;
            if (
              _M0L6_2atmpS1529 < 0
              || _M0L6_2atmpS1529 >= Moonbit_array_length(_M0L6resultS537)
            ) {
              #line 610 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
              moonbit_panic();
            }
            _M0L6resultS537[_M0L6_2atmpS1529] = _M0L6_2atmpS1530;
            _M0L6_2atmpS1537 = _M0L1iS562 + 1;
            _M0L6_2atmpS1538 = _M0L6outputS563 / 10ull;
            _M0L1iS562 = _M0L6_2atmpS1537;
            _M0L6outputS563 = _M0L6_2atmpS1538;
            continue;
          }
          break;
        }
        _M0L6_2atmpS1539 = _M0Lm5indexS538;
        _M0Lm5indexS538 = _M0L6_2atmpS1539 + _M0L7olengthS542;
        _M0L6_2atmpS1544 = _M0Lm3expS543;
        _M0L7_2abindS565 = _M0L6_2atmpS1544 + 1;
        _M0L1iS566 = _M0L7olengthS542;
        while (1) {
          if (_M0L1iS566 < _M0L7_2abindS565) {
            int32_t _M0L6_2atmpS1542 = _M0Lm5indexS538;
            int32_t _M0L6_2atmpS1541 = _M0L6_2atmpS1542 + _M0L1iS566;
            int32_t _M0L6_2atmpS1540 = _M0L6_2atmpS1541 - _M0L7olengthS542;
            int32_t _M0L6_2atmpS1543;
            if (
              _M0L6_2atmpS1540 < 0
              || _M0L6_2atmpS1540 >= Moonbit_array_length(_M0L6resultS537)
            ) {
              #line 615 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
              moonbit_panic();
            }
            _M0L6resultS537[_M0L6_2atmpS1540] = 48;
            _M0L6_2atmpS1543 = _M0L1iS566 + 1;
            _M0L1iS566 = _M0L6_2atmpS1543;
            continue;
          }
          break;
        }
        _M0L6_2atmpS1545 = _M0Lm5indexS538;
        _M0L6_2atmpS1548 = _M0Lm3expS543;
        _M0L6_2atmpS1547 = _M0L6_2atmpS1548 + 1;
        _M0L6_2atmpS1546 = _M0L6_2atmpS1547 - _M0L7olengthS542;
        _M0Lm5indexS538 = _M0L6_2atmpS1545 + _M0L6_2atmpS1546;
      } else {
        int32_t _M0L6_2atmpS1565 = _M0Lm5indexS538;
        int32_t _M0L6_2atmpS1564 = _M0L6_2atmpS1565 + 1;
        int32_t _M0L1iS568 = 0;
        int32_t _M0L7currentS569 = _M0L6_2atmpS1564;
        uint64_t _M0L6outputS570 = _M0L6outputS540;
        int32_t _M0L6_2atmpS1566;
        int32_t _M0L6_2atmpS1567;
        while (1) {
          if (_M0L1iS568 < _M0L7olengthS542) {
            int32_t _M0L6_2atmpS1560 = _M0L7olengthS542 - _M0L1iS568;
            int32_t _M0L6_2atmpS1558 = _M0L6_2atmpS1560 - 1;
            int32_t _M0L6_2atmpS1559 = _M0Lm3expS543;
            int32_t _M0L7currentS571;
            int32_t _M0L6_2atmpS1555;
            int32_t _M0L6_2atmpS1554;
            int32_t _M0L6_2atmpS1549;
            uint64_t _M0L6_2atmpS1553;
            int32_t _M0L6_2atmpS1552;
            int32_t _M0L6_2atmpS1551;
            int32_t _M0L6_2atmpS1550;
            int32_t _M0L6_2atmpS1556;
            uint64_t _M0L6_2atmpS1557;
            if (_M0L6_2atmpS1558 == _M0L6_2atmpS1559) {
              int32_t _M0L6_2atmpS1563 = _M0L7currentS569 + _M0L7olengthS542;
              int32_t _M0L6_2atmpS1562 = _M0L6_2atmpS1563 - _M0L1iS568;
              int32_t _M0L6_2atmpS1561 = _M0L6_2atmpS1562 - 1;
              if (
                _M0L6_2atmpS1561 < 0
                || _M0L6_2atmpS1561 >= Moonbit_array_length(_M0L6resultS537)
              ) {
                #line 622 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
                moonbit_panic();
              }
              _M0L6resultS537[_M0L6_2atmpS1561] = 46;
              _M0L7currentS571 = _M0L7currentS569 - 1;
            } else {
              _M0L7currentS571 = _M0L7currentS569;
            }
            _M0L6_2atmpS1555 = _M0L7currentS571 + _M0L7olengthS542;
            _M0L6_2atmpS1554 = _M0L6_2atmpS1555 - _M0L1iS568;
            _M0L6_2atmpS1549 = _M0L6_2atmpS1554 - 1;
            _M0L6_2atmpS1553 = _M0L6outputS570 % 10ull;
            _M0L6_2atmpS1552 = (int32_t)_M0L6_2atmpS1553;
            _M0L6_2atmpS1551 = 48 + _M0L6_2atmpS1552;
            _M0L6_2atmpS1550 = _M0L6_2atmpS1551 & 0xff;
            if (
              _M0L6_2atmpS1549 < 0
              || _M0L6_2atmpS1549 >= Moonbit_array_length(_M0L6resultS537)
            ) {
              #line 627 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
              moonbit_panic();
            }
            _M0L6resultS537[_M0L6_2atmpS1549] = _M0L6_2atmpS1550;
            _M0L6_2atmpS1556 = _M0L1iS568 + 1;
            _M0L6_2atmpS1557 = _M0L6outputS570 / 10ull;
            _M0L1iS568 = _M0L6_2atmpS1556;
            _M0L7currentS569 = _M0L7currentS571;
            _M0L6outputS570 = _M0L6_2atmpS1557;
            continue;
          }
          break;
        }
        _M0L6_2atmpS1566 = _M0Lm5indexS538;
        _M0L6_2atmpS1567 = _M0L7olengthS542 + 1;
        _M0Lm5indexS538 = _M0L6_2atmpS1566 + _M0L6_2atmpS1567;
      }
    }
    _M0L6_2atmpS1568 = _M0Lm5indexS538;
    #line 632 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _result_2136
    = _M0FPB19string__from__bytes(_M0L6resultS537, 0, _M0L6_2atmpS1568);
    moonbit_decref(_M0L6resultS537);
    return _result_2136;
  }
}

struct _M0TPB17FloatingDecimal64* _M0FPB3d2d(
  uint64_t _M0L12ieeeMantissaS483,
  uint32_t _M0L12ieeeExponentS482
) {
  int32_t _M0Lm2e2S480;
  uint64_t _M0Lm2m2S481;
  uint64_t _M0L6_2atmpS1442;
  uint64_t _M0L6_2atmpS1441;
  int32_t _M0L4evenS484;
  uint64_t _M0L6_2atmpS1440;
  uint64_t _M0L2mvS485;
  int32_t _M0L7mmShiftS486;
  uint64_t _M0Lm2vrS487;
  uint64_t _M0Lm2vpS488;
  uint64_t _M0Lm2vmS489;
  int32_t _M0Lm3e10S490;
  int32_t _M0Lm17vmIsTrailingZerosS491;
  int32_t _M0Lm17vrIsTrailingZerosS492;
  int32_t _M0L6_2atmpS1342;
  int32_t _M0Lm7removedS511;
  int32_t _M0Lm16lastRemovedDigitS512;
  uint64_t _M0Lm6outputS513;
  int32_t _M0L6_2atmpS1438;
  int32_t _M0L6_2atmpS1439;
  int32_t _M0L3expS536;
  uint64_t _M0L6_2atmpS1437;
  struct _M0TPB17FloatingDecimal64* _block_2142;
  #line 347 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0Lm2e2S480 = 0;
  _M0Lm2m2S481 = 0ull;
  if (_M0L12ieeeExponentS482 == 0u) {
    _M0Lm2e2S480 = -1076;
    _M0Lm2m2S481 = _M0L12ieeeMantissaS483;
  } else {
    int32_t _M0L6_2atmpS1341 = *(int32_t*)&_M0L12ieeeExponentS482;
    int32_t _M0L6_2atmpS1340 = _M0L6_2atmpS1341 - 1023;
    int32_t _M0L6_2atmpS1339 = _M0L6_2atmpS1340 - 52;
    _M0Lm2e2S480 = _M0L6_2atmpS1339 - 2;
    _M0Lm2m2S481 = 4503599627370496ull | _M0L12ieeeMantissaS483;
  }
  _M0L6_2atmpS1442 = _M0Lm2m2S481;
  _M0L6_2atmpS1441 = _M0L6_2atmpS1442 & 1ull;
  _M0L4evenS484 = _M0L6_2atmpS1441 == 0ull;
  _M0L6_2atmpS1440 = _M0Lm2m2S481;
  _M0L2mvS485 = 4ull * _M0L6_2atmpS1440;
  _M0L7mmShiftS486
  = _M0L12ieeeMantissaS483 != 0ull || _M0L12ieeeExponentS482 <= 1u;
  _M0Lm2vrS487 = 0ull;
  _M0Lm2vpS488 = 0ull;
  _M0Lm2vmS489 = 0ull;
  _M0Lm3e10S490 = 0;
  _M0Lm17vmIsTrailingZerosS491 = 0;
  _M0Lm17vrIsTrailingZerosS492 = 0;
  _M0L6_2atmpS1342 = _M0Lm2e2S480;
  if (_M0L6_2atmpS1342 >= 0) {
    int32_t _M0L6_2atmpS1364 = _M0Lm2e2S480;
    int32_t _M0L6_2atmpS1360;
    int32_t _M0L6_2atmpS1363;
    int32_t _M0L6_2atmpS1362;
    int32_t _M0L6_2atmpS1361;
    int32_t _M0L1qS493;
    int32_t _M0L6_2atmpS1359;
    int32_t _M0L6_2atmpS1358;
    int32_t _M0L1kS494;
    int32_t _M0L6_2atmpS1357;
    int32_t _M0L6_2atmpS1356;
    int32_t _M0L6_2atmpS1355;
    int32_t _M0L1iS495;
    struct _M0TPB8Pow5Pair _M0L4pow5S496;
    uint64_t _M0L6_2atmpS1354;
    struct _M0TPB19MulShiftAll64Result _M0L7_2abindS497;
    uint64_t _M0L8_2avrOutS498;
    uint64_t _M0L8_2avpOutS499;
    uint64_t _M0L8_2avmOutS500;
    #line 383 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1360 = _M0FPB9log10Pow2(_M0L6_2atmpS1364);
    _M0L6_2atmpS1363 = _M0Lm2e2S480;
    _M0L6_2atmpS1362 = _M0L6_2atmpS1363 > 3;
    #line 383 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1361 = _M0MPC14bool4Bool7to__int(_M0L6_2atmpS1362);
    _M0L1qS493 = _M0L6_2atmpS1360 - _M0L6_2atmpS1361;
    _M0Lm3e10S490 = _M0L1qS493;
    #line 385 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1359 = _M0FPB8pow5bits(_M0L1qS493);
    _M0L6_2atmpS1358 = 125 + _M0L6_2atmpS1359;
    _M0L1kS494 = _M0L6_2atmpS1358 - 1;
    _M0L6_2atmpS1357 = _M0Lm2e2S480;
    _M0L6_2atmpS1356 = -_M0L6_2atmpS1357;
    _M0L6_2atmpS1355 = _M0L6_2atmpS1356 + _M0L1qS493;
    _M0L1iS495 = _M0L6_2atmpS1355 + _M0L1kS494;
    #line 387 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L4pow5S496 = _M0FPB22double__computeInvPow5(_M0L1qS493);
    _M0L6_2atmpS1354 = _M0Lm2m2S481;
    #line 388 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L7_2abindS497
    = _M0FPB13mulShiftAll64(_M0L6_2atmpS1354, _M0L4pow5S496, _M0L1iS495, _M0L7mmShiftS486);
    _M0L8_2avrOutS498 = _M0L7_2abindS497.$0;
    _M0L8_2avpOutS499 = _M0L7_2abindS497.$1;
    _M0L8_2avmOutS500 = _M0L7_2abindS497.$2;
    _M0Lm2vrS487 = _M0L8_2avrOutS498;
    _M0Lm2vpS488 = _M0L8_2avpOutS499;
    _M0Lm2vmS489 = _M0L8_2avmOutS500;
    if (_M0L1qS493 <= 21) {
      int32_t _M0L6_2atmpS1350 = (int32_t)_M0L2mvS485;
      uint64_t _M0L6_2atmpS1353 = _M0L2mvS485 / 5ull;
      int32_t _M0L6_2atmpS1352 = (int32_t)_M0L6_2atmpS1353;
      int32_t _M0L6_2atmpS1351 = 5 * _M0L6_2atmpS1352;
      int32_t _M0L6mvMod5S501 = _M0L6_2atmpS1350 - _M0L6_2atmpS1351;
      if (_M0L6mvMod5S501 == 0) {
        #line 400 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0Lm17vrIsTrailingZerosS492
        = _M0FPB18multipleOfPowerOf5(_M0L2mvS485, _M0L1qS493);
      } else if (_M0L4evenS484) {
        uint64_t _M0L6_2atmpS1344 = _M0L2mvS485 - 1ull;
        uint64_t _M0L6_2atmpS1345;
        uint64_t _M0L6_2atmpS1343;
        #line 406 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0L6_2atmpS1345 = _M0MPC14bool4Bool10to__uint64(_M0L7mmShiftS486);
        _M0L6_2atmpS1343 = _M0L6_2atmpS1344 - _M0L6_2atmpS1345;
        #line 405 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0Lm17vmIsTrailingZerosS491
        = _M0FPB18multipleOfPowerOf5(_M0L6_2atmpS1343, _M0L1qS493);
      } else {
        uint64_t _M0L6_2atmpS1346 = _M0Lm2vpS488;
        uint64_t _M0L6_2atmpS1349 = _M0L2mvS485 + 2ull;
        int32_t _M0L6_2atmpS1348;
        uint64_t _M0L6_2atmpS1347;
        #line 410 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0L6_2atmpS1348
        = _M0FPB18multipleOfPowerOf5(_M0L6_2atmpS1349, _M0L1qS493);
        #line 410 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0L6_2atmpS1347 = _M0MPC14bool4Bool10to__uint64(_M0L6_2atmpS1348);
        _M0Lm2vpS488 = _M0L6_2atmpS1346 - _M0L6_2atmpS1347;
      }
    }
  } else {
    int32_t _M0L6_2atmpS1378 = _M0Lm2e2S480;
    int32_t _M0L6_2atmpS1377 = -_M0L6_2atmpS1378;
    int32_t _M0L6_2atmpS1372;
    int32_t _M0L6_2atmpS1376;
    int32_t _M0L6_2atmpS1375;
    int32_t _M0L6_2atmpS1374;
    int32_t _M0L6_2atmpS1373;
    int32_t _M0L1qS502;
    int32_t _M0L6_2atmpS1365;
    int32_t _M0L6_2atmpS1371;
    int32_t _M0L6_2atmpS1370;
    int32_t _M0L1iS503;
    int32_t _M0L6_2atmpS1369;
    int32_t _M0L1kS504;
    int32_t _M0L1jS505;
    struct _M0TPB8Pow5Pair _M0L4pow5S506;
    uint64_t _M0L6_2atmpS1368;
    struct _M0TPB19MulShiftAll64Result _M0L7_2abindS507;
    uint64_t _M0L8_2avrOutS508;
    uint64_t _M0L8_2avpOutS509;
    uint64_t _M0L8_2avmOutS510;
    #line 415 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1372 = _M0FPB9log10Pow5(_M0L6_2atmpS1377);
    _M0L6_2atmpS1376 = _M0Lm2e2S480;
    _M0L6_2atmpS1375 = -_M0L6_2atmpS1376;
    _M0L6_2atmpS1374 = _M0L6_2atmpS1375 > 1;
    #line 415 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1373 = _M0MPC14bool4Bool7to__int(_M0L6_2atmpS1374);
    _M0L1qS502 = _M0L6_2atmpS1372 - _M0L6_2atmpS1373;
    _M0L6_2atmpS1365 = _M0Lm2e2S480;
    _M0Lm3e10S490 = _M0L1qS502 + _M0L6_2atmpS1365;
    _M0L6_2atmpS1371 = _M0Lm2e2S480;
    _M0L6_2atmpS1370 = -_M0L6_2atmpS1371;
    _M0L1iS503 = _M0L6_2atmpS1370 - _M0L1qS502;
    #line 418 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1369 = _M0FPB8pow5bits(_M0L1iS503);
    _M0L1kS504 = _M0L6_2atmpS1369 - 125;
    _M0L1jS505 = _M0L1qS502 - _M0L1kS504;
    #line 420 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L4pow5S506 = _M0FPB19double__computePow5(_M0L1iS503);
    _M0L6_2atmpS1368 = _M0Lm2m2S481;
    #line 421 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L7_2abindS507
    = _M0FPB13mulShiftAll64(_M0L6_2atmpS1368, _M0L4pow5S506, _M0L1jS505, _M0L7mmShiftS486);
    _M0L8_2avrOutS508 = _M0L7_2abindS507.$0;
    _M0L8_2avpOutS509 = _M0L7_2abindS507.$1;
    _M0L8_2avmOutS510 = _M0L7_2abindS507.$2;
    _M0Lm2vrS487 = _M0L8_2avrOutS508;
    _M0Lm2vpS488 = _M0L8_2avpOutS509;
    _M0Lm2vmS489 = _M0L8_2avmOutS510;
    if (_M0L1qS502 <= 1) {
      _M0Lm17vrIsTrailingZerosS492 = 1;
      if (_M0L4evenS484) {
        int32_t _M0L6_2atmpS1366;
        #line 432 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0L6_2atmpS1366 = _M0MPC14bool4Bool7to__int(_M0L7mmShiftS486);
        _M0Lm17vmIsTrailingZerosS491 = _M0L6_2atmpS1366 == 1;
      } else {
        uint64_t _M0L6_2atmpS1367 = _M0Lm2vpS488;
        _M0Lm2vpS488 = _M0L6_2atmpS1367 - 1ull;
      }
    } else if (_M0L1qS502 < 63) {
      #line 437 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0Lm17vrIsTrailingZerosS492
      = _M0FPB18multipleOfPowerOf2(_M0L2mvS485, _M0L1qS502);
    }
  }
  _M0Lm7removedS511 = 0;
  _M0Lm16lastRemovedDigitS512 = 0;
  _M0Lm6outputS513 = 0ull;
  if (_M0Lm17vmIsTrailingZerosS491 || _M0Lm17vrIsTrailingZerosS492) {
    int32_t _if__result_2139;
    uint64_t _M0L6_2atmpS1408;
    uint64_t _M0L6_2atmpS1414;
    uint64_t _M0L6_2atmpS1415;
    int32_t _if__result_2140;
    int32_t _M0L6_2atmpS1411;
    int64_t _M0L6_2atmpS1410;
    uint64_t _M0L6_2atmpS1409;
    while (1) {
      uint64_t _M0L6_2atmpS1391 = _M0Lm2vpS488;
      uint64_t _M0L7vpDiv10S514 = _M0L6_2atmpS1391 / 10ull;
      uint64_t _M0L6_2atmpS1390 = _M0Lm2vmS489;
      uint64_t _M0L7vmDiv10S515 = _M0L6_2atmpS1390 / 10ull;
      uint64_t _M0L6_2atmpS1389;
      int32_t _M0L6_2atmpS1386;
      int32_t _M0L6_2atmpS1388;
      int32_t _M0L6_2atmpS1387;
      int32_t _M0L7vmMod10S517;
      uint64_t _M0L6_2atmpS1385;
      uint64_t _M0L7vrDiv10S518;
      uint64_t _M0L6_2atmpS1384;
      int32_t _M0L6_2atmpS1381;
      int32_t _M0L6_2atmpS1383;
      int32_t _M0L6_2atmpS1382;
      int32_t _M0L7vrMod10S519;
      int32_t _M0L6_2atmpS1380;
      if (_M0L7vpDiv10S514 <= _M0L7vmDiv10S515) {
        break;
      }
      _M0L6_2atmpS1389 = _M0Lm2vmS489;
      _M0L6_2atmpS1386 = (int32_t)_M0L6_2atmpS1389;
      _M0L6_2atmpS1388 = (int32_t)_M0L7vmDiv10S515;
      _M0L6_2atmpS1387 = 10 * _M0L6_2atmpS1388;
      _M0L7vmMod10S517 = _M0L6_2atmpS1386 - _M0L6_2atmpS1387;
      _M0L6_2atmpS1385 = _M0Lm2vrS487;
      _M0L7vrDiv10S518 = _M0L6_2atmpS1385 / 10ull;
      _M0L6_2atmpS1384 = _M0Lm2vrS487;
      _M0L6_2atmpS1381 = (int32_t)_M0L6_2atmpS1384;
      _M0L6_2atmpS1383 = (int32_t)_M0L7vrDiv10S518;
      _M0L6_2atmpS1382 = 10 * _M0L6_2atmpS1383;
      _M0L7vrMod10S519 = _M0L6_2atmpS1381 - _M0L6_2atmpS1382;
      _M0Lm17vmIsTrailingZerosS491
      = _M0Lm17vmIsTrailingZerosS491 && _M0L7vmMod10S517 == 0;
      if (_M0Lm17vrIsTrailingZerosS492) {
        int32_t _M0L6_2atmpS1379 = _M0Lm16lastRemovedDigitS512;
        _M0Lm17vrIsTrailingZerosS492 = _M0L6_2atmpS1379 == 0;
      } else {
        _M0Lm17vrIsTrailingZerosS492 = 0;
      }
      _M0Lm16lastRemovedDigitS512 = _M0L7vrMod10S519;
      _M0Lm2vrS487 = _M0L7vrDiv10S518;
      _M0Lm2vpS488 = _M0L7vpDiv10S514;
      _M0Lm2vmS489 = _M0L7vmDiv10S515;
      _M0L6_2atmpS1380 = _M0Lm7removedS511;
      _M0Lm7removedS511 = _M0L6_2atmpS1380 + 1;
      continue;
      break;
    }
    if (_M0Lm17vmIsTrailingZerosS491) {
      while (1) {
        uint64_t _M0L6_2atmpS1404 = _M0Lm2vmS489;
        uint64_t _M0L7vmDiv10S520 = _M0L6_2atmpS1404 / 10ull;
        uint64_t _M0L6_2atmpS1403 = _M0Lm2vmS489;
        int32_t _M0L6_2atmpS1400 = (int32_t)_M0L6_2atmpS1403;
        int32_t _M0L6_2atmpS1402 = (int32_t)_M0L7vmDiv10S520;
        int32_t _M0L6_2atmpS1401 = 10 * _M0L6_2atmpS1402;
        int32_t _M0L7vmMod10S521 = _M0L6_2atmpS1400 - _M0L6_2atmpS1401;
        uint64_t _M0L6_2atmpS1399;
        uint64_t _M0L7vpDiv10S523;
        uint64_t _M0L6_2atmpS1398;
        uint64_t _M0L7vrDiv10S524;
        uint64_t _M0L6_2atmpS1397;
        int32_t _M0L6_2atmpS1394;
        int32_t _M0L6_2atmpS1396;
        int32_t _M0L6_2atmpS1395;
        int32_t _M0L7vrMod10S525;
        int32_t _M0L6_2atmpS1393;
        if (_M0L7vmMod10S521 != 0) {
          break;
        }
        _M0L6_2atmpS1399 = _M0Lm2vpS488;
        _M0L7vpDiv10S523 = _M0L6_2atmpS1399 / 10ull;
        _M0L6_2atmpS1398 = _M0Lm2vrS487;
        _M0L7vrDiv10S524 = _M0L6_2atmpS1398 / 10ull;
        _M0L6_2atmpS1397 = _M0Lm2vrS487;
        _M0L6_2atmpS1394 = (int32_t)_M0L6_2atmpS1397;
        _M0L6_2atmpS1396 = (int32_t)_M0L7vrDiv10S524;
        _M0L6_2atmpS1395 = 10 * _M0L6_2atmpS1396;
        _M0L7vrMod10S525 = _M0L6_2atmpS1394 - _M0L6_2atmpS1395;
        if (_M0Lm17vrIsTrailingZerosS492) {
          int32_t _M0L6_2atmpS1392 = _M0Lm16lastRemovedDigitS512;
          _M0Lm17vrIsTrailingZerosS492 = _M0L6_2atmpS1392 == 0;
        } else {
          _M0Lm17vrIsTrailingZerosS492 = 0;
        }
        _M0Lm16lastRemovedDigitS512 = _M0L7vrMod10S525;
        _M0Lm2vrS487 = _M0L7vrDiv10S524;
        _M0Lm2vpS488 = _M0L7vpDiv10S523;
        _M0Lm2vmS489 = _M0L7vmDiv10S520;
        _M0L6_2atmpS1393 = _M0Lm7removedS511;
        _M0Lm7removedS511 = _M0L6_2atmpS1393 + 1;
        continue;
        break;
      }
    }
    if (_M0Lm17vrIsTrailingZerosS492) {
      int32_t _M0L6_2atmpS1407 = _M0Lm16lastRemovedDigitS512;
      if (_M0L6_2atmpS1407 == 5) {
        uint64_t _M0L6_2atmpS1406 = _M0Lm2vrS487;
        uint64_t _M0L6_2atmpS1405 = _M0L6_2atmpS1406 % 2ull;
        _if__result_2139 = _M0L6_2atmpS1405 == 0ull;
      } else {
        _if__result_2139 = 0;
      }
    } else {
      _if__result_2139 = 0;
    }
    if (_if__result_2139) {
      _M0Lm16lastRemovedDigitS512 = 4;
    }
    _M0L6_2atmpS1408 = _M0Lm2vrS487;
    _M0L6_2atmpS1414 = _M0Lm2vrS487;
    _M0L6_2atmpS1415 = _M0Lm2vmS489;
    if (_M0L6_2atmpS1414 == _M0L6_2atmpS1415) {
      if (!_M0L4evenS484) {
        _if__result_2140 = 1;
      } else {
        int32_t _M0L6_2atmpS1413 = _M0Lm17vmIsTrailingZerosS491;
        _if__result_2140 = !_M0L6_2atmpS1413;
      }
    } else {
      _if__result_2140 = 0;
    }
    if (_if__result_2140) {
      _M0L6_2atmpS1411 = 1;
    } else {
      int32_t _M0L6_2atmpS1412 = _M0Lm16lastRemovedDigitS512;
      _M0L6_2atmpS1411 = _M0L6_2atmpS1412 >= 5;
    }
    #line 487 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1410 = _M0MPC14bool4Bool9to__int64(_M0L6_2atmpS1411);
    _M0L6_2atmpS1409 = *(uint64_t*)&_M0L6_2atmpS1410;
    _M0Lm6outputS513 = _M0L6_2atmpS1408 + _M0L6_2atmpS1409;
  } else {
    int32_t _M0Lm7roundUpS526 = 0;
    uint64_t _M0L6_2atmpS1436 = _M0Lm2vpS488;
    uint64_t _M0L8vpDiv100S527 = _M0L6_2atmpS1436 / 100ull;
    uint64_t _M0L6_2atmpS1435 = _M0Lm2vmS489;
    uint64_t _M0L8vmDiv100S528 = _M0L6_2atmpS1435 / 100ull;
    uint64_t _M0L6_2atmpS1430;
    uint64_t _M0L6_2atmpS1433;
    uint64_t _M0L6_2atmpS1434;
    int32_t _M0L6_2atmpS1432;
    uint64_t _M0L6_2atmpS1431;
    if (_M0L8vpDiv100S527 > _M0L8vmDiv100S528) {
      uint64_t _M0L6_2atmpS1421 = _M0Lm2vrS487;
      uint64_t _M0L8vrDiv100S529 = _M0L6_2atmpS1421 / 100ull;
      uint64_t _M0L6_2atmpS1420 = _M0Lm2vrS487;
      int32_t _M0L6_2atmpS1417 = (int32_t)_M0L6_2atmpS1420;
      int32_t _M0L6_2atmpS1419 = (int32_t)_M0L8vrDiv100S529;
      int32_t _M0L6_2atmpS1418 = 100 * _M0L6_2atmpS1419;
      int32_t _M0L8vrMod100S530 = _M0L6_2atmpS1417 - _M0L6_2atmpS1418;
      int32_t _M0L6_2atmpS1416;
      _M0Lm7roundUpS526 = _M0L8vrMod100S530 >= 50;
      _M0Lm2vrS487 = _M0L8vrDiv100S529;
      _M0Lm2vpS488 = _M0L8vpDiv100S527;
      _M0Lm2vmS489 = _M0L8vmDiv100S528;
      _M0L6_2atmpS1416 = _M0Lm7removedS511;
      _M0Lm7removedS511 = _M0L6_2atmpS1416 + 2;
    }
    while (1) {
      uint64_t _M0L6_2atmpS1429 = _M0Lm2vpS488;
      uint64_t _M0L7vpDiv10S531 = _M0L6_2atmpS1429 / 10ull;
      uint64_t _M0L6_2atmpS1428 = _M0Lm2vmS489;
      uint64_t _M0L7vmDiv10S532 = _M0L6_2atmpS1428 / 10ull;
      uint64_t _M0L6_2atmpS1427;
      uint64_t _M0L7vrDiv10S534;
      uint64_t _M0L6_2atmpS1426;
      int32_t _M0L6_2atmpS1423;
      int32_t _M0L6_2atmpS1425;
      int32_t _M0L6_2atmpS1424;
      int32_t _M0L7vrMod10S535;
      int32_t _M0L6_2atmpS1422;
      if (_M0L7vpDiv10S531 <= _M0L7vmDiv10S532) {
        break;
      }
      _M0L6_2atmpS1427 = _M0Lm2vrS487;
      _M0L7vrDiv10S534 = _M0L6_2atmpS1427 / 10ull;
      _M0L6_2atmpS1426 = _M0Lm2vrS487;
      _M0L6_2atmpS1423 = (int32_t)_M0L6_2atmpS1426;
      _M0L6_2atmpS1425 = (int32_t)_M0L7vrDiv10S534;
      _M0L6_2atmpS1424 = 10 * _M0L6_2atmpS1425;
      _M0L7vrMod10S535 = _M0L6_2atmpS1423 - _M0L6_2atmpS1424;
      _M0Lm7roundUpS526 = _M0L7vrMod10S535 >= 5;
      _M0Lm2vrS487 = _M0L7vrDiv10S534;
      _M0Lm2vpS488 = _M0L7vpDiv10S531;
      _M0Lm2vmS489 = _M0L7vmDiv10S532;
      _M0L6_2atmpS1422 = _M0Lm7removedS511;
      _M0Lm7removedS511 = _M0L6_2atmpS1422 + 1;
      continue;
      break;
    }
    _M0L6_2atmpS1430 = _M0Lm2vrS487;
    _M0L6_2atmpS1433 = _M0Lm2vrS487;
    _M0L6_2atmpS1434 = _M0Lm2vmS489;
    _M0L6_2atmpS1432
    = _M0L6_2atmpS1433 == _M0L6_2atmpS1434 || _M0Lm7roundUpS526;
    #line 522 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1431 = _M0MPC14bool4Bool10to__uint64(_M0L6_2atmpS1432);
    _M0Lm6outputS513 = _M0L6_2atmpS1430 + _M0L6_2atmpS1431;
  }
  _M0L6_2atmpS1438 = _M0Lm3e10S490;
  _M0L6_2atmpS1439 = _M0Lm7removedS511;
  _M0L3expS536 = _M0L6_2atmpS1438 + _M0L6_2atmpS1439;
  _M0L6_2atmpS1437 = _M0Lm6outputS513;
  _block_2142
  = (struct _M0TPB17FloatingDecimal64*)moonbit_malloc(sizeof(struct _M0TPB17FloatingDecimal64));
  Moonbit_object_header(_block_2142)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_2142->$0 = _M0L6_2atmpS1437;
  _block_2142->$1 = _M0L3expS536;
  return _block_2142;
}

uint64_t _M0MPC14bool4Bool10to__uint64(int32_t _M0L4selfS479) {
  #line 110 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bool.mbt"
  if (_M0L4selfS479) {
    return 1ull;
  } else {
    return 0ull;
  }
}

int64_t _M0MPC14bool4Bool9to__int64(int32_t _M0L4selfS478) {
  #line 58 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bool.mbt"
  if (_M0L4selfS478) {
    return 1ll;
  } else {
    return 0ll;
  }
}

int32_t _M0MPC14bool4Bool7to__int(int32_t _M0L4selfS477) {
  #line 32 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bool.mbt"
  if (_M0L4selfS477) {
    return 1;
  } else {
    return 0;
  }
}

int32_t _M0FPB17decimal__length17(uint64_t _M0L1vS476) {
  #line 280 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  if (_M0L1vS476 >= 10000000000000000ull) {
    return 17;
  }
  if (_M0L1vS476 >= 1000000000000000ull) {
    return 16;
  }
  if (_M0L1vS476 >= 100000000000000ull) {
    return 15;
  }
  if (_M0L1vS476 >= 10000000000000ull) {
    return 14;
  }
  if (_M0L1vS476 >= 1000000000000ull) {
    return 13;
  }
  if (_M0L1vS476 >= 100000000000ull) {
    return 12;
  }
  if (_M0L1vS476 >= 10000000000ull) {
    return 11;
  }
  if (_M0L1vS476 >= 1000000000ull) {
    return 10;
  }
  if (_M0L1vS476 >= 100000000ull) {
    return 9;
  }
  if (_M0L1vS476 >= 10000000ull) {
    return 8;
  }
  if (_M0L1vS476 >= 1000000ull) {
    return 7;
  }
  if (_M0L1vS476 >= 100000ull) {
    return 6;
  }
  if (_M0L1vS476 >= 10000ull) {
    return 5;
  }
  if (_M0L1vS476 >= 1000ull) {
    return 4;
  }
  if (_M0L1vS476 >= 100ull) {
    return 3;
  }
  if (_M0L1vS476 >= 10ull) {
    return 2;
  }
  return 1;
}

struct _M0TPB8Pow5Pair _M0FPB22double__computeInvPow5(int32_t _M0L1iS459) {
  int32_t _M0L6_2atmpS1338;
  int32_t _M0L6_2atmpS1337;
  int32_t _M0L4baseS458;
  int32_t _M0L5base2S460;
  int32_t _M0L6offsetS461;
  int32_t _M0L6_2atmpS1336;
  uint64_t _M0L4mul0S462;
  int32_t _M0L6_2atmpS1335;
  int32_t _M0L6_2atmpS1334;
  uint64_t _M0L4mul1S463;
  uint64_t _M0L1mS464;
  struct _M0TPB7Umul128 _M0L7_2abindS465;
  uint64_t _M0L7_2alow1S466;
  uint64_t _M0L8_2ahigh1S467;
  struct _M0TPB7Umul128 _M0L7_2abindS468;
  uint64_t _M0L7_2alow0S469;
  uint64_t _M0L8_2ahigh0S470;
  uint64_t _M0L3sumS471;
  uint64_t _M0Lm5high1S472;
  int32_t _M0L6_2atmpS1332;
  int32_t _M0L6_2atmpS1333;
  int32_t _M0L5deltaS473;
  uint64_t _M0L6_2atmpS1331;
  uint64_t _M0L6_2atmpS1323;
  int32_t _M0L6_2atmpS1330;
  uint32_t _M0L6_2atmpS1327;
  int32_t _M0L6_2atmpS1329;
  int32_t _M0L6_2atmpS1328;
  uint32_t _M0L6_2atmpS1326;
  uint32_t _M0L6_2atmpS1325;
  uint64_t _M0L6_2atmpS1324;
  uint64_t _M0L1aS474;
  uint64_t _M0L6_2atmpS1322;
  uint64_t _M0L1bS475;
  #line 239 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1338 = _M0L1iS459 + 26;
  _M0L6_2atmpS1337 = _M0L6_2atmpS1338 - 1;
  _M0L4baseS458 = _M0L6_2atmpS1337 / 26;
  _M0L5base2S460 = _M0L4baseS458 * 26;
  _M0L6offsetS461 = _M0L5base2S460 - _M0L1iS459;
  _M0L6_2atmpS1336 = _M0L4baseS458 * 2;
  #line 243 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4mul0S462
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB26gDOUBLE__POW5__INV__SPLIT2, _M0L6_2atmpS1336);
  _M0L6_2atmpS1335 = _M0L4baseS458 * 2;
  _M0L6_2atmpS1334 = _M0L6_2atmpS1335 + 1;
  #line 244 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4mul1S463
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB26gDOUBLE__POW5__INV__SPLIT2, _M0L6_2atmpS1334);
  if (_M0L6offsetS461 == 0) {
    return (struct _M0TPB8Pow5Pair){.$0 = _M0L4mul0S462, .$1 = _M0L4mul1S463};
  }
  #line 248 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L1mS464
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB20gDOUBLE__POW5__TABLE, _M0L6offsetS461);
  #line 249 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS465 = _M0FPB7umul128(_M0L1mS464, _M0L4mul1S463);
  _M0L7_2alow1S466 = _M0L7_2abindS465.$0;
  _M0L8_2ahigh1S467 = _M0L7_2abindS465.$1;
  #line 250 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS468 = _M0FPB7umul128(_M0L1mS464, _M0L4mul0S462);
  _M0L7_2alow0S469 = _M0L7_2abindS468.$0;
  _M0L8_2ahigh0S470 = _M0L7_2abindS468.$1;
  _M0L3sumS471 = _M0L8_2ahigh0S470 + _M0L7_2alow1S466;
  _M0Lm5high1S472 = _M0L8_2ahigh1S467;
  if (_M0L3sumS471 < _M0L8_2ahigh0S470) {
    uint64_t _M0L6_2atmpS1321 = _M0Lm5high1S472;
    _M0Lm5high1S472 = _M0L6_2atmpS1321 + 1ull;
  }
  #line 256 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1332 = _M0FPB8pow5bits(_M0L5base2S460);
  #line 256 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1333 = _M0FPB8pow5bits(_M0L1iS459);
  _M0L5deltaS473 = _M0L6_2atmpS1332 - _M0L6_2atmpS1333;
  #line 257 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1331
  = _M0FPB13shiftright128(_M0L7_2alow0S469, _M0L3sumS471, _M0L5deltaS473);
  _M0L6_2atmpS1323 = _M0L6_2atmpS1331 + 1ull;
  _M0L6_2atmpS1330 = _M0L1iS459 / 16;
  #line 259 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1327
  = _M0MPC15array13ReadOnlyArray2atGjE(_M0FPB19gPOW5__INV__OFFSETS, _M0L6_2atmpS1330);
  _M0L6_2atmpS1329 = _M0L1iS459 % 16;
  _M0L6_2atmpS1328 = _M0L6_2atmpS1329 << 1;
  _M0L6_2atmpS1326 = _M0L6_2atmpS1327 >> (_M0L6_2atmpS1328 & 31);
  _M0L6_2atmpS1325 = _M0L6_2atmpS1326 & 3u;
  #line 259 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1324 = _M0MPC14uint4UInt10to__uint64(_M0L6_2atmpS1325);
  _M0L1aS474 = _M0L6_2atmpS1323 + _M0L6_2atmpS1324;
  _M0L6_2atmpS1322 = _M0Lm5high1S472;
  #line 260 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L1bS475
  = _M0FPB13shiftright128(_M0L3sumS471, _M0L6_2atmpS1322, _M0L5deltaS473);
  return (struct _M0TPB8Pow5Pair){.$0 = _M0L1aS474, .$1 = _M0L1bS475};
}

struct _M0TPB8Pow5Pair _M0FPB19double__computePow5(int32_t _M0L1iS441) {
  int32_t _M0L4baseS440;
  int32_t _M0L5base2S442;
  int32_t _M0L6offsetS443;
  int32_t _M0L6_2atmpS1320;
  uint64_t _M0L4mul0S444;
  int32_t _M0L6_2atmpS1319;
  int32_t _M0L6_2atmpS1318;
  uint64_t _M0L4mul1S445;
  uint64_t _M0L1mS446;
  struct _M0TPB7Umul128 _M0L7_2abindS447;
  uint64_t _M0L7_2alow1S448;
  uint64_t _M0L8_2ahigh1S449;
  struct _M0TPB7Umul128 _M0L7_2abindS450;
  uint64_t _M0L7_2alow0S451;
  uint64_t _M0L8_2ahigh0S452;
  uint64_t _M0L3sumS453;
  uint64_t _M0Lm5high1S454;
  int32_t _M0L6_2atmpS1316;
  int32_t _M0L6_2atmpS1317;
  int32_t _M0L5deltaS455;
  uint64_t _M0L6_2atmpS1308;
  int32_t _M0L6_2atmpS1315;
  uint32_t _M0L6_2atmpS1312;
  int32_t _M0L6_2atmpS1314;
  int32_t _M0L6_2atmpS1313;
  uint32_t _M0L6_2atmpS1311;
  uint32_t _M0L6_2atmpS1310;
  uint64_t _M0L6_2atmpS1309;
  uint64_t _M0L1aS456;
  uint64_t _M0L6_2atmpS1307;
  uint64_t _M0L1bS457;
  #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4baseS440 = _M0L1iS441 / 26;
  _M0L5base2S442 = _M0L4baseS440 * 26;
  _M0L6offsetS443 = _M0L1iS441 - _M0L5base2S442;
  _M0L6_2atmpS1320 = _M0L4baseS440 * 2;
  #line 217 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4mul0S444
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB21gDOUBLE__POW5__SPLIT2, _M0L6_2atmpS1320);
  _M0L6_2atmpS1319 = _M0L4baseS440 * 2;
  _M0L6_2atmpS1318 = _M0L6_2atmpS1319 + 1;
  #line 218 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4mul1S445
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB21gDOUBLE__POW5__SPLIT2, _M0L6_2atmpS1318);
  if (_M0L6offsetS443 == 0) {
    return (struct _M0TPB8Pow5Pair){.$0 = _M0L4mul0S444, .$1 = _M0L4mul1S445};
  }
  #line 222 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L1mS446
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB20gDOUBLE__POW5__TABLE, _M0L6offsetS443);
  #line 223 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS447 = _M0FPB7umul128(_M0L1mS446, _M0L4mul1S445);
  _M0L7_2alow1S448 = _M0L7_2abindS447.$0;
  _M0L8_2ahigh1S449 = _M0L7_2abindS447.$1;
  #line 224 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS450 = _M0FPB7umul128(_M0L1mS446, _M0L4mul0S444);
  _M0L7_2alow0S451 = _M0L7_2abindS450.$0;
  _M0L8_2ahigh0S452 = _M0L7_2abindS450.$1;
  _M0L3sumS453 = _M0L8_2ahigh0S452 + _M0L7_2alow1S448;
  _M0Lm5high1S454 = _M0L8_2ahigh1S449;
  if (_M0L3sumS453 < _M0L8_2ahigh0S452) {
    uint64_t _M0L6_2atmpS1306 = _M0Lm5high1S454;
    _M0Lm5high1S454 = _M0L6_2atmpS1306 + 1ull;
  }
  #line 230 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1316 = _M0FPB8pow5bits(_M0L1iS441);
  #line 230 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1317 = _M0FPB8pow5bits(_M0L5base2S442);
  _M0L5deltaS455 = _M0L6_2atmpS1316 - _M0L6_2atmpS1317;
  #line 231 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1308
  = _M0FPB13shiftright128(_M0L7_2alow0S451, _M0L3sumS453, _M0L5deltaS455);
  _M0L6_2atmpS1315 = _M0L1iS441 / 16;
  #line 232 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1312
  = _M0MPC15array13ReadOnlyArray2atGjE(_M0FPB14gPOW5__OFFSETS, _M0L6_2atmpS1315);
  _M0L6_2atmpS1314 = _M0L1iS441 % 16;
  _M0L6_2atmpS1313 = _M0L6_2atmpS1314 << 1;
  _M0L6_2atmpS1311 = _M0L6_2atmpS1312 >> (_M0L6_2atmpS1313 & 31);
  _M0L6_2atmpS1310 = _M0L6_2atmpS1311 & 3u;
  #line 232 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1309 = _M0MPC14uint4UInt10to__uint64(_M0L6_2atmpS1310);
  _M0L1aS456 = _M0L6_2atmpS1308 + _M0L6_2atmpS1309;
  _M0L6_2atmpS1307 = _M0Lm5high1S454;
  #line 233 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L1bS457
  = _M0FPB13shiftright128(_M0L3sumS453, _M0L6_2atmpS1307, _M0L5deltaS455);
  return (struct _M0TPB8Pow5Pair){.$0 = _M0L1aS456, .$1 = _M0L1bS457};
}

struct _M0TPB19MulShiftAll64Result _M0FPB13mulShiftAll64(
  uint64_t _M0L1mS414,
  struct _M0TPB8Pow5Pair _M0L3mulS411,
  int32_t _M0L1jS427,
  int32_t _M0L7mmShiftS429
) {
  uint64_t _M0L7_2amul0S410;
  uint64_t _M0L7_2amul1S412;
  uint64_t _M0L1mS413;
  struct _M0TPB7Umul128 _M0L7_2abindS415;
  uint64_t _M0L5_2aloS416;
  uint64_t _M0L6_2atmpS417;
  struct _M0TPB7Umul128 _M0L7_2abindS418;
  uint64_t _M0L6_2alo2S419;
  uint64_t _M0L6_2ahi2S420;
  uint64_t _M0L3midS421;
  uint64_t _M0L6_2atmpS1305;
  uint64_t _M0L2hiS422;
  uint64_t _M0L3lo2S423;
  uint64_t _M0L6_2atmpS1303;
  uint64_t _M0L6_2atmpS1304;
  uint64_t _M0L4mid2S424;
  uint64_t _M0L6_2atmpS1302;
  uint64_t _M0L3hi2S425;
  int32_t _M0L6_2atmpS1301;
  int32_t _M0L6_2atmpS1300;
  uint64_t _M0L2vpS426;
  uint64_t _M0Lm2vmS428;
  int32_t _M0L6_2atmpS1299;
  int32_t _M0L6_2atmpS1298;
  uint64_t _M0L2vrS439;
  uint64_t _M0L6_2atmpS1297;
  #line 129 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2amul0S410 = _M0L3mulS411.$0;
  _M0L7_2amul1S412 = _M0L3mulS411.$1;
  _M0L1mS413 = _M0L1mS414 << 1;
  #line 137 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS415 = _M0FPB7umul128(_M0L1mS413, _M0L7_2amul0S410);
  _M0L5_2aloS416 = _M0L7_2abindS415.$0;
  _M0L6_2atmpS417 = _M0L7_2abindS415.$1;
  #line 138 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS418 = _M0FPB7umul128(_M0L1mS413, _M0L7_2amul1S412);
  _M0L6_2alo2S419 = _M0L7_2abindS418.$0;
  _M0L6_2ahi2S420 = _M0L7_2abindS418.$1;
  _M0L3midS421 = _M0L6_2atmpS417 + _M0L6_2alo2S419;
  if (_M0L3midS421 < _M0L6_2atmpS417) {
    _M0L6_2atmpS1305 = 1ull;
  } else {
    _M0L6_2atmpS1305 = 0ull;
  }
  _M0L2hiS422 = _M0L6_2ahi2S420 + _M0L6_2atmpS1305;
  _M0L3lo2S423 = _M0L5_2aloS416 + _M0L7_2amul0S410;
  _M0L6_2atmpS1303 = _M0L3midS421 + _M0L7_2amul1S412;
  if (_M0L3lo2S423 < _M0L5_2aloS416) {
    _M0L6_2atmpS1304 = 1ull;
  } else {
    _M0L6_2atmpS1304 = 0ull;
  }
  _M0L4mid2S424 = _M0L6_2atmpS1303 + _M0L6_2atmpS1304;
  if (_M0L4mid2S424 < _M0L3midS421) {
    _M0L6_2atmpS1302 = 1ull;
  } else {
    _M0L6_2atmpS1302 = 0ull;
  }
  _M0L3hi2S425 = _M0L2hiS422 + _M0L6_2atmpS1302;
  _M0L6_2atmpS1301 = _M0L1jS427 - 64;
  _M0L6_2atmpS1300 = _M0L6_2atmpS1301 - 1;
  #line 144 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L2vpS426
  = _M0FPB13shiftright128(_M0L4mid2S424, _M0L3hi2S425, _M0L6_2atmpS1300);
  _M0Lm2vmS428 = 0ull;
  if (_M0L7mmShiftS429) {
    uint64_t _M0L3lo3S430 = _M0L5_2aloS416 - _M0L7_2amul0S410;
    uint64_t _M0L6_2atmpS1287 = _M0L3midS421 - _M0L7_2amul1S412;
    uint64_t _M0L6_2atmpS1288;
    uint64_t _M0L4mid3S431;
    uint64_t _M0L6_2atmpS1286;
    uint64_t _M0L3hi3S432;
    int32_t _M0L6_2atmpS1285;
    int32_t _M0L6_2atmpS1284;
    if (_M0L5_2aloS416 < _M0L3lo3S430) {
      _M0L6_2atmpS1288 = 1ull;
    } else {
      _M0L6_2atmpS1288 = 0ull;
    }
    _M0L4mid3S431 = _M0L6_2atmpS1287 - _M0L6_2atmpS1288;
    if (_M0L3midS421 < _M0L4mid3S431) {
      _M0L6_2atmpS1286 = 1ull;
    } else {
      _M0L6_2atmpS1286 = 0ull;
    }
    _M0L3hi3S432 = _M0L2hiS422 - _M0L6_2atmpS1286;
    _M0L6_2atmpS1285 = _M0L1jS427 - 64;
    _M0L6_2atmpS1284 = _M0L6_2atmpS1285 - 1;
    #line 150 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0Lm2vmS428
    = _M0FPB13shiftright128(_M0L4mid3S431, _M0L3hi3S432, _M0L6_2atmpS1284);
  } else {
    uint64_t _M0L3lo3S433 = _M0L5_2aloS416 + _M0L5_2aloS416;
    uint64_t _M0L6_2atmpS1295 = _M0L3midS421 + _M0L3midS421;
    uint64_t _M0L6_2atmpS1296;
    uint64_t _M0L4mid3S434;
    uint64_t _M0L6_2atmpS1293;
    uint64_t _M0L6_2atmpS1294;
    uint64_t _M0L3hi3S435;
    uint64_t _M0L3lo4S436;
    uint64_t _M0L6_2atmpS1291;
    uint64_t _M0L6_2atmpS1292;
    uint64_t _M0L4mid4S437;
    uint64_t _M0L6_2atmpS1290;
    uint64_t _M0L3hi4S438;
    int32_t _M0L6_2atmpS1289;
    if (_M0L3lo3S433 < _M0L5_2aloS416) {
      _M0L6_2atmpS1296 = 1ull;
    } else {
      _M0L6_2atmpS1296 = 0ull;
    }
    _M0L4mid3S434 = _M0L6_2atmpS1295 + _M0L6_2atmpS1296;
    _M0L6_2atmpS1293 = _M0L2hiS422 + _M0L2hiS422;
    if (_M0L4mid3S434 < _M0L3midS421) {
      _M0L6_2atmpS1294 = 1ull;
    } else {
      _M0L6_2atmpS1294 = 0ull;
    }
    _M0L3hi3S435 = _M0L6_2atmpS1293 + _M0L6_2atmpS1294;
    _M0L3lo4S436 = _M0L3lo3S433 - _M0L7_2amul0S410;
    _M0L6_2atmpS1291 = _M0L4mid3S434 - _M0L7_2amul1S412;
    if (_M0L3lo3S433 < _M0L3lo4S436) {
      _M0L6_2atmpS1292 = 1ull;
    } else {
      _M0L6_2atmpS1292 = 0ull;
    }
    _M0L4mid4S437 = _M0L6_2atmpS1291 - _M0L6_2atmpS1292;
    if (_M0L4mid3S434 < _M0L4mid4S437) {
      _M0L6_2atmpS1290 = 1ull;
    } else {
      _M0L6_2atmpS1290 = 0ull;
    }
    _M0L3hi4S438 = _M0L3hi3S435 - _M0L6_2atmpS1290;
    _M0L6_2atmpS1289 = _M0L1jS427 - 64;
    #line 158 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0Lm2vmS428
    = _M0FPB13shiftright128(_M0L4mid4S437, _M0L3hi4S438, _M0L6_2atmpS1289);
  }
  _M0L6_2atmpS1299 = _M0L1jS427 - 64;
  _M0L6_2atmpS1298 = _M0L6_2atmpS1299 - 1;
  #line 160 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L2vrS439
  = _M0FPB13shiftright128(_M0L3midS421, _M0L2hiS422, _M0L6_2atmpS1298);
  _M0L6_2atmpS1297 = _M0Lm2vmS428;
  return (struct _M0TPB19MulShiftAll64Result){.$0 = _M0L2vrS439,
                                                .$1 = _M0L2vpS426,
                                                .$2 = _M0L6_2atmpS1297};
}

int32_t _M0FPB18multipleOfPowerOf2(
  uint64_t _M0L5valueS408,
  int32_t _M0L1pS409
) {
  uint64_t _M0L6_2atmpS1283;
  uint64_t _M0L6_2atmpS1282;
  uint64_t _M0L6_2atmpS1281;
  #line 124 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1283 = 1ull << (_M0L1pS409 & 63);
  _M0L6_2atmpS1282 = _M0L6_2atmpS1283 - 1ull;
  _M0L6_2atmpS1281 = _M0L5valueS408 & _M0L6_2atmpS1282;
  return _M0L6_2atmpS1281 == 0ull;
}

int32_t _M0FPB18multipleOfPowerOf5(
  uint64_t _M0L5valueS406,
  int32_t _M0L1pS407
) {
  int32_t _M0L6_2atmpS1280;
  #line 119 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  #line 120 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1280 = _M0FPB10pow5Factor(_M0L5valueS406);
  return _M0L6_2atmpS1280 >= _M0L1pS407;
}

int32_t _M0FPB10pow5Factor(uint64_t _M0L5valueS401) {
  uint64_t _M0L6_2atmpS1271;
  uint64_t _M0L6_2atmpS1272;
  uint64_t _M0L6_2atmpS1273;
  uint64_t _M0L6_2atmpS1274;
  uint64_t _M0L6_2atmpS1279;
  int32_t _M0L5countS402;
  uint64_t _M0L1vS403;
  #line 94 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1271 = _M0L5valueS401 % 5ull;
  if (_M0L6_2atmpS1271 != 0ull) {
    return 0;
  }
  _M0L6_2atmpS1272 = _M0L5valueS401 % 25ull;
  if (_M0L6_2atmpS1272 != 0ull) {
    return 1;
  }
  _M0L6_2atmpS1273 = _M0L5valueS401 % 125ull;
  if (_M0L6_2atmpS1273 != 0ull) {
    return 2;
  }
  _M0L6_2atmpS1274 = _M0L5valueS401 % 625ull;
  if (_M0L6_2atmpS1274 != 0ull) {
    return 3;
  }
  _M0L6_2atmpS1279 = _M0L5valueS401 / 625ull;
  _M0L5countS402 = 4;
  _M0L1vS403 = _M0L6_2atmpS1279;
  while (1) {
    if (_M0L1vS403 > 0ull) {
      uint64_t _M0L6_2atmpS1275 = _M0L1vS403 % 5ull;
      int32_t _M0L6_2atmpS1276;
      uint64_t _M0L6_2atmpS1277;
      if (_M0L6_2atmpS1275 != 0ull) {
        return _M0L5countS402;
      }
      _M0L6_2atmpS1276 = _M0L5countS402 + 1;
      _M0L6_2atmpS1277 = _M0L1vS403 / 5ull;
      _M0L5countS402 = _M0L6_2atmpS1276;
      _M0L1vS403 = _M0L6_2atmpS1277;
      continue;
    } else {
      struct _M0TPB13StringBuilder* _M0L18_2astring__builderS405;
      moonbit_string_t _M0L6_2atmpS1278;
      int32_t _result_2144;
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0L18_2astring__builderS405
      = _M0MPB13StringBuilder21StringBuilder_2einner(25);
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS405, (moonbit_string_t)moonbit_string_literal_13.data);
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0MPB13StringBuilder13write__objectGmE(_M0L18_2astring__builderS405, _M0L5valueS401);
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0L6_2atmpS1278
      = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS405);
      moonbit_decref(_M0L18_2astring__builderS405);
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _result_2144 = _M0FPC15abort5abortGiE(_M0L6_2atmpS1278);
      moonbit_decref(_M0L6_2atmpS1278);
      return _result_2144;
    }
    break;
  }
}

uint64_t _M0FPB13shiftright128(
  uint64_t _M0L2loS400,
  uint64_t _M0L2hiS398,
  int32_t _M0L4distS399
) {
  int32_t _M0L6_2atmpS1270;
  uint64_t _M0L6_2atmpS1268;
  uint64_t _M0L6_2atmpS1269;
  #line 89 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1270 = 64 - _M0L4distS399;
  _M0L6_2atmpS1268 = _M0L2hiS398 << (_M0L6_2atmpS1270 & 63);
  _M0L6_2atmpS1269 = _M0L2loS400 >> (_M0L4distS399 & 63);
  return _M0L6_2atmpS1268 | _M0L6_2atmpS1269;
}

struct _M0TPB7Umul128 _M0FPB7umul128(
  uint64_t _M0L1aS388,
  uint64_t _M0L1bS391
) {
  uint64_t _M0L3aLoS387;
  uint64_t _M0L3aHiS389;
  uint64_t _M0L3bLoS390;
  uint64_t _M0L3bHiS392;
  uint64_t _M0L1xS393;
  uint64_t _M0L6_2atmpS1266;
  uint64_t _M0L6_2atmpS1267;
  uint64_t _M0L1yS394;
  uint64_t _M0L6_2atmpS1264;
  uint64_t _M0L6_2atmpS1265;
  uint64_t _M0L1zS395;
  uint64_t _M0L6_2atmpS1262;
  uint64_t _M0L6_2atmpS1263;
  uint64_t _M0L6_2atmpS1260;
  uint64_t _M0L6_2atmpS1261;
  uint64_t _M0L1wS396;
  uint64_t _M0L2loS397;
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L3aLoS387 = _M0L1aS388 & 4294967295ull;
  _M0L3aHiS389 = _M0L1aS388 >> 32;
  _M0L3bLoS390 = _M0L1bS391 & 4294967295ull;
  _M0L3bHiS392 = _M0L1bS391 >> 32;
  _M0L1xS393 = _M0L3aLoS387 * _M0L3bLoS390;
  _M0L6_2atmpS1266 = _M0L3aHiS389 * _M0L3bLoS390;
  _M0L6_2atmpS1267 = _M0L1xS393 >> 32;
  _M0L1yS394 = _M0L6_2atmpS1266 + _M0L6_2atmpS1267;
  _M0L6_2atmpS1264 = _M0L3aLoS387 * _M0L3bHiS392;
  _M0L6_2atmpS1265 = _M0L1yS394 & 4294967295ull;
  _M0L1zS395 = _M0L6_2atmpS1264 + _M0L6_2atmpS1265;
  _M0L6_2atmpS1262 = _M0L3aHiS389 * _M0L3bHiS392;
  _M0L6_2atmpS1263 = _M0L1yS394 >> 32;
  _M0L6_2atmpS1260 = _M0L6_2atmpS1262 + _M0L6_2atmpS1263;
  _M0L6_2atmpS1261 = _M0L1zS395 >> 32;
  _M0L1wS396 = _M0L6_2atmpS1260 + _M0L6_2atmpS1261;
  _M0L2loS397 = _M0L1aS388 * _M0L1bS391;
  return (struct _M0TPB7Umul128){.$0 = _M0L2loS397, .$1 = _M0L1wS396};
}

moonbit_string_t _M0FPB19string__from__bytes(
  moonbit_bytes_t _M0L5bytesS385,
  int32_t _M0L4fromS382,
  int32_t _M0L2toS381
) {
  int32_t _M0L3lenS380;
  int32_t _M0L6_2atmpS1259;
  uint16_t* _M0L6bufferS383;
  int32_t _M0L1iS384;
  #line 52 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L3lenS380 = _M0L2toS381 - _M0L4fromS382;
  #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1259 = _M0IPC16uint166UInt16PB7Default7default();
  _M0L6bufferS383
  = (uint16_t*)moonbit_make_string(_M0L3lenS380, _M0L6_2atmpS1259);
  _M0L1iS384 = 0;
  while (1) {
    if (_M0L1iS384 < _M0L3lenS380) {
      int32_t _M0L6_2atmpS1257 = _M0L4fromS382 + _M0L1iS384;
      int32_t _M0L6_2atmpS1256;
      int32_t _M0L6_2atmpS1255;
      int32_t _M0L6_2atmpS1258;
      if (
        _M0L6_2atmpS1257 < 0
        || _M0L6_2atmpS1257 >= Moonbit_array_length(_M0L5bytesS385)
      ) {
        #line 56 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6_2atmpS1256 = (int32_t)_M0L5bytesS385[_M0L6_2atmpS1257];
      _M0L6_2atmpS1255 = (uint16_t)_M0L6_2atmpS1256;
      if (
        _M0L1iS384 < 0 || _M0L1iS384 >= Moonbit_array_length(_M0L6bufferS383)
      ) {
        #line 56 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6bufferS383[_M0L1iS384] = _M0L6_2atmpS1255;
      _M0L6_2atmpS1258 = _M0L1iS384 + 1;
      _M0L1iS384 = _M0L6_2atmpS1258;
      continue;
    }
    break;
  }
  return _M0L6bufferS383;
}

int32_t _M0FPB9log10Pow2(int32_t _M0L1eS379) {
  int32_t _M0L6_2atmpS1254;
  uint32_t _M0L6_2atmpS1253;
  uint32_t _M0L6_2atmpS1252;
  #line 44 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1254 = _M0L1eS379 * 78913;
  _M0L6_2atmpS1253 = *(uint32_t*)&_M0L6_2atmpS1254;
  _M0L6_2atmpS1252 = _M0L6_2atmpS1253 >> 18;
  return *(int32_t*)&_M0L6_2atmpS1252;
}

int32_t _M0FPB9log10Pow5(int32_t _M0L1eS378) {
  int32_t _M0L6_2atmpS1251;
  uint32_t _M0L6_2atmpS1250;
  uint32_t _M0L6_2atmpS1249;
  #line 37 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1251 = _M0L1eS378 * 732923;
  _M0L6_2atmpS1250 = *(uint32_t*)&_M0L6_2atmpS1251;
  _M0L6_2atmpS1249 = _M0L6_2atmpS1250 >> 20;
  return *(int32_t*)&_M0L6_2atmpS1249;
}

moonbit_string_t _M0FPB18copy__special__str(
  int32_t _M0L4signS376,
  int32_t _M0L8exponentS377,
  int32_t _M0L8mantissaS374
) {
  moonbit_string_t _M0L1sS375;
  moonbit_string_t _result_2147;
  #line 23 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  if (_M0L8mantissaS374) {
    return (moonbit_string_t)moonbit_string_literal_14.data;
  }
  if (_M0L4signS376) {
    _M0L1sS375 = (moonbit_string_t)moonbit_string_literal_15.data;
  } else {
    _M0L1sS375 = (moonbit_string_t)moonbit_string_literal_0.data;
  }
  if (_M0L8exponentS377) {
    moonbit_string_t _result_2146;
    #line 29 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _result_2146
    = moonbit_add_string(_M0L1sS375, (moonbit_string_t)moonbit_string_literal_16.data);
    moonbit_decref(_M0L1sS375);
    return _result_2146;
  }
  #line 31 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _result_2147
  = moonbit_add_string(_M0L1sS375, (moonbit_string_t)moonbit_string_literal_17.data);
  moonbit_decref(_M0L1sS375);
  return _result_2147;
}

int32_t _M0FPB8pow5bits(int32_t _M0L1eS373) {
  int32_t _M0L6_2atmpS1248;
  uint32_t _M0L6_2atmpS1247;
  uint32_t _M0L6_2atmpS1246;
  int32_t _M0L6_2atmpS1245;
  #line 18 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1248 = _M0L1eS373 * 1217359;
  _M0L6_2atmpS1247 = *(uint32_t*)&_M0L6_2atmpS1248;
  _M0L6_2atmpS1246 = _M0L6_2atmpS1247 >> 19;
  _M0L6_2atmpS1245 = *(int32_t*)&_M0L6_2atmpS1246;
  return _M0L6_2atmpS1245 + 1;
}

int32_t _M0MPC16double6Double7to__int(double _M0L4selfS372) {
  #line 43 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_to_int.mbt"
  if (_M0L4selfS372 != _M0L4selfS372) {
    return 0;
  } else if (_M0L4selfS372 >= 0x1.fffffffcp+30) {
    return 2147483647;
  } else if (_M0L4selfS372 <= -0x1p+31) {
    return (int32_t)0x80000000;
  } else {
    return (int32_t)_M0L4selfS372;
  }
}

int64_t _M0MPC16double6Double9to__int64(double _M0L4selfS371) {
  #line 46 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_to_int64_native.mbt"
  if (_M0L4selfS371 != _M0L4selfS371) {
    return 0ll;
  } else if (_M0L4selfS371 >= 0x1p+63) {
    return 9223372036854775807ll;
  } else if (_M0L4selfS371 <= -0x1p+63) {
    return (int64_t)0x8000000000000000ll;
  } else {
    return (int64_t)_M0L4selfS371;
  }
}

struct _M0TPB5ArrayGfE* _M0MPC15array5Array20unsafe__make__uninitGfE(
  int32_t _M0L3lenS368
) {
  float* _M0L6_2atmpS1242;
  struct _M0TPB5ArrayGfE* _block_2148;
  #line 30 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1242 = (float*)moonbit_make_float_array_raw(_M0L3lenS368);
  _block_2148
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_block_2148)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 0, 0);
  _block_2148->$0 = _M0L6_2atmpS1242;
  _block_2148->$1 = _M0L3lenS368;
  return _block_2148;
}

struct _M0TPB5ArrayGbE* _M0MPC15array5Array20unsafe__make__uninitGbE(
  int32_t _M0L3lenS369
) {
  uint8_t* _M0L6_2atmpS1243;
  struct _M0TPB5ArrayGbE* _block_2149;
  #line 30 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1243 = (uint8_t*)moonbit_make_bytes_raw(_M0L3lenS369);
  _block_2149
  = (struct _M0TPB5ArrayGbE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGbE));
  Moonbit_object_header(_block_2149)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 51, 0);
  _block_2149->$0 = _M0L6_2atmpS1243;
  _block_2149->$1 = _M0L3lenS369;
  return _block_2149;
}

struct _M0TPB5ArrayGiE* _M0MPC15array5Array20unsafe__make__uninitGiE(
  int32_t _M0L3lenS370
) {
  int32_t* _M0L6_2atmpS1244;
  struct _M0TPB5ArrayGiE* _block_2150;
  #line 30 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1244 = (int32_t*)moonbit_make_int32_array_raw(_M0L3lenS370);
  _block_2150
  = (struct _M0TPB5ArrayGiE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGiE));
  Moonbit_object_header(_block_2150)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 3, 0);
  _block_2150->$0 = _M0L6_2atmpS1244;
  _block_2150->$1 = _M0L3lenS370;
  return _block_2150;
}

uint64_t _M0MPC15array13ReadOnlyArray2atGmE(
  uint64_t* _M0L4selfS364,
  int32_t _M0L5indexS365
) {
  uint64_t* _M0L6_2atmpS1240;
  #line 38 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\readonlyarray.mbt"
  _M0L6_2atmpS1240 = _M0L4selfS364;
  if (
    _M0L5indexS365 < 0
    || _M0L5indexS365 >= Moonbit_array_length(_M0L6_2atmpS1240)
  ) {
    #line 40 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\readonlyarray.mbt"
    moonbit_panic();
  }
  return (uint64_t)_M0L6_2atmpS1240[_M0L5indexS365];
}

uint32_t _M0MPC15array13ReadOnlyArray2atGjE(
  uint32_t* _M0L4selfS366,
  int32_t _M0L5indexS367
) {
  uint32_t* _M0L6_2atmpS1241;
  #line 38 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\readonlyarray.mbt"
  _M0L6_2atmpS1241 = _M0L4selfS366;
  if (
    _M0L5indexS367 < 0
    || _M0L5indexS367 >= Moonbit_array_length(_M0L6_2atmpS1241)
  ) {
    #line 40 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\readonlyarray.mbt"
    moonbit_panic();
  }
  return (uint32_t)_M0L6_2atmpS1241[_M0L5indexS367];
}

moonbit_string_t _M0IPC16uint646UInt64PB4Show10to__string(
  uint64_t _M0L4selfS363
) {
  #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  #line 51 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  return _M0MPC16uint646UInt6418to__string_2einner(_M0L4selfS363, 10);
}

moonbit_string_t _M0IPC13int3IntPB4Show10to__string(int32_t _M0L4selfS362) {
  #line 35 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  #line 36 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  return _M0MPC13int3Int18to__string_2einner(_M0L4selfS362, 10);
}

uint64_t _M0MPC14uint4UInt10to__uint64(uint32_t _M0L4selfS361) {
  #line 2576 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  return (uint64_t)_M0L4selfS361;
}

int32_t _M0MPC15array5Array4pushGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS355,
  float _M0L5valueS357
) {
  int32_t _M0L3lenS1226;
  float* _M0L6_2atmpS1228;
  int32_t _M0L6_2atmpS1227;
  int32_t _M0L6lengthS356;
  float* _M0L3bufS1231;
  int32_t _M0L6_2atmpS1232;
  #line 406 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L3lenS1226 = _M0L4selfS355->$1;
  #line 408 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1228 = _M0MPC15array5Array6bufferGfE(_M0L4selfS355);
  _M0L6_2atmpS1227 = Moonbit_array_length(_M0L6_2atmpS1228);
  moonbit_decref(_M0L6_2atmpS1228);
  if (_M0L3lenS1226 == _M0L6_2atmpS1227) {
    int32_t _M0L3lenS1230 = _M0L4selfS355->$1;
    int32_t _M0L6_2atmpS1229 = _M0L3lenS1230 + 1;
    #line 409 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0MPC15array5Array7reallocGfE(_M0L4selfS355, _M0L6_2atmpS1229);
  }
  _M0L6lengthS356 = _M0L4selfS355->$1;
  _M0L3bufS1231 = _M0L4selfS355->$0;
  _M0L3bufS1231[_M0L6lengthS356] = _M0L5valueS357;
  _M0L6_2atmpS1232 = _M0L6lengthS356 + 1;
  _M0L4selfS355->$1 = _M0L6_2atmpS1232;
  return 0;
}

int32_t _M0MPC15array5Array4pushGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS358,
  int32_t _M0L5valueS360
) {
  int32_t _M0L3lenS1233;
  int32_t* _M0L6_2atmpS1235;
  int32_t _M0L6_2atmpS1234;
  int32_t _M0L6lengthS359;
  int32_t* _M0L3bufS1238;
  int32_t _M0L6_2atmpS1239;
  #line 406 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L3lenS1233 = _M0L4selfS358->$1;
  #line 408 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1235 = _M0MPC15array5Array6bufferGiE(_M0L4selfS358);
  _M0L6_2atmpS1234 = Moonbit_array_length(_M0L6_2atmpS1235);
  moonbit_decref(_M0L6_2atmpS1235);
  if (_M0L3lenS1233 == _M0L6_2atmpS1234) {
    int32_t _M0L3lenS1237 = _M0L4selfS358->$1;
    int32_t _M0L6_2atmpS1236 = _M0L3lenS1237 + 1;
    #line 409 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0MPC15array5Array7reallocGiE(_M0L4selfS358, _M0L6_2atmpS1236);
  }
  _M0L6lengthS359 = _M0L4selfS358->$1;
  _M0L3bufS1238 = _M0L4selfS358->$0;
  _M0L3bufS1238[_M0L6lengthS359] = _M0L5valueS360;
  _M0L6_2atmpS1239 = _M0L6lengthS359 + 1;
  _M0L4selfS358->$1 = _M0L6_2atmpS1239;
  return 0;
}

int32_t _M0MPC15array5Array7reallocGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS348,
  int32_t _M0L8requiredS350
) {
  int32_t _M0L8old__capS347;
  int32_t _M0L3lenS1224;
  int32_t _M0L8new__capS349;
  #line 302 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  #line 304 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__capS347 = _M0MPC15array5Array8capacityGiE(_M0L4selfS348);
  _M0L3lenS1224 = _M0L4selfS348->$1;
  #line 305 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__capS349
  = _M0FPB23array__growth__capacity(_M0L8old__capS347, _M0L3lenS1224, _M0L8requiredS350);
  #line 306 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0MPC15array5Array14resize__bufferGiE(_M0L4selfS348, _M0L8new__capS349);
  return 0;
}

int32_t _M0MPC15array5Array7reallocGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS352,
  int32_t _M0L8requiredS354
) {
  int32_t _M0L8old__capS351;
  int32_t _M0L3lenS1225;
  int32_t _M0L8new__capS353;
  #line 302 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  #line 304 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__capS351 = _M0MPC15array5Array8capacityGfE(_M0L4selfS352);
  _M0L3lenS1225 = _M0L4selfS352->$1;
  #line 305 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__capS353
  = _M0FPB23array__growth__capacity(_M0L8old__capS351, _M0L3lenS1225, _M0L8requiredS354);
  #line 306 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0MPC15array5Array14resize__bufferGfE(_M0L4selfS352, _M0L8new__capS353);
  return 0;
}

int32_t _M0MPC15array5Array14resize__bufferGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS336,
  int32_t _M0L13new__capacityS339
) {
  int32_t* _M0L8old__bufS335;
  int32_t _M0L3lenS337;
  int32_t _M0L9copy__lenS338;
  int32_t* _M0L8new__bufS340;
  int32_t* _M0L6_2aoldS2051;
  #line 242 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__bufS335 = _M0L4selfS336->$0;
  _M0L3lenS337 = _M0L4selfS336->$1;
  if (_M0L3lenS337 < _M0L13new__capacityS339) {
    _M0L9copy__lenS338 = _M0L3lenS337;
  } else {
    _M0L9copy__lenS338 = _M0L13new__capacityS339;
  }
  moonbit_incref(_M0L8old__bufS335);
  #line 249 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__bufS340
  = _M0MPB18UninitializedArray23make__and__blit_2einnerGiE(_M0L8old__bufS335, _M0L13new__capacityS339, _M0L9copy__lenS338, 0, 0);
  _M0L6_2aoldS2051 = _M0L4selfS336->$0;
  moonbit_decref(_M0L6_2aoldS2051);
  _M0L4selfS336->$0 = _M0L8new__bufS340;
  return 0;
}

int32_t _M0MPC15array5Array14resize__bufferGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS342,
  int32_t _M0L13new__capacityS345
) {
  float* _M0L8old__bufS341;
  int32_t _M0L3lenS343;
  int32_t _M0L9copy__lenS344;
  float* _M0L8new__bufS346;
  float* _M0L6_2aoldS2052;
  #line 242 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__bufS341 = _M0L4selfS342->$0;
  _M0L3lenS343 = _M0L4selfS342->$1;
  if (_M0L3lenS343 < _M0L13new__capacityS345) {
    _M0L9copy__lenS344 = _M0L3lenS343;
  } else {
    _M0L9copy__lenS344 = _M0L13new__capacityS345;
  }
  moonbit_incref(_M0L8old__bufS341);
  #line 249 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__bufS346
  = _M0MPB18UninitializedArray23make__and__blit_2einnerGfE(_M0L8old__bufS341, _M0L13new__capacityS345, _M0L9copy__lenS344, 0, 0);
  _M0L6_2aoldS2052 = _M0L4selfS342->$0;
  moonbit_decref(_M0L6_2aoldS2052);
  _M0L4selfS342->$0 = _M0L8new__bufS346;
  return 0;
}

int32_t _M0MPC15array5Array8capacityGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS333
) {
  int32_t* _M0L6_2atmpS1222;
  int32_t _result_2151;
  #line 132 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L6_2atmpS1222 = _M0MPC15array5Array6bufferGiE(_M0L4selfS333);
  _result_2151 = Moonbit_array_length(_M0L6_2atmpS1222);
  moonbit_decref(_M0L6_2atmpS1222);
  return _result_2151;
}

int32_t _M0MPC15array5Array8capacityGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS334
) {
  float* _M0L6_2atmpS1223;
  int32_t _result_2152;
  #line 132 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L6_2atmpS1223 = _M0MPC15array5Array6bufferGfE(_M0L4selfS334);
  _result_2152 = Moonbit_array_length(_M0L6_2atmpS1223);
  moonbit_decref(_M0L6_2atmpS1223);
  return _result_2152;
}

int32_t _M0FPB23array__growth__capacity(
  int32_t _M0L7currentS329,
  int32_t _M0L3lenS327,
  int32_t _M0L8requiredS326
) {
  int32_t _M0L5startS328;
  int32_t _M0L5spaceS330;
  #line 200 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  if (_M0L8requiredS326 < _M0L3lenS327) {
    #line 203 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_18.data);
  }
  if (_M0L7currentS329 == 0) {
    _M0L5startS328 = 8;
  } else {
    _M0L5startS328 = _M0L7currentS329;
  }
  _M0L5spaceS330 = _M0L5startS328;
  while (1) {
    if (_M0L5spaceS330 < _M0L8requiredS326) {
      int32_t _M0L4nextS331 = _M0L5spaceS330 * 2;
      if (_M0L4nextS331 <= _M0L5spaceS330) {
        return _M0L8requiredS326;
      }
      _M0L5spaceS330 = _M0L4nextS331;
      continue;
    } else {
      return _M0L5spaceS330;
    }
    break;
  }
}

int32_t _M0MPC15array5Array6lengthGfE(struct _M0TPB5ArrayGfE* _M0L4selfS324) {
  #line 147 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  return _M0L4selfS324->$1;
}

int32_t _M0MPC15array5Array6lengthGbE(struct _M0TPB5ArrayGbE* _M0L4selfS325) {
  #line 147 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  return _M0L4selfS325->$1;
}

float* _M0MPC15array5Array6bufferGfE(struct _M0TPB5ArrayGfE* _M0L4selfS320) {
  float* _M0L8_2afieldS2053;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS2053 = _M0L4selfS320->$0;
  moonbit_incref(_M0L8_2afieldS2053);
  return _M0L8_2afieldS2053;
}

struct _M0TP26RiantR8snn__mbt7Monitor** _M0MPC15array5Array6bufferGRP26RiantR8snn__mbt7MonitorE(
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt7MonitorE* _M0L4selfS321
) {
  struct _M0TP26RiantR8snn__mbt7Monitor** _M0L8_2afieldS2054;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS2054 = _M0L4selfS321->$0;
  moonbit_incref(_M0L8_2afieldS2054);
  return _M0L8_2afieldS2054;
}

int32_t* _M0MPC15array5Array6bufferGiE(struct _M0TPB5ArrayGiE* _M0L4selfS322) {
  int32_t* _M0L8_2afieldS2055;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS2055 = _M0L4selfS322->$0;
  moonbit_incref(_M0L8_2afieldS2055);
  return _M0L8_2afieldS2055;
}

uint8_t* _M0MPC15array5Array6bufferGbE(struct _M0TPB5ArrayGbE* _M0L4selfS323) {
  uint8_t* _M0L8_2afieldS2056;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS2056 = _M0L4selfS323->$0;
  moonbit_incref(_M0L8_2afieldS2056);
  return _M0L8_2afieldS2056;
}

moonbit_string_t _M0IPC16string6StringPB4Show10to__string(
  moonbit_string_t _M0L4selfS319
) {
  #line 222 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  moonbit_incref(_M0L4selfS319);
  return _M0L4selfS319;
}

int32_t _M0IPB13StringBuilderPB6Logger11write__view(
  struct _M0TPB13StringBuilder* _M0L4selfS318,
  struct _M0TPC16string10StringView _M0L3strS316
) {
  int32_t _M0L3endS1220;
  int32_t _M0L5startS1221;
  int32_t _M0L8str__lenS315;
  int32_t _M0L3lenS1219;
  int32_t _M0L8requiredS317;
  uint16_t* _M0L4dataS1212;
  int32_t _M0L6_2atmpS1211;
  int32_t _if__result_2154;
  uint16_t* _M0L4dataS1213;
  int32_t _M0L3lenS1214;
  moonbit_string_t _M0L6_2atmpS1215;
  int32_t _M0L6_2atmpS1216;
  int32_t _M0L3lenS1218;
  int32_t _M0L6_2atmpS1217;
  #line 158 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L3endS1220 = _M0L3strS316.$2;
  _M0L5startS1221 = _M0L3strS316.$1;
  _M0L8str__lenS315 = _M0L3endS1220 - _M0L5startS1221;
  if (_M0L8str__lenS315 == 0) {
    return 0;
  }
  _M0L3lenS1219 = _M0L4selfS318->$1;
  _M0L8requiredS317 = _M0L3lenS1219 + _M0L8str__lenS315;
  _M0L4dataS1212 = _M0L4selfS318->$0;
  _M0L6_2atmpS1211 = Moonbit_array_length(_M0L4dataS1212);
  if (_M0L8requiredS317 > _M0L6_2atmpS1211) {
    _if__result_2154 = 1;
  } else {
    int32_t _M0L3lenS1210 = _M0L4selfS318->$1;
    _if__result_2154 = _M0L8requiredS317 < _M0L3lenS1210;
  }
  if (_if__result_2154) {
    #line 168 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0MPB13StringBuilder4grow(_M0L4selfS318, _M0L8requiredS317);
  }
  _M0L4dataS1213 = _M0L4selfS318->$0;
  _M0L3lenS1214 = _M0L4selfS318->$1;
  moonbit_incref(_M0L4dataS1213);
  #line 172 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L6_2atmpS1215 = _M0MPC16string10StringView4data(_M0L3strS316);
  #line 173 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L6_2atmpS1216 = _M0MPC16string10StringView13start__offset(_M0L3strS316);
  #line 170 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0MPC15array10FixedArray26unsafe__blit__from__string(_M0L4dataS1213, _M0L3lenS1214, _M0L6_2atmpS1215, _M0L6_2atmpS1216, _M0L8str__lenS315);
  moonbit_decref(_M0L4dataS1213);
  moonbit_decref(_M0L6_2atmpS1215);
  _M0L3lenS1218 = _M0L4selfS318->$1;
  _M0L6_2atmpS1217 = _M0L3lenS1218 + _M0L8str__lenS315;
  _M0L4selfS318->$1 = _M0L6_2atmpS1217;
  return 0;
}

moonbit_string_t _M0MPC16uint646UInt6418to__string_2einner(
  uint64_t _M0L4selfS307,
  int32_t _M0L5radixS306
) {
  uint16_t* _M0L6bufferS308;
  #line 607 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  if (_M0L5radixS306 < 2 || _M0L5radixS306 > 36) {
    #line 611 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_19.data);
  }
  if (_M0L4selfS307 == 0ull) {
    return (moonbit_string_t)moonbit_string_literal_12.data;
  }
  switch (_M0L5radixS306) {
    case 10: {
      int32_t _M0L3lenS309;
      uint16_t* _M0L6bufferS310;
      #line 622 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L3lenS309 = _M0FPB12dec__count64(_M0L4selfS307);
      _M0L6bufferS310 = (uint16_t*)moonbit_make_string(_M0L3lenS309, 0);
      #line 624 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0FPB22int64__to__string__dec(_M0L6bufferS310, _M0L4selfS307, 0, _M0L3lenS309);
      _M0L6bufferS308 = _M0L6bufferS310;
      break;
    }
    
    case 16: {
      int32_t _M0L3lenS311;
      uint16_t* _M0L6bufferS312;
      #line 628 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L3lenS311 = _M0FPB12hex__count64(_M0L4selfS307);
      _M0L6bufferS312 = (uint16_t*)moonbit_make_string(_M0L3lenS311, 0);
      #line 630 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0FPB22int64__to__string__hex(_M0L6bufferS312, _M0L4selfS307, 0, _M0L3lenS311);
      _M0L6bufferS308 = _M0L6bufferS312;
      break;
    }
    default: {
      int32_t _M0L3lenS313;
      uint16_t* _M0L6bufferS314;
      #line 634 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L3lenS313 = _M0FPB14radix__count64(_M0L4selfS307, _M0L5radixS306);
      _M0L6bufferS314 = (uint16_t*)moonbit_make_string(_M0L3lenS313, 0);
      #line 636 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0FPB26int64__to__string__generic(_M0L6bufferS314, _M0L4selfS307, 0, _M0L3lenS313, _M0L5radixS306);
      _M0L6bufferS308 = _M0L6bufferS314;
      break;
    }
  }
  return _M0L6bufferS308;
}

moonbit_string_t _M0MPC15int645Int6418to__string_2einner(
  int64_t _M0L4selfS290,
  int32_t _M0L5radixS289
) {
  int32_t _M0L12is__negativeS291;
  uint64_t _M0L3numS292;
  uint16_t* _M0L6bufferS293;
  #line 548 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  if (_M0L5radixS289 < 2 || _M0L5radixS289 > 36) {
    #line 552 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_19.data);
  }
  if (_M0L4selfS290 == 0ll) {
    return (moonbit_string_t)moonbit_string_literal_12.data;
  }
  _M0L12is__negativeS291 = _M0L4selfS290 < 0ll;
  if (_M0L12is__negativeS291) {
    int64_t _M0L6_2atmpS1209 = -_M0L4selfS290;
    _M0L3numS292 = *(uint64_t*)&_M0L6_2atmpS1209;
  } else {
    _M0L3numS292 = *(uint64_t*)&_M0L4selfS290;
  }
  switch (_M0L5radixS289) {
    case 10: {
      int32_t _M0L10digit__lenS294;
      int32_t _M0L6_2atmpS1206;
      int32_t _M0L10total__lenS295;
      uint16_t* _M0L6bufferS296;
      int32_t _M0L12digit__startS297;
      #line 573 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS294 = _M0FPB12dec__count64(_M0L3numS292);
      if (_M0L12is__negativeS291) {
        _M0L6_2atmpS1206 = 1;
      } else {
        _M0L6_2atmpS1206 = 0;
      }
      _M0L10total__lenS295 = _M0L10digit__lenS294 + _M0L6_2atmpS1206;
      _M0L6bufferS296
      = (uint16_t*)moonbit_make_string(_M0L10total__lenS295, 0);
      if (_M0L12is__negativeS291) {
        _M0L12digit__startS297 = 1;
      } else {
        _M0L12digit__startS297 = 0;
      }
      #line 577 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0FPB22int64__to__string__dec(_M0L6bufferS296, _M0L3numS292, _M0L12digit__startS297, _M0L10total__lenS295);
      _M0L6bufferS293 = _M0L6bufferS296;
      break;
    }
    
    case 16: {
      int32_t _M0L10digit__lenS298;
      int32_t _M0L6_2atmpS1207;
      int32_t _M0L10total__lenS299;
      uint16_t* _M0L6bufferS300;
      int32_t _M0L12digit__startS301;
      #line 581 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS298 = _M0FPB12hex__count64(_M0L3numS292);
      if (_M0L12is__negativeS291) {
        _M0L6_2atmpS1207 = 1;
      } else {
        _M0L6_2atmpS1207 = 0;
      }
      _M0L10total__lenS299 = _M0L10digit__lenS298 + _M0L6_2atmpS1207;
      _M0L6bufferS300
      = (uint16_t*)moonbit_make_string(_M0L10total__lenS299, 0);
      if (_M0L12is__negativeS291) {
        _M0L12digit__startS301 = 1;
      } else {
        _M0L12digit__startS301 = 0;
      }
      #line 585 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0FPB22int64__to__string__hex(_M0L6bufferS300, _M0L3numS292, _M0L12digit__startS301, _M0L10total__lenS299);
      _M0L6bufferS293 = _M0L6bufferS300;
      break;
    }
    default: {
      int32_t _M0L10digit__lenS302;
      int32_t _M0L6_2atmpS1208;
      int32_t _M0L10total__lenS303;
      uint16_t* _M0L6bufferS304;
      int32_t _M0L12digit__startS305;
      #line 589 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS302
      = _M0FPB14radix__count64(_M0L3numS292, _M0L5radixS289);
      if (_M0L12is__negativeS291) {
        _M0L6_2atmpS1208 = 1;
      } else {
        _M0L6_2atmpS1208 = 0;
      }
      _M0L10total__lenS303 = _M0L10digit__lenS302 + _M0L6_2atmpS1208;
      _M0L6bufferS304
      = (uint16_t*)moonbit_make_string(_M0L10total__lenS303, 0);
      if (_M0L12is__negativeS291) {
        _M0L12digit__startS305 = 1;
      } else {
        _M0L12digit__startS305 = 0;
      }
      #line 593 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0FPB26int64__to__string__generic(_M0L6bufferS304, _M0L3numS292, _M0L12digit__startS305, _M0L10total__lenS303, _M0L5radixS289);
      _M0L6bufferS293 = _M0L6bufferS304;
      break;
    }
  }
  if (_M0L12is__negativeS291) {
    _M0L6bufferS293[0] = 45;
  }
  return _M0L6bufferS293;
}

int32_t _M0FPB22int64__to__string__dec(
  uint16_t* _M0L6bufferS275,
  uint64_t _M0L3numS287,
  int32_t _M0L12digit__startS276,
  int32_t _M0L10total__lenS288
) {
  int32_t _M0L6_2atmpS1205;
  uint64_t _M0L3numS265;
  int32_t _M0L6offsetS266;
  #line 493 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L6_2atmpS1205 = _M0L10total__lenS288 - _M0L12digit__startS276;
  _M0L3numS265 = _M0L3numS287;
  _M0L6offsetS266 = _M0L6_2atmpS1205;
  while (1) {
    if (_M0L3numS265 >= 10000ull) {
      uint64_t _M0L1tS267 = _M0L3numS265 / 10000ull;
      uint64_t _M0L6_2atmpS1182 = _M0L3numS265 % 10000ull;
      int32_t _M0L1rS268 = (int32_t)_M0L6_2atmpS1182;
      int32_t _M0L2d1S269 = _M0L1rS268 / 100;
      int32_t _M0L2d2S270 = _M0L1rS268 % 100;
      int32_t _M0L6_2atmpS1181 = _M0L2d1S269 / 10;
      int32_t _M0L6_2atmpS1180 = 48 + _M0L6_2atmpS1181;
      int32_t _M0L6d1__hiS271 = (uint16_t)_M0L6_2atmpS1180;
      int32_t _M0L6_2atmpS1179 = _M0L2d1S269 % 10;
      int32_t _M0L6_2atmpS1178 = 48 + _M0L6_2atmpS1179;
      int32_t _M0L6d1__loS272 = (uint16_t)_M0L6_2atmpS1178;
      int32_t _M0L6_2atmpS1177 = _M0L2d2S270 / 10;
      int32_t _M0L6_2atmpS1176 = 48 + _M0L6_2atmpS1177;
      int32_t _M0L6d2__hiS273 = (uint16_t)_M0L6_2atmpS1176;
      int32_t _M0L6_2atmpS1175 = _M0L2d2S270 % 10;
      int32_t _M0L6_2atmpS1174 = 48 + _M0L6_2atmpS1175;
      int32_t _M0L6d2__loS274 = (uint16_t)_M0L6_2atmpS1174;
      int32_t _M0L6_2atmpS1166 = _M0L12digit__startS276 + _M0L6offsetS266;
      int32_t _M0L6_2atmpS1165 = _M0L6_2atmpS1166 - 4;
      int32_t _M0L6_2atmpS1168;
      int32_t _M0L6_2atmpS1167;
      int32_t _M0L6_2atmpS1170;
      int32_t _M0L6_2atmpS1169;
      int32_t _M0L6_2atmpS1172;
      int32_t _M0L6_2atmpS1171;
      int32_t _M0L6_2atmpS1173;
      _M0L6bufferS275[_M0L6_2atmpS1165] = _M0L6d1__hiS271;
      _M0L6_2atmpS1168 = _M0L12digit__startS276 + _M0L6offsetS266;
      _M0L6_2atmpS1167 = _M0L6_2atmpS1168 - 3;
      _M0L6bufferS275[_M0L6_2atmpS1167] = _M0L6d1__loS272;
      _M0L6_2atmpS1170 = _M0L12digit__startS276 + _M0L6offsetS266;
      _M0L6_2atmpS1169 = _M0L6_2atmpS1170 - 2;
      _M0L6bufferS275[_M0L6_2atmpS1169] = _M0L6d2__hiS273;
      _M0L6_2atmpS1172 = _M0L12digit__startS276 + _M0L6offsetS266;
      _M0L6_2atmpS1171 = _M0L6_2atmpS1172 - 1;
      _M0L6bufferS275[_M0L6_2atmpS1171] = _M0L6d2__loS274;
      _M0L6_2atmpS1173 = _M0L6offsetS266 - 4;
      _M0L3numS265 = _M0L1tS267;
      _M0L6offsetS266 = _M0L6_2atmpS1173;
      continue;
    } else {
      int32_t _M0L6_2atmpS1204 = (int32_t)_M0L3numS265;
      int32_t _M0L9remainingS278 = _M0L6_2atmpS1204;
      int32_t _M0L6offsetS279 = _M0L6offsetS266;
      while (1) {
        if (_M0L9remainingS278 >= 100) {
          int32_t _M0L1tS280 = _M0L9remainingS278 / 100;
          int32_t _M0L1dS281 = _M0L9remainingS278 % 100;
          int32_t _M0L6_2atmpS1191 = _M0L1dS281 / 10;
          int32_t _M0L6_2atmpS1190 = 48 + _M0L6_2atmpS1191;
          int32_t _M0L5d__hiS282 = (uint16_t)_M0L6_2atmpS1190;
          int32_t _M0L6_2atmpS1189 = _M0L1dS281 % 10;
          int32_t _M0L6_2atmpS1188 = 48 + _M0L6_2atmpS1189;
          int32_t _M0L5d__loS283 = (uint16_t)_M0L6_2atmpS1188;
          int32_t _M0L6_2atmpS1184 = _M0L12digit__startS276 + _M0L6offsetS279;
          int32_t _M0L6_2atmpS1183 = _M0L6_2atmpS1184 - 2;
          int32_t _M0L6_2atmpS1186;
          int32_t _M0L6_2atmpS1185;
          int32_t _M0L6_2atmpS1187;
          _M0L6bufferS275[_M0L6_2atmpS1183] = _M0L5d__hiS282;
          _M0L6_2atmpS1186 = _M0L12digit__startS276 + _M0L6offsetS279;
          _M0L6_2atmpS1185 = _M0L6_2atmpS1186 - 1;
          _M0L6bufferS275[_M0L6_2atmpS1185] = _M0L5d__loS283;
          _M0L6_2atmpS1187 = _M0L6offsetS279 - 2;
          _M0L9remainingS278 = _M0L1tS280;
          _M0L6offsetS279 = _M0L6_2atmpS1187;
          continue;
        } else if (_M0L9remainingS278 >= 10) {
          int32_t _M0L6_2atmpS1199 = _M0L9remainingS278 / 10;
          int32_t _M0L6_2atmpS1198 = 48 + _M0L6_2atmpS1199;
          int32_t _M0L5d__hiS285 = (uint16_t)_M0L6_2atmpS1198;
          int32_t _M0L6_2atmpS1197 = _M0L9remainingS278 % 10;
          int32_t _M0L6_2atmpS1196 = 48 + _M0L6_2atmpS1197;
          int32_t _M0L5d__loS286 = (uint16_t)_M0L6_2atmpS1196;
          int32_t _M0L6_2atmpS1193 = _M0L12digit__startS276 + _M0L6offsetS279;
          int32_t _M0L6_2atmpS1192 = _M0L6_2atmpS1193 - 2;
          int32_t _M0L6_2atmpS1195;
          int32_t _M0L6_2atmpS1194;
          _M0L6bufferS275[_M0L6_2atmpS1192] = _M0L5d__hiS285;
          _M0L6_2atmpS1195 = _M0L12digit__startS276 + _M0L6offsetS279;
          _M0L6_2atmpS1194 = _M0L6_2atmpS1195 - 1;
          _M0L6bufferS275[_M0L6_2atmpS1194] = _M0L5d__loS286;
        } else {
          int32_t _M0L6_2atmpS1203 = _M0L12digit__startS276 + _M0L6offsetS279;
          int32_t _M0L6_2atmpS1200 = _M0L6_2atmpS1203 - 1;
          int32_t _M0L6_2atmpS1202 = 48 + _M0L9remainingS278;
          int32_t _M0L6_2atmpS1201 = (uint16_t)_M0L6_2atmpS1202;
          _M0L6bufferS275[_M0L6_2atmpS1200] = _M0L6_2atmpS1201;
        }
        break;
      }
    }
    break;
  }
  return 0;
}

int32_t _M0FPB26int64__to__string__generic(
  uint16_t* _M0L6bufferS255,
  uint64_t _M0L3numS259,
  int32_t _M0L12digit__startS256,
  int32_t _M0L10total__lenS258,
  int32_t _M0L5radixS249
) {
  uint64_t _M0L4baseS248;
  int32_t _M0L6_2atmpS1150;
  int32_t _M0L6_2atmpS1149;
  #line 462 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  #line 470 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L4baseS248 = _M0MPC13int3Int10to__uint64(_M0L5radixS249);
  _M0L6_2atmpS1150 = _M0L5radixS249 - 1;
  _M0L6_2atmpS1149 = _M0L5radixS249 & _M0L6_2atmpS1150;
  if (_M0L6_2atmpS1149 == 0) {
    int32_t _M0L5shiftS250;
    uint64_t _M0L4maskS251;
    int32_t _M0L6_2atmpS1157;
    int32_t _M0L6offsetS252;
    uint64_t _M0L1nS253;
    #line 473 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0L5shiftS250 = moonbit_ctz32(_M0L5radixS249);
    _M0L4maskS251 = _M0L4baseS248 - 1ull;
    _M0L6_2atmpS1157 = _M0L10total__lenS258 - _M0L12digit__startS256;
    _M0L6offsetS252 = _M0L6_2atmpS1157;
    _M0L1nS253 = _M0L3numS259;
    while (1) {
      if (_M0L1nS253 > 0ull) {
        uint64_t _M0L6_2atmpS1156 = _M0L1nS253 & _M0L4maskS251;
        int32_t _M0L5digitS254 = (int32_t)_M0L6_2atmpS1156;
        int32_t _M0L6_2atmpS1153 = _M0L12digit__startS256 + _M0L6offsetS252;
        int32_t _M0L6_2atmpS1151 = _M0L6_2atmpS1153 - 1;
        int32_t _M0L6_2atmpS1152 =
          ((moonbit_string_t)moonbit_string_literal_20.data)[_M0L5digitS254];
        int32_t _M0L6_2atmpS1154;
        uint64_t _M0L6_2atmpS1155;
        _M0L6bufferS255[_M0L6_2atmpS1151] = _M0L6_2atmpS1152;
        _M0L6_2atmpS1154 = _M0L6offsetS252 - 1;
        _M0L6_2atmpS1155 = _M0L1nS253 >> (_M0L5shiftS250 & 63);
        _M0L6offsetS252 = _M0L6_2atmpS1154;
        _M0L1nS253 = _M0L6_2atmpS1155;
        continue;
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1164 = _M0L10total__lenS258 - _M0L12digit__startS256;
    int32_t _M0L6offsetS260 = _M0L6_2atmpS1164;
    uint64_t _M0L1nS261 = _M0L3numS259;
    while (1) {
      if (_M0L1nS261 > 0ull) {
        uint64_t _M0L1qS262 = _M0L1nS261 / _M0L4baseS248;
        uint64_t _M0L6_2atmpS1163 = _M0L1qS262 * _M0L4baseS248;
        uint64_t _M0L6_2atmpS1162 = _M0L1nS261 - _M0L6_2atmpS1163;
        int32_t _M0L5digitS263 = (int32_t)_M0L6_2atmpS1162;
        int32_t _M0L6_2atmpS1160 = _M0L12digit__startS256 + _M0L6offsetS260;
        int32_t _M0L6_2atmpS1158 = _M0L6_2atmpS1160 - 1;
        int32_t _M0L6_2atmpS1159 =
          ((moonbit_string_t)moonbit_string_literal_20.data)[_M0L5digitS263];
        int32_t _M0L6_2atmpS1161;
        _M0L6bufferS255[_M0L6_2atmpS1158] = _M0L6_2atmpS1159;
        _M0L6_2atmpS1161 = _M0L6offsetS260 - 1;
        _M0L6offsetS260 = _M0L6_2atmpS1161;
        _M0L1nS261 = _M0L1qS262;
        continue;
      }
      break;
    }
  }
  return 0;
}

int32_t _M0FPB22int64__to__string__hex(
  uint16_t* _M0L6bufferS242,
  uint64_t _M0L3numS247,
  int32_t _M0L12digit__startS243,
  int32_t _M0L10total__lenS246
) {
  int32_t _M0L6_2atmpS1148;
  int32_t _M0L6offsetS237;
  uint64_t _M0L1nS238;
  #line 434 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L6_2atmpS1148 = _M0L10total__lenS246 - _M0L12digit__startS243;
  _M0L6offsetS237 = _M0L6_2atmpS1148;
  _M0L1nS238 = _M0L3numS247;
  while (1) {
    if (_M0L6offsetS237 >= 2) {
      uint64_t _M0L6_2atmpS1145 = _M0L1nS238 & 255ull;
      int32_t _M0L9byte__valS239 = (int32_t)_M0L6_2atmpS1145;
      int32_t _M0L2hiS240 = _M0L9byte__valS239 / 16;
      int32_t _M0L2loS241 = _M0L9byte__valS239 % 16;
      int32_t _M0L6_2atmpS1139 = _M0L12digit__startS243 + _M0L6offsetS237;
      int32_t _M0L6_2atmpS1137 = _M0L6_2atmpS1139 - 2;
      int32_t _M0L6_2atmpS1138 =
        ((moonbit_string_t)moonbit_string_literal_20.data)[_M0L2hiS240];
      int32_t _M0L6_2atmpS1142;
      int32_t _M0L6_2atmpS1140;
      int32_t _M0L6_2atmpS1141;
      int32_t _M0L6_2atmpS1143;
      uint64_t _M0L6_2atmpS1144;
      _M0L6bufferS242[_M0L6_2atmpS1137] = _M0L6_2atmpS1138;
      _M0L6_2atmpS1142 = _M0L12digit__startS243 + _M0L6offsetS237;
      _M0L6_2atmpS1140 = _M0L6_2atmpS1142 - 1;
      _M0L6_2atmpS1141
      = ((moonbit_string_t)moonbit_string_literal_20.data)[
        _M0L2loS241
      ];
      _M0L6bufferS242[_M0L6_2atmpS1140] = _M0L6_2atmpS1141;
      _M0L6_2atmpS1143 = _M0L6offsetS237 - 2;
      _M0L6_2atmpS1144 = _M0L1nS238 >> 8;
      _M0L6offsetS237 = _M0L6_2atmpS1143;
      _M0L1nS238 = _M0L6_2atmpS1144;
      continue;
    } else if (_M0L6offsetS237 == 1) {
      uint64_t _M0L6_2atmpS1147 = _M0L1nS238 & 15ull;
      int32_t _M0L6nibbleS245 = (int32_t)_M0L6_2atmpS1147;
      int32_t _M0L6_2atmpS1146 =
        ((moonbit_string_t)moonbit_string_literal_20.data)[_M0L6nibbleS245];
      _M0L6bufferS242[_M0L12digit__startS243] = _M0L6_2atmpS1146;
    }
    break;
  }
  return 0;
}

int32_t _M0FPB14radix__count64(
  uint64_t _M0L5valueS231,
  int32_t _M0L5radixS233
) {
  uint64_t _M0L4baseS232;
  uint64_t _M0L3numS234;
  int32_t _M0L5countS235;
  #line 419 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  if (_M0L5valueS231 == 0ull) {
    return 1;
  }
  #line 424 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L4baseS232 = _M0MPC13int3Int10to__uint64(_M0L5radixS233);
  _M0L3numS234 = _M0L5valueS231;
  _M0L5countS235 = 0;
  while (1) {
    if (_M0L3numS234 > 0ull) {
      uint64_t _M0L6_2atmpS1135 = _M0L3numS234 / _M0L4baseS232;
      int32_t _M0L6_2atmpS1136 = _M0L5countS235 + 1;
      _M0L3numS234 = _M0L6_2atmpS1135;
      _M0L5countS235 = _M0L6_2atmpS1136;
      continue;
    } else {
      return _M0L5countS235;
    }
    break;
  }
}

int32_t _M0FPB12hex__count64(uint64_t _M0L5valueS229) {
  #line 407 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  if (_M0L5valueS229 == 0ull) {
    return 1;
  } else {
    int32_t _M0L14leading__zerosS230;
    int32_t _M0L6_2atmpS1134;
    int32_t _M0L6_2atmpS1133;
    #line 412 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0L14leading__zerosS230 = moonbit_clz64(_M0L5valueS229);
    _M0L6_2atmpS1134 = 63 - _M0L14leading__zerosS230;
    _M0L6_2atmpS1133 = _M0L6_2atmpS1134 / 4;
    return _M0L6_2atmpS1133 + 1;
  }
}

int32_t _M0FPB12dec__count64(uint64_t _M0L5valueS228) {
  #line 343 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  if (_M0L5valueS228 >= 10000000000ull) {
    if (_M0L5valueS228 >= 100000000000000ull) {
      if (_M0L5valueS228 >= 10000000000000000ull) {
        if (_M0L5valueS228 >= 1000000000000000000ull) {
          if (_M0L5valueS228 >= 10000000000000000000ull) {
            return 20;
          } else {
            return 19;
          }
        } else if (_M0L5valueS228 >= 100000000000000000ull) {
          return 18;
        } else {
          return 17;
        }
      } else if (_M0L5valueS228 >= 1000000000000000ull) {
        return 16;
      } else {
        return 15;
      }
    } else if (_M0L5valueS228 >= 1000000000000ull) {
      if (_M0L5valueS228 >= 10000000000000ull) {
        return 14;
      } else {
        return 13;
      }
    } else if (_M0L5valueS228 >= 100000000000ull) {
      return 12;
    } else {
      return 11;
    }
  } else if (_M0L5valueS228 >= 100000ull) {
    if (_M0L5valueS228 >= 10000000ull) {
      if (_M0L5valueS228 >= 1000000000ull) {
        return 10;
      } else if (_M0L5valueS228 >= 100000000ull) {
        return 9;
      } else {
        return 8;
      }
    } else if (_M0L5valueS228 >= 1000000ull) {
      return 7;
    } else {
      return 6;
    }
  } else if (_M0L5valueS228 >= 1000ull) {
    if (_M0L5valueS228 >= 10000ull) {
      return 5;
    } else {
      return 4;
    }
  } else if (_M0L5valueS228 >= 100ull) {
    return 3;
  } else if (_M0L5valueS228 >= 10ull) {
    return 2;
  } else {
    return 1;
  }
}

moonbit_string_t _M0MPC13int3Int18to__string_2einner(
  int32_t _M0L4selfS212,
  int32_t _M0L5radixS211
) {
  int32_t _M0L12is__negativeS213;
  uint32_t _M0L3numS214;
  uint16_t* _M0L6bufferS215;
  #line 209 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  if (_M0L5radixS211 < 2 || _M0L5radixS211 > 36) {
    #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_19.data);
  }
  if (_M0L4selfS212 == 0) {
    return (moonbit_string_t)moonbit_string_literal_12.data;
  }
  _M0L12is__negativeS213 = _M0L4selfS212 < 0;
  if (_M0L12is__negativeS213) {
    int32_t _M0L6_2atmpS1132 = -_M0L4selfS212;
    _M0L3numS214 = *(uint32_t*)&_M0L6_2atmpS1132;
  } else {
    _M0L3numS214 = *(uint32_t*)&_M0L4selfS212;
  }
  switch (_M0L5radixS211) {
    case 10: {
      int32_t _M0L10digit__lenS216;
      int32_t _M0L6_2atmpS1129;
      int32_t _M0L10total__lenS217;
      uint16_t* _M0L6bufferS218;
      int32_t _M0L12digit__startS219;
      #line 235 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS216 = _M0FPB12dec__count32(_M0L3numS214);
      if (_M0L12is__negativeS213) {
        _M0L6_2atmpS1129 = 1;
      } else {
        _M0L6_2atmpS1129 = 0;
      }
      _M0L10total__lenS217 = _M0L10digit__lenS216 + _M0L6_2atmpS1129;
      _M0L6bufferS218
      = (uint16_t*)moonbit_make_string(_M0L10total__lenS217, 0);
      if (_M0L12is__negativeS213) {
        _M0L12digit__startS219 = 1;
      } else {
        _M0L12digit__startS219 = 0;
      }
      #line 239 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0FPB20int__to__string__dec(_M0L6bufferS218, _M0L3numS214, _M0L12digit__startS219, _M0L10total__lenS217);
      _M0L6bufferS215 = _M0L6bufferS218;
      break;
    }
    
    case 16: {
      int32_t _M0L10digit__lenS220;
      int32_t _M0L6_2atmpS1130;
      int32_t _M0L10total__lenS221;
      uint16_t* _M0L6bufferS222;
      int32_t _M0L12digit__startS223;
      #line 243 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS220 = _M0FPB12hex__count32(_M0L3numS214);
      if (_M0L12is__negativeS213) {
        _M0L6_2atmpS1130 = 1;
      } else {
        _M0L6_2atmpS1130 = 0;
      }
      _M0L10total__lenS221 = _M0L10digit__lenS220 + _M0L6_2atmpS1130;
      _M0L6bufferS222
      = (uint16_t*)moonbit_make_string(_M0L10total__lenS221, 0);
      if (_M0L12is__negativeS213) {
        _M0L12digit__startS223 = 1;
      } else {
        _M0L12digit__startS223 = 0;
      }
      #line 247 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0FPB20int__to__string__hex(_M0L6bufferS222, _M0L3numS214, _M0L12digit__startS223, _M0L10total__lenS221);
      _M0L6bufferS215 = _M0L6bufferS222;
      break;
    }
    default: {
      int32_t _M0L10digit__lenS224;
      int32_t _M0L6_2atmpS1131;
      int32_t _M0L10total__lenS225;
      uint16_t* _M0L6bufferS226;
      int32_t _M0L12digit__startS227;
      #line 251 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS224
      = _M0FPB14radix__count32(_M0L3numS214, _M0L5radixS211);
      if (_M0L12is__negativeS213) {
        _M0L6_2atmpS1131 = 1;
      } else {
        _M0L6_2atmpS1131 = 0;
      }
      _M0L10total__lenS225 = _M0L10digit__lenS224 + _M0L6_2atmpS1131;
      _M0L6bufferS226
      = (uint16_t*)moonbit_make_string(_M0L10total__lenS225, 0);
      if (_M0L12is__negativeS213) {
        _M0L12digit__startS227 = 1;
      } else {
        _M0L12digit__startS227 = 0;
      }
      #line 255 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0FPB24int__to__string__generic(_M0L6bufferS226, _M0L3numS214, _M0L12digit__startS227, _M0L10total__lenS225, _M0L5radixS211);
      _M0L6bufferS215 = _M0L6bufferS226;
      break;
    }
  }
  if (_M0L12is__negativeS213) {
    _M0L6bufferS215[0] = 45;
  }
  return _M0L6bufferS215;
}

int32_t _M0FPB14radix__count32(
  uint32_t _M0L5valueS205,
  int32_t _M0L5radixS207
) {
  uint32_t _M0L4baseS206;
  uint32_t _M0L3numS208;
  int32_t _M0L5countS209;
  #line 189 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  if (_M0L5valueS205 == 0u) {
    return 1;
  }
  _M0L4baseS206 = *(uint32_t*)&_M0L5radixS207;
  _M0L3numS208 = _M0L5valueS205;
  _M0L5countS209 = 0;
  while (1) {
    if (_M0L3numS208 > 0u) {
      uint32_t _M0L6_2atmpS1127 = _M0L3numS208 / _M0L4baseS206;
      int32_t _M0L6_2atmpS1128 = _M0L5countS209 + 1;
      _M0L3numS208 = _M0L6_2atmpS1127;
      _M0L5countS209 = _M0L6_2atmpS1128;
      continue;
    } else {
      return _M0L5countS209;
    }
    break;
  }
}

int32_t _M0FPB12hex__count32(uint32_t _M0L5valueS203) {
  #line 177 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  if (_M0L5valueS203 == 0u) {
    return 1;
  } else {
    int32_t _M0L14leading__zerosS204;
    int32_t _M0L6_2atmpS1126;
    int32_t _M0L6_2atmpS1125;
    #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0L14leading__zerosS204 = moonbit_clz32(_M0L5valueS203);
    _M0L6_2atmpS1126 = 31 - _M0L14leading__zerosS204;
    _M0L6_2atmpS1125 = _M0L6_2atmpS1126 / 4;
    return _M0L6_2atmpS1125 + 1;
  }
}

int32_t _M0FPB12dec__count32(uint32_t _M0L5valueS202) {
  #line 143 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  if (_M0L5valueS202 >= 100000u) {
    if (_M0L5valueS202 >= 10000000u) {
      if (_M0L5valueS202 >= 1000000000u) {
        return 10;
      } else if (_M0L5valueS202 >= 100000000u) {
        return 9;
      } else {
        return 8;
      }
    } else if (_M0L5valueS202 >= 1000000u) {
      return 7;
    } else {
      return 6;
    }
  } else if (_M0L5valueS202 >= 1000u) {
    if (_M0L5valueS202 >= 10000u) {
      return 5;
    } else {
      return 4;
    }
  } else if (_M0L5valueS202 >= 100u) {
    return 3;
  } else if (_M0L5valueS202 >= 10u) {
    return 2;
  } else {
    return 1;
  }
}

int32_t _M0FPB20int__to__string__dec(
  uint16_t* _M0L6bufferS188,
  uint32_t _M0L3numS200,
  int32_t _M0L12digit__startS189,
  int32_t _M0L10total__lenS201
) {
  int32_t _M0L6_2atmpS1124;
  uint32_t _M0L3numS178;
  int32_t _M0L6offsetS179;
  #line 88 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L6_2atmpS1124 = _M0L10total__lenS201 - _M0L12digit__startS189;
  _M0L3numS178 = _M0L3numS200;
  _M0L6offsetS179 = _M0L6_2atmpS1124;
  while (1) {
    if (_M0L3numS178 >= 10000u) {
      uint32_t _M0L1tS180 = _M0L3numS178 / 10000u;
      uint32_t _M0L6_2atmpS1101 = _M0L3numS178 % 10000u;
      int32_t _M0L1rS181 = *(int32_t*)&_M0L6_2atmpS1101;
      int32_t _M0L2d1S182 = _M0L1rS181 / 100;
      int32_t _M0L2d2S183 = _M0L1rS181 % 100;
      int32_t _M0L6_2atmpS1100 = _M0L2d1S182 / 10;
      int32_t _M0L6_2atmpS1099 = 48 + _M0L6_2atmpS1100;
      int32_t _M0L6d1__hiS184 = (uint16_t)_M0L6_2atmpS1099;
      int32_t _M0L6_2atmpS1098 = _M0L2d1S182 % 10;
      int32_t _M0L6_2atmpS1097 = 48 + _M0L6_2atmpS1098;
      int32_t _M0L6d1__loS185 = (uint16_t)_M0L6_2atmpS1097;
      int32_t _M0L6_2atmpS1096 = _M0L2d2S183 / 10;
      int32_t _M0L6_2atmpS1095 = 48 + _M0L6_2atmpS1096;
      int32_t _M0L6d2__hiS186 = (uint16_t)_M0L6_2atmpS1095;
      int32_t _M0L6_2atmpS1094 = _M0L2d2S183 % 10;
      int32_t _M0L6_2atmpS1093 = 48 + _M0L6_2atmpS1094;
      int32_t _M0L6d2__loS187 = (uint16_t)_M0L6_2atmpS1093;
      int32_t _M0L6_2atmpS1085 = _M0L12digit__startS189 + _M0L6offsetS179;
      int32_t _M0L6_2atmpS1084 = _M0L6_2atmpS1085 - 4;
      int32_t _M0L6_2atmpS1087;
      int32_t _M0L6_2atmpS1086;
      int32_t _M0L6_2atmpS1089;
      int32_t _M0L6_2atmpS1088;
      int32_t _M0L6_2atmpS1091;
      int32_t _M0L6_2atmpS1090;
      int32_t _M0L6_2atmpS1092;
      _M0L6bufferS188[_M0L6_2atmpS1084] = _M0L6d1__hiS184;
      _M0L6_2atmpS1087 = _M0L12digit__startS189 + _M0L6offsetS179;
      _M0L6_2atmpS1086 = _M0L6_2atmpS1087 - 3;
      _M0L6bufferS188[_M0L6_2atmpS1086] = _M0L6d1__loS185;
      _M0L6_2atmpS1089 = _M0L12digit__startS189 + _M0L6offsetS179;
      _M0L6_2atmpS1088 = _M0L6_2atmpS1089 - 2;
      _M0L6bufferS188[_M0L6_2atmpS1088] = _M0L6d2__hiS186;
      _M0L6_2atmpS1091 = _M0L12digit__startS189 + _M0L6offsetS179;
      _M0L6_2atmpS1090 = _M0L6_2atmpS1091 - 1;
      _M0L6bufferS188[_M0L6_2atmpS1090] = _M0L6d2__loS187;
      _M0L6_2atmpS1092 = _M0L6offsetS179 - 4;
      _M0L3numS178 = _M0L1tS180;
      _M0L6offsetS179 = _M0L6_2atmpS1092;
      continue;
    } else {
      int32_t _M0L6_2atmpS1123 = *(int32_t*)&_M0L3numS178;
      int32_t _M0L9remainingS191 = _M0L6_2atmpS1123;
      int32_t _M0L6offsetS192 = _M0L6offsetS179;
      while (1) {
        if (_M0L9remainingS191 >= 100) {
          int32_t _M0L1tS193 = _M0L9remainingS191 / 100;
          int32_t _M0L1dS194 = _M0L9remainingS191 % 100;
          int32_t _M0L6_2atmpS1110 = _M0L1dS194 / 10;
          int32_t _M0L6_2atmpS1109 = 48 + _M0L6_2atmpS1110;
          int32_t _M0L5d__hiS195 = (uint16_t)_M0L6_2atmpS1109;
          int32_t _M0L6_2atmpS1108 = _M0L1dS194 % 10;
          int32_t _M0L6_2atmpS1107 = 48 + _M0L6_2atmpS1108;
          int32_t _M0L5d__loS196 = (uint16_t)_M0L6_2atmpS1107;
          int32_t _M0L6_2atmpS1103 = _M0L12digit__startS189 + _M0L6offsetS192;
          int32_t _M0L6_2atmpS1102 = _M0L6_2atmpS1103 - 2;
          int32_t _M0L6_2atmpS1105;
          int32_t _M0L6_2atmpS1104;
          int32_t _M0L6_2atmpS1106;
          _M0L6bufferS188[_M0L6_2atmpS1102] = _M0L5d__hiS195;
          _M0L6_2atmpS1105 = _M0L12digit__startS189 + _M0L6offsetS192;
          _M0L6_2atmpS1104 = _M0L6_2atmpS1105 - 1;
          _M0L6bufferS188[_M0L6_2atmpS1104] = _M0L5d__loS196;
          _M0L6_2atmpS1106 = _M0L6offsetS192 - 2;
          _M0L9remainingS191 = _M0L1tS193;
          _M0L6offsetS192 = _M0L6_2atmpS1106;
          continue;
        } else if (_M0L9remainingS191 >= 10) {
          int32_t _M0L6_2atmpS1118 = _M0L9remainingS191 / 10;
          int32_t _M0L6_2atmpS1117 = 48 + _M0L6_2atmpS1118;
          int32_t _M0L5d__hiS198 = (uint16_t)_M0L6_2atmpS1117;
          int32_t _M0L6_2atmpS1116 = _M0L9remainingS191 % 10;
          int32_t _M0L6_2atmpS1115 = 48 + _M0L6_2atmpS1116;
          int32_t _M0L5d__loS199 = (uint16_t)_M0L6_2atmpS1115;
          int32_t _M0L6_2atmpS1112 = _M0L12digit__startS189 + _M0L6offsetS192;
          int32_t _M0L6_2atmpS1111 = _M0L6_2atmpS1112 - 2;
          int32_t _M0L6_2atmpS1114;
          int32_t _M0L6_2atmpS1113;
          _M0L6bufferS188[_M0L6_2atmpS1111] = _M0L5d__hiS198;
          _M0L6_2atmpS1114 = _M0L12digit__startS189 + _M0L6offsetS192;
          _M0L6_2atmpS1113 = _M0L6_2atmpS1114 - 1;
          _M0L6bufferS188[_M0L6_2atmpS1113] = _M0L5d__loS199;
        } else {
          int32_t _M0L6_2atmpS1122 = _M0L12digit__startS189 + _M0L6offsetS192;
          int32_t _M0L6_2atmpS1119 = _M0L6_2atmpS1122 - 1;
          int32_t _M0L6_2atmpS1121 = 48 + _M0L9remainingS191;
          int32_t _M0L6_2atmpS1120 = (uint16_t)_M0L6_2atmpS1121;
          _M0L6bufferS188[_M0L6_2atmpS1119] = _M0L6_2atmpS1120;
        }
        break;
      }
    }
    break;
  }
  return 0;
}

int32_t _M0FPB24int__to__string__generic(
  uint16_t* _M0L6bufferS168,
  uint32_t _M0L3numS172,
  int32_t _M0L12digit__startS169,
  int32_t _M0L10total__lenS171,
  int32_t _M0L5radixS162
) {
  uint32_t _M0L4baseS161;
  int32_t _M0L6_2atmpS1069;
  int32_t _M0L6_2atmpS1068;
  #line 57 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L4baseS161 = *(uint32_t*)&_M0L5radixS162;
  _M0L6_2atmpS1069 = _M0L5radixS162 - 1;
  _M0L6_2atmpS1068 = _M0L5radixS162 & _M0L6_2atmpS1069;
  if (_M0L6_2atmpS1068 == 0) {
    int32_t _M0L5shiftS163;
    uint32_t _M0L4maskS164;
    int32_t _M0L6_2atmpS1076;
    int32_t _M0L6offsetS165;
    uint32_t _M0L1nS166;
    #line 68 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0L5shiftS163 = moonbit_ctz32(_M0L5radixS162);
    _M0L4maskS164 = _M0L4baseS161 - 1u;
    _M0L6_2atmpS1076 = _M0L10total__lenS171 - _M0L12digit__startS169;
    _M0L6offsetS165 = _M0L6_2atmpS1076;
    _M0L1nS166 = _M0L3numS172;
    while (1) {
      if (_M0L1nS166 > 0u) {
        uint32_t _M0L6_2atmpS1075 = _M0L1nS166 & _M0L4maskS164;
        int32_t _M0L5digitS167 = *(int32_t*)&_M0L6_2atmpS1075;
        int32_t _M0L6_2atmpS1072 = _M0L12digit__startS169 + _M0L6offsetS165;
        int32_t _M0L6_2atmpS1070 = _M0L6_2atmpS1072 - 1;
        int32_t _M0L6_2atmpS1071 =
          ((moonbit_string_t)moonbit_string_literal_20.data)[_M0L5digitS167];
        int32_t _M0L6_2atmpS1073;
        uint32_t _M0L6_2atmpS1074;
        _M0L6bufferS168[_M0L6_2atmpS1070] = _M0L6_2atmpS1071;
        _M0L6_2atmpS1073 = _M0L6offsetS165 - 1;
        _M0L6_2atmpS1074 = _M0L1nS166 >> (_M0L5shiftS163 & 31);
        _M0L6offsetS165 = _M0L6_2atmpS1073;
        _M0L1nS166 = _M0L6_2atmpS1074;
        continue;
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1083 = _M0L10total__lenS171 - _M0L12digit__startS169;
    int32_t _M0L6offsetS173 = _M0L6_2atmpS1083;
    uint32_t _M0L1nS174 = _M0L3numS172;
    while (1) {
      if (_M0L1nS174 > 0u) {
        uint32_t _M0L1qS175 = _M0L1nS174 / _M0L4baseS161;
        uint32_t _M0L6_2atmpS1082 = _M0L1qS175 * _M0L4baseS161;
        uint32_t _M0L6_2atmpS1081 = _M0L1nS174 - _M0L6_2atmpS1082;
        int32_t _M0L5digitS176 = *(int32_t*)&_M0L6_2atmpS1081;
        int32_t _M0L6_2atmpS1079 = _M0L12digit__startS169 + _M0L6offsetS173;
        int32_t _M0L6_2atmpS1077 = _M0L6_2atmpS1079 - 1;
        int32_t _M0L6_2atmpS1078 =
          ((moonbit_string_t)moonbit_string_literal_20.data)[_M0L5digitS176];
        int32_t _M0L6_2atmpS1080;
        _M0L6bufferS168[_M0L6_2atmpS1077] = _M0L6_2atmpS1078;
        _M0L6_2atmpS1080 = _M0L6offsetS173 - 1;
        _M0L6offsetS173 = _M0L6_2atmpS1080;
        _M0L1nS174 = _M0L1qS175;
        continue;
      }
      break;
    }
  }
  return 0;
}

int32_t _M0FPB20int__to__string__hex(
  uint16_t* _M0L6bufferS155,
  uint32_t _M0L3numS160,
  int32_t _M0L12digit__startS156,
  int32_t _M0L10total__lenS159
) {
  int32_t _M0L6_2atmpS1067;
  int32_t _M0L6offsetS150;
  uint32_t _M0L1nS151;
  #line 29 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L6_2atmpS1067 = _M0L10total__lenS159 - _M0L12digit__startS156;
  _M0L6offsetS150 = _M0L6_2atmpS1067;
  _M0L1nS151 = _M0L3numS160;
  while (1) {
    if (_M0L6offsetS150 >= 2) {
      uint32_t _M0L6_2atmpS1064 = _M0L1nS151 & 255u;
      int32_t _M0L9byte__valS152 = *(int32_t*)&_M0L6_2atmpS1064;
      int32_t _M0L2hiS153 = _M0L9byte__valS152 / 16;
      int32_t _M0L2loS154 = _M0L9byte__valS152 % 16;
      int32_t _M0L6_2atmpS1058 = _M0L12digit__startS156 + _M0L6offsetS150;
      int32_t _M0L6_2atmpS1056 = _M0L6_2atmpS1058 - 2;
      int32_t _M0L6_2atmpS1057 =
        ((moonbit_string_t)moonbit_string_literal_20.data)[_M0L2hiS153];
      int32_t _M0L6_2atmpS1061;
      int32_t _M0L6_2atmpS1059;
      int32_t _M0L6_2atmpS1060;
      int32_t _M0L6_2atmpS1062;
      uint32_t _M0L6_2atmpS1063;
      _M0L6bufferS155[_M0L6_2atmpS1056] = _M0L6_2atmpS1057;
      _M0L6_2atmpS1061 = _M0L12digit__startS156 + _M0L6offsetS150;
      _M0L6_2atmpS1059 = _M0L6_2atmpS1061 - 1;
      _M0L6_2atmpS1060
      = ((moonbit_string_t)moonbit_string_literal_20.data)[
        _M0L2loS154
      ];
      _M0L6bufferS155[_M0L6_2atmpS1059] = _M0L6_2atmpS1060;
      _M0L6_2atmpS1062 = _M0L6offsetS150 - 2;
      _M0L6_2atmpS1063 = _M0L1nS151 >> 8;
      _M0L6offsetS150 = _M0L6_2atmpS1062;
      _M0L1nS151 = _M0L6_2atmpS1063;
      continue;
    } else if (_M0L6offsetS150 == 1) {
      uint32_t _M0L6_2atmpS1066 = _M0L1nS151 & 15u;
      int32_t _M0L6nibbleS158 = *(int32_t*)&_M0L6_2atmpS1066;
      int32_t _M0L6_2atmpS1065 =
        ((moonbit_string_t)moonbit_string_literal_20.data)[_M0L6nibbleS158];
      _M0L6bufferS155[_M0L12digit__startS156] = _M0L6_2atmpS1065;
    }
    break;
  }
  return 0;
}

int32_t _M0IP016_24default__implPB4Show6outputGsE(
  moonbit_string_t _M0L4selfS145,
  struct _M0TPB6Logger _M0L6loggerS144
) {
  moonbit_string_t _M0L6_2atmpS1053;
  #line 159 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  #line 160 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS1053 = _M0IPC16string6StringPB4Show10to__string(_M0L4selfS145);
  #line 160 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6loggerS144.$0->$method_0(_M0L6loggerS144.$1, _M0L6_2atmpS1053);
  moonbit_decref(_M0L6_2atmpS1053);
  return 0;
}

int32_t _M0IP016_24default__implPB4Show6outputGiE(
  int32_t _M0L4selfS147,
  struct _M0TPB6Logger _M0L6loggerS146
) {
  moonbit_string_t _M0L6_2atmpS1054;
  #line 159 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  #line 160 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS1054 = _M0IPC13int3IntPB4Show10to__string(_M0L4selfS147);
  #line 160 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6loggerS146.$0->$method_0(_M0L6loggerS146.$1, _M0L6_2atmpS1054);
  moonbit_decref(_M0L6_2atmpS1054);
  return 0;
}

int32_t _M0IP016_24default__implPB4Show6outputGmE(
  uint64_t _M0L4selfS149,
  struct _M0TPB6Logger _M0L6loggerS148
) {
  moonbit_string_t _M0L6_2atmpS1055;
  #line 159 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  #line 160 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS1055 = _M0IPC16uint646UInt64PB4Show10to__string(_M0L4selfS149);
  #line 160 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6loggerS148.$0->$method_0(_M0L6loggerS148.$1, _M0L6_2atmpS1055);
  moonbit_decref(_M0L6_2atmpS1055);
  return 0;
}

int32_t _M0MPC16string10StringView13start__offset(
  struct _M0TPC16string10StringView _M0L4selfS143
) {
  #line 99 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
  return _M0L4selfS143.$1;
}

moonbit_string_t _M0MPC16string10StringView4data(
  struct _M0TPC16string10StringView _M0L4selfS142
) {
  moonbit_string_t _M0L8_2afieldS2057;
  #line 92 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
  _M0L8_2afieldS2057 = _M0L4selfS142.$0;
  moonbit_incref(_M0L8_2afieldS2057);
  return _M0L8_2afieldS2057;
}

int32_t _M0IP016_24default__implPB6Logger16write__substringGRPB13StringBuilderE(
  struct _M0TPB13StringBuilder* _M0L4selfS138,
  moonbit_string_t _M0L5valueS139,
  int32_t _M0L5startS140,
  int32_t _M0L3lenS141
) {
  int32_t _M0L6_2atmpS1052;
  int64_t _M0L6_2atmpS1051;
  struct _M0TPC16string10StringView _M0L6_2atmpS1050;
  #line 122 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS1052 = _M0L5startS140 + _M0L3lenS141;
  _M0L6_2atmpS1051 = (int64_t)_M0L6_2atmpS1052;
  #line 123 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS1050
  = _M0MPC16string6String11sub_2einner(_M0L5valueS139, _M0L5startS140, _M0L6_2atmpS1051);
  #line 123 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0IPB13StringBuilderPB6Logger11write__view(_M0L4selfS138, _M0L6_2atmpS1050);
  moonbit_decref(_M0L6_2atmpS1050.$0);
  return 0;
}

struct _M0TPC16string10StringView _M0MPC16string6String11sub_2einner(
  moonbit_string_t _M0L4selfS130,
  int32_t _M0L5startS137,
  int64_t _M0L3endS134
) {
  int32_t _M0L3lenS129;
  int32_t _M0L3endS133;
  int32_t _M0L3endS131;
  #line 923 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
  _M0L3lenS129 = Moonbit_array_length(_M0L4selfS130);
  if (_M0L3endS134 == 4294967296ll) {
    _M0L3endS133 = _M0L3lenS129;
    goto join_132;
  } else {
    int64_t _M0L7_2aSomeS135 = _M0L3endS134;
    int32_t _M0L6_2aendS136 = (int32_t)_M0L7_2aSomeS135;
    _M0L3endS133 = _M0L6_2aendS136;
    goto join_132;
  }
  goto joinlet_2167;
  join_132:;
  _M0L3endS131 = _M0L3endS133;
  joinlet_2167:;
  if (
    _M0L5startS137 >= 0
    && _M0L5startS137 <= _M0L3endS131
    && _M0L3endS131 <= _M0L3lenS129
  ) {
    if (_M0L5startS137 < _M0L3lenS129) {
      int32_t _M0L6_2atmpS1047 = _M0L4selfS130[_M0L5startS137];
      int32_t _M0L6_2atmpS1046;
      #line 931 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
      _M0L6_2atmpS1046
      = _M0MPC16uint166UInt1623is__trailing__surrogate(_M0L6_2atmpS1047);
      if (!_M0L6_2atmpS1046) {
        
      } else {
        #line 931 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
        moonbit_panic();
      }
    }
    if (_M0L3endS131 < _M0L3lenS129) {
      int32_t _M0L6_2atmpS1049 = _M0L4selfS130[_M0L3endS131];
      int32_t _M0L6_2atmpS1048;
      #line 934 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
      _M0L6_2atmpS1048
      = _M0MPC16uint166UInt1623is__trailing__surrogate(_M0L6_2atmpS1049);
      if (!_M0L6_2atmpS1048) {
        
      } else {
        #line 934 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
        moonbit_panic();
      }
    }
    moonbit_incref(_M0L4selfS130);
    return (struct _M0TPC16string10StringView){.$0 = _M0L4selfS130,
                                                 .$1 = _M0L5startS137,
                                                 .$2 = _M0L3endS131};
  } else {
    #line 929 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
    moonbit_panic();
  }
}

int32_t _M0IP016_24default__implPB6Logger5writeGRPB13StringBuilderE(
  struct _M0TPB13StringBuilder* _M0L4selfS128,
  struct _M0TPB4Show _M0L4showS127
) {
  struct _M0TPB6Logger _M0L6_2atmpS1045;
  #line 116 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  moonbit_incref(_M0L4selfS128);
  _M0L6_2atmpS1045
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS128
  };
  #line 117 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L4showS127.$0->$method_0(_M0L4showS127.$1, _M0L6_2atmpS1045);
  if (_M0L6_2atmpS1045.$1) {
    moonbit_decref(_M0L6_2atmpS1045.$1);
  }
  return 0;
}

int32_t _M0IP016_24default__implPB6Logger28write__string__interpolationGRPB13StringBuilderE(
  struct _M0TPB13StringBuilder* _M0L4selfS126,
  struct _M0TPB4Show _M0L4showS125
) {
  struct _M0TPB6Logger _M0L6_2atmpS1044;
  #line 111 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  moonbit_incref(_M0L4selfS126);
  _M0L6_2atmpS1044
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS126
  };
  #line 112 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L4showS125.$0->$method_0(_M0L4showS125.$1, _M0L6_2atmpS1044);
  if (_M0L6_2atmpS1044.$1) {
    moonbit_decref(_M0L6_2atmpS1044.$1);
  }
  return 0;
}

uint64_t _M0MPC13int3Int10to__uint64(int32_t _M0L4selfS124) {
  int64_t _M0L6_2atmpS1043;
  #line 989 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS1043 = (int64_t)_M0L4selfS124;
  return *(uint64_t*)&_M0L6_2atmpS1043;
}

int32_t _M0IPC16uint166UInt16PB7Default7default() {
  #line 201 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uint16_char.mbt"
  return 0;
}

int32_t _M0MPC16uint166UInt1623is__trailing__surrogate(int32_t _M0L4selfS123) {
  #line 58 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uint16_char.mbt"
  return _M0L4selfS123 >= 56320 && _M0L4selfS123 <= 57343;
}

int32_t _M0IPB13StringBuilderPB6Logger13write__string(
  struct _M0TPB13StringBuilder* _M0L4selfS122,
  moonbit_string_t _M0L3strS120
) {
  int32_t _M0L8str__lenS119;
  int32_t _M0L3lenS1042;
  int32_t _M0L8requiredS121;
  uint16_t* _M0L4dataS1037;
  int32_t _M0L6_2atmpS1036;
  int32_t _if__result_2168;
  uint16_t* _M0L4dataS1038;
  int32_t _M0L3lenS1039;
  int32_t _M0L3lenS1041;
  int32_t _M0L6_2atmpS1040;
  #line 105 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L8str__lenS119 = Moonbit_array_length(_M0L3strS120);
  if (_M0L8str__lenS119 == 0) {
    return 0;
  }
  _M0L3lenS1042 = _M0L4selfS122->$1;
  _M0L8requiredS121 = _M0L3lenS1042 + _M0L8str__lenS119;
  _M0L4dataS1037 = _M0L4selfS122->$0;
  _M0L6_2atmpS1036 = Moonbit_array_length(_M0L4dataS1037);
  if (_M0L8requiredS121 > _M0L6_2atmpS1036) {
    _if__result_2168 = 1;
  } else {
    int32_t _M0L3lenS1035 = _M0L4selfS122->$1;
    _if__result_2168 = _M0L8requiredS121 < _M0L3lenS1035;
  }
  if (_if__result_2168) {
    #line 112 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0MPB13StringBuilder4grow(_M0L4selfS122, _M0L8requiredS121);
  }
  _M0L4dataS1038 = _M0L4selfS122->$0;
  _M0L3lenS1039 = _M0L4selfS122->$1;
  moonbit_incref(_M0L4dataS1038);
  #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0MPC15array10FixedArray26unsafe__blit__from__string(_M0L4dataS1038, _M0L3lenS1039, _M0L3strS120, 0, _M0L8str__lenS119);
  moonbit_decref(_M0L4dataS1038);
  _M0L3lenS1041 = _M0L4selfS122->$1;
  _M0L6_2atmpS1040 = _M0L3lenS1041 + _M0L8str__lenS119;
  _M0L4selfS122->$1 = _M0L6_2atmpS1040;
  return 0;
}

int32_t _M0MPC15array10FixedArray26unsafe__blit__from__string(
  uint16_t* _M0L4selfS115,
  int32_t _M0L11dst__offsetS118,
  moonbit_string_t _M0L3strS116,
  int32_t _M0L11str__offsetS111,
  int32_t _M0L3lenS112
) {
  int32_t _M0L16end__str__offsetS110;
  int32_t _M0L1iS113;
  int32_t _M0L1jS114;
  #line 90 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L16end__str__offsetS110 = _M0L11str__offsetS111 + _M0L3lenS112;
  _M0L1iS113 = _M0L11str__offsetS111;
  _M0L1jS114 = _M0L11dst__offsetS118;
  while (1) {
    if (_M0L1iS113 < _M0L16end__str__offsetS110) {
      int32_t _M0L6_2atmpS1032 = _M0L3strS116[_M0L1iS113];
      int32_t _M0L6_2atmpS1033;
      int32_t _M0L6_2atmpS1034;
      _M0L4selfS115[_M0L1jS114] = _M0L6_2atmpS1032;
      _M0L6_2atmpS1033 = _M0L1iS113 + 1;
      _M0L6_2atmpS1034 = _M0L1jS114 + 1;
      _M0L1iS113 = _M0L6_2atmpS1033;
      _M0L1jS114 = _M0L6_2atmpS1034;
      continue;
    }
    break;
  }
  return 0;
}

int32_t _M0IPB13StringBuilderPB6Logger11write__char(
  struct _M0TPB13StringBuilder* _M0L4selfS108,
  int32_t _M0L2chS107
) {
  uint32_t _M0L4codeS106;
  #line 120 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  #line 121 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L4codeS106 = _M0MPC14char4Char8to__uint(_M0L2chS107);
  if (_M0L4codeS106 <= 65535u) {
    int32_t _M0L3lenS1003 = _M0L4selfS108->$1;
    uint16_t* _M0L4dataS1005 = _M0L4selfS108->$0;
    int32_t _M0L6_2atmpS1004 = Moonbit_array_length(_M0L4dataS1005);
    uint16_t* _M0L4dataS1008;
    int32_t _M0L3lenS1009;
    int32_t _M0L6_2atmpS1010;
    int32_t _M0L3lenS1012;
    int32_t _M0L6_2atmpS1011;
    if (_M0L3lenS1003 >= _M0L6_2atmpS1004) {
      int32_t _M0L3lenS1007 = _M0L4selfS108->$1;
      int32_t _M0L6_2atmpS1006 = _M0L3lenS1007 + 1;
      #line 124 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0MPB13StringBuilder4grow(_M0L4selfS108, _M0L6_2atmpS1006);
    }
    _M0L4dataS1008 = _M0L4selfS108->$0;
    _M0L3lenS1009 = _M0L4selfS108->$1;
    moonbit_incref(_M0L4dataS1008);
    #line 126 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0L6_2atmpS1010 = _M0MPC14uint4UInt10to__uint16(_M0L4codeS106);
    if (
      _M0L3lenS1009 < 0
      || _M0L3lenS1009 >= Moonbit_array_length(_M0L4dataS1008)
    ) {
      #line 126 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      moonbit_panic();
    }
    _M0L4dataS1008[_M0L3lenS1009] = _M0L6_2atmpS1010;
    moonbit_decref(_M0L4dataS1008);
    _M0L3lenS1012 = _M0L4selfS108->$1;
    _M0L6_2atmpS1011 = _M0L3lenS1012 + 1;
    _M0L4selfS108->$1 = _M0L6_2atmpS1011;
  } else if (_M0L4codeS106 <= 1114111u) {
    uint16_t* _M0L4dataS1016 = _M0L4selfS108->$0;
    int32_t _M0L6_2atmpS1014 = Moonbit_array_length(_M0L4dataS1016);
    int32_t _M0L3lenS1015 = _M0L4selfS108->$1;
    int32_t _M0L6_2atmpS1013 = _M0L6_2atmpS1014 - _M0L3lenS1015;
    uint32_t _M0L4codeS109;
    uint16_t* _M0L4dataS1019;
    int32_t _M0L3lenS1020;
    uint32_t _M0L6_2atmpS1023;
    uint32_t _M0L6_2atmpS1022;
    int32_t _M0L6_2atmpS1021;
    uint16_t* _M0L4dataS1024;
    int32_t _M0L3lenS1029;
    int32_t _M0L6_2atmpS1025;
    uint32_t _M0L6_2atmpS1028;
    uint32_t _M0L6_2atmpS1027;
    int32_t _M0L6_2atmpS1026;
    int32_t _M0L3lenS1031;
    int32_t _M0L6_2atmpS1030;
    if (_M0L6_2atmpS1013 < 2) {
      int32_t _M0L3lenS1018 = _M0L4selfS108->$1;
      int32_t _M0L6_2atmpS1017 = _M0L3lenS1018 + 2;
      #line 130 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0MPB13StringBuilder4grow(_M0L4selfS108, _M0L6_2atmpS1017);
    }
    _M0L4codeS109 = _M0L4codeS106 - 65536u;
    _M0L4dataS1019 = _M0L4selfS108->$0;
    _M0L3lenS1020 = _M0L4selfS108->$1;
    _M0L6_2atmpS1023 = _M0L4codeS109 >> 10;
    _M0L6_2atmpS1022 = 55296u + _M0L6_2atmpS1023;
    moonbit_incref(_M0L4dataS1019);
    #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0L6_2atmpS1021 = _M0MPC14uint4UInt10to__uint16(_M0L6_2atmpS1022);
    if (
      _M0L3lenS1020 < 0
      || _M0L3lenS1020 >= Moonbit_array_length(_M0L4dataS1019)
    ) {
      #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      moonbit_panic();
    }
    _M0L4dataS1019[_M0L3lenS1020] = _M0L6_2atmpS1021;
    moonbit_decref(_M0L4dataS1019);
    _M0L4dataS1024 = _M0L4selfS108->$0;
    _M0L3lenS1029 = _M0L4selfS108->$1;
    _M0L6_2atmpS1025 = _M0L3lenS1029 + 1;
    _M0L6_2atmpS1028 = _M0L4codeS109 & 1023u;
    _M0L6_2atmpS1027 = 56320u + _M0L6_2atmpS1028;
    moonbit_incref(_M0L4dataS1024);
    #line 134 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0L6_2atmpS1026 = _M0MPC14uint4UInt10to__uint16(_M0L6_2atmpS1027);
    if (
      _M0L6_2atmpS1025 < 0
      || _M0L6_2atmpS1025 >= Moonbit_array_length(_M0L4dataS1024)
    ) {
      #line 134 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      moonbit_panic();
    }
    _M0L4dataS1024[_M0L6_2atmpS1025] = _M0L6_2atmpS1026;
    moonbit_decref(_M0L4dataS1024);
    _M0L3lenS1031 = _M0L4selfS108->$1;
    _M0L6_2atmpS1030 = _M0L3lenS1031 + 2;
    _M0L4selfS108->$1 = _M0L6_2atmpS1030;
  } else {
    #line 137 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_21.data);
  }
  return 0;
}

int32_t _M0MPB13StringBuilder4grow(
  struct _M0TPB13StringBuilder* _M0L4selfS103,
  int32_t _M0L8requiredS104
) {
  uint16_t* _M0L4dataS1002;
  int32_t _M0L6_2atmpS1000;
  int32_t _M0L3lenS1001;
  int32_t _M0L13new__capacityS102;
  uint16_t* _M0L4dataS997;
  int32_t _M0L6_2atmpS998;
  int32_t _M0L3lenS999;
  uint16_t* _M0L9new__dataS105;
  uint16_t* _M0L6_2aoldS2058;
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L4dataS1002 = _M0L4selfS103->$0;
  _M0L6_2atmpS1000 = Moonbit_array_length(_M0L4dataS1002);
  _M0L3lenS1001 = _M0L4selfS103->$1;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L13new__capacityS102
  = _M0FPB31stringbuilder__growth__capacity(_M0L6_2atmpS1000, _M0L3lenS1001, _M0L8requiredS104);
  _M0L4dataS997 = _M0L4selfS103->$0;
  moonbit_incref(_M0L4dataS997);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L6_2atmpS998 = _M0IPC16uint166UInt16PB7Default7default();
  _M0L3lenS999 = _M0L4selfS103->$1;
  #line 80 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L9new__dataS105
  = _M0MPC15array10FixedArray23make__and__blit_2einnerGkE(_M0L4dataS997, _M0L13new__capacityS102, _M0L6_2atmpS998, _M0L3lenS999, 0, 0);
  _M0L6_2aoldS2058 = _M0L4selfS103->$0;
  moonbit_decref(_M0L6_2aoldS2058);
  _M0L4selfS103->$0 = _M0L9new__dataS105;
  return 0;
}

int32_t _M0FPB31stringbuilder__growth__capacity(
  int32_t _M0L7currentS101,
  int32_t _M0L3lenS97,
  int32_t _M0L8requiredS96
) {
  int32_t _M0L5spaceS98;
  #line 48 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  if (_M0L8requiredS96 < _M0L3lenS97) {
    #line 55 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_22.data);
  }
  _M0L5spaceS98 = _M0L7currentS101;
  while (1) {
    if (_M0L5spaceS98 < _M0L8requiredS96) {
      int32_t _M0L4nextS99 = _M0L5spaceS98 * 2;
      if (_M0L4nextS99 <= _M0L5spaceS98) {
        return _M0L8requiredS96;
      }
      _M0L5spaceS98 = _M0L4nextS99;
      continue;
    } else {
      return _M0L5spaceS98;
    }
    break;
  }
}

int32_t _M0MPC14uint4UInt10to__uint16(uint32_t _M0L4selfS95) {
  int32_t _M0L6_2atmpS996;
  #line 2785 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS996 = *(int32_t*)&_M0L4selfS95;
  return (uint16_t)_M0L6_2atmpS996;
}

uint32_t _M0MPC14char4Char8to__uint(int32_t _M0L4selfS94) {
  int32_t _M0L6_2atmpS995;
  #line 1335 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS995 = _M0L4selfS94;
  return *(uint32_t*)&_M0L6_2atmpS995;
}

moonbit_string_t _M0MPB13StringBuilder10to__string(
  struct _M0TPB13StringBuilder* _M0L4selfS92
) {
  int32_t _M0L3lenS986;
  #line 181 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L3lenS986 = _M0L4selfS92->$1;
  if (_M0L3lenS986 == 0) {
    return (moonbit_string_t)moonbit_string_literal_0.data;
  } else {
    int32_t _M0L3lenS987 = _M0L4selfS92->$1;
    uint16_t* _M0L4dataS989 = _M0L4selfS92->$0;
    int32_t _M0L6_2atmpS988 = Moonbit_array_length(_M0L4dataS989);
    if (_M0L3lenS987 == _M0L6_2atmpS988) {
      uint16_t* _M0L4dataS990 = _M0L4selfS92->$0;
      moonbit_incref(_M0L4dataS990);
      return _M0L4dataS990;
    } else {
      uint16_t* _M0L4dataS991 = _M0L4selfS92->$0;
      int32_t _M0L3lenS992 = _M0L4selfS92->$1;
      int32_t _M0L6_2atmpS993;
      int32_t _M0L3lenS994;
      uint16_t* _M0L4dataS93;
      moonbit_incref(_M0L4dataS991);
      #line 190 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0L6_2atmpS993 = _M0IPC16uint166UInt16PB7Default7default();
      _M0L3lenS994 = _M0L4selfS92->$1;
      #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0L4dataS93
      = _M0MPC15array10FixedArray23make__and__blit_2einnerGkE(_M0L4dataS991, _M0L3lenS992, _M0L6_2atmpS993, _M0L3lenS994, 0, 0);
      return _M0L4dataS93;
    }
  }
}

uint16_t* _M0MPC15array10FixedArray23make__and__blit_2einnerGkE(
  uint16_t* _M0L3srcS89,
  int32_t _M0L13allocate__lenS85,
  int32_t _M0L4initS90,
  int32_t _M0L3lenS86,
  int32_t _M0L11src__offsetS87,
  int32_t _M0L11dst__offsetS88
) {
  int32_t _if__result_2171;
  #line 97 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
  if (_M0L13allocate__lenS85 >= 0) {
    if (_M0L3lenS86 >= 0) {
      if (_M0L11src__offsetS87 >= 0) {
        if (_M0L11dst__offsetS88 >= 0) {
          int32_t _M0L6_2atmpS982 = _M0L11src__offsetS87 + _M0L3lenS86;
          int32_t _M0L6_2atmpS983 = Moonbit_array_length(_M0L3srcS89);
          if (_M0L6_2atmpS982 <= _M0L6_2atmpS983) {
            int32_t _M0L6_2atmpS981 = _M0L11dst__offsetS88 + _M0L3lenS86;
            _if__result_2171 = _M0L6_2atmpS981 <= _M0L13allocate__lenS85;
          } else {
            _if__result_2171 = 0;
          }
        } else {
          _if__result_2171 = 0;
        }
      } else {
        _if__result_2171 = 0;
      }
    } else {
      _if__result_2171 = 0;
    }
  } else {
    _if__result_2171 = 0;
  }
  if (_if__result_2171) {
    #line 116 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    return _M0MPC15array10FixedArray23unsafe__make__and__blitGkE(_M0L3srcS89, _M0L13allocate__lenS85, _M0L4initS90, _M0L11src__offsetS87, _M0L11dst__offsetS88, _M0L3lenS86);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS91;
    int32_t _M0L6_2atmpS985;
    moonbit_string_t _M0L6_2atmpS984;
    uint16_t* _result_2172;
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0L18_2astring__builderS91
    = _M0MPB13StringBuilder21StringBuilder_2einner(89);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS91, (moonbit_string_t)moonbit_string_literal_23.data);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS91, _M0L13allocate__lenS85);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS91, (moonbit_string_t)moonbit_string_literal_24.data);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS91, _M0L11src__offsetS87);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS91, (moonbit_string_t)moonbit_string_literal_25.data);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS91, _M0L11dst__offsetS88);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS91, (moonbit_string_t)moonbit_string_literal_26.data);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS91, _M0L3lenS86);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS91, (moonbit_string_t)moonbit_string_literal_27.data);
    _M0L6_2atmpS985 = Moonbit_array_length(_M0L3srcS89);
    moonbit_decref(_M0L3srcS89);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS91, _M0L6_2atmpS985);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0L6_2atmpS984
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS91);
    moonbit_decref(_M0L18_2astring__builderS91);
    #line 112 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _result_2172 = _M0FPC15abort5abortGAkE(_M0L6_2atmpS984);
    moonbit_decref(_M0L6_2atmpS984);
    return _result_2172;
  }
}

uint16_t* _M0MPC15array10FixedArray23unsafe__make__and__blitGkE(
  uint16_t* _M0L3srcS82,
  int32_t _M0L13allocate__lenS79,
  int32_t _M0L4initS80,
  int32_t _M0L11src__offsetS83,
  int32_t _M0L11dst__offsetS81,
  int32_t _M0L9blit__lenS84
) {
  uint16_t* _M0L3dstS78;
  #line 79 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
  _M0L3dstS78
  = (uint16_t*)moonbit_make_string(_M0L13allocate__lenS79, _M0L4initS80);
  moonbit_incref(_M0L3dstS78);
  #line 90 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
  moonbit_unsafe_val_array_blit(_M0L3dstS78, _M0L11dst__offsetS81, _M0L3srcS82, _M0L11src__offsetS83, _M0L9blit__lenS84, sizeof(uint16_t));
  return _M0L3dstS78;
}

struct _M0TPB13StringBuilder* _M0MPB13StringBuilder21StringBuilder_2einner(
  int32_t _M0L10size__hintS76
) {
  int32_t _M0L7initialS75;
  uint16_t* _M0L4dataS77;
  struct _M0TPB13StringBuilder* _block_2173;
  #line 32 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  if (_M0L10size__hintS76 < 1) {
    _M0L7initialS75 = 1;
  } else {
    int32_t _M0L6_2atmpS980 = _M0L10size__hintS76 + 1;
    _M0L7initialS75 = _M0L6_2atmpS980 / 2;
  }
  _M0L4dataS77 = (uint16_t*)moonbit_make_string(_M0L7initialS75, 0);
  _block_2173
  = (struct _M0TPB13StringBuilder*)moonbit_malloc(sizeof(struct _M0TPB13StringBuilder));
  Moonbit_object_header(_block_2173)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 54, 0);
  _block_2173->$0 = _M0L4dataS77;
  _block_2173->$1 = 0;
  return _block_2173;
}

int32_t* _M0MPB18UninitializedArray23make__and__blit_2einnerGiE(
  int32_t* _M0L3srcS67,
  int32_t _M0L13allocate__lenS63,
  int32_t _M0L3lenS64,
  int32_t _M0L11src__offsetS65,
  int32_t _M0L11dst__offsetS66
) {
  int32_t _if__result_2174;
  #line 167 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  if (_M0L13allocate__lenS63 >= 0) {
    if (_M0L3lenS64 >= 0) {
      if (_M0L11src__offsetS65 >= 0) {
        if (_M0L11dst__offsetS66 >= 0) {
          int32_t _M0L6_2atmpS971 = _M0L11src__offsetS65 + _M0L3lenS64;
          int32_t _M0L6_2atmpS972;
          #line 179 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
          _M0L6_2atmpS972 = _M0MPB18UninitializedArray6lengthGiE(_M0L3srcS67);
          if (_M0L6_2atmpS971 <= _M0L6_2atmpS972) {
            int32_t _M0L6_2atmpS970 = _M0L11dst__offsetS66 + _M0L3lenS64;
            _if__result_2174 = _M0L6_2atmpS970 <= _M0L13allocate__lenS63;
          } else {
            _if__result_2174 = 0;
          }
        } else {
          _if__result_2174 = 0;
        }
      } else {
        _if__result_2174 = 0;
      }
    } else {
      _if__result_2174 = 0;
    }
  } else {
    _if__result_2174 = 0;
  }
  if (_if__result_2174) {
    #line 185 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    return _M0MPB18UninitializedArray23unsafe__make__and__blitGiE(_M0L3srcS67, _M0L13allocate__lenS63, _M0L11src__offsetS65, _M0L11dst__offsetS66, _M0L3lenS64);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS68;
    int32_t _M0L6_2atmpS974;
    moonbit_string_t _M0L6_2atmpS973;
    int32_t* _result_2175;
    #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L18_2astring__builderS68
    = _M0MPB13StringBuilder21StringBuilder_2einner(89);
    #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS68, (moonbit_string_t)moonbit_string_literal_23.data);
    #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS68, _M0L13allocate__lenS63);
    #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS68, (moonbit_string_t)moonbit_string_literal_24.data);
    #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS68, _M0L11src__offsetS65);
    #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS68, (moonbit_string_t)moonbit_string_literal_25.data);
    #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS68, _M0L11dst__offsetS66);
    #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS68, (moonbit_string_t)moonbit_string_literal_26.data);
    #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS68, _M0L3lenS64);
    #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS68, (moonbit_string_t)moonbit_string_literal_27.data);
    #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS974 = _M0MPB18UninitializedArray6lengthGiE(_M0L3srcS67);
    moonbit_decref(_M0L3srcS67);
    #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS68, _M0L6_2atmpS974);
    #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS973
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS68);
    moonbit_decref(_M0L18_2astring__builderS68);
    #line 181 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _result_2175
    = _M0FPC15abort5abortGRPB18UninitializedArrayGiEE(_M0L6_2atmpS973);
    moonbit_decref(_M0L6_2atmpS973);
    return _result_2175;
  }
}

float* _M0MPB18UninitializedArray23make__and__blit_2einnerGfE(
  float* _M0L3srcS73,
  int32_t _M0L13allocate__lenS69,
  int32_t _M0L3lenS70,
  int32_t _M0L11src__offsetS71,
  int32_t _M0L11dst__offsetS72
) {
  int32_t _if__result_2176;
  #line 167 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  if (_M0L13allocate__lenS69 >= 0) {
    if (_M0L3lenS70 >= 0) {
      if (_M0L11src__offsetS71 >= 0) {
        if (_M0L11dst__offsetS72 >= 0) {
          int32_t _M0L6_2atmpS976 = _M0L11src__offsetS71 + _M0L3lenS70;
          int32_t _M0L6_2atmpS977;
          #line 179 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
          _M0L6_2atmpS977 = _M0MPB18UninitializedArray6lengthGfE(_M0L3srcS73);
          if (_M0L6_2atmpS976 <= _M0L6_2atmpS977) {
            int32_t _M0L6_2atmpS975 = _M0L11dst__offsetS72 + _M0L3lenS70;
            _if__result_2176 = _M0L6_2atmpS975 <= _M0L13allocate__lenS69;
          } else {
            _if__result_2176 = 0;
          }
        } else {
          _if__result_2176 = 0;
        }
      } else {
        _if__result_2176 = 0;
      }
    } else {
      _if__result_2176 = 0;
    }
  } else {
    _if__result_2176 = 0;
  }
  if (_if__result_2176) {
    #line 185 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    return _M0MPB18UninitializedArray23unsafe__make__and__blitGfE(_M0L3srcS73, _M0L13allocate__lenS69, _M0L11src__offsetS71, _M0L11dst__offsetS72, _M0L3lenS70);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS74;
    int32_t _M0L6_2atmpS979;
    moonbit_string_t _M0L6_2atmpS978;
    float* _result_2177;
    #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L18_2astring__builderS74
    = _M0MPB13StringBuilder21StringBuilder_2einner(89);
    #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS74, (moonbit_string_t)moonbit_string_literal_23.data);
    #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS74, _M0L13allocate__lenS69);
    #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS74, (moonbit_string_t)moonbit_string_literal_24.data);
    #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS74, _M0L11src__offsetS71);
    #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS74, (moonbit_string_t)moonbit_string_literal_25.data);
    #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS74, _M0L11dst__offsetS72);
    #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS74, (moonbit_string_t)moonbit_string_literal_26.data);
    #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS74, _M0L3lenS70);
    #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS74, (moonbit_string_t)moonbit_string_literal_27.data);
    #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS979 = _M0MPB18UninitializedArray6lengthGfE(_M0L3srcS73);
    moonbit_decref(_M0L3srcS73);
    #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS74, _M0L6_2atmpS979);
    #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS978
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS74);
    moonbit_decref(_M0L18_2astring__builderS74);
    #line 181 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _result_2177
    = _M0FPC15abort5abortGRPB18UninitializedArrayGfEE(_M0L6_2atmpS978);
    moonbit_decref(_M0L6_2atmpS978);
    return _result_2177;
  }
}

int32_t _M0MPB13StringBuilder13write__objectGsE(
  struct _M0TPB13StringBuilder* _M0L4selfS58,
  moonbit_string_t _M0L3objS57
) {
  struct _M0TPB6Logger _M0L6_2atmpS967;
  #line 17 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  moonbit_incref(_M0L4selfS58);
  _M0L6_2atmpS967
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS58
  };
  #line 23 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  _M0IP016_24default__implPB4Show6outputGsE(_M0L3objS57, _M0L6_2atmpS967);
  if (_M0L6_2atmpS967.$1) {
    moonbit_decref(_M0L6_2atmpS967.$1);
  }
  return 0;
}

int32_t _M0MPB13StringBuilder13write__objectGiE(
  struct _M0TPB13StringBuilder* _M0L4selfS60,
  int32_t _M0L3objS59
) {
  struct _M0TPB6Logger _M0L6_2atmpS968;
  #line 17 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  moonbit_incref(_M0L4selfS60);
  _M0L6_2atmpS968
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS60
  };
  #line 23 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  _M0IP016_24default__implPB4Show6outputGiE(_M0L3objS59, _M0L6_2atmpS968);
  if (_M0L6_2atmpS968.$1) {
    moonbit_decref(_M0L6_2atmpS968.$1);
  }
  return 0;
}

int32_t _M0MPB13StringBuilder13write__objectGmE(
  struct _M0TPB13StringBuilder* _M0L4selfS62,
  uint64_t _M0L3objS61
) {
  struct _M0TPB6Logger _M0L6_2atmpS969;
  #line 17 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  moonbit_incref(_M0L4selfS62);
  _M0L6_2atmpS969
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS62
  };
  #line 23 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  _M0IP016_24default__implPB4Show6outputGmE(_M0L3objS61, _M0L6_2atmpS969);
  if (_M0L6_2atmpS969.$1) {
    moonbit_decref(_M0L6_2atmpS969.$1);
  }
  return 0;
}

int32_t* _M0MPB18UninitializedArray23unsafe__make__and__blitGiE(
  int32_t* _M0L3srcS48,
  int32_t _M0L13allocate__lenS46,
  int32_t _M0L11src__offsetS49,
  int32_t _M0L11dst__offsetS47,
  int32_t _M0L9blit__lenS50
) {
  int32_t* _M0L3dstS45;
  #line 132 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  _M0L3dstS45
  = (int32_t*)moonbit_make_int32_array_raw(_M0L13allocate__lenS46);
  #line 142 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  _M0MPB18UninitializedArray12unsafe__blitGiE(_M0L3dstS45, _M0L11dst__offsetS47, _M0L3srcS48, _M0L11src__offsetS49, _M0L9blit__lenS50);
  moonbit_decref(_M0L3srcS48);
  return _M0L3dstS45;
}

float* _M0MPB18UninitializedArray23unsafe__make__and__blitGfE(
  float* _M0L3srcS54,
  int32_t _M0L13allocate__lenS52,
  int32_t _M0L11src__offsetS55,
  int32_t _M0L11dst__offsetS53,
  int32_t _M0L9blit__lenS56
) {
  float* _M0L3dstS51;
  #line 132 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  _M0L3dstS51 = (float*)moonbit_make_float_array_raw(_M0L13allocate__lenS52);
  #line 142 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  _M0MPB18UninitializedArray12unsafe__blitGfE(_M0L3dstS51, _M0L11dst__offsetS53, _M0L3srcS54, _M0L11src__offsetS55, _M0L9blit__lenS56);
  moonbit_decref(_M0L3srcS54);
  return _M0L3dstS51;
}

int32_t _M0MPB18UninitializedArray12unsafe__blitGiE(
  int32_t* _M0L3dstS35,
  int32_t _M0L11dst__offsetS36,
  int32_t* _M0L3srcS37,
  int32_t _M0L11src__offsetS38,
  int32_t _M0L3lenS39
) {
  #line 118 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  moonbit_incref(_M0L3srcS37);
  moonbit_incref(_M0L3dstS35);
  #line 127 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  moonbit_unsafe_val_array_blit(_M0L3dstS35, _M0L11dst__offsetS36, _M0L3srcS37, _M0L11src__offsetS38, _M0L3lenS39, sizeof(int32_t));
  return 0;
}

int32_t _M0MPB18UninitializedArray12unsafe__blitGfE(
  float* _M0L3dstS40,
  int32_t _M0L11dst__offsetS41,
  float* _M0L3srcS42,
  int32_t _M0L11src__offsetS43,
  int32_t _M0L3lenS44
) {
  #line 118 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  moonbit_incref(_M0L3srcS42);
  moonbit_incref(_M0L3dstS40);
  #line 127 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  moonbit_unsafe_val_array_blit(_M0L3dstS40, _M0L11dst__offsetS41, _M0L3srcS42, _M0L11src__offsetS43, _M0L3lenS44, sizeof(float));
  return 0;
}

int32_t _M0MPC15array10FixedArray12unsafe__blitGRPB17UnsafeMaybeUninitGiEE(
  int32_t* _M0L3dstS8,
  int32_t _M0L11dst__offsetS10,
  int32_t* _M0L3srcS9,
  int32_t _M0L11src__offsetS11,
  int32_t _M0L3lenS13
) {
  #line 38 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
  if (
    _M0L3dstS8 == _M0L3srcS9 && _M0L11dst__offsetS10 < _M0L11src__offsetS11
  ) {
    int32_t _M0L1iS12 = 0;
    while (1) {
      if (_M0L1iS12 < _M0L3lenS13) {
        int32_t _M0L6_2atmpS940 = _M0L11dst__offsetS10 + _M0L1iS12;
        int32_t _M0L6_2atmpS942 = _M0L11src__offsetS11 + _M0L1iS12;
        int32_t _M0L6_2atmpS941;
        int32_t _M0L6_2atmpS943;
        if (
          _M0L6_2atmpS942 < 0
          || _M0L6_2atmpS942 >= Moonbit_array_length(_M0L3srcS9)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS941 = (int32_t)_M0L3srcS9[_M0L6_2atmpS942];
        if (
          _M0L6_2atmpS940 < 0
          || _M0L6_2atmpS940 >= Moonbit_array_length(_M0L3dstS8)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS8[_M0L6_2atmpS940] = _M0L6_2atmpS941;
        _M0L6_2atmpS943 = _M0L1iS12 + 1;
        _M0L1iS12 = _M0L6_2atmpS943;
        continue;
      } else {
        moonbit_decref(_M0L3srcS9);
        moonbit_decref(_M0L3dstS8);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS948 = _M0L3lenS13 - 1;
    int32_t _M0L1iS15 = _M0L6_2atmpS948;
    while (1) {
      if (_M0L1iS15 >= 0) {
        int32_t _M0L6_2atmpS944 = _M0L11dst__offsetS10 + _M0L1iS15;
        int32_t _M0L6_2atmpS946 = _M0L11src__offsetS11 + _M0L1iS15;
        int32_t _M0L6_2atmpS945;
        int32_t _M0L6_2atmpS947;
        if (
          _M0L6_2atmpS946 < 0
          || _M0L6_2atmpS946 >= Moonbit_array_length(_M0L3srcS9)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS945 = (int32_t)_M0L3srcS9[_M0L6_2atmpS946];
        if (
          _M0L6_2atmpS944 < 0
          || _M0L6_2atmpS944 >= Moonbit_array_length(_M0L3dstS8)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS8[_M0L6_2atmpS944] = _M0L6_2atmpS945;
        _M0L6_2atmpS947 = _M0L1iS15 - 1;
        _M0L1iS15 = _M0L6_2atmpS947;
        continue;
      } else {
        moonbit_decref(_M0L3srcS9);
        moonbit_decref(_M0L3dstS8);
      }
      break;
    }
  }
  return 0;
}

int32_t _M0MPC15array10FixedArray12unsafe__blitGRPB17UnsafeMaybeUninitGfEE(
  float* _M0L3dstS17,
  int32_t _M0L11dst__offsetS19,
  float* _M0L3srcS18,
  int32_t _M0L11src__offsetS20,
  int32_t _M0L3lenS22
) {
  #line 38 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
  if (
    _M0L3dstS17 == _M0L3srcS18 && _M0L11dst__offsetS19 < _M0L11src__offsetS20
  ) {
    int32_t _M0L1iS21 = 0;
    while (1) {
      if (_M0L1iS21 < _M0L3lenS22) {
        int32_t _M0L6_2atmpS949 = _M0L11dst__offsetS19 + _M0L1iS21;
        int32_t _M0L6_2atmpS951 = _M0L11src__offsetS20 + _M0L1iS21;
        float _M0L6_2atmpS950;
        int32_t _M0L6_2atmpS952;
        if (
          _M0L6_2atmpS951 < 0
          || _M0L6_2atmpS951 >= Moonbit_array_length(_M0L3srcS18)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS950 = (float)_M0L3srcS18[_M0L6_2atmpS951];
        if (
          _M0L6_2atmpS949 < 0
          || _M0L6_2atmpS949 >= Moonbit_array_length(_M0L3dstS17)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS17[_M0L6_2atmpS949] = _M0L6_2atmpS950;
        _M0L6_2atmpS952 = _M0L1iS21 + 1;
        _M0L1iS21 = _M0L6_2atmpS952;
        continue;
      } else {
        moonbit_decref(_M0L3srcS18);
        moonbit_decref(_M0L3dstS17);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS957 = _M0L3lenS22 - 1;
    int32_t _M0L1iS24 = _M0L6_2atmpS957;
    while (1) {
      if (_M0L1iS24 >= 0) {
        int32_t _M0L6_2atmpS953 = _M0L11dst__offsetS19 + _M0L1iS24;
        int32_t _M0L6_2atmpS955 = _M0L11src__offsetS20 + _M0L1iS24;
        float _M0L6_2atmpS954;
        int32_t _M0L6_2atmpS956;
        if (
          _M0L6_2atmpS955 < 0
          || _M0L6_2atmpS955 >= Moonbit_array_length(_M0L3srcS18)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS954 = (float)_M0L3srcS18[_M0L6_2atmpS955];
        if (
          _M0L6_2atmpS953 < 0
          || _M0L6_2atmpS953 >= Moonbit_array_length(_M0L3dstS17)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS17[_M0L6_2atmpS953] = _M0L6_2atmpS954;
        _M0L6_2atmpS956 = _M0L1iS24 - 1;
        _M0L1iS24 = _M0L6_2atmpS956;
        continue;
      } else {
        moonbit_decref(_M0L3srcS18);
        moonbit_decref(_M0L3dstS17);
      }
      break;
    }
  }
  return 0;
}

int32_t _M0MPC15array10FixedArray12unsafe__blitGkE(
  uint16_t* _M0L3dstS26,
  int32_t _M0L11dst__offsetS28,
  uint16_t* _M0L3srcS27,
  int32_t _M0L11src__offsetS29,
  int32_t _M0L3lenS31
) {
  #line 38 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
  if (
    _M0L3dstS26 == _M0L3srcS27 && _M0L11dst__offsetS28 < _M0L11src__offsetS29
  ) {
    int32_t _M0L1iS30 = 0;
    while (1) {
      if (_M0L1iS30 < _M0L3lenS31) {
        int32_t _M0L6_2atmpS958 = _M0L11dst__offsetS28 + _M0L1iS30;
        int32_t _M0L6_2atmpS960 = _M0L11src__offsetS29 + _M0L1iS30;
        int32_t _M0L6_2atmpS959;
        int32_t _M0L6_2atmpS961;
        if (
          _M0L6_2atmpS960 < 0
          || _M0L6_2atmpS960 >= Moonbit_array_length(_M0L3srcS27)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS959 = (int32_t)_M0L3srcS27[_M0L6_2atmpS960];
        if (
          _M0L6_2atmpS958 < 0
          || _M0L6_2atmpS958 >= Moonbit_array_length(_M0L3dstS26)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS26[_M0L6_2atmpS958] = _M0L6_2atmpS959;
        _M0L6_2atmpS961 = _M0L1iS30 + 1;
        _M0L1iS30 = _M0L6_2atmpS961;
        continue;
      } else {
        moonbit_decref(_M0L3srcS27);
        moonbit_decref(_M0L3dstS26);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS966 = _M0L3lenS31 - 1;
    int32_t _M0L1iS33 = _M0L6_2atmpS966;
    while (1) {
      if (_M0L1iS33 >= 0) {
        int32_t _M0L6_2atmpS962 = _M0L11dst__offsetS28 + _M0L1iS33;
        int32_t _M0L6_2atmpS964 = _M0L11src__offsetS29 + _M0L1iS33;
        int32_t _M0L6_2atmpS963;
        int32_t _M0L6_2atmpS965;
        if (
          _M0L6_2atmpS964 < 0
          || _M0L6_2atmpS964 >= Moonbit_array_length(_M0L3srcS27)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS963 = (int32_t)_M0L3srcS27[_M0L6_2atmpS964];
        if (
          _M0L6_2atmpS962 < 0
          || _M0L6_2atmpS962 >= Moonbit_array_length(_M0L3dstS26)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS26[_M0L6_2atmpS962] = _M0L6_2atmpS963;
        _M0L6_2atmpS965 = _M0L1iS33 - 1;
        _M0L1iS33 = _M0L6_2atmpS965;
        continue;
      } else {
        moonbit_decref(_M0L3srcS27);
        moonbit_decref(_M0L3dstS26);
      }
      break;
    }
  }
  return 0;
}

int32_t _M0MPB18UninitializedArray6lengthGiE(int32_t* _M0L4selfS6) {
  #line 112 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  return Moonbit_array_length(_M0L4selfS6);
}

int32_t _M0MPB18UninitializedArray6lengthGfE(float* _M0L4selfS7) {
  #line 112 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  return Moonbit_array_length(_M0L4selfS7);
}

int32_t _M0FPC15abort5abortGuE(moonbit_string_t _M0L3msgS1) {
  #line 47 "C:\\Users\\31379\\.moon\\lib\\core\\abort\\abort.mbt"
  #line 49 "C:\\Users\\31379\\.moon\\lib\\core\\abort\\abort.mbt"
  moonbit_println(_M0L3msgS1);
  #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\abort\\abort.mbt"
  moonbit_panic();
  return 0;
}

uint16_t* _M0FPC15abort5abortGAkE(moonbit_string_t _M0L3msgS2) {
  #line 47 "C:\\Users\\31379\\.moon\\lib\\core\\abort\\abort.mbt"
  #line 49 "C:\\Users\\31379\\.moon\\lib\\core\\abort\\abort.mbt"
  moonbit_println(_M0L3msgS2);
  #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\abort\\abort.mbt"
  moonbit_panic();
}

int32_t* _M0FPC15abort5abortGRPB18UninitializedArrayGiEE(
  moonbit_string_t _M0L3msgS3
) {
  #line 47 "C:\\Users\\31379\\.moon\\lib\\core\\abort\\abort.mbt"
  #line 49 "C:\\Users\\31379\\.moon\\lib\\core\\abort\\abort.mbt"
  moonbit_println(_M0L3msgS3);
  #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\abort\\abort.mbt"
  moonbit_panic();
}

float* _M0FPC15abort5abortGRPB18UninitializedArrayGfEE(
  moonbit_string_t _M0L3msgS4
) {
  #line 47 "C:\\Users\\31379\\.moon\\lib\\core\\abort\\abort.mbt"
  #line 49 "C:\\Users\\31379\\.moon\\lib\\core\\abort\\abort.mbt"
  moonbit_println(_M0L3msgS4);
  #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\abort\\abort.mbt"
  moonbit_panic();
}

int32_t _M0FPC15abort5abortGiE(moonbit_string_t _M0L3msgS5) {
  #line 47 "C:\\Users\\31379\\.moon\\lib\\core\\abort\\abort.mbt"
  #line 49 "C:\\Users\\31379\\.moon\\lib\\core\\abort\\abort.mbt"
  moonbit_println(_M0L3msgS5);
  #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\abort\\abort.mbt"
  moonbit_panic();
}

int32_t _M0IP016_24default__implPB6Logger61write_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLoggerGRPB13StringBuilderE(
  void* _M0L11_2aobj__ptrS909,
  struct _M0TPB4Show _M0L8_2aparamS908
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS907 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS909;
  _M0IP016_24default__implPB6Logger5writeGRPB13StringBuilderE(_M0L7_2aselfS907, _M0L8_2aparamS908);
  return 0;
}

int32_t _M0IP016_24default__implPB6Logger84write__string__interpolation_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLoggerGRPB13StringBuilderE(
  void* _M0L11_2aobj__ptrS906,
  struct _M0TPB4Show _M0L8_2aparamS905
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS904 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS906;
  _M0IP016_24default__implPB6Logger28write__string__interpolationGRPB13StringBuilderE(_M0L7_2aselfS904, _M0L8_2aparamS905);
  return 0;
}

int32_t _M0IPB13StringBuilderPB6Logger67write__char_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLogger(
  void* _M0L11_2aobj__ptrS903,
  int32_t _M0L8_2aparamS902
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS901 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS903;
  _M0IPB13StringBuilderPB6Logger11write__char(_M0L7_2aselfS901, _M0L8_2aparamS902);
  return 0;
}

int32_t _M0IPB13StringBuilderPB6Logger67write__view_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLogger(
  void* _M0L11_2aobj__ptrS900,
  struct _M0TPC16string10StringView _M0L8_2aparamS899
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS898 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS900;
  _M0IPB13StringBuilderPB6Logger11write__view(_M0L7_2aselfS898, _M0L8_2aparamS899);
  return 0;
}

int32_t _M0IP016_24default__implPB6Logger72write__substring_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLoggerGRPB13StringBuilderE(
  void* _M0L11_2aobj__ptrS897,
  moonbit_string_t _M0L8_2aparamS894,
  int32_t _M0L8_2aparamS895,
  int32_t _M0L8_2aparamS896
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS893 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS897;
  _M0IP016_24default__implPB6Logger16write__substringGRPB13StringBuilderE(_M0L7_2aselfS893, _M0L8_2aparamS894, _M0L8_2aparamS895, _M0L8_2aparamS896);
  return 0;
}

int32_t _M0IPB13StringBuilderPB6Logger69write__string_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLogger(
  void* _M0L11_2aobj__ptrS892,
  moonbit_string_t _M0L8_2aparamS891
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS890 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS892;
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L7_2aselfS890, _M0L8_2aparamS891);
  return 0;
}

void moonbit_init() {
  moonbit_layout_table = moonbit_layout_table_data;
}

int main(int argc, char** argv) {
  int32_t _M0L1nS884;
  struct _M0TP26RiantR8snn__mbt11IFParameter* _M0L5paramS885;
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS886;
  struct _M0TP26RiantR8snn__mbt2IF* _M0L6e__popS887;
  struct _M0TPB5ArrayGfE* _M0L1iS910;
  struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0L2eeS888;
  struct _M0TP26RiantR8snn__mbt2IF** _M0L6_2atmpS939;
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt2IFE* _M0L6_2atmpS932;
  struct _M0TP26RiantR8snn__mbt14SpikingSynapse** _M0L6_2atmpS938;
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt14SpikingSynapseE* _M0L6_2atmpS933;
  struct _M0TP26RiantR8snn__mbt7Monitor* _M0L6_2atmpS936;
  struct _M0TP26RiantR8snn__mbt7Monitor* _M0L6_2atmpS937;
  struct _M0TP26RiantR8snn__mbt7Monitor** _M0L6_2atmpS935;
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt7MonitorE* _M0L6_2atmpS934;
  struct _M0TP26RiantR8snn__mbt5Model* _M0L5modelS889;
  float _M0L6_2atmpS911;
  struct _M0TPB5ArrayGfE* _M0L1vS915;
  float _M0L6_2atmpS914;
  moonbit_string_t _M0L6_2atmpS913;
  moonbit_string_t _M0L6_2atmpS912;
  struct _M0TPB5ArrayGfE* _M0L1vS919;
  float _M0L6_2atmpS918;
  moonbit_string_t _M0L6_2atmpS917;
  moonbit_string_t _M0L6_2atmpS916;
  struct _M0TPB5ArrayGfE* _M0L1vS923;
  int32_t _M0L6_2acntS2059;
  float _M0L6_2atmpS922;
  moonbit_string_t _M0L6_2atmpS921;
  moonbit_string_t _M0L6_2atmpS920;
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt7MonitorE* _M0L8monitorsS929;
  struct _M0TP26RiantR8snn__mbt7Monitor* _M0L6_2atmpS928;
  struct _M0TPB5ArrayGfE* _M0L4dataS927;
  int32_t _M0L6_2acntS2076;
  int32_t _M0L6_2atmpS926;
  moonbit_string_t _M0L6_2atmpS925;
  moonbit_string_t _M0L6_2atmpS924;
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt7MonitorE* _M0L8monitorsS931;
  int32_t _M0L6_2acntS2081;
  struct _M0TP26RiantR8snn__mbt7Monitor* _M0L6_2atmpS930;
  moonbit_runtime_init(argc, argv);
  moonbit_init();
  _M0L1nS884 = 3;
  #line 30 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\chain\\main.mbt"
  _M0L5paramS885 = _M0MP26RiantR8snn__mbt11IFParameter3new();
  #line 31 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\chain\\main.mbt"
  _M0L3rngS886 = _M0MP26RiantR8snn__mbt7Xoshiro7default();
  #line 32 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\chain\\main.mbt"
  _M0L6e__popS887
  = _M0MP26RiantR8snn__mbt2IF3new(_M0L1nS884, _M0L5paramS885, _M0L3rngS886);
  moonbit_decref(_M0L5paramS885);
  moonbit_decref(_M0L3rngS886);
  _M0L1iS910 = _M0L6e__popS887->$7;
  moonbit_incref(_M0L1iS910);
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\chain\\main.mbt"
  _M0MPC15array5Array3setGfE(_M0L1iS910, 0, 0x1.ep+4f);
  moonbit_decref(_M0L1iS910);
  #line 38 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\chain\\main.mbt"
  _M0L2eeS888
  = _M0MP26RiantR8snn__mbt14SpikingSynapse3new(_M0L6e__popS887, _M0L6e__popS887, (moonbit_string_t)moonbit_string_literal_1.data);
  #line 39 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\chain\\main.mbt"
  _M0FP26RiantR8snn__mbt16spiking__connect(_M0L2eeS888, 1, 2, 0x1.9p+5f);
  #line 40 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\chain\\main.mbt"
  _M0FP26RiantR8snn__mbt16spiking__connect(_M0L2eeS888, 2, 3, 0x1.9p+5f);
  moonbit_incref(_M0L6e__popS887);
  _M0L6_2atmpS939
  = (struct _M0TP26RiantR8snn__mbt2IF**)moonbit_make_ref_array_raw(1);
  _M0L6_2atmpS939[0] = _M0L6e__popS887;
  _M0L6_2atmpS932
  = (struct _M0TPB5ArrayGRP26RiantR8snn__mbt2IFE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGRP26RiantR8snn__mbt2IFE));
  Moonbit_object_header(_M0L6_2atmpS932)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 57, 0);
  _M0L6_2atmpS932->$0 = _M0L6_2atmpS939;
  _M0L6_2atmpS932->$1 = 1;
  _M0L6_2atmpS938
  = (struct _M0TP26RiantR8snn__mbt14SpikingSynapse**)moonbit_make_ref_array_raw(1);
  _M0L6_2atmpS938[0] = _M0L2eeS888;
  _M0L6_2atmpS933
  = (struct _M0TPB5ArrayGRP26RiantR8snn__mbt14SpikingSynapseE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGRP26RiantR8snn__mbt14SpikingSynapseE));
  Moonbit_object_header(_M0L6_2atmpS933)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 60, 0);
  _M0L6_2atmpS933->$0 = _M0L6_2atmpS938;
  _M0L6_2atmpS933->$1 = 1;
  #line 47 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\chain\\main.mbt"
  _M0L6_2atmpS936 = _M0MP26RiantR8snn__mbt7Monitor6new__v(_M0L6e__popS887, 0);
  #line 48 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\chain\\main.mbt"
  _M0L6_2atmpS937 = _M0MP26RiantR8snn__mbt7Monitor6new__v(_M0L6e__popS887, 2);
  _M0L6_2atmpS935
  = (struct _M0TP26RiantR8snn__mbt7Monitor**)moonbit_make_ref_array_raw(2);
  _M0L6_2atmpS935[0] = _M0L6_2atmpS936;
  _M0L6_2atmpS935[1] = _M0L6_2atmpS937;
  _M0L6_2atmpS934
  = (struct _M0TPB5ArrayGRP26RiantR8snn__mbt7MonitorE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGRP26RiantR8snn__mbt7MonitorE));
  Moonbit_object_header(_M0L6_2atmpS934)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 63, 0);
  _M0L6_2atmpS934->$0 = _M0L6_2atmpS935;
  _M0L6_2atmpS934->$1 = 2;
  _M0L5modelS889
  = (struct _M0TP26RiantR8snn__mbt5Model*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt5Model));
  Moonbit_object_header(_M0L5modelS889)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 66, 0);
  _M0L5modelS889->$0 = _M0L6_2atmpS932;
  _M0L5modelS889->$1 = _M0L6_2atmpS933;
  _M0L5modelS889->$2 = _M0L6_2atmpS934;
  _M0L6_2atmpS911 = 0x1.9p+6f * _M0FP26RiantR8snn__mbt2ms;
  #line 53 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\chain\\main.mbt"
  _M0FP26RiantR8snn__mbt8sim__for(_M0L5modelS889, _M0L6_2atmpS911);
  #line 56 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\chain\\main.mbt"
  _M0FPB7printlnGsE((moonbit_string_t)moonbit_string_literal_28.data);
  _M0L1vS915 = _M0L6e__popS887->$3;
  moonbit_incref(_M0L1vS915);
  #line 57 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\chain\\main.mbt"
  _M0L6_2atmpS914 = _M0MPC15array5Array2atGfE(_M0L1vS915, 0);
  moonbit_decref(_M0L1vS915);
  #line 57 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\chain\\main.mbt"
  _M0L6_2atmpS913 = _M0IPC15float5FloatPB4Show10to__string(_M0L6_2atmpS914);
  #line 57 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\chain\\main.mbt"
  _M0L6_2atmpS912
  = moonbit_add_string((moonbit_string_t)moonbit_string_literal_29.data, _M0L6_2atmpS913);
  moonbit_decref(_M0L6_2atmpS913);
  #line 57 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\chain\\main.mbt"
  _M0FPB7printlnGsE(_M0L6_2atmpS912);
  moonbit_decref(_M0L6_2atmpS912);
  _M0L1vS919 = _M0L6e__popS887->$3;
  moonbit_incref(_M0L1vS919);
  #line 58 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\chain\\main.mbt"
  _M0L6_2atmpS918 = _M0MPC15array5Array2atGfE(_M0L1vS919, 1);
  moonbit_decref(_M0L1vS919);
  #line 58 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\chain\\main.mbt"
  _M0L6_2atmpS917 = _M0IPC15float5FloatPB4Show10to__string(_M0L6_2atmpS918);
  #line 58 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\chain\\main.mbt"
  _M0L6_2atmpS916
  = moonbit_add_string((moonbit_string_t)moonbit_string_literal_30.data, _M0L6_2atmpS917);
  moonbit_decref(_M0L6_2atmpS917);
  #line 58 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\chain\\main.mbt"
  _M0FPB7printlnGsE(_M0L6_2atmpS916);
  moonbit_decref(_M0L6_2atmpS916);
  _M0L1vS923 = _M0L6e__popS887->$3;
  _M0L6_2acntS2059 = Moonbit_rc_count(Moonbit_object_header(_M0L6e__popS887));
  if (_M0L6_2acntS2059 > 1) {
    int32_t _M0L11_2anew__cntS2075 = _M0L6_2acntS2059 - 1;
    Moonbit_set_rc_count(Moonbit_object_header(_M0L6e__popS887), _M0L11_2anew__cntS2075);
    moonbit_incref(_M0L1vS923);
  } else if (_M0L6_2acntS2059 == 1) {
    struct _M0TPB5ArrayGfE* _M0L8_2afieldS2074 = _M0L6e__popS887->$16;
    struct _M0TPB5ArrayGfE* _M0L8_2afieldS2073;
    struct _M0TPB5ArrayGfE* _M0L8_2afieldS2072;
    struct _M0TPB5ArrayGfE* _M0L8_2afieldS2071;
    struct _M0TPB5ArrayGfE* _M0L8_2afieldS2070;
    struct _M0TPB5ArrayGfE* _M0L8_2afieldS2069;
    struct _M0TPB5ArrayGfE* _M0L8_2afieldS2068;
    struct _M0TPB5ArrayGfE* _M0L8_2afieldS2067;
    struct _M0TPB5ArrayGfE* _M0L8_2afieldS2066;
    struct _M0TPB5ArrayGfE* _M0L8_2afieldS2065;
    struct _M0TPB5ArrayGiE* _M0L8_2afieldS2064;
    struct _M0TPB5ArrayGbE* _M0L8_2afieldS2063;
    struct _M0TPB5ArrayGfE* _M0L8_2afieldS2062;
    struct _M0TP26RiantR8snn__mbt9PostSpike* _M0L8_2afieldS2061;
    struct _M0TP26RiantR8snn__mbt11IFParameter* _M0L8_2afieldS2060;
    moonbit_decref(_M0L8_2afieldS2074);
    _M0L8_2afieldS2073 = _M0L6e__popS887->$15;
    moonbit_decref(_M0L8_2afieldS2073);
    _M0L8_2afieldS2072 = _M0L6e__popS887->$14;
    moonbit_decref(_M0L8_2afieldS2072);
    _M0L8_2afieldS2071 = _M0L6e__popS887->$13;
    moonbit_decref(_M0L8_2afieldS2071);
    _M0L8_2afieldS2070 = _M0L6e__popS887->$12;
    moonbit_decref(_M0L8_2afieldS2070);
    _M0L8_2afieldS2069 = _M0L6e__popS887->$11;
    moonbit_decref(_M0L8_2afieldS2069);
    _M0L8_2afieldS2068 = _M0L6e__popS887->$10;
    moonbit_decref(_M0L8_2afieldS2068);
    _M0L8_2afieldS2067 = _M0L6e__popS887->$9;
    moonbit_decref(_M0L8_2afieldS2067);
    _M0L8_2afieldS2066 = _M0L6e__popS887->$8;
    moonbit_decref(_M0L8_2afieldS2066);
    _M0L8_2afieldS2065 = _M0L6e__popS887->$7;
    moonbit_decref(_M0L8_2afieldS2065);
    _M0L8_2afieldS2064 = _M0L6e__popS887->$6;
    moonbit_decref(_M0L8_2afieldS2064);
    _M0L8_2afieldS2063 = _M0L6e__popS887->$5;
    moonbit_decref(_M0L8_2afieldS2063);
    _M0L8_2afieldS2062 = _M0L6e__popS887->$4;
    moonbit_decref(_M0L8_2afieldS2062);
    _M0L8_2afieldS2061 = _M0L6e__popS887->$1;
    moonbit_decref(_M0L8_2afieldS2061);
    _M0L8_2afieldS2060 = _M0L6e__popS887->$0;
    moonbit_decref(_M0L8_2afieldS2060);
    #line 43 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\chain\\main.mbt"
    moonbit_free(_M0L6e__popS887);
  }
  #line 59 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\chain\\main.mbt"
  _M0L6_2atmpS922 = _M0MPC15array5Array2atGfE(_M0L1vS923, 2);
  moonbit_decref(_M0L1vS923);
  #line 59 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\chain\\main.mbt"
  _M0L6_2atmpS921 = _M0IPC15float5FloatPB4Show10to__string(_M0L6_2atmpS922);
  #line 59 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\chain\\main.mbt"
  _M0L6_2atmpS920
  = moonbit_add_string((moonbit_string_t)moonbit_string_literal_31.data, _M0L6_2atmpS921);
  moonbit_decref(_M0L6_2atmpS921);
  #line 59 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\chain\\main.mbt"
  _M0FPB7printlnGsE(_M0L6_2atmpS920);
  moonbit_decref(_M0L6_2atmpS920);
  _M0L8monitorsS929 = _M0L5modelS889->$2;
  moonbit_incref(_M0L8monitorsS929);
  #line 60 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\chain\\main.mbt"
  _M0L6_2atmpS928
  = _M0MPC15array5Array2atGRP26RiantR8snn__mbt7MonitorE(_M0L8monitorsS929, 0);
  moonbit_decref(_M0L8monitorsS929);
  _M0L4dataS927 = _M0L6_2atmpS928->$2;
  _M0L6_2acntS2076 = Moonbit_rc_count(Moonbit_object_header(_M0L6_2atmpS928));
  if (_M0L6_2acntS2076 > 1) {
    int32_t _M0L11_2anew__cntS2080 = _M0L6_2acntS2076 - 1;
    Moonbit_set_rc_count(Moonbit_object_header(_M0L6_2atmpS928), _M0L11_2anew__cntS2080);
    moonbit_incref(_M0L4dataS927);
  } else if (_M0L6_2acntS2076 == 1) {
    struct _M0TPB5ArrayGfE* _M0L8_2afieldS2079 = _M0L6_2atmpS928->$3;
    moonbit_string_t _M0L8_2afieldS2078;
    struct _M0TP26RiantR8snn__mbt2IF* _M0L8_2afieldS2077;
    moonbit_decref(_M0L8_2afieldS2079);
    _M0L8_2afieldS2078 = _M0L6_2atmpS928->$1;
    moonbit_decref(_M0L8_2afieldS2078);
    _M0L8_2afieldS2077 = _M0L6_2atmpS928->$0;
    moonbit_decref(_M0L8_2afieldS2077);
    #line 43 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\chain\\main.mbt"
    moonbit_free(_M0L6_2atmpS928);
  }
  #line 60 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\chain\\main.mbt"
  _M0L6_2atmpS926 = _M0MPC15array5Array6lengthGfE(_M0L4dataS927);
  moonbit_decref(_M0L4dataS927);
  #line 60 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\chain\\main.mbt"
  _M0L6_2atmpS925 = _M0MPC13int3Int18to__string_2einner(_M0L6_2atmpS926, 10);
  #line 60 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\chain\\main.mbt"
  _M0L6_2atmpS924
  = moonbit_add_string((moonbit_string_t)moonbit_string_literal_32.data, _M0L6_2atmpS925);
  moonbit_decref(_M0L6_2atmpS925);
  #line 60 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\chain\\main.mbt"
  _M0FPB7printlnGsE(_M0L6_2atmpS924);
  moonbit_decref(_M0L6_2atmpS924);
  _M0L8monitorsS931 = _M0L5modelS889->$2;
  _M0L6_2acntS2081 = Moonbit_rc_count(Moonbit_object_header(_M0L5modelS889));
  if (_M0L6_2acntS2081 > 1) {
    int32_t _M0L11_2anew__cntS2084 = _M0L6_2acntS2081 - 1;
    Moonbit_set_rc_count(Moonbit_object_header(_M0L5modelS889), _M0L11_2anew__cntS2084);
    moonbit_incref(_M0L8monitorsS931);
  } else if (_M0L6_2acntS2081 == 1) {
    struct _M0TPB5ArrayGRP26RiantR8snn__mbt14SpikingSynapseE* _M0L8_2afieldS2083 =
      _M0L5modelS889->$1;
    struct _M0TPB5ArrayGRP26RiantR8snn__mbt2IFE* _M0L8_2afieldS2082;
    moonbit_decref(_M0L8_2afieldS2083);
    _M0L8_2afieldS2082 = _M0L5modelS889->$0;
    moonbit_decref(_M0L8_2afieldS2082);
    #line 43 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\chain\\main.mbt"
    moonbit_free(_M0L5modelS889);
  }
  #line 62 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\chain\\main.mbt"
  _M0L6_2atmpS930
  = _M0MPC15array5Array2atGRP26RiantR8snn__mbt7MonitorE(_M0L8monitorsS931, 0);
  moonbit_decref(_M0L8monitorsS931);
  #line 62 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\chain\\main.mbt"
  _M0MP26RiantR8snn__mbt7Monitor13dump__summary(_M0L6_2atmpS930);
  moonbit_decref(_M0L6_2atmpS930);
  return 0;
}