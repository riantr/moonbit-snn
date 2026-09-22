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

struct _M0TP26RiantR8snn__mbt15IFParameterGsyn;

struct _M0TPB4Show;

struct _M0TPB8MutLocalGfE;

struct _M0TPB13StringBuilder;

struct _M0TPB5ArrayGfE;

struct _M0TPB17FloatingDecimal64;

struct _M0TP26RiantR8snn__mbt9PostSpike;

struct _M0TUdiE;

struct _M0TPB5ArrayGbE;

struct _M0TPB5ArrayGRPB5ArrayGfEE;

struct _M0BTPB6Logger;

struct _M0BTPB4Show;

struct _M0TP26RiantR8snn__mbt7Monitor;

struct _M0TPC16string10StringView;

struct _M0TP26RiantR8snn__mbt2IF;

struct _M0KTPB6LoggerTPB13StringBuilder;

struct _M0TPB6Logger;

struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR;

struct _M0TP26RiantR8snn__mbt7Xoshiro;

struct _M0TUmmmmE;

struct _M0TPB5ArrayGiE;

struct _M0TPB19MulShiftAll64Result;

struct _M0TP26RiantR8snn__mbt11IFParameter;

struct _M0TUddE;

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

struct _M0TP26RiantR8snn__mbt15IFParameterGsyn {
  struct _M0TP26RiantR8snn__mbt11IFParameter* $0;
  float $1;
  float $2;
  
};

struct _M0TPB4Show {
  struct _M0BTPB4Show* $0;
  void* $1;
  
};

struct _M0TPB8MutLocalGfE {
  float $0;
  
};

struct _M0TPB13StringBuilder {
  uint16_t* $0;
  int32_t $1;
  
};

struct _M0TPB5ArrayGfE {
  float* $0;
  int32_t $1;
  
};

struct _M0TPB17FloatingDecimal64 {
  uint64_t $0;
  int32_t $1;
  
};

struct _M0TP26RiantR8snn__mbt9PostSpike {
  float $0;
  
};

struct _M0TUdiE {
  double $0;
  int32_t $1;
  
};

struct _M0TPB5ArrayGbE {
  uint8_t* $0;
  int32_t $1;
  
};

struct _M0TPB5ArrayGRPB5ArrayGfEE {
  struct _M0TPB5ArrayGfE** $0;
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

struct _M0TPB5ArrayGiE {
  int32_t* $0;
  int32_t $1;
  
};

struct _M0TPB19MulShiftAll64Result {
  uint64_t $0;
  uint64_t $1;
  uint64_t $2;
  
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

struct _M0TUddE {
  double $0;
  double $1;
  
};

struct _M0TPB7Umul128 {
  uint64_t $0;
  uint64_t $1;
  
};

struct _M0TPB8Pow5Pair {
  uint64_t $0;
  uint64_t $1;
  
};

struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0MP26RiantR8snn__mbt14SpikingSynapse6random(
  struct _M0TP26RiantR8snn__mbt2IF*,
  struct _M0TP26RiantR8snn__mbt2IF*,
  moonbit_string_t,
  float,
  float,
  float,
  struct _M0TP26RiantR8snn__mbt7Xoshiro*
);

struct _M0TP26RiantR8snn__mbt11IFParameter* _M0MP26RiantR8snn__mbt11IFParameter8with__el(
  float
);

struct _M0TP26RiantR8snn__mbt2IF* _M0MP26RiantR8snn__mbt2IF10with__gsyn(
  int32_t,
  struct _M0TP26RiantR8snn__mbt11IFParameter*,
  float,
  float,
  struct _M0TP26RiantR8snn__mbt7Xoshiro*
);

struct _M0TP26RiantR8snn__mbt2IF* _M0MP26RiantR8snn__mbt2IF3new(
  int32_t,
  struct _M0TP26RiantR8snn__mbt11IFParameter*,
  struct _M0TP26RiantR8snn__mbt7Xoshiro*
);

int32_t _M0MP26RiantR8snn__mbt15IFParameterGsyn5apply(
  struct _M0TP26RiantR8snn__mbt15IFParameterGsyn*,
  struct _M0TP26RiantR8snn__mbt2IF*
);

struct _M0TP26RiantR8snn__mbt9PostSpike* _M0MP26RiantR8snn__mbt9PostSpike3new(
  
);

struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0MP26RiantR8snn__mbt7Xoshiro7default(
  
);

int32_t _M0FP26RiantR8snn__mbt16forward__synapse(
  struct _M0TP26RiantR8snn__mbt14SpikingSynapse*,
  float
);

int32_t _M0FP26RiantR8snn__mbt13apply__weight(
  struct _M0TP26RiantR8snn__mbt14SpikingSynapse*,
  int32_t,
  float
);

int32_t _M0FP26RiantR8snn__mbt11record__one(
  struct _M0TP26RiantR8snn__mbt7Monitor*,
  float
);

struct _M0TP26RiantR8snn__mbt7Monitor* _M0MP26RiantR8snn__mbt7Monitor9new__fire(
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

struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0MP26RiantR8snn__mbt15SparseMatrixCSR6random(
  int32_t,
  int32_t,
  float,
  float,
  float,
  struct _M0TP26RiantR8snn__mbt7Xoshiro*
);

struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0MP26RiantR8snn__mbt15SparseMatrixCSR18random__with__rule(
  int32_t,
  int32_t,
  float,
  float,
  float,
  int32_t,
  struct _M0TP26RiantR8snn__mbt7Xoshiro*
);

struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0MP26RiantR8snn__mbt7Xoshiro3new(
  uint64_t
);

struct _M0TUmmmmE* _M0FP26RiantR8snn__mbt10splitmix64(uint64_t);

uint64_t _M0FP26RiantR8snn__mbt5mix64(uint64_t);

struct _M0TUddE* _M0FP26RiantR8snn__mbt11box__muller(
  struct _M0TP26RiantR8snn__mbt7Xoshiro*
);

double _M0FP26RiantR8snn__mbt9next__f64(
  struct _M0TP26RiantR8snn__mbt7Xoshiro*
);

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

int32_t _M0MP26RiantR8snn__mbt7Monitor13count__spikes(
  struct _M0TP26RiantR8snn__mbt7Monitor*
);

double _M0FPC14math2ln(double);

#define _M0FPC14math3cos cos

#define _M0FPC14math3sin sin

struct _M0TUdiE* _M0FPC14math5frexp(double);

struct _M0TUdiE* _M0FPC14math9normalize(double);

moonbit_string_t _M0IPC15float5FloatPB4Show10to__string(float);

int32_t _M0MPC15float5Float7to__int(float);

struct _M0TPB5ArrayGfE* _M0MPC15array5Array4makeGfE(int32_t, float);

struct _M0TPB5ArrayGbE* _M0MPC15array5Array4makeGbE(int32_t, int32_t);

struct _M0TPB5ArrayGiE* _M0MPC15array5Array4makeGiE(int32_t, int32_t);

struct _M0TPB5ArrayGRPB5ArrayGfEE* _M0MPC15array5Array4makeGRPB5ArrayGfEE(
  int32_t,
  struct _M0TPB5ArrayGfE*
);

int32_t _M0MPC15array5Array3setGfE(struct _M0TPB5ArrayGfE*, int32_t, float);

int32_t _M0MPC15array5Array3setGbE(struct _M0TPB5ArrayGbE*, int32_t, int32_t);

int32_t _M0MPC15array5Array3setGiE(struct _M0TPB5ArrayGiE*, int32_t, int32_t);

int32_t _M0MPC15array5Array3setGRPB5ArrayGfEE(
  struct _M0TPB5ArrayGRPB5ArrayGfEE*,
  int32_t,
  struct _M0TPB5ArrayGfE*
);

int32_t _M0MPC15array5Array2atGbE(struct _M0TPB5ArrayGbE*, int32_t);

int32_t _M0MPC15array5Array2atGiE(struct _M0TPB5ArrayGiE*, int32_t);

float _M0MPC15array5Array2atGfE(struct _M0TPB5ArrayGfE*, int32_t);

struct _M0TPB5ArrayGfE* _M0MPC15array5Array2atGRPB5ArrayGfEE(
  struct _M0TPB5ArrayGRPB5ArrayGfEE*,
  int32_t
);

int32_t _M0FPB7printlnGsE(moonbit_string_t);

moonbit_string_t _M0MPC16double6Double10to__string(double);

int32_t _M0MPC16double6Double7is__inf(double);

int32_t _M0MPC16double6Double7is__nan(double);

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

struct _M0TPB5ArrayGRPB5ArrayGfEE* _M0MPC15array5Array20unsafe__make__uninitGRPB5ArrayGfEE(
  int32_t
);

uint64_t _M0MPC15array13ReadOnlyArray2atGmE(uint64_t*, int32_t);

uint32_t _M0MPC15array13ReadOnlyArray2atGjE(uint32_t*, int32_t);

moonbit_string_t _M0IPC16uint646UInt64PB4Show10to__string(uint64_t);

moonbit_string_t _M0IPC13int3IntPB4Show10to__string(int32_t);

uint64_t _M0MPC14uint4UInt10to__uint64(uint32_t);

int32_t _M0MPC15array5Array4pushGfE(struct _M0TPB5ArrayGfE*, float);

int32_t _M0MPC15array5Array4pushGiE(struct _M0TPB5ArrayGiE*, int32_t);

int32_t _M0MPC15array5Array7reallocGfE(struct _M0TPB5ArrayGfE*, int32_t);

int32_t _M0MPC15array5Array7reallocGiE(struct _M0TPB5ArrayGiE*, int32_t);

int32_t _M0MPC15array5Array14resize__bufferGfE(
  struct _M0TPB5ArrayGfE*,
  int32_t
);

int32_t _M0MPC15array5Array14resize__bufferGiE(
  struct _M0TPB5ArrayGiE*,
  int32_t
);

int32_t _M0MPC15array5Array8capacityGfE(struct _M0TPB5ArrayGfE*);

int32_t _M0MPC15array5Array8capacityGiE(struct _M0TPB5ArrayGiE*);

int32_t _M0FPB23array__growth__capacity(int32_t, int32_t, int32_t);

int32_t _M0MPC15array5Array6lengthGfE(struct _M0TPB5ArrayGfE*);

int32_t _M0MPC15array5Array6lengthGbE(struct _M0TPB5ArrayGbE*);

float* _M0MPC15array5Array6bufferGfE(struct _M0TPB5ArrayGfE*);

uint8_t* _M0MPC15array5Array6bufferGbE(struct _M0TPB5ArrayGbE*);

int32_t* _M0MPC15array5Array6bufferGiE(struct _M0TPB5ArrayGiE*);

struct _M0TPB5ArrayGfE** _M0MPC15array5Array6bufferGRPB5ArrayGfEE(
  struct _M0TPB5ArrayGRPB5ArrayGfEE*
);

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

float* _M0MPB18UninitializedArray23make__and__blit_2einnerGfE(
  float*,
  int32_t,
  int32_t,
  int32_t,
  int32_t
);

int32_t* _M0MPB18UninitializedArray23make__and__blit_2einnerGiE(
  int32_t*,
  int32_t,
  int32_t,
  int32_t,
  int32_t
);

int32_t _M0MPB13StringBuilder13write__objectGiE(
  struct _M0TPB13StringBuilder*,
  int32_t
);

int32_t _M0MPB13StringBuilder13write__objectGmE(
  struct _M0TPB13StringBuilder*,
  uint64_t
);

float* _M0MPB18UninitializedArray23unsafe__make__and__blitGfE(
  float*,
  int32_t,
  int32_t,
  int32_t,
  int32_t
);

int32_t* _M0MPB18UninitializedArray23unsafe__make__and__blitGiE(
  int32_t*,
  int32_t,
  int32_t,
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

int32_t _M0MPB18UninitializedArray12unsafe__blitGiE(
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

int32_t _M0MPC15array10FixedArray12unsafe__blitGRPB17UnsafeMaybeUninitGiEE(
  int32_t*,
  int32_t,
  int32_t*,
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

int32_t _M0MPB18UninitializedArray6lengthGfE(float*);

int32_t _M0MPB18UninitializedArray6lengthGiE(int32_t*);

int32_t _M0FPC15abort5abortGuE(moonbit_string_t);

float* _M0FPC15abort5abortGRPB18UninitializedArrayGfEE(moonbit_string_t);

int32_t* _M0FPC15abort5abortGRPB18UninitializedArrayGiEE(moonbit_string_t);

int32_t _M0FPC15abort5abortGiE(moonbit_string_t);

uint16_t* _M0FPC15abort5abortGAkE(moonbit_string_t);

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

double cos(double);

double sin(double);

struct { int32_t rc; uint32_t meta; uint16_t const data[23]; 
} const moonbit_string_literal_29 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 22, 61, 61, 
    61, 32, 70, 105, 114, 105, 110, 103, 32, 114, 97, 116, 101, 115, 
    32, 111, 118, 101, 114, 32, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[1]; 
} const moonbit_string_literal_0 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 0, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[4]; 
} const moonbit_string_literal_32 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 3, 32, 72, 122, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[8]; 
} const moonbit_string_literal_30 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 7, 32, 109, 
    115, 32, 61, 61, 61, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[16]; 
} const moonbit_string_literal_17 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 15, 44, 32, 
    100, 115, 116, 95, 111, 102, 102, 115, 101, 116, 32, 61, 32, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[19]; 
} const moonbit_string_literal_13 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 18, 105, 110, 
    118, 97, 108, 105, 100, 32, 99, 111, 100, 101, 32, 112, 111, 105, 
    110, 116, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[2]; 
} const moonbit_string_literal_7 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 1, 45, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[28]; 
} const moonbit_string_literal_26 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 27, 32, 40, 
    103, 115, 121, 110, 95, 101, 61, 49, 46, 48, 52, 44, 32, 103, 115, 
    121, 110, 95, 105, 61, 48, 46, 56, 52, 41, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[29]; 
} const moonbit_string_literal_24 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 28, 32, 40, 
    103, 115, 121, 110, 95, 101, 61, 48, 46, 55, 51, 44, 32, 103, 115, 
    121, 110, 95, 105, 61, 48, 46, 50, 54, 53, 41, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[14]; 
} const moonbit_string_literal_23 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 13, 32, 32, 
    69, 32, 32, 32, 61, 32, 73, 70, 32, 120, 32, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[31]; 
} const moonbit_string_literal_11 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 30, 114, 97, 
    100, 105, 120, 32, 109, 117, 115, 116, 32, 98, 101, 32, 98, 101, 
    116, 119, 101, 101, 110, 32, 50, 32, 97, 110, 100, 32, 51, 54, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[9]; 
} const moonbit_string_literal_8 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 8, 73, 110, 
    102, 105, 110, 105, 116, 121, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[4]; 
} const moonbit_string_literal_6 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 3, 78, 97, 78, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[14]; 
} const moonbit_string_literal_25 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 13, 32, 32, 
    80, 86, 32, 32, 61, 32, 73, 70, 32, 120, 32, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[2]; 
} const moonbit_string_literal_4 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 1, 48, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[73]; 
} const moonbit_string_literal_21 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 72, 100, 117, 
    97, 114, 116, 101, 50, 48, 49, 57, 46, 109, 98, 116, 58, 32, 69, 
    45, 73, 32, 110, 101, 116, 119, 111, 114, 107, 32, 119, 105, 116, 
    104, 32, 80, 86, 32, 43, 32, 83, 83, 84, 32, 105, 110, 116, 101, 
    114, 110, 101, 117, 114, 111, 110, 115, 32, 40, 73, 70, 80, 97, 114, 
    97, 109, 101, 116, 101, 114, 71, 115, 121, 110, 41, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[9]; 
} const moonbit_string_literal_18 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 8, 44, 32, 
    108, 101, 110, 32, 61, 32, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[37]; 
} const moonbit_string_literal_15 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 36, 98, 111, 
    117, 110, 100, 115, 32, 99, 104, 101, 99, 107, 32, 102, 97, 105, 
    108, 101, 100, 58, 32, 97, 108, 108, 111, 99, 97, 116, 101, 95, 108, 
    101, 110, 32, 61, 32, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[9]; 
} const moonbit_string_literal_33 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 8, 32, 32, 
    80, 86, 32, 32, 61, 32, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[14]; 
} const moonbit_string_literal_27 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 13, 32, 32, 
    83, 83, 84, 32, 61, 32, 73, 70, 32, 120, 32, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[3]; 
} const moonbit_string_literal_1 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 2, 103, 101, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[20]; 
} const moonbit_string_literal_22 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 19, 61, 61, 
    61, 32, 80, 111, 112, 117, 108, 97, 116, 105, 111, 110, 115, 32, 
    61, 61, 61, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[3]; 
} const moonbit_string_literal_20 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 2, 103, 105, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[37]; 
} const moonbit_string_literal_12 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 36, 48, 49, 
    50, 51, 52, 53, 54, 55, 56, 57, 97, 98, 99, 100, 101, 102, 103, 104, 
    105, 106, 107, 108, 109, 110, 111, 112, 113, 114, 115, 116, 117, 
    118, 119, 120, 121, 122, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[5]; 
} const moonbit_string_literal_3 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 4, 102, 105, 
    114, 101, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[16]; 
} const moonbit_string_literal_19 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 15, 44, 32, 
    115, 114, 99, 46, 108, 101, 110, 103, 116, 104, 32, 61, 32, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[28]; 
} const moonbit_string_literal_28 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 27, 32, 40, 
    103, 115, 121, 110, 95, 101, 61, 48, 46, 53, 54, 44, 32, 103, 115, 
    121, 110, 95, 105, 61, 48, 46, 53, 57, 41, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[9]; 
} const moonbit_string_literal_34 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 8, 32, 32, 
    83, 83, 84, 32, 61, 32, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[16]; 
} const moonbit_string_literal_16 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 15, 44, 32, 
    115, 114, 99, 95, 111, 102, 102, 115, 101, 116, 32, 61, 32, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[2]; 
} const moonbit_string_literal_2 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 1, 118, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[9]; 
} const moonbit_string_literal_31 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 8, 32, 32, 
    69, 32, 32, 32, 61, 32, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[24]; 
} const moonbit_string_literal_10 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 23, 65, 114, 
    114, 97, 121, 32, 99, 97, 112, 97, 99, 105, 116, 121, 32, 111, 118, 
    101, 114, 102, 108, 111, 119, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[26]; 
} const moonbit_string_literal_5 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 25, 73, 108, 
    108, 101, 103, 97, 108, 65, 114, 103, 117, 109, 101, 110, 116, 69, 
    120, 99, 101, 112, 116, 105, 111, 110, 32, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[32]; 
} const moonbit_string_literal_14 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 31, 83, 116, 
    114, 105, 110, 103, 66, 117, 105, 108, 100, 101, 114, 32, 99, 97, 
    112, 97, 99, 105, 116, 121, 32, 111, 118, 101, 114, 102, 108, 111, 
    119, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[4]; 
} const moonbit_string_literal_9 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 3, 48, 46, 48, 0};

uint32_t const moonbit_layout_table_data[59] =
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
    sizeof(struct _M0TP26RiantR8snn__mbt15IFParameterGsyn) / 4, 1,
    offsetof(struct _M0TP26RiantR8snn__mbt15IFParameterGsyn, $0) / 4 * 2,
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
    sizeof(struct _M0TPB5ArrayGbE) / 4, 1,
    offsetof(struct _M0TPB5ArrayGbE, $0) / 4 * 2,
    sizeof(struct _M0TPB5ArrayGRPB5ArrayGfEE) / 4, 1,
    offsetof(struct _M0TPB5ArrayGRPB5ArrayGfEE, $0) / 4 * 2,
    sizeof(struct _M0TPB13StringBuilder) / 4, 1,
    offsetof(struct _M0TPB13StringBuilder, $0) / 4 * 2
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

double _M0FPB18double__max__value;

double _M0FPB18double__min__value;

double _M0FPC16double14not__a__number;

double _M0FPC16double13neg__infinity;

double _M0FPC16double13min__positive;

struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0MP26RiantR8snn__mbt14SpikingSynapse6random(
  struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS925,
  struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS926,
  moonbit_string_t _M0L3symS931,
  float _M0L2muS927,
  float _M0L5sigmaS928,
  float _M0L1pS929,
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS930
) {
  int32_t _M0L1nS2102;
  int32_t _M0L1nS2103;
  struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS924;
  float* _M0L6_2atmpS2101;
  struct _M0TPB5ArrayGfE* _M0L6_2atmpS2092;
  float* _M0L6_2atmpS2100;
  struct _M0TPB5ArrayGfE* _M0L6_2atmpS2093;
  float* _M0L6_2atmpS2099;
  struct _M0TPB5ArrayGfE* _M0L6_2atmpS2094;
  int32_t* _M0L6_2atmpS2098;
  struct _M0TPB5ArrayGiE* _M0L6_2atmpS2095;
  float* _M0L6_2atmpS2097;
  struct _M0TPB5ArrayGfE* _M0L6_2atmpS2096;
  struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _block_2117;
  #line 131 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
  _M0L1nS2102 = _M0L3preS925->$2;
  _M0L1nS2103 = _M0L4postS926->$2;
  #line 140 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
  _M0L6matrixS924
  = _M0MP26RiantR8snn__mbt15SparseMatrixCSR6random(_M0L1nS2102, _M0L1nS2103, _M0L2muS927, _M0L5sigmaS928, _M0L1pS929, _M0L3rngS930);
  _M0L6_2atmpS2101 = moonbit_empty_float_array;
  _M0L6_2atmpS2092
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_M0L6_2atmpS2092)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 0, 0);
  _M0L6_2atmpS2092->$0 = _M0L6_2atmpS2101;
  _M0L6_2atmpS2092->$1 = 0;
  _M0L6_2atmpS2100 = moonbit_empty_float_array;
  _M0L6_2atmpS2093
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_M0L6_2atmpS2093)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 0, 0);
  _M0L6_2atmpS2093->$0 = _M0L6_2atmpS2100;
  _M0L6_2atmpS2093->$1 = 0;
  _M0L6_2atmpS2099 = moonbit_empty_float_array;
  _M0L6_2atmpS2094
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_M0L6_2atmpS2094)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 0, 0);
  _M0L6_2atmpS2094->$0 = _M0L6_2atmpS2099;
  _M0L6_2atmpS2094->$1 = 0;
  _M0L6_2atmpS2098 = (int32_t*)moonbit_empty_int32_array;
  _M0L6_2atmpS2095
  = (struct _M0TPB5ArrayGiE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGiE));
  Moonbit_object_header(_M0L6_2atmpS2095)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 3, 0);
  _M0L6_2atmpS2095->$0 = _M0L6_2atmpS2098;
  _M0L6_2atmpS2095->$1 = 0;
  _M0L6_2atmpS2097 = moonbit_empty_float_array;
  _M0L6_2atmpS2096
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_M0L6_2atmpS2096)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 0, 0);
  _M0L6_2atmpS2096->$0 = _M0L6_2atmpS2097;
  _M0L6_2atmpS2096->$1 = 0;
  moonbit_incref(_M0L3preS925);
  moonbit_incref(_M0L4postS926);
  moonbit_incref(_M0L3symS931);
  _block_2117
  = (struct _M0TP26RiantR8snn__mbt14SpikingSynapse*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt14SpikingSynapse));
  Moonbit_object_header(_block_2117)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 6, 0);
  _block_2117->$0 = _M0L3preS925;
  _block_2117->$1 = _M0L4postS926;
  _block_2117->$2 = _M0L3symS931;
  _block_2117->$3 = (moonbit_string_t)moonbit_string_literal_0.data;
  _block_2117->$4 = _M0L6matrixS924;
  _block_2117->$5 = _M0L6_2atmpS2092;
  _block_2117->$6 = _M0L6_2atmpS2093;
  _block_2117->$7 = _M0L6_2atmpS2094;
  _block_2117->$8 = _M0L6_2atmpS2095;
  _block_2117->$9 = _M0L6_2atmpS2096;
  return _block_2117;
}

struct _M0TP26RiantR8snn__mbt11IFParameter* _M0MP26RiantR8snn__mbt11IFParameter8with__el(
  float _M0L2elS923
) {
  float _M0L1cS921;
  float _M0L2glS922;
  struct _M0TP26RiantR8snn__mbt11IFParameter* _block_2118;
  #line 49 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L1cS921 = -0x1p+0f;
  _M0L2glS922 = -0x1p+0f;
  _block_2118
  = (struct _M0TP26RiantR8snn__mbt11IFParameter*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt11IFParameter));
  Moonbit_object_header(_block_2118)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_2118->$0 = _M0L1cS921;
  _block_2118->$1 = _M0L2glS922;
  _block_2118->$2 = 0x1.ep+3f;
  _block_2118->$3 = -0x1.9p+5f;
  _block_2118->$4 = -0x1.ep+5f;
  _block_2118->$5 = _M0L2elS923;
  _block_2118->$6 = 0x1.eb851eb851eb8p-5f;
  _block_2118->$7 = 0x1p+1f;
  _block_2118->$8 = 0x0p+0f;
  _block_2118->$9 = 0x0p+0f;
  _block_2118->$10 = 0x0p+0f;
  return _block_2118;
}

struct _M0TP26RiantR8snn__mbt2IF* _M0MP26RiantR8snn__mbt2IF10with__gsyn(
  int32_t _M0L1nS915,
  struct _M0TP26RiantR8snn__mbt11IFParameter* _M0L4baseS916,
  float _M0L7gsyn__eS919,
  float _M0L7gsyn__iS920,
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS917
) {
  struct _M0TP26RiantR8snn__mbt2IF* _M0L1pS914;
  struct _M0TP26RiantR8snn__mbt15IFParameterGsyn* _M0L2gsS918;
  #line 81 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if_gsyn.mbt"
  #line 88 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if_gsyn.mbt"
  _M0L1pS914
  = _M0MP26RiantR8snn__mbt2IF3new(_M0L1nS915, _M0L4baseS916, _M0L3rngS917);
  moonbit_incref(_M0L4baseS916);
  _M0L2gsS918
  = (struct _M0TP26RiantR8snn__mbt15IFParameterGsyn*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt15IFParameterGsyn));
  Moonbit_object_header(_M0L2gsS918)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 18, 0);
  _M0L2gsS918->$0 = _M0L4baseS916;
  _M0L2gsS918->$1 = _M0L7gsyn__eS919;
  _M0L2gsS918->$2 = _M0L7gsyn__iS920;
  #line 90 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if_gsyn.mbt"
  _M0MP26RiantR8snn__mbt15IFParameterGsyn5apply(_M0L2gsS918, _M0L1pS914);
  moonbit_decref(_M0L2gsS918);
  return _M0L1pS914;
}

struct _M0TP26RiantR8snn__mbt2IF* _M0MP26RiantR8snn__mbt2IF3new(
  int32_t _M0L1nS888,
  struct _M0TP26RiantR8snn__mbt11IFParameter* _M0L5paramS890,
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS893
) {
  struct _M0TPB5ArrayGfE* _M0L1vS887;
  float _M0L2vtS2090;
  float _M0L2vrS2091;
  float _M0L6spreadS889;
  int32_t _M0L7_2abindS891;
  int32_t _M0L1kS892;
  struct _M0TPB5ArrayGfE* _M0L1wS895;
  struct _M0TPB5ArrayGbE* _M0L4fireS896;
  struct _M0TPB5ArrayGiE* _M0L4tabsS897;
  struct _M0TPB5ArrayGfE* _M0L1iS898;
  struct _M0TPB5ArrayGfE* _M0L9syn__currS899;
  struct _M0TPB5ArrayGfE* _M0L2geS900;
  struct _M0TPB5ArrayGfE* _M0L2giS901;
  struct _M0TPB5ArrayGfE* _M0L2heS902;
  struct _M0TPB5ArrayGfE* _M0L2hiS903;
  struct _M0TPB5ArrayGfE* _M0L3gluS904;
  struct _M0TPB5ArrayGfE* _M0L4gabaS905;
  struct _M0TPB5ArrayGfE* _M0L7gsyn__eS906;
  struct _M0TPB5ArrayGfE* _M0L7gsyn__iS907;
  float _M0L4e__eS908;
  float _M0L4e__iS909;
  float _M0L3treS910;
  float _M0L3tdeS911;
  float _M0L3triS912;
  float _M0L3tdiS913;
  struct _M0TP26RiantR8snn__mbt9PostSpike* _M0L6_2atmpS2089;
  struct _M0TP26RiantR8snn__mbt2IF* _block_2120;
  #line 113 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  #line 114 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L1vS887 = _M0MPC15array5Array4makeGfE(_M0L1nS888, 0x0p+0f);
  _M0L2vtS2090 = _M0L5paramS890->$3;
  _M0L2vrS2091 = _M0L5paramS890->$4;
  _M0L6spreadS889 = _M0L2vtS2090 - _M0L2vrS2091;
  _M0L7_2abindS891 = 0;
  _M0L1kS892 = _M0L7_2abindS891;
  while (1) {
    if (_M0L1kS892 < _M0L1nS888) {
      float _M0L2vrS2085 = _M0L5paramS890->$4;
      float _M0L6_2atmpS2087;
      float _M0L6_2atmpS2086;
      float _M0L6_2atmpS2084;
      int32_t _M0L6_2atmpS2088;
      #line 117 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2087 = _M0FP26RiantR8snn__mbt9next__f32(_M0L3rngS893);
      _M0L6_2atmpS2086 = _M0L6_2atmpS2087 * _M0L6spreadS889;
      _M0L6_2atmpS2084 = _M0L2vrS2085 + _M0L6_2atmpS2086;
      #line 117 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS887, _M0L1kS892, _M0L6_2atmpS2084);
      _M0L6_2atmpS2088 = _M0L1kS892 + 1;
      _M0L1kS892 = _M0L6_2atmpS2088;
      continue;
    }
    break;
  }
  #line 119 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L1wS895 = _M0MPC15array5Array4makeGfE(_M0L1nS888, 0x0p+0f);
  #line 120 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L4fireS896 = _M0MPC15array5Array4makeGbE(_M0L1nS888, 0);
  #line 121 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L4tabsS897 = _M0MPC15array5Array4makeGiE(_M0L1nS888, 0);
  #line 122 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L1iS898 = _M0MPC15array5Array4makeGfE(_M0L1nS888, 0x0p+0f);
  #line 123 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L9syn__currS899 = _M0MPC15array5Array4makeGfE(_M0L1nS888, 0x0p+0f);
  #line 124 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L2geS900 = _M0MPC15array5Array4makeGfE(_M0L1nS888, 0x0p+0f);
  #line 125 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L2giS901 = _M0MPC15array5Array4makeGfE(_M0L1nS888, 0x0p+0f);
  #line 126 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L2heS902 = _M0MPC15array5Array4makeGfE(_M0L1nS888, 0x0p+0f);
  #line 127 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L2hiS903 = _M0MPC15array5Array4makeGfE(_M0L1nS888, 0x0p+0f);
  #line 128 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L3gluS904 = _M0MPC15array5Array4makeGfE(_M0L1nS888, 0x0p+0f);
  #line 129 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L4gabaS905 = _M0MPC15array5Array4makeGfE(_M0L1nS888, 0x0p+0f);
  #line 130 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L7gsyn__eS906 = _M0MPC15array5Array4makeGfE(_M0L1nS888, 0x1p+0f);
  #line 131 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L7gsyn__iS907 = _M0MPC15array5Array4makeGfE(_M0L1nS888, 0x1p+0f);
  _M0L4e__eS908 = 0x0p+0f;
  _M0L4e__iS909 = -0x1.2cp+6f;
  _M0L3treS910 = 0x1p+0f;
  _M0L3tdeS911 = 0x1.8p+2f;
  _M0L3triS912 = 0x1p-1f;
  _M0L3tdiS913 = 0x1p+1f;
  #line 138 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L6_2atmpS2089 = _M0MP26RiantR8snn__mbt9PostSpike3new();
  moonbit_incref(_M0L5paramS890);
  _block_2120
  = (struct _M0TP26RiantR8snn__mbt2IF*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt2IF));
  Moonbit_object_header(_block_2120)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 21, 0);
  _block_2120->$0 = _M0L5paramS890;
  _block_2120->$1 = _M0L6_2atmpS2089;
  _block_2120->$2 = _M0L1nS888;
  _block_2120->$3 = _M0L1vS887;
  _block_2120->$4 = _M0L1wS895;
  _block_2120->$5 = _M0L4fireS896;
  _block_2120->$6 = _M0L4tabsS897;
  _block_2120->$7 = _M0L1iS898;
  _block_2120->$8 = _M0L9syn__currS899;
  _block_2120->$9 = _M0L2geS900;
  _block_2120->$10 = _M0L2giS901;
  _block_2120->$11 = _M0L2heS902;
  _block_2120->$12 = _M0L2hiS903;
  _block_2120->$13 = _M0L3gluS904;
  _block_2120->$14 = _M0L4gabaS905;
  _block_2120->$15 = _M0L7gsyn__eS906;
  _block_2120->$16 = _M0L7gsyn__iS907;
  _block_2120->$17 = _M0L4e__eS908;
  _block_2120->$18 = _M0L4e__iS909;
  _block_2120->$19 = _M0L3treS910;
  _block_2120->$20 = _M0L3tdeS911;
  _block_2120->$21 = _M0L3triS912;
  _block_2120->$22 = _M0L3tdiS913;
  return _block_2120;
}

int32_t _M0MP26RiantR8snn__mbt15IFParameterGsyn5apply(
  struct _M0TP26RiantR8snn__mbt15IFParameterGsyn* _M0L1sS885,
  struct _M0TP26RiantR8snn__mbt2IF* _M0L1pS883
) {
  int32_t _M0L1nS882;
  struct _M0TPB8MutLocalGiE* _M0L1iS884;
  #line 69 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if_gsyn.mbt"
  _M0L1nS882 = _M0L1pS883->$2;
  _M0L1iS884
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1iS884)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1iS884->$0 = 0;
  while (1) {
    int32_t _M0L3valS2075 = _M0L1iS884->$0;
    if (_M0L3valS2075 < _M0L1nS882) {
      struct _M0TPB5ArrayGfE* _M0L7gsyn__eS2076 = _M0L1pS883->$15;
      int32_t _M0L3valS2077 = _M0L1iS884->$0;
      float _M0L7gsyn__eS2078 = _M0L1sS885->$1;
      struct _M0TPB5ArrayGfE* _M0L7gsyn__iS2079;
      int32_t _M0L3valS2080;
      float _M0L7gsyn__iS2081;
      int32_t _M0L3valS2083;
      int32_t _M0L6_2atmpS2082;
      #line 73 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if_gsyn.mbt"
      _M0MPC15array5Array3setGfE(_M0L7gsyn__eS2076, _M0L3valS2077, _M0L7gsyn__eS2078);
      _M0L7gsyn__iS2079 = _M0L1pS883->$16;
      _M0L3valS2080 = _M0L1iS884->$0;
      _M0L7gsyn__iS2081 = _M0L1sS885->$2;
      #line 74 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if_gsyn.mbt"
      _M0MPC15array5Array3setGfE(_M0L7gsyn__iS2079, _M0L3valS2080, _M0L7gsyn__iS2081);
      _M0L3valS2083 = _M0L1iS884->$0;
      _M0L6_2atmpS2082 = _M0L3valS2083 + 1;
      _M0L1iS884->$0 = _M0L6_2atmpS2082;
      continue;
    } else {
      moonbit_decref(_M0L1iS884);
    }
    break;
  }
  return 0;
}

struct _M0TP26RiantR8snn__mbt9PostSpike* _M0MP26RiantR8snn__mbt9PostSpike3new(
  
) {
  struct _M0TP26RiantR8snn__mbt9PostSpike* _block_2122;
  #line 75 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _block_2122
  = (struct _M0TP26RiantR8snn__mbt9PostSpike*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt9PostSpike));
  Moonbit_object_header(_block_2122)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_2122->$0 = 0x1p+1f;
  return _block_2122;
}

struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0MP26RiantR8snn__mbt7Xoshiro7default(
  
) {
  #line 56 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  #line 57 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  return _M0MP26RiantR8snn__mbt7Xoshiro3new(0ull);
}

int32_t _M0FP26RiantR8snn__mbt16forward__synapse(
  struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0L1cS858,
  float _M0L6t__nowS869
) {
  struct _M0TPB5ArrayGfE* _M0L6delaysS2074;
  int32_t _M0L6_2atmpS2073;
  int32_t _M0L10use__delayS857;
  struct _M0TPB5ArrayGfE* _M0L3rhoS2072;
  int32_t _M0L6_2atmpS2071;
  int32_t _M0L8use__rhoS859;
  #line 246 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
  _M0L6delaysS2074 = _M0L1cS858->$5;
  #line 247 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
  _M0L6_2atmpS2073 = _M0MPC15array5Array6lengthGfE(_M0L6delaysS2074);
  _M0L10use__delayS857 = _M0L6_2atmpS2073 > 0;
  _M0L3rhoS2072 = _M0L1cS858->$6;
  #line 248 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
  _M0L6_2atmpS2071 = _M0MPC15array5Array6lengthGfE(_M0L3rhoS2072);
  _M0L8use__rhoS859 = _M0L6_2atmpS2071 > 0;
  if (_M0L10use__delayS857) {
    struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS2034 = _M0L1cS858->$0;
    struct _M0TPB5ArrayGbE* _M0L4fireS2033 = _M0L3preS2034->$5;
    int32_t _M0L6n__preS860;
    struct _M0TPB8MutLocalGiE* _M0L1jS861;
    #line 251 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
    _M0L6n__preS860 = _M0MPC15array5Array6lengthGbE(_M0L4fireS2033);
    _M0L1jS861
    = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
    Moonbit_object_header(_M0L1jS861)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
    _M0L1jS861->$0 = 0;
    while (1) {
      int32_t _M0L3valS2002 = _M0L1jS861->$0;
      if (_M0L3valS2002 < _M0L6n__preS860) {
        struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS2005 = _M0L1cS858->$0;
        struct _M0TPB5ArrayGbE* _M0L4fireS2003 = _M0L3preS2005->$5;
        int32_t _M0L3valS2004 = _M0L1jS861->$0;
        int32_t _M0L3valS2032;
        int32_t _M0L6_2atmpS2031;
        #line 254 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
        if (_M0MPC15array5Array2atGbE(_M0L4fireS2003, _M0L3valS2004)) {
          struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS2030 =
            _M0L1cS858->$4;
          struct _M0TPB5ArrayGiE* _M0L6rowptrS2028 = _M0L6matrixS2030->$2;
          int32_t _M0L3valS2029 = _M0L1jS861->$0;
          int32_t _M0L5startS862;
          struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS2027;
          struct _M0TPB5ArrayGiE* _M0L6rowptrS2024;
          int32_t _M0L3valS2026;
          int32_t _M0L6_2atmpS2025;
          int32_t _M0L3endS863;
          struct _M0TPB8MutLocalGiE* _M0L1sS864;
          #line 255 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
          _M0L5startS862
          = _M0MPC15array5Array2atGiE(_M0L6rowptrS2028, _M0L3valS2029);
          _M0L6matrixS2027 = _M0L1cS858->$4;
          _M0L6rowptrS2024 = _M0L6matrixS2027->$2;
          _M0L3valS2026 = _M0L1jS861->$0;
          _M0L6_2atmpS2025 = _M0L3valS2026 + 1;
          #line 256 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
          _M0L3endS863
          = _M0MPC15array5Array2atGiE(_M0L6rowptrS2024, _M0L6_2atmpS2025);
          _M0L1sS864
          = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
          Moonbit_object_header(_M0L1sS864)->meta
          = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
          _M0L1sS864->$0 = _M0L5startS862;
          while (1) {
            int32_t _M0L3valS2006 = _M0L1sS864->$0;
            if (_M0L3valS2006 < _M0L3endS863) {
              struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS2023 =
                _M0L1cS858->$4;
              struct _M0TPB5ArrayGiE* _M0L6colptrS2021 = _M0L6matrixS2023->$3;
              int32_t _M0L3valS2022 = _M0L1sS864->$0;
              int32_t _M0L9post__idxS865;
              struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS2020;
              struct _M0TPB5ArrayGfE* _M0L4valsS2018;
              int32_t _M0L3valS2019;
              float _M0L1wS866;
              struct _M0TPB5ArrayGfE* _M0L6delaysS2016;
              int32_t _M0L3valS2017;
              float _M0L1dS867;
              float _M0L9w__scaledS868;
              int32_t _M0L3valS2012;
              int32_t _M0L6_2atmpS2011;
              #line 259 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
              _M0L9post__idxS865
              = _M0MPC15array5Array2atGiE(_M0L6colptrS2021, _M0L3valS2022);
              _M0L6matrixS2020 = _M0L1cS858->$4;
              _M0L4valsS2018 = _M0L6matrixS2020->$4;
              _M0L3valS2019 = _M0L1sS864->$0;
              #line 260 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
              _M0L1wS866
              = _M0MPC15array5Array2atGfE(_M0L4valsS2018, _M0L3valS2019);
              _M0L6delaysS2016 = _M0L1cS858->$5;
              _M0L3valS2017 = _M0L1sS864->$0;
              #line 261 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
              _M0L1dS867
              = _M0MPC15array5Array2atGfE(_M0L6delaysS2016, _M0L3valS2017);
              if (_M0L8use__rhoS859) {
                struct _M0TPB5ArrayGfE* _M0L3rhoS2014 = _M0L1cS858->$6;
                int32_t _M0L3valS2015 = _M0L1sS864->$0;
                float _M0L6_2atmpS2013;
                #line 263 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
                _M0L6_2atmpS2013
                = _M0MPC15array5Array2atGfE(_M0L3rhoS2014, _M0L3valS2015);
                _M0L9w__scaledS868 = _M0L1wS866 * _M0L6_2atmpS2013;
              } else {
                _M0L9w__scaledS868 = _M0L1wS866;
              }
              if (_M0L1dS867 == 0x0p+0f) {
                #line 266 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
                _M0FP26RiantR8snn__mbt13apply__weight(_M0L1cS858, _M0L9post__idxS865, _M0L9w__scaledS868);
              } else {
                struct _M0TPB5ArrayGfE* _M0L14pending__timesS2007 =
                  _M0L1cS858->$7;
                float _M0L6_2atmpS2008 = _M0L6t__nowS869 + _M0L1dS867;
                struct _M0TPB5ArrayGiE* _M0L14pending__postsS2009;
                struct _M0TPB5ArrayGfE* _M0L16pending__weightsS2010;
                #line 269 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
                _M0MPC15array5Array4pushGfE(_M0L14pending__timesS2007, _M0L6_2atmpS2008);
                _M0L14pending__postsS2009 = _M0L1cS858->$8;
                #line 270 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
                _M0MPC15array5Array4pushGiE(_M0L14pending__postsS2009, _M0L9post__idxS865);
                _M0L16pending__weightsS2010 = _M0L1cS858->$9;
                #line 271 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
                _M0MPC15array5Array4pushGfE(_M0L16pending__weightsS2010, _M0L9w__scaledS868);
              }
              _M0L3valS2012 = _M0L1sS864->$0;
              _M0L6_2atmpS2011 = _M0L3valS2012 + 1;
              _M0L1sS864->$0 = _M0L6_2atmpS2011;
              continue;
            } else {
              moonbit_decref(_M0L1sS864);
            }
            break;
          }
        }
        _M0L3valS2032 = _M0L1jS861->$0;
        _M0L6_2atmpS2031 = _M0L3valS2032 + 1;
        _M0L1jS861->$0 = _M0L6_2atmpS2031;
        continue;
      } else {
        moonbit_decref(_M0L1jS861);
      }
      break;
    }
  } else {
    moonbit_string_t _M0L3symS2068 = _M0L1cS858->$2;
    struct _M0TPB5ArrayGfE* _M0L6targetS872;
    #line 280 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
    if (
      _M0L3symS2068 == (moonbit_string_t)moonbit_string_literal_1.data
      || Moonbit_array_length(_M0L3symS2068)
         == Moonbit_array_length((moonbit_string_t)moonbit_string_literal_1.data)
         && 0
            == memcmp(_M0L3symS2068, (moonbit_string_t)moonbit_string_literal_1.data, Moonbit_array_length(_M0L3symS2068) * 2)
    ) {
      struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS2069 = _M0L1cS858->$1;
      struct _M0TPB5ArrayGfE* _M0L8_2afieldS2104 = _M0L4postS2069->$13;
      moonbit_incref(_M0L8_2afieldS2104);
      _M0L6targetS872 = _M0L8_2afieldS2104;
    } else {
      struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS2070 = _M0L1cS858->$1;
      struct _M0TPB5ArrayGfE* _M0L8_2afieldS2105 = _M0L4postS2070->$14;
      moonbit_incref(_M0L8_2afieldS2105);
      _M0L6targetS872 = _M0L8_2afieldS2105;
    }
    if (_M0L8use__rhoS859) {
      struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS2064 = _M0L1cS858->$0;
      struct _M0TPB5ArrayGbE* _M0L4fireS2063 = _M0L3preS2064->$5;
      int32_t _M0L6n__preS873;
      struct _M0TPB8MutLocalGiE* _M0L1jS874;
      #line 283 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
      _M0L6n__preS873 = _M0MPC15array5Array6lengthGbE(_M0L4fireS2063);
      _M0L1jS874
      = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
      Moonbit_object_header(_M0L1jS874)->meta
      = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
      _M0L1jS874->$0 = 0;
      while (1) {
        int32_t _M0L3valS2035 = _M0L1jS874->$0;
        if (_M0L3valS2035 < _M0L6n__preS873) {
          struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS2038 = _M0L1cS858->$0;
          struct _M0TPB5ArrayGbE* _M0L4fireS2036 = _M0L3preS2038->$5;
          int32_t _M0L3valS2037 = _M0L1jS874->$0;
          int32_t _M0L3valS2062;
          int32_t _M0L6_2atmpS2061;
          #line 286 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
          if (_M0MPC15array5Array2atGbE(_M0L4fireS2036, _M0L3valS2037)) {
            struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS2060 =
              _M0L1cS858->$4;
            struct _M0TPB5ArrayGiE* _M0L6rowptrS2058 = _M0L6matrixS2060->$2;
            int32_t _M0L3valS2059 = _M0L1jS874->$0;
            int32_t _M0L5startS875;
            struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS2057;
            struct _M0TPB5ArrayGiE* _M0L6rowptrS2054;
            int32_t _M0L3valS2056;
            int32_t _M0L6_2atmpS2055;
            int32_t _M0L3endS876;
            struct _M0TPB8MutLocalGiE* _M0L1sS877;
            #line 287 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
            _M0L5startS875
            = _M0MPC15array5Array2atGiE(_M0L6rowptrS2058, _M0L3valS2059);
            _M0L6matrixS2057 = _M0L1cS858->$4;
            _M0L6rowptrS2054 = _M0L6matrixS2057->$2;
            _M0L3valS2056 = _M0L1jS874->$0;
            _M0L6_2atmpS2055 = _M0L3valS2056 + 1;
            #line 288 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
            _M0L3endS876
            = _M0MPC15array5Array2atGiE(_M0L6rowptrS2054, _M0L6_2atmpS2055);
            _M0L1sS877
            = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
            Moonbit_object_header(_M0L1sS877)->meta
            = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
            _M0L1sS877->$0 = _M0L5startS875;
            while (1) {
              int32_t _M0L3valS2039 = _M0L1sS877->$0;
              if (_M0L3valS2039 < _M0L3endS876) {
                struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS2053 =
                  _M0L1cS858->$4;
                struct _M0TPB5ArrayGiE* _M0L6colptrS2051 =
                  _M0L6matrixS2053->$3;
                int32_t _M0L3valS2052 = _M0L1sS877->$0;
                int32_t _M0L9post__idxS878;
                struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS2050;
                struct _M0TPB5ArrayGfE* _M0L4valsS2048;
                int32_t _M0L3valS2049;
                float _M0L6_2atmpS2044;
                struct _M0TPB5ArrayGfE* _M0L3rhoS2046;
                int32_t _M0L3valS2047;
                float _M0L6_2atmpS2045;
                float _M0L9w__scaledS879;
                float _M0L6_2atmpS2041;
                float _M0L6_2atmpS2040;
                int32_t _M0L3valS2043;
                int32_t _M0L6_2atmpS2042;
                #line 291 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
                _M0L9post__idxS878
                = _M0MPC15array5Array2atGiE(_M0L6colptrS2051, _M0L3valS2052);
                _M0L6matrixS2050 = _M0L1cS858->$4;
                _M0L4valsS2048 = _M0L6matrixS2050->$4;
                _M0L3valS2049 = _M0L1sS877->$0;
                #line 292 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
                _M0L6_2atmpS2044
                = _M0MPC15array5Array2atGfE(_M0L4valsS2048, _M0L3valS2049);
                _M0L3rhoS2046 = _M0L1cS858->$6;
                _M0L3valS2047 = _M0L1sS877->$0;
                #line 292 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
                _M0L6_2atmpS2045
                = _M0MPC15array5Array2atGfE(_M0L3rhoS2046, _M0L3valS2047);
                _M0L9w__scaledS879 = _M0L6_2atmpS2044 * _M0L6_2atmpS2045;
                #line 293 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
                _M0L6_2atmpS2041
                = _M0MPC15array5Array2atGfE(_M0L6targetS872, _M0L9post__idxS878);
                _M0L6_2atmpS2040 = _M0L6_2atmpS2041 + _M0L9w__scaledS879;
                #line 293 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
                _M0MPC15array5Array3setGfE(_M0L6targetS872, _M0L9post__idxS878, _M0L6_2atmpS2040);
                _M0L3valS2043 = _M0L1sS877->$0;
                _M0L6_2atmpS2042 = _M0L3valS2043 + 1;
                _M0L1sS877->$0 = _M0L6_2atmpS2042;
                continue;
              } else {
                moonbit_decref(_M0L1sS877);
              }
              break;
            }
          }
          _M0L3valS2062 = _M0L1jS874->$0;
          _M0L6_2atmpS2061 = _M0L3valS2062 + 1;
          _M0L1jS874->$0 = _M0L6_2atmpS2061;
          continue;
        } else {
          moonbit_decref(_M0L1jS874);
          moonbit_decref(_M0L6targetS872);
        }
        break;
      }
    } else {
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS2065 =
        _M0L1cS858->$4;
      struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS2067 = _M0L1cS858->$0;
      struct _M0TPB5ArrayGbE* _M0L4fireS2066 = _M0L3preS2067->$5;
      #line 300 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
      _M0MP26RiantR8snn__mbt15SparseMatrixCSR7forward(_M0L6matrixS2065, _M0L4fireS2066, _M0L6targetS872);
      moonbit_decref(_M0L6targetS872);
    }
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt13apply__weight(
  struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0L1cS854,
  int32_t _M0L9post__idxS855,
  float _M0L1wS856
) {
  moonbit_string_t _M0L3symS1989;
  #line 346 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
  _M0L3symS1989 = _M0L1cS854->$2;
  #line 347 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
  if (
    _M0L3symS1989 == (moonbit_string_t)moonbit_string_literal_1.data
    || Moonbit_array_length(_M0L3symS1989)
       == Moonbit_array_length((moonbit_string_t)moonbit_string_literal_1.data)
       && 0
          == memcmp(_M0L3symS1989, (moonbit_string_t)moonbit_string_literal_1.data, Moonbit_array_length(_M0L3symS1989) * 2)
  ) {
    struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS1995 = _M0L1cS854->$1;
    struct _M0TPB5ArrayGfE* _M0L3gluS1990 = _M0L4postS1995->$13;
    struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS1994 = _M0L1cS854->$1;
    struct _M0TPB5ArrayGfE* _M0L3gluS1993 = _M0L4postS1994->$13;
    float _M0L6_2atmpS1992;
    float _M0L6_2atmpS1991;
    #line 348 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
    _M0L6_2atmpS1992
    = _M0MPC15array5Array2atGfE(_M0L3gluS1993, _M0L9post__idxS855);
    _M0L6_2atmpS1991 = _M0L6_2atmpS1992 + _M0L1wS856;
    #line 348 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
    _M0MPC15array5Array3setGfE(_M0L3gluS1990, _M0L9post__idxS855, _M0L6_2atmpS1991);
  } else {
    struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS2001 = _M0L1cS854->$1;
    struct _M0TPB5ArrayGfE* _M0L4gabaS1996 = _M0L4postS2001->$14;
    struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS2000 = _M0L1cS854->$1;
    struct _M0TPB5ArrayGfE* _M0L4gabaS1999 = _M0L4postS2000->$14;
    float _M0L6_2atmpS1998;
    float _M0L6_2atmpS1997;
    #line 350 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
    _M0L6_2atmpS1998
    = _M0MPC15array5Array2atGfE(_M0L4gabaS1999, _M0L9post__idxS855);
    _M0L6_2atmpS1997 = _M0L6_2atmpS1998 + _M0L1wS856;
    #line 350 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
    _M0MPC15array5Array3setGfE(_M0L4gabaS1996, _M0L9post__idxS855, _M0L6_2atmpS1997);
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt11record__one(
  struct _M0TP26RiantR8snn__mbt7Monitor* _M0L1mS851,
  float _M0L1tS853
) {
  int32_t _M0L11step__countS1975;
  int32_t _M0L6_2atmpS1974;
  int32_t _M0L11step__countS1977;
  int32_t _M0L9rec__stepS1978;
  int32_t _M0L6_2atmpS1976;
  moonbit_string_t _M0L3symS1981;
  float _M0L1vS852;
  struct _M0TPB5ArrayGfE* _M0L4dataS1979;
  struct _M0TPB5ArrayGfE* _M0L5timesS1980;
  #line 61 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
  _M0L11step__countS1975 = _M0L1mS851->$6;
  _M0L6_2atmpS1974 = _M0L11step__countS1975 + 1;
  _M0L1mS851->$6 = _M0L6_2atmpS1974;
  _M0L11step__countS1977 = _M0L1mS851->$6;
  _M0L9rec__stepS1978 = _M0L1mS851->$5;
  _M0L6_2atmpS1976 = _M0L11step__countS1977 % _M0L9rec__stepS1978;
  if (_M0L6_2atmpS1976 != 0) {
    return 0;
  }
  _M0L3symS1981 = _M0L1mS851->$1;
  #line 66 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
  if (
    _M0L3symS1981 == (moonbit_string_t)moonbit_string_literal_2.data
    || Moonbit_array_length(_M0L3symS1981)
       == Moonbit_array_length((moonbit_string_t)moonbit_string_literal_2.data)
       && 0
          == memcmp(_M0L3symS1981, (moonbit_string_t)moonbit_string_literal_2.data, Moonbit_array_length(_M0L3symS1981) * 2)
  ) {
    struct _M0TP26RiantR8snn__mbt2IF* _M0L3popS1984 = _M0L1mS851->$0;
    struct _M0TPB5ArrayGfE* _M0L1vS1982 = _M0L3popS1984->$3;
    int32_t _M0L6neuronS1983 = _M0L1mS851->$4;
    #line 67 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
    _M0L1vS852 = _M0MPC15array5Array2atGfE(_M0L1vS1982, _M0L6neuronS1983);
  } else {
    moonbit_string_t _M0L3symS1985 = _M0L1mS851->$1;
    #line 68 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
    if (
      _M0L3symS1985 == (moonbit_string_t)moonbit_string_literal_3.data
      || Moonbit_array_length(_M0L3symS1985)
         == Moonbit_array_length((moonbit_string_t)moonbit_string_literal_3.data)
         && 0
            == memcmp(_M0L3symS1985, (moonbit_string_t)moonbit_string_literal_3.data, Moonbit_array_length(_M0L3symS1985) * 2)
    ) {
      struct _M0TP26RiantR8snn__mbt2IF* _M0L3popS1988 = _M0L1mS851->$0;
      struct _M0TPB5ArrayGbE* _M0L4fireS1986 = _M0L3popS1988->$5;
      int32_t _M0L6neuronS1987 = _M0L1mS851->$4;
      #line 69 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS1986, _M0L6neuronS1987)) {
        _M0L1vS852 = 0x1p+0f;
      } else {
        _M0L1vS852 = 0x0p+0f;
      }
    } else {
      _M0L1vS852 = 0x0p+0f;
    }
  }
  _M0L4dataS1979 = _M0L1mS851->$2;
  #line 73 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
  _M0MPC15array5Array4pushGfE(_M0L4dataS1979, _M0L1vS852);
  _M0L5timesS1980 = _M0L1mS851->$3;
  #line 74 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
  _M0MPC15array5Array4pushGfE(_M0L5timesS1980, _M0L1tS853);
  return 0;
}

struct _M0TP26RiantR8snn__mbt7Monitor* _M0MP26RiantR8snn__mbt7Monitor9new__fire(
  struct _M0TP26RiantR8snn__mbt2IF* _M0L3popS849,
  int32_t _M0L6neuronS850
) {
  float* _M0L6_2atmpS1973;
  struct _M0TPB5ArrayGfE* _M0L6_2atmpS1970;
  float* _M0L6_2atmpS1972;
  struct _M0TPB5ArrayGfE* _M0L6_2atmpS1971;
  struct _M0TP26RiantR8snn__mbt7Monitor* _block_2127;
  #line 55 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
  _M0L6_2atmpS1973 = moonbit_empty_float_array;
  _M0L6_2atmpS1970
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_M0L6_2atmpS1970)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 0, 0);
  _M0L6_2atmpS1970->$0 = _M0L6_2atmpS1973;
  _M0L6_2atmpS1970->$1 = 0;
  _M0L6_2atmpS1972 = moonbit_empty_float_array;
  _M0L6_2atmpS1971
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_M0L6_2atmpS1971)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 0, 0);
  _M0L6_2atmpS1971->$0 = _M0L6_2atmpS1972;
  _M0L6_2atmpS1971->$1 = 0;
  moonbit_incref(_M0L3popS849);
  _block_2127
  = (struct _M0TP26RiantR8snn__mbt7Monitor*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt7Monitor));
  Moonbit_object_header(_block_2127)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 39, 0);
  _block_2127->$0 = _M0L3popS849;
  _block_2127->$1 = (moonbit_string_t)moonbit_string_literal_3.data;
  _block_2127->$2 = _M0L6_2atmpS1970;
  _block_2127->$3 = _M0L6_2atmpS1971;
  _block_2127->$4 = _M0L6neuronS850;
  _block_2127->$5 = 1;
  _block_2127->$6 = 0;
  return _block_2127;
}

int32_t _M0FP26RiantR8snn__mbt17synaptic__current(
  struct _M0TP26RiantR8snn__mbt2IF* _M0L1pS845
) {
  int32_t _M0L1nS844;
  int32_t _M0L7_2abindS846;
  int32_t _M0L1iS847;
  #line 165 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L1nS844 = _M0L1pS845->$2;
  _M0L7_2abindS846 = 0;
  _M0L1iS847 = _M0L7_2abindS846;
  while (1) {
    if (_M0L1iS847 < _M0L1nS844) {
      struct _M0TPB5ArrayGfE* _M0L9syn__currS1947 = _M0L1pS845->$8;
      struct _M0TPB5ArrayGfE* _M0L2geS1968 = _M0L1pS845->$9;
      float _M0L6_2atmpS1963;
      struct _M0TPB5ArrayGfE* _M0L1vS1967;
      float _M0L6_2atmpS1965;
      float _M0L4e__eS1966;
      float _M0L6_2atmpS1964;
      float _M0L6_2atmpS1960;
      struct _M0TPB5ArrayGfE* _M0L7gsyn__eS1962;
      float _M0L6_2atmpS1961;
      float _M0L6_2atmpS1949;
      struct _M0TPB5ArrayGfE* _M0L2giS1959;
      float _M0L6_2atmpS1954;
      struct _M0TPB5ArrayGfE* _M0L1vS1958;
      float _M0L6_2atmpS1956;
      float _M0L4e__iS1957;
      float _M0L6_2atmpS1955;
      float _M0L6_2atmpS1951;
      struct _M0TPB5ArrayGfE* _M0L7gsyn__iS1953;
      float _M0L6_2atmpS1952;
      float _M0L6_2atmpS1950;
      float _M0L6_2atmpS1948;
      int32_t _M0L6_2atmpS1969;
      #line 168 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1963 = _M0MPC15array5Array2atGfE(_M0L2geS1968, _M0L1iS847);
      _M0L1vS1967 = _M0L1pS845->$3;
      #line 168 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1965 = _M0MPC15array5Array2atGfE(_M0L1vS1967, _M0L1iS847);
      _M0L4e__eS1966 = _M0L1pS845->$17;
      _M0L6_2atmpS1964 = _M0L6_2atmpS1965 - _M0L4e__eS1966;
      _M0L6_2atmpS1960 = _M0L6_2atmpS1963 * _M0L6_2atmpS1964;
      _M0L7gsyn__eS1962 = _M0L1pS845->$15;
      #line 168 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1961
      = _M0MPC15array5Array2atGfE(_M0L7gsyn__eS1962, _M0L1iS847);
      _M0L6_2atmpS1949 = _M0L6_2atmpS1960 * _M0L6_2atmpS1961;
      _M0L2giS1959 = _M0L1pS845->$10;
      #line 169 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1954 = _M0MPC15array5Array2atGfE(_M0L2giS1959, _M0L1iS847);
      _M0L1vS1958 = _M0L1pS845->$3;
      #line 169 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1956 = _M0MPC15array5Array2atGfE(_M0L1vS1958, _M0L1iS847);
      _M0L4e__iS1957 = _M0L1pS845->$18;
      _M0L6_2atmpS1955 = _M0L6_2atmpS1956 - _M0L4e__iS1957;
      _M0L6_2atmpS1951 = _M0L6_2atmpS1954 * _M0L6_2atmpS1955;
      _M0L7gsyn__iS1953 = _M0L1pS845->$16;
      #line 169 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1952
      = _M0MPC15array5Array2atGfE(_M0L7gsyn__iS1953, _M0L1iS847);
      _M0L6_2atmpS1950 = _M0L6_2atmpS1951 * _M0L6_2atmpS1952;
      _M0L6_2atmpS1948 = _M0L6_2atmpS1949 + _M0L6_2atmpS1950;
      #line 168 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L9syn__currS1947, _M0L1iS847, _M0L6_2atmpS1948);
      _M0L6_2atmpS1969 = _M0L1iS847 + 1;
      _M0L1iS847 = _M0L6_2atmpS1969;
      continue;
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt14step__synapses(
  struct _M0TP26RiantR8snn__mbt2IF* _M0L1pS836,
  float _M0L2dtS839
) {
  int32_t _M0L1nS835;
  int32_t _M0L7_2abindS837;
  int32_t _M0L1iS838;
  int32_t _M0L7_2abindS841;
  int32_t _M0L1iS842;
  #line 146 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L1nS835 = _M0L1pS836->$2;
  _M0L7_2abindS837 = 0;
  _M0L1iS838 = _M0L7_2abindS837;
  while (1) {
    if (_M0L1iS838 < _M0L1nS835) {
      struct _M0TPB5ArrayGfE* _M0L2heS1885 = _M0L1pS836->$11;
      struct _M0TPB5ArrayGfE* _M0L2heS1890 = _M0L1pS836->$11;
      float _M0L6_2atmpS1887;
      struct _M0TPB5ArrayGfE* _M0L3gluS1889;
      float _M0L6_2atmpS1888;
      float _M0L6_2atmpS1886;
      struct _M0TPB5ArrayGfE* _M0L2hiS1891;
      struct _M0TPB5ArrayGfE* _M0L2hiS1896;
      float _M0L6_2atmpS1893;
      struct _M0TPB5ArrayGfE* _M0L4gabaS1895;
      float _M0L6_2atmpS1894;
      float _M0L6_2atmpS1892;
      struct _M0TPB5ArrayGfE* _M0L2geS1897;
      struct _M0TPB5ArrayGfE* _M0L2geS1909;
      float _M0L6_2atmpS1899;
      struct _M0TPB5ArrayGfE* _M0L2geS1908;
      float _M0L6_2atmpS1907;
      float _M0L6_2atmpS1905;
      float _M0L3tdeS1906;
      float _M0L6_2atmpS1902;
      struct _M0TPB5ArrayGfE* _M0L2heS1904;
      float _M0L6_2atmpS1903;
      float _M0L6_2atmpS1901;
      float _M0L6_2atmpS1900;
      float _M0L6_2atmpS1898;
      struct _M0TPB5ArrayGfE* _M0L2heS1910;
      struct _M0TPB5ArrayGfE* _M0L2heS1919;
      float _M0L6_2atmpS1912;
      struct _M0TPB5ArrayGfE* _M0L2heS1918;
      float _M0L6_2atmpS1917;
      float _M0L6_2atmpS1915;
      float _M0L3treS1916;
      float _M0L6_2atmpS1914;
      float _M0L6_2atmpS1913;
      float _M0L6_2atmpS1911;
      struct _M0TPB5ArrayGfE* _M0L2giS1920;
      struct _M0TPB5ArrayGfE* _M0L2giS1932;
      float _M0L6_2atmpS1922;
      struct _M0TPB5ArrayGfE* _M0L2giS1931;
      float _M0L6_2atmpS1930;
      float _M0L6_2atmpS1928;
      float _M0L3tdiS1929;
      float _M0L6_2atmpS1925;
      struct _M0TPB5ArrayGfE* _M0L2hiS1927;
      float _M0L6_2atmpS1926;
      float _M0L6_2atmpS1924;
      float _M0L6_2atmpS1923;
      float _M0L6_2atmpS1921;
      struct _M0TPB5ArrayGfE* _M0L2hiS1933;
      struct _M0TPB5ArrayGfE* _M0L2hiS1942;
      float _M0L6_2atmpS1935;
      struct _M0TPB5ArrayGfE* _M0L2hiS1941;
      float _M0L6_2atmpS1940;
      float _M0L6_2atmpS1938;
      float _M0L3triS1939;
      float _M0L6_2atmpS1937;
      float _M0L6_2atmpS1936;
      float _M0L6_2atmpS1934;
      int32_t _M0L6_2atmpS1943;
      #line 149 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1887 = _M0MPC15array5Array2atGfE(_M0L2heS1890, _M0L1iS838);
      _M0L3gluS1889 = _M0L1pS836->$13;
      #line 149 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1888 = _M0MPC15array5Array2atGfE(_M0L3gluS1889, _M0L1iS838);
      _M0L6_2atmpS1886 = _M0L6_2atmpS1887 + _M0L6_2atmpS1888;
      #line 149 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L2heS1885, _M0L1iS838, _M0L6_2atmpS1886);
      _M0L2hiS1891 = _M0L1pS836->$12;
      _M0L2hiS1896 = _M0L1pS836->$12;
      #line 150 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1893 = _M0MPC15array5Array2atGfE(_M0L2hiS1896, _M0L1iS838);
      _M0L4gabaS1895 = _M0L1pS836->$14;
      #line 150 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1894
      = _M0MPC15array5Array2atGfE(_M0L4gabaS1895, _M0L1iS838);
      _M0L6_2atmpS1892 = _M0L6_2atmpS1893 + _M0L6_2atmpS1894;
      #line 150 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L2hiS1891, _M0L1iS838, _M0L6_2atmpS1892);
      _M0L2geS1897 = _M0L1pS836->$9;
      _M0L2geS1909 = _M0L1pS836->$9;
      #line 151 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1899 = _M0MPC15array5Array2atGfE(_M0L2geS1909, _M0L1iS838);
      _M0L2geS1908 = _M0L1pS836->$9;
      #line 151 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1907 = _M0MPC15array5Array2atGfE(_M0L2geS1908, _M0L1iS838);
      _M0L6_2atmpS1905 = -_M0L6_2atmpS1907;
      _M0L3tdeS1906 = _M0L1pS836->$20;
      _M0L6_2atmpS1902 = _M0L6_2atmpS1905 / _M0L3tdeS1906;
      _M0L2heS1904 = _M0L1pS836->$11;
      #line 151 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1903 = _M0MPC15array5Array2atGfE(_M0L2heS1904, _M0L1iS838);
      _M0L6_2atmpS1901 = _M0L6_2atmpS1902 + _M0L6_2atmpS1903;
      _M0L6_2atmpS1900 = _M0L2dtS839 * _M0L6_2atmpS1901;
      _M0L6_2atmpS1898 = _M0L6_2atmpS1899 + _M0L6_2atmpS1900;
      #line 151 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L2geS1897, _M0L1iS838, _M0L6_2atmpS1898);
      _M0L2heS1910 = _M0L1pS836->$11;
      _M0L2heS1919 = _M0L1pS836->$11;
      #line 152 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1912 = _M0MPC15array5Array2atGfE(_M0L2heS1919, _M0L1iS838);
      _M0L2heS1918 = _M0L1pS836->$11;
      #line 152 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1917 = _M0MPC15array5Array2atGfE(_M0L2heS1918, _M0L1iS838);
      _M0L6_2atmpS1915 = -_M0L6_2atmpS1917;
      _M0L3treS1916 = _M0L1pS836->$19;
      _M0L6_2atmpS1914 = _M0L6_2atmpS1915 / _M0L3treS1916;
      _M0L6_2atmpS1913 = _M0L2dtS839 * _M0L6_2atmpS1914;
      _M0L6_2atmpS1911 = _M0L6_2atmpS1912 + _M0L6_2atmpS1913;
      #line 152 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L2heS1910, _M0L1iS838, _M0L6_2atmpS1911);
      _M0L2giS1920 = _M0L1pS836->$10;
      _M0L2giS1932 = _M0L1pS836->$10;
      #line 153 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1922 = _M0MPC15array5Array2atGfE(_M0L2giS1932, _M0L1iS838);
      _M0L2giS1931 = _M0L1pS836->$10;
      #line 153 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1930 = _M0MPC15array5Array2atGfE(_M0L2giS1931, _M0L1iS838);
      _M0L6_2atmpS1928 = -_M0L6_2atmpS1930;
      _M0L3tdiS1929 = _M0L1pS836->$22;
      _M0L6_2atmpS1925 = _M0L6_2atmpS1928 / _M0L3tdiS1929;
      _M0L2hiS1927 = _M0L1pS836->$12;
      #line 153 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1926 = _M0MPC15array5Array2atGfE(_M0L2hiS1927, _M0L1iS838);
      _M0L6_2atmpS1924 = _M0L6_2atmpS1925 + _M0L6_2atmpS1926;
      _M0L6_2atmpS1923 = _M0L2dtS839 * _M0L6_2atmpS1924;
      _M0L6_2atmpS1921 = _M0L6_2atmpS1922 + _M0L6_2atmpS1923;
      #line 153 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L2giS1920, _M0L1iS838, _M0L6_2atmpS1921);
      _M0L2hiS1933 = _M0L1pS836->$12;
      _M0L2hiS1942 = _M0L1pS836->$12;
      #line 154 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1935 = _M0MPC15array5Array2atGfE(_M0L2hiS1942, _M0L1iS838);
      _M0L2hiS1941 = _M0L1pS836->$12;
      #line 154 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1940 = _M0MPC15array5Array2atGfE(_M0L2hiS1941, _M0L1iS838);
      _M0L6_2atmpS1938 = -_M0L6_2atmpS1940;
      _M0L3triS1939 = _M0L1pS836->$21;
      _M0L6_2atmpS1937 = _M0L6_2atmpS1938 / _M0L3triS1939;
      _M0L6_2atmpS1936 = _M0L2dtS839 * _M0L6_2atmpS1937;
      _M0L6_2atmpS1934 = _M0L6_2atmpS1935 + _M0L6_2atmpS1936;
      #line 154 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L2hiS1933, _M0L1iS838, _M0L6_2atmpS1934);
      _M0L6_2atmpS1943 = _M0L1iS838 + 1;
      _M0L1iS838 = _M0L6_2atmpS1943;
      continue;
    }
    break;
  }
  _M0L7_2abindS841 = 0;
  _M0L1iS842 = _M0L7_2abindS841;
  while (1) {
    if (_M0L1iS842 < _M0L1nS835) {
      struct _M0TPB5ArrayGfE* _M0L3gluS1944 = _M0L1pS836->$13;
      struct _M0TPB5ArrayGfE* _M0L4gabaS1945;
      int32_t _M0L6_2atmpS1946;
      #line 157 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L3gluS1944, _M0L1iS842, 0x0p+0f);
      _M0L4gabaS1945 = _M0L1pS836->$14;
      #line 158 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L4gabaS1945, _M0L1iS842, 0x0p+0f);
      _M0L6_2atmpS1946 = _M0L1iS842 + 1;
      _M0L1iS842 = _M0L6_2atmpS1946;
      continue;
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt12step__neuron(
  struct _M0TP26RiantR8snn__mbt2IF* _M0L1pS821,
  float _M0L2dtS830
) {
  int32_t _M0L1nS820;
  struct _M0TP26RiantR8snn__mbt11IFParameter* _M0L3p__S822;
  float _M0L2tmS823;
  float _M0L2elS824;
  float _M0L1rS825;
  float _M0L2vtS826;
  float _M0L2vrS827;
  struct _M0TP26RiantR8snn__mbt9PostSpike* _M0L5spikeS1884;
  float _M0L11tabs__constS828;
  float _M0L6_2atmpS1883;
  int32_t _M0L11tabs__stepsS829;
  int32_t _M0L7_2abindS831;
  int32_t _M0L1iS832;
  #line 176 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L1nS820 = _M0L1pS821->$2;
  _M0L3p__S822 = _M0L1pS821->$0;
  _M0L2tmS823 = _M0L3p__S822->$2;
  _M0L2elS824 = _M0L3p__S822->$5;
  _M0L1rS825 = _M0L3p__S822->$6;
  _M0L2vtS826 = _M0L3p__S822->$3;
  _M0L2vrS827 = _M0L3p__S822->$4;
  _M0L5spikeS1884 = _M0L1pS821->$1;
  _M0L11tabs__constS828 = _M0L5spikeS1884->$0;
  _M0L6_2atmpS1883 = _M0L11tabs__constS828 / _M0L2dtS830;
  #line 185 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L11tabs__stepsS829 = _M0MPC15float5Float7to__int(_M0L6_2atmpS1883);
  _M0L7_2abindS831 = 0;
  _M0L1iS832 = _M0L7_2abindS831;
  while (1) {
    if (_M0L1iS832 < _M0L1nS820) {
      struct _M0TPB5ArrayGiE* _M0L4tabsS1843 = _M0L1pS821->$6;
      int32_t _M0L6_2atmpS1842;
      struct _M0TPB5ArrayGfE* _M0L1vS1849;
      struct _M0TPB5ArrayGfE* _M0L1vS1870;
      float _M0L6_2atmpS1851;
      float _M0L6_2atmpS1853;
      struct _M0TPB5ArrayGfE* _M0L1vS1869;
      float _M0L6_2atmpS1868;
      float _M0L6_2atmpS1867;
      float _M0L6_2atmpS1859;
      struct _M0TPB5ArrayGfE* _M0L1wS1866;
      float _M0L6_2atmpS1865;
      float _M0L6_2atmpS1862;
      struct _M0TPB5ArrayGfE* _M0L1iS1864;
      float _M0L6_2atmpS1863;
      float _M0L6_2atmpS1861;
      float _M0L6_2atmpS1860;
      float _M0L6_2atmpS1855;
      struct _M0TPB5ArrayGfE* _M0L9syn__currS1858;
      float _M0L6_2atmpS1857;
      float _M0L6_2atmpS1856;
      float _M0L6_2atmpS1854;
      float _M0L6_2atmpS1852;
      float _M0L6_2atmpS1850;
      struct _M0TPB5ArrayGbE* _M0L4fireS1871;
      struct _M0TPB5ArrayGfE* _M0L1vS1874;
      float _M0L6_2atmpS1873;
      int32_t _M0L6_2atmpS1872;
      struct _M0TPB5ArrayGfE* _M0L1vS1875;
      struct _M0TPB5ArrayGbE* _M0L4fireS1877;
      float _M0L6_2atmpS1876;
      struct _M0TPB5ArrayGiE* _M0L4tabsS1879;
      struct _M0TPB5ArrayGbE* _M0L4fireS1881;
      int32_t _M0L6_2atmpS1880;
      int32_t _M0L6_2atmpS1841;
      #line 188 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1842
      = _M0MPC15array5Array2atGiE(_M0L4tabsS1843, _M0L1iS832);
      if (_M0L6_2atmpS1842 > 0) {
        struct _M0TPB5ArrayGbE* _M0L4fireS1844 = _M0L1pS821->$5;
        struct _M0TPB5ArrayGiE* _M0L4tabsS1845;
        struct _M0TPB5ArrayGiE* _M0L4tabsS1848;
        int32_t _M0L6_2atmpS1847;
        int32_t _M0L6_2atmpS1846;
        #line 189 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
        _M0MPC15array5Array3setGbE(_M0L4fireS1844, _M0L1iS832, 0);
        _M0L4tabsS1845 = _M0L1pS821->$6;
        _M0L4tabsS1848 = _M0L1pS821->$6;
        #line 190 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
        _M0L6_2atmpS1847
        = _M0MPC15array5Array2atGiE(_M0L4tabsS1848, _M0L1iS832);
        _M0L6_2atmpS1846 = _M0L6_2atmpS1847 - 1;
        #line 190 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
        _M0MPC15array5Array3setGiE(_M0L4tabsS1845, _M0L1iS832, _M0L6_2atmpS1846);
        goto join_833;
      }
      _M0L1vS1849 = _M0L1pS821->$3;
      _M0L1vS1870 = _M0L1pS821->$3;
      #line 193 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1851 = _M0MPC15array5Array2atGfE(_M0L1vS1870, _M0L1iS832);
      _M0L6_2atmpS1853 = _M0L2dtS830 / _M0L2tmS823;
      _M0L1vS1869 = _M0L1pS821->$3;
      #line 194 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1868 = _M0MPC15array5Array2atGfE(_M0L1vS1869, _M0L1iS832);
      _M0L6_2atmpS1867 = _M0L6_2atmpS1868 - _M0L2elS824;
      _M0L6_2atmpS1859 = -_M0L6_2atmpS1867;
      _M0L1wS1866 = _M0L1pS821->$4;
      #line 194 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1865 = _M0MPC15array5Array2atGfE(_M0L1wS1866, _M0L1iS832);
      _M0L6_2atmpS1862 = -_M0L6_2atmpS1865;
      _M0L1iS1864 = _M0L1pS821->$7;
      #line 194 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1863 = _M0MPC15array5Array2atGfE(_M0L1iS1864, _M0L1iS832);
      _M0L6_2atmpS1861 = _M0L6_2atmpS1862 + _M0L6_2atmpS1863;
      _M0L6_2atmpS1860 = _M0L1rS825 * _M0L6_2atmpS1861;
      _M0L6_2atmpS1855 = _M0L6_2atmpS1859 + _M0L6_2atmpS1860;
      _M0L9syn__currS1858 = _M0L1pS821->$8;
      #line 194 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1857
      = _M0MPC15array5Array2atGfE(_M0L9syn__currS1858, _M0L1iS832);
      _M0L6_2atmpS1856 = _M0L1rS825 * _M0L6_2atmpS1857;
      _M0L6_2atmpS1854 = _M0L6_2atmpS1855 - _M0L6_2atmpS1856;
      _M0L6_2atmpS1852 = _M0L6_2atmpS1853 * _M0L6_2atmpS1854;
      _M0L6_2atmpS1850 = _M0L6_2atmpS1851 + _M0L6_2atmpS1852;
      #line 193 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS1849, _M0L1iS832, _M0L6_2atmpS1850);
      _M0L4fireS1871 = _M0L1pS821->$5;
      _M0L1vS1874 = _M0L1pS821->$3;
      #line 195 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1873 = _M0MPC15array5Array2atGfE(_M0L1vS1874, _M0L1iS832);
      _M0L6_2atmpS1872 = _M0L6_2atmpS1873 > _M0L2vtS826;
      #line 195 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGbE(_M0L4fireS1871, _M0L1iS832, _M0L6_2atmpS1872);
      _M0L1vS1875 = _M0L1pS821->$3;
      _M0L4fireS1877 = _M0L1pS821->$5;
      #line 196 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS1877, _M0L1iS832)) {
        _M0L6_2atmpS1876 = _M0L2vrS827;
      } else {
        struct _M0TPB5ArrayGfE* _M0L1vS1878 = _M0L1pS821->$3;
        #line 196 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
        _M0L6_2atmpS1876 = _M0MPC15array5Array2atGfE(_M0L1vS1878, _M0L1iS832);
      }
      #line 196 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS1875, _M0L1iS832, _M0L6_2atmpS1876);
      _M0L4tabsS1879 = _M0L1pS821->$6;
      _M0L4fireS1881 = _M0L1pS821->$5;
      #line 197 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS1881, _M0L1iS832)) {
        _M0L6_2atmpS1880 = _M0L11tabs__stepsS829;
      } else {
        struct _M0TPB5ArrayGiE* _M0L4tabsS1882 = _M0L1pS821->$6;
        #line 197 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
        _M0L6_2atmpS1880
        = _M0MPC15array5Array2atGiE(_M0L4tabsS1882, _M0L1iS832);
      }
      #line 197 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGiE(_M0L4tabsS1879, _M0L1iS832, _M0L6_2atmpS1880);
      goto join_833;
      goto joinlet_2132;
      join_833:;
      _M0L6_2atmpS1841 = _M0L1iS832 + 1;
      _M0L1iS832 = _M0L6_2atmpS1841;
      continue;
      joinlet_2132:;
    }
    break;
  }
  return 0;
}

int32_t _M0MP26RiantR8snn__mbt15SparseMatrixCSR7forward(
  struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L1mS808,
  struct _M0TPB5ArrayGbE* _M0L9pre__fireS811,
  struct _M0TPB5ArrayGfE* _M0L7post__gS817
) {
  int32_t _M0L4rowsS807;
  int32_t _M0L7_2abindS809;
  int32_t _M0L1iS810;
  #line 287 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  _M0L4rowsS807 = _M0L1mS808->$0;
  _M0L7_2abindS809 = 0;
  _M0L1iS810 = _M0L7_2abindS809;
  while (1) {
    if (_M0L1iS810 < _M0L4rowsS807) {
      int32_t _M0L6_2atmpS1840;
      #line 294 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L9pre__fireS811, _M0L1iS810)) {
        struct _M0TPB5ArrayGiE* _M0L6rowptrS1839 = _M0L1mS808->$2;
        int32_t _M0L5startS812;
        struct _M0TPB5ArrayGiE* _M0L6rowptrS1837;
        int32_t _M0L6_2atmpS1838;
        int32_t _M0L3endS813;
        int32_t _M0L1kS814;
        #line 295 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
        _M0L5startS812
        = _M0MPC15array5Array2atGiE(_M0L6rowptrS1839, _M0L1iS810);
        _M0L6rowptrS1837 = _M0L1mS808->$2;
        _M0L6_2atmpS1838 = _M0L1iS810 + 1;
        #line 296 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
        _M0L3endS813
        = _M0MPC15array5Array2atGiE(_M0L6rowptrS1837, _M0L6_2atmpS1838);
        _M0L1kS814 = _M0L5startS812;
        while (1) {
          if (_M0L1kS814 < _M0L3endS813) {
            struct _M0TPB5ArrayGiE* _M0L6colptrS1835 = _M0L1mS808->$3;
            int32_t _M0L9post__idxS815;
            struct _M0TPB5ArrayGfE* _M0L4valsS1834;
            float _M0L1wS816;
            float _M0L6_2atmpS1833;
            float _M0L6_2atmpS1832;
            int32_t _M0L6_2atmpS1836;
            #line 298 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
            _M0L9post__idxS815
            = _M0MPC15array5Array2atGiE(_M0L6colptrS1835, _M0L1kS814);
            _M0L4valsS1834 = _M0L1mS808->$4;
            #line 299 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
            _M0L1wS816
            = _M0MPC15array5Array2atGfE(_M0L4valsS1834, _M0L1kS814);
            #line 300 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
            _M0L6_2atmpS1833
            = _M0MPC15array5Array2atGfE(_M0L7post__gS817, _M0L9post__idxS815);
            _M0L6_2atmpS1832 = _M0L6_2atmpS1833 + _M0L1wS816;
            #line 300 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
            _M0MPC15array5Array3setGfE(_M0L7post__gS817, _M0L9post__idxS815, _M0L6_2atmpS1832);
            _M0L6_2atmpS1836 = _M0L1kS814 + 1;
            _M0L1kS814 = _M0L6_2atmpS1836;
            continue;
          }
          break;
        }
      }
      _M0L6_2atmpS1840 = _M0L1iS810 + 1;
      _M0L1iS810 = _M0L6_2atmpS1840;
      continue;
    }
    break;
  }
  return 0;
}

struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0MP26RiantR8snn__mbt15SparseMatrixCSR6random(
  int32_t _M0L4rowsS801,
  int32_t _M0L4colsS802,
  float _M0L2muS803,
  float _M0L5sigmaS804,
  float _M0L1pS805,
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS806
) {
  #line 94 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  #line 103 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  return _M0MP26RiantR8snn__mbt15SparseMatrixCSR18random__with__rule(_M0L4rowsS801, _M0L4colsS802, _M0L2muS803, _M0L5sigmaS804, _M0L1pS805, 0, _M0L3rngS806);
}

struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0MP26RiantR8snn__mbt15SparseMatrixCSR18random__with__rule(
  int32_t _M0L4rowsS715,
  int32_t _M0L4colsS719,
  float _M0L2muS725,
  float _M0L5sigmaS726,
  float _M0L1pS738,
  int32_t _M0L4ruleS732,
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS728
) {
  float* _M0L6_2atmpS1831;
  struct _M0TPB5ArrayGfE* _M0L6_2atmpS1830;
  struct _M0TPB5ArrayGRPB5ArrayGfEE* _M0L5denseS714;
  int32_t _M0L7_2abindS716;
  int32_t _M0L1iS717;
  int32_t _M0L6_2atmpS1829;
  struct _M0TPB5ArrayGiE* _M0L6rowptrS791;
  int32_t* _M0L6_2atmpS1828;
  struct _M0TPB5ArrayGiE* _M0L6colptrS792;
  float* _M0L6_2atmpS1827;
  struct _M0TPB5ArrayGfE* _M0L4valsS793;
  int32_t _M0L7_2abindS794;
  int32_t _M0L1iS795;
  int32_t _M0L6_2atmpS1826;
  struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _block_2154;
  #line 109 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  _M0L6_2atmpS1831 = moonbit_empty_float_array;
  _M0L6_2atmpS1830
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_M0L6_2atmpS1830)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 0, 0);
  _M0L6_2atmpS1830->$0 = _M0L6_2atmpS1831;
  _M0L6_2atmpS1830->$1 = 0;
  #line 120 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  _M0L5denseS714
  = _M0MPC15array5Array4makeGRPB5ArrayGfEE(_M0L4rowsS715, _M0L6_2atmpS1830);
  _M0L7_2abindS716 = 0;
  _M0L1iS717 = _M0L7_2abindS716;
  while (1) {
    if (_M0L1iS717 < _M0L4rowsS715) {
      struct _M0TPB5ArrayGfE* _M0L3rowS718;
      int32_t _M0L7_2abindS720;
      int32_t _M0L1jS721;
      int32_t _M0L6_2atmpS1784;
      #line 122 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
      _M0L3rowS718 = _M0MPC15array5Array4makeGfE(_M0L4colsS719, 0x0p+0f);
      _M0L7_2abindS720 = 0;
      _M0L1jS721 = _M0L7_2abindS720;
      while (1) {
        if (_M0L1jS721 < _M0L4colsS719) {
          double _M0L2z1S723;
          struct _M0TUddE* _M0L7_2abindS727;
          double _M0L5_2az1S729;
          float _M0L6_2atmpS1782;
          float _M0L6_2atmpS1781;
          float _M0L1wS724;
          int32_t _M0L6_2atmpS1783;
          #line 131 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
          _M0L7_2abindS727
          = _M0FP26RiantR8snn__mbt11box__muller(_M0L3rngS728);
          _M0L5_2az1S729 = _M0L7_2abindS727->$0;
          moonbit_decref(_M0L7_2abindS727);
          _M0L2z1S723 = _M0L5_2az1S729;
          goto join_722;
          goto joinlet_2137;
          join_722:;
          _M0L6_2atmpS1782 = (float)_M0L2z1S723;
          _M0L6_2atmpS1781 = _M0L5sigmaS726 * _M0L6_2atmpS1782;
          _M0L1wS724 = _M0L2muS725 + _M0L6_2atmpS1781;
          #line 133 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
          _M0MPC15array5Array3setGfE(_M0L3rowS718, _M0L1jS721, _M0L1wS724);
          joinlet_2137:;
          _M0L6_2atmpS1783 = _M0L1jS721 + 1;
          _M0L1jS721 = _M0L6_2atmpS1783;
          continue;
        }
        break;
      }
      #line 135 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
      _M0MPC15array5Array3setGRPB5ArrayGfEE(_M0L5denseS714, _M0L1iS717, _M0L3rowS718);
      _M0L6_2atmpS1784 = _M0L1iS717 + 1;
      _M0L1iS717 = _M0L6_2atmpS1784;
      continue;
    }
    break;
  }
  switch (_M0L4ruleS732) {
    case 0: {
      int32_t _M0L7_2abindS733 = 0;
      int32_t _M0L1iS734 = _M0L7_2abindS733;
      while (1) {
        if (_M0L1iS734 < _M0L4rowsS715) {
          int32_t _M0L7_2abindS735 = 0;
          int32_t _M0L1jS736 = _M0L7_2abindS735;
          int32_t _M0L6_2atmpS1787;
          while (1) {
            if (_M0L1jS736 < _M0L4colsS719) {
              float _M0L1uS737;
              int32_t _M0L6_2atmpS1786;
              #line 143 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
              _M0L1uS737 = _M0FP26RiantR8snn__mbt9next__f32(_M0L3rngS728);
              if (_M0L1uS737 >= _M0L1pS738) {
                struct _M0TPB5ArrayGfE* _M0L6_2atmpS1785;
                #line 145 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS1785
                = _M0MPC15array5Array2atGRPB5ArrayGfEE(_M0L5denseS714, _M0L1iS734);
                #line 145 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array3setGfE(_M0L6_2atmpS1785, _M0L1jS736, 0x0p+0f);
                moonbit_decref(_M0L6_2atmpS1785);
              }
              _M0L6_2atmpS1786 = _M0L1jS736 + 1;
              _M0L1jS736 = _M0L6_2atmpS1786;
              continue;
            }
            break;
          }
          _M0L6_2atmpS1787 = _M0L1iS734 + 1;
          _M0L1iS734 = _M0L6_2atmpS1787;
          continue;
        }
        break;
      }
      break;
    }
    
    case 1: {
      float _M0L6_2atmpS1804 = (float)_M0L4rowsS715;
      float _M0L6_2atmpS1803 = _M0L6_2atmpS1804 * _M0L1pS738;
      int32_t _M0L7n__keepS741;
      #line 154 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
      _M0L7n__keepS741 = _M0MPC15float5Float7to__int(_M0L6_2atmpS1803);
      if (_M0L7n__keepS741 > 0 && _M0L7n__keepS741 <= _M0L4rowsS715) {
        int32_t _M0L7_2abindS742 = 0;
        int32_t _M0L1jS743 = _M0L7_2abindS742;
        while (1) {
          if (_M0L1jS743 < _M0L4colsS719) {
            int32_t* _M0L6_2atmpS1798 = (int32_t*)moonbit_empty_int32_array;
            struct _M0TPB5ArrayGiE* _M0L8pre__idxS744 =
              (struct _M0TPB5ArrayGiE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGiE));
            int32_t _M0L7_2abindS745;
            int32_t _M0L1kS746;
            int32_t _M0L7n__dropS748;
            int32_t _M0L7_2abindS749;
            int32_t _M0L1kS750;
            int32_t _M0L7_2abindS756;
            int32_t _M0L1kS757;
            int32_t _M0L6_2atmpS1799;
            Moonbit_object_header(_M0L8pre__idxS744)->meta
            = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 3, 0);
            _M0L8pre__idxS744->$0 = _M0L6_2atmpS1798;
            _M0L8pre__idxS744->$1 = 0;
            _M0L7_2abindS745 = 0;
            _M0L1kS746 = _M0L7_2abindS745;
            while (1) {
              if (_M0L1kS746 < _M0L4rowsS715) {
                int32_t _M0L6_2atmpS1788;
                #line 161 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array4pushGiE(_M0L8pre__idxS744, _M0L1kS746);
                _M0L6_2atmpS1788 = _M0L1kS746 + 1;
                _M0L1kS746 = _M0L6_2atmpS1788;
                continue;
              }
              break;
            }
            _M0L7n__dropS748 = _M0L4rowsS715 - _M0L7n__keepS741;
            _M0L7_2abindS749 = 0;
            _M0L1kS750 = _M0L7_2abindS749;
            while (1) {
              if (_M0L1kS750 < _M0L7n__dropS748) {
                float _M0L1uS751;
                int32_t _M0L6_2atmpS1793;
                float _M0L6_2atmpS1792;
                float _M0L6_2atmpS1791;
                int32_t _M0L6_2atmpS1790;
                int32_t _M0L6r__idxS752;
                int32_t _M0L10r__clampedS753;
                int32_t _M0L3tmpS754;
                int32_t _M0L6_2atmpS1789;
                int32_t _M0L6_2atmpS1794;
                #line 167 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L1uS751 = _M0FP26RiantR8snn__mbt9next__f32(_M0L3rngS728);
                _M0L6_2atmpS1793 = _M0L4rowsS715 - _M0L1kS750;
                _M0L6_2atmpS1792 = (float)_M0L6_2atmpS1793;
                _M0L6_2atmpS1791 = _M0L6_2atmpS1792 * _M0L1uS751;
                #line 168 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS1790
                = _M0MPC15float5Float7to__int(_M0L6_2atmpS1791);
                _M0L6r__idxS752 = _M0L1kS750 + _M0L6_2atmpS1790;
                if (_M0L6r__idxS752 >= _M0L4rowsS715) {
                  _M0L10r__clampedS753 = _M0L4rowsS715 - 1;
                } else {
                  _M0L10r__clampedS753 = _M0L6r__idxS752;
                }
                #line 171 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L3tmpS754
                = _M0MPC15array5Array2atGiE(_M0L8pre__idxS744, _M0L1kS750);
                #line 172 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS1789
                = _M0MPC15array5Array2atGiE(_M0L8pre__idxS744, _M0L10r__clampedS753);
                #line 172 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array3setGiE(_M0L8pre__idxS744, _M0L1kS750, _M0L6_2atmpS1789);
                #line 173 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array3setGiE(_M0L8pre__idxS744, _M0L10r__clampedS753, _M0L3tmpS754);
                _M0L6_2atmpS1794 = _M0L1kS750 + 1;
                _M0L1kS750 = _M0L6_2atmpS1794;
                continue;
              }
              break;
            }
            _M0L7_2abindS756 = 0;
            _M0L1kS757 = _M0L7_2abindS756;
            while (1) {
              if (_M0L1kS757 < _M0L7n__dropS748) {
                int32_t _M0L6_2atmpS1796;
                struct _M0TPB5ArrayGfE* _M0L6_2atmpS1795;
                int32_t _M0L6_2atmpS1797;
                #line 176 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS1796
                = _M0MPC15array5Array2atGiE(_M0L8pre__idxS744, _M0L1kS757);
                #line 176 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS1795
                = _M0MPC15array5Array2atGRPB5ArrayGfEE(_M0L5denseS714, _M0L6_2atmpS1796);
                #line 176 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array3setGfE(_M0L6_2atmpS1795, _M0L1jS743, 0x0p+0f);
                moonbit_decref(_M0L6_2atmpS1795);
                _M0L6_2atmpS1797 = _M0L1kS757 + 1;
                _M0L1kS757 = _M0L6_2atmpS1797;
                continue;
              } else {
                moonbit_decref(_M0L8pre__idxS744);
              }
              break;
            }
            _M0L6_2atmpS1799 = _M0L1jS743 + 1;
            _M0L1jS743 = _M0L6_2atmpS1799;
            continue;
          }
          break;
        }
      } else if (_M0L7n__keepS741 == 0) {
        int32_t _M0L7_2abindS760 = 0;
        int32_t _M0L1iS761 = _M0L7_2abindS760;
        while (1) {
          if (_M0L1iS761 < _M0L4rowsS715) {
            int32_t _M0L7_2abindS762 = 0;
            int32_t _M0L1jS763 = _M0L7_2abindS762;
            int32_t _M0L6_2atmpS1802;
            while (1) {
              if (_M0L1jS763 < _M0L4colsS719) {
                struct _M0TPB5ArrayGfE* _M0L6_2atmpS1800;
                int32_t _M0L6_2atmpS1801;
                #line 183 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS1800
                = _M0MPC15array5Array2atGRPB5ArrayGfEE(_M0L5denseS714, _M0L1iS761);
                #line 183 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array3setGfE(_M0L6_2atmpS1800, _M0L1jS763, 0x0p+0f);
                moonbit_decref(_M0L6_2atmpS1800);
                _M0L6_2atmpS1801 = _M0L1jS763 + 1;
                _M0L1jS763 = _M0L6_2atmpS1801;
                continue;
              }
              break;
            }
            _M0L6_2atmpS1802 = _M0L1iS761 + 1;
            _M0L1iS761 = _M0L6_2atmpS1802;
            continue;
          }
          break;
        }
      }
      break;
    }
    default: {
      float _M0L6_2atmpS1821 = (float)_M0L4colsS719;
      float _M0L6_2atmpS1820 = _M0L6_2atmpS1821 * _M0L1pS738;
      int32_t _M0L7n__keepS766;
      #line 191 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
      _M0L7n__keepS766 = _M0MPC15float5Float7to__int(_M0L6_2atmpS1820);
      if (_M0L7n__keepS766 > 0 && _M0L7n__keepS766 <= _M0L4colsS719) {
        int32_t _M0L7_2abindS767 = 0;
        int32_t _M0L1iS768 = _M0L7_2abindS767;
        while (1) {
          if (_M0L1iS768 < _M0L4rowsS715) {
            int32_t* _M0L6_2atmpS1815 = (int32_t*)moonbit_empty_int32_array;
            struct _M0TPB5ArrayGiE* _M0L9post__idxS769 =
              (struct _M0TPB5ArrayGiE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGiE));
            int32_t _M0L7_2abindS770;
            int32_t _M0L1kS771;
            int32_t _M0L7n__dropS773;
            int32_t _M0L7_2abindS774;
            int32_t _M0L1kS775;
            int32_t _M0L7_2abindS781;
            int32_t _M0L1kS782;
            int32_t _M0L6_2atmpS1816;
            Moonbit_object_header(_M0L9post__idxS769)->meta
            = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 3, 0);
            _M0L9post__idxS769->$0 = _M0L6_2atmpS1815;
            _M0L9post__idxS769->$1 = 0;
            _M0L7_2abindS770 = 0;
            _M0L1kS771 = _M0L7_2abindS770;
            while (1) {
              if (_M0L1kS771 < _M0L4colsS719) {
                int32_t _M0L6_2atmpS1805;
                #line 196 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array4pushGiE(_M0L9post__idxS769, _M0L1kS771);
                _M0L6_2atmpS1805 = _M0L1kS771 + 1;
                _M0L1kS771 = _M0L6_2atmpS1805;
                continue;
              }
              break;
            }
            _M0L7n__dropS773 = _M0L4colsS719 - _M0L7n__keepS766;
            _M0L7_2abindS774 = 0;
            _M0L1kS775 = _M0L7_2abindS774;
            while (1) {
              if (_M0L1kS775 < _M0L7n__dropS773) {
                float _M0L1uS776;
                int32_t _M0L6_2atmpS1810;
                float _M0L6_2atmpS1809;
                float _M0L6_2atmpS1808;
                int32_t _M0L6_2atmpS1807;
                int32_t _M0L6r__idxS777;
                int32_t _M0L10r__clampedS778;
                int32_t _M0L3tmpS779;
                int32_t _M0L6_2atmpS1806;
                int32_t _M0L6_2atmpS1811;
                #line 200 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L1uS776 = _M0FP26RiantR8snn__mbt9next__f32(_M0L3rngS728);
                _M0L6_2atmpS1810 = _M0L4colsS719 - _M0L1kS775;
                _M0L6_2atmpS1809 = (float)_M0L6_2atmpS1810;
                _M0L6_2atmpS1808 = _M0L6_2atmpS1809 * _M0L1uS776;
                #line 201 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS1807
                = _M0MPC15float5Float7to__int(_M0L6_2atmpS1808);
                _M0L6r__idxS777 = _M0L1kS775 + _M0L6_2atmpS1807;
                if (_M0L6r__idxS777 >= _M0L4colsS719) {
                  _M0L10r__clampedS778 = _M0L4colsS719 - 1;
                } else {
                  _M0L10r__clampedS778 = _M0L6r__idxS777;
                }
                #line 203 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L3tmpS779
                = _M0MPC15array5Array2atGiE(_M0L9post__idxS769, _M0L1kS775);
                #line 204 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS1806
                = _M0MPC15array5Array2atGiE(_M0L9post__idxS769, _M0L10r__clampedS778);
                #line 204 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array3setGiE(_M0L9post__idxS769, _M0L1kS775, _M0L6_2atmpS1806);
                #line 205 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array3setGiE(_M0L9post__idxS769, _M0L10r__clampedS778, _M0L3tmpS779);
                _M0L6_2atmpS1811 = _M0L1kS775 + 1;
                _M0L1kS775 = _M0L6_2atmpS1811;
                continue;
              }
              break;
            }
            _M0L7_2abindS781 = 0;
            _M0L1kS782 = _M0L7_2abindS781;
            while (1) {
              if (_M0L1kS782 < _M0L7n__dropS773) {
                struct _M0TPB5ArrayGfE* _M0L6_2atmpS1812;
                int32_t _M0L6_2atmpS1813;
                int32_t _M0L6_2atmpS1814;
                #line 208 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS1812
                = _M0MPC15array5Array2atGRPB5ArrayGfEE(_M0L5denseS714, _M0L1iS768);
                #line 208 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS1813
                = _M0MPC15array5Array2atGiE(_M0L9post__idxS769, _M0L1kS782);
                #line 208 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array3setGfE(_M0L6_2atmpS1812, _M0L6_2atmpS1813, 0x0p+0f);
                moonbit_decref(_M0L6_2atmpS1812);
                _M0L6_2atmpS1814 = _M0L1kS782 + 1;
                _M0L1kS782 = _M0L6_2atmpS1814;
                continue;
              } else {
                moonbit_decref(_M0L9post__idxS769);
              }
              break;
            }
            _M0L6_2atmpS1816 = _M0L1iS768 + 1;
            _M0L1iS768 = _M0L6_2atmpS1816;
            continue;
          }
          break;
        }
      } else if (_M0L7n__keepS766 == 0) {
        int32_t _M0L7_2abindS785 = 0;
        int32_t _M0L1iS786 = _M0L7_2abindS785;
        while (1) {
          if (_M0L1iS786 < _M0L4rowsS715) {
            int32_t _M0L7_2abindS787 = 0;
            int32_t _M0L1jS788 = _M0L7_2abindS787;
            int32_t _M0L6_2atmpS1819;
            while (1) {
              if (_M0L1jS788 < _M0L4colsS719) {
                struct _M0TPB5ArrayGfE* _M0L6_2atmpS1817;
                int32_t _M0L6_2atmpS1818;
                #line 214 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS1817
                = _M0MPC15array5Array2atGRPB5ArrayGfEE(_M0L5denseS714, _M0L1iS786);
                #line 214 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array3setGfE(_M0L6_2atmpS1817, _M0L1jS788, 0x0p+0f);
                moonbit_decref(_M0L6_2atmpS1817);
                _M0L6_2atmpS1818 = _M0L1jS788 + 1;
                _M0L1jS788 = _M0L6_2atmpS1818;
                continue;
              }
              break;
            }
            _M0L6_2atmpS1819 = _M0L1iS786 + 1;
            _M0L1iS786 = _M0L6_2atmpS1819;
            continue;
          }
          break;
        }
      }
      break;
    }
  }
  _M0L6_2atmpS1829 = _M0L4rowsS715 + 1;
  #line 221 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  _M0L6rowptrS791 = _M0MPC15array5Array4makeGiE(_M0L6_2atmpS1829, 0);
  _M0L6_2atmpS1828 = (int32_t*)moonbit_empty_int32_array;
  _M0L6colptrS792
  = (struct _M0TPB5ArrayGiE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGiE));
  Moonbit_object_header(_M0L6colptrS792)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 3, 0);
  _M0L6colptrS792->$0 = _M0L6_2atmpS1828;
  _M0L6colptrS792->$1 = 0;
  _M0L6_2atmpS1827 = moonbit_empty_float_array;
  _M0L4valsS793
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_M0L4valsS793)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 0, 0);
  _M0L4valsS793->$0 = _M0L6_2atmpS1827;
  _M0L4valsS793->$1 = 0;
  _M0L7_2abindS794 = 0;
  _M0L1iS795 = _M0L7_2abindS794;
  while (1) {
    if (_M0L1iS795 < _M0L4rowsS715) {
      int32_t _M0L6_2atmpS1822;
      int32_t _M0L7_2abindS796;
      int32_t _M0L1jS797;
      int32_t _M0L6_2atmpS1825;
      #line 225 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
      _M0L6_2atmpS1822 = _M0MPC15array5Array6lengthGfE(_M0L4valsS793);
      #line 225 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
      _M0MPC15array5Array3setGiE(_M0L6rowptrS791, _M0L1iS795, _M0L6_2atmpS1822);
      _M0L7_2abindS796 = 0;
      _M0L1jS797 = _M0L7_2abindS796;
      while (1) {
        if (_M0L1jS797 < _M0L4colsS719) {
          struct _M0TPB5ArrayGfE* _M0L6_2atmpS1823;
          float _M0L1vS798;
          int32_t _M0L6_2atmpS1824;
          #line 227 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
          _M0L6_2atmpS1823
          = _M0MPC15array5Array2atGRPB5ArrayGfEE(_M0L5denseS714, _M0L1iS795);
          #line 227 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
          _M0L1vS798
          = _M0MPC15array5Array2atGfE(_M0L6_2atmpS1823, _M0L1jS797);
          moonbit_decref(_M0L6_2atmpS1823);
          if (_M0L1vS798 != 0x0p+0f) {
            #line 229 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
            _M0MPC15array5Array4pushGiE(_M0L6colptrS792, _M0L1jS797);
            #line 230 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
            _M0MPC15array5Array4pushGfE(_M0L4valsS793, _M0L1vS798);
          }
          _M0L6_2atmpS1824 = _M0L1jS797 + 1;
          _M0L1jS797 = _M0L6_2atmpS1824;
          continue;
        }
        break;
      }
      _M0L6_2atmpS1825 = _M0L1iS795 + 1;
      _M0L1iS795 = _M0L6_2atmpS1825;
      continue;
    } else {
      moonbit_decref(_M0L5denseS714);
    }
    break;
  }
  #line 234 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  _M0L6_2atmpS1826 = _M0MPC15array5Array6lengthGfE(_M0L4valsS793);
  #line 234 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  _M0MPC15array5Array3setGiE(_M0L6rowptrS791, _M0L4rowsS715, _M0L6_2atmpS1826);
  _block_2154
  = (struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR));
  Moonbit_object_header(_block_2154)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 45, 0);
  _block_2154->$0 = _M0L4rowsS715;
  _block_2154->$1 = _M0L4colsS719;
  _block_2154->$2 = _M0L6rowptrS791;
  _block_2154->$3 = _M0L6colptrS792;
  _block_2154->$4 = _M0L4valsS793;
  return _block_2154;
}

struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0MP26RiantR8snn__mbt7Xoshiro3new(
  uint64_t _M0L4seedS712
) {
  struct _M0TUmmmmE* _M0L1sS711;
  uint64_t _M0L6_2atmpS1780;
  struct _M0TUmmmmE* _M0L1tS713;
  uint64_t _M0L6_2atmpS1776;
  uint64_t _M0L6_2atmpS1777;
  uint64_t _M0L6_2atmpS1778;
  uint64_t _M0L6_2atmpS1779;
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _block_2155;
  #line 48 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  #line 49 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L1sS711 = _M0FP26RiantR8snn__mbt10splitmix64(_M0L4seedS712);
  _M0L6_2atmpS1780 = _M0L1sS711->$3;
  #line 50 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L1tS713 = _M0FP26RiantR8snn__mbt10splitmix64(_M0L6_2atmpS1780);
  _M0L6_2atmpS1776 = _M0L1sS711->$0;
  _M0L6_2atmpS1777 = _M0L1sS711->$1;
  _M0L6_2atmpS1778 = _M0L1sS711->$2;
  moonbit_decref(_M0L1sS711);
  _M0L6_2atmpS1779 = _M0L1tS713->$0;
  moonbit_decref(_M0L1tS713);
  _block_2155
  = (struct _M0TP26RiantR8snn__mbt7Xoshiro*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt7Xoshiro));
  Moonbit_object_header(_block_2155)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_2155->$0 = _M0L6_2atmpS1776;
  _block_2155->$1 = _M0L6_2atmpS1777;
  _block_2155->$2 = _M0L6_2atmpS1778;
  _block_2155->$3 = _M0L6_2atmpS1779;
  return _block_2155;
}

struct _M0TUmmmmE* _M0FP26RiantR8snn__mbt10splitmix64(uint64_t _M0L4seedS703) {
  uint64_t _M0L2s1S702;
  uint64_t _M0L2z1S704;
  uint64_t _M0L2s2S705;
  uint64_t _M0L2z2S706;
  uint64_t _M0L2s3S707;
  uint64_t _M0L2z3S708;
  uint64_t _M0L2s4S709;
  uint64_t _M0L2z4S710;
  struct _M0TUmmmmE* _block_2156;
  #line 62 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2s1S702 = _M0L4seedS703 + 11400714819323198485ull;
  #line 64 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2z1S704 = _M0FP26RiantR8snn__mbt5mix64(_M0L2s1S702);
  _M0L2s2S705 = _M0L2s1S702 + 11400714819323198485ull;
  #line 66 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2z2S706 = _M0FP26RiantR8snn__mbt5mix64(_M0L2s2S705);
  _M0L2s3S707 = _M0L2s2S705 + 11400714819323198485ull;
  #line 68 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2z3S708 = _M0FP26RiantR8snn__mbt5mix64(_M0L2s3S707);
  _M0L2s4S709 = _M0L2s3S707 + 11400714819323198485ull;
  #line 70 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2z4S710 = _M0FP26RiantR8snn__mbt5mix64(_M0L2s4S709);
  _block_2156 = (struct _M0TUmmmmE*)moonbit_malloc(sizeof(struct _M0TUmmmmE));
  Moonbit_object_header(_block_2156)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_2156->$0 = _M0L2z1S704;
  _block_2156->$1 = _M0L2z2S706;
  _block_2156->$2 = _M0L2z3S708;
  _block_2156->$3 = _M0L2z4S710;
  return _block_2156;
}

uint64_t _M0FP26RiantR8snn__mbt5mix64(uint64_t _M0L1zS700) {
  uint64_t _M0L6_2atmpS1775;
  uint64_t _M0L6_2atmpS1774;
  uint64_t _M0L1zS699;
  uint64_t _M0L6_2atmpS1773;
  uint64_t _M0L6_2atmpS1772;
  uint64_t _M0L1zS701;
  uint64_t _M0L6_2atmpS1771;
  #line 75 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L6_2atmpS1775 = _M0L1zS700 >> 30;
  _M0L6_2atmpS1774 = _M0L1zS700 ^ _M0L6_2atmpS1775;
  _M0L1zS699 = _M0L6_2atmpS1774 * 13787848793156543929ull;
  _M0L6_2atmpS1773 = _M0L1zS699 >> 27;
  _M0L6_2atmpS1772 = _M0L1zS699 ^ _M0L6_2atmpS1773;
  _M0L1zS701 = _M0L6_2atmpS1772 * 10723151780598845931ull;
  _M0L6_2atmpS1771 = _M0L1zS701 >> 31;
  return _M0L1zS701 ^ _M0L6_2atmpS1771;
}

struct _M0TUddE* _M0FP26RiantR8snn__mbt11box__muller(
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS694
) {
  double _M0L2u1S693;
  double _M0L8u1__safeS695;
  double _M0L2u2S696;
  double _M0L6_2atmpS1770;
  double _M0L6_2atmpS1769;
  double _M0L1rS697;
  double _M0L5thetaS698;
  double _M0L6_2atmpS1768;
  double _M0L6_2atmpS1765;
  double _M0L6_2atmpS1767;
  double _M0L6_2atmpS1766;
  struct _M0TUddE* _block_2157;
  #line 126 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  #line 127 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  _M0L2u1S693 = _M0FP26RiantR8snn__mbt9next__f64(_M0L3rngS694);
  if (_M0L2u1S693 < 0x1.203af9ee75616p-50) {
    _M0L8u1__safeS695 = 0x1.203af9ee75616p-50;
  } else {
    _M0L8u1__safeS695 = _M0L2u1S693;
  }
  #line 130 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  _M0L2u2S696 = _M0FP26RiantR8snn__mbt9next__f64(_M0L3rngS694);
  #line 131 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  _M0L6_2atmpS1770 = _M0FPC14math2ln(_M0L8u1__safeS695);
  _M0L6_2atmpS1769 = -0x1p+1 * _M0L6_2atmpS1770;
  #line 131 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  _M0L1rS697 = sqrt(_M0L6_2atmpS1769);
  _M0L5thetaS698 = 0x1.921fb54442d18p+2 * _M0L2u2S696;
  #line 133 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  _M0L6_2atmpS1768 = _M0FPC14math3cos(_M0L5thetaS698);
  _M0L6_2atmpS1765 = _M0L1rS697 * _M0L6_2atmpS1768;
  #line 133 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  _M0L6_2atmpS1767 = _M0FPC14math3sin(_M0L5thetaS698);
  _M0L6_2atmpS1766 = _M0L1rS697 * _M0L6_2atmpS1767;
  _block_2157 = (struct _M0TUddE*)moonbit_malloc(sizeof(struct _M0TUddE));
  Moonbit_object_header(_block_2157)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_2157->$0 = _M0L6_2atmpS1765;
  _block_2157->$1 = _M0L6_2atmpS1766;
  return _block_2157;
}

double _M0FP26RiantR8snn__mbt9next__f64(
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L1rS691
) {
  uint64_t _M0L1uS690;
  uint64_t _M0L4bitsS692;
  double _M0L6_2atmpS1764;
  #line 118 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  #line 119 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L1uS690 = _M0FP26RiantR8snn__mbt9next__u64(_M0L1rS691);
  _M0L4bitsS692 = _M0L1uS690 >> 11;
  _M0L6_2atmpS1764 = (double)_M0L4bitsS692;
  return _M0L6_2atmpS1764 * 0x1p-53;
}

float _M0FP26RiantR8snn__mbt9next__f32(
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L1rS688
) {
  uint32_t _M0L1uS687;
  uint32_t _M0L4bitsS689;
  double _M0L6_2atmpS1763;
  double _M0L6_2atmpS1762;
  #line 128 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  #line 129 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L1uS687 = _M0FP26RiantR8snn__mbt9next__u32(_M0L1rS688);
  _M0L4bitsS689 = _M0L1uS687 >> 8;
  _M0L6_2atmpS1763 = (double)_M0L4bitsS689;
  _M0L6_2atmpS1762 = _M0L6_2atmpS1763 * 0x1p-24;
  return (float)_M0L6_2atmpS1762;
}

uint32_t _M0FP26RiantR8snn__mbt9next__u32(
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L1rS686
) {
  uint64_t _M0L1uS685;
  uint64_t _M0L6_2atmpS1761;
  #line 110 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  #line 111 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L1uS685 = _M0FP26RiantR8snn__mbt9next__u64(_M0L1rS686);
  _M0L6_2atmpS1761 = _M0L1uS685 >> 32;
  return (uint32_t)_M0L6_2atmpS1761;
}

uint64_t _M0FP26RiantR8snn__mbt9next__u64(
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L1rS678
) {
  uint64_t _M0L2s0S677;
  uint64_t _M0L2s1S679;
  uint64_t _M0L2s2S680;
  uint64_t _M0L2s3S681;
  uint64_t _M0L3tmpS682;
  uint64_t _M0L6_2atmpS1760;
  uint64_t _M0L3resS683;
  uint64_t _M0L1tS684;
  uint64_t _M0L6_2atmpS1750;
  uint64_t _M0L6_2atmpS1751;
  uint64_t _M0L2s2S1753;
  uint64_t _M0L6_2atmpS1752;
  uint64_t _M0L2s3S1755;
  uint64_t _M0L6_2atmpS1754;
  uint64_t _M0L2s2S1757;
  uint64_t _M0L6_2atmpS1756;
  uint64_t _M0L2s3S1759;
  uint64_t _M0L6_2atmpS1758;
  #line 90 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2s0S677 = _M0L1rS678->$0;
  _M0L2s1S679 = _M0L1rS678->$1;
  _M0L2s2S680 = _M0L1rS678->$2;
  _M0L2s3S681 = _M0L1rS678->$3;
  _M0L3tmpS682 = _M0L2s0S677 + _M0L2s3S681;
  #line 96 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L6_2atmpS1760 = _M0FP26RiantR8snn__mbt4rotl(_M0L3tmpS682, 23);
  _M0L3resS683 = _M0L6_2atmpS1760 + _M0L2s0S677;
  _M0L1tS684 = _M0L2s1S679 << 17;
  _M0L6_2atmpS1750 = _M0L2s2S680 ^ _M0L2s0S677;
  _M0L1rS678->$2 = _M0L6_2atmpS1750;
  _M0L6_2atmpS1751 = _M0L2s3S681 ^ _M0L2s1S679;
  _M0L1rS678->$3 = _M0L6_2atmpS1751;
  _M0L2s2S1753 = _M0L1rS678->$2;
  _M0L6_2atmpS1752 = _M0L2s1S679 ^ _M0L2s2S1753;
  _M0L1rS678->$1 = _M0L6_2atmpS1752;
  _M0L2s3S1755 = _M0L1rS678->$3;
  _M0L6_2atmpS1754 = _M0L2s0S677 ^ _M0L2s3S1755;
  _M0L1rS678->$0 = _M0L6_2atmpS1754;
  _M0L2s2S1757 = _M0L1rS678->$2;
  _M0L6_2atmpS1756 = _M0L2s2S1757 ^ _M0L1tS684;
  _M0L1rS678->$2 = _M0L6_2atmpS1756;
  _M0L2s3S1759 = _M0L1rS678->$3;
  #line 103 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L6_2atmpS1758 = _M0FP26RiantR8snn__mbt4rotl(_M0L2s3S1759, 45);
  _M0L1rS678->$3 = _M0L6_2atmpS1758;
  return _M0L3resS683;
}

uint64_t _M0FP26RiantR8snn__mbt4rotl(uint64_t _M0L1xS675, int32_t _M0L1kS676) {
  uint64_t _M0L6_2atmpS1747;
  int32_t _M0L6_2atmpS1749;
  uint64_t _M0L6_2atmpS1748;
  #line 83 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L6_2atmpS1747 = _M0L1xS675 << (_M0L1kS676 & 63);
  _M0L6_2atmpS1749 = 64 - _M0L1kS676;
  _M0L6_2atmpS1748 = _M0L1xS675 >> (_M0L6_2atmpS1749 & 63);
  return _M0L6_2atmpS1747 | _M0L6_2atmpS1748;
}

int32_t _M0MP26RiantR8snn__mbt7Monitor13count__spikes(
  struct _M0TP26RiantR8snn__mbt7Monitor* _M0L1mS668
) {
  struct _M0TPB5ArrayGfE* _M0L4dataS1746;
  int32_t _M0L1nS667;
  struct _M0TPB8MutLocalGiE* _M0L5countS669;
  struct _M0TPB8MutLocalGfE* _M0L4prevS670;
  int32_t _M0L7_2abindS671;
  int32_t _M0L1iS672;
  int32_t _result_2159;
  #line 17 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\vecplot.mbt"
  _M0L4dataS1746 = _M0L1mS668->$2;
  #line 18 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\vecplot.mbt"
  _M0L1nS667 = _M0MPC15array5Array6lengthGfE(_M0L4dataS1746);
  if (_M0L1nS667 == 0) {
    return 0;
  }
  _M0L5countS669
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L5countS669)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L5countS669->$0 = 0;
  _M0L4prevS670
  = (struct _M0TPB8MutLocalGfE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGfE));
  Moonbit_object_header(_M0L4prevS670)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L4prevS670->$0 = 0x0p+0f;
  _M0L7_2abindS671 = 0;
  _M0L1iS672 = _M0L7_2abindS671;
  while (1) {
    if (_M0L1iS672 < _M0L1nS667) {
      struct _M0TPB5ArrayGfE* _M0L4dataS1744 = _M0L1mS668->$2;
      float _M0L3curS673;
      float _M0L3valS1741;
      int32_t _M0L6_2atmpS1745;
      #line 25 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\vecplot.mbt"
      _M0L3curS673 = _M0MPC15array5Array2atGfE(_M0L4dataS1744, _M0L1iS672);
      _M0L3valS1741 = _M0L4prevS670->$0;
      if (_M0L3valS1741 < 0x1p-1f && _M0L3curS673 >= 0x1p-1f) {
        int32_t _M0L3valS1743 = _M0L5countS669->$0;
        int32_t _M0L6_2atmpS1742 = _M0L3valS1743 + 1;
        _M0L5countS669->$0 = _M0L6_2atmpS1742;
      }
      _M0L4prevS670->$0 = _M0L3curS673;
      _M0L6_2atmpS1745 = _M0L1iS672 + 1;
      _M0L1iS672 = _M0L6_2atmpS1745;
      continue;
    } else {
      moonbit_decref(_M0L4prevS670);
    }
    break;
  }
  _result_2159 = _M0L5countS669->$0;
  moonbit_decref(_M0L5countS669);
  return _result_2159;
}

double _M0FPC14math2ln(double _M0L1xS653) {
  struct _M0TUdiE* _M0L7_2abindS654;
  double _M0L5_2af1S655;
  int32_t _M0L5_2akiS656;
  double _M0L1fS658;
  double _M0L1kS659;
  double _M0L6_2atmpS1734;
  double _M0L1sS660;
  double _M0L2s2S661;
  double _M0L2s4S662;
  double _M0L6_2atmpS1733;
  double _M0L6_2atmpS1732;
  double _M0L6_2atmpS1731;
  double _M0L6_2atmpS1730;
  double _M0L6_2atmpS1729;
  double _M0L6_2atmpS1728;
  double _M0L2t1S663;
  double _M0L6_2atmpS1727;
  double _M0L6_2atmpS1726;
  double _M0L6_2atmpS1725;
  double _M0L6_2atmpS1724;
  double _M0L2t2S664;
  double _M0L1rS665;
  double _M0L6_2atmpS1723;
  double _M0L4hfsqS666;
  double _M0L6_2atmpS1716;
  double _M0L6_2atmpS1722;
  double _M0L6_2atmpS1720;
  double _M0L6_2atmpS1721;
  double _M0L6_2atmpS1719;
  double _M0L6_2atmpS1718;
  double _M0L6_2atmpS1717;
  #line 55 "C:\\Users\\31379\\.moon\\lib\\core\\math\\log_double_nonjs.mbt"
  if (_M0L1xS653 < 0x0p+0) {
    return _M0FPC16double14not__a__number;
  } else {
    #line 65 "C:\\Users\\31379\\.moon\\lib\\core\\math\\log_double_nonjs.mbt"
    #line 65 "C:\\Users\\31379\\.moon\\lib\\core\\math\\log_double_nonjs.mbt"
    if (
      _M0MPC16double6Double7is__nan(_M0L1xS653)
      || _M0MPC16double6Double7is__inf(_M0L1xS653)
    ) {
      return _M0L1xS653;
    } else if (_M0L1xS653 == 0x0p+0) {
      return _M0FPC16double13neg__infinity;
    }
  }
  #line 70 "C:\\Users\\31379\\.moon\\lib\\core\\math\\log_double_nonjs.mbt"
  _M0L7_2abindS654 = _M0FPC14math5frexp(_M0L1xS653);
  _M0L5_2af1S655 = _M0L7_2abindS654->$0;
  _M0L5_2akiS656 = _M0L7_2abindS654->$1;
  moonbit_decref(_M0L7_2abindS654);
  if (_M0L5_2af1S655 < 0x1.6a09e667f3bcdp-1) {
    double _M0L6_2atmpS1738 = _M0L5_2af1S655 * 0x1p+1;
    double _M0L6_2atmpS1735 = _M0L6_2atmpS1738 - 0x1p+0;
    int32_t _M0L6_2atmpS1737 = _M0L5_2akiS656 - 1;
    double _M0L6_2atmpS1736 = (double)_M0L6_2atmpS1737;
    _M0L1fS658 = _M0L6_2atmpS1735;
    _M0L1kS659 = _M0L6_2atmpS1736;
    goto join_657;
  } else {
    double _M0L6_2atmpS1739 = _M0L5_2af1S655 - 0x1p+0;
    double _M0L6_2atmpS1740 = (double)_M0L5_2akiS656;
    _M0L1fS658 = _M0L6_2atmpS1739;
    _M0L1kS659 = _M0L6_2atmpS1740;
    goto join_657;
  }
  join_657:;
  _M0L6_2atmpS1734 = 0x1p+1 + _M0L1fS658;
  _M0L1sS660 = _M0L1fS658 / _M0L6_2atmpS1734;
  _M0L2s2S661 = _M0L1sS660 * _M0L1sS660;
  _M0L2s4S662 = _M0L2s2S661 * _M0L2s2S661;
  _M0L6_2atmpS1733 = _M0L2s4S662 * 0x1.2f112df3e5244p-3;
  _M0L6_2atmpS1732 = 0x1.7466496cb03dep-3 + _M0L6_2atmpS1733;
  _M0L6_2atmpS1731 = _M0L2s4S662 * _M0L6_2atmpS1732;
  _M0L6_2atmpS1730 = 0x1.2492494229359p-2 + _M0L6_2atmpS1731;
  _M0L6_2atmpS1729 = _M0L2s4S662 * _M0L6_2atmpS1730;
  _M0L6_2atmpS1728 = 0x1.5555555555593p-1 + _M0L6_2atmpS1729;
  _M0L2t1S663 = _M0L2s2S661 * _M0L6_2atmpS1728;
  _M0L6_2atmpS1727 = _M0L2s4S662 * 0x1.39a09d078c69fp-3;
  _M0L6_2atmpS1726 = 0x1.c71c51d8e78afp-3 + _M0L6_2atmpS1727;
  _M0L6_2atmpS1725 = _M0L2s4S662 * _M0L6_2atmpS1726;
  _M0L6_2atmpS1724 = 0x1.999999997fa04p-2 + _M0L6_2atmpS1725;
  _M0L2t2S664 = _M0L2s4S662 * _M0L6_2atmpS1724;
  _M0L1rS665 = _M0L2t1S663 + _M0L2t2S664;
  _M0L6_2atmpS1723 = 0x1p-1 * _M0L1fS658;
  _M0L4hfsqS666 = _M0L6_2atmpS1723 * _M0L1fS658;
  _M0L6_2atmpS1716 = _M0L1kS659 * 0x1.62e42feep-1;
  _M0L6_2atmpS1722 = _M0L4hfsqS666 + _M0L1rS665;
  _M0L6_2atmpS1720 = _M0L1sS660 * _M0L6_2atmpS1722;
  _M0L6_2atmpS1721 = _M0L1kS659 * 0x1.a39ef35793c76p-33;
  _M0L6_2atmpS1719 = _M0L6_2atmpS1720 + _M0L6_2atmpS1721;
  _M0L6_2atmpS1718 = _M0L4hfsqS666 - _M0L6_2atmpS1719;
  _M0L6_2atmpS1717 = _M0L6_2atmpS1718 - _M0L1fS658;
  return _M0L6_2atmpS1716 - _M0L6_2atmpS1717;
}

struct _M0TUdiE* _M0FPC14math5frexp(double _M0L1fS646) {
  struct _M0TUdiE* _M0L7_2abindS647;
  double _M0L10_2anorm__fS648;
  int32_t _M0L6_2aexpS649;
  uint64_t _M0L1uS650;
  uint64_t _M0L6_2atmpS1715;
  uint64_t _M0L6_2atmpS1714;
  int32_t _M0L6_2atmpS1713;
  int32_t _M0L6_2atmpS1712;
  int32_t _M0L3expS651;
  uint64_t _M0L6_2atmpS1711;
  uint64_t _M0L6_2atmpS1710;
  uint64_t _M0L6_2atmpS1709;
  double _M0L4fracS652;
  struct _M0TUdiE* _block_2162;
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\math\\utils.mbt"
  #line 134 "C:\\Users\\31379\\.moon\\lib\\core\\math\\utils.mbt"
  #line 134 "C:\\Users\\31379\\.moon\\lib\\core\\math\\utils.mbt"
  if (
    _M0L1fS646 == 0x0p+0
    || _M0MPC16double6Double7is__inf(_M0L1fS646)
    || _M0MPC16double6Double7is__nan(_M0L1fS646)
  ) {
    struct _M0TUdiE* _block_2161 =
      (struct _M0TUdiE*)moonbit_malloc(sizeof(struct _M0TUdiE));
    Moonbit_object_header(_block_2161)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
    _block_2161->$0 = _M0L1fS646;
    _block_2161->$1 = 0;
    return _block_2161;
  }
  #line 137 "C:\\Users\\31379\\.moon\\lib\\core\\math\\utils.mbt"
  _M0L7_2abindS647 = _M0FPC14math9normalize(_M0L1fS646);
  _M0L10_2anorm__fS648 = _M0L7_2abindS647->$0;
  _M0L6_2aexpS649 = _M0L7_2abindS647->$1;
  moonbit_decref(_M0L7_2abindS647);
  _M0L1uS650 = *(int64_t*)&_M0L10_2anorm__fS648;
  _M0L6_2atmpS1715 = _M0L1uS650 >> 52;
  _M0L6_2atmpS1714 = _M0L6_2atmpS1715 & 2047ull;
  _M0L6_2atmpS1713 = (int32_t)_M0L6_2atmpS1714;
  _M0L6_2atmpS1712 = _M0L6_2aexpS649 + _M0L6_2atmpS1713;
  _M0L3expS651 = _M0L6_2atmpS1712 - 1022;
  _M0L6_2atmpS1711 = ~9218868437227405312ull;
  _M0L6_2atmpS1710 = _M0L1uS650 & _M0L6_2atmpS1711;
  _M0L6_2atmpS1709 = _M0L6_2atmpS1710 | 4602678819172646912ull;
  _M0L4fracS652 = *(double*)&_M0L6_2atmpS1709;
  _block_2162 = (struct _M0TUdiE*)moonbit_malloc(sizeof(struct _M0TUdiE));
  Moonbit_object_header(_block_2162)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_2162->$0 = _M0L4fracS652;
  _block_2162->$1 = _M0L3expS651;
  return _block_2162;
}

struct _M0TUdiE* _M0FPC14math9normalize(double _M0L1fS645) {
  double _M0L6_2atmpS1706;
  struct _M0TUdiE* _block_2164;
  #line 125 "C:\\Users\\31379\\.moon\\lib\\core\\math\\utils.mbt"
  #line 126 "C:\\Users\\31379\\.moon\\lib\\core\\math\\utils.mbt"
  _M0L6_2atmpS1706 = fabs(_M0L1fS645);
  if (_M0L6_2atmpS1706 < _M0FPC16double13min__positive) {
    double _M0L6_2atmpS1708 = (double)4503599627370496ll;
    double _M0L6_2atmpS1707 = _M0L1fS645 * _M0L6_2atmpS1708;
    struct _M0TUdiE* _block_2163 =
      (struct _M0TUdiE*)moonbit_malloc(sizeof(struct _M0TUdiE));
    Moonbit_object_header(_block_2163)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
    _block_2163->$0 = _M0L6_2atmpS1707;
    _block_2163->$1 = -52;
    return _block_2163;
  }
  _block_2164 = (struct _M0TUdiE*)moonbit_malloc(sizeof(struct _M0TUdiE));
  Moonbit_object_header(_block_2164)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_2164->$0 = _M0L1fS645;
  _block_2164->$1 = 0;
  return _block_2164;
}

moonbit_string_t _M0IPC15float5FloatPB4Show10to__string(float _M0L4selfS644) {
  double _M0L6_2atmpS1705;
  #line 16 "C:\\Users\\31379\\.moon\\lib\\core\\float\\methods.mbt"
  _M0L6_2atmpS1705 = (double)_M0L4selfS644;
  #line 17 "C:\\Users\\31379\\.moon\\lib\\core\\float\\methods.mbt"
  return _M0MPC16double6Double10to__string(_M0L6_2atmpS1705);
}

int32_t _M0MPC15float5Float7to__int(float _M0L4selfS643) {
  #line 43 "C:\\Users\\31379\\.moon\\lib\\core\\float\\to_int.mbt"
  if (_M0L4selfS643 != _M0L4selfS643) {
    return 0;
  } else if (_M0L4selfS643 >= 0x1.fffffffcp+30f) {
    return 2147483647;
  } else if (_M0L4selfS643 <= -0x1p+31f) {
    return (int32_t)0x80000000;
  } else {
    return (int32_t)_M0L4selfS643;
  }
}

struct _M0TPB5ArrayGfE* _M0MPC15array5Array4makeGfE(
  int32_t _M0L3lenS624,
  float _M0L4elemS626
) {
  struct _M0TPB5ArrayGfE* _M0L3arrS623;
  int32_t _M0L1iS625;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3arrS623 = _M0MPC15array5Array20unsafe__make__uninitGfE(_M0L3lenS624);
  _M0L1iS625 = 0;
  while (1) {
    if (_M0L1iS625 < _M0L3lenS624) {
      float* _M0L3bufS1697 = _M0L3arrS623->$0;
      int32_t _M0L6_2atmpS1698;
      _M0L3bufS1697[_M0L1iS625] = _M0L4elemS626;
      _M0L6_2atmpS1698 = _M0L1iS625 + 1;
      _M0L1iS625 = _M0L6_2atmpS1698;
      continue;
    }
    break;
  }
  return _M0L3arrS623;
}

struct _M0TPB5ArrayGbE* _M0MPC15array5Array4makeGbE(
  int32_t _M0L3lenS629,
  int32_t _M0L4elemS631
) {
  struct _M0TPB5ArrayGbE* _M0L3arrS628;
  int32_t _M0L1iS630;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3arrS628 = _M0MPC15array5Array20unsafe__make__uninitGbE(_M0L3lenS629);
  _M0L1iS630 = 0;
  while (1) {
    if (_M0L1iS630 < _M0L3lenS629) {
      uint8_t* _M0L3bufS1699 = _M0L3arrS628->$0;
      int32_t _M0L6_2atmpS1700;
      _M0L3bufS1699[_M0L1iS630] = _M0L4elemS631;
      _M0L6_2atmpS1700 = _M0L1iS630 + 1;
      _M0L1iS630 = _M0L6_2atmpS1700;
      continue;
    }
    break;
  }
  return _M0L3arrS628;
}

struct _M0TPB5ArrayGiE* _M0MPC15array5Array4makeGiE(
  int32_t _M0L3lenS634,
  int32_t _M0L4elemS636
) {
  struct _M0TPB5ArrayGiE* _M0L3arrS633;
  int32_t _M0L1iS635;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3arrS633 = _M0MPC15array5Array20unsafe__make__uninitGiE(_M0L3lenS634);
  _M0L1iS635 = 0;
  while (1) {
    if (_M0L1iS635 < _M0L3lenS634) {
      int32_t* _M0L3bufS1701 = _M0L3arrS633->$0;
      int32_t _M0L6_2atmpS1702;
      _M0L3bufS1701[_M0L1iS635] = _M0L4elemS636;
      _M0L6_2atmpS1702 = _M0L1iS635 + 1;
      _M0L1iS635 = _M0L6_2atmpS1702;
      continue;
    }
    break;
  }
  return _M0L3arrS633;
}

struct _M0TPB5ArrayGRPB5ArrayGfEE* _M0MPC15array5Array4makeGRPB5ArrayGfEE(
  int32_t _M0L3lenS639,
  struct _M0TPB5ArrayGfE* _M0L4elemS641
) {
  struct _M0TPB5ArrayGRPB5ArrayGfEE* _M0L3arrS638;
  int32_t _M0L1iS640;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3arrS638
  = _M0MPC15array5Array20unsafe__make__uninitGRPB5ArrayGfEE(_M0L3lenS639);
  _M0L1iS640 = 0;
  while (1) {
    if (_M0L1iS640 < _M0L3lenS639) {
      struct _M0TPB5ArrayGfE** _M0L3bufS1703 = _M0L3arrS638->$0;
      struct _M0TPB5ArrayGfE* _M0L6_2aoldS2106 =
        (struct _M0TPB5ArrayGfE*)_M0L3bufS1703[_M0L1iS640];
      int32_t _M0L6_2atmpS1704;
      moonbit_incref(_M0L4elemS641);
      if (_M0L6_2aoldS2106) {
        moonbit_decref(_M0L6_2aoldS2106);
      }
      _M0L3bufS1703[_M0L1iS640] = _M0L4elemS641;
      _M0L6_2atmpS1704 = _M0L1iS640 + 1;
      _M0L1iS640 = _M0L6_2atmpS1704;
      continue;
    } else {
      moonbit_decref(_M0L4elemS641);
    }
    break;
  }
  return _M0L3arrS638;
}

int32_t _M0MPC15array5Array3setGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS608,
  int32_t _M0L5indexS609,
  float _M0L5valueS610
) {
  int32_t _M0L3lenS607;
  #line 262 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS607 = _M0L4selfS608->$1;
  if (_M0L5indexS609 >= 0 && _M0L5indexS609 < _M0L3lenS607) {
    float* _M0L6_2atmpS1693;
    #line 268 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS1693 = _M0MPC15array5Array6bufferGfE(_M0L4selfS608);
    _M0L6_2atmpS1693[_M0L5indexS609] = _M0L5valueS610;
    moonbit_decref(_M0L6_2atmpS1693);
  } else {
    #line 267 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
  return 0;
}

int32_t _M0MPC15array5Array3setGbE(
  struct _M0TPB5ArrayGbE* _M0L4selfS612,
  int32_t _M0L5indexS613,
  int32_t _M0L5valueS614
) {
  int32_t _M0L3lenS611;
  #line 262 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS611 = _M0L4selfS612->$1;
  if (_M0L5indexS613 >= 0 && _M0L5indexS613 < _M0L3lenS611) {
    uint8_t* _M0L6_2atmpS1694;
    #line 268 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS1694 = _M0MPC15array5Array6bufferGbE(_M0L4selfS612);
    _M0L6_2atmpS1694[_M0L5indexS613] = _M0L5valueS614;
    moonbit_decref(_M0L6_2atmpS1694);
  } else {
    #line 267 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
  return 0;
}

int32_t _M0MPC15array5Array3setGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS616,
  int32_t _M0L5indexS617,
  int32_t _M0L5valueS618
) {
  int32_t _M0L3lenS615;
  #line 262 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS615 = _M0L4selfS616->$1;
  if (_M0L5indexS617 >= 0 && _M0L5indexS617 < _M0L3lenS615) {
    int32_t* _M0L6_2atmpS1695;
    #line 268 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS1695 = _M0MPC15array5Array6bufferGiE(_M0L4selfS616);
    _M0L6_2atmpS1695[_M0L5indexS617] = _M0L5valueS618;
    moonbit_decref(_M0L6_2atmpS1695);
  } else {
    #line 267 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
  return 0;
}

int32_t _M0MPC15array5Array3setGRPB5ArrayGfEE(
  struct _M0TPB5ArrayGRPB5ArrayGfEE* _M0L4selfS620,
  int32_t _M0L5indexS621,
  struct _M0TPB5ArrayGfE* _M0L5valueS622
) {
  int32_t _M0L3lenS619;
  #line 262 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS619 = _M0L4selfS620->$1;
  if (_M0L5indexS621 >= 0 && _M0L5indexS621 < _M0L3lenS619) {
    struct _M0TPB5ArrayGfE** _M0L6_2atmpS1696;
    struct _M0TPB5ArrayGfE* _M0L6_2aoldS2107;
    #line 268 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS1696
    = _M0MPC15array5Array6bufferGRPB5ArrayGfEE(_M0L4selfS620);
    _M0L6_2aoldS2107
    = (struct _M0TPB5ArrayGfE*)_M0L6_2atmpS1696[_M0L5indexS621];
    if (_M0L6_2aoldS2107) {
      moonbit_decref(_M0L6_2aoldS2107);
    }
    _M0L6_2atmpS1696[_M0L5indexS621] = _M0L5valueS622;
    moonbit_decref(_M0L6_2atmpS1696);
  } else {
    moonbit_decref(_M0L5valueS622);
    #line 267 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
  return 0;
}

int32_t _M0MPC15array5Array2atGbE(
  struct _M0TPB5ArrayGbE* _M0L4selfS596,
  int32_t _M0L5indexS597
) {
  int32_t _M0L3lenS595;
  #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS595 = _M0L4selfS596->$1;
  if (_M0L5indexS597 >= 0 && _M0L5indexS597 < _M0L3lenS595) {
    uint8_t* _M0L6_2atmpS1689;
    int32_t _result_2169;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS1689 = _M0MPC15array5Array6bufferGbE(_M0L4selfS596);
    _result_2169 = (int32_t)_M0L6_2atmpS1689[_M0L5indexS597];
    moonbit_decref(_M0L6_2atmpS1689);
    return _result_2169;
  } else {
    #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
}

int32_t _M0MPC15array5Array2atGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS599,
  int32_t _M0L5indexS600
) {
  int32_t _M0L3lenS598;
  #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS598 = _M0L4selfS599->$1;
  if (_M0L5indexS600 >= 0 && _M0L5indexS600 < _M0L3lenS598) {
    int32_t* _M0L6_2atmpS1690;
    int32_t _result_2170;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS1690 = _M0MPC15array5Array6bufferGiE(_M0L4selfS599);
    _result_2170 = (int32_t)_M0L6_2atmpS1690[_M0L5indexS600];
    moonbit_decref(_M0L6_2atmpS1690);
    return _result_2170;
  } else {
    #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
}

float _M0MPC15array5Array2atGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS602,
  int32_t _M0L5indexS603
) {
  int32_t _M0L3lenS601;
  #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS601 = _M0L4selfS602->$1;
  if (_M0L5indexS603 >= 0 && _M0L5indexS603 < _M0L3lenS601) {
    float* _M0L6_2atmpS1691;
    float _result_2171;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS1691 = _M0MPC15array5Array6bufferGfE(_M0L4selfS602);
    _result_2171 = (float)_M0L6_2atmpS1691[_M0L5indexS603];
    moonbit_decref(_M0L6_2atmpS1691);
    return _result_2171;
  } else {
    #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
}

struct _M0TPB5ArrayGfE* _M0MPC15array5Array2atGRPB5ArrayGfEE(
  struct _M0TPB5ArrayGRPB5ArrayGfEE* _M0L4selfS605,
  int32_t _M0L5indexS606
) {
  int32_t _M0L3lenS604;
  #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS604 = _M0L4selfS605->$1;
  if (_M0L5indexS606 >= 0 && _M0L5indexS606 < _M0L3lenS604) {
    struct _M0TPB5ArrayGfE** _M0L6_2atmpS1692;
    struct _M0TPB5ArrayGfE* _M0L6_2atmpS2108;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS1692
    = _M0MPC15array5Array6bufferGRPB5ArrayGfEE(_M0L4selfS605);
    _M0L6_2atmpS2108
    = (struct _M0TPB5ArrayGfE*)_M0L6_2atmpS1692[_M0L5indexS606];
    if (_M0L6_2atmpS2108) {
      moonbit_incref(_M0L6_2atmpS2108);
    }
    moonbit_decref(_M0L6_2atmpS1692);
    return _M0L6_2atmpS2108;
  } else {
    #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
}

int32_t _M0FPB7printlnGsE(moonbit_string_t _M0L5inputS594) {
  moonbit_string_t _M0L6_2atmpS1688;
  #line 36 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\console.mbt"
  #line 37 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\console.mbt"
  _M0L6_2atmpS1688 = _M0IPC16string6StringPB4Show10to__string(_M0L5inputS594);
  #line 37 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\console.mbt"
  moonbit_println(_M0L6_2atmpS1688);
  moonbit_decref(_M0L6_2atmpS1688);
  return 0;
}

moonbit_string_t _M0MPC16double6Double10to__string(double _M0L4selfS593) {
  #line 296 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double.mbt"
  #line 298 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double.mbt"
  return _M0FPB15ryu__to__string(_M0L4selfS593);
}

int32_t _M0MPC16double6Double7is__inf(double _M0L4selfS592) {
  #line 221 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double.mbt"
  return _M0L4selfS592 > _M0FPB18double__max__value
         || _M0L4selfS592 < _M0FPB18double__min__value;
}

int32_t _M0MPC16double6Double7is__nan(double _M0L4selfS591) {
  #line 196 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double.mbt"
  return _M0L4selfS591 != _M0L4selfS591;
}

moonbit_string_t _M0FPB15ryu__to__string(double _M0L3valS576) {
  uint64_t _M0L4bitsS579;
  uint64_t _M0L6_2atmpS1687;
  uint64_t _M0L6_2atmpS1686;
  int32_t _M0L8ieeeSignS580;
  uint64_t _M0L12ieeeMantissaS581;
  uint64_t _M0L6_2atmpS1685;
  uint64_t _M0L6_2atmpS1684;
  int32_t _M0L12ieeeExponentS582;
  struct _M0TPB17FloatingDecimal64* _M0L7_2abindS583;
  struct _M0TPB17FloatingDecimal64* _M0L1vS584;
  moonbit_string_t _result_2173;
  #line 668 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  if (_M0L3valS576 == 0x0p+0) {
    return (moonbit_string_t)moonbit_string_literal_4.data;
  }
  if (_M0L3valS576 >= -0x1p+53 && _M0L3valS576 <= 0x1p+53) {
    if (_M0L3valS576 >= -0x1p+31 && _M0L3valS576 <= 0x1.fffffffcp+30) {
      int32_t _M0L1iS577;
      double _M0L6_2atmpS1673;
      #line 683 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0L1iS577 = _M0MPC16double6Double7to__int(_M0L3valS576);
      _M0L6_2atmpS1673 = (double)_M0L1iS577;
      if (_M0L6_2atmpS1673 == _M0L3valS576) {
        #line 685 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        return _M0MPC13int3Int18to__string_2einner(_M0L1iS577, 10);
      }
    } else {
      int64_t _M0L1iS578;
      double _M0L6_2atmpS1674;
      #line 688 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0L1iS578 = _M0MPC16double6Double9to__int64(_M0L3valS576);
      _M0L6_2atmpS1674 = (double)_M0L1iS578;
      if (_M0L6_2atmpS1674 == _M0L3valS576) {
        #line 690 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        return _M0MPC15int645Int6418to__string_2einner(_M0L1iS578, 10);
      }
    }
  }
  _M0L4bitsS579 = *(int64_t*)&_M0L3valS576;
  _M0L6_2atmpS1687 = _M0L4bitsS579 >> 63;
  _M0L6_2atmpS1686 = _M0L6_2atmpS1687 & 1ull;
  _M0L8ieeeSignS580 = _M0L6_2atmpS1686 != 0ull;
  _M0L12ieeeMantissaS581 = _M0L4bitsS579 & 4503599627370495ull;
  _M0L6_2atmpS1685 = _M0L4bitsS579 >> 52;
  _M0L6_2atmpS1684 = _M0L6_2atmpS1685 & 2047ull;
  _M0L12ieeeExponentS582 = (int32_t)_M0L6_2atmpS1684;
  if (
    _M0L12ieeeExponentS582 == 2047
    || _M0L12ieeeExponentS582 == 0 && _M0L12ieeeMantissaS581 == 0ull
  ) {
    int32_t _M0L6_2atmpS1675 = _M0L12ieeeExponentS582 != 0;
    int32_t _M0L6_2atmpS1676 = _M0L12ieeeMantissaS581 != 0ull;
    #line 707 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    return _M0FPB18copy__special__str(_M0L8ieeeSignS580, _M0L6_2atmpS1675, _M0L6_2atmpS1676);
  }
  #line 709 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS583
  = _M0FPB15d2d__small__int(_M0L12ieeeMantissaS581, _M0L12ieeeExponentS582);
  if (_M0L7_2abindS583 == 0) {
    uint32_t _M0L6_2atmpS1677;
    if (_M0L7_2abindS583) {
      moonbit_decref(_M0L7_2abindS583);
    }
    _M0L6_2atmpS1677 = *(uint32_t*)&_M0L12ieeeExponentS582;
    #line 719 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L1vS584 = _M0FPB3d2d(_M0L12ieeeMantissaS581, _M0L6_2atmpS1677);
  } else {
    struct _M0TPB17FloatingDecimal64* _M0L7_2aSomeS585 = _M0L7_2abindS583;
    struct _M0TPB17FloatingDecimal64* _M0L4_2afS586 = _M0L7_2aSomeS585;
    struct _M0TPB17FloatingDecimal64* _M0L1xS587 = _M0L4_2afS586;
    while (1) {
      uint64_t _M0L8mantissaS1683 = _M0L1xS587->$0;
      uint64_t _M0L1qS588 = _M0L8mantissaS1683 / 10ull;
      uint64_t _M0L8mantissaS1681 = _M0L1xS587->$0;
      uint64_t _M0L6_2atmpS1682 = 10ull * _M0L1qS588;
      uint64_t _M0L1rS589 = _M0L8mantissaS1681 - _M0L6_2atmpS1682;
      int32_t _M0L8exponentS1680;
      int32_t _M0L6_2atmpS1679;
      struct _M0TPB17FloatingDecimal64* _M0L6_2atmpS1678;
      if (_M0L1rS589 != 0ull) {
        _M0L1vS584 = _M0L1xS587;
        break;
      }
      _M0L8exponentS1680 = _M0L1xS587->$1;
      moonbit_decref(_M0L1xS587);
      _M0L6_2atmpS1679 = _M0L8exponentS1680 + 1;
      _M0L6_2atmpS1678
      = (struct _M0TPB17FloatingDecimal64*)moonbit_malloc(sizeof(struct _M0TPB17FloatingDecimal64));
      Moonbit_object_header(_M0L6_2atmpS1678)->meta
      = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
      _M0L6_2atmpS1678->$0 = _M0L1qS588;
      _M0L6_2atmpS1678->$1 = _M0L6_2atmpS1679;
      _M0L1xS587 = _M0L6_2atmpS1678;
      continue;
      break;
    }
  }
  #line 721 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _result_2173 = _M0FPB9to__chars(_M0L1vS584, _M0L8ieeeSignS580);
  moonbit_decref(_M0L1vS584);
  return _result_2173;
}

struct _M0TPB17FloatingDecimal64* _M0FPB15d2d__small__int(
  uint64_t _M0L12ieeeMantissaS571,
  int32_t _M0L12ieeeExponentS573
) {
  uint64_t _M0L2m2S570;
  int32_t _M0L6_2atmpS1672;
  int32_t _M0L2e2S572;
  int32_t _M0L6_2atmpS1671;
  uint64_t _M0L6_2atmpS1670;
  uint64_t _M0L4maskS574;
  uint64_t _M0L8fractionS575;
  int32_t _M0L6_2atmpS1669;
  uint64_t _M0L6_2atmpS1668;
  struct _M0TPB17FloatingDecimal64* _M0L6_2atmpS1667;
  #line 637 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L2m2S570 = 4503599627370496ull | _M0L12ieeeMantissaS571;
  _M0L6_2atmpS1672 = _M0L12ieeeExponentS573 - 1023;
  _M0L2e2S572 = _M0L6_2atmpS1672 - 52;
  if (_M0L2e2S572 > 0) {
    return 0;
  }
  if (_M0L2e2S572 < -52) {
    return 0;
  }
  _M0L6_2atmpS1671 = -_M0L2e2S572;
  _M0L6_2atmpS1670 = 1ull << (_M0L6_2atmpS1671 & 63);
  _M0L4maskS574 = _M0L6_2atmpS1670 - 1ull;
  _M0L8fractionS575 = _M0L2m2S570 & _M0L4maskS574;
  if (_M0L8fractionS575 != 0ull) {
    return 0;
  }
  _M0L6_2atmpS1669 = -_M0L2e2S572;
  _M0L6_2atmpS1668 = _M0L2m2S570 >> (_M0L6_2atmpS1669 & 63);
  _M0L6_2atmpS1667
  = (struct _M0TPB17FloatingDecimal64*)moonbit_malloc(sizeof(struct _M0TPB17FloatingDecimal64));
  Moonbit_object_header(_M0L6_2atmpS1667)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L6_2atmpS1667->$0 = _M0L6_2atmpS1668;
  _M0L6_2atmpS1667->$1 = 0;
  return _M0L6_2atmpS1667;
}

moonbit_string_t _M0FPB9to__chars(
  struct _M0TPB17FloatingDecimal64* _M0L1vS538,
  int32_t _M0L4signS536
) {
  moonbit_bytes_t _M0L6resultS534;
  int32_t _M0Lm5indexS535;
  uint64_t _M0L6outputS537;
  int32_t _M0L7olengthS539;
  int32_t _M0L8exponentS1666;
  int32_t _M0L6_2atmpS1665;
  int32_t _M0Lm3expS540;
  int32_t _M0L6_2atmpS1664;
  int32_t _M0L6_2atmpS1662;
  int32_t _M0L18scientificNotationS541;
  #line 530 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6resultS534 = (moonbit_bytes_t)moonbit_make_bytes(25, 0);
  _M0Lm5indexS535 = 0;
  if (_M0L4signS536) {
    int32_t _M0L6_2atmpS1536 = _M0Lm5indexS535;
    int32_t _M0L6_2atmpS1537;
    if (
      _M0L6_2atmpS1536 < 0
      || _M0L6_2atmpS1536 >= Moonbit_array_length(_M0L6resultS534)
    ) {
      #line 535 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      moonbit_panic();
    }
    _M0L6resultS534[_M0L6_2atmpS1536] = 45;
    _M0L6_2atmpS1537 = _M0Lm5indexS535;
    _M0Lm5indexS535 = _M0L6_2atmpS1537 + 1;
  }
  _M0L6outputS537 = _M0L1vS538->$0;
  #line 539 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7olengthS539 = _M0FPB17decimal__length17(_M0L6outputS537);
  _M0L8exponentS1666 = _M0L1vS538->$1;
  _M0L6_2atmpS1665 = _M0L8exponentS1666 + _M0L7olengthS539;
  _M0Lm3expS540 = _M0L6_2atmpS1665 - 1;
  _M0L6_2atmpS1664 = _M0Lm3expS540;
  if (_M0L6_2atmpS1664 >= -6) {
    int32_t _M0L6_2atmpS1663 = _M0Lm3expS540;
    _M0L6_2atmpS1662 = _M0L6_2atmpS1663 < 21;
  } else {
    _M0L6_2atmpS1662 = 0;
  }
  _M0L18scientificNotationS541 = !_M0L6_2atmpS1662;
  if (_M0L18scientificNotationS541) {
    int32_t _M0L7_2abindS542 = _M0L7olengthS539 - 1;
    uint64_t _M0L6outputS543;
    int32_t _M0L1iS544 = 0;
    uint64_t _M0L6outputS545 = _M0L6outputS537;
    int32_t _M0L6_2atmpS1538;
    int32_t _M0L6_2atmpS1542;
    int32_t _M0L6_2atmpS1541;
    int32_t _M0L6_2atmpS1540;
    int32_t _M0L6_2atmpS1539;
    int32_t _M0L6_2atmpS1546;
    int32_t _M0L6_2atmpS1547;
    int32_t _M0L6_2atmpS1548;
    int32_t _M0L6_2atmpS1549;
    int32_t _M0L6_2atmpS1550;
    int32_t _M0L6_2atmpS1556;
    int32_t _M0L6_2atmpS1589;
    moonbit_string_t _result_2175;
    while (1) {
      if (_M0L1iS544 < _M0L7_2abindS542) {
        uint64_t _M0L1cS546 = _M0L6outputS545 % 10ull;
        int32_t _M0L6_2atmpS1595 = _M0Lm5indexS535;
        int32_t _M0L6_2atmpS1594 = _M0L6_2atmpS1595 + _M0L7olengthS539;
        int32_t _M0L6_2atmpS1590 = _M0L6_2atmpS1594 - _M0L1iS544;
        int32_t _M0L6_2atmpS1593 = (int32_t)_M0L1cS546;
        int32_t _M0L6_2atmpS1592 = 48 + _M0L6_2atmpS1593;
        int32_t _M0L6_2atmpS1591 = _M0L6_2atmpS1592 & 0xff;
        int32_t _M0L6_2atmpS1596;
        uint64_t _M0L6_2atmpS1597;
        if (
          _M0L6_2atmpS1590 < 0
          || _M0L6_2atmpS1590 >= Moonbit_array_length(_M0L6resultS534)
        ) {
          #line 547 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
          moonbit_panic();
        }
        _M0L6resultS534[_M0L6_2atmpS1590] = _M0L6_2atmpS1591;
        _M0L6_2atmpS1596 = _M0L1iS544 + 1;
        _M0L6_2atmpS1597 = _M0L6outputS545 / 10ull;
        _M0L1iS544 = _M0L6_2atmpS1596;
        _M0L6outputS545 = _M0L6_2atmpS1597;
        continue;
      } else {
        _M0L6outputS543 = _M0L6outputS545;
      }
      break;
    }
    _M0L6_2atmpS1538 = _M0Lm5indexS535;
    _M0L6_2atmpS1542 = (int32_t)_M0L6outputS543;
    _M0L6_2atmpS1541 = _M0L6_2atmpS1542 % 10;
    _M0L6_2atmpS1540 = 48 + _M0L6_2atmpS1541;
    _M0L6_2atmpS1539 = _M0L6_2atmpS1540 & 0xff;
    if (
      _M0L6_2atmpS1538 < 0
      || _M0L6_2atmpS1538 >= Moonbit_array_length(_M0L6resultS534)
    ) {
      #line 552 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      moonbit_panic();
    }
    _M0L6resultS534[_M0L6_2atmpS1538] = _M0L6_2atmpS1539;
    if (_M0L7olengthS539 > 1) {
      int32_t _M0L6_2atmpS1544 = _M0Lm5indexS535;
      int32_t _M0L6_2atmpS1543 = _M0L6_2atmpS1544 + 1;
      if (
        _M0L6_2atmpS1543 < 0
        || _M0L6_2atmpS1543 >= Moonbit_array_length(_M0L6resultS534)
      ) {
        #line 554 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS534[_M0L6_2atmpS1543] = 46;
    } else {
      int32_t _M0L6_2atmpS1545 = _M0Lm5indexS535;
      _M0Lm5indexS535 = _M0L6_2atmpS1545 - 1;
    }
    _M0L6_2atmpS1546 = _M0Lm5indexS535;
    _M0L6_2atmpS1547 = _M0L7olengthS539 + 1;
    _M0Lm5indexS535 = _M0L6_2atmpS1546 + _M0L6_2atmpS1547;
    _M0L6_2atmpS1548 = _M0Lm5indexS535;
    if (
      _M0L6_2atmpS1548 < 0
      || _M0L6_2atmpS1548 >= Moonbit_array_length(_M0L6resultS534)
    ) {
      #line 562 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      moonbit_panic();
    }
    _M0L6resultS534[_M0L6_2atmpS1548] = 101;
    _M0L6_2atmpS1549 = _M0Lm5indexS535;
    _M0Lm5indexS535 = _M0L6_2atmpS1549 + 1;
    _M0L6_2atmpS1550 = _M0Lm3expS540;
    if (_M0L6_2atmpS1550 < 0) {
      int32_t _M0L6_2atmpS1551 = _M0Lm5indexS535;
      int32_t _M0L6_2atmpS1552;
      int32_t _M0L6_2atmpS1553;
      if (
        _M0L6_2atmpS1551 < 0
        || _M0L6_2atmpS1551 >= Moonbit_array_length(_M0L6resultS534)
      ) {
        #line 565 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS534[_M0L6_2atmpS1551] = 45;
      _M0L6_2atmpS1552 = _M0Lm5indexS535;
      _M0Lm5indexS535 = _M0L6_2atmpS1552 + 1;
      _M0L6_2atmpS1553 = _M0Lm3expS540;
      _M0Lm3expS540 = -_M0L6_2atmpS1553;
    } else {
      int32_t _M0L6_2atmpS1554 = _M0Lm5indexS535;
      int32_t _M0L6_2atmpS1555;
      if (
        _M0L6_2atmpS1554 < 0
        || _M0L6_2atmpS1554 >= Moonbit_array_length(_M0L6resultS534)
      ) {
        #line 569 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS534[_M0L6_2atmpS1554] = 43;
      _M0L6_2atmpS1555 = _M0Lm5indexS535;
      _M0Lm5indexS535 = _M0L6_2atmpS1555 + 1;
    }
    _M0L6_2atmpS1556 = _M0Lm3expS540;
    if (_M0L6_2atmpS1556 >= 100) {
      int32_t _M0L6_2atmpS1572 = _M0Lm3expS540;
      int32_t _M0L1aS548 = _M0L6_2atmpS1572 / 100;
      int32_t _M0L6_2atmpS1571 = _M0Lm3expS540;
      int32_t _M0L6_2atmpS1570 = _M0L6_2atmpS1571 / 10;
      int32_t _M0L1bS549 = _M0L6_2atmpS1570 % 10;
      int32_t _M0L6_2atmpS1569 = _M0Lm3expS540;
      int32_t _M0L1cS550 = _M0L6_2atmpS1569 % 10;
      int32_t _M0L6_2atmpS1557 = _M0Lm5indexS535;
      int32_t _M0L6_2atmpS1559 = 48 + _M0L1aS548;
      int32_t _M0L6_2atmpS1558 = _M0L6_2atmpS1559 & 0xff;
      int32_t _M0L6_2atmpS1563;
      int32_t _M0L6_2atmpS1560;
      int32_t _M0L6_2atmpS1562;
      int32_t _M0L6_2atmpS1561;
      int32_t _M0L6_2atmpS1567;
      int32_t _M0L6_2atmpS1564;
      int32_t _M0L6_2atmpS1566;
      int32_t _M0L6_2atmpS1565;
      int32_t _M0L6_2atmpS1568;
      if (
        _M0L6_2atmpS1557 < 0
        || _M0L6_2atmpS1557 >= Moonbit_array_length(_M0L6resultS534)
      ) {
        #line 576 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS534[_M0L6_2atmpS1557] = _M0L6_2atmpS1558;
      _M0L6_2atmpS1563 = _M0Lm5indexS535;
      _M0L6_2atmpS1560 = _M0L6_2atmpS1563 + 1;
      _M0L6_2atmpS1562 = 48 + _M0L1bS549;
      _M0L6_2atmpS1561 = _M0L6_2atmpS1562 & 0xff;
      if (
        _M0L6_2atmpS1560 < 0
        || _M0L6_2atmpS1560 >= Moonbit_array_length(_M0L6resultS534)
      ) {
        #line 577 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS534[_M0L6_2atmpS1560] = _M0L6_2atmpS1561;
      _M0L6_2atmpS1567 = _M0Lm5indexS535;
      _M0L6_2atmpS1564 = _M0L6_2atmpS1567 + 2;
      _M0L6_2atmpS1566 = 48 + _M0L1cS550;
      _M0L6_2atmpS1565 = _M0L6_2atmpS1566 & 0xff;
      if (
        _M0L6_2atmpS1564 < 0
        || _M0L6_2atmpS1564 >= Moonbit_array_length(_M0L6resultS534)
      ) {
        #line 578 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS534[_M0L6_2atmpS1564] = _M0L6_2atmpS1565;
      _M0L6_2atmpS1568 = _M0Lm5indexS535;
      _M0Lm5indexS535 = _M0L6_2atmpS1568 + 3;
    } else {
      int32_t _M0L6_2atmpS1573 = _M0Lm3expS540;
      if (_M0L6_2atmpS1573 >= 10) {
        int32_t _M0L6_2atmpS1583 = _M0Lm3expS540;
        int32_t _M0L1aS551 = _M0L6_2atmpS1583 / 10;
        int32_t _M0L6_2atmpS1582 = _M0Lm3expS540;
        int32_t _M0L1bS552 = _M0L6_2atmpS1582 % 10;
        int32_t _M0L6_2atmpS1574 = _M0Lm5indexS535;
        int32_t _M0L6_2atmpS1576 = 48 + _M0L1aS551;
        int32_t _M0L6_2atmpS1575 = _M0L6_2atmpS1576 & 0xff;
        int32_t _M0L6_2atmpS1580;
        int32_t _M0L6_2atmpS1577;
        int32_t _M0L6_2atmpS1579;
        int32_t _M0L6_2atmpS1578;
        int32_t _M0L6_2atmpS1581;
        if (
          _M0L6_2atmpS1574 < 0
          || _M0L6_2atmpS1574 >= Moonbit_array_length(_M0L6resultS534)
        ) {
          #line 583 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
          moonbit_panic();
        }
        _M0L6resultS534[_M0L6_2atmpS1574] = _M0L6_2atmpS1575;
        _M0L6_2atmpS1580 = _M0Lm5indexS535;
        _M0L6_2atmpS1577 = _M0L6_2atmpS1580 + 1;
        _M0L6_2atmpS1579 = 48 + _M0L1bS552;
        _M0L6_2atmpS1578 = _M0L6_2atmpS1579 & 0xff;
        if (
          _M0L6_2atmpS1577 < 0
          || _M0L6_2atmpS1577 >= Moonbit_array_length(_M0L6resultS534)
        ) {
          #line 584 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
          moonbit_panic();
        }
        _M0L6resultS534[_M0L6_2atmpS1577] = _M0L6_2atmpS1578;
        _M0L6_2atmpS1581 = _M0Lm5indexS535;
        _M0Lm5indexS535 = _M0L6_2atmpS1581 + 2;
      } else {
        int32_t _M0L6_2atmpS1584 = _M0Lm5indexS535;
        int32_t _M0L6_2atmpS1587 = _M0Lm3expS540;
        int32_t _M0L6_2atmpS1586 = 48 + _M0L6_2atmpS1587;
        int32_t _M0L6_2atmpS1585 = _M0L6_2atmpS1586 & 0xff;
        int32_t _M0L6_2atmpS1588;
        if (
          _M0L6_2atmpS1584 < 0
          || _M0L6_2atmpS1584 >= Moonbit_array_length(_M0L6resultS534)
        ) {
          #line 587 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
          moonbit_panic();
        }
        _M0L6resultS534[_M0L6_2atmpS1584] = _M0L6_2atmpS1585;
        _M0L6_2atmpS1588 = _M0Lm5indexS535;
        _M0Lm5indexS535 = _M0L6_2atmpS1588 + 1;
      }
    }
    _M0L6_2atmpS1589 = _M0Lm5indexS535;
    #line 590 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _result_2175
    = _M0FPB19string__from__bytes(_M0L6resultS534, 0, _M0L6_2atmpS1589);
    moonbit_decref(_M0L6resultS534);
    return _result_2175;
  } else {
    int32_t _M0L6_2atmpS1598 = _M0Lm3expS540;
    int32_t _M0L6_2atmpS1661;
    moonbit_string_t _result_2181;
    if (_M0L6_2atmpS1598 < 0) {
      int32_t _M0L6_2atmpS1599 = _M0Lm5indexS535;
      int32_t _M0L6_2atmpS1601;
      int32_t _M0L6_2atmpS1600;
      int32_t _M0L6_2atmpS1602;
      int32_t _M0L1iS553;
      int32_t _M0L6_2atmpS1617;
      int32_t _M0L6_2atmpS1619;
      int32_t _M0L6_2atmpS1618;
      int32_t _M0L7currentS555;
      int32_t _M0L1iS556;
      uint64_t _M0L6outputS557;
      if (
        _M0L6_2atmpS1599 < 0
        || _M0L6_2atmpS1599 >= Moonbit_array_length(_M0L6resultS534)
      ) {
        #line 595 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS534[_M0L6_2atmpS1599] = 48;
      _M0L6_2atmpS1601 = _M0Lm5indexS535;
      _M0L6_2atmpS1600 = _M0L6_2atmpS1601 + 1;
      if (
        _M0L6_2atmpS1600 < 0
        || _M0L6_2atmpS1600 >= Moonbit_array_length(_M0L6resultS534)
      ) {
        #line 596 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS534[_M0L6_2atmpS1600] = 46;
      _M0L6_2atmpS1602 = _M0Lm5indexS535;
      _M0Lm5indexS535 = _M0L6_2atmpS1602 + 2;
      _M0L1iS553 = -1;
      while (1) {
        int32_t _M0L6_2atmpS1603 = _M0Lm3expS540;
        if (_M0L1iS553 > _M0L6_2atmpS1603) {
          int32_t _M0L6_2atmpS1606 = _M0Lm5indexS535;
          int32_t _M0L6_2atmpS1605 = _M0L6_2atmpS1606 - _M0L1iS553;
          int32_t _M0L6_2atmpS1604 = _M0L6_2atmpS1605 - 1;
          int32_t _M0L6_2atmpS1607;
          if (
            _M0L6_2atmpS1604 < 0
            || _M0L6_2atmpS1604 >= Moonbit_array_length(_M0L6resultS534)
          ) {
            #line 599 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
            moonbit_panic();
          }
          _M0L6resultS534[_M0L6_2atmpS1604] = 48;
          _M0L6_2atmpS1607 = _M0L1iS553 - 1;
          _M0L1iS553 = _M0L6_2atmpS1607;
          continue;
        }
        break;
      }
      _M0L6_2atmpS1617 = _M0Lm5indexS535;
      _M0L6_2atmpS1619 = _M0Lm3expS540;
      _M0L6_2atmpS1618 = -1 - _M0L6_2atmpS1619;
      _M0L7currentS555 = _M0L6_2atmpS1617 + _M0L6_2atmpS1618;
      _M0L1iS556 = 0;
      _M0L6outputS557 = _M0L6outputS537;
      while (1) {
        if (_M0L1iS556 < _M0L7olengthS539) {
          int32_t _M0L6_2atmpS1614 = _M0L7currentS555 + _M0L7olengthS539;
          int32_t _M0L6_2atmpS1613 = _M0L6_2atmpS1614 - _M0L1iS556;
          int32_t _M0L6_2atmpS1608 = _M0L6_2atmpS1613 - 1;
          uint64_t _M0L6_2atmpS1612 = _M0L6outputS557 % 10ull;
          int32_t _M0L6_2atmpS1611 = (int32_t)_M0L6_2atmpS1612;
          int32_t _M0L6_2atmpS1610 = 48 + _M0L6_2atmpS1611;
          int32_t _M0L6_2atmpS1609 = _M0L6_2atmpS1610 & 0xff;
          int32_t _M0L6_2atmpS1615;
          uint64_t _M0L6_2atmpS1616;
          if (
            _M0L6_2atmpS1608 < 0
            || _M0L6_2atmpS1608 >= Moonbit_array_length(_M0L6resultS534)
          ) {
            #line 603 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
            moonbit_panic();
          }
          _M0L6resultS534[_M0L6_2atmpS1608] = _M0L6_2atmpS1609;
          _M0L6_2atmpS1615 = _M0L1iS556 + 1;
          _M0L6_2atmpS1616 = _M0L6outputS557 / 10ull;
          _M0L1iS556 = _M0L6_2atmpS1615;
          _M0L6outputS557 = _M0L6_2atmpS1616;
          continue;
        }
        break;
      }
      _M0Lm5indexS535 = _M0L7currentS555 + _M0L7olengthS539;
    } else {
      int32_t _M0L6_2atmpS1621 = _M0Lm3expS540;
      int32_t _M0L6_2atmpS1620 = _M0L6_2atmpS1621 + 1;
      if (_M0L6_2atmpS1620 >= _M0L7olengthS539) {
        int32_t _M0L1iS559 = 0;
        uint64_t _M0L6outputS560 = _M0L6outputS537;
        int32_t _M0L6_2atmpS1632;
        int32_t _M0L6_2atmpS1637;
        int32_t _M0L7_2abindS562;
        int32_t _M0L1iS563;
        int32_t _M0L6_2atmpS1638;
        int32_t _M0L6_2atmpS1641;
        int32_t _M0L6_2atmpS1640;
        int32_t _M0L6_2atmpS1639;
        while (1) {
          if (_M0L1iS559 < _M0L7olengthS539) {
            int32_t _M0L6_2atmpS1629 = _M0Lm5indexS535;
            int32_t _M0L6_2atmpS1628 = _M0L6_2atmpS1629 + _M0L7olengthS539;
            int32_t _M0L6_2atmpS1627 = _M0L6_2atmpS1628 - _M0L1iS559;
            int32_t _M0L6_2atmpS1622 = _M0L6_2atmpS1627 - 1;
            uint64_t _M0L6_2atmpS1626 = _M0L6outputS560 % 10ull;
            int32_t _M0L6_2atmpS1625 = (int32_t)_M0L6_2atmpS1626;
            int32_t _M0L6_2atmpS1624 = 48 + _M0L6_2atmpS1625;
            int32_t _M0L6_2atmpS1623 = _M0L6_2atmpS1624 & 0xff;
            int32_t _M0L6_2atmpS1630;
            uint64_t _M0L6_2atmpS1631;
            if (
              _M0L6_2atmpS1622 < 0
              || _M0L6_2atmpS1622 >= Moonbit_array_length(_M0L6resultS534)
            ) {
              #line 610 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
              moonbit_panic();
            }
            _M0L6resultS534[_M0L6_2atmpS1622] = _M0L6_2atmpS1623;
            _M0L6_2atmpS1630 = _M0L1iS559 + 1;
            _M0L6_2atmpS1631 = _M0L6outputS560 / 10ull;
            _M0L1iS559 = _M0L6_2atmpS1630;
            _M0L6outputS560 = _M0L6_2atmpS1631;
            continue;
          }
          break;
        }
        _M0L6_2atmpS1632 = _M0Lm5indexS535;
        _M0Lm5indexS535 = _M0L6_2atmpS1632 + _M0L7olengthS539;
        _M0L6_2atmpS1637 = _M0Lm3expS540;
        _M0L7_2abindS562 = _M0L6_2atmpS1637 + 1;
        _M0L1iS563 = _M0L7olengthS539;
        while (1) {
          if (_M0L1iS563 < _M0L7_2abindS562) {
            int32_t _M0L6_2atmpS1635 = _M0Lm5indexS535;
            int32_t _M0L6_2atmpS1634 = _M0L6_2atmpS1635 + _M0L1iS563;
            int32_t _M0L6_2atmpS1633 = _M0L6_2atmpS1634 - _M0L7olengthS539;
            int32_t _M0L6_2atmpS1636;
            if (
              _M0L6_2atmpS1633 < 0
              || _M0L6_2atmpS1633 >= Moonbit_array_length(_M0L6resultS534)
            ) {
              #line 615 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
              moonbit_panic();
            }
            _M0L6resultS534[_M0L6_2atmpS1633] = 48;
            _M0L6_2atmpS1636 = _M0L1iS563 + 1;
            _M0L1iS563 = _M0L6_2atmpS1636;
            continue;
          }
          break;
        }
        _M0L6_2atmpS1638 = _M0Lm5indexS535;
        _M0L6_2atmpS1641 = _M0Lm3expS540;
        _M0L6_2atmpS1640 = _M0L6_2atmpS1641 + 1;
        _M0L6_2atmpS1639 = _M0L6_2atmpS1640 - _M0L7olengthS539;
        _M0Lm5indexS535 = _M0L6_2atmpS1638 + _M0L6_2atmpS1639;
      } else {
        int32_t _M0L6_2atmpS1658 = _M0Lm5indexS535;
        int32_t _M0L6_2atmpS1657 = _M0L6_2atmpS1658 + 1;
        int32_t _M0L1iS565 = 0;
        int32_t _M0L7currentS566 = _M0L6_2atmpS1657;
        uint64_t _M0L6outputS567 = _M0L6outputS537;
        int32_t _M0L6_2atmpS1659;
        int32_t _M0L6_2atmpS1660;
        while (1) {
          if (_M0L1iS565 < _M0L7olengthS539) {
            int32_t _M0L6_2atmpS1653 = _M0L7olengthS539 - _M0L1iS565;
            int32_t _M0L6_2atmpS1651 = _M0L6_2atmpS1653 - 1;
            int32_t _M0L6_2atmpS1652 = _M0Lm3expS540;
            int32_t _M0L7currentS568;
            int32_t _M0L6_2atmpS1648;
            int32_t _M0L6_2atmpS1647;
            int32_t _M0L6_2atmpS1642;
            uint64_t _M0L6_2atmpS1646;
            int32_t _M0L6_2atmpS1645;
            int32_t _M0L6_2atmpS1644;
            int32_t _M0L6_2atmpS1643;
            int32_t _M0L6_2atmpS1649;
            uint64_t _M0L6_2atmpS1650;
            if (_M0L6_2atmpS1651 == _M0L6_2atmpS1652) {
              int32_t _M0L6_2atmpS1656 = _M0L7currentS566 + _M0L7olengthS539;
              int32_t _M0L6_2atmpS1655 = _M0L6_2atmpS1656 - _M0L1iS565;
              int32_t _M0L6_2atmpS1654 = _M0L6_2atmpS1655 - 1;
              if (
                _M0L6_2atmpS1654 < 0
                || _M0L6_2atmpS1654 >= Moonbit_array_length(_M0L6resultS534)
              ) {
                #line 622 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
                moonbit_panic();
              }
              _M0L6resultS534[_M0L6_2atmpS1654] = 46;
              _M0L7currentS568 = _M0L7currentS566 - 1;
            } else {
              _M0L7currentS568 = _M0L7currentS566;
            }
            _M0L6_2atmpS1648 = _M0L7currentS568 + _M0L7olengthS539;
            _M0L6_2atmpS1647 = _M0L6_2atmpS1648 - _M0L1iS565;
            _M0L6_2atmpS1642 = _M0L6_2atmpS1647 - 1;
            _M0L6_2atmpS1646 = _M0L6outputS567 % 10ull;
            _M0L6_2atmpS1645 = (int32_t)_M0L6_2atmpS1646;
            _M0L6_2atmpS1644 = 48 + _M0L6_2atmpS1645;
            _M0L6_2atmpS1643 = _M0L6_2atmpS1644 & 0xff;
            if (
              _M0L6_2atmpS1642 < 0
              || _M0L6_2atmpS1642 >= Moonbit_array_length(_M0L6resultS534)
            ) {
              #line 627 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
              moonbit_panic();
            }
            _M0L6resultS534[_M0L6_2atmpS1642] = _M0L6_2atmpS1643;
            _M0L6_2atmpS1649 = _M0L1iS565 + 1;
            _M0L6_2atmpS1650 = _M0L6outputS567 / 10ull;
            _M0L1iS565 = _M0L6_2atmpS1649;
            _M0L7currentS566 = _M0L7currentS568;
            _M0L6outputS567 = _M0L6_2atmpS1650;
            continue;
          }
          break;
        }
        _M0L6_2atmpS1659 = _M0Lm5indexS535;
        _M0L6_2atmpS1660 = _M0L7olengthS539 + 1;
        _M0Lm5indexS535 = _M0L6_2atmpS1659 + _M0L6_2atmpS1660;
      }
    }
    _M0L6_2atmpS1661 = _M0Lm5indexS535;
    #line 632 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _result_2181
    = _M0FPB19string__from__bytes(_M0L6resultS534, 0, _M0L6_2atmpS1661);
    moonbit_decref(_M0L6resultS534);
    return _result_2181;
  }
}

struct _M0TPB17FloatingDecimal64* _M0FPB3d2d(
  uint64_t _M0L12ieeeMantissaS480,
  uint32_t _M0L12ieeeExponentS479
) {
  int32_t _M0Lm2e2S477;
  uint64_t _M0Lm2m2S478;
  uint64_t _M0L6_2atmpS1535;
  uint64_t _M0L6_2atmpS1534;
  int32_t _M0L4evenS481;
  uint64_t _M0L6_2atmpS1533;
  uint64_t _M0L2mvS482;
  int32_t _M0L7mmShiftS483;
  uint64_t _M0Lm2vrS484;
  uint64_t _M0Lm2vpS485;
  uint64_t _M0Lm2vmS486;
  int32_t _M0Lm3e10S487;
  int32_t _M0Lm17vmIsTrailingZerosS488;
  int32_t _M0Lm17vrIsTrailingZerosS489;
  int32_t _M0L6_2atmpS1435;
  int32_t _M0Lm7removedS508;
  int32_t _M0Lm16lastRemovedDigitS509;
  uint64_t _M0Lm6outputS510;
  int32_t _M0L6_2atmpS1531;
  int32_t _M0L6_2atmpS1532;
  int32_t _M0L3expS533;
  uint64_t _M0L6_2atmpS1530;
  struct _M0TPB17FloatingDecimal64* _block_2187;
  #line 347 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0Lm2e2S477 = 0;
  _M0Lm2m2S478 = 0ull;
  if (_M0L12ieeeExponentS479 == 0u) {
    _M0Lm2e2S477 = -1076;
    _M0Lm2m2S478 = _M0L12ieeeMantissaS480;
  } else {
    int32_t _M0L6_2atmpS1434 = *(int32_t*)&_M0L12ieeeExponentS479;
    int32_t _M0L6_2atmpS1433 = _M0L6_2atmpS1434 - 1023;
    int32_t _M0L6_2atmpS1432 = _M0L6_2atmpS1433 - 52;
    _M0Lm2e2S477 = _M0L6_2atmpS1432 - 2;
    _M0Lm2m2S478 = 4503599627370496ull | _M0L12ieeeMantissaS480;
  }
  _M0L6_2atmpS1535 = _M0Lm2m2S478;
  _M0L6_2atmpS1534 = _M0L6_2atmpS1535 & 1ull;
  _M0L4evenS481 = _M0L6_2atmpS1534 == 0ull;
  _M0L6_2atmpS1533 = _M0Lm2m2S478;
  _M0L2mvS482 = 4ull * _M0L6_2atmpS1533;
  _M0L7mmShiftS483
  = _M0L12ieeeMantissaS480 != 0ull || _M0L12ieeeExponentS479 <= 1u;
  _M0Lm2vrS484 = 0ull;
  _M0Lm2vpS485 = 0ull;
  _M0Lm2vmS486 = 0ull;
  _M0Lm3e10S487 = 0;
  _M0Lm17vmIsTrailingZerosS488 = 0;
  _M0Lm17vrIsTrailingZerosS489 = 0;
  _M0L6_2atmpS1435 = _M0Lm2e2S477;
  if (_M0L6_2atmpS1435 >= 0) {
    int32_t _M0L6_2atmpS1457 = _M0Lm2e2S477;
    int32_t _M0L6_2atmpS1453;
    int32_t _M0L6_2atmpS1456;
    int32_t _M0L6_2atmpS1455;
    int32_t _M0L6_2atmpS1454;
    int32_t _M0L1qS490;
    int32_t _M0L6_2atmpS1452;
    int32_t _M0L6_2atmpS1451;
    int32_t _M0L1kS491;
    int32_t _M0L6_2atmpS1450;
    int32_t _M0L6_2atmpS1449;
    int32_t _M0L6_2atmpS1448;
    int32_t _M0L1iS492;
    struct _M0TPB8Pow5Pair _M0L4pow5S493;
    uint64_t _M0L6_2atmpS1447;
    struct _M0TPB19MulShiftAll64Result _M0L7_2abindS494;
    uint64_t _M0L8_2avrOutS495;
    uint64_t _M0L8_2avpOutS496;
    uint64_t _M0L8_2avmOutS497;
    #line 383 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1453 = _M0FPB9log10Pow2(_M0L6_2atmpS1457);
    _M0L6_2atmpS1456 = _M0Lm2e2S477;
    _M0L6_2atmpS1455 = _M0L6_2atmpS1456 > 3;
    #line 383 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1454 = _M0MPC14bool4Bool7to__int(_M0L6_2atmpS1455);
    _M0L1qS490 = _M0L6_2atmpS1453 - _M0L6_2atmpS1454;
    _M0Lm3e10S487 = _M0L1qS490;
    #line 385 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1452 = _M0FPB8pow5bits(_M0L1qS490);
    _M0L6_2atmpS1451 = 125 + _M0L6_2atmpS1452;
    _M0L1kS491 = _M0L6_2atmpS1451 - 1;
    _M0L6_2atmpS1450 = _M0Lm2e2S477;
    _M0L6_2atmpS1449 = -_M0L6_2atmpS1450;
    _M0L6_2atmpS1448 = _M0L6_2atmpS1449 + _M0L1qS490;
    _M0L1iS492 = _M0L6_2atmpS1448 + _M0L1kS491;
    #line 387 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L4pow5S493 = _M0FPB22double__computeInvPow5(_M0L1qS490);
    _M0L6_2atmpS1447 = _M0Lm2m2S478;
    #line 388 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L7_2abindS494
    = _M0FPB13mulShiftAll64(_M0L6_2atmpS1447, _M0L4pow5S493, _M0L1iS492, _M0L7mmShiftS483);
    _M0L8_2avrOutS495 = _M0L7_2abindS494.$0;
    _M0L8_2avpOutS496 = _M0L7_2abindS494.$1;
    _M0L8_2avmOutS497 = _M0L7_2abindS494.$2;
    _M0Lm2vrS484 = _M0L8_2avrOutS495;
    _M0Lm2vpS485 = _M0L8_2avpOutS496;
    _M0Lm2vmS486 = _M0L8_2avmOutS497;
    if (_M0L1qS490 <= 21) {
      int32_t _M0L6_2atmpS1443 = (int32_t)_M0L2mvS482;
      uint64_t _M0L6_2atmpS1446 = _M0L2mvS482 / 5ull;
      int32_t _M0L6_2atmpS1445 = (int32_t)_M0L6_2atmpS1446;
      int32_t _M0L6_2atmpS1444 = 5 * _M0L6_2atmpS1445;
      int32_t _M0L6mvMod5S498 = _M0L6_2atmpS1443 - _M0L6_2atmpS1444;
      if (_M0L6mvMod5S498 == 0) {
        #line 400 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0Lm17vrIsTrailingZerosS489
        = _M0FPB18multipleOfPowerOf5(_M0L2mvS482, _M0L1qS490);
      } else if (_M0L4evenS481) {
        uint64_t _M0L6_2atmpS1437 = _M0L2mvS482 - 1ull;
        uint64_t _M0L6_2atmpS1438;
        uint64_t _M0L6_2atmpS1436;
        #line 406 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0L6_2atmpS1438 = _M0MPC14bool4Bool10to__uint64(_M0L7mmShiftS483);
        _M0L6_2atmpS1436 = _M0L6_2atmpS1437 - _M0L6_2atmpS1438;
        #line 405 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0Lm17vmIsTrailingZerosS488
        = _M0FPB18multipleOfPowerOf5(_M0L6_2atmpS1436, _M0L1qS490);
      } else {
        uint64_t _M0L6_2atmpS1439 = _M0Lm2vpS485;
        uint64_t _M0L6_2atmpS1442 = _M0L2mvS482 + 2ull;
        int32_t _M0L6_2atmpS1441;
        uint64_t _M0L6_2atmpS1440;
        #line 410 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0L6_2atmpS1441
        = _M0FPB18multipleOfPowerOf5(_M0L6_2atmpS1442, _M0L1qS490);
        #line 410 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0L6_2atmpS1440 = _M0MPC14bool4Bool10to__uint64(_M0L6_2atmpS1441);
        _M0Lm2vpS485 = _M0L6_2atmpS1439 - _M0L6_2atmpS1440;
      }
    }
  } else {
    int32_t _M0L6_2atmpS1471 = _M0Lm2e2S477;
    int32_t _M0L6_2atmpS1470 = -_M0L6_2atmpS1471;
    int32_t _M0L6_2atmpS1465;
    int32_t _M0L6_2atmpS1469;
    int32_t _M0L6_2atmpS1468;
    int32_t _M0L6_2atmpS1467;
    int32_t _M0L6_2atmpS1466;
    int32_t _M0L1qS499;
    int32_t _M0L6_2atmpS1458;
    int32_t _M0L6_2atmpS1464;
    int32_t _M0L6_2atmpS1463;
    int32_t _M0L1iS500;
    int32_t _M0L6_2atmpS1462;
    int32_t _M0L1kS501;
    int32_t _M0L1jS502;
    struct _M0TPB8Pow5Pair _M0L4pow5S503;
    uint64_t _M0L6_2atmpS1461;
    struct _M0TPB19MulShiftAll64Result _M0L7_2abindS504;
    uint64_t _M0L8_2avrOutS505;
    uint64_t _M0L8_2avpOutS506;
    uint64_t _M0L8_2avmOutS507;
    #line 415 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1465 = _M0FPB9log10Pow5(_M0L6_2atmpS1470);
    _M0L6_2atmpS1469 = _M0Lm2e2S477;
    _M0L6_2atmpS1468 = -_M0L6_2atmpS1469;
    _M0L6_2atmpS1467 = _M0L6_2atmpS1468 > 1;
    #line 415 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1466 = _M0MPC14bool4Bool7to__int(_M0L6_2atmpS1467);
    _M0L1qS499 = _M0L6_2atmpS1465 - _M0L6_2atmpS1466;
    _M0L6_2atmpS1458 = _M0Lm2e2S477;
    _M0Lm3e10S487 = _M0L1qS499 + _M0L6_2atmpS1458;
    _M0L6_2atmpS1464 = _M0Lm2e2S477;
    _M0L6_2atmpS1463 = -_M0L6_2atmpS1464;
    _M0L1iS500 = _M0L6_2atmpS1463 - _M0L1qS499;
    #line 418 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1462 = _M0FPB8pow5bits(_M0L1iS500);
    _M0L1kS501 = _M0L6_2atmpS1462 - 125;
    _M0L1jS502 = _M0L1qS499 - _M0L1kS501;
    #line 420 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L4pow5S503 = _M0FPB19double__computePow5(_M0L1iS500);
    _M0L6_2atmpS1461 = _M0Lm2m2S478;
    #line 421 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L7_2abindS504
    = _M0FPB13mulShiftAll64(_M0L6_2atmpS1461, _M0L4pow5S503, _M0L1jS502, _M0L7mmShiftS483);
    _M0L8_2avrOutS505 = _M0L7_2abindS504.$0;
    _M0L8_2avpOutS506 = _M0L7_2abindS504.$1;
    _M0L8_2avmOutS507 = _M0L7_2abindS504.$2;
    _M0Lm2vrS484 = _M0L8_2avrOutS505;
    _M0Lm2vpS485 = _M0L8_2avpOutS506;
    _M0Lm2vmS486 = _M0L8_2avmOutS507;
    if (_M0L1qS499 <= 1) {
      _M0Lm17vrIsTrailingZerosS489 = 1;
      if (_M0L4evenS481) {
        int32_t _M0L6_2atmpS1459;
        #line 432 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0L6_2atmpS1459 = _M0MPC14bool4Bool7to__int(_M0L7mmShiftS483);
        _M0Lm17vmIsTrailingZerosS488 = _M0L6_2atmpS1459 == 1;
      } else {
        uint64_t _M0L6_2atmpS1460 = _M0Lm2vpS485;
        _M0Lm2vpS485 = _M0L6_2atmpS1460 - 1ull;
      }
    } else if (_M0L1qS499 < 63) {
      #line 437 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0Lm17vrIsTrailingZerosS489
      = _M0FPB18multipleOfPowerOf2(_M0L2mvS482, _M0L1qS499);
    }
  }
  _M0Lm7removedS508 = 0;
  _M0Lm16lastRemovedDigitS509 = 0;
  _M0Lm6outputS510 = 0ull;
  if (_M0Lm17vmIsTrailingZerosS488 || _M0Lm17vrIsTrailingZerosS489) {
    int32_t _if__result_2184;
    uint64_t _M0L6_2atmpS1501;
    uint64_t _M0L6_2atmpS1507;
    uint64_t _M0L6_2atmpS1508;
    int32_t _if__result_2185;
    int32_t _M0L6_2atmpS1504;
    int64_t _M0L6_2atmpS1503;
    uint64_t _M0L6_2atmpS1502;
    while (1) {
      uint64_t _M0L6_2atmpS1484 = _M0Lm2vpS485;
      uint64_t _M0L7vpDiv10S511 = _M0L6_2atmpS1484 / 10ull;
      uint64_t _M0L6_2atmpS1483 = _M0Lm2vmS486;
      uint64_t _M0L7vmDiv10S512 = _M0L6_2atmpS1483 / 10ull;
      uint64_t _M0L6_2atmpS1482;
      int32_t _M0L6_2atmpS1479;
      int32_t _M0L6_2atmpS1481;
      int32_t _M0L6_2atmpS1480;
      int32_t _M0L7vmMod10S514;
      uint64_t _M0L6_2atmpS1478;
      uint64_t _M0L7vrDiv10S515;
      uint64_t _M0L6_2atmpS1477;
      int32_t _M0L6_2atmpS1474;
      int32_t _M0L6_2atmpS1476;
      int32_t _M0L6_2atmpS1475;
      int32_t _M0L7vrMod10S516;
      int32_t _M0L6_2atmpS1473;
      if (_M0L7vpDiv10S511 <= _M0L7vmDiv10S512) {
        break;
      }
      _M0L6_2atmpS1482 = _M0Lm2vmS486;
      _M0L6_2atmpS1479 = (int32_t)_M0L6_2atmpS1482;
      _M0L6_2atmpS1481 = (int32_t)_M0L7vmDiv10S512;
      _M0L6_2atmpS1480 = 10 * _M0L6_2atmpS1481;
      _M0L7vmMod10S514 = _M0L6_2atmpS1479 - _M0L6_2atmpS1480;
      _M0L6_2atmpS1478 = _M0Lm2vrS484;
      _M0L7vrDiv10S515 = _M0L6_2atmpS1478 / 10ull;
      _M0L6_2atmpS1477 = _M0Lm2vrS484;
      _M0L6_2atmpS1474 = (int32_t)_M0L6_2atmpS1477;
      _M0L6_2atmpS1476 = (int32_t)_M0L7vrDiv10S515;
      _M0L6_2atmpS1475 = 10 * _M0L6_2atmpS1476;
      _M0L7vrMod10S516 = _M0L6_2atmpS1474 - _M0L6_2atmpS1475;
      _M0Lm17vmIsTrailingZerosS488
      = _M0Lm17vmIsTrailingZerosS488 && _M0L7vmMod10S514 == 0;
      if (_M0Lm17vrIsTrailingZerosS489) {
        int32_t _M0L6_2atmpS1472 = _M0Lm16lastRemovedDigitS509;
        _M0Lm17vrIsTrailingZerosS489 = _M0L6_2atmpS1472 == 0;
      } else {
        _M0Lm17vrIsTrailingZerosS489 = 0;
      }
      _M0Lm16lastRemovedDigitS509 = _M0L7vrMod10S516;
      _M0Lm2vrS484 = _M0L7vrDiv10S515;
      _M0Lm2vpS485 = _M0L7vpDiv10S511;
      _M0Lm2vmS486 = _M0L7vmDiv10S512;
      _M0L6_2atmpS1473 = _M0Lm7removedS508;
      _M0Lm7removedS508 = _M0L6_2atmpS1473 + 1;
      continue;
      break;
    }
    if (_M0Lm17vmIsTrailingZerosS488) {
      while (1) {
        uint64_t _M0L6_2atmpS1497 = _M0Lm2vmS486;
        uint64_t _M0L7vmDiv10S517 = _M0L6_2atmpS1497 / 10ull;
        uint64_t _M0L6_2atmpS1496 = _M0Lm2vmS486;
        int32_t _M0L6_2atmpS1493 = (int32_t)_M0L6_2atmpS1496;
        int32_t _M0L6_2atmpS1495 = (int32_t)_M0L7vmDiv10S517;
        int32_t _M0L6_2atmpS1494 = 10 * _M0L6_2atmpS1495;
        int32_t _M0L7vmMod10S518 = _M0L6_2atmpS1493 - _M0L6_2atmpS1494;
        uint64_t _M0L6_2atmpS1492;
        uint64_t _M0L7vpDiv10S520;
        uint64_t _M0L6_2atmpS1491;
        uint64_t _M0L7vrDiv10S521;
        uint64_t _M0L6_2atmpS1490;
        int32_t _M0L6_2atmpS1487;
        int32_t _M0L6_2atmpS1489;
        int32_t _M0L6_2atmpS1488;
        int32_t _M0L7vrMod10S522;
        int32_t _M0L6_2atmpS1486;
        if (_M0L7vmMod10S518 != 0) {
          break;
        }
        _M0L6_2atmpS1492 = _M0Lm2vpS485;
        _M0L7vpDiv10S520 = _M0L6_2atmpS1492 / 10ull;
        _M0L6_2atmpS1491 = _M0Lm2vrS484;
        _M0L7vrDiv10S521 = _M0L6_2atmpS1491 / 10ull;
        _M0L6_2atmpS1490 = _M0Lm2vrS484;
        _M0L6_2atmpS1487 = (int32_t)_M0L6_2atmpS1490;
        _M0L6_2atmpS1489 = (int32_t)_M0L7vrDiv10S521;
        _M0L6_2atmpS1488 = 10 * _M0L6_2atmpS1489;
        _M0L7vrMod10S522 = _M0L6_2atmpS1487 - _M0L6_2atmpS1488;
        if (_M0Lm17vrIsTrailingZerosS489) {
          int32_t _M0L6_2atmpS1485 = _M0Lm16lastRemovedDigitS509;
          _M0Lm17vrIsTrailingZerosS489 = _M0L6_2atmpS1485 == 0;
        } else {
          _M0Lm17vrIsTrailingZerosS489 = 0;
        }
        _M0Lm16lastRemovedDigitS509 = _M0L7vrMod10S522;
        _M0Lm2vrS484 = _M0L7vrDiv10S521;
        _M0Lm2vpS485 = _M0L7vpDiv10S520;
        _M0Lm2vmS486 = _M0L7vmDiv10S517;
        _M0L6_2atmpS1486 = _M0Lm7removedS508;
        _M0Lm7removedS508 = _M0L6_2atmpS1486 + 1;
        continue;
        break;
      }
    }
    if (_M0Lm17vrIsTrailingZerosS489) {
      int32_t _M0L6_2atmpS1500 = _M0Lm16lastRemovedDigitS509;
      if (_M0L6_2atmpS1500 == 5) {
        uint64_t _M0L6_2atmpS1499 = _M0Lm2vrS484;
        uint64_t _M0L6_2atmpS1498 = _M0L6_2atmpS1499 % 2ull;
        _if__result_2184 = _M0L6_2atmpS1498 == 0ull;
      } else {
        _if__result_2184 = 0;
      }
    } else {
      _if__result_2184 = 0;
    }
    if (_if__result_2184) {
      _M0Lm16lastRemovedDigitS509 = 4;
    }
    _M0L6_2atmpS1501 = _M0Lm2vrS484;
    _M0L6_2atmpS1507 = _M0Lm2vrS484;
    _M0L6_2atmpS1508 = _M0Lm2vmS486;
    if (_M0L6_2atmpS1507 == _M0L6_2atmpS1508) {
      if (!_M0L4evenS481) {
        _if__result_2185 = 1;
      } else {
        int32_t _M0L6_2atmpS1506 = _M0Lm17vmIsTrailingZerosS488;
        _if__result_2185 = !_M0L6_2atmpS1506;
      }
    } else {
      _if__result_2185 = 0;
    }
    if (_if__result_2185) {
      _M0L6_2atmpS1504 = 1;
    } else {
      int32_t _M0L6_2atmpS1505 = _M0Lm16lastRemovedDigitS509;
      _M0L6_2atmpS1504 = _M0L6_2atmpS1505 >= 5;
    }
    #line 487 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1503 = _M0MPC14bool4Bool9to__int64(_M0L6_2atmpS1504);
    _M0L6_2atmpS1502 = *(uint64_t*)&_M0L6_2atmpS1503;
    _M0Lm6outputS510 = _M0L6_2atmpS1501 + _M0L6_2atmpS1502;
  } else {
    int32_t _M0Lm7roundUpS523 = 0;
    uint64_t _M0L6_2atmpS1529 = _M0Lm2vpS485;
    uint64_t _M0L8vpDiv100S524 = _M0L6_2atmpS1529 / 100ull;
    uint64_t _M0L6_2atmpS1528 = _M0Lm2vmS486;
    uint64_t _M0L8vmDiv100S525 = _M0L6_2atmpS1528 / 100ull;
    uint64_t _M0L6_2atmpS1523;
    uint64_t _M0L6_2atmpS1526;
    uint64_t _M0L6_2atmpS1527;
    int32_t _M0L6_2atmpS1525;
    uint64_t _M0L6_2atmpS1524;
    if (_M0L8vpDiv100S524 > _M0L8vmDiv100S525) {
      uint64_t _M0L6_2atmpS1514 = _M0Lm2vrS484;
      uint64_t _M0L8vrDiv100S526 = _M0L6_2atmpS1514 / 100ull;
      uint64_t _M0L6_2atmpS1513 = _M0Lm2vrS484;
      int32_t _M0L6_2atmpS1510 = (int32_t)_M0L6_2atmpS1513;
      int32_t _M0L6_2atmpS1512 = (int32_t)_M0L8vrDiv100S526;
      int32_t _M0L6_2atmpS1511 = 100 * _M0L6_2atmpS1512;
      int32_t _M0L8vrMod100S527 = _M0L6_2atmpS1510 - _M0L6_2atmpS1511;
      int32_t _M0L6_2atmpS1509;
      _M0Lm7roundUpS523 = _M0L8vrMod100S527 >= 50;
      _M0Lm2vrS484 = _M0L8vrDiv100S526;
      _M0Lm2vpS485 = _M0L8vpDiv100S524;
      _M0Lm2vmS486 = _M0L8vmDiv100S525;
      _M0L6_2atmpS1509 = _M0Lm7removedS508;
      _M0Lm7removedS508 = _M0L6_2atmpS1509 + 2;
    }
    while (1) {
      uint64_t _M0L6_2atmpS1522 = _M0Lm2vpS485;
      uint64_t _M0L7vpDiv10S528 = _M0L6_2atmpS1522 / 10ull;
      uint64_t _M0L6_2atmpS1521 = _M0Lm2vmS486;
      uint64_t _M0L7vmDiv10S529 = _M0L6_2atmpS1521 / 10ull;
      uint64_t _M0L6_2atmpS1520;
      uint64_t _M0L7vrDiv10S531;
      uint64_t _M0L6_2atmpS1519;
      int32_t _M0L6_2atmpS1516;
      int32_t _M0L6_2atmpS1518;
      int32_t _M0L6_2atmpS1517;
      int32_t _M0L7vrMod10S532;
      int32_t _M0L6_2atmpS1515;
      if (_M0L7vpDiv10S528 <= _M0L7vmDiv10S529) {
        break;
      }
      _M0L6_2atmpS1520 = _M0Lm2vrS484;
      _M0L7vrDiv10S531 = _M0L6_2atmpS1520 / 10ull;
      _M0L6_2atmpS1519 = _M0Lm2vrS484;
      _M0L6_2atmpS1516 = (int32_t)_M0L6_2atmpS1519;
      _M0L6_2atmpS1518 = (int32_t)_M0L7vrDiv10S531;
      _M0L6_2atmpS1517 = 10 * _M0L6_2atmpS1518;
      _M0L7vrMod10S532 = _M0L6_2atmpS1516 - _M0L6_2atmpS1517;
      _M0Lm7roundUpS523 = _M0L7vrMod10S532 >= 5;
      _M0Lm2vrS484 = _M0L7vrDiv10S531;
      _M0Lm2vpS485 = _M0L7vpDiv10S528;
      _M0Lm2vmS486 = _M0L7vmDiv10S529;
      _M0L6_2atmpS1515 = _M0Lm7removedS508;
      _M0Lm7removedS508 = _M0L6_2atmpS1515 + 1;
      continue;
      break;
    }
    _M0L6_2atmpS1523 = _M0Lm2vrS484;
    _M0L6_2atmpS1526 = _M0Lm2vrS484;
    _M0L6_2atmpS1527 = _M0Lm2vmS486;
    _M0L6_2atmpS1525
    = _M0L6_2atmpS1526 == _M0L6_2atmpS1527 || _M0Lm7roundUpS523;
    #line 522 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1524 = _M0MPC14bool4Bool10to__uint64(_M0L6_2atmpS1525);
    _M0Lm6outputS510 = _M0L6_2atmpS1523 + _M0L6_2atmpS1524;
  }
  _M0L6_2atmpS1531 = _M0Lm3e10S487;
  _M0L6_2atmpS1532 = _M0Lm7removedS508;
  _M0L3expS533 = _M0L6_2atmpS1531 + _M0L6_2atmpS1532;
  _M0L6_2atmpS1530 = _M0Lm6outputS510;
  _block_2187
  = (struct _M0TPB17FloatingDecimal64*)moonbit_malloc(sizeof(struct _M0TPB17FloatingDecimal64));
  Moonbit_object_header(_block_2187)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_2187->$0 = _M0L6_2atmpS1530;
  _block_2187->$1 = _M0L3expS533;
  return _block_2187;
}

uint64_t _M0MPC14bool4Bool10to__uint64(int32_t _M0L4selfS476) {
  #line 110 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bool.mbt"
  if (_M0L4selfS476) {
    return 1ull;
  } else {
    return 0ull;
  }
}

int64_t _M0MPC14bool4Bool9to__int64(int32_t _M0L4selfS475) {
  #line 58 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bool.mbt"
  if (_M0L4selfS475) {
    return 1ll;
  } else {
    return 0ll;
  }
}

int32_t _M0MPC14bool4Bool7to__int(int32_t _M0L4selfS474) {
  #line 32 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bool.mbt"
  if (_M0L4selfS474) {
    return 1;
  } else {
    return 0;
  }
}

int32_t _M0FPB17decimal__length17(uint64_t _M0L1vS473) {
  #line 280 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  if (_M0L1vS473 >= 10000000000000000ull) {
    return 17;
  }
  if (_M0L1vS473 >= 1000000000000000ull) {
    return 16;
  }
  if (_M0L1vS473 >= 100000000000000ull) {
    return 15;
  }
  if (_M0L1vS473 >= 10000000000000ull) {
    return 14;
  }
  if (_M0L1vS473 >= 1000000000000ull) {
    return 13;
  }
  if (_M0L1vS473 >= 100000000000ull) {
    return 12;
  }
  if (_M0L1vS473 >= 10000000000ull) {
    return 11;
  }
  if (_M0L1vS473 >= 1000000000ull) {
    return 10;
  }
  if (_M0L1vS473 >= 100000000ull) {
    return 9;
  }
  if (_M0L1vS473 >= 10000000ull) {
    return 8;
  }
  if (_M0L1vS473 >= 1000000ull) {
    return 7;
  }
  if (_M0L1vS473 >= 100000ull) {
    return 6;
  }
  if (_M0L1vS473 >= 10000ull) {
    return 5;
  }
  if (_M0L1vS473 >= 1000ull) {
    return 4;
  }
  if (_M0L1vS473 >= 100ull) {
    return 3;
  }
  if (_M0L1vS473 >= 10ull) {
    return 2;
  }
  return 1;
}

struct _M0TPB8Pow5Pair _M0FPB22double__computeInvPow5(int32_t _M0L1iS456) {
  int32_t _M0L6_2atmpS1431;
  int32_t _M0L6_2atmpS1430;
  int32_t _M0L4baseS455;
  int32_t _M0L5base2S457;
  int32_t _M0L6offsetS458;
  int32_t _M0L6_2atmpS1429;
  uint64_t _M0L4mul0S459;
  int32_t _M0L6_2atmpS1428;
  int32_t _M0L6_2atmpS1427;
  uint64_t _M0L4mul1S460;
  uint64_t _M0L1mS461;
  struct _M0TPB7Umul128 _M0L7_2abindS462;
  uint64_t _M0L7_2alow1S463;
  uint64_t _M0L8_2ahigh1S464;
  struct _M0TPB7Umul128 _M0L7_2abindS465;
  uint64_t _M0L7_2alow0S466;
  uint64_t _M0L8_2ahigh0S467;
  uint64_t _M0L3sumS468;
  uint64_t _M0Lm5high1S469;
  int32_t _M0L6_2atmpS1425;
  int32_t _M0L6_2atmpS1426;
  int32_t _M0L5deltaS470;
  uint64_t _M0L6_2atmpS1424;
  uint64_t _M0L6_2atmpS1416;
  int32_t _M0L6_2atmpS1423;
  uint32_t _M0L6_2atmpS1420;
  int32_t _M0L6_2atmpS1422;
  int32_t _M0L6_2atmpS1421;
  uint32_t _M0L6_2atmpS1419;
  uint32_t _M0L6_2atmpS1418;
  uint64_t _M0L6_2atmpS1417;
  uint64_t _M0L1aS471;
  uint64_t _M0L6_2atmpS1415;
  uint64_t _M0L1bS472;
  #line 239 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1431 = _M0L1iS456 + 26;
  _M0L6_2atmpS1430 = _M0L6_2atmpS1431 - 1;
  _M0L4baseS455 = _M0L6_2atmpS1430 / 26;
  _M0L5base2S457 = _M0L4baseS455 * 26;
  _M0L6offsetS458 = _M0L5base2S457 - _M0L1iS456;
  _M0L6_2atmpS1429 = _M0L4baseS455 * 2;
  #line 243 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4mul0S459
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB26gDOUBLE__POW5__INV__SPLIT2, _M0L6_2atmpS1429);
  _M0L6_2atmpS1428 = _M0L4baseS455 * 2;
  _M0L6_2atmpS1427 = _M0L6_2atmpS1428 + 1;
  #line 244 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4mul1S460
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB26gDOUBLE__POW5__INV__SPLIT2, _M0L6_2atmpS1427);
  if (_M0L6offsetS458 == 0) {
    return (struct _M0TPB8Pow5Pair){.$0 = _M0L4mul0S459, .$1 = _M0L4mul1S460};
  }
  #line 248 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L1mS461
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB20gDOUBLE__POW5__TABLE, _M0L6offsetS458);
  #line 249 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS462 = _M0FPB7umul128(_M0L1mS461, _M0L4mul1S460);
  _M0L7_2alow1S463 = _M0L7_2abindS462.$0;
  _M0L8_2ahigh1S464 = _M0L7_2abindS462.$1;
  #line 250 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS465 = _M0FPB7umul128(_M0L1mS461, _M0L4mul0S459);
  _M0L7_2alow0S466 = _M0L7_2abindS465.$0;
  _M0L8_2ahigh0S467 = _M0L7_2abindS465.$1;
  _M0L3sumS468 = _M0L8_2ahigh0S467 + _M0L7_2alow1S463;
  _M0Lm5high1S469 = _M0L8_2ahigh1S464;
  if (_M0L3sumS468 < _M0L8_2ahigh0S467) {
    uint64_t _M0L6_2atmpS1414 = _M0Lm5high1S469;
    _M0Lm5high1S469 = _M0L6_2atmpS1414 + 1ull;
  }
  #line 256 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1425 = _M0FPB8pow5bits(_M0L5base2S457);
  #line 256 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1426 = _M0FPB8pow5bits(_M0L1iS456);
  _M0L5deltaS470 = _M0L6_2atmpS1425 - _M0L6_2atmpS1426;
  #line 257 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1424
  = _M0FPB13shiftright128(_M0L7_2alow0S466, _M0L3sumS468, _M0L5deltaS470);
  _M0L6_2atmpS1416 = _M0L6_2atmpS1424 + 1ull;
  _M0L6_2atmpS1423 = _M0L1iS456 / 16;
  #line 259 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1420
  = _M0MPC15array13ReadOnlyArray2atGjE(_M0FPB19gPOW5__INV__OFFSETS, _M0L6_2atmpS1423);
  _M0L6_2atmpS1422 = _M0L1iS456 % 16;
  _M0L6_2atmpS1421 = _M0L6_2atmpS1422 << 1;
  _M0L6_2atmpS1419 = _M0L6_2atmpS1420 >> (_M0L6_2atmpS1421 & 31);
  _M0L6_2atmpS1418 = _M0L6_2atmpS1419 & 3u;
  #line 259 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1417 = _M0MPC14uint4UInt10to__uint64(_M0L6_2atmpS1418);
  _M0L1aS471 = _M0L6_2atmpS1416 + _M0L6_2atmpS1417;
  _M0L6_2atmpS1415 = _M0Lm5high1S469;
  #line 260 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L1bS472
  = _M0FPB13shiftright128(_M0L3sumS468, _M0L6_2atmpS1415, _M0L5deltaS470);
  return (struct _M0TPB8Pow5Pair){.$0 = _M0L1aS471, .$1 = _M0L1bS472};
}

struct _M0TPB8Pow5Pair _M0FPB19double__computePow5(int32_t _M0L1iS438) {
  int32_t _M0L4baseS437;
  int32_t _M0L5base2S439;
  int32_t _M0L6offsetS440;
  int32_t _M0L6_2atmpS1413;
  uint64_t _M0L4mul0S441;
  int32_t _M0L6_2atmpS1412;
  int32_t _M0L6_2atmpS1411;
  uint64_t _M0L4mul1S442;
  uint64_t _M0L1mS443;
  struct _M0TPB7Umul128 _M0L7_2abindS444;
  uint64_t _M0L7_2alow1S445;
  uint64_t _M0L8_2ahigh1S446;
  struct _M0TPB7Umul128 _M0L7_2abindS447;
  uint64_t _M0L7_2alow0S448;
  uint64_t _M0L8_2ahigh0S449;
  uint64_t _M0L3sumS450;
  uint64_t _M0Lm5high1S451;
  int32_t _M0L6_2atmpS1409;
  int32_t _M0L6_2atmpS1410;
  int32_t _M0L5deltaS452;
  uint64_t _M0L6_2atmpS1401;
  int32_t _M0L6_2atmpS1408;
  uint32_t _M0L6_2atmpS1405;
  int32_t _M0L6_2atmpS1407;
  int32_t _M0L6_2atmpS1406;
  uint32_t _M0L6_2atmpS1404;
  uint32_t _M0L6_2atmpS1403;
  uint64_t _M0L6_2atmpS1402;
  uint64_t _M0L1aS453;
  uint64_t _M0L6_2atmpS1400;
  uint64_t _M0L1bS454;
  #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4baseS437 = _M0L1iS438 / 26;
  _M0L5base2S439 = _M0L4baseS437 * 26;
  _M0L6offsetS440 = _M0L1iS438 - _M0L5base2S439;
  _M0L6_2atmpS1413 = _M0L4baseS437 * 2;
  #line 217 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4mul0S441
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB21gDOUBLE__POW5__SPLIT2, _M0L6_2atmpS1413);
  _M0L6_2atmpS1412 = _M0L4baseS437 * 2;
  _M0L6_2atmpS1411 = _M0L6_2atmpS1412 + 1;
  #line 218 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4mul1S442
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB21gDOUBLE__POW5__SPLIT2, _M0L6_2atmpS1411);
  if (_M0L6offsetS440 == 0) {
    return (struct _M0TPB8Pow5Pair){.$0 = _M0L4mul0S441, .$1 = _M0L4mul1S442};
  }
  #line 222 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L1mS443
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB20gDOUBLE__POW5__TABLE, _M0L6offsetS440);
  #line 223 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS444 = _M0FPB7umul128(_M0L1mS443, _M0L4mul1S442);
  _M0L7_2alow1S445 = _M0L7_2abindS444.$0;
  _M0L8_2ahigh1S446 = _M0L7_2abindS444.$1;
  #line 224 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS447 = _M0FPB7umul128(_M0L1mS443, _M0L4mul0S441);
  _M0L7_2alow0S448 = _M0L7_2abindS447.$0;
  _M0L8_2ahigh0S449 = _M0L7_2abindS447.$1;
  _M0L3sumS450 = _M0L8_2ahigh0S449 + _M0L7_2alow1S445;
  _M0Lm5high1S451 = _M0L8_2ahigh1S446;
  if (_M0L3sumS450 < _M0L8_2ahigh0S449) {
    uint64_t _M0L6_2atmpS1399 = _M0Lm5high1S451;
    _M0Lm5high1S451 = _M0L6_2atmpS1399 + 1ull;
  }
  #line 230 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1409 = _M0FPB8pow5bits(_M0L1iS438);
  #line 230 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1410 = _M0FPB8pow5bits(_M0L5base2S439);
  _M0L5deltaS452 = _M0L6_2atmpS1409 - _M0L6_2atmpS1410;
  #line 231 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1401
  = _M0FPB13shiftright128(_M0L7_2alow0S448, _M0L3sumS450, _M0L5deltaS452);
  _M0L6_2atmpS1408 = _M0L1iS438 / 16;
  #line 232 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1405
  = _M0MPC15array13ReadOnlyArray2atGjE(_M0FPB14gPOW5__OFFSETS, _M0L6_2atmpS1408);
  _M0L6_2atmpS1407 = _M0L1iS438 % 16;
  _M0L6_2atmpS1406 = _M0L6_2atmpS1407 << 1;
  _M0L6_2atmpS1404 = _M0L6_2atmpS1405 >> (_M0L6_2atmpS1406 & 31);
  _M0L6_2atmpS1403 = _M0L6_2atmpS1404 & 3u;
  #line 232 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1402 = _M0MPC14uint4UInt10to__uint64(_M0L6_2atmpS1403);
  _M0L1aS453 = _M0L6_2atmpS1401 + _M0L6_2atmpS1402;
  _M0L6_2atmpS1400 = _M0Lm5high1S451;
  #line 233 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L1bS454
  = _M0FPB13shiftright128(_M0L3sumS450, _M0L6_2atmpS1400, _M0L5deltaS452);
  return (struct _M0TPB8Pow5Pair){.$0 = _M0L1aS453, .$1 = _M0L1bS454};
}

struct _M0TPB19MulShiftAll64Result _M0FPB13mulShiftAll64(
  uint64_t _M0L1mS411,
  struct _M0TPB8Pow5Pair _M0L3mulS408,
  int32_t _M0L1jS424,
  int32_t _M0L7mmShiftS426
) {
  uint64_t _M0L7_2amul0S407;
  uint64_t _M0L7_2amul1S409;
  uint64_t _M0L1mS410;
  struct _M0TPB7Umul128 _M0L7_2abindS412;
  uint64_t _M0L5_2aloS413;
  uint64_t _M0L6_2atmpS414;
  struct _M0TPB7Umul128 _M0L7_2abindS415;
  uint64_t _M0L6_2alo2S416;
  uint64_t _M0L6_2ahi2S417;
  uint64_t _M0L3midS418;
  uint64_t _M0L6_2atmpS1398;
  uint64_t _M0L2hiS419;
  uint64_t _M0L3lo2S420;
  uint64_t _M0L6_2atmpS1396;
  uint64_t _M0L6_2atmpS1397;
  uint64_t _M0L4mid2S421;
  uint64_t _M0L6_2atmpS1395;
  uint64_t _M0L3hi2S422;
  int32_t _M0L6_2atmpS1394;
  int32_t _M0L6_2atmpS1393;
  uint64_t _M0L2vpS423;
  uint64_t _M0Lm2vmS425;
  int32_t _M0L6_2atmpS1392;
  int32_t _M0L6_2atmpS1391;
  uint64_t _M0L2vrS436;
  uint64_t _M0L6_2atmpS1390;
  #line 129 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2amul0S407 = _M0L3mulS408.$0;
  _M0L7_2amul1S409 = _M0L3mulS408.$1;
  _M0L1mS410 = _M0L1mS411 << 1;
  #line 137 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS412 = _M0FPB7umul128(_M0L1mS410, _M0L7_2amul0S407);
  _M0L5_2aloS413 = _M0L7_2abindS412.$0;
  _M0L6_2atmpS414 = _M0L7_2abindS412.$1;
  #line 138 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS415 = _M0FPB7umul128(_M0L1mS410, _M0L7_2amul1S409);
  _M0L6_2alo2S416 = _M0L7_2abindS415.$0;
  _M0L6_2ahi2S417 = _M0L7_2abindS415.$1;
  _M0L3midS418 = _M0L6_2atmpS414 + _M0L6_2alo2S416;
  if (_M0L3midS418 < _M0L6_2atmpS414) {
    _M0L6_2atmpS1398 = 1ull;
  } else {
    _M0L6_2atmpS1398 = 0ull;
  }
  _M0L2hiS419 = _M0L6_2ahi2S417 + _M0L6_2atmpS1398;
  _M0L3lo2S420 = _M0L5_2aloS413 + _M0L7_2amul0S407;
  _M0L6_2atmpS1396 = _M0L3midS418 + _M0L7_2amul1S409;
  if (_M0L3lo2S420 < _M0L5_2aloS413) {
    _M0L6_2atmpS1397 = 1ull;
  } else {
    _M0L6_2atmpS1397 = 0ull;
  }
  _M0L4mid2S421 = _M0L6_2atmpS1396 + _M0L6_2atmpS1397;
  if (_M0L4mid2S421 < _M0L3midS418) {
    _M0L6_2atmpS1395 = 1ull;
  } else {
    _M0L6_2atmpS1395 = 0ull;
  }
  _M0L3hi2S422 = _M0L2hiS419 + _M0L6_2atmpS1395;
  _M0L6_2atmpS1394 = _M0L1jS424 - 64;
  _M0L6_2atmpS1393 = _M0L6_2atmpS1394 - 1;
  #line 144 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L2vpS423
  = _M0FPB13shiftright128(_M0L4mid2S421, _M0L3hi2S422, _M0L6_2atmpS1393);
  _M0Lm2vmS425 = 0ull;
  if (_M0L7mmShiftS426) {
    uint64_t _M0L3lo3S427 = _M0L5_2aloS413 - _M0L7_2amul0S407;
    uint64_t _M0L6_2atmpS1380 = _M0L3midS418 - _M0L7_2amul1S409;
    uint64_t _M0L6_2atmpS1381;
    uint64_t _M0L4mid3S428;
    uint64_t _M0L6_2atmpS1379;
    uint64_t _M0L3hi3S429;
    int32_t _M0L6_2atmpS1378;
    int32_t _M0L6_2atmpS1377;
    if (_M0L5_2aloS413 < _M0L3lo3S427) {
      _M0L6_2atmpS1381 = 1ull;
    } else {
      _M0L6_2atmpS1381 = 0ull;
    }
    _M0L4mid3S428 = _M0L6_2atmpS1380 - _M0L6_2atmpS1381;
    if (_M0L3midS418 < _M0L4mid3S428) {
      _M0L6_2atmpS1379 = 1ull;
    } else {
      _M0L6_2atmpS1379 = 0ull;
    }
    _M0L3hi3S429 = _M0L2hiS419 - _M0L6_2atmpS1379;
    _M0L6_2atmpS1378 = _M0L1jS424 - 64;
    _M0L6_2atmpS1377 = _M0L6_2atmpS1378 - 1;
    #line 150 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0Lm2vmS425
    = _M0FPB13shiftright128(_M0L4mid3S428, _M0L3hi3S429, _M0L6_2atmpS1377);
  } else {
    uint64_t _M0L3lo3S430 = _M0L5_2aloS413 + _M0L5_2aloS413;
    uint64_t _M0L6_2atmpS1388 = _M0L3midS418 + _M0L3midS418;
    uint64_t _M0L6_2atmpS1389;
    uint64_t _M0L4mid3S431;
    uint64_t _M0L6_2atmpS1386;
    uint64_t _M0L6_2atmpS1387;
    uint64_t _M0L3hi3S432;
    uint64_t _M0L3lo4S433;
    uint64_t _M0L6_2atmpS1384;
    uint64_t _M0L6_2atmpS1385;
    uint64_t _M0L4mid4S434;
    uint64_t _M0L6_2atmpS1383;
    uint64_t _M0L3hi4S435;
    int32_t _M0L6_2atmpS1382;
    if (_M0L3lo3S430 < _M0L5_2aloS413) {
      _M0L6_2atmpS1389 = 1ull;
    } else {
      _M0L6_2atmpS1389 = 0ull;
    }
    _M0L4mid3S431 = _M0L6_2atmpS1388 + _M0L6_2atmpS1389;
    _M0L6_2atmpS1386 = _M0L2hiS419 + _M0L2hiS419;
    if (_M0L4mid3S431 < _M0L3midS418) {
      _M0L6_2atmpS1387 = 1ull;
    } else {
      _M0L6_2atmpS1387 = 0ull;
    }
    _M0L3hi3S432 = _M0L6_2atmpS1386 + _M0L6_2atmpS1387;
    _M0L3lo4S433 = _M0L3lo3S430 - _M0L7_2amul0S407;
    _M0L6_2atmpS1384 = _M0L4mid3S431 - _M0L7_2amul1S409;
    if (_M0L3lo3S430 < _M0L3lo4S433) {
      _M0L6_2atmpS1385 = 1ull;
    } else {
      _M0L6_2atmpS1385 = 0ull;
    }
    _M0L4mid4S434 = _M0L6_2atmpS1384 - _M0L6_2atmpS1385;
    if (_M0L4mid3S431 < _M0L4mid4S434) {
      _M0L6_2atmpS1383 = 1ull;
    } else {
      _M0L6_2atmpS1383 = 0ull;
    }
    _M0L3hi4S435 = _M0L3hi3S432 - _M0L6_2atmpS1383;
    _M0L6_2atmpS1382 = _M0L1jS424 - 64;
    #line 158 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0Lm2vmS425
    = _M0FPB13shiftright128(_M0L4mid4S434, _M0L3hi4S435, _M0L6_2atmpS1382);
  }
  _M0L6_2atmpS1392 = _M0L1jS424 - 64;
  _M0L6_2atmpS1391 = _M0L6_2atmpS1392 - 1;
  #line 160 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L2vrS436
  = _M0FPB13shiftright128(_M0L3midS418, _M0L2hiS419, _M0L6_2atmpS1391);
  _M0L6_2atmpS1390 = _M0Lm2vmS425;
  return (struct _M0TPB19MulShiftAll64Result){.$0 = _M0L2vrS436,
                                                .$1 = _M0L2vpS423,
                                                .$2 = _M0L6_2atmpS1390};
}

int32_t _M0FPB18multipleOfPowerOf2(
  uint64_t _M0L5valueS405,
  int32_t _M0L1pS406
) {
  uint64_t _M0L6_2atmpS1376;
  uint64_t _M0L6_2atmpS1375;
  uint64_t _M0L6_2atmpS1374;
  #line 124 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1376 = 1ull << (_M0L1pS406 & 63);
  _M0L6_2atmpS1375 = _M0L6_2atmpS1376 - 1ull;
  _M0L6_2atmpS1374 = _M0L5valueS405 & _M0L6_2atmpS1375;
  return _M0L6_2atmpS1374 == 0ull;
}

int32_t _M0FPB18multipleOfPowerOf5(
  uint64_t _M0L5valueS403,
  int32_t _M0L1pS404
) {
  int32_t _M0L6_2atmpS1373;
  #line 119 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  #line 120 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1373 = _M0FPB10pow5Factor(_M0L5valueS403);
  return _M0L6_2atmpS1373 >= _M0L1pS404;
}

int32_t _M0FPB10pow5Factor(uint64_t _M0L5valueS398) {
  uint64_t _M0L6_2atmpS1364;
  uint64_t _M0L6_2atmpS1365;
  uint64_t _M0L6_2atmpS1366;
  uint64_t _M0L6_2atmpS1367;
  uint64_t _M0L6_2atmpS1372;
  int32_t _M0L5countS399;
  uint64_t _M0L1vS400;
  #line 94 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1364 = _M0L5valueS398 % 5ull;
  if (_M0L6_2atmpS1364 != 0ull) {
    return 0;
  }
  _M0L6_2atmpS1365 = _M0L5valueS398 % 25ull;
  if (_M0L6_2atmpS1365 != 0ull) {
    return 1;
  }
  _M0L6_2atmpS1366 = _M0L5valueS398 % 125ull;
  if (_M0L6_2atmpS1366 != 0ull) {
    return 2;
  }
  _M0L6_2atmpS1367 = _M0L5valueS398 % 625ull;
  if (_M0L6_2atmpS1367 != 0ull) {
    return 3;
  }
  _M0L6_2atmpS1372 = _M0L5valueS398 / 625ull;
  _M0L5countS399 = 4;
  _M0L1vS400 = _M0L6_2atmpS1372;
  while (1) {
    if (_M0L1vS400 > 0ull) {
      uint64_t _M0L6_2atmpS1368 = _M0L1vS400 % 5ull;
      int32_t _M0L6_2atmpS1369;
      uint64_t _M0L6_2atmpS1370;
      if (_M0L6_2atmpS1368 != 0ull) {
        return _M0L5countS399;
      }
      _M0L6_2atmpS1369 = _M0L5countS399 + 1;
      _M0L6_2atmpS1370 = _M0L1vS400 / 5ull;
      _M0L5countS399 = _M0L6_2atmpS1369;
      _M0L1vS400 = _M0L6_2atmpS1370;
      continue;
    } else {
      struct _M0TPB13StringBuilder* _M0L18_2astring__builderS402;
      moonbit_string_t _M0L6_2atmpS1371;
      int32_t _result_2189;
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0L18_2astring__builderS402
      = _M0MPB13StringBuilder21StringBuilder_2einner(25);
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS402, (moonbit_string_t)moonbit_string_literal_5.data);
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0MPB13StringBuilder13write__objectGmE(_M0L18_2astring__builderS402, _M0L5valueS398);
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0L6_2atmpS1371
      = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS402);
      moonbit_decref(_M0L18_2astring__builderS402);
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _result_2189 = _M0FPC15abort5abortGiE(_M0L6_2atmpS1371);
      moonbit_decref(_M0L6_2atmpS1371);
      return _result_2189;
    }
    break;
  }
}

uint64_t _M0FPB13shiftright128(
  uint64_t _M0L2loS397,
  uint64_t _M0L2hiS395,
  int32_t _M0L4distS396
) {
  int32_t _M0L6_2atmpS1363;
  uint64_t _M0L6_2atmpS1361;
  uint64_t _M0L6_2atmpS1362;
  #line 89 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1363 = 64 - _M0L4distS396;
  _M0L6_2atmpS1361 = _M0L2hiS395 << (_M0L6_2atmpS1363 & 63);
  _M0L6_2atmpS1362 = _M0L2loS397 >> (_M0L4distS396 & 63);
  return _M0L6_2atmpS1361 | _M0L6_2atmpS1362;
}

struct _M0TPB7Umul128 _M0FPB7umul128(
  uint64_t _M0L1aS385,
  uint64_t _M0L1bS388
) {
  uint64_t _M0L3aLoS384;
  uint64_t _M0L3aHiS386;
  uint64_t _M0L3bLoS387;
  uint64_t _M0L3bHiS389;
  uint64_t _M0L1xS390;
  uint64_t _M0L6_2atmpS1359;
  uint64_t _M0L6_2atmpS1360;
  uint64_t _M0L1yS391;
  uint64_t _M0L6_2atmpS1357;
  uint64_t _M0L6_2atmpS1358;
  uint64_t _M0L1zS392;
  uint64_t _M0L6_2atmpS1355;
  uint64_t _M0L6_2atmpS1356;
  uint64_t _M0L6_2atmpS1353;
  uint64_t _M0L6_2atmpS1354;
  uint64_t _M0L1wS393;
  uint64_t _M0L2loS394;
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L3aLoS384 = _M0L1aS385 & 4294967295ull;
  _M0L3aHiS386 = _M0L1aS385 >> 32;
  _M0L3bLoS387 = _M0L1bS388 & 4294967295ull;
  _M0L3bHiS389 = _M0L1bS388 >> 32;
  _M0L1xS390 = _M0L3aLoS384 * _M0L3bLoS387;
  _M0L6_2atmpS1359 = _M0L3aHiS386 * _M0L3bLoS387;
  _M0L6_2atmpS1360 = _M0L1xS390 >> 32;
  _M0L1yS391 = _M0L6_2atmpS1359 + _M0L6_2atmpS1360;
  _M0L6_2atmpS1357 = _M0L3aLoS384 * _M0L3bHiS389;
  _M0L6_2atmpS1358 = _M0L1yS391 & 4294967295ull;
  _M0L1zS392 = _M0L6_2atmpS1357 + _M0L6_2atmpS1358;
  _M0L6_2atmpS1355 = _M0L3aHiS386 * _M0L3bHiS389;
  _M0L6_2atmpS1356 = _M0L1yS391 >> 32;
  _M0L6_2atmpS1353 = _M0L6_2atmpS1355 + _M0L6_2atmpS1356;
  _M0L6_2atmpS1354 = _M0L1zS392 >> 32;
  _M0L1wS393 = _M0L6_2atmpS1353 + _M0L6_2atmpS1354;
  _M0L2loS394 = _M0L1aS385 * _M0L1bS388;
  return (struct _M0TPB7Umul128){.$0 = _M0L2loS394, .$1 = _M0L1wS393};
}

moonbit_string_t _M0FPB19string__from__bytes(
  moonbit_bytes_t _M0L5bytesS382,
  int32_t _M0L4fromS379,
  int32_t _M0L2toS378
) {
  int32_t _M0L3lenS377;
  int32_t _M0L6_2atmpS1352;
  uint16_t* _M0L6bufferS380;
  int32_t _M0L1iS381;
  #line 52 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L3lenS377 = _M0L2toS378 - _M0L4fromS379;
  #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1352 = _M0IPC16uint166UInt16PB7Default7default();
  _M0L6bufferS380
  = (uint16_t*)moonbit_make_string(_M0L3lenS377, _M0L6_2atmpS1352);
  _M0L1iS381 = 0;
  while (1) {
    if (_M0L1iS381 < _M0L3lenS377) {
      int32_t _M0L6_2atmpS1350 = _M0L4fromS379 + _M0L1iS381;
      int32_t _M0L6_2atmpS1349;
      int32_t _M0L6_2atmpS1348;
      int32_t _M0L6_2atmpS1351;
      if (
        _M0L6_2atmpS1350 < 0
        || _M0L6_2atmpS1350 >= Moonbit_array_length(_M0L5bytesS382)
      ) {
        #line 56 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6_2atmpS1349 = (int32_t)_M0L5bytesS382[_M0L6_2atmpS1350];
      _M0L6_2atmpS1348 = (uint16_t)_M0L6_2atmpS1349;
      if (
        _M0L1iS381 < 0 || _M0L1iS381 >= Moonbit_array_length(_M0L6bufferS380)
      ) {
        #line 56 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6bufferS380[_M0L1iS381] = _M0L6_2atmpS1348;
      _M0L6_2atmpS1351 = _M0L1iS381 + 1;
      _M0L1iS381 = _M0L6_2atmpS1351;
      continue;
    }
    break;
  }
  return _M0L6bufferS380;
}

int32_t _M0FPB9log10Pow2(int32_t _M0L1eS376) {
  int32_t _M0L6_2atmpS1347;
  uint32_t _M0L6_2atmpS1346;
  uint32_t _M0L6_2atmpS1345;
  #line 44 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1347 = _M0L1eS376 * 78913;
  _M0L6_2atmpS1346 = *(uint32_t*)&_M0L6_2atmpS1347;
  _M0L6_2atmpS1345 = _M0L6_2atmpS1346 >> 18;
  return *(int32_t*)&_M0L6_2atmpS1345;
}

int32_t _M0FPB9log10Pow5(int32_t _M0L1eS375) {
  int32_t _M0L6_2atmpS1344;
  uint32_t _M0L6_2atmpS1343;
  uint32_t _M0L6_2atmpS1342;
  #line 37 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1344 = _M0L1eS375 * 732923;
  _M0L6_2atmpS1343 = *(uint32_t*)&_M0L6_2atmpS1344;
  _M0L6_2atmpS1342 = _M0L6_2atmpS1343 >> 20;
  return *(int32_t*)&_M0L6_2atmpS1342;
}

moonbit_string_t _M0FPB18copy__special__str(
  int32_t _M0L4signS373,
  int32_t _M0L8exponentS374,
  int32_t _M0L8mantissaS371
) {
  moonbit_string_t _M0L1sS372;
  moonbit_string_t _result_2192;
  #line 23 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  if (_M0L8mantissaS371) {
    return (moonbit_string_t)moonbit_string_literal_6.data;
  }
  if (_M0L4signS373) {
    _M0L1sS372 = (moonbit_string_t)moonbit_string_literal_7.data;
  } else {
    _M0L1sS372 = (moonbit_string_t)moonbit_string_literal_0.data;
  }
  if (_M0L8exponentS374) {
    moonbit_string_t _result_2191;
    #line 29 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _result_2191
    = moonbit_add_string(_M0L1sS372, (moonbit_string_t)moonbit_string_literal_8.data);
    moonbit_decref(_M0L1sS372);
    return _result_2191;
  }
  #line 31 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _result_2192
  = moonbit_add_string(_M0L1sS372, (moonbit_string_t)moonbit_string_literal_9.data);
  moonbit_decref(_M0L1sS372);
  return _result_2192;
}

int32_t _M0FPB8pow5bits(int32_t _M0L1eS370) {
  int32_t _M0L6_2atmpS1341;
  uint32_t _M0L6_2atmpS1340;
  uint32_t _M0L6_2atmpS1339;
  int32_t _M0L6_2atmpS1338;
  #line 18 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1341 = _M0L1eS370 * 1217359;
  _M0L6_2atmpS1340 = *(uint32_t*)&_M0L6_2atmpS1341;
  _M0L6_2atmpS1339 = _M0L6_2atmpS1340 >> 19;
  _M0L6_2atmpS1338 = *(int32_t*)&_M0L6_2atmpS1339;
  return _M0L6_2atmpS1338 + 1;
}

int32_t _M0MPC16double6Double7to__int(double _M0L4selfS369) {
  #line 43 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_to_int.mbt"
  if (_M0L4selfS369 != _M0L4selfS369) {
    return 0;
  } else if (_M0L4selfS369 >= 0x1.fffffffcp+30) {
    return 2147483647;
  } else if (_M0L4selfS369 <= -0x1p+31) {
    return (int32_t)0x80000000;
  } else {
    return (int32_t)_M0L4selfS369;
  }
}

int64_t _M0MPC16double6Double9to__int64(double _M0L4selfS368) {
  #line 46 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_to_int64_native.mbt"
  if (_M0L4selfS368 != _M0L4selfS368) {
    return 0ll;
  } else if (_M0L4selfS368 >= 0x1p+63) {
    return 9223372036854775807ll;
  } else if (_M0L4selfS368 <= -0x1p+63) {
    return (int64_t)0x8000000000000000ll;
  } else {
    return (int64_t)_M0L4selfS368;
  }
}

struct _M0TPB5ArrayGfE* _M0MPC15array5Array20unsafe__make__uninitGfE(
  int32_t _M0L3lenS364
) {
  float* _M0L6_2atmpS1334;
  struct _M0TPB5ArrayGfE* _block_2193;
  #line 30 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1334 = (float*)moonbit_make_float_array_raw(_M0L3lenS364);
  _block_2193
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_block_2193)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 0, 0);
  _block_2193->$0 = _M0L6_2atmpS1334;
  _block_2193->$1 = _M0L3lenS364;
  return _block_2193;
}

struct _M0TPB5ArrayGbE* _M0MPC15array5Array20unsafe__make__uninitGbE(
  int32_t _M0L3lenS365
) {
  uint8_t* _M0L6_2atmpS1335;
  struct _M0TPB5ArrayGbE* _block_2194;
  #line 30 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1335 = (uint8_t*)moonbit_make_bytes_raw(_M0L3lenS365);
  _block_2194
  = (struct _M0TPB5ArrayGbE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGbE));
  Moonbit_object_header(_block_2194)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 50, 0);
  _block_2194->$0 = _M0L6_2atmpS1335;
  _block_2194->$1 = _M0L3lenS365;
  return _block_2194;
}

struct _M0TPB5ArrayGiE* _M0MPC15array5Array20unsafe__make__uninitGiE(
  int32_t _M0L3lenS366
) {
  int32_t* _M0L6_2atmpS1336;
  struct _M0TPB5ArrayGiE* _block_2195;
  #line 30 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1336 = (int32_t*)moonbit_make_int32_array_raw(_M0L3lenS366);
  _block_2195
  = (struct _M0TPB5ArrayGiE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGiE));
  Moonbit_object_header(_block_2195)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 3, 0);
  _block_2195->$0 = _M0L6_2atmpS1336;
  _block_2195->$1 = _M0L3lenS366;
  return _block_2195;
}

struct _M0TPB5ArrayGRPB5ArrayGfEE* _M0MPC15array5Array20unsafe__make__uninitGRPB5ArrayGfEE(
  int32_t _M0L3lenS367
) {
  struct _M0TPB5ArrayGfE** _M0L6_2atmpS1337;
  struct _M0TPB5ArrayGRPB5ArrayGfEE* _block_2196;
  #line 30 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1337
  = (struct _M0TPB5ArrayGfE**)moonbit_make_ref_array(_M0L3lenS367, 0);
  _block_2196
  = (struct _M0TPB5ArrayGRPB5ArrayGfEE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGRPB5ArrayGfEE));
  Moonbit_object_header(_block_2196)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 53, 0);
  _block_2196->$0 = _M0L6_2atmpS1337;
  _block_2196->$1 = _M0L3lenS367;
  return _block_2196;
}

uint64_t _M0MPC15array13ReadOnlyArray2atGmE(
  uint64_t* _M0L4selfS360,
  int32_t _M0L5indexS361
) {
  uint64_t* _M0L6_2atmpS1332;
  #line 38 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\readonlyarray.mbt"
  _M0L6_2atmpS1332 = _M0L4selfS360;
  if (
    _M0L5indexS361 < 0
    || _M0L5indexS361 >= Moonbit_array_length(_M0L6_2atmpS1332)
  ) {
    #line 40 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\readonlyarray.mbt"
    moonbit_panic();
  }
  return (uint64_t)_M0L6_2atmpS1332[_M0L5indexS361];
}

uint32_t _M0MPC15array13ReadOnlyArray2atGjE(
  uint32_t* _M0L4selfS362,
  int32_t _M0L5indexS363
) {
  uint32_t* _M0L6_2atmpS1333;
  #line 38 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\readonlyarray.mbt"
  _M0L6_2atmpS1333 = _M0L4selfS362;
  if (
    _M0L5indexS363 < 0
    || _M0L5indexS363 >= Moonbit_array_length(_M0L6_2atmpS1333)
  ) {
    #line 40 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\readonlyarray.mbt"
    moonbit_panic();
  }
  return (uint32_t)_M0L6_2atmpS1333[_M0L5indexS363];
}

moonbit_string_t _M0IPC16uint646UInt64PB4Show10to__string(
  uint64_t _M0L4selfS359
) {
  #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  #line 51 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  return _M0MPC16uint646UInt6418to__string_2einner(_M0L4selfS359, 10);
}

moonbit_string_t _M0IPC13int3IntPB4Show10to__string(int32_t _M0L4selfS358) {
  #line 35 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  #line 36 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  return _M0MPC13int3Int18to__string_2einner(_M0L4selfS358, 10);
}

uint64_t _M0MPC14uint4UInt10to__uint64(uint32_t _M0L4selfS357) {
  #line 2576 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  return (uint64_t)_M0L4selfS357;
}

int32_t _M0MPC15array5Array4pushGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS351,
  float _M0L5valueS353
) {
  int32_t _M0L3lenS1318;
  float* _M0L6_2atmpS1320;
  int32_t _M0L6_2atmpS1319;
  int32_t _M0L6lengthS352;
  float* _M0L3bufS1323;
  int32_t _M0L6_2atmpS1324;
  #line 406 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L3lenS1318 = _M0L4selfS351->$1;
  #line 408 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1320 = _M0MPC15array5Array6bufferGfE(_M0L4selfS351);
  _M0L6_2atmpS1319 = Moonbit_array_length(_M0L6_2atmpS1320);
  moonbit_decref(_M0L6_2atmpS1320);
  if (_M0L3lenS1318 == _M0L6_2atmpS1319) {
    int32_t _M0L3lenS1322 = _M0L4selfS351->$1;
    int32_t _M0L6_2atmpS1321 = _M0L3lenS1322 + 1;
    #line 409 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0MPC15array5Array7reallocGfE(_M0L4selfS351, _M0L6_2atmpS1321);
  }
  _M0L6lengthS352 = _M0L4selfS351->$1;
  _M0L3bufS1323 = _M0L4selfS351->$0;
  _M0L3bufS1323[_M0L6lengthS352] = _M0L5valueS353;
  _M0L6_2atmpS1324 = _M0L6lengthS352 + 1;
  _M0L4selfS351->$1 = _M0L6_2atmpS1324;
  return 0;
}

int32_t _M0MPC15array5Array4pushGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS354,
  int32_t _M0L5valueS356
) {
  int32_t _M0L3lenS1325;
  int32_t* _M0L6_2atmpS1327;
  int32_t _M0L6_2atmpS1326;
  int32_t _M0L6lengthS355;
  int32_t* _M0L3bufS1330;
  int32_t _M0L6_2atmpS1331;
  #line 406 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L3lenS1325 = _M0L4selfS354->$1;
  #line 408 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1327 = _M0MPC15array5Array6bufferGiE(_M0L4selfS354);
  _M0L6_2atmpS1326 = Moonbit_array_length(_M0L6_2atmpS1327);
  moonbit_decref(_M0L6_2atmpS1327);
  if (_M0L3lenS1325 == _M0L6_2atmpS1326) {
    int32_t _M0L3lenS1329 = _M0L4selfS354->$1;
    int32_t _M0L6_2atmpS1328 = _M0L3lenS1329 + 1;
    #line 409 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0MPC15array5Array7reallocGiE(_M0L4selfS354, _M0L6_2atmpS1328);
  }
  _M0L6lengthS355 = _M0L4selfS354->$1;
  _M0L3bufS1330 = _M0L4selfS354->$0;
  _M0L3bufS1330[_M0L6lengthS355] = _M0L5valueS356;
  _M0L6_2atmpS1331 = _M0L6lengthS355 + 1;
  _M0L4selfS354->$1 = _M0L6_2atmpS1331;
  return 0;
}

int32_t _M0MPC15array5Array7reallocGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS344,
  int32_t _M0L8requiredS346
) {
  int32_t _M0L8old__capS343;
  int32_t _M0L3lenS1316;
  int32_t _M0L8new__capS345;
  #line 302 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  #line 304 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__capS343 = _M0MPC15array5Array8capacityGfE(_M0L4selfS344);
  _M0L3lenS1316 = _M0L4selfS344->$1;
  #line 305 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__capS345
  = _M0FPB23array__growth__capacity(_M0L8old__capS343, _M0L3lenS1316, _M0L8requiredS346);
  #line 306 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0MPC15array5Array14resize__bufferGfE(_M0L4selfS344, _M0L8new__capS345);
  return 0;
}

int32_t _M0MPC15array5Array7reallocGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS348,
  int32_t _M0L8requiredS350
) {
  int32_t _M0L8old__capS347;
  int32_t _M0L3lenS1317;
  int32_t _M0L8new__capS349;
  #line 302 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  #line 304 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__capS347 = _M0MPC15array5Array8capacityGiE(_M0L4selfS348);
  _M0L3lenS1317 = _M0L4selfS348->$1;
  #line 305 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__capS349
  = _M0FPB23array__growth__capacity(_M0L8old__capS347, _M0L3lenS1317, _M0L8requiredS350);
  #line 306 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0MPC15array5Array14resize__bufferGiE(_M0L4selfS348, _M0L8new__capS349);
  return 0;
}

int32_t _M0MPC15array5Array14resize__bufferGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS332,
  int32_t _M0L13new__capacityS335
) {
  float* _M0L8old__bufS331;
  int32_t _M0L3lenS333;
  int32_t _M0L9copy__lenS334;
  float* _M0L8new__bufS336;
  float* _M0L6_2aoldS2109;
  #line 242 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__bufS331 = _M0L4selfS332->$0;
  _M0L3lenS333 = _M0L4selfS332->$1;
  if (_M0L3lenS333 < _M0L13new__capacityS335) {
    _M0L9copy__lenS334 = _M0L3lenS333;
  } else {
    _M0L9copy__lenS334 = _M0L13new__capacityS335;
  }
  moonbit_incref(_M0L8old__bufS331);
  #line 249 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__bufS336
  = _M0MPB18UninitializedArray23make__and__blit_2einnerGfE(_M0L8old__bufS331, _M0L13new__capacityS335, _M0L9copy__lenS334, 0, 0);
  _M0L6_2aoldS2109 = _M0L4selfS332->$0;
  moonbit_decref(_M0L6_2aoldS2109);
  _M0L4selfS332->$0 = _M0L8new__bufS336;
  return 0;
}

int32_t _M0MPC15array5Array14resize__bufferGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS338,
  int32_t _M0L13new__capacityS341
) {
  int32_t* _M0L8old__bufS337;
  int32_t _M0L3lenS339;
  int32_t _M0L9copy__lenS340;
  int32_t* _M0L8new__bufS342;
  int32_t* _M0L6_2aoldS2110;
  #line 242 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__bufS337 = _M0L4selfS338->$0;
  _M0L3lenS339 = _M0L4selfS338->$1;
  if (_M0L3lenS339 < _M0L13new__capacityS341) {
    _M0L9copy__lenS340 = _M0L3lenS339;
  } else {
    _M0L9copy__lenS340 = _M0L13new__capacityS341;
  }
  moonbit_incref(_M0L8old__bufS337);
  #line 249 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__bufS342
  = _M0MPB18UninitializedArray23make__and__blit_2einnerGiE(_M0L8old__bufS337, _M0L13new__capacityS341, _M0L9copy__lenS340, 0, 0);
  _M0L6_2aoldS2110 = _M0L4selfS338->$0;
  moonbit_decref(_M0L6_2aoldS2110);
  _M0L4selfS338->$0 = _M0L8new__bufS342;
  return 0;
}

int32_t _M0MPC15array5Array8capacityGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS329
) {
  float* _M0L6_2atmpS1314;
  int32_t _result_2197;
  #line 132 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L6_2atmpS1314 = _M0MPC15array5Array6bufferGfE(_M0L4selfS329);
  _result_2197 = Moonbit_array_length(_M0L6_2atmpS1314);
  moonbit_decref(_M0L6_2atmpS1314);
  return _result_2197;
}

int32_t _M0MPC15array5Array8capacityGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS330
) {
  int32_t* _M0L6_2atmpS1315;
  int32_t _result_2198;
  #line 132 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L6_2atmpS1315 = _M0MPC15array5Array6bufferGiE(_M0L4selfS330);
  _result_2198 = Moonbit_array_length(_M0L6_2atmpS1315);
  moonbit_decref(_M0L6_2atmpS1315);
  return _result_2198;
}

int32_t _M0FPB23array__growth__capacity(
  int32_t _M0L7currentS325,
  int32_t _M0L3lenS323,
  int32_t _M0L8requiredS322
) {
  int32_t _M0L5startS324;
  int32_t _M0L5spaceS326;
  #line 200 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  if (_M0L8requiredS322 < _M0L3lenS323) {
    #line 203 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_10.data);
  }
  if (_M0L7currentS325 == 0) {
    _M0L5startS324 = 8;
  } else {
    _M0L5startS324 = _M0L7currentS325;
  }
  _M0L5spaceS326 = _M0L5startS324;
  while (1) {
    if (_M0L5spaceS326 < _M0L8requiredS322) {
      int32_t _M0L4nextS327 = _M0L5spaceS326 * 2;
      if (_M0L4nextS327 <= _M0L5spaceS326) {
        return _M0L8requiredS322;
      }
      _M0L5spaceS326 = _M0L4nextS327;
      continue;
    } else {
      return _M0L5spaceS326;
    }
    break;
  }
}

int32_t _M0MPC15array5Array6lengthGfE(struct _M0TPB5ArrayGfE* _M0L4selfS320) {
  #line 147 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  return _M0L4selfS320->$1;
}

int32_t _M0MPC15array5Array6lengthGbE(struct _M0TPB5ArrayGbE* _M0L4selfS321) {
  #line 147 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  return _M0L4selfS321->$1;
}

float* _M0MPC15array5Array6bufferGfE(struct _M0TPB5ArrayGfE* _M0L4selfS316) {
  float* _M0L8_2afieldS2111;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS2111 = _M0L4selfS316->$0;
  moonbit_incref(_M0L8_2afieldS2111);
  return _M0L8_2afieldS2111;
}

uint8_t* _M0MPC15array5Array6bufferGbE(struct _M0TPB5ArrayGbE* _M0L4selfS317) {
  uint8_t* _M0L8_2afieldS2112;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS2112 = _M0L4selfS317->$0;
  moonbit_incref(_M0L8_2afieldS2112);
  return _M0L8_2afieldS2112;
}

int32_t* _M0MPC15array5Array6bufferGiE(struct _M0TPB5ArrayGiE* _M0L4selfS318) {
  int32_t* _M0L8_2afieldS2113;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS2113 = _M0L4selfS318->$0;
  moonbit_incref(_M0L8_2afieldS2113);
  return _M0L8_2afieldS2113;
}

struct _M0TPB5ArrayGfE** _M0MPC15array5Array6bufferGRPB5ArrayGfEE(
  struct _M0TPB5ArrayGRPB5ArrayGfEE* _M0L4selfS319
) {
  struct _M0TPB5ArrayGfE** _M0L8_2afieldS2114;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS2114 = _M0L4selfS319->$0;
  moonbit_incref(_M0L8_2afieldS2114);
  return _M0L8_2afieldS2114;
}

moonbit_string_t _M0IPC16string6StringPB4Show10to__string(
  moonbit_string_t _M0L4selfS315
) {
  #line 222 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  moonbit_incref(_M0L4selfS315);
  return _M0L4selfS315;
}

int32_t _M0IPB13StringBuilderPB6Logger11write__view(
  struct _M0TPB13StringBuilder* _M0L4selfS314,
  struct _M0TPC16string10StringView _M0L3strS312
) {
  int32_t _M0L3endS1312;
  int32_t _M0L5startS1313;
  int32_t _M0L8str__lenS311;
  int32_t _M0L3lenS1311;
  int32_t _M0L8requiredS313;
  uint16_t* _M0L4dataS1304;
  int32_t _M0L6_2atmpS1303;
  int32_t _if__result_2200;
  uint16_t* _M0L4dataS1305;
  int32_t _M0L3lenS1306;
  moonbit_string_t _M0L6_2atmpS1307;
  int32_t _M0L6_2atmpS1308;
  int32_t _M0L3lenS1310;
  int32_t _M0L6_2atmpS1309;
  #line 158 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L3endS1312 = _M0L3strS312.$2;
  _M0L5startS1313 = _M0L3strS312.$1;
  _M0L8str__lenS311 = _M0L3endS1312 - _M0L5startS1313;
  if (_M0L8str__lenS311 == 0) {
    return 0;
  }
  _M0L3lenS1311 = _M0L4selfS314->$1;
  _M0L8requiredS313 = _M0L3lenS1311 + _M0L8str__lenS311;
  _M0L4dataS1304 = _M0L4selfS314->$0;
  _M0L6_2atmpS1303 = Moonbit_array_length(_M0L4dataS1304);
  if (_M0L8requiredS313 > _M0L6_2atmpS1303) {
    _if__result_2200 = 1;
  } else {
    int32_t _M0L3lenS1302 = _M0L4selfS314->$1;
    _if__result_2200 = _M0L8requiredS313 < _M0L3lenS1302;
  }
  if (_if__result_2200) {
    #line 168 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0MPB13StringBuilder4grow(_M0L4selfS314, _M0L8requiredS313);
  }
  _M0L4dataS1305 = _M0L4selfS314->$0;
  _M0L3lenS1306 = _M0L4selfS314->$1;
  moonbit_incref(_M0L4dataS1305);
  #line 172 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L6_2atmpS1307 = _M0MPC16string10StringView4data(_M0L3strS312);
  #line 173 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L6_2atmpS1308 = _M0MPC16string10StringView13start__offset(_M0L3strS312);
  #line 170 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0MPC15array10FixedArray26unsafe__blit__from__string(_M0L4dataS1305, _M0L3lenS1306, _M0L6_2atmpS1307, _M0L6_2atmpS1308, _M0L8str__lenS311);
  moonbit_decref(_M0L4dataS1305);
  moonbit_decref(_M0L6_2atmpS1307);
  _M0L3lenS1310 = _M0L4selfS314->$1;
  _M0L6_2atmpS1309 = _M0L3lenS1310 + _M0L8str__lenS311;
  _M0L4selfS314->$1 = _M0L6_2atmpS1309;
  return 0;
}

moonbit_string_t _M0MPC16uint646UInt6418to__string_2einner(
  uint64_t _M0L4selfS303,
  int32_t _M0L5radixS302
) {
  uint16_t* _M0L6bufferS304;
  #line 607 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  if (_M0L5radixS302 < 2 || _M0L5radixS302 > 36) {
    #line 611 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_11.data);
  }
  if (_M0L4selfS303 == 0ull) {
    return (moonbit_string_t)moonbit_string_literal_4.data;
  }
  switch (_M0L5radixS302) {
    case 10: {
      int32_t _M0L3lenS305;
      uint16_t* _M0L6bufferS306;
      #line 622 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L3lenS305 = _M0FPB12dec__count64(_M0L4selfS303);
      _M0L6bufferS306 = (uint16_t*)moonbit_make_string(_M0L3lenS305, 0);
      #line 624 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0FPB22int64__to__string__dec(_M0L6bufferS306, _M0L4selfS303, 0, _M0L3lenS305);
      _M0L6bufferS304 = _M0L6bufferS306;
      break;
    }
    
    case 16: {
      int32_t _M0L3lenS307;
      uint16_t* _M0L6bufferS308;
      #line 628 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L3lenS307 = _M0FPB12hex__count64(_M0L4selfS303);
      _M0L6bufferS308 = (uint16_t*)moonbit_make_string(_M0L3lenS307, 0);
      #line 630 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0FPB22int64__to__string__hex(_M0L6bufferS308, _M0L4selfS303, 0, _M0L3lenS307);
      _M0L6bufferS304 = _M0L6bufferS308;
      break;
    }
    default: {
      int32_t _M0L3lenS309;
      uint16_t* _M0L6bufferS310;
      #line 634 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L3lenS309 = _M0FPB14radix__count64(_M0L4selfS303, _M0L5radixS302);
      _M0L6bufferS310 = (uint16_t*)moonbit_make_string(_M0L3lenS309, 0);
      #line 636 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0FPB26int64__to__string__generic(_M0L6bufferS310, _M0L4selfS303, 0, _M0L3lenS309, _M0L5radixS302);
      _M0L6bufferS304 = _M0L6bufferS310;
      break;
    }
  }
  return _M0L6bufferS304;
}

moonbit_string_t _M0MPC15int645Int6418to__string_2einner(
  int64_t _M0L4selfS286,
  int32_t _M0L5radixS285
) {
  int32_t _M0L12is__negativeS287;
  uint64_t _M0L3numS288;
  uint16_t* _M0L6bufferS289;
  #line 548 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  if (_M0L5radixS285 < 2 || _M0L5radixS285 > 36) {
    #line 552 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_11.data);
  }
  if (_M0L4selfS286 == 0ll) {
    return (moonbit_string_t)moonbit_string_literal_4.data;
  }
  _M0L12is__negativeS287 = _M0L4selfS286 < 0ll;
  if (_M0L12is__negativeS287) {
    int64_t _M0L6_2atmpS1301 = -_M0L4selfS286;
    _M0L3numS288 = *(uint64_t*)&_M0L6_2atmpS1301;
  } else {
    _M0L3numS288 = *(uint64_t*)&_M0L4selfS286;
  }
  switch (_M0L5radixS285) {
    case 10: {
      int32_t _M0L10digit__lenS290;
      int32_t _M0L6_2atmpS1298;
      int32_t _M0L10total__lenS291;
      uint16_t* _M0L6bufferS292;
      int32_t _M0L12digit__startS293;
      #line 573 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS290 = _M0FPB12dec__count64(_M0L3numS288);
      if (_M0L12is__negativeS287) {
        _M0L6_2atmpS1298 = 1;
      } else {
        _M0L6_2atmpS1298 = 0;
      }
      _M0L10total__lenS291 = _M0L10digit__lenS290 + _M0L6_2atmpS1298;
      _M0L6bufferS292
      = (uint16_t*)moonbit_make_string(_M0L10total__lenS291, 0);
      if (_M0L12is__negativeS287) {
        _M0L12digit__startS293 = 1;
      } else {
        _M0L12digit__startS293 = 0;
      }
      #line 577 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0FPB22int64__to__string__dec(_M0L6bufferS292, _M0L3numS288, _M0L12digit__startS293, _M0L10total__lenS291);
      _M0L6bufferS289 = _M0L6bufferS292;
      break;
    }
    
    case 16: {
      int32_t _M0L10digit__lenS294;
      int32_t _M0L6_2atmpS1299;
      int32_t _M0L10total__lenS295;
      uint16_t* _M0L6bufferS296;
      int32_t _M0L12digit__startS297;
      #line 581 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS294 = _M0FPB12hex__count64(_M0L3numS288);
      if (_M0L12is__negativeS287) {
        _M0L6_2atmpS1299 = 1;
      } else {
        _M0L6_2atmpS1299 = 0;
      }
      _M0L10total__lenS295 = _M0L10digit__lenS294 + _M0L6_2atmpS1299;
      _M0L6bufferS296
      = (uint16_t*)moonbit_make_string(_M0L10total__lenS295, 0);
      if (_M0L12is__negativeS287) {
        _M0L12digit__startS297 = 1;
      } else {
        _M0L12digit__startS297 = 0;
      }
      #line 585 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0FPB22int64__to__string__hex(_M0L6bufferS296, _M0L3numS288, _M0L12digit__startS297, _M0L10total__lenS295);
      _M0L6bufferS289 = _M0L6bufferS296;
      break;
    }
    default: {
      int32_t _M0L10digit__lenS298;
      int32_t _M0L6_2atmpS1300;
      int32_t _M0L10total__lenS299;
      uint16_t* _M0L6bufferS300;
      int32_t _M0L12digit__startS301;
      #line 589 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS298
      = _M0FPB14radix__count64(_M0L3numS288, _M0L5radixS285);
      if (_M0L12is__negativeS287) {
        _M0L6_2atmpS1300 = 1;
      } else {
        _M0L6_2atmpS1300 = 0;
      }
      _M0L10total__lenS299 = _M0L10digit__lenS298 + _M0L6_2atmpS1300;
      _M0L6bufferS300
      = (uint16_t*)moonbit_make_string(_M0L10total__lenS299, 0);
      if (_M0L12is__negativeS287) {
        _M0L12digit__startS301 = 1;
      } else {
        _M0L12digit__startS301 = 0;
      }
      #line 593 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0FPB26int64__to__string__generic(_M0L6bufferS300, _M0L3numS288, _M0L12digit__startS301, _M0L10total__lenS299, _M0L5radixS285);
      _M0L6bufferS289 = _M0L6bufferS300;
      break;
    }
  }
  if (_M0L12is__negativeS287) {
    _M0L6bufferS289[0] = 45;
  }
  return _M0L6bufferS289;
}

int32_t _M0FPB22int64__to__string__dec(
  uint16_t* _M0L6bufferS271,
  uint64_t _M0L3numS283,
  int32_t _M0L12digit__startS272,
  int32_t _M0L10total__lenS284
) {
  int32_t _M0L6_2atmpS1297;
  uint64_t _M0L3numS261;
  int32_t _M0L6offsetS262;
  #line 493 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L6_2atmpS1297 = _M0L10total__lenS284 - _M0L12digit__startS272;
  _M0L3numS261 = _M0L3numS283;
  _M0L6offsetS262 = _M0L6_2atmpS1297;
  while (1) {
    if (_M0L3numS261 >= 10000ull) {
      uint64_t _M0L1tS263 = _M0L3numS261 / 10000ull;
      uint64_t _M0L6_2atmpS1274 = _M0L3numS261 % 10000ull;
      int32_t _M0L1rS264 = (int32_t)_M0L6_2atmpS1274;
      int32_t _M0L2d1S265 = _M0L1rS264 / 100;
      int32_t _M0L2d2S266 = _M0L1rS264 % 100;
      int32_t _M0L6_2atmpS1273 = _M0L2d1S265 / 10;
      int32_t _M0L6_2atmpS1272 = 48 + _M0L6_2atmpS1273;
      int32_t _M0L6d1__hiS267 = (uint16_t)_M0L6_2atmpS1272;
      int32_t _M0L6_2atmpS1271 = _M0L2d1S265 % 10;
      int32_t _M0L6_2atmpS1270 = 48 + _M0L6_2atmpS1271;
      int32_t _M0L6d1__loS268 = (uint16_t)_M0L6_2atmpS1270;
      int32_t _M0L6_2atmpS1269 = _M0L2d2S266 / 10;
      int32_t _M0L6_2atmpS1268 = 48 + _M0L6_2atmpS1269;
      int32_t _M0L6d2__hiS269 = (uint16_t)_M0L6_2atmpS1268;
      int32_t _M0L6_2atmpS1267 = _M0L2d2S266 % 10;
      int32_t _M0L6_2atmpS1266 = 48 + _M0L6_2atmpS1267;
      int32_t _M0L6d2__loS270 = (uint16_t)_M0L6_2atmpS1266;
      int32_t _M0L6_2atmpS1258 = _M0L12digit__startS272 + _M0L6offsetS262;
      int32_t _M0L6_2atmpS1257 = _M0L6_2atmpS1258 - 4;
      int32_t _M0L6_2atmpS1260;
      int32_t _M0L6_2atmpS1259;
      int32_t _M0L6_2atmpS1262;
      int32_t _M0L6_2atmpS1261;
      int32_t _M0L6_2atmpS1264;
      int32_t _M0L6_2atmpS1263;
      int32_t _M0L6_2atmpS1265;
      _M0L6bufferS271[_M0L6_2atmpS1257] = _M0L6d1__hiS267;
      _M0L6_2atmpS1260 = _M0L12digit__startS272 + _M0L6offsetS262;
      _M0L6_2atmpS1259 = _M0L6_2atmpS1260 - 3;
      _M0L6bufferS271[_M0L6_2atmpS1259] = _M0L6d1__loS268;
      _M0L6_2atmpS1262 = _M0L12digit__startS272 + _M0L6offsetS262;
      _M0L6_2atmpS1261 = _M0L6_2atmpS1262 - 2;
      _M0L6bufferS271[_M0L6_2atmpS1261] = _M0L6d2__hiS269;
      _M0L6_2atmpS1264 = _M0L12digit__startS272 + _M0L6offsetS262;
      _M0L6_2atmpS1263 = _M0L6_2atmpS1264 - 1;
      _M0L6bufferS271[_M0L6_2atmpS1263] = _M0L6d2__loS270;
      _M0L6_2atmpS1265 = _M0L6offsetS262 - 4;
      _M0L3numS261 = _M0L1tS263;
      _M0L6offsetS262 = _M0L6_2atmpS1265;
      continue;
    } else {
      int32_t _M0L6_2atmpS1296 = (int32_t)_M0L3numS261;
      int32_t _M0L9remainingS274 = _M0L6_2atmpS1296;
      int32_t _M0L6offsetS275 = _M0L6offsetS262;
      while (1) {
        if (_M0L9remainingS274 >= 100) {
          int32_t _M0L1tS276 = _M0L9remainingS274 / 100;
          int32_t _M0L1dS277 = _M0L9remainingS274 % 100;
          int32_t _M0L6_2atmpS1283 = _M0L1dS277 / 10;
          int32_t _M0L6_2atmpS1282 = 48 + _M0L6_2atmpS1283;
          int32_t _M0L5d__hiS278 = (uint16_t)_M0L6_2atmpS1282;
          int32_t _M0L6_2atmpS1281 = _M0L1dS277 % 10;
          int32_t _M0L6_2atmpS1280 = 48 + _M0L6_2atmpS1281;
          int32_t _M0L5d__loS279 = (uint16_t)_M0L6_2atmpS1280;
          int32_t _M0L6_2atmpS1276 = _M0L12digit__startS272 + _M0L6offsetS275;
          int32_t _M0L6_2atmpS1275 = _M0L6_2atmpS1276 - 2;
          int32_t _M0L6_2atmpS1278;
          int32_t _M0L6_2atmpS1277;
          int32_t _M0L6_2atmpS1279;
          _M0L6bufferS271[_M0L6_2atmpS1275] = _M0L5d__hiS278;
          _M0L6_2atmpS1278 = _M0L12digit__startS272 + _M0L6offsetS275;
          _M0L6_2atmpS1277 = _M0L6_2atmpS1278 - 1;
          _M0L6bufferS271[_M0L6_2atmpS1277] = _M0L5d__loS279;
          _M0L6_2atmpS1279 = _M0L6offsetS275 - 2;
          _M0L9remainingS274 = _M0L1tS276;
          _M0L6offsetS275 = _M0L6_2atmpS1279;
          continue;
        } else if (_M0L9remainingS274 >= 10) {
          int32_t _M0L6_2atmpS1291 = _M0L9remainingS274 / 10;
          int32_t _M0L6_2atmpS1290 = 48 + _M0L6_2atmpS1291;
          int32_t _M0L5d__hiS281 = (uint16_t)_M0L6_2atmpS1290;
          int32_t _M0L6_2atmpS1289 = _M0L9remainingS274 % 10;
          int32_t _M0L6_2atmpS1288 = 48 + _M0L6_2atmpS1289;
          int32_t _M0L5d__loS282 = (uint16_t)_M0L6_2atmpS1288;
          int32_t _M0L6_2atmpS1285 = _M0L12digit__startS272 + _M0L6offsetS275;
          int32_t _M0L6_2atmpS1284 = _M0L6_2atmpS1285 - 2;
          int32_t _M0L6_2atmpS1287;
          int32_t _M0L6_2atmpS1286;
          _M0L6bufferS271[_M0L6_2atmpS1284] = _M0L5d__hiS281;
          _M0L6_2atmpS1287 = _M0L12digit__startS272 + _M0L6offsetS275;
          _M0L6_2atmpS1286 = _M0L6_2atmpS1287 - 1;
          _M0L6bufferS271[_M0L6_2atmpS1286] = _M0L5d__loS282;
        } else {
          int32_t _M0L6_2atmpS1295 = _M0L12digit__startS272 + _M0L6offsetS275;
          int32_t _M0L6_2atmpS1292 = _M0L6_2atmpS1295 - 1;
          int32_t _M0L6_2atmpS1294 = 48 + _M0L9remainingS274;
          int32_t _M0L6_2atmpS1293 = (uint16_t)_M0L6_2atmpS1294;
          _M0L6bufferS271[_M0L6_2atmpS1292] = _M0L6_2atmpS1293;
        }
        break;
      }
    }
    break;
  }
  return 0;
}

int32_t _M0FPB26int64__to__string__generic(
  uint16_t* _M0L6bufferS251,
  uint64_t _M0L3numS255,
  int32_t _M0L12digit__startS252,
  int32_t _M0L10total__lenS254,
  int32_t _M0L5radixS245
) {
  uint64_t _M0L4baseS244;
  int32_t _M0L6_2atmpS1242;
  int32_t _M0L6_2atmpS1241;
  #line 462 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  #line 470 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L4baseS244 = _M0MPC13int3Int10to__uint64(_M0L5radixS245);
  _M0L6_2atmpS1242 = _M0L5radixS245 - 1;
  _M0L6_2atmpS1241 = _M0L5radixS245 & _M0L6_2atmpS1242;
  if (_M0L6_2atmpS1241 == 0) {
    int32_t _M0L5shiftS246;
    uint64_t _M0L4maskS247;
    int32_t _M0L6_2atmpS1249;
    int32_t _M0L6offsetS248;
    uint64_t _M0L1nS249;
    #line 473 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0L5shiftS246 = moonbit_ctz32(_M0L5radixS245);
    _M0L4maskS247 = _M0L4baseS244 - 1ull;
    _M0L6_2atmpS1249 = _M0L10total__lenS254 - _M0L12digit__startS252;
    _M0L6offsetS248 = _M0L6_2atmpS1249;
    _M0L1nS249 = _M0L3numS255;
    while (1) {
      if (_M0L1nS249 > 0ull) {
        uint64_t _M0L6_2atmpS1248 = _M0L1nS249 & _M0L4maskS247;
        int32_t _M0L5digitS250 = (int32_t)_M0L6_2atmpS1248;
        int32_t _M0L6_2atmpS1245 = _M0L12digit__startS252 + _M0L6offsetS248;
        int32_t _M0L6_2atmpS1243 = _M0L6_2atmpS1245 - 1;
        int32_t _M0L6_2atmpS1244 =
          ((moonbit_string_t)moonbit_string_literal_12.data)[_M0L5digitS250];
        int32_t _M0L6_2atmpS1246;
        uint64_t _M0L6_2atmpS1247;
        _M0L6bufferS251[_M0L6_2atmpS1243] = _M0L6_2atmpS1244;
        _M0L6_2atmpS1246 = _M0L6offsetS248 - 1;
        _M0L6_2atmpS1247 = _M0L1nS249 >> (_M0L5shiftS246 & 63);
        _M0L6offsetS248 = _M0L6_2atmpS1246;
        _M0L1nS249 = _M0L6_2atmpS1247;
        continue;
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1256 = _M0L10total__lenS254 - _M0L12digit__startS252;
    int32_t _M0L6offsetS256 = _M0L6_2atmpS1256;
    uint64_t _M0L1nS257 = _M0L3numS255;
    while (1) {
      if (_M0L1nS257 > 0ull) {
        uint64_t _M0L1qS258 = _M0L1nS257 / _M0L4baseS244;
        uint64_t _M0L6_2atmpS1255 = _M0L1qS258 * _M0L4baseS244;
        uint64_t _M0L6_2atmpS1254 = _M0L1nS257 - _M0L6_2atmpS1255;
        int32_t _M0L5digitS259 = (int32_t)_M0L6_2atmpS1254;
        int32_t _M0L6_2atmpS1252 = _M0L12digit__startS252 + _M0L6offsetS256;
        int32_t _M0L6_2atmpS1250 = _M0L6_2atmpS1252 - 1;
        int32_t _M0L6_2atmpS1251 =
          ((moonbit_string_t)moonbit_string_literal_12.data)[_M0L5digitS259];
        int32_t _M0L6_2atmpS1253;
        _M0L6bufferS251[_M0L6_2atmpS1250] = _M0L6_2atmpS1251;
        _M0L6_2atmpS1253 = _M0L6offsetS256 - 1;
        _M0L6offsetS256 = _M0L6_2atmpS1253;
        _M0L1nS257 = _M0L1qS258;
        continue;
      }
      break;
    }
  }
  return 0;
}

int32_t _M0FPB22int64__to__string__hex(
  uint16_t* _M0L6bufferS238,
  uint64_t _M0L3numS243,
  int32_t _M0L12digit__startS239,
  int32_t _M0L10total__lenS242
) {
  int32_t _M0L6_2atmpS1240;
  int32_t _M0L6offsetS233;
  uint64_t _M0L1nS234;
  #line 434 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L6_2atmpS1240 = _M0L10total__lenS242 - _M0L12digit__startS239;
  _M0L6offsetS233 = _M0L6_2atmpS1240;
  _M0L1nS234 = _M0L3numS243;
  while (1) {
    if (_M0L6offsetS233 >= 2) {
      uint64_t _M0L6_2atmpS1237 = _M0L1nS234 & 255ull;
      int32_t _M0L9byte__valS235 = (int32_t)_M0L6_2atmpS1237;
      int32_t _M0L2hiS236 = _M0L9byte__valS235 / 16;
      int32_t _M0L2loS237 = _M0L9byte__valS235 % 16;
      int32_t _M0L6_2atmpS1231 = _M0L12digit__startS239 + _M0L6offsetS233;
      int32_t _M0L6_2atmpS1229 = _M0L6_2atmpS1231 - 2;
      int32_t _M0L6_2atmpS1230 =
        ((moonbit_string_t)moonbit_string_literal_12.data)[_M0L2hiS236];
      int32_t _M0L6_2atmpS1234;
      int32_t _M0L6_2atmpS1232;
      int32_t _M0L6_2atmpS1233;
      int32_t _M0L6_2atmpS1235;
      uint64_t _M0L6_2atmpS1236;
      _M0L6bufferS238[_M0L6_2atmpS1229] = _M0L6_2atmpS1230;
      _M0L6_2atmpS1234 = _M0L12digit__startS239 + _M0L6offsetS233;
      _M0L6_2atmpS1232 = _M0L6_2atmpS1234 - 1;
      _M0L6_2atmpS1233
      = ((moonbit_string_t)moonbit_string_literal_12.data)[
        _M0L2loS237
      ];
      _M0L6bufferS238[_M0L6_2atmpS1232] = _M0L6_2atmpS1233;
      _M0L6_2atmpS1235 = _M0L6offsetS233 - 2;
      _M0L6_2atmpS1236 = _M0L1nS234 >> 8;
      _M0L6offsetS233 = _M0L6_2atmpS1235;
      _M0L1nS234 = _M0L6_2atmpS1236;
      continue;
    } else if (_M0L6offsetS233 == 1) {
      uint64_t _M0L6_2atmpS1239 = _M0L1nS234 & 15ull;
      int32_t _M0L6nibbleS241 = (int32_t)_M0L6_2atmpS1239;
      int32_t _M0L6_2atmpS1238 =
        ((moonbit_string_t)moonbit_string_literal_12.data)[_M0L6nibbleS241];
      _M0L6bufferS238[_M0L12digit__startS239] = _M0L6_2atmpS1238;
    }
    break;
  }
  return 0;
}

int32_t _M0FPB14radix__count64(
  uint64_t _M0L5valueS227,
  int32_t _M0L5radixS229
) {
  uint64_t _M0L4baseS228;
  uint64_t _M0L3numS230;
  int32_t _M0L5countS231;
  #line 419 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  if (_M0L5valueS227 == 0ull) {
    return 1;
  }
  #line 424 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L4baseS228 = _M0MPC13int3Int10to__uint64(_M0L5radixS229);
  _M0L3numS230 = _M0L5valueS227;
  _M0L5countS231 = 0;
  while (1) {
    if (_M0L3numS230 > 0ull) {
      uint64_t _M0L6_2atmpS1227 = _M0L3numS230 / _M0L4baseS228;
      int32_t _M0L6_2atmpS1228 = _M0L5countS231 + 1;
      _M0L3numS230 = _M0L6_2atmpS1227;
      _M0L5countS231 = _M0L6_2atmpS1228;
      continue;
    } else {
      return _M0L5countS231;
    }
    break;
  }
}

int32_t _M0FPB12hex__count64(uint64_t _M0L5valueS225) {
  #line 407 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  if (_M0L5valueS225 == 0ull) {
    return 1;
  } else {
    int32_t _M0L14leading__zerosS226;
    int32_t _M0L6_2atmpS1226;
    int32_t _M0L6_2atmpS1225;
    #line 412 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0L14leading__zerosS226 = moonbit_clz64(_M0L5valueS225);
    _M0L6_2atmpS1226 = 63 - _M0L14leading__zerosS226;
    _M0L6_2atmpS1225 = _M0L6_2atmpS1226 / 4;
    return _M0L6_2atmpS1225 + 1;
  }
}

int32_t _M0FPB12dec__count64(uint64_t _M0L5valueS224) {
  #line 343 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  if (_M0L5valueS224 >= 10000000000ull) {
    if (_M0L5valueS224 >= 100000000000000ull) {
      if (_M0L5valueS224 >= 10000000000000000ull) {
        if (_M0L5valueS224 >= 1000000000000000000ull) {
          if (_M0L5valueS224 >= 10000000000000000000ull) {
            return 20;
          } else {
            return 19;
          }
        } else if (_M0L5valueS224 >= 100000000000000000ull) {
          return 18;
        } else {
          return 17;
        }
      } else if (_M0L5valueS224 >= 1000000000000000ull) {
        return 16;
      } else {
        return 15;
      }
    } else if (_M0L5valueS224 >= 1000000000000ull) {
      if (_M0L5valueS224 >= 10000000000000ull) {
        return 14;
      } else {
        return 13;
      }
    } else if (_M0L5valueS224 >= 100000000000ull) {
      return 12;
    } else {
      return 11;
    }
  } else if (_M0L5valueS224 >= 100000ull) {
    if (_M0L5valueS224 >= 10000000ull) {
      if (_M0L5valueS224 >= 1000000000ull) {
        return 10;
      } else if (_M0L5valueS224 >= 100000000ull) {
        return 9;
      } else {
        return 8;
      }
    } else if (_M0L5valueS224 >= 1000000ull) {
      return 7;
    } else {
      return 6;
    }
  } else if (_M0L5valueS224 >= 1000ull) {
    if (_M0L5valueS224 >= 10000ull) {
      return 5;
    } else {
      return 4;
    }
  } else if (_M0L5valueS224 >= 100ull) {
    return 3;
  } else if (_M0L5valueS224 >= 10ull) {
    return 2;
  } else {
    return 1;
  }
}

moonbit_string_t _M0MPC13int3Int18to__string_2einner(
  int32_t _M0L4selfS208,
  int32_t _M0L5radixS207
) {
  int32_t _M0L12is__negativeS209;
  uint32_t _M0L3numS210;
  uint16_t* _M0L6bufferS211;
  #line 209 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  if (_M0L5radixS207 < 2 || _M0L5radixS207 > 36) {
    #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_11.data);
  }
  if (_M0L4selfS208 == 0) {
    return (moonbit_string_t)moonbit_string_literal_4.data;
  }
  _M0L12is__negativeS209 = _M0L4selfS208 < 0;
  if (_M0L12is__negativeS209) {
    int32_t _M0L6_2atmpS1224 = -_M0L4selfS208;
    _M0L3numS210 = *(uint32_t*)&_M0L6_2atmpS1224;
  } else {
    _M0L3numS210 = *(uint32_t*)&_M0L4selfS208;
  }
  switch (_M0L5radixS207) {
    case 10: {
      int32_t _M0L10digit__lenS212;
      int32_t _M0L6_2atmpS1221;
      int32_t _M0L10total__lenS213;
      uint16_t* _M0L6bufferS214;
      int32_t _M0L12digit__startS215;
      #line 235 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS212 = _M0FPB12dec__count32(_M0L3numS210);
      if (_M0L12is__negativeS209) {
        _M0L6_2atmpS1221 = 1;
      } else {
        _M0L6_2atmpS1221 = 0;
      }
      _M0L10total__lenS213 = _M0L10digit__lenS212 + _M0L6_2atmpS1221;
      _M0L6bufferS214
      = (uint16_t*)moonbit_make_string(_M0L10total__lenS213, 0);
      if (_M0L12is__negativeS209) {
        _M0L12digit__startS215 = 1;
      } else {
        _M0L12digit__startS215 = 0;
      }
      #line 239 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0FPB20int__to__string__dec(_M0L6bufferS214, _M0L3numS210, _M0L12digit__startS215, _M0L10total__lenS213);
      _M0L6bufferS211 = _M0L6bufferS214;
      break;
    }
    
    case 16: {
      int32_t _M0L10digit__lenS216;
      int32_t _M0L6_2atmpS1222;
      int32_t _M0L10total__lenS217;
      uint16_t* _M0L6bufferS218;
      int32_t _M0L12digit__startS219;
      #line 243 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS216 = _M0FPB12hex__count32(_M0L3numS210);
      if (_M0L12is__negativeS209) {
        _M0L6_2atmpS1222 = 1;
      } else {
        _M0L6_2atmpS1222 = 0;
      }
      _M0L10total__lenS217 = _M0L10digit__lenS216 + _M0L6_2atmpS1222;
      _M0L6bufferS218
      = (uint16_t*)moonbit_make_string(_M0L10total__lenS217, 0);
      if (_M0L12is__negativeS209) {
        _M0L12digit__startS219 = 1;
      } else {
        _M0L12digit__startS219 = 0;
      }
      #line 247 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0FPB20int__to__string__hex(_M0L6bufferS218, _M0L3numS210, _M0L12digit__startS219, _M0L10total__lenS217);
      _M0L6bufferS211 = _M0L6bufferS218;
      break;
    }
    default: {
      int32_t _M0L10digit__lenS220;
      int32_t _M0L6_2atmpS1223;
      int32_t _M0L10total__lenS221;
      uint16_t* _M0L6bufferS222;
      int32_t _M0L12digit__startS223;
      #line 251 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS220
      = _M0FPB14radix__count32(_M0L3numS210, _M0L5radixS207);
      if (_M0L12is__negativeS209) {
        _M0L6_2atmpS1223 = 1;
      } else {
        _M0L6_2atmpS1223 = 0;
      }
      _M0L10total__lenS221 = _M0L10digit__lenS220 + _M0L6_2atmpS1223;
      _M0L6bufferS222
      = (uint16_t*)moonbit_make_string(_M0L10total__lenS221, 0);
      if (_M0L12is__negativeS209) {
        _M0L12digit__startS223 = 1;
      } else {
        _M0L12digit__startS223 = 0;
      }
      #line 255 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0FPB24int__to__string__generic(_M0L6bufferS222, _M0L3numS210, _M0L12digit__startS223, _M0L10total__lenS221, _M0L5radixS207);
      _M0L6bufferS211 = _M0L6bufferS222;
      break;
    }
  }
  if (_M0L12is__negativeS209) {
    _M0L6bufferS211[0] = 45;
  }
  return _M0L6bufferS211;
}

int32_t _M0FPB14radix__count32(
  uint32_t _M0L5valueS201,
  int32_t _M0L5radixS203
) {
  uint32_t _M0L4baseS202;
  uint32_t _M0L3numS204;
  int32_t _M0L5countS205;
  #line 189 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  if (_M0L5valueS201 == 0u) {
    return 1;
  }
  _M0L4baseS202 = *(uint32_t*)&_M0L5radixS203;
  _M0L3numS204 = _M0L5valueS201;
  _M0L5countS205 = 0;
  while (1) {
    if (_M0L3numS204 > 0u) {
      uint32_t _M0L6_2atmpS1219 = _M0L3numS204 / _M0L4baseS202;
      int32_t _M0L6_2atmpS1220 = _M0L5countS205 + 1;
      _M0L3numS204 = _M0L6_2atmpS1219;
      _M0L5countS205 = _M0L6_2atmpS1220;
      continue;
    } else {
      return _M0L5countS205;
    }
    break;
  }
}

int32_t _M0FPB12hex__count32(uint32_t _M0L5valueS199) {
  #line 177 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  if (_M0L5valueS199 == 0u) {
    return 1;
  } else {
    int32_t _M0L14leading__zerosS200;
    int32_t _M0L6_2atmpS1218;
    int32_t _M0L6_2atmpS1217;
    #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0L14leading__zerosS200 = moonbit_clz32(_M0L5valueS199);
    _M0L6_2atmpS1218 = 31 - _M0L14leading__zerosS200;
    _M0L6_2atmpS1217 = _M0L6_2atmpS1218 / 4;
    return _M0L6_2atmpS1217 + 1;
  }
}

int32_t _M0FPB12dec__count32(uint32_t _M0L5valueS198) {
  #line 143 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  if (_M0L5valueS198 >= 100000u) {
    if (_M0L5valueS198 >= 10000000u) {
      if (_M0L5valueS198 >= 1000000000u) {
        return 10;
      } else if (_M0L5valueS198 >= 100000000u) {
        return 9;
      } else {
        return 8;
      }
    } else if (_M0L5valueS198 >= 1000000u) {
      return 7;
    } else {
      return 6;
    }
  } else if (_M0L5valueS198 >= 1000u) {
    if (_M0L5valueS198 >= 10000u) {
      return 5;
    } else {
      return 4;
    }
  } else if (_M0L5valueS198 >= 100u) {
    return 3;
  } else if (_M0L5valueS198 >= 10u) {
    return 2;
  } else {
    return 1;
  }
}

int32_t _M0FPB20int__to__string__dec(
  uint16_t* _M0L6bufferS184,
  uint32_t _M0L3numS196,
  int32_t _M0L12digit__startS185,
  int32_t _M0L10total__lenS197
) {
  int32_t _M0L6_2atmpS1216;
  uint32_t _M0L3numS174;
  int32_t _M0L6offsetS175;
  #line 88 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L6_2atmpS1216 = _M0L10total__lenS197 - _M0L12digit__startS185;
  _M0L3numS174 = _M0L3numS196;
  _M0L6offsetS175 = _M0L6_2atmpS1216;
  while (1) {
    if (_M0L3numS174 >= 10000u) {
      uint32_t _M0L1tS176 = _M0L3numS174 / 10000u;
      uint32_t _M0L6_2atmpS1193 = _M0L3numS174 % 10000u;
      int32_t _M0L1rS177 = *(int32_t*)&_M0L6_2atmpS1193;
      int32_t _M0L2d1S178 = _M0L1rS177 / 100;
      int32_t _M0L2d2S179 = _M0L1rS177 % 100;
      int32_t _M0L6_2atmpS1192 = _M0L2d1S178 / 10;
      int32_t _M0L6_2atmpS1191 = 48 + _M0L6_2atmpS1192;
      int32_t _M0L6d1__hiS180 = (uint16_t)_M0L6_2atmpS1191;
      int32_t _M0L6_2atmpS1190 = _M0L2d1S178 % 10;
      int32_t _M0L6_2atmpS1189 = 48 + _M0L6_2atmpS1190;
      int32_t _M0L6d1__loS181 = (uint16_t)_M0L6_2atmpS1189;
      int32_t _M0L6_2atmpS1188 = _M0L2d2S179 / 10;
      int32_t _M0L6_2atmpS1187 = 48 + _M0L6_2atmpS1188;
      int32_t _M0L6d2__hiS182 = (uint16_t)_M0L6_2atmpS1187;
      int32_t _M0L6_2atmpS1186 = _M0L2d2S179 % 10;
      int32_t _M0L6_2atmpS1185 = 48 + _M0L6_2atmpS1186;
      int32_t _M0L6d2__loS183 = (uint16_t)_M0L6_2atmpS1185;
      int32_t _M0L6_2atmpS1177 = _M0L12digit__startS185 + _M0L6offsetS175;
      int32_t _M0L6_2atmpS1176 = _M0L6_2atmpS1177 - 4;
      int32_t _M0L6_2atmpS1179;
      int32_t _M0L6_2atmpS1178;
      int32_t _M0L6_2atmpS1181;
      int32_t _M0L6_2atmpS1180;
      int32_t _M0L6_2atmpS1183;
      int32_t _M0L6_2atmpS1182;
      int32_t _M0L6_2atmpS1184;
      _M0L6bufferS184[_M0L6_2atmpS1176] = _M0L6d1__hiS180;
      _M0L6_2atmpS1179 = _M0L12digit__startS185 + _M0L6offsetS175;
      _M0L6_2atmpS1178 = _M0L6_2atmpS1179 - 3;
      _M0L6bufferS184[_M0L6_2atmpS1178] = _M0L6d1__loS181;
      _M0L6_2atmpS1181 = _M0L12digit__startS185 + _M0L6offsetS175;
      _M0L6_2atmpS1180 = _M0L6_2atmpS1181 - 2;
      _M0L6bufferS184[_M0L6_2atmpS1180] = _M0L6d2__hiS182;
      _M0L6_2atmpS1183 = _M0L12digit__startS185 + _M0L6offsetS175;
      _M0L6_2atmpS1182 = _M0L6_2atmpS1183 - 1;
      _M0L6bufferS184[_M0L6_2atmpS1182] = _M0L6d2__loS183;
      _M0L6_2atmpS1184 = _M0L6offsetS175 - 4;
      _M0L3numS174 = _M0L1tS176;
      _M0L6offsetS175 = _M0L6_2atmpS1184;
      continue;
    } else {
      int32_t _M0L6_2atmpS1215 = *(int32_t*)&_M0L3numS174;
      int32_t _M0L9remainingS187 = _M0L6_2atmpS1215;
      int32_t _M0L6offsetS188 = _M0L6offsetS175;
      while (1) {
        if (_M0L9remainingS187 >= 100) {
          int32_t _M0L1tS189 = _M0L9remainingS187 / 100;
          int32_t _M0L1dS190 = _M0L9remainingS187 % 100;
          int32_t _M0L6_2atmpS1202 = _M0L1dS190 / 10;
          int32_t _M0L6_2atmpS1201 = 48 + _M0L6_2atmpS1202;
          int32_t _M0L5d__hiS191 = (uint16_t)_M0L6_2atmpS1201;
          int32_t _M0L6_2atmpS1200 = _M0L1dS190 % 10;
          int32_t _M0L6_2atmpS1199 = 48 + _M0L6_2atmpS1200;
          int32_t _M0L5d__loS192 = (uint16_t)_M0L6_2atmpS1199;
          int32_t _M0L6_2atmpS1195 = _M0L12digit__startS185 + _M0L6offsetS188;
          int32_t _M0L6_2atmpS1194 = _M0L6_2atmpS1195 - 2;
          int32_t _M0L6_2atmpS1197;
          int32_t _M0L6_2atmpS1196;
          int32_t _M0L6_2atmpS1198;
          _M0L6bufferS184[_M0L6_2atmpS1194] = _M0L5d__hiS191;
          _M0L6_2atmpS1197 = _M0L12digit__startS185 + _M0L6offsetS188;
          _M0L6_2atmpS1196 = _M0L6_2atmpS1197 - 1;
          _M0L6bufferS184[_M0L6_2atmpS1196] = _M0L5d__loS192;
          _M0L6_2atmpS1198 = _M0L6offsetS188 - 2;
          _M0L9remainingS187 = _M0L1tS189;
          _M0L6offsetS188 = _M0L6_2atmpS1198;
          continue;
        } else if (_M0L9remainingS187 >= 10) {
          int32_t _M0L6_2atmpS1210 = _M0L9remainingS187 / 10;
          int32_t _M0L6_2atmpS1209 = 48 + _M0L6_2atmpS1210;
          int32_t _M0L5d__hiS194 = (uint16_t)_M0L6_2atmpS1209;
          int32_t _M0L6_2atmpS1208 = _M0L9remainingS187 % 10;
          int32_t _M0L6_2atmpS1207 = 48 + _M0L6_2atmpS1208;
          int32_t _M0L5d__loS195 = (uint16_t)_M0L6_2atmpS1207;
          int32_t _M0L6_2atmpS1204 = _M0L12digit__startS185 + _M0L6offsetS188;
          int32_t _M0L6_2atmpS1203 = _M0L6_2atmpS1204 - 2;
          int32_t _M0L6_2atmpS1206;
          int32_t _M0L6_2atmpS1205;
          _M0L6bufferS184[_M0L6_2atmpS1203] = _M0L5d__hiS194;
          _M0L6_2atmpS1206 = _M0L12digit__startS185 + _M0L6offsetS188;
          _M0L6_2atmpS1205 = _M0L6_2atmpS1206 - 1;
          _M0L6bufferS184[_M0L6_2atmpS1205] = _M0L5d__loS195;
        } else {
          int32_t _M0L6_2atmpS1214 = _M0L12digit__startS185 + _M0L6offsetS188;
          int32_t _M0L6_2atmpS1211 = _M0L6_2atmpS1214 - 1;
          int32_t _M0L6_2atmpS1213 = 48 + _M0L9remainingS187;
          int32_t _M0L6_2atmpS1212 = (uint16_t)_M0L6_2atmpS1213;
          _M0L6bufferS184[_M0L6_2atmpS1211] = _M0L6_2atmpS1212;
        }
        break;
      }
    }
    break;
  }
  return 0;
}

int32_t _M0FPB24int__to__string__generic(
  uint16_t* _M0L6bufferS164,
  uint32_t _M0L3numS168,
  int32_t _M0L12digit__startS165,
  int32_t _M0L10total__lenS167,
  int32_t _M0L5radixS158
) {
  uint32_t _M0L4baseS157;
  int32_t _M0L6_2atmpS1161;
  int32_t _M0L6_2atmpS1160;
  #line 57 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L4baseS157 = *(uint32_t*)&_M0L5radixS158;
  _M0L6_2atmpS1161 = _M0L5radixS158 - 1;
  _M0L6_2atmpS1160 = _M0L5radixS158 & _M0L6_2atmpS1161;
  if (_M0L6_2atmpS1160 == 0) {
    int32_t _M0L5shiftS159;
    uint32_t _M0L4maskS160;
    int32_t _M0L6_2atmpS1168;
    int32_t _M0L6offsetS161;
    uint32_t _M0L1nS162;
    #line 68 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0L5shiftS159 = moonbit_ctz32(_M0L5radixS158);
    _M0L4maskS160 = _M0L4baseS157 - 1u;
    _M0L6_2atmpS1168 = _M0L10total__lenS167 - _M0L12digit__startS165;
    _M0L6offsetS161 = _M0L6_2atmpS1168;
    _M0L1nS162 = _M0L3numS168;
    while (1) {
      if (_M0L1nS162 > 0u) {
        uint32_t _M0L6_2atmpS1167 = _M0L1nS162 & _M0L4maskS160;
        int32_t _M0L5digitS163 = *(int32_t*)&_M0L6_2atmpS1167;
        int32_t _M0L6_2atmpS1164 = _M0L12digit__startS165 + _M0L6offsetS161;
        int32_t _M0L6_2atmpS1162 = _M0L6_2atmpS1164 - 1;
        int32_t _M0L6_2atmpS1163 =
          ((moonbit_string_t)moonbit_string_literal_12.data)[_M0L5digitS163];
        int32_t _M0L6_2atmpS1165;
        uint32_t _M0L6_2atmpS1166;
        _M0L6bufferS164[_M0L6_2atmpS1162] = _M0L6_2atmpS1163;
        _M0L6_2atmpS1165 = _M0L6offsetS161 - 1;
        _M0L6_2atmpS1166 = _M0L1nS162 >> (_M0L5shiftS159 & 31);
        _M0L6offsetS161 = _M0L6_2atmpS1165;
        _M0L1nS162 = _M0L6_2atmpS1166;
        continue;
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1175 = _M0L10total__lenS167 - _M0L12digit__startS165;
    int32_t _M0L6offsetS169 = _M0L6_2atmpS1175;
    uint32_t _M0L1nS170 = _M0L3numS168;
    while (1) {
      if (_M0L1nS170 > 0u) {
        uint32_t _M0L1qS171 = _M0L1nS170 / _M0L4baseS157;
        uint32_t _M0L6_2atmpS1174 = _M0L1qS171 * _M0L4baseS157;
        uint32_t _M0L6_2atmpS1173 = _M0L1nS170 - _M0L6_2atmpS1174;
        int32_t _M0L5digitS172 = *(int32_t*)&_M0L6_2atmpS1173;
        int32_t _M0L6_2atmpS1171 = _M0L12digit__startS165 + _M0L6offsetS169;
        int32_t _M0L6_2atmpS1169 = _M0L6_2atmpS1171 - 1;
        int32_t _M0L6_2atmpS1170 =
          ((moonbit_string_t)moonbit_string_literal_12.data)[_M0L5digitS172];
        int32_t _M0L6_2atmpS1172;
        _M0L6bufferS164[_M0L6_2atmpS1169] = _M0L6_2atmpS1170;
        _M0L6_2atmpS1172 = _M0L6offsetS169 - 1;
        _M0L6offsetS169 = _M0L6_2atmpS1172;
        _M0L1nS170 = _M0L1qS171;
        continue;
      }
      break;
    }
  }
  return 0;
}

int32_t _M0FPB20int__to__string__hex(
  uint16_t* _M0L6bufferS151,
  uint32_t _M0L3numS156,
  int32_t _M0L12digit__startS152,
  int32_t _M0L10total__lenS155
) {
  int32_t _M0L6_2atmpS1159;
  int32_t _M0L6offsetS146;
  uint32_t _M0L1nS147;
  #line 29 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L6_2atmpS1159 = _M0L10total__lenS155 - _M0L12digit__startS152;
  _M0L6offsetS146 = _M0L6_2atmpS1159;
  _M0L1nS147 = _M0L3numS156;
  while (1) {
    if (_M0L6offsetS146 >= 2) {
      uint32_t _M0L6_2atmpS1156 = _M0L1nS147 & 255u;
      int32_t _M0L9byte__valS148 = *(int32_t*)&_M0L6_2atmpS1156;
      int32_t _M0L2hiS149 = _M0L9byte__valS148 / 16;
      int32_t _M0L2loS150 = _M0L9byte__valS148 % 16;
      int32_t _M0L6_2atmpS1150 = _M0L12digit__startS152 + _M0L6offsetS146;
      int32_t _M0L6_2atmpS1148 = _M0L6_2atmpS1150 - 2;
      int32_t _M0L6_2atmpS1149 =
        ((moonbit_string_t)moonbit_string_literal_12.data)[_M0L2hiS149];
      int32_t _M0L6_2atmpS1153;
      int32_t _M0L6_2atmpS1151;
      int32_t _M0L6_2atmpS1152;
      int32_t _M0L6_2atmpS1154;
      uint32_t _M0L6_2atmpS1155;
      _M0L6bufferS151[_M0L6_2atmpS1148] = _M0L6_2atmpS1149;
      _M0L6_2atmpS1153 = _M0L12digit__startS152 + _M0L6offsetS146;
      _M0L6_2atmpS1151 = _M0L6_2atmpS1153 - 1;
      _M0L6_2atmpS1152
      = ((moonbit_string_t)moonbit_string_literal_12.data)[
        _M0L2loS150
      ];
      _M0L6bufferS151[_M0L6_2atmpS1151] = _M0L6_2atmpS1152;
      _M0L6_2atmpS1154 = _M0L6offsetS146 - 2;
      _M0L6_2atmpS1155 = _M0L1nS147 >> 8;
      _M0L6offsetS146 = _M0L6_2atmpS1154;
      _M0L1nS147 = _M0L6_2atmpS1155;
      continue;
    } else if (_M0L6offsetS146 == 1) {
      uint32_t _M0L6_2atmpS1158 = _M0L1nS147 & 15u;
      int32_t _M0L6nibbleS154 = *(int32_t*)&_M0L6_2atmpS1158;
      int32_t _M0L6_2atmpS1157 =
        ((moonbit_string_t)moonbit_string_literal_12.data)[_M0L6nibbleS154];
      _M0L6bufferS151[_M0L12digit__startS152] = _M0L6_2atmpS1157;
    }
    break;
  }
  return 0;
}

int32_t _M0IP016_24default__implPB4Show6outputGiE(
  int32_t _M0L4selfS143,
  struct _M0TPB6Logger _M0L6loggerS142
) {
  moonbit_string_t _M0L6_2atmpS1146;
  #line 159 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  #line 160 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS1146 = _M0IPC13int3IntPB4Show10to__string(_M0L4selfS143);
  #line 160 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6loggerS142.$0->$method_0(_M0L6loggerS142.$1, _M0L6_2atmpS1146);
  moonbit_decref(_M0L6_2atmpS1146);
  return 0;
}

int32_t _M0IP016_24default__implPB4Show6outputGmE(
  uint64_t _M0L4selfS145,
  struct _M0TPB6Logger _M0L6loggerS144
) {
  moonbit_string_t _M0L6_2atmpS1147;
  #line 159 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  #line 160 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS1147 = _M0IPC16uint646UInt64PB4Show10to__string(_M0L4selfS145);
  #line 160 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6loggerS144.$0->$method_0(_M0L6loggerS144.$1, _M0L6_2atmpS1147);
  moonbit_decref(_M0L6_2atmpS1147);
  return 0;
}

int32_t _M0MPC16string10StringView13start__offset(
  struct _M0TPC16string10StringView _M0L4selfS141
) {
  #line 99 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
  return _M0L4selfS141.$1;
}

moonbit_string_t _M0MPC16string10StringView4data(
  struct _M0TPC16string10StringView _M0L4selfS140
) {
  moonbit_string_t _M0L8_2afieldS2115;
  #line 92 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
  _M0L8_2afieldS2115 = _M0L4selfS140.$0;
  moonbit_incref(_M0L8_2afieldS2115);
  return _M0L8_2afieldS2115;
}

int32_t _M0IP016_24default__implPB6Logger16write__substringGRPB13StringBuilderE(
  struct _M0TPB13StringBuilder* _M0L4selfS136,
  moonbit_string_t _M0L5valueS137,
  int32_t _M0L5startS138,
  int32_t _M0L3lenS139
) {
  int32_t _M0L6_2atmpS1145;
  int64_t _M0L6_2atmpS1144;
  struct _M0TPC16string10StringView _M0L6_2atmpS1143;
  #line 122 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS1145 = _M0L5startS138 + _M0L3lenS139;
  _M0L6_2atmpS1144 = (int64_t)_M0L6_2atmpS1145;
  #line 123 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS1143
  = _M0MPC16string6String11sub_2einner(_M0L5valueS137, _M0L5startS138, _M0L6_2atmpS1144);
  #line 123 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0IPB13StringBuilderPB6Logger11write__view(_M0L4selfS136, _M0L6_2atmpS1143);
  moonbit_decref(_M0L6_2atmpS1143.$0);
  return 0;
}

struct _M0TPC16string10StringView _M0MPC16string6String11sub_2einner(
  moonbit_string_t _M0L4selfS128,
  int32_t _M0L5startS135,
  int64_t _M0L3endS132
) {
  int32_t _M0L3lenS127;
  int32_t _M0L3endS131;
  int32_t _M0L3endS129;
  #line 923 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
  _M0L3lenS127 = Moonbit_array_length(_M0L4selfS128);
  if (_M0L3endS132 == 4294967296ll) {
    _M0L3endS131 = _M0L3lenS127;
    goto join_130;
  } else {
    int64_t _M0L7_2aSomeS133 = _M0L3endS132;
    int32_t _M0L6_2aendS134 = (int32_t)_M0L7_2aSomeS133;
    _M0L3endS131 = _M0L6_2aendS134;
    goto join_130;
  }
  goto joinlet_2213;
  join_130:;
  _M0L3endS129 = _M0L3endS131;
  joinlet_2213:;
  if (
    _M0L5startS135 >= 0
    && _M0L5startS135 <= _M0L3endS129
    && _M0L3endS129 <= _M0L3lenS127
  ) {
    if (_M0L5startS135 < _M0L3lenS127) {
      int32_t _M0L6_2atmpS1140 = _M0L4selfS128[_M0L5startS135];
      int32_t _M0L6_2atmpS1139;
      #line 931 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
      _M0L6_2atmpS1139
      = _M0MPC16uint166UInt1623is__trailing__surrogate(_M0L6_2atmpS1140);
      if (!_M0L6_2atmpS1139) {
        
      } else {
        #line 931 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
        moonbit_panic();
      }
    }
    if (_M0L3endS129 < _M0L3lenS127) {
      int32_t _M0L6_2atmpS1142 = _M0L4selfS128[_M0L3endS129];
      int32_t _M0L6_2atmpS1141;
      #line 934 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
      _M0L6_2atmpS1141
      = _M0MPC16uint166UInt1623is__trailing__surrogate(_M0L6_2atmpS1142);
      if (!_M0L6_2atmpS1141) {
        
      } else {
        #line 934 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
        moonbit_panic();
      }
    }
    moonbit_incref(_M0L4selfS128);
    return (struct _M0TPC16string10StringView){.$0 = _M0L4selfS128,
                                                 .$1 = _M0L5startS135,
                                                 .$2 = _M0L3endS129};
  } else {
    #line 929 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
    moonbit_panic();
  }
}

int32_t _M0IP016_24default__implPB6Logger5writeGRPB13StringBuilderE(
  struct _M0TPB13StringBuilder* _M0L4selfS126,
  struct _M0TPB4Show _M0L4showS125
) {
  struct _M0TPB6Logger _M0L6_2atmpS1138;
  #line 116 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  moonbit_incref(_M0L4selfS126);
  _M0L6_2atmpS1138
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS126
  };
  #line 117 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L4showS125.$0->$method_0(_M0L4showS125.$1, _M0L6_2atmpS1138);
  if (_M0L6_2atmpS1138.$1) {
    moonbit_decref(_M0L6_2atmpS1138.$1);
  }
  return 0;
}

int32_t _M0IP016_24default__implPB6Logger28write__string__interpolationGRPB13StringBuilderE(
  struct _M0TPB13StringBuilder* _M0L4selfS124,
  struct _M0TPB4Show _M0L4showS123
) {
  struct _M0TPB6Logger _M0L6_2atmpS1137;
  #line 111 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  moonbit_incref(_M0L4selfS124);
  _M0L6_2atmpS1137
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS124
  };
  #line 112 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L4showS123.$0->$method_0(_M0L4showS123.$1, _M0L6_2atmpS1137);
  if (_M0L6_2atmpS1137.$1) {
    moonbit_decref(_M0L6_2atmpS1137.$1);
  }
  return 0;
}

uint64_t _M0MPC13int3Int10to__uint64(int32_t _M0L4selfS122) {
  int64_t _M0L6_2atmpS1136;
  #line 989 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS1136 = (int64_t)_M0L4selfS122;
  return *(uint64_t*)&_M0L6_2atmpS1136;
}

int32_t _M0IPC16uint166UInt16PB7Default7default() {
  #line 201 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uint16_char.mbt"
  return 0;
}

int32_t _M0MPC16uint166UInt1623is__trailing__surrogate(int32_t _M0L4selfS121) {
  #line 58 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uint16_char.mbt"
  return _M0L4selfS121 >= 56320 && _M0L4selfS121 <= 57343;
}

int32_t _M0IPB13StringBuilderPB6Logger13write__string(
  struct _M0TPB13StringBuilder* _M0L4selfS120,
  moonbit_string_t _M0L3strS118
) {
  int32_t _M0L8str__lenS117;
  int32_t _M0L3lenS1135;
  int32_t _M0L8requiredS119;
  uint16_t* _M0L4dataS1130;
  int32_t _M0L6_2atmpS1129;
  int32_t _if__result_2214;
  uint16_t* _M0L4dataS1131;
  int32_t _M0L3lenS1132;
  int32_t _M0L3lenS1134;
  int32_t _M0L6_2atmpS1133;
  #line 105 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L8str__lenS117 = Moonbit_array_length(_M0L3strS118);
  if (_M0L8str__lenS117 == 0) {
    return 0;
  }
  _M0L3lenS1135 = _M0L4selfS120->$1;
  _M0L8requiredS119 = _M0L3lenS1135 + _M0L8str__lenS117;
  _M0L4dataS1130 = _M0L4selfS120->$0;
  _M0L6_2atmpS1129 = Moonbit_array_length(_M0L4dataS1130);
  if (_M0L8requiredS119 > _M0L6_2atmpS1129) {
    _if__result_2214 = 1;
  } else {
    int32_t _M0L3lenS1128 = _M0L4selfS120->$1;
    _if__result_2214 = _M0L8requiredS119 < _M0L3lenS1128;
  }
  if (_if__result_2214) {
    #line 112 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0MPB13StringBuilder4grow(_M0L4selfS120, _M0L8requiredS119);
  }
  _M0L4dataS1131 = _M0L4selfS120->$0;
  _M0L3lenS1132 = _M0L4selfS120->$1;
  moonbit_incref(_M0L4dataS1131);
  #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0MPC15array10FixedArray26unsafe__blit__from__string(_M0L4dataS1131, _M0L3lenS1132, _M0L3strS118, 0, _M0L8str__lenS117);
  moonbit_decref(_M0L4dataS1131);
  _M0L3lenS1134 = _M0L4selfS120->$1;
  _M0L6_2atmpS1133 = _M0L3lenS1134 + _M0L8str__lenS117;
  _M0L4selfS120->$1 = _M0L6_2atmpS1133;
  return 0;
}

int32_t _M0MPC15array10FixedArray26unsafe__blit__from__string(
  uint16_t* _M0L4selfS113,
  int32_t _M0L11dst__offsetS116,
  moonbit_string_t _M0L3strS114,
  int32_t _M0L11str__offsetS109,
  int32_t _M0L3lenS110
) {
  int32_t _M0L16end__str__offsetS108;
  int32_t _M0L1iS111;
  int32_t _M0L1jS112;
  #line 90 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L16end__str__offsetS108 = _M0L11str__offsetS109 + _M0L3lenS110;
  _M0L1iS111 = _M0L11str__offsetS109;
  _M0L1jS112 = _M0L11dst__offsetS116;
  while (1) {
    if (_M0L1iS111 < _M0L16end__str__offsetS108) {
      int32_t _M0L6_2atmpS1125 = _M0L3strS114[_M0L1iS111];
      int32_t _M0L6_2atmpS1126;
      int32_t _M0L6_2atmpS1127;
      _M0L4selfS113[_M0L1jS112] = _M0L6_2atmpS1125;
      _M0L6_2atmpS1126 = _M0L1iS111 + 1;
      _M0L6_2atmpS1127 = _M0L1jS112 + 1;
      _M0L1iS111 = _M0L6_2atmpS1126;
      _M0L1jS112 = _M0L6_2atmpS1127;
      continue;
    }
    break;
  }
  return 0;
}

int32_t _M0IPB13StringBuilderPB6Logger11write__char(
  struct _M0TPB13StringBuilder* _M0L4selfS106,
  int32_t _M0L2chS105
) {
  uint32_t _M0L4codeS104;
  #line 120 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  #line 121 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L4codeS104 = _M0MPC14char4Char8to__uint(_M0L2chS105);
  if (_M0L4codeS104 <= 65535u) {
    int32_t _M0L3lenS1096 = _M0L4selfS106->$1;
    uint16_t* _M0L4dataS1098 = _M0L4selfS106->$0;
    int32_t _M0L6_2atmpS1097 = Moonbit_array_length(_M0L4dataS1098);
    uint16_t* _M0L4dataS1101;
    int32_t _M0L3lenS1102;
    int32_t _M0L6_2atmpS1103;
    int32_t _M0L3lenS1105;
    int32_t _M0L6_2atmpS1104;
    if (_M0L3lenS1096 >= _M0L6_2atmpS1097) {
      int32_t _M0L3lenS1100 = _M0L4selfS106->$1;
      int32_t _M0L6_2atmpS1099 = _M0L3lenS1100 + 1;
      #line 124 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0MPB13StringBuilder4grow(_M0L4selfS106, _M0L6_2atmpS1099);
    }
    _M0L4dataS1101 = _M0L4selfS106->$0;
    _M0L3lenS1102 = _M0L4selfS106->$1;
    moonbit_incref(_M0L4dataS1101);
    #line 126 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0L6_2atmpS1103 = _M0MPC14uint4UInt10to__uint16(_M0L4codeS104);
    if (
      _M0L3lenS1102 < 0
      || _M0L3lenS1102 >= Moonbit_array_length(_M0L4dataS1101)
    ) {
      #line 126 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      moonbit_panic();
    }
    _M0L4dataS1101[_M0L3lenS1102] = _M0L6_2atmpS1103;
    moonbit_decref(_M0L4dataS1101);
    _M0L3lenS1105 = _M0L4selfS106->$1;
    _M0L6_2atmpS1104 = _M0L3lenS1105 + 1;
    _M0L4selfS106->$1 = _M0L6_2atmpS1104;
  } else if (_M0L4codeS104 <= 1114111u) {
    uint16_t* _M0L4dataS1109 = _M0L4selfS106->$0;
    int32_t _M0L6_2atmpS1107 = Moonbit_array_length(_M0L4dataS1109);
    int32_t _M0L3lenS1108 = _M0L4selfS106->$1;
    int32_t _M0L6_2atmpS1106 = _M0L6_2atmpS1107 - _M0L3lenS1108;
    uint32_t _M0L4codeS107;
    uint16_t* _M0L4dataS1112;
    int32_t _M0L3lenS1113;
    uint32_t _M0L6_2atmpS1116;
    uint32_t _M0L6_2atmpS1115;
    int32_t _M0L6_2atmpS1114;
    uint16_t* _M0L4dataS1117;
    int32_t _M0L3lenS1122;
    int32_t _M0L6_2atmpS1118;
    uint32_t _M0L6_2atmpS1121;
    uint32_t _M0L6_2atmpS1120;
    int32_t _M0L6_2atmpS1119;
    int32_t _M0L3lenS1124;
    int32_t _M0L6_2atmpS1123;
    if (_M0L6_2atmpS1106 < 2) {
      int32_t _M0L3lenS1111 = _M0L4selfS106->$1;
      int32_t _M0L6_2atmpS1110 = _M0L3lenS1111 + 2;
      #line 130 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0MPB13StringBuilder4grow(_M0L4selfS106, _M0L6_2atmpS1110);
    }
    _M0L4codeS107 = _M0L4codeS104 - 65536u;
    _M0L4dataS1112 = _M0L4selfS106->$0;
    _M0L3lenS1113 = _M0L4selfS106->$1;
    _M0L6_2atmpS1116 = _M0L4codeS107 >> 10;
    _M0L6_2atmpS1115 = 55296u + _M0L6_2atmpS1116;
    moonbit_incref(_M0L4dataS1112);
    #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0L6_2atmpS1114 = _M0MPC14uint4UInt10to__uint16(_M0L6_2atmpS1115);
    if (
      _M0L3lenS1113 < 0
      || _M0L3lenS1113 >= Moonbit_array_length(_M0L4dataS1112)
    ) {
      #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      moonbit_panic();
    }
    _M0L4dataS1112[_M0L3lenS1113] = _M0L6_2atmpS1114;
    moonbit_decref(_M0L4dataS1112);
    _M0L4dataS1117 = _M0L4selfS106->$0;
    _M0L3lenS1122 = _M0L4selfS106->$1;
    _M0L6_2atmpS1118 = _M0L3lenS1122 + 1;
    _M0L6_2atmpS1121 = _M0L4codeS107 & 1023u;
    _M0L6_2atmpS1120 = 56320u + _M0L6_2atmpS1121;
    moonbit_incref(_M0L4dataS1117);
    #line 134 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0L6_2atmpS1119 = _M0MPC14uint4UInt10to__uint16(_M0L6_2atmpS1120);
    if (
      _M0L6_2atmpS1118 < 0
      || _M0L6_2atmpS1118 >= Moonbit_array_length(_M0L4dataS1117)
    ) {
      #line 134 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      moonbit_panic();
    }
    _M0L4dataS1117[_M0L6_2atmpS1118] = _M0L6_2atmpS1119;
    moonbit_decref(_M0L4dataS1117);
    _M0L3lenS1124 = _M0L4selfS106->$1;
    _M0L6_2atmpS1123 = _M0L3lenS1124 + 2;
    _M0L4selfS106->$1 = _M0L6_2atmpS1123;
  } else {
    #line 137 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_13.data);
  }
  return 0;
}

int32_t _M0MPB13StringBuilder4grow(
  struct _M0TPB13StringBuilder* _M0L4selfS101,
  int32_t _M0L8requiredS102
) {
  uint16_t* _M0L4dataS1095;
  int32_t _M0L6_2atmpS1093;
  int32_t _M0L3lenS1094;
  int32_t _M0L13new__capacityS100;
  uint16_t* _M0L4dataS1090;
  int32_t _M0L6_2atmpS1091;
  int32_t _M0L3lenS1092;
  uint16_t* _M0L9new__dataS103;
  uint16_t* _M0L6_2aoldS2116;
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L4dataS1095 = _M0L4selfS101->$0;
  _M0L6_2atmpS1093 = Moonbit_array_length(_M0L4dataS1095);
  _M0L3lenS1094 = _M0L4selfS101->$1;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L13new__capacityS100
  = _M0FPB31stringbuilder__growth__capacity(_M0L6_2atmpS1093, _M0L3lenS1094, _M0L8requiredS102);
  _M0L4dataS1090 = _M0L4selfS101->$0;
  moonbit_incref(_M0L4dataS1090);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L6_2atmpS1091 = _M0IPC16uint166UInt16PB7Default7default();
  _M0L3lenS1092 = _M0L4selfS101->$1;
  #line 80 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L9new__dataS103
  = _M0MPC15array10FixedArray23make__and__blit_2einnerGkE(_M0L4dataS1090, _M0L13new__capacityS100, _M0L6_2atmpS1091, _M0L3lenS1092, 0, 0);
  _M0L6_2aoldS2116 = _M0L4selfS101->$0;
  moonbit_decref(_M0L6_2aoldS2116);
  _M0L4selfS101->$0 = _M0L9new__dataS103;
  return 0;
}

int32_t _M0FPB31stringbuilder__growth__capacity(
  int32_t _M0L7currentS99,
  int32_t _M0L3lenS95,
  int32_t _M0L8requiredS94
) {
  int32_t _M0L5spaceS96;
  #line 48 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  if (_M0L8requiredS94 < _M0L3lenS95) {
    #line 55 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_14.data);
  }
  _M0L5spaceS96 = _M0L7currentS99;
  while (1) {
    if (_M0L5spaceS96 < _M0L8requiredS94) {
      int32_t _M0L4nextS97 = _M0L5spaceS96 * 2;
      if (_M0L4nextS97 <= _M0L5spaceS96) {
        return _M0L8requiredS94;
      }
      _M0L5spaceS96 = _M0L4nextS97;
      continue;
    } else {
      return _M0L5spaceS96;
    }
    break;
  }
}

int32_t _M0MPC14uint4UInt10to__uint16(uint32_t _M0L4selfS93) {
  int32_t _M0L6_2atmpS1089;
  #line 2785 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS1089 = *(int32_t*)&_M0L4selfS93;
  return (uint16_t)_M0L6_2atmpS1089;
}

uint32_t _M0MPC14char4Char8to__uint(int32_t _M0L4selfS92) {
  int32_t _M0L6_2atmpS1088;
  #line 1335 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS1088 = _M0L4selfS92;
  return *(uint32_t*)&_M0L6_2atmpS1088;
}

moonbit_string_t _M0MPB13StringBuilder10to__string(
  struct _M0TPB13StringBuilder* _M0L4selfS90
) {
  int32_t _M0L3lenS1079;
  #line 181 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L3lenS1079 = _M0L4selfS90->$1;
  if (_M0L3lenS1079 == 0) {
    return (moonbit_string_t)moonbit_string_literal_0.data;
  } else {
    int32_t _M0L3lenS1080 = _M0L4selfS90->$1;
    uint16_t* _M0L4dataS1082 = _M0L4selfS90->$0;
    int32_t _M0L6_2atmpS1081 = Moonbit_array_length(_M0L4dataS1082);
    if (_M0L3lenS1080 == _M0L6_2atmpS1081) {
      uint16_t* _M0L4dataS1083 = _M0L4selfS90->$0;
      moonbit_incref(_M0L4dataS1083);
      return _M0L4dataS1083;
    } else {
      uint16_t* _M0L4dataS1084 = _M0L4selfS90->$0;
      int32_t _M0L3lenS1085 = _M0L4selfS90->$1;
      int32_t _M0L6_2atmpS1086;
      int32_t _M0L3lenS1087;
      uint16_t* _M0L4dataS91;
      moonbit_incref(_M0L4dataS1084);
      #line 190 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0L6_2atmpS1086 = _M0IPC16uint166UInt16PB7Default7default();
      _M0L3lenS1087 = _M0L4selfS90->$1;
      #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0L4dataS91
      = _M0MPC15array10FixedArray23make__and__blit_2einnerGkE(_M0L4dataS1084, _M0L3lenS1085, _M0L6_2atmpS1086, _M0L3lenS1087, 0, 0);
      return _M0L4dataS91;
    }
  }
}

uint16_t* _M0MPC15array10FixedArray23make__and__blit_2einnerGkE(
  uint16_t* _M0L3srcS87,
  int32_t _M0L13allocate__lenS83,
  int32_t _M0L4initS88,
  int32_t _M0L3lenS84,
  int32_t _M0L11src__offsetS85,
  int32_t _M0L11dst__offsetS86
) {
  int32_t _if__result_2217;
  #line 97 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
  if (_M0L13allocate__lenS83 >= 0) {
    if (_M0L3lenS84 >= 0) {
      if (_M0L11src__offsetS85 >= 0) {
        if (_M0L11dst__offsetS86 >= 0) {
          int32_t _M0L6_2atmpS1075 = _M0L11src__offsetS85 + _M0L3lenS84;
          int32_t _M0L6_2atmpS1076 = Moonbit_array_length(_M0L3srcS87);
          if (_M0L6_2atmpS1075 <= _M0L6_2atmpS1076) {
            int32_t _M0L6_2atmpS1074 = _M0L11dst__offsetS86 + _M0L3lenS84;
            _if__result_2217 = _M0L6_2atmpS1074 <= _M0L13allocate__lenS83;
          } else {
            _if__result_2217 = 0;
          }
        } else {
          _if__result_2217 = 0;
        }
      } else {
        _if__result_2217 = 0;
      }
    } else {
      _if__result_2217 = 0;
    }
  } else {
    _if__result_2217 = 0;
  }
  if (_if__result_2217) {
    #line 116 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    return _M0MPC15array10FixedArray23unsafe__make__and__blitGkE(_M0L3srcS87, _M0L13allocate__lenS83, _M0L4initS88, _M0L11src__offsetS85, _M0L11dst__offsetS86, _M0L3lenS84);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS89;
    int32_t _M0L6_2atmpS1078;
    moonbit_string_t _M0L6_2atmpS1077;
    uint16_t* _result_2218;
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0L18_2astring__builderS89
    = _M0MPB13StringBuilder21StringBuilder_2einner(89);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS89, (moonbit_string_t)moonbit_string_literal_15.data);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS89, _M0L13allocate__lenS83);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS89, (moonbit_string_t)moonbit_string_literal_16.data);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS89, _M0L11src__offsetS85);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS89, (moonbit_string_t)moonbit_string_literal_17.data);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS89, _M0L11dst__offsetS86);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS89, (moonbit_string_t)moonbit_string_literal_18.data);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS89, _M0L3lenS84);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS89, (moonbit_string_t)moonbit_string_literal_19.data);
    _M0L6_2atmpS1078 = Moonbit_array_length(_M0L3srcS87);
    moonbit_decref(_M0L3srcS87);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS89, _M0L6_2atmpS1078);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0L6_2atmpS1077
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS89);
    moonbit_decref(_M0L18_2astring__builderS89);
    #line 112 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _result_2218 = _M0FPC15abort5abortGAkE(_M0L6_2atmpS1077);
    moonbit_decref(_M0L6_2atmpS1077);
    return _result_2218;
  }
}

uint16_t* _M0MPC15array10FixedArray23unsafe__make__and__blitGkE(
  uint16_t* _M0L3srcS80,
  int32_t _M0L13allocate__lenS77,
  int32_t _M0L4initS78,
  int32_t _M0L11src__offsetS81,
  int32_t _M0L11dst__offsetS79,
  int32_t _M0L9blit__lenS82
) {
  uint16_t* _M0L3dstS76;
  #line 79 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
  _M0L3dstS76
  = (uint16_t*)moonbit_make_string(_M0L13allocate__lenS77, _M0L4initS78);
  moonbit_incref(_M0L3dstS76);
  #line 90 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
  moonbit_unsafe_val_array_blit(_M0L3dstS76, _M0L11dst__offsetS79, _M0L3srcS80, _M0L11src__offsetS81, _M0L9blit__lenS82, sizeof(uint16_t));
  return _M0L3dstS76;
}

struct _M0TPB13StringBuilder* _M0MPB13StringBuilder21StringBuilder_2einner(
  int32_t _M0L10size__hintS74
) {
  int32_t _M0L7initialS73;
  uint16_t* _M0L4dataS75;
  struct _M0TPB13StringBuilder* _block_2219;
  #line 32 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  if (_M0L10size__hintS74 < 1) {
    _M0L7initialS73 = 1;
  } else {
    int32_t _M0L6_2atmpS1073 = _M0L10size__hintS74 + 1;
    _M0L7initialS73 = _M0L6_2atmpS1073 / 2;
  }
  _M0L4dataS75 = (uint16_t*)moonbit_make_string(_M0L7initialS73, 0);
  _block_2219
  = (struct _M0TPB13StringBuilder*)moonbit_malloc(sizeof(struct _M0TPB13StringBuilder));
  Moonbit_object_header(_block_2219)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 56, 0);
  _block_2219->$0 = _M0L4dataS75;
  _block_2219->$1 = 0;
  return _block_2219;
}

float* _M0MPB18UninitializedArray23make__and__blit_2einnerGfE(
  float* _M0L3srcS65,
  int32_t _M0L13allocate__lenS61,
  int32_t _M0L3lenS62,
  int32_t _M0L11src__offsetS63,
  int32_t _M0L11dst__offsetS64
) {
  int32_t _if__result_2220;
  #line 167 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  if (_M0L13allocate__lenS61 >= 0) {
    if (_M0L3lenS62 >= 0) {
      if (_M0L11src__offsetS63 >= 0) {
        if (_M0L11dst__offsetS64 >= 0) {
          int32_t _M0L6_2atmpS1064 = _M0L11src__offsetS63 + _M0L3lenS62;
          int32_t _M0L6_2atmpS1065;
          #line 179 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
          _M0L6_2atmpS1065
          = _M0MPB18UninitializedArray6lengthGfE(_M0L3srcS65);
          if (_M0L6_2atmpS1064 <= _M0L6_2atmpS1065) {
            int32_t _M0L6_2atmpS1063 = _M0L11dst__offsetS64 + _M0L3lenS62;
            _if__result_2220 = _M0L6_2atmpS1063 <= _M0L13allocate__lenS61;
          } else {
            _if__result_2220 = 0;
          }
        } else {
          _if__result_2220 = 0;
        }
      } else {
        _if__result_2220 = 0;
      }
    } else {
      _if__result_2220 = 0;
    }
  } else {
    _if__result_2220 = 0;
  }
  if (_if__result_2220) {
    #line 185 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    return _M0MPB18UninitializedArray23unsafe__make__and__blitGfE(_M0L3srcS65, _M0L13allocate__lenS61, _M0L11src__offsetS63, _M0L11dst__offsetS64, _M0L3lenS62);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS66;
    int32_t _M0L6_2atmpS1067;
    moonbit_string_t _M0L6_2atmpS1066;
    float* _result_2221;
    #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L18_2astring__builderS66
    = _M0MPB13StringBuilder21StringBuilder_2einner(89);
    #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS66, (moonbit_string_t)moonbit_string_literal_15.data);
    #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS66, _M0L13allocate__lenS61);
    #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS66, (moonbit_string_t)moonbit_string_literal_16.data);
    #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS66, _M0L11src__offsetS63);
    #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS66, (moonbit_string_t)moonbit_string_literal_17.data);
    #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS66, _M0L11dst__offsetS64);
    #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS66, (moonbit_string_t)moonbit_string_literal_18.data);
    #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS66, _M0L3lenS62);
    #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS66, (moonbit_string_t)moonbit_string_literal_19.data);
    #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS1067 = _M0MPB18UninitializedArray6lengthGfE(_M0L3srcS65);
    moonbit_decref(_M0L3srcS65);
    #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS66, _M0L6_2atmpS1067);
    #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS1066
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS66);
    moonbit_decref(_M0L18_2astring__builderS66);
    #line 181 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _result_2221
    = _M0FPC15abort5abortGRPB18UninitializedArrayGfEE(_M0L6_2atmpS1066);
    moonbit_decref(_M0L6_2atmpS1066);
    return _result_2221;
  }
}

int32_t* _M0MPB18UninitializedArray23make__and__blit_2einnerGiE(
  int32_t* _M0L3srcS71,
  int32_t _M0L13allocate__lenS67,
  int32_t _M0L3lenS68,
  int32_t _M0L11src__offsetS69,
  int32_t _M0L11dst__offsetS70
) {
  int32_t _if__result_2222;
  #line 167 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  if (_M0L13allocate__lenS67 >= 0) {
    if (_M0L3lenS68 >= 0) {
      if (_M0L11src__offsetS69 >= 0) {
        if (_M0L11dst__offsetS70 >= 0) {
          int32_t _M0L6_2atmpS1069 = _M0L11src__offsetS69 + _M0L3lenS68;
          int32_t _M0L6_2atmpS1070;
          #line 179 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
          _M0L6_2atmpS1070
          = _M0MPB18UninitializedArray6lengthGiE(_M0L3srcS71);
          if (_M0L6_2atmpS1069 <= _M0L6_2atmpS1070) {
            int32_t _M0L6_2atmpS1068 = _M0L11dst__offsetS70 + _M0L3lenS68;
            _if__result_2222 = _M0L6_2atmpS1068 <= _M0L13allocate__lenS67;
          } else {
            _if__result_2222 = 0;
          }
        } else {
          _if__result_2222 = 0;
        }
      } else {
        _if__result_2222 = 0;
      }
    } else {
      _if__result_2222 = 0;
    }
  } else {
    _if__result_2222 = 0;
  }
  if (_if__result_2222) {
    #line 185 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    return _M0MPB18UninitializedArray23unsafe__make__and__blitGiE(_M0L3srcS71, _M0L13allocate__lenS67, _M0L11src__offsetS69, _M0L11dst__offsetS70, _M0L3lenS68);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS72;
    int32_t _M0L6_2atmpS1072;
    moonbit_string_t _M0L6_2atmpS1071;
    int32_t* _result_2223;
    #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L18_2astring__builderS72
    = _M0MPB13StringBuilder21StringBuilder_2einner(89);
    #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS72, (moonbit_string_t)moonbit_string_literal_15.data);
    #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS72, _M0L13allocate__lenS67);
    #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS72, (moonbit_string_t)moonbit_string_literal_16.data);
    #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS72, _M0L11src__offsetS69);
    #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS72, (moonbit_string_t)moonbit_string_literal_17.data);
    #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS72, _M0L11dst__offsetS70);
    #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS72, (moonbit_string_t)moonbit_string_literal_18.data);
    #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS72, _M0L3lenS68);
    #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS72, (moonbit_string_t)moonbit_string_literal_19.data);
    #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS1072 = _M0MPB18UninitializedArray6lengthGiE(_M0L3srcS71);
    moonbit_decref(_M0L3srcS71);
    #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS72, _M0L6_2atmpS1072);
    #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS1071
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS72);
    moonbit_decref(_M0L18_2astring__builderS72);
    #line 181 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _result_2223
    = _M0FPC15abort5abortGRPB18UninitializedArrayGiEE(_M0L6_2atmpS1071);
    moonbit_decref(_M0L6_2atmpS1071);
    return _result_2223;
  }
}

int32_t _M0MPB13StringBuilder13write__objectGiE(
  struct _M0TPB13StringBuilder* _M0L4selfS58,
  int32_t _M0L3objS57
) {
  struct _M0TPB6Logger _M0L6_2atmpS1061;
  #line 17 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  moonbit_incref(_M0L4selfS58);
  _M0L6_2atmpS1061
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS58
  };
  #line 23 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  _M0IP016_24default__implPB4Show6outputGiE(_M0L3objS57, _M0L6_2atmpS1061);
  if (_M0L6_2atmpS1061.$1) {
    moonbit_decref(_M0L6_2atmpS1061.$1);
  }
  return 0;
}

int32_t _M0MPB13StringBuilder13write__objectGmE(
  struct _M0TPB13StringBuilder* _M0L4selfS60,
  uint64_t _M0L3objS59
) {
  struct _M0TPB6Logger _M0L6_2atmpS1062;
  #line 17 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  moonbit_incref(_M0L4selfS60);
  _M0L6_2atmpS1062
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS60
  };
  #line 23 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  _M0IP016_24default__implPB4Show6outputGmE(_M0L3objS59, _M0L6_2atmpS1062);
  if (_M0L6_2atmpS1062.$1) {
    moonbit_decref(_M0L6_2atmpS1062.$1);
  }
  return 0;
}

float* _M0MPB18UninitializedArray23unsafe__make__and__blitGfE(
  float* _M0L3srcS48,
  int32_t _M0L13allocate__lenS46,
  int32_t _M0L11src__offsetS49,
  int32_t _M0L11dst__offsetS47,
  int32_t _M0L9blit__lenS50
) {
  float* _M0L3dstS45;
  #line 132 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  _M0L3dstS45 = (float*)moonbit_make_float_array_raw(_M0L13allocate__lenS46);
  #line 142 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  _M0MPB18UninitializedArray12unsafe__blitGfE(_M0L3dstS45, _M0L11dst__offsetS47, _M0L3srcS48, _M0L11src__offsetS49, _M0L9blit__lenS50);
  moonbit_decref(_M0L3srcS48);
  return _M0L3dstS45;
}

int32_t* _M0MPB18UninitializedArray23unsafe__make__and__blitGiE(
  int32_t* _M0L3srcS54,
  int32_t _M0L13allocate__lenS52,
  int32_t _M0L11src__offsetS55,
  int32_t _M0L11dst__offsetS53,
  int32_t _M0L9blit__lenS56
) {
  int32_t* _M0L3dstS51;
  #line 132 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  _M0L3dstS51
  = (int32_t*)moonbit_make_int32_array_raw(_M0L13allocate__lenS52);
  #line 142 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  _M0MPB18UninitializedArray12unsafe__blitGiE(_M0L3dstS51, _M0L11dst__offsetS53, _M0L3srcS54, _M0L11src__offsetS55, _M0L9blit__lenS56);
  moonbit_decref(_M0L3srcS54);
  return _M0L3dstS51;
}

int32_t _M0MPB18UninitializedArray12unsafe__blitGfE(
  float* _M0L3dstS35,
  int32_t _M0L11dst__offsetS36,
  float* _M0L3srcS37,
  int32_t _M0L11src__offsetS38,
  int32_t _M0L3lenS39
) {
  #line 118 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  moonbit_incref(_M0L3srcS37);
  moonbit_incref(_M0L3dstS35);
  #line 127 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  moonbit_unsafe_val_array_blit(_M0L3dstS35, _M0L11dst__offsetS36, _M0L3srcS37, _M0L11src__offsetS38, _M0L3lenS39, sizeof(float));
  return 0;
}

int32_t _M0MPB18UninitializedArray12unsafe__blitGiE(
  int32_t* _M0L3dstS40,
  int32_t _M0L11dst__offsetS41,
  int32_t* _M0L3srcS42,
  int32_t _M0L11src__offsetS43,
  int32_t _M0L3lenS44
) {
  #line 118 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  moonbit_incref(_M0L3srcS42);
  moonbit_incref(_M0L3dstS40);
  #line 127 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  moonbit_unsafe_val_array_blit(_M0L3dstS40, _M0L11dst__offsetS41, _M0L3srcS42, _M0L11src__offsetS43, _M0L3lenS44, sizeof(int32_t));
  return 0;
}

int32_t _M0MPC15array10FixedArray12unsafe__blitGRPB17UnsafeMaybeUninitGfEE(
  float* _M0L3dstS8,
  int32_t _M0L11dst__offsetS10,
  float* _M0L3srcS9,
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
        int32_t _M0L6_2atmpS1034 = _M0L11dst__offsetS10 + _M0L1iS12;
        int32_t _M0L6_2atmpS1036 = _M0L11src__offsetS11 + _M0L1iS12;
        float _M0L6_2atmpS1035;
        int32_t _M0L6_2atmpS1037;
        if (
          _M0L6_2atmpS1036 < 0
          || _M0L6_2atmpS1036 >= Moonbit_array_length(_M0L3srcS9)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1035 = (float)_M0L3srcS9[_M0L6_2atmpS1036];
        if (
          _M0L6_2atmpS1034 < 0
          || _M0L6_2atmpS1034 >= Moonbit_array_length(_M0L3dstS8)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS8[_M0L6_2atmpS1034] = _M0L6_2atmpS1035;
        _M0L6_2atmpS1037 = _M0L1iS12 + 1;
        _M0L1iS12 = _M0L6_2atmpS1037;
        continue;
      } else {
        moonbit_decref(_M0L3srcS9);
        moonbit_decref(_M0L3dstS8);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1042 = _M0L3lenS13 - 1;
    int32_t _M0L1iS15 = _M0L6_2atmpS1042;
    while (1) {
      if (_M0L1iS15 >= 0) {
        int32_t _M0L6_2atmpS1038 = _M0L11dst__offsetS10 + _M0L1iS15;
        int32_t _M0L6_2atmpS1040 = _M0L11src__offsetS11 + _M0L1iS15;
        float _M0L6_2atmpS1039;
        int32_t _M0L6_2atmpS1041;
        if (
          _M0L6_2atmpS1040 < 0
          || _M0L6_2atmpS1040 >= Moonbit_array_length(_M0L3srcS9)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1039 = (float)_M0L3srcS9[_M0L6_2atmpS1040];
        if (
          _M0L6_2atmpS1038 < 0
          || _M0L6_2atmpS1038 >= Moonbit_array_length(_M0L3dstS8)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS8[_M0L6_2atmpS1038] = _M0L6_2atmpS1039;
        _M0L6_2atmpS1041 = _M0L1iS15 - 1;
        _M0L1iS15 = _M0L6_2atmpS1041;
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

int32_t _M0MPC15array10FixedArray12unsafe__blitGRPB17UnsafeMaybeUninitGiEE(
  int32_t* _M0L3dstS17,
  int32_t _M0L11dst__offsetS19,
  int32_t* _M0L3srcS18,
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
        int32_t _M0L6_2atmpS1043 = _M0L11dst__offsetS19 + _M0L1iS21;
        int32_t _M0L6_2atmpS1045 = _M0L11src__offsetS20 + _M0L1iS21;
        int32_t _M0L6_2atmpS1044;
        int32_t _M0L6_2atmpS1046;
        if (
          _M0L6_2atmpS1045 < 0
          || _M0L6_2atmpS1045 >= Moonbit_array_length(_M0L3srcS18)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1044 = (int32_t)_M0L3srcS18[_M0L6_2atmpS1045];
        if (
          _M0L6_2atmpS1043 < 0
          || _M0L6_2atmpS1043 >= Moonbit_array_length(_M0L3dstS17)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS17[_M0L6_2atmpS1043] = _M0L6_2atmpS1044;
        _M0L6_2atmpS1046 = _M0L1iS21 + 1;
        _M0L1iS21 = _M0L6_2atmpS1046;
        continue;
      } else {
        moonbit_decref(_M0L3srcS18);
        moonbit_decref(_M0L3dstS17);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1051 = _M0L3lenS22 - 1;
    int32_t _M0L1iS24 = _M0L6_2atmpS1051;
    while (1) {
      if (_M0L1iS24 >= 0) {
        int32_t _M0L6_2atmpS1047 = _M0L11dst__offsetS19 + _M0L1iS24;
        int32_t _M0L6_2atmpS1049 = _M0L11src__offsetS20 + _M0L1iS24;
        int32_t _M0L6_2atmpS1048;
        int32_t _M0L6_2atmpS1050;
        if (
          _M0L6_2atmpS1049 < 0
          || _M0L6_2atmpS1049 >= Moonbit_array_length(_M0L3srcS18)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1048 = (int32_t)_M0L3srcS18[_M0L6_2atmpS1049];
        if (
          _M0L6_2atmpS1047 < 0
          || _M0L6_2atmpS1047 >= Moonbit_array_length(_M0L3dstS17)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS17[_M0L6_2atmpS1047] = _M0L6_2atmpS1048;
        _M0L6_2atmpS1050 = _M0L1iS24 - 1;
        _M0L1iS24 = _M0L6_2atmpS1050;
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
        int32_t _M0L6_2atmpS1052 = _M0L11dst__offsetS28 + _M0L1iS30;
        int32_t _M0L6_2atmpS1054 = _M0L11src__offsetS29 + _M0L1iS30;
        int32_t _M0L6_2atmpS1053;
        int32_t _M0L6_2atmpS1055;
        if (
          _M0L6_2atmpS1054 < 0
          || _M0L6_2atmpS1054 >= Moonbit_array_length(_M0L3srcS27)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1053 = (int32_t)_M0L3srcS27[_M0L6_2atmpS1054];
        if (
          _M0L6_2atmpS1052 < 0
          || _M0L6_2atmpS1052 >= Moonbit_array_length(_M0L3dstS26)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS26[_M0L6_2atmpS1052] = _M0L6_2atmpS1053;
        _M0L6_2atmpS1055 = _M0L1iS30 + 1;
        _M0L1iS30 = _M0L6_2atmpS1055;
        continue;
      } else {
        moonbit_decref(_M0L3srcS27);
        moonbit_decref(_M0L3dstS26);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1060 = _M0L3lenS31 - 1;
    int32_t _M0L1iS33 = _M0L6_2atmpS1060;
    while (1) {
      if (_M0L1iS33 >= 0) {
        int32_t _M0L6_2atmpS1056 = _M0L11dst__offsetS28 + _M0L1iS33;
        int32_t _M0L6_2atmpS1058 = _M0L11src__offsetS29 + _M0L1iS33;
        int32_t _M0L6_2atmpS1057;
        int32_t _M0L6_2atmpS1059;
        if (
          _M0L6_2atmpS1058 < 0
          || _M0L6_2atmpS1058 >= Moonbit_array_length(_M0L3srcS27)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1057 = (int32_t)_M0L3srcS27[_M0L6_2atmpS1058];
        if (
          _M0L6_2atmpS1056 < 0
          || _M0L6_2atmpS1056 >= Moonbit_array_length(_M0L3dstS26)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS26[_M0L6_2atmpS1056] = _M0L6_2atmpS1057;
        _M0L6_2atmpS1059 = _M0L1iS33 - 1;
        _M0L1iS33 = _M0L6_2atmpS1059;
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

int32_t _M0MPB18UninitializedArray6lengthGfE(float* _M0L4selfS6) {
  #line 112 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  return Moonbit_array_length(_M0L4selfS6);
}

int32_t _M0MPB18UninitializedArray6lengthGiE(int32_t* _M0L4selfS7) {
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

float* _M0FPC15abort5abortGRPB18UninitializedArrayGfEE(
  moonbit_string_t _M0L3msgS2
) {
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

int32_t _M0FPC15abort5abortGiE(moonbit_string_t _M0L3msgS4) {
  #line 47 "C:\\Users\\31379\\.moon\\lib\\core\\abort\\abort.mbt"
  #line 49 "C:\\Users\\31379\\.moon\\lib\\core\\abort\\abort.mbt"
  moonbit_println(_M0L3msgS4);
  #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\abort\\abort.mbt"
  moonbit_panic();
}

uint16_t* _M0FPC15abort5abortGAkE(moonbit_string_t _M0L3msgS5) {
  #line 47 "C:\\Users\\31379\\.moon\\lib\\core\\abort\\abort.mbt"
  #line 49 "C:\\Users\\31379\\.moon\\lib\\core\\abort\\abort.mbt"
  moonbit_println(_M0L3msgS5);
  #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\abort\\abort.mbt"
  moonbit_panic();
}

int32_t _M0IP016_24default__implPB6Logger61write_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLoggerGRPB13StringBuilderE(
  void* _M0L11_2aobj__ptrS991,
  struct _M0TPB4Show _M0L8_2aparamS990
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS989 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS991;
  _M0IP016_24default__implPB6Logger5writeGRPB13StringBuilderE(_M0L7_2aselfS989, _M0L8_2aparamS990);
  return 0;
}

int32_t _M0IP016_24default__implPB6Logger84write__string__interpolation_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLoggerGRPB13StringBuilderE(
  void* _M0L11_2aobj__ptrS988,
  struct _M0TPB4Show _M0L8_2aparamS987
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS986 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS988;
  _M0IP016_24default__implPB6Logger28write__string__interpolationGRPB13StringBuilderE(_M0L7_2aselfS986, _M0L8_2aparamS987);
  return 0;
}

int32_t _M0IPB13StringBuilderPB6Logger67write__char_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLogger(
  void* _M0L11_2aobj__ptrS985,
  int32_t _M0L8_2aparamS984
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS983 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS985;
  _M0IPB13StringBuilderPB6Logger11write__char(_M0L7_2aselfS983, _M0L8_2aparamS984);
  return 0;
}

int32_t _M0IPB13StringBuilderPB6Logger67write__view_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLogger(
  void* _M0L11_2aobj__ptrS982,
  struct _M0TPC16string10StringView _M0L8_2aparamS981
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS980 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS982;
  _M0IPB13StringBuilderPB6Logger11write__view(_M0L7_2aselfS980, _M0L8_2aparamS981);
  return 0;
}

int32_t _M0IP016_24default__implPB6Logger72write__substring_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLoggerGRPB13StringBuilderE(
  void* _M0L11_2aobj__ptrS979,
  moonbit_string_t _M0L8_2aparamS976,
  int32_t _M0L8_2aparamS977,
  int32_t _M0L8_2aparamS978
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS975 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS979;
  _M0IP016_24default__implPB6Logger16write__substringGRPB13StringBuilderE(_M0L7_2aselfS975, _M0L8_2aparamS976, _M0L8_2aparamS977, _M0L8_2aparamS978);
  return 0;
}

int32_t _M0IPB13StringBuilderPB6Logger69write__string_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLogger(
  void* _M0L11_2aobj__ptrS974,
  moonbit_string_t _M0L8_2aparamS973
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS972 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS974;
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L7_2aselfS972, _M0L8_2aparamS973);
  return 0;
}

void moonbit_init() {
  moonbit_layout_table = moonbit_layout_table_data;
  int64_t _tmp_2230 = 9218868437227405311ll;
  int64_t _tmp_2231;
  int64_t _tmp_2232;
  int64_t _tmp_2233;
  int64_t _tmp_2234;
  _M0FPB18double__max__value = *(double*)&_tmp_2230;
  _tmp_2231 = -4503599627370497ll;
  _M0FPB18double__min__value = *(double*)&_tmp_2231;
  _tmp_2232 = 9221120237041090561ll;
  _M0FPC16double14not__a__number = *(double*)&_tmp_2232;
  _tmp_2233 = -4503599627370496ll;
  _M0FPC16double13neg__infinity = *(double*)&_tmp_2233;
  _tmp_2234 = 4503599627370496ll;
  _M0FPC16double13min__positive = *(double*)&_tmp_2234;
}

int main(int argc, char** argv) {
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS932;
  int32_t _M0L2neS933;
  int32_t _M0L3npvS934;
  int32_t _M0L4nsstS935;
  float _M0L2dtS936;
  struct _M0TP26RiantR8snn__mbt11IFParameter* _M0L7e__baseS937;
  struct _M0TP26RiantR8snn__mbt2IF* _M0L6e__popS938;
  struct _M0TP26RiantR8snn__mbt2IF* _M0L7pv__popS939;
  struct _M0TP26RiantR8snn__mbt2IF* _M0L8sst__popS940;
  int32_t _M0L7_2abindS941;
  int32_t _M0L1kS942;
  int32_t _M0L7_2abindS944;
  int32_t _M0L1kS945;
  int32_t _M0L7_2abindS947;
  int32_t _M0L1kS948;
  struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0L9e__to__pvS950;
  struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0L10e__to__sstS951;
  struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0L9pv__to__eS952;
  struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0L10sst__to__eS953;
  struct _M0TP26RiantR8snn__mbt7Monitor* _M0L5fm__eS954;
  struct _M0TP26RiantR8snn__mbt7Monitor* _M0L6fm__pvS955;
  struct _M0TP26RiantR8snn__mbt7Monitor* _M0L7fm__sstS956;
  float _M0L12duration__msS957;
  float _M0L6_2atmpS1033;
  int32_t _M0L5stepsS958;
  int32_t _M0L7_2abindS959;
  int32_t _M0L4stepS960;
  float _M0L10t__sim__msS963;
  int32_t _M0L8e__totalS964;
  int32_t _M0L9pv__totalS965;
  int32_t _M0L10sst__totalS966;
  float _M0L6_2atmpS1031;
  float _M0L6_2atmpS1032;
  float _M0L6_2atmpS1029;
  float _M0L6_2atmpS1030;
  float _M0L7e__rateS967;
  float _M0L6_2atmpS1027;
  float _M0L6_2atmpS1028;
  float _M0L6_2atmpS1025;
  float _M0L6_2atmpS1026;
  float _M0L8pv__rateS968;
  float _M0L6_2atmpS1023;
  float _M0L6_2atmpS1024;
  float _M0L6_2atmpS1021;
  float _M0L6_2atmpS1022;
  float _M0L9sst__rateS969;
  moonbit_string_t _M0L6_2atmpS1002;
  moonbit_string_t _M0L6_2atmpS1001;
  moonbit_string_t _M0L6_2atmpS1000;
  moonbit_string_t _M0L6_2atmpS1005;
  moonbit_string_t _M0L6_2atmpS1004;
  moonbit_string_t _M0L6_2atmpS1003;
  moonbit_string_t _M0L6_2atmpS1008;
  moonbit_string_t _M0L6_2atmpS1007;
  moonbit_string_t _M0L6_2atmpS1006;
  moonbit_string_t _M0L6_2atmpS1011;
  moonbit_string_t _M0L6_2atmpS1010;
  moonbit_string_t _M0L6_2atmpS1009;
  moonbit_string_t _M0L6_2atmpS1014;
  moonbit_string_t _M0L6_2atmpS1013;
  moonbit_string_t _M0L6_2atmpS1012;
  moonbit_string_t _M0L6_2atmpS1017;
  moonbit_string_t _M0L6_2atmpS1016;
  moonbit_string_t _M0L6_2atmpS1015;
  moonbit_string_t _M0L6_2atmpS1020;
  moonbit_string_t _M0L6_2atmpS1019;
  moonbit_string_t _M0L6_2atmpS1018;
  moonbit_runtime_init(argc, argv);
  moonbit_init();
  #line 22 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\duarte2019\\main.mbt"
  _M0L3rngS932 = _M0MP26RiantR8snn__mbt7Xoshiro7default();
  _M0L2neS933 = 20;
  _M0L3npvS934 = 8;
  _M0L4nsstS935 = 8;
  _M0L2dtS936 = 0x1p-3f;
  #line 34 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\duarte2019\\main.mbt"
  _M0L7e__baseS937
  = _M0MP26RiantR8snn__mbt11IFParameter8with__el(-0x1.18p+6f);
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\duarte2019\\main.mbt"
  _M0L6e__popS938
  = _M0MP26RiantR8snn__mbt2IF10with__gsyn(_M0L2neS933, _M0L7e__baseS937, 0x1.75c28f5c28f5cp-1f, 0x1.0f5c28f5c28f6p-2f, _M0L3rngS932);
  #line 38 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\duarte2019\\main.mbt"
  _M0L7pv__popS939
  = _M0MP26RiantR8snn__mbt2IF10with__gsyn(_M0L3npvS934, _M0L7e__baseS937, 0x1.0a3d70a3d70a4p+0f, 0x1.ae147ae147ae1p-1f, _M0L3rngS932);
  #line 41 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\duarte2019\\main.mbt"
  _M0L8sst__popS940
  = _M0MP26RiantR8snn__mbt2IF10with__gsyn(_M0L4nsstS935, _M0L7e__baseS937, 0x1.1eb851eb851ecp-1f, 0x1.2e147ae147ae1p-1f, _M0L3rngS932);
  moonbit_decref(_M0L7e__baseS937);
  _M0L7_2abindS941 = 0;
  _M0L1kS942 = _M0L7_2abindS941;
  while (1) {
    if (_M0L1kS942 < _M0L2neS933) {
      struct _M0TPB5ArrayGfE* _M0L1iS992 = _M0L6e__popS938->$7;
      int32_t _M0L6_2atmpS993;
      moonbit_incref(_M0L1iS992);
      #line 44 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\duarte2019\\main.mbt"
      _M0MPC15array5Array3setGfE(_M0L1iS992, _M0L1kS942, 0x1.9p+8f);
      moonbit_decref(_M0L1iS992);
      _M0L6_2atmpS993 = _M0L1kS942 + 1;
      _M0L1kS942 = _M0L6_2atmpS993;
      continue;
    }
    break;
  }
  _M0L7_2abindS944 = 0;
  _M0L1kS945 = _M0L7_2abindS944;
  while (1) {
    if (_M0L1kS945 < _M0L3npvS934) {
      struct _M0TPB5ArrayGfE* _M0L1iS994 = _M0L7pv__popS939->$7;
      int32_t _M0L6_2atmpS995;
      moonbit_incref(_M0L1iS994);
      #line 45 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\duarte2019\\main.mbt"
      _M0MPC15array5Array3setGfE(_M0L1iS994, _M0L1kS945, 0x1.5ep+8f);
      moonbit_decref(_M0L1iS994);
      _M0L6_2atmpS995 = _M0L1kS945 + 1;
      _M0L1kS945 = _M0L6_2atmpS995;
      continue;
    }
    break;
  }
  _M0L7_2abindS947 = 0;
  _M0L1kS948 = _M0L7_2abindS947;
  while (1) {
    if (_M0L1kS948 < _M0L4nsstS935) {
      struct _M0TPB5ArrayGfE* _M0L1iS996 = _M0L8sst__popS940->$7;
      int32_t _M0L6_2atmpS997;
      moonbit_incref(_M0L1iS996);
      #line 46 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\duarte2019\\main.mbt"
      _M0MPC15array5Array3setGfE(_M0L1iS996, _M0L1kS948, 0x1.2cp+8f);
      moonbit_decref(_M0L1iS996);
      _M0L6_2atmpS997 = _M0L1kS948 + 1;
      _M0L1kS948 = _M0L6_2atmpS997;
      continue;
    }
    break;
  }
  #line 49 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\duarte2019\\main.mbt"
  _M0L9e__to__pvS950
  = _M0MP26RiantR8snn__mbt14SpikingSynapse6random(_M0L6e__popS938, _M0L7pv__popS939, (moonbit_string_t)moonbit_string_literal_1.data, 0x1p+0f, 0x0p+0f, 0x1.3333333333333p-2f, _M0L3rngS932);
  #line 52 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\duarte2019\\main.mbt"
  _M0L10e__to__sstS951
  = _M0MP26RiantR8snn__mbt14SpikingSynapse6random(_M0L6e__popS938, _M0L8sst__popS940, (moonbit_string_t)moonbit_string_literal_1.data, 0x1p+0f, 0x0p+0f, 0x1.3333333333333p-2f, _M0L3rngS932);
  #line 55 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\duarte2019\\main.mbt"
  _M0L9pv__to__eS952
  = _M0MP26RiantR8snn__mbt14SpikingSynapse6random(_M0L7pv__popS939, _M0L6e__popS938, (moonbit_string_t)moonbit_string_literal_20.data, 0x1p+1f, 0x0p+0f, 0x1.3333333333333p-2f, _M0L3rngS932);
  #line 58 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\duarte2019\\main.mbt"
  _M0L10sst__to__eS953
  = _M0MP26RiantR8snn__mbt14SpikingSynapse6random(_M0L8sst__popS940, _M0L6e__popS938, (moonbit_string_t)moonbit_string_literal_20.data, 0x1p+1f, 0x0p+0f, 0x1.3333333333333p-2f, _M0L3rngS932);
  moonbit_decref(_M0L3rngS932);
  #line 63 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\duarte2019\\main.mbt"
  _M0L5fm__eS954
  = _M0MP26RiantR8snn__mbt7Monitor9new__fire(_M0L6e__popS938, 0);
  #line 64 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\duarte2019\\main.mbt"
  _M0L6fm__pvS955
  = _M0MP26RiantR8snn__mbt7Monitor9new__fire(_M0L7pv__popS939, 0);
  #line 65 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\duarte2019\\main.mbt"
  _M0L7fm__sstS956
  = _M0MP26RiantR8snn__mbt7Monitor9new__fire(_M0L8sst__popS940, 0);
  _M0L12duration__msS957 = 0x1.9p+6f;
  _M0L6_2atmpS1033 = _M0L12duration__msS957 / _M0L2dtS936;
  #line 69 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\duarte2019\\main.mbt"
  _M0L5stepsS958 = _M0MPC15float5Float7to__int(_M0L6_2atmpS1033);
  _M0L7_2abindS959 = 0;
  _M0L4stepS960 = _M0L7_2abindS959;
  while (1) {
    if (_M0L4stepS960 < _M0L5stepsS958) {
      float _M0L6_2atmpS998 = (float)_M0L4stepS960;
      float _M0L6t__nowS961 = _M0L6_2atmpS998 * _M0L2dtS936;
      int32_t _M0L6_2atmpS999;
      #line 74 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\duarte2019\\main.mbt"
      _M0FP26RiantR8snn__mbt16forward__synapse(_M0L9e__to__pvS950, _M0L6t__nowS961);
      #line 75 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\duarte2019\\main.mbt"
      _M0FP26RiantR8snn__mbt16forward__synapse(_M0L10e__to__sstS951, _M0L6t__nowS961);
      #line 76 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\duarte2019\\main.mbt"
      _M0FP26RiantR8snn__mbt16forward__synapse(_M0L9pv__to__eS952, _M0L6t__nowS961);
      #line 77 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\duarte2019\\main.mbt"
      _M0FP26RiantR8snn__mbt16forward__synapse(_M0L10sst__to__eS953, _M0L6t__nowS961);
      #line 80 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\duarte2019\\main.mbt"
      _M0FP26RiantR8snn__mbt14step__synapses(_M0L6e__popS938, _M0L2dtS936);
      #line 81 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\duarte2019\\main.mbt"
      _M0FP26RiantR8snn__mbt17synaptic__current(_M0L6e__popS938);
      #line 82 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\duarte2019\\main.mbt"
      _M0FP26RiantR8snn__mbt12step__neuron(_M0L6e__popS938, _M0L2dtS936);
      #line 83 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\duarte2019\\main.mbt"
      _M0FP26RiantR8snn__mbt14step__synapses(_M0L7pv__popS939, _M0L2dtS936);
      #line 84 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\duarte2019\\main.mbt"
      _M0FP26RiantR8snn__mbt17synaptic__current(_M0L7pv__popS939);
      #line 85 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\duarte2019\\main.mbt"
      _M0FP26RiantR8snn__mbt12step__neuron(_M0L7pv__popS939, _M0L2dtS936);
      #line 86 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\duarte2019\\main.mbt"
      _M0FP26RiantR8snn__mbt14step__synapses(_M0L8sst__popS940, _M0L2dtS936);
      #line 87 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\duarte2019\\main.mbt"
      _M0FP26RiantR8snn__mbt17synaptic__current(_M0L8sst__popS940);
      #line 88 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\duarte2019\\main.mbt"
      _M0FP26RiantR8snn__mbt12step__neuron(_M0L8sst__popS940, _M0L2dtS936);
      #line 91 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\duarte2019\\main.mbt"
      _M0FP26RiantR8snn__mbt11record__one(_M0L5fm__eS954, _M0L6t__nowS961);
      #line 92 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\duarte2019\\main.mbt"
      _M0FP26RiantR8snn__mbt11record__one(_M0L6fm__pvS955, _M0L6t__nowS961);
      #line 93 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\duarte2019\\main.mbt"
      _M0FP26RiantR8snn__mbt11record__one(_M0L7fm__sstS956, _M0L6t__nowS961);
      _M0L6_2atmpS999 = _M0L4stepS960 + 1;
      _M0L4stepS960 = _M0L6_2atmpS999;
      continue;
    } else {
      moonbit_decref(_M0L10sst__to__eS953);
      moonbit_decref(_M0L9pv__to__eS952);
      moonbit_decref(_M0L10e__to__sstS951);
      moonbit_decref(_M0L9e__to__pvS950);
      moonbit_decref(_M0L8sst__popS940);
      moonbit_decref(_M0L7pv__popS939);
      moonbit_decref(_M0L6e__popS938);
    }
    break;
  }
  _M0L10t__sim__msS963 = _M0L12duration__msS957;
  #line 98 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\duarte2019\\main.mbt"
  _M0L8e__totalS964
  = _M0MP26RiantR8snn__mbt7Monitor13count__spikes(_M0L5fm__eS954);
  moonbit_decref(_M0L5fm__eS954);
  #line 99 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\duarte2019\\main.mbt"
  _M0L9pv__totalS965
  = _M0MP26RiantR8snn__mbt7Monitor13count__spikes(_M0L6fm__pvS955);
  moonbit_decref(_M0L6fm__pvS955);
  #line 100 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\duarte2019\\main.mbt"
  _M0L10sst__totalS966
  = _M0MP26RiantR8snn__mbt7Monitor13count__spikes(_M0L7fm__sstS956);
  moonbit_decref(_M0L7fm__sstS956);
  _M0L6_2atmpS1031 = (float)_M0L8e__totalS964;
  _M0L6_2atmpS1032 = (float)_M0L2neS933;
  _M0L6_2atmpS1029 = _M0L6_2atmpS1031 / _M0L6_2atmpS1032;
  _M0L6_2atmpS1030 = _M0L10t__sim__msS963 / 0x1.f4p+9f;
  _M0L7e__rateS967 = _M0L6_2atmpS1029 / _M0L6_2atmpS1030;
  _M0L6_2atmpS1027 = (float)_M0L9pv__totalS965;
  _M0L6_2atmpS1028 = (float)_M0L3npvS934;
  _M0L6_2atmpS1025 = _M0L6_2atmpS1027 / _M0L6_2atmpS1028;
  _M0L6_2atmpS1026 = _M0L10t__sim__msS963 / 0x1.f4p+9f;
  _M0L8pv__rateS968 = _M0L6_2atmpS1025 / _M0L6_2atmpS1026;
  _M0L6_2atmpS1023 = (float)_M0L10sst__totalS966;
  _M0L6_2atmpS1024 = (float)_M0L4nsstS935;
  _M0L6_2atmpS1021 = _M0L6_2atmpS1023 / _M0L6_2atmpS1024;
  _M0L6_2atmpS1022 = _M0L10t__sim__msS963 / 0x1.f4p+9f;
  _M0L9sst__rateS969 = _M0L6_2atmpS1021 / _M0L6_2atmpS1022;
  #line 104 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\duarte2019\\main.mbt"
  _M0FPB7printlnGsE((moonbit_string_t)moonbit_string_literal_21.data);
  #line 107 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\duarte2019\\main.mbt"
  _M0FPB7printlnGsE((moonbit_string_t)moonbit_string_literal_0.data);
  #line 108 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\duarte2019\\main.mbt"
  _M0FPB7printlnGsE((moonbit_string_t)moonbit_string_literal_22.data);
  #line 109 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\duarte2019\\main.mbt"
  _M0L6_2atmpS1002 = _M0MPC13int3Int18to__string_2einner(_M0L2neS933, 10);
  #line 109 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\duarte2019\\main.mbt"
  _M0L6_2atmpS1001
  = moonbit_add_string((moonbit_string_t)moonbit_string_literal_23.data, _M0L6_2atmpS1002);
  moonbit_decref(_M0L6_2atmpS1002);
  #line 109 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\duarte2019\\main.mbt"
  _M0L6_2atmpS1000
  = moonbit_add_string(_M0L6_2atmpS1001, (moonbit_string_t)moonbit_string_literal_24.data);
  moonbit_decref(_M0L6_2atmpS1001);
  #line 109 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\duarte2019\\main.mbt"
  _M0FPB7printlnGsE(_M0L6_2atmpS1000);
  moonbit_decref(_M0L6_2atmpS1000);
  #line 110 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\duarte2019\\main.mbt"
  _M0L6_2atmpS1005 = _M0MPC13int3Int18to__string_2einner(_M0L3npvS934, 10);
  #line 110 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\duarte2019\\main.mbt"
  _M0L6_2atmpS1004
  = moonbit_add_string((moonbit_string_t)moonbit_string_literal_25.data, _M0L6_2atmpS1005);
  moonbit_decref(_M0L6_2atmpS1005);
  #line 110 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\duarte2019\\main.mbt"
  _M0L6_2atmpS1003
  = moonbit_add_string(_M0L6_2atmpS1004, (moonbit_string_t)moonbit_string_literal_26.data);
  moonbit_decref(_M0L6_2atmpS1004);
  #line 110 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\duarte2019\\main.mbt"
  _M0FPB7printlnGsE(_M0L6_2atmpS1003);
  moonbit_decref(_M0L6_2atmpS1003);
  #line 111 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\duarte2019\\main.mbt"
  _M0L6_2atmpS1008 = _M0MPC13int3Int18to__string_2einner(_M0L4nsstS935, 10);
  #line 111 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\duarte2019\\main.mbt"
  _M0L6_2atmpS1007
  = moonbit_add_string((moonbit_string_t)moonbit_string_literal_27.data, _M0L6_2atmpS1008);
  moonbit_decref(_M0L6_2atmpS1008);
  #line 111 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\duarte2019\\main.mbt"
  _M0L6_2atmpS1006
  = moonbit_add_string(_M0L6_2atmpS1007, (moonbit_string_t)moonbit_string_literal_28.data);
  moonbit_decref(_M0L6_2atmpS1007);
  #line 111 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\duarte2019\\main.mbt"
  _M0FPB7printlnGsE(_M0L6_2atmpS1006);
  moonbit_decref(_M0L6_2atmpS1006);
  #line 112 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\duarte2019\\main.mbt"
  _M0FPB7printlnGsE((moonbit_string_t)moonbit_string_literal_0.data);
  #line 113 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\duarte2019\\main.mbt"
  _M0L6_2atmpS1011
  = _M0IPC15float5FloatPB4Show10to__string(_M0L10t__sim__msS963);
  #line 113 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\duarte2019\\main.mbt"
  _M0L6_2atmpS1010
  = moonbit_add_string((moonbit_string_t)moonbit_string_literal_29.data, _M0L6_2atmpS1011);
  moonbit_decref(_M0L6_2atmpS1011);
  #line 113 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\duarte2019\\main.mbt"
  _M0L6_2atmpS1009
  = moonbit_add_string(_M0L6_2atmpS1010, (moonbit_string_t)moonbit_string_literal_30.data);
  moonbit_decref(_M0L6_2atmpS1010);
  #line 113 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\duarte2019\\main.mbt"
  _M0FPB7printlnGsE(_M0L6_2atmpS1009);
  moonbit_decref(_M0L6_2atmpS1009);
  #line 114 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\duarte2019\\main.mbt"
  _M0L6_2atmpS1014 = _M0IPC15float5FloatPB4Show10to__string(_M0L7e__rateS967);
  #line 114 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\duarte2019\\main.mbt"
  _M0L6_2atmpS1013
  = moonbit_add_string((moonbit_string_t)moonbit_string_literal_31.data, _M0L6_2atmpS1014);
  moonbit_decref(_M0L6_2atmpS1014);
  #line 114 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\duarte2019\\main.mbt"
  _M0L6_2atmpS1012
  = moonbit_add_string(_M0L6_2atmpS1013, (moonbit_string_t)moonbit_string_literal_32.data);
  moonbit_decref(_M0L6_2atmpS1013);
  #line 114 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\duarte2019\\main.mbt"
  _M0FPB7printlnGsE(_M0L6_2atmpS1012);
  moonbit_decref(_M0L6_2atmpS1012);
  #line 115 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\duarte2019\\main.mbt"
  _M0L6_2atmpS1017
  = _M0IPC15float5FloatPB4Show10to__string(_M0L8pv__rateS968);
  #line 115 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\duarte2019\\main.mbt"
  _M0L6_2atmpS1016
  = moonbit_add_string((moonbit_string_t)moonbit_string_literal_33.data, _M0L6_2atmpS1017);
  moonbit_decref(_M0L6_2atmpS1017);
  #line 115 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\duarte2019\\main.mbt"
  _M0L6_2atmpS1015
  = moonbit_add_string(_M0L6_2atmpS1016, (moonbit_string_t)moonbit_string_literal_32.data);
  moonbit_decref(_M0L6_2atmpS1016);
  #line 115 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\duarte2019\\main.mbt"
  _M0FPB7printlnGsE(_M0L6_2atmpS1015);
  moonbit_decref(_M0L6_2atmpS1015);
  #line 116 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\duarte2019\\main.mbt"
  _M0L6_2atmpS1020
  = _M0IPC15float5FloatPB4Show10to__string(_M0L9sst__rateS969);
  #line 116 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\duarte2019\\main.mbt"
  _M0L6_2atmpS1019
  = moonbit_add_string((moonbit_string_t)moonbit_string_literal_34.data, _M0L6_2atmpS1020);
  moonbit_decref(_M0L6_2atmpS1020);
  #line 116 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\duarte2019\\main.mbt"
  _M0L6_2atmpS1018
  = moonbit_add_string(_M0L6_2atmpS1019, (moonbit_string_t)moonbit_string_literal_32.data);
  moonbit_decref(_M0L6_2atmpS1019);
  #line 116 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\duarte2019\\main.mbt"
  _M0FPB7printlnGsE(_M0L6_2atmpS1018);
  moonbit_decref(_M0L6_2atmpS1018);
  return 0;
}