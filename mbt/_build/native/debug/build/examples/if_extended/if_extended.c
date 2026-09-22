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
struct _M0BTPB6Logger;

struct _M0BTPB4Show;

struct _M0TP26RiantR8snn__mbt10ExtendedIF;

struct _M0TPB8MutLocalGiE;

struct _M0TPC16string10StringView;

struct _M0KTPB6LoggerTPB13StringBuilder;

struct _M0TPB6Logger;

struct _M0TP26RiantR8snn__mbt19ExtendedIFParameter;

struct _M0TPB4Show;

struct _M0TPB13StringBuilder;

struct _M0TPB5ArrayGfE;

struct _M0TPB17FloatingDecimal64;

struct _M0TPB19MulShiftAll64Result;

struct _M0TPB5ArrayGbE;

struct _M0TPB7Umul128;

struct _M0TPB8Pow5Pair;

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

struct _M0TP26RiantR8snn__mbt10ExtendedIF {
  int32_t $0;
  struct _M0TP26RiantR8snn__mbt19ExtendedIFParameter* $1;
  struct _M0TPB5ArrayGfE* $2;
  struct _M0TPB5ArrayGfE* $3;
  struct _M0TPB5ArrayGfE* $4;
  struct _M0TPB5ArrayGfE* $5;
  struct _M0TPB5ArrayGfE* $6;
  struct _M0TPB5ArrayGbE* $7;
  struct _M0TPB5ArrayGfE* $8;
  
};

struct _M0TPB8MutLocalGiE {
  int32_t $0;
  
};

struct _M0TPC16string10StringView {
  moonbit_string_t $0;
  int32_t $1;
  int32_t $2;
  
};

struct _M0KTPB6LoggerTPB13StringBuilder {
  struct _M0BTPB6Logger* $0;
  void* $1;
  
};

struct _M0TPB6Logger {
  struct _M0BTPB6Logger* $0;
  void* $1;
  
};

struct _M0TP26RiantR8snn__mbt19ExtendedIFParameter {
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

struct _M0TPB4Show {
  struct _M0BTPB4Show* $0;
  void* $1;
  
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

struct _M0TPB19MulShiftAll64Result {
  uint64_t $0;
  uint64_t $1;
  uint64_t $2;
  
};

struct _M0TPB5ArrayGbE {
  uint8_t* $0;
  int32_t $1;
  
};

struct _M0TPB7Umul128 {
  uint64_t $0;
  uint64_t $1;
  
};

struct _M0TPB8Pow5Pair {
  uint64_t $0;
  uint64_t $1;
  
};

int32_t _M0FP26RiantR8snn__mbt23integrate__extended__if(
  struct _M0TP26RiantR8snn__mbt10ExtendedIF*,
  struct _M0TP26RiantR8snn__mbt19ExtendedIFParameter*,
  float
);

int32_t _M0FP26RiantR8snn__mbt28update__neuron__extended__if(
  struct _M0TP26RiantR8snn__mbt10ExtendedIF*,
  struct _M0TP26RiantR8snn__mbt19ExtendedIFParameter*,
  float
);

int32_t _M0FP26RiantR8snn__mbt30update__synapses__extended__if(
  struct _M0TP26RiantR8snn__mbt10ExtendedIF*,
  struct _M0TP26RiantR8snn__mbt19ExtendedIFParameter*,
  float
);

struct _M0TP26RiantR8snn__mbt10ExtendedIF* _M0MP26RiantR8snn__mbt10ExtendedIF3new(
  int64_t,
  struct _M0TP26RiantR8snn__mbt19ExtendedIFParameter*
);

struct _M0TP26RiantR8snn__mbt10ExtendedIF* _M0MP26RiantR8snn__mbt10ExtendedIF11new_2einner(
  int32_t,
  struct _M0TP26RiantR8snn__mbt19ExtendedIFParameter*
);

struct _M0TP26RiantR8snn__mbt19ExtendedIFParameter* _M0MP26RiantR8snn__mbt19ExtendedIFParameter6custom(
  float,
  float,
  float,
  float,
  float,
  float,
  float,
  float,
  float,
  float,
  float
);

struct _M0TP26RiantR8snn__mbt19ExtendedIFParameter* _M0MP26RiantR8snn__mbt19ExtendedIFParameter3new(
  
);

moonbit_string_t _M0IPC15float5FloatPB4Show10to__string(float);

int32_t _M0MPC15float5Float7to__int(float);

struct _M0TPB5ArrayGfE* _M0MPC15array5Array4makeGfE(int32_t, float);

struct _M0TPB5ArrayGbE* _M0MPC15array5Array4makeGbE(int32_t, int32_t);

int32_t _M0MPC15array5Array3setGfE(struct _M0TPB5ArrayGfE*, int32_t, float);

int32_t _M0MPC15array5Array3setGbE(struct _M0TPB5ArrayGbE*, int32_t, int32_t);

int32_t _M0MPC15array5Array2atGbE(struct _M0TPB5ArrayGbE*, int32_t);

float _M0MPC15array5Array2atGfE(struct _M0TPB5ArrayGfE*, int32_t);

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

uint64_t _M0MPC15array13ReadOnlyArray2atGmE(uint64_t*, int32_t);

uint32_t _M0MPC15array13ReadOnlyArray2atGjE(uint32_t*, int32_t);

moonbit_string_t _M0IPC16uint646UInt64PB4Show10to__string(uint64_t);

moonbit_string_t _M0IPC13int3IntPB4Show10to__string(int32_t);

moonbit_string_t _M0IPC14bool4BoolPB4Show10to__string(int32_t);

uint64_t _M0MPC14uint4UInt10to__uint64(uint32_t);

float* _M0MPC15array5Array6bufferGfE(struct _M0TPB5ArrayGfE*);

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

int32_t _M0IP016_24default__implPB4Show6outputGiE(
  int32_t,
  struct _M0TPB6Logger
);

int32_t _M0IP016_24default__implPB4Show6outputGfE(
  float,
  struct _M0TPB6Logger
);

int32_t _M0IP016_24default__implPB4Show6outputGbE(
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

int32_t _M0MPB13StringBuilder13write__objectGiE(
  struct _M0TPB13StringBuilder*,
  int32_t
);

int32_t _M0MPB13StringBuilder13write__objectGfE(
  struct _M0TPB13StringBuilder*,
  float
);

int32_t _M0MPB13StringBuilder13write__objectGbE(
  struct _M0TPB13StringBuilder*,
  int32_t
);

int32_t _M0MPB13StringBuilder13write__objectGmE(
  struct _M0TPB13StringBuilder*,
  uint64_t
);

int32_t _M0MPC15array10FixedArray12unsafe__blitGkE(
  uint16_t*,
  int32_t,
  uint16_t*,
  int32_t,
  int32_t
);

int32_t _M0FPC15abort5abortGuE(moonbit_string_t);

uint16_t* _M0FPC15abort5abortGAkE(moonbit_string_t);

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

struct { int32_t rc; uint32_t meta; uint16_t const data[66]; 
} const moonbit_string_literal_25 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 65, 105, 102, 
    95, 101, 120, 116, 101, 110, 100, 101, 100, 46, 109, 98, 116, 58, 
    32, 115, 105, 109, 117, 108, 97, 116, 105, 111, 110, 32, 100, 111, 
    110, 101, 32, 40, 49, 48, 48, 109, 115, 32, 64, 32, 100, 116, 61, 
    48, 46, 49, 50, 53, 109, 115, 32, 61, 32, 56, 48, 48, 32, 115, 116, 
    101, 112, 115, 41, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[1]; 
} const moonbit_string_literal_4 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 0, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[16]; 
} const moonbit_string_literal_27 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 15, 32, 32, 
    69, 91, 48, 93, 32, 103, 95, 101, 120, 99, 32, 61, 32, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[45]; 
} const moonbit_string_literal_24 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 44, 32, 110, 
    101, 117, 114, 111, 110, 115, 32, 40, 86, 116, 61, 45, 52, 53, 44, 
    32, 86, 114, 61, 45, 53, 53, 44, 32, 116, 97, 117, 95, 105, 61, 51, 
    48, 109, 115, 44, 32, 945, 61, 48, 46, 53, 41, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[16]; 
} const moonbit_string_literal_15 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 15, 44, 32, 
    100, 115, 116, 95, 111, 102, 102, 115, 101, 116, 32, 61, 32, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[19]; 
} const moonbit_string_literal_11 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 18, 105, 110, 
    118, 97, 108, 105, 100, 32, 99, 111, 100, 101, 32, 112, 111, 105, 
    110, 116, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[2]; 
} const moonbit_string_literal_3 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 1, 45, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[31]; 
} const moonbit_string_literal_9 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 30, 114, 97, 
    100, 105, 120, 32, 109, 117, 115, 116, 32, 98, 101, 32, 98, 101, 
    116, 119, 101, 101, 110, 32, 50, 32, 97, 110, 100, 32, 51, 54, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[9]; 
} const moonbit_string_literal_5 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 8, 73, 110, 
    102, 105, 110, 105, 116, 121, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[4]; 
} const moonbit_string_literal_2 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 3, 78, 97, 78, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[18]; 
} const moonbit_string_literal_26 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 17, 32, 32, 
    69, 91, 48, 93, 32, 118, 32, 102, 105, 110, 97, 108, 32, 61, 32, 
    0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[15]; 
} const moonbit_string_literal_30 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 14, 32, 32, 
    69, 91, 48, 93, 32, 102, 105, 114, 101, 32, 61, 32, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[2]; 
} const moonbit_string_literal_0 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 1, 48, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[38]; 
} const moonbit_string_literal_22 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 37, 32, 110, 
    101, 117, 114, 111, 110, 115, 32, 40, 86, 116, 61, 45, 53, 48, 44, 
    32, 86, 114, 61, 45, 54, 48, 44, 32, 116, 97, 117, 95, 105, 61, 49, 
    53, 109, 115, 41, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[9]; 
} const moonbit_string_literal_16 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 8, 44, 32, 
    108, 101, 110, 32, 61, 32, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[6]; 
} const moonbit_string_literal_8 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 5, 102, 97, 
    108, 115, 101, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[37]; 
} const moonbit_string_literal_13 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 36, 98, 111, 
    117, 110, 100, 115, 32, 99, 104, 101, 99, 107, 32, 102, 97, 105, 
    108, 101, 100, 58, 32, 97, 108, 108, 111, 99, 97, 116, 101, 95, 108, 
    101, 110, 32, 61, 32, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[9]; 
} const moonbit_string_literal_21 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 8, 32, 32, 
    80, 86, 32, 32, 61, 32, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[29]; 
} const moonbit_string_literal_31 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 28, 32, 32, 
    69, 32, 116, 111, 116, 97, 108, 32, 115, 112, 105, 107, 101, 115, 
    32, 105, 110, 32, 49, 48, 48, 109, 115, 32, 61, 32, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[5]; 
} const moonbit_string_literal_7 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 4, 116, 114, 
    117, 101, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[37]; 
} const moonbit_string_literal_10 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 36, 48, 49, 
    50, 51, 52, 53, 54, 55, 56, 57, 97, 98, 99, 100, 101, 102, 103, 104, 
    105, 106, 107, 108, 109, 110, 111, 112, 113, 114, 115, 116, 117, 
    118, 119, 120, 121, 122, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[53]; 
} const moonbit_string_literal_18 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 52, 105, 102, 
    95, 101, 120, 116, 101, 110, 100, 101, 100, 46, 109, 98, 116, 58, 
    32, 51, 45, 112, 111, 112, 117, 108, 97, 116, 105, 111, 110, 32, 
    69, 47, 80, 86, 47, 83, 83, 84, 32, 110, 101, 116, 119, 111, 114, 
    107, 32, 98, 117, 105, 108, 116, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[16]; 
} const moonbit_string_literal_17 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 15, 44, 32, 
    115, 114, 99, 46, 108, 101, 110, 103, 116, 104, 32, 61, 32, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[10]; 
} const moonbit_string_literal_28 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 9, 44, 32, 
    103, 95, 112, 118, 32, 61, 32, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[13]; 
} const moonbit_string_literal_32 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 12, 32, 32, 
    80, 86, 91, 48, 93, 32, 118, 32, 61, 32, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[9]; 
} const moonbit_string_literal_23 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 8, 32, 32, 
    83, 83, 84, 32, 61, 32, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[16]; 
} const moonbit_string_literal_14 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 15, 44, 32, 
    115, 114, 99, 95, 111, 102, 102, 115, 101, 116, 32, 61, 32, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[9]; 
} const moonbit_string_literal_19 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 8, 32, 32, 
    69, 32, 32, 32, 61, 32, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[11]; 
} const moonbit_string_literal_29 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 10, 44, 32, 
    103, 95, 115, 115, 116, 32, 61, 32, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[14]; 
} const moonbit_string_literal_33 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 13, 44, 32, 
    83, 83, 84, 91, 48, 93, 32, 118, 32, 61, 32, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[26]; 
} const moonbit_string_literal_1 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 25, 73, 108, 
    108, 101, 103, 97, 108, 65, 114, 103, 117, 109, 101, 110, 116, 69, 
    120, 99, 101, 112, 116, 105, 111, 110, 32, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[37]; 
} const moonbit_string_literal_20 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 36, 32, 110, 
    101, 117, 114, 111, 110, 115, 32, 40, 100, 101, 102, 97, 117, 108, 
    116, 32, 69, 120, 116, 101, 110, 100, 101, 100, 73, 70, 32, 112, 
    97, 114, 97, 109, 115, 41, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[32]; 
} const moonbit_string_literal_12 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 31, 83, 116, 
    114, 105, 110, 103, 66, 117, 105, 108, 100, 101, 114, 32, 99, 97, 
    112, 97, 99, 105, 116, 121, 32, 111, 118, 101, 114, 102, 108, 111, 
    119, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[4]; 
} const moonbit_string_literal_6 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 3, 48, 46, 48, 0};

uint32_t const moonbit_layout_table_data[19] =
  {
    sizeof(struct _M0TP26RiantR8snn__mbt10ExtendedIF) / 4, 8,
    offsetof(struct _M0TP26RiantR8snn__mbt10ExtendedIF, $1) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt10ExtendedIF, $2) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt10ExtendedIF, $3) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt10ExtendedIF, $4) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt10ExtendedIF, $5) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt10ExtendedIF, $6) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt10ExtendedIF, $7) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt10ExtendedIF, $8) / 4 * 2,
    sizeof(struct _M0TPB5ArrayGfE) / 4, 1,
    offsetof(struct _M0TPB5ArrayGfE, $0) / 4 * 2,
    sizeof(struct _M0TPB5ArrayGbE) / 4, 1,
    offsetof(struct _M0TPB5ArrayGbE, $0) / 4 * 2,
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

int32_t _M0FP26RiantR8snn__mbt23integrate__extended__if(
  struct _M0TP26RiantR8snn__mbt10ExtendedIF* _M0L1pS600,
  struct _M0TP26RiantR8snn__mbt19ExtendedIFParameter* _M0L5paramS601,
  float _M0L2dtS602
) {
  #line 214 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_extended_if.mbt"
  #line 219 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_extended_if.mbt"
  _M0FP26RiantR8snn__mbt30update__synapses__extended__if(_M0L1pS600, _M0L5paramS601, _M0L2dtS602);
  #line 220 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_extended_if.mbt"
  _M0FP26RiantR8snn__mbt28update__neuron__extended__if(_M0L1pS600, _M0L5paramS601, _M0L2dtS602);
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt28update__neuron__extended__if(
  struct _M0TP26RiantR8snn__mbt10ExtendedIF* _M0L1pS569,
  struct _M0TP26RiantR8snn__mbt19ExtendedIFParameter* _M0L5paramS578,
  float _M0L2dtS588
) {
  int32_t _M0L1nS568;
  struct _M0TPB5ArrayGfE* _M0L1vS570;
  struct _M0TPB5ArrayGfE* _M0L6g__excS571;
  struct _M0TPB5ArrayGfE* _M0L5g__pvS572;
  struct _M0TPB5ArrayGfE* _M0L6g__sstS573;
  struct _M0TPB5ArrayGfE* _M0L4tabsS574;
  struct _M0TPB5ArrayGbE* _M0L4fireS575;
  struct _M0TPB5ArrayGfE* _M0L1iS576;
  float _M0L2cmS577;
  float _M0L2vtS579;
  float _M0L2vrS580;
  float _M0L2elS581;
  float _M0L2glS582;
  float _M0L4e__iS583;
  float _M0L4e__eS584;
  float _M0L8tau__absS585;
  float _M0L5alphaS586;
  float _M0L6_2atmpS1389;
  float _M0L6_2atmpS1388;
  int32_t _M0L11tabs__stepsS587;
  struct _M0TPB8MutLocalGiE* _M0L1kS589;
  #line 157 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_extended_if.mbt"
  _M0L1nS568 = _M0L1pS569->$0;
  _M0L1vS570 = _M0L1pS569->$2;
  _M0L6g__excS571 = _M0L1pS569->$3;
  _M0L5g__pvS572 = _M0L1pS569->$4;
  _M0L6g__sstS573 = _M0L1pS569->$5;
  _M0L4tabsS574 = _M0L1pS569->$6;
  _M0L4fireS575 = _M0L1pS569->$7;
  _M0L1iS576 = _M0L1pS569->$8;
  _M0L2cmS577 = _M0L5paramS578->$0;
  _M0L2vtS579 = _M0L5paramS578->$1;
  _M0L2vrS580 = _M0L5paramS578->$2;
  _M0L2elS581 = _M0L5paramS578->$3;
  _M0L2glS582 = _M0L5paramS578->$4;
  _M0L4e__iS583 = _M0L5paramS578->$7;
  _M0L4e__eS584 = _M0L5paramS578->$8;
  _M0L8tau__absS585 = _M0L5paramS578->$9;
  _M0L5alphaS586 = _M0L5paramS578->$10;
  _M0L6_2atmpS1389 = _M0L8tau__absS585 / _M0L2dtS588;
  _M0L6_2atmpS1388 = _M0L6_2atmpS1389 + 0x1p-1f;
  #line 179 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_extended_if.mbt"
  _M0L11tabs__stepsS587 = _M0MPC15float5Float7to__int(_M0L6_2atmpS1388);
  _M0L1kS589
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1kS589)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1kS589->$0 = 0;
  while (1) {
    int32_t _M0L3valS1345 = _M0L1kS589->$0;
    if (_M0L3valS1345 < _M0L1nS568) {
      int32_t _M0L3valS1387 = _M0L1kS589->$0;
      float _M0L7tabs__kS590;
      int32_t _M0L3valS1386;
      float _M0L4v__kS592;
      int32_t _M0L3valS1385;
      float _M0L4g__eS593;
      int32_t _M0L3valS1384;
      float _M0L4g__pS594;
      int32_t _M0L3valS1383;
      float _M0L4g__sS595;
      int32_t _M0L3valS1382;
      float _M0L4i__kS596;
      float _M0L6_2atmpS1381;
      float _M0L6_2atmpS1378;
      float _M0L6_2atmpS1380;
      float _M0L6_2atmpS1379;
      float _M0L6_2atmpS1375;
      float _M0L6_2atmpS1377;
      float _M0L6_2atmpS1376;
      float _M0L6_2atmpS1372;
      float _M0L6_2atmpS1374;
      float _M0L6_2atmpS1373;
      float _M0L6_2atmpS1366;
      float _M0L6_2atmpS1371;
      float _M0L6_2atmpS1370;
      float _M0L6_2atmpS1368;
      float _M0L6_2atmpS1369;
      float _M0L6_2atmpS1367;
      float _M0L6_2atmpS1365;
      float _M0L6_2atmpS1364;
      float _M0L2dvS597;
      float _M0L6_2atmpS1363;
      float _M0L6v__newS598;
      int32_t _M0L1fS599;
      int32_t _M0L3valS1354;
      float _M0L6_2atmpS1355;
      int32_t _M0L6_2atmpS1353;
      int32_t _M0L3valS1357;
      int32_t _M0L6_2atmpS1356;
      int32_t _M0L3valS1359;
      float _M0L6_2atmpS1360;
      int32_t _M0L6_2atmpS1358;
      int32_t _M0L3valS1362;
      int32_t _M0L6_2atmpS1361;
      #line 182 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_extended_if.mbt"
      _M0L7tabs__kS590
      = _M0MPC15array5Array2atGfE(_M0L4tabsS574, _M0L3valS1387);
      if (_M0L7tabs__kS590 > 0x0p+0f) {
        int32_t _M0L3valS1347 = _M0L1kS589->$0;
        int32_t _M0L6_2atmpS1346;
        int32_t _M0L3valS1349;
        float _M0L6_2atmpS1350;
        int32_t _M0L6_2atmpS1348;
        int32_t _M0L3valS1352;
        int32_t _M0L6_2atmpS1351;
        #line 184 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_extended_if.mbt"
        _M0L6_2atmpS1346
        = _M0MPC15array5Array3setGbE(_M0L4fireS575, _M0L3valS1347, 0);
        _M0L3valS1349 = _M0L1kS589->$0;
        _M0L6_2atmpS1350 = _M0L7tabs__kS590 - 0x1p+0f;
        #line 185 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_extended_if.mbt"
        _M0L6_2atmpS1348
        = _M0MPC15array5Array3setGfE(_M0L4tabsS574, _M0L3valS1349, _M0L6_2atmpS1350);
        _M0L3valS1352 = _M0L1kS589->$0;
        _M0L6_2atmpS1351 = _M0L3valS1352 + 1;
        _M0L1kS589->$0 = _M0L6_2atmpS1351;
        continue;
      }
      _M0L3valS1386 = _M0L1kS589->$0;
      #line 189 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_extended_if.mbt"
      _M0L4v__kS592 = _M0MPC15array5Array2atGfE(_M0L1vS570, _M0L3valS1386);
      _M0L3valS1385 = _M0L1kS589->$0;
      #line 190 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_extended_if.mbt"
      _M0L4g__eS593
      = _M0MPC15array5Array2atGfE(_M0L6g__excS571, _M0L3valS1385);
      _M0L3valS1384 = _M0L1kS589->$0;
      #line 191 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_extended_if.mbt"
      _M0L4g__pS594
      = _M0MPC15array5Array2atGfE(_M0L5g__pvS572, _M0L3valS1384);
      _M0L3valS1383 = _M0L1kS589->$0;
      #line 192 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_extended_if.mbt"
      _M0L4g__sS595
      = _M0MPC15array5Array2atGfE(_M0L6g__sstS573, _M0L3valS1383);
      _M0L3valS1382 = _M0L1kS589->$0;
      #line 193 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_extended_if.mbt"
      _M0L4i__kS596 = _M0MPC15array5Array2atGfE(_M0L1iS576, _M0L3valS1382);
      _M0L6_2atmpS1381 = _M0L2elS581 - _M0L4v__kS592;
      _M0L6_2atmpS1378 = _M0L2glS582 * _M0L6_2atmpS1381;
      _M0L6_2atmpS1380 = _M0L4e__eS584 - _M0L4v__kS592;
      _M0L6_2atmpS1379 = _M0L4g__eS593 * _M0L6_2atmpS1380;
      _M0L6_2atmpS1375 = _M0L6_2atmpS1378 + _M0L6_2atmpS1379;
      _M0L6_2atmpS1377 = _M0L4e__iS583 - _M0L4v__kS592;
      _M0L6_2atmpS1376 = _M0L4g__pS594 * _M0L6_2atmpS1377;
      _M0L6_2atmpS1372 = _M0L6_2atmpS1375 + _M0L6_2atmpS1376;
      _M0L6_2atmpS1374 = _M0L4e__iS583 - _M0L4v__kS592;
      _M0L6_2atmpS1373 = _M0L4g__sS595 * _M0L6_2atmpS1374;
      _M0L6_2atmpS1366 = _M0L6_2atmpS1372 + _M0L6_2atmpS1373;
      _M0L6_2atmpS1371 = -_M0L5alphaS586;
      _M0L6_2atmpS1370 = _M0L6_2atmpS1371 * _M0L4g__eS593;
      _M0L6_2atmpS1368 = _M0L6_2atmpS1370 * _M0L4g__sS595;
      _M0L6_2atmpS1369 = _M0L4e__eS584 - _M0L4v__kS592;
      _M0L6_2atmpS1367 = _M0L6_2atmpS1368 * _M0L6_2atmpS1369;
      _M0L6_2atmpS1365 = _M0L6_2atmpS1366 + _M0L6_2atmpS1367;
      _M0L6_2atmpS1364 = _M0L6_2atmpS1365 + _M0L4i__kS596;
      _M0L2dvS597 = _M0L6_2atmpS1364 / _M0L2cmS577;
      _M0L6_2atmpS1363 = _M0L2dtS588 * _M0L2dvS597;
      _M0L6v__newS598 = _M0L4v__kS592 + _M0L6_2atmpS1363;
      _M0L1fS599 = _M0L6v__newS598 > _M0L2vtS579;
      _M0L3valS1354 = _M0L1kS589->$0;
      if (_M0L1fS599) {
        _M0L6_2atmpS1355 = _M0L2vrS580;
      } else {
        _M0L6_2atmpS1355 = _M0L6v__newS598;
      }
      #line 204 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_extended_if.mbt"
      _M0L6_2atmpS1353
      = _M0MPC15array5Array3setGfE(_M0L1vS570, _M0L3valS1354, _M0L6_2atmpS1355);
      _M0L3valS1357 = _M0L1kS589->$0;
      #line 205 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_extended_if.mbt"
      _M0L6_2atmpS1356
      = _M0MPC15array5Array3setGbE(_M0L4fireS575, _M0L3valS1357, _M0L1fS599);
      _M0L3valS1359 = _M0L1kS589->$0;
      if (_M0L1fS599) {
        _M0L6_2atmpS1360 = (float)_M0L11tabs__stepsS587;
      } else {
        _M0L6_2atmpS1360 = 0x0p+0f;
      }
      #line 206 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_extended_if.mbt"
      _M0L6_2atmpS1358
      = _M0MPC15array5Array3setGfE(_M0L4tabsS574, _M0L3valS1359, _M0L6_2atmpS1360);
      _M0L3valS1362 = _M0L1kS589->$0;
      _M0L6_2atmpS1361 = _M0L3valS1362 + 1;
      _M0L1kS589->$0 = _M0L6_2atmpS1361;
      continue;
    } else {
      moonbit_decref(_M0L1kS589);
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt30update__synapses__extended__if(
  struct _M0TP26RiantR8snn__mbt10ExtendedIF* _M0L1pS558,
  struct _M0TP26RiantR8snn__mbt19ExtendedIFParameter* _M0L5paramS563,
  float _M0L2dtS566
) {
  int32_t _M0L1nS557;
  struct _M0TPB5ArrayGfE* _M0L6g__excS559;
  struct _M0TPB5ArrayGfE* _M0L5g__pvS560;
  struct _M0TPB5ArrayGfE* _M0L6g__sstS561;
  float _M0L6tau__eS562;
  float _M0L6tau__iS564;
  struct _M0TPB8MutLocalGiE* _M0L1kS565;
  #line 126 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_extended_if.mbt"
  _M0L1nS557 = _M0L1pS558->$0;
  _M0L6g__excS559 = _M0L1pS558->$3;
  _M0L5g__pvS560 = _M0L1pS558->$4;
  _M0L6g__sstS561 = _M0L1pS558->$5;
  _M0L6tau__eS562 = _M0L5paramS563->$5;
  _M0L6tau__iS564 = _M0L5paramS563->$6;
  _M0L1kS565
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1kS565)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1kS565->$0 = 0;
  while (1) {
    int32_t _M0L3valS1312 = _M0L1kS565->$0;
    if (_M0L3valS1312 < _M0L1nS557) {
      int32_t _M0L3valS1314 = _M0L1kS565->$0;
      int32_t _M0L3valS1322 = _M0L1kS565->$0;
      float _M0L6_2atmpS1316;
      int32_t _M0L3valS1321;
      float _M0L6_2atmpS1320;
      float _M0L6_2atmpS1319;
      float _M0L6_2atmpS1318;
      float _M0L6_2atmpS1317;
      float _M0L6_2atmpS1315;
      int32_t _M0L6_2atmpS1313;
      int32_t _M0L3valS1324;
      int32_t _M0L3valS1332;
      float _M0L6_2atmpS1326;
      int32_t _M0L3valS1331;
      float _M0L6_2atmpS1330;
      float _M0L6_2atmpS1329;
      float _M0L6_2atmpS1328;
      float _M0L6_2atmpS1327;
      float _M0L6_2atmpS1325;
      int32_t _M0L6_2atmpS1323;
      int32_t _M0L3valS1334;
      int32_t _M0L3valS1342;
      float _M0L6_2atmpS1336;
      int32_t _M0L3valS1341;
      float _M0L6_2atmpS1340;
      float _M0L6_2atmpS1339;
      float _M0L6_2atmpS1338;
      float _M0L6_2atmpS1337;
      float _M0L6_2atmpS1335;
      int32_t _M0L6_2atmpS1333;
      int32_t _M0L3valS1344;
      int32_t _M0L6_2atmpS1343;
      #line 139 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_extended_if.mbt"
      _M0L6_2atmpS1316
      = _M0MPC15array5Array2atGfE(_M0L6g__excS559, _M0L3valS1322);
      _M0L3valS1321 = _M0L1kS565->$0;
      #line 139 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_extended_if.mbt"
      _M0L6_2atmpS1320
      = _M0MPC15array5Array2atGfE(_M0L6g__excS559, _M0L3valS1321);
      _M0L6_2atmpS1319 = -_M0L6_2atmpS1320;
      _M0L6_2atmpS1318 = _M0L6_2atmpS1319 / _M0L6tau__eS562;
      _M0L6_2atmpS1317 = _M0L2dtS566 * _M0L6_2atmpS1318;
      _M0L6_2atmpS1315 = _M0L6_2atmpS1316 + _M0L6_2atmpS1317;
      #line 139 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_extended_if.mbt"
      _M0L6_2atmpS1313
      = _M0MPC15array5Array3setGfE(_M0L6g__excS559, _M0L3valS1314, _M0L6_2atmpS1315);
      _M0L3valS1324 = _M0L1kS565->$0;
      _M0L3valS1332 = _M0L1kS565->$0;
      #line 140 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_extended_if.mbt"
      _M0L6_2atmpS1326
      = _M0MPC15array5Array2atGfE(_M0L5g__pvS560, _M0L3valS1332);
      _M0L3valS1331 = _M0L1kS565->$0;
      #line 140 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_extended_if.mbt"
      _M0L6_2atmpS1330
      = _M0MPC15array5Array2atGfE(_M0L5g__pvS560, _M0L3valS1331);
      _M0L6_2atmpS1329 = -_M0L6_2atmpS1330;
      _M0L6_2atmpS1328 = _M0L6_2atmpS1329 / _M0L6tau__iS564;
      _M0L6_2atmpS1327 = _M0L2dtS566 * _M0L6_2atmpS1328;
      _M0L6_2atmpS1325 = _M0L6_2atmpS1326 + _M0L6_2atmpS1327;
      #line 140 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_extended_if.mbt"
      _M0L6_2atmpS1323
      = _M0MPC15array5Array3setGfE(_M0L5g__pvS560, _M0L3valS1324, _M0L6_2atmpS1325);
      _M0L3valS1334 = _M0L1kS565->$0;
      _M0L3valS1342 = _M0L1kS565->$0;
      #line 141 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_extended_if.mbt"
      _M0L6_2atmpS1336
      = _M0MPC15array5Array2atGfE(_M0L6g__sstS561, _M0L3valS1342);
      _M0L3valS1341 = _M0L1kS565->$0;
      #line 141 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_extended_if.mbt"
      _M0L6_2atmpS1340
      = _M0MPC15array5Array2atGfE(_M0L6g__sstS561, _M0L3valS1341);
      _M0L6_2atmpS1339 = -_M0L6_2atmpS1340;
      _M0L6_2atmpS1338 = _M0L6_2atmpS1339 / _M0L6tau__iS564;
      _M0L6_2atmpS1337 = _M0L2dtS566 * _M0L6_2atmpS1338;
      _M0L6_2atmpS1335 = _M0L6_2atmpS1336 + _M0L6_2atmpS1337;
      #line 141 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_extended_if.mbt"
      _M0L6_2atmpS1333
      = _M0MPC15array5Array3setGfE(_M0L6g__sstS561, _M0L3valS1334, _M0L6_2atmpS1335);
      _M0L3valS1344 = _M0L1kS565->$0;
      _M0L6_2atmpS1343 = _M0L3valS1344 + 1;
      _M0L1kS565->$0 = _M0L6_2atmpS1343;
      continue;
    } else {
      moonbit_decref(_M0L1kS565);
    }
    break;
  }
  return 0;
}

struct _M0TP26RiantR8snn__mbt10ExtendedIF* _M0MP26RiantR8snn__mbt10ExtendedIF3new(
  int64_t _M0L7n_2eoptS552,
  struct _M0TP26RiantR8snn__mbt19ExtendedIFParameter* _M0L11param_2eoptS555
) {
  int32_t _M0L1nS551;
  struct _M0TP26RiantR8snn__mbt19ExtendedIFParameter* _M0L5paramS554;
  struct _M0TP26RiantR8snn__mbt10ExtendedIF* _result_1424;
  if (_M0L7n_2eoptS552 == 4294967296ll) {
    _M0L1nS551 = 100;
  } else {
    int64_t _M0L7_2aSomeS553 = _M0L7n_2eoptS552;
    _M0L1nS551 = (int32_t)_M0L7_2aSomeS553;
  }
  if (_M0L11param_2eoptS555 == 0) {
    #line 112 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_extended_if.mbt"
    _M0L5paramS554 = _M0MP26RiantR8snn__mbt19ExtendedIFParameter3new();
  } else {
    struct _M0TP26RiantR8snn__mbt19ExtendedIFParameter* _M0L7_2aSomeS556 =
      _M0L11param_2eoptS555;
    if (_M0L7_2aSomeS556) {
      moonbit_incref(_M0L7_2aSomeS556);
    }
    _M0L5paramS554 = _M0L7_2aSomeS556;
  }
  _result_1424
  = _M0MP26RiantR8snn__mbt10ExtendedIF11new_2einner(_M0L1nS551, _M0L5paramS554);
  moonbit_decref(_M0L5paramS554);
  return _result_1424;
}

struct _M0TP26RiantR8snn__mbt10ExtendedIF* _M0MP26RiantR8snn__mbt10ExtendedIF11new_2einner(
  int32_t _M0L1nS543,
  struct _M0TP26RiantR8snn__mbt19ExtendedIFParameter* _M0L5paramS544
) {
  float _M0L2vrS1311;
  struct _M0TPB5ArrayGfE* _M0L1vS542;
  struct _M0TPB5ArrayGfE* _M0L6g__excS545;
  struct _M0TPB5ArrayGfE* _M0L5g__pvS546;
  struct _M0TPB5ArrayGfE* _M0L6g__sstS547;
  struct _M0TPB5ArrayGfE* _M0L4tabsS548;
  struct _M0TPB5ArrayGbE* _M0L4fireS549;
  struct _M0TPB5ArrayGfE* _M0L1iS550;
  struct _M0TP26RiantR8snn__mbt10ExtendedIF* _block_1425;
  #line 110 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_extended_if.mbt"
  _M0L2vrS1311 = _M0L5paramS544->$2;
  #line 114 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_extended_if.mbt"
  _M0L1vS542 = _M0MPC15array5Array4makeGfE(_M0L1nS543, _M0L2vrS1311);
  #line 115 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_extended_if.mbt"
  _M0L6g__excS545 = _M0MPC15array5Array4makeGfE(_M0L1nS543, 0x0p+0f);
  #line 116 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_extended_if.mbt"
  _M0L5g__pvS546 = _M0MPC15array5Array4makeGfE(_M0L1nS543, 0x0p+0f);
  #line 117 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_extended_if.mbt"
  _M0L6g__sstS547 = _M0MPC15array5Array4makeGfE(_M0L1nS543, 0x0p+0f);
  #line 118 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_extended_if.mbt"
  _M0L4tabsS548 = _M0MPC15array5Array4makeGfE(_M0L1nS543, 0x0p+0f);
  #line 119 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_extended_if.mbt"
  _M0L4fireS549 = _M0MPC15array5Array4makeGbE(_M0L1nS543, 0);
  #line 120 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_extended_if.mbt"
  _M0L1iS550 = _M0MPC15array5Array4makeGfE(_M0L1nS543, 0x0p+0f);
  moonbit_incref(_M0L5paramS544);
  _block_1425
  = (struct _M0TP26RiantR8snn__mbt10ExtendedIF*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt10ExtendedIF));
  Moonbit_object_header(_block_1425)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 0, 0);
  _block_1425->$0 = _M0L1nS543;
  _block_1425->$1 = _M0L5paramS544;
  _block_1425->$2 = _M0L1vS542;
  _block_1425->$3 = _M0L6g__excS545;
  _block_1425->$4 = _M0L5g__pvS546;
  _block_1425->$5 = _M0L6g__sstS547;
  _block_1425->$6 = _M0L4tabsS548;
  _block_1425->$7 = _M0L4fireS549;
  _block_1425->$8 = _M0L1iS550;
  return _block_1425;
}

struct _M0TP26RiantR8snn__mbt19ExtendedIFParameter* _M0MP26RiantR8snn__mbt19ExtendedIFParameter6custom(
  float _M0L2cmS531,
  float _M0L2vtS532,
  float _M0L2vrS533,
  float _M0L2elS534,
  float _M0L2glS535,
  float _M0L6tau__eS536,
  float _M0L6tau__iS537,
  float _M0L4e__iS538,
  float _M0L4e__eS539,
  float _M0L8tau__absS540,
  float _M0L5alphaS541
) {
  struct _M0TP26RiantR8snn__mbt19ExtendedIFParameter* _block_1426;
  #line 67 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_extended_if.mbt"
  _block_1426
  = (struct _M0TP26RiantR8snn__mbt19ExtendedIFParameter*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt19ExtendedIFParameter));
  Moonbit_object_header(_block_1426)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_1426->$0 = _M0L2cmS531;
  _block_1426->$1 = _M0L2vtS532;
  _block_1426->$2 = _M0L2vrS533;
  _block_1426->$3 = _M0L2elS534;
  _block_1426->$4 = _M0L2glS535;
  _block_1426->$5 = _M0L6tau__eS536;
  _block_1426->$6 = _M0L6tau__iS537;
  _block_1426->$7 = _M0L4e__iS538;
  _block_1426->$8 = _M0L4e__eS539;
  _block_1426->$9 = _M0L8tau__absS540;
  _block_1426->$10 = _M0L5alphaS541;
  return _block_1426;
}

struct _M0TP26RiantR8snn__mbt19ExtendedIFParameter* _M0MP26RiantR8snn__mbt19ExtendedIFParameter3new(
  
) {
  struct _M0TP26RiantR8snn__mbt19ExtendedIFParameter* _block_1427;
  #line 57 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_extended_if.mbt"
  _block_1427
  = (struct _M0TP26RiantR8snn__mbt19ExtendedIFParameter*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt19ExtendedIFParameter));
  Moonbit_object_header(_block_1427)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_1427->$0 = 0x1.f4p+7f;
  _block_1427->$1 = -0x1.4p+5f;
  _block_1427->$2 = -0x1.04p+6f;
  _block_1427->$3 = -0x1.18p+6f;
  _block_1427->$4 = 0x1.4p+3f;
  _block_1427->$5 = 0x1.8p+2f;
  _block_1427->$6 = 0x1.4p+4f;
  _block_1427->$7 = -0x1.2cp+6f;
  _block_1427->$8 = 0x0p+0f;
  _block_1427->$9 = 0x1.4p+2f;
  _block_1427->$10 = 0x0p+0f;
  return _block_1427;
}

moonbit_string_t _M0IPC15float5FloatPB4Show10to__string(float _M0L4selfS530) {
  double _M0L6_2atmpS1310;
  #line 16 "C:\\Users\\31379\\.moon\\lib\\core\\float\\methods.mbt"
  _M0L6_2atmpS1310 = (double)_M0L4selfS530;
  #line 17 "C:\\Users\\31379\\.moon\\lib\\core\\float\\methods.mbt"
  return _M0MPC16double6Double10to__string(_M0L6_2atmpS1310);
}

int32_t _M0MPC15float5Float7to__int(float _M0L4selfS529) {
  #line 43 "C:\\Users\\31379\\.moon\\lib\\core\\float\\to_int.mbt"
  if (_M0L4selfS529 != _M0L4selfS529) {
    return 0;
  } else if (_M0L4selfS529 >= 0x1.fffffffcp+30f) {
    return 2147483647;
  } else if (_M0L4selfS529 <= -0x1p+31f) {
    return (int32_t)0x80000000;
  } else {
    return (int32_t)_M0L4selfS529;
  }
}

struct _M0TPB5ArrayGfE* _M0MPC15array5Array4makeGfE(
  int32_t _M0L3lenS520,
  float _M0L4elemS522
) {
  struct _M0TPB5ArrayGfE* _M0L3arrS519;
  int32_t _M0L1iS521;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3arrS519 = _M0MPC15array5Array20unsafe__make__uninitGfE(_M0L3lenS520);
  _M0L1iS521 = 0;
  while (1) {
    if (_M0L1iS521 < _M0L3lenS520) {
      float* _M0L3bufS1306 = _M0L3arrS519->$0;
      int32_t _M0L6_2atmpS1307;
      _M0L3bufS1306[_M0L1iS521] = _M0L4elemS522;
      _M0L6_2atmpS1307 = _M0L1iS521 + 1;
      _M0L1iS521 = _M0L6_2atmpS1307;
      continue;
    }
    break;
  }
  return _M0L3arrS519;
}

struct _M0TPB5ArrayGbE* _M0MPC15array5Array4makeGbE(
  int32_t _M0L3lenS525,
  int32_t _M0L4elemS527
) {
  struct _M0TPB5ArrayGbE* _M0L3arrS524;
  int32_t _M0L1iS526;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3arrS524 = _M0MPC15array5Array20unsafe__make__uninitGbE(_M0L3lenS525);
  _M0L1iS526 = 0;
  while (1) {
    if (_M0L1iS526 < _M0L3lenS525) {
      uint8_t* _M0L3bufS1308 = _M0L3arrS524->$0;
      int32_t _M0L6_2atmpS1309;
      _M0L3bufS1308[_M0L1iS526] = _M0L4elemS527;
      _M0L6_2atmpS1309 = _M0L1iS526 + 1;
      _M0L1iS526 = _M0L6_2atmpS1309;
      continue;
    }
    break;
  }
  return _M0L3arrS524;
}

int32_t _M0MPC15array5Array3setGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS512,
  int32_t _M0L5indexS513,
  float _M0L5valueS514
) {
  int32_t _M0L3lenS511;
  #line 262 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS511 = _M0L4selfS512->$1;
  if (_M0L5indexS513 >= 0 && _M0L5indexS513 < _M0L3lenS511) {
    float* _M0L6_2atmpS1304;
    #line 268 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS1304 = _M0MPC15array5Array6bufferGfE(_M0L4selfS512);
    _M0L6_2atmpS1304[_M0L5indexS513] = _M0L5valueS514;
    moonbit_decref(_M0L6_2atmpS1304);
  } else {
    #line 267 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
  return 0;
}

int32_t _M0MPC15array5Array3setGbE(
  struct _M0TPB5ArrayGbE* _M0L4selfS516,
  int32_t _M0L5indexS517,
  int32_t _M0L5valueS518
) {
  int32_t _M0L3lenS515;
  #line 262 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS515 = _M0L4selfS516->$1;
  if (_M0L5indexS517 >= 0 && _M0L5indexS517 < _M0L3lenS515) {
    uint8_t* _M0L6_2atmpS1305;
    #line 268 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS1305 = _M0MPC15array5Array6bufferGbE(_M0L4selfS516);
    _M0L6_2atmpS1305[_M0L5indexS517] = _M0L5valueS518;
    moonbit_decref(_M0L6_2atmpS1305);
  } else {
    #line 267 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
  return 0;
}

int32_t _M0MPC15array5Array2atGbE(
  struct _M0TPB5ArrayGbE* _M0L4selfS506,
  int32_t _M0L5indexS507
) {
  int32_t _M0L3lenS505;
  #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS505 = _M0L4selfS506->$1;
  if (_M0L5indexS507 >= 0 && _M0L5indexS507 < _M0L3lenS505) {
    uint8_t* _M0L6_2atmpS1302;
    int32_t _result_1430;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS1302 = _M0MPC15array5Array6bufferGbE(_M0L4selfS506);
    _result_1430 = (int32_t)_M0L6_2atmpS1302[_M0L5indexS507];
    moonbit_decref(_M0L6_2atmpS1302);
    return _result_1430;
  } else {
    #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
}

float _M0MPC15array5Array2atGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS509,
  int32_t _M0L5indexS510
) {
  int32_t _M0L3lenS508;
  #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS508 = _M0L4selfS509->$1;
  if (_M0L5indexS510 >= 0 && _M0L5indexS510 < _M0L3lenS508) {
    float* _M0L6_2atmpS1303;
    float _result_1431;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS1303 = _M0MPC15array5Array6bufferGfE(_M0L4selfS509);
    _result_1431 = (float)_M0L6_2atmpS1303[_M0L5indexS510];
    moonbit_decref(_M0L6_2atmpS1303);
    return _result_1431;
  } else {
    #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
}

int32_t _M0FPB7printlnGsE(moonbit_string_t _M0L5inputS504) {
  moonbit_string_t _M0L6_2atmpS1301;
  #line 36 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\console.mbt"
  #line 37 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\console.mbt"
  _M0L6_2atmpS1301 = _M0IPC16string6StringPB4Show10to__string(_M0L5inputS504);
  #line 37 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\console.mbt"
  moonbit_println(_M0L6_2atmpS1301);
  moonbit_decref(_M0L6_2atmpS1301);
  return 0;
}

moonbit_string_t _M0MPC16double6Double10to__string(double _M0L4selfS503) {
  #line 296 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double.mbt"
  #line 298 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double.mbt"
  return _M0FPB15ryu__to__string(_M0L4selfS503);
}

moonbit_string_t _M0FPB15ryu__to__string(double _M0L3valS488) {
  uint64_t _M0L4bitsS491;
  uint64_t _M0L6_2atmpS1300;
  uint64_t _M0L6_2atmpS1299;
  int32_t _M0L8ieeeSignS492;
  uint64_t _M0L12ieeeMantissaS493;
  uint64_t _M0L6_2atmpS1298;
  uint64_t _M0L6_2atmpS1297;
  int32_t _M0L12ieeeExponentS494;
  struct _M0TPB17FloatingDecimal64* _M0L7_2abindS495;
  struct _M0TPB17FloatingDecimal64* _M0L1vS496;
  moonbit_string_t _result_1433;
  #line 668 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  if (_M0L3valS488 == 0x0p+0) {
    return (moonbit_string_t)moonbit_string_literal_0.data;
  }
  if (_M0L3valS488 >= -0x1p+53 && _M0L3valS488 <= 0x1p+53) {
    if (_M0L3valS488 >= -0x1p+31 && _M0L3valS488 <= 0x1.fffffffcp+30) {
      int32_t _M0L1iS489;
      double _M0L6_2atmpS1286;
      #line 683 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0L1iS489 = _M0MPC16double6Double7to__int(_M0L3valS488);
      _M0L6_2atmpS1286 = (double)_M0L1iS489;
      if (_M0L6_2atmpS1286 == _M0L3valS488) {
        #line 685 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        return _M0MPC13int3Int18to__string_2einner(_M0L1iS489, 10);
      }
    } else {
      int64_t _M0L1iS490;
      double _M0L6_2atmpS1287;
      #line 688 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0L1iS490 = _M0MPC16double6Double9to__int64(_M0L3valS488);
      _M0L6_2atmpS1287 = (double)_M0L1iS490;
      if (_M0L6_2atmpS1287 == _M0L3valS488) {
        #line 690 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        return _M0MPC15int645Int6418to__string_2einner(_M0L1iS490, 10);
      }
    }
  }
  _M0L4bitsS491 = *(int64_t*)&_M0L3valS488;
  _M0L6_2atmpS1300 = _M0L4bitsS491 >> 63;
  _M0L6_2atmpS1299 = _M0L6_2atmpS1300 & 1ull;
  _M0L8ieeeSignS492 = _M0L6_2atmpS1299 != 0ull;
  _M0L12ieeeMantissaS493 = _M0L4bitsS491 & 4503599627370495ull;
  _M0L6_2atmpS1298 = _M0L4bitsS491 >> 52;
  _M0L6_2atmpS1297 = _M0L6_2atmpS1298 & 2047ull;
  _M0L12ieeeExponentS494 = (int32_t)_M0L6_2atmpS1297;
  if (
    _M0L12ieeeExponentS494 == 2047
    || _M0L12ieeeExponentS494 == 0 && _M0L12ieeeMantissaS493 == 0ull
  ) {
    int32_t _M0L6_2atmpS1288 = _M0L12ieeeExponentS494 != 0;
    int32_t _M0L6_2atmpS1289 = _M0L12ieeeMantissaS493 != 0ull;
    #line 707 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    return _M0FPB18copy__special__str(_M0L8ieeeSignS492, _M0L6_2atmpS1288, _M0L6_2atmpS1289);
  }
  #line 709 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS495
  = _M0FPB15d2d__small__int(_M0L12ieeeMantissaS493, _M0L12ieeeExponentS494);
  if (_M0L7_2abindS495 == 0) {
    uint32_t _M0L6_2atmpS1290;
    if (_M0L7_2abindS495) {
      moonbit_decref(_M0L7_2abindS495);
    }
    _M0L6_2atmpS1290 = *(uint32_t*)&_M0L12ieeeExponentS494;
    #line 719 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L1vS496 = _M0FPB3d2d(_M0L12ieeeMantissaS493, _M0L6_2atmpS1290);
  } else {
    struct _M0TPB17FloatingDecimal64* _M0L7_2aSomeS497 = _M0L7_2abindS495;
    struct _M0TPB17FloatingDecimal64* _M0L4_2afS498 = _M0L7_2aSomeS497;
    struct _M0TPB17FloatingDecimal64* _M0L1xS499 = _M0L4_2afS498;
    while (1) {
      uint64_t _M0L8mantissaS1296 = _M0L1xS499->$0;
      uint64_t _M0L1qS500 = _M0L8mantissaS1296 / 10ull;
      uint64_t _M0L8mantissaS1294 = _M0L1xS499->$0;
      uint64_t _M0L6_2atmpS1295 = 10ull * _M0L1qS500;
      uint64_t _M0L1rS501 = _M0L8mantissaS1294 - _M0L6_2atmpS1295;
      int32_t _M0L8exponentS1293;
      int32_t _M0L6_2atmpS1292;
      struct _M0TPB17FloatingDecimal64* _M0L6_2atmpS1291;
      if (_M0L1rS501 != 0ull) {
        _M0L1vS496 = _M0L1xS499;
        break;
      }
      _M0L8exponentS1293 = _M0L1xS499->$1;
      moonbit_decref(_M0L1xS499);
      _M0L6_2atmpS1292 = _M0L8exponentS1293 + 1;
      _M0L6_2atmpS1291
      = (struct _M0TPB17FloatingDecimal64*)moonbit_malloc(sizeof(struct _M0TPB17FloatingDecimal64));
      Moonbit_object_header(_M0L6_2atmpS1291)->meta
      = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
      _M0L6_2atmpS1291->$0 = _M0L1qS500;
      _M0L6_2atmpS1291->$1 = _M0L6_2atmpS1292;
      _M0L1xS499 = _M0L6_2atmpS1291;
      continue;
      break;
    }
  }
  #line 721 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _result_1433 = _M0FPB9to__chars(_M0L1vS496, _M0L8ieeeSignS492);
  moonbit_decref(_M0L1vS496);
  return _result_1433;
}

struct _M0TPB17FloatingDecimal64* _M0FPB15d2d__small__int(
  uint64_t _M0L12ieeeMantissaS483,
  int32_t _M0L12ieeeExponentS485
) {
  uint64_t _M0L2m2S482;
  int32_t _M0L6_2atmpS1285;
  int32_t _M0L2e2S484;
  int32_t _M0L6_2atmpS1284;
  uint64_t _M0L6_2atmpS1283;
  uint64_t _M0L4maskS486;
  uint64_t _M0L8fractionS487;
  int32_t _M0L6_2atmpS1282;
  uint64_t _M0L6_2atmpS1281;
  struct _M0TPB17FloatingDecimal64* _M0L6_2atmpS1280;
  #line 637 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L2m2S482 = 4503599627370496ull | _M0L12ieeeMantissaS483;
  _M0L6_2atmpS1285 = _M0L12ieeeExponentS485 - 1023;
  _M0L2e2S484 = _M0L6_2atmpS1285 - 52;
  if (_M0L2e2S484 > 0) {
    return 0;
  }
  if (_M0L2e2S484 < -52) {
    return 0;
  }
  _M0L6_2atmpS1284 = -_M0L2e2S484;
  _M0L6_2atmpS1283 = 1ull << (_M0L6_2atmpS1284 & 63);
  _M0L4maskS486 = _M0L6_2atmpS1283 - 1ull;
  _M0L8fractionS487 = _M0L2m2S482 & _M0L4maskS486;
  if (_M0L8fractionS487 != 0ull) {
    return 0;
  }
  _M0L6_2atmpS1282 = -_M0L2e2S484;
  _M0L6_2atmpS1281 = _M0L2m2S482 >> (_M0L6_2atmpS1282 & 63);
  _M0L6_2atmpS1280
  = (struct _M0TPB17FloatingDecimal64*)moonbit_malloc(sizeof(struct _M0TPB17FloatingDecimal64));
  Moonbit_object_header(_M0L6_2atmpS1280)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L6_2atmpS1280->$0 = _M0L6_2atmpS1281;
  _M0L6_2atmpS1280->$1 = 0;
  return _M0L6_2atmpS1280;
}

moonbit_string_t _M0FPB9to__chars(
  struct _M0TPB17FloatingDecimal64* _M0L1vS450,
  int32_t _M0L4signS448
) {
  moonbit_bytes_t _M0L6resultS446;
  int32_t _M0Lm5indexS447;
  uint64_t _M0L6outputS449;
  int32_t _M0L7olengthS451;
  int32_t _M0L8exponentS1279;
  int32_t _M0L6_2atmpS1278;
  int32_t _M0Lm3expS452;
  int32_t _M0L6_2atmpS1277;
  int32_t _M0L6_2atmpS1275;
  int32_t _M0L18scientificNotationS453;
  #line 530 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6resultS446 = (moonbit_bytes_t)moonbit_make_bytes(25, 0);
  _M0Lm5indexS447 = 0;
  if (_M0L4signS448) {
    int32_t _M0L6_2atmpS1149 = _M0Lm5indexS447;
    int32_t _M0L6_2atmpS1150;
    if (
      _M0L6_2atmpS1149 < 0
      || _M0L6_2atmpS1149 >= Moonbit_array_length(_M0L6resultS446)
    ) {
      #line 535 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      moonbit_panic();
    }
    _M0L6resultS446[_M0L6_2atmpS1149] = 45;
    _M0L6_2atmpS1150 = _M0Lm5indexS447;
    _M0Lm5indexS447 = _M0L6_2atmpS1150 + 1;
  }
  _M0L6outputS449 = _M0L1vS450->$0;
  #line 539 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7olengthS451 = _M0FPB17decimal__length17(_M0L6outputS449);
  _M0L8exponentS1279 = _M0L1vS450->$1;
  _M0L6_2atmpS1278 = _M0L8exponentS1279 + _M0L7olengthS451;
  _M0Lm3expS452 = _M0L6_2atmpS1278 - 1;
  _M0L6_2atmpS1277 = _M0Lm3expS452;
  if (_M0L6_2atmpS1277 >= -6) {
    int32_t _M0L6_2atmpS1276 = _M0Lm3expS452;
    _M0L6_2atmpS1275 = _M0L6_2atmpS1276 < 21;
  } else {
    _M0L6_2atmpS1275 = 0;
  }
  _M0L18scientificNotationS453 = !_M0L6_2atmpS1275;
  if (_M0L18scientificNotationS453) {
    int32_t _M0L7_2abindS454 = _M0L7olengthS451 - 1;
    uint64_t _M0L6outputS455;
    int32_t _M0L1iS456 = 0;
    uint64_t _M0L6outputS457 = _M0L6outputS449;
    int32_t _M0L6_2atmpS1151;
    int32_t _M0L6_2atmpS1155;
    int32_t _M0L6_2atmpS1154;
    int32_t _M0L6_2atmpS1153;
    int32_t _M0L6_2atmpS1152;
    int32_t _M0L6_2atmpS1159;
    int32_t _M0L6_2atmpS1160;
    int32_t _M0L6_2atmpS1161;
    int32_t _M0L6_2atmpS1162;
    int32_t _M0L6_2atmpS1163;
    int32_t _M0L6_2atmpS1169;
    int32_t _M0L6_2atmpS1202;
    moonbit_string_t _result_1435;
    while (1) {
      if (_M0L1iS456 < _M0L7_2abindS454) {
        uint64_t _M0L1cS458 = _M0L6outputS457 % 10ull;
        int32_t _M0L6_2atmpS1208 = _M0Lm5indexS447;
        int32_t _M0L6_2atmpS1207 = _M0L6_2atmpS1208 + _M0L7olengthS451;
        int32_t _M0L6_2atmpS1203 = _M0L6_2atmpS1207 - _M0L1iS456;
        int32_t _M0L6_2atmpS1206 = (int32_t)_M0L1cS458;
        int32_t _M0L6_2atmpS1205 = 48 + _M0L6_2atmpS1206;
        int32_t _M0L6_2atmpS1204 = _M0L6_2atmpS1205 & 0xff;
        int32_t _M0L6_2atmpS1209;
        uint64_t _M0L6_2atmpS1210;
        if (
          _M0L6_2atmpS1203 < 0
          || _M0L6_2atmpS1203 >= Moonbit_array_length(_M0L6resultS446)
        ) {
          #line 547 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
          moonbit_panic();
        }
        _M0L6resultS446[_M0L6_2atmpS1203] = _M0L6_2atmpS1204;
        _M0L6_2atmpS1209 = _M0L1iS456 + 1;
        _M0L6_2atmpS1210 = _M0L6outputS457 / 10ull;
        _M0L1iS456 = _M0L6_2atmpS1209;
        _M0L6outputS457 = _M0L6_2atmpS1210;
        continue;
      } else {
        _M0L6outputS455 = _M0L6outputS457;
      }
      break;
    }
    _M0L6_2atmpS1151 = _M0Lm5indexS447;
    _M0L6_2atmpS1155 = (int32_t)_M0L6outputS455;
    _M0L6_2atmpS1154 = _M0L6_2atmpS1155 % 10;
    _M0L6_2atmpS1153 = 48 + _M0L6_2atmpS1154;
    _M0L6_2atmpS1152 = _M0L6_2atmpS1153 & 0xff;
    if (
      _M0L6_2atmpS1151 < 0
      || _M0L6_2atmpS1151 >= Moonbit_array_length(_M0L6resultS446)
    ) {
      #line 552 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      moonbit_panic();
    }
    _M0L6resultS446[_M0L6_2atmpS1151] = _M0L6_2atmpS1152;
    if (_M0L7olengthS451 > 1) {
      int32_t _M0L6_2atmpS1157 = _M0Lm5indexS447;
      int32_t _M0L6_2atmpS1156 = _M0L6_2atmpS1157 + 1;
      if (
        _M0L6_2atmpS1156 < 0
        || _M0L6_2atmpS1156 >= Moonbit_array_length(_M0L6resultS446)
      ) {
        #line 554 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS446[_M0L6_2atmpS1156] = 46;
    } else {
      int32_t _M0L6_2atmpS1158 = _M0Lm5indexS447;
      _M0Lm5indexS447 = _M0L6_2atmpS1158 - 1;
    }
    _M0L6_2atmpS1159 = _M0Lm5indexS447;
    _M0L6_2atmpS1160 = _M0L7olengthS451 + 1;
    _M0Lm5indexS447 = _M0L6_2atmpS1159 + _M0L6_2atmpS1160;
    _M0L6_2atmpS1161 = _M0Lm5indexS447;
    if (
      _M0L6_2atmpS1161 < 0
      || _M0L6_2atmpS1161 >= Moonbit_array_length(_M0L6resultS446)
    ) {
      #line 562 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      moonbit_panic();
    }
    _M0L6resultS446[_M0L6_2atmpS1161] = 101;
    _M0L6_2atmpS1162 = _M0Lm5indexS447;
    _M0Lm5indexS447 = _M0L6_2atmpS1162 + 1;
    _M0L6_2atmpS1163 = _M0Lm3expS452;
    if (_M0L6_2atmpS1163 < 0) {
      int32_t _M0L6_2atmpS1164 = _M0Lm5indexS447;
      int32_t _M0L6_2atmpS1165;
      int32_t _M0L6_2atmpS1166;
      if (
        _M0L6_2atmpS1164 < 0
        || _M0L6_2atmpS1164 >= Moonbit_array_length(_M0L6resultS446)
      ) {
        #line 565 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS446[_M0L6_2atmpS1164] = 45;
      _M0L6_2atmpS1165 = _M0Lm5indexS447;
      _M0Lm5indexS447 = _M0L6_2atmpS1165 + 1;
      _M0L6_2atmpS1166 = _M0Lm3expS452;
      _M0Lm3expS452 = -_M0L6_2atmpS1166;
    } else {
      int32_t _M0L6_2atmpS1167 = _M0Lm5indexS447;
      int32_t _M0L6_2atmpS1168;
      if (
        _M0L6_2atmpS1167 < 0
        || _M0L6_2atmpS1167 >= Moonbit_array_length(_M0L6resultS446)
      ) {
        #line 569 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS446[_M0L6_2atmpS1167] = 43;
      _M0L6_2atmpS1168 = _M0Lm5indexS447;
      _M0Lm5indexS447 = _M0L6_2atmpS1168 + 1;
    }
    _M0L6_2atmpS1169 = _M0Lm3expS452;
    if (_M0L6_2atmpS1169 >= 100) {
      int32_t _M0L6_2atmpS1185 = _M0Lm3expS452;
      int32_t _M0L1aS460 = _M0L6_2atmpS1185 / 100;
      int32_t _M0L6_2atmpS1184 = _M0Lm3expS452;
      int32_t _M0L6_2atmpS1183 = _M0L6_2atmpS1184 / 10;
      int32_t _M0L1bS461 = _M0L6_2atmpS1183 % 10;
      int32_t _M0L6_2atmpS1182 = _M0Lm3expS452;
      int32_t _M0L1cS462 = _M0L6_2atmpS1182 % 10;
      int32_t _M0L6_2atmpS1170 = _M0Lm5indexS447;
      int32_t _M0L6_2atmpS1172 = 48 + _M0L1aS460;
      int32_t _M0L6_2atmpS1171 = _M0L6_2atmpS1172 & 0xff;
      int32_t _M0L6_2atmpS1176;
      int32_t _M0L6_2atmpS1173;
      int32_t _M0L6_2atmpS1175;
      int32_t _M0L6_2atmpS1174;
      int32_t _M0L6_2atmpS1180;
      int32_t _M0L6_2atmpS1177;
      int32_t _M0L6_2atmpS1179;
      int32_t _M0L6_2atmpS1178;
      int32_t _M0L6_2atmpS1181;
      if (
        _M0L6_2atmpS1170 < 0
        || _M0L6_2atmpS1170 >= Moonbit_array_length(_M0L6resultS446)
      ) {
        #line 576 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS446[_M0L6_2atmpS1170] = _M0L6_2atmpS1171;
      _M0L6_2atmpS1176 = _M0Lm5indexS447;
      _M0L6_2atmpS1173 = _M0L6_2atmpS1176 + 1;
      _M0L6_2atmpS1175 = 48 + _M0L1bS461;
      _M0L6_2atmpS1174 = _M0L6_2atmpS1175 & 0xff;
      if (
        _M0L6_2atmpS1173 < 0
        || _M0L6_2atmpS1173 >= Moonbit_array_length(_M0L6resultS446)
      ) {
        #line 577 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS446[_M0L6_2atmpS1173] = _M0L6_2atmpS1174;
      _M0L6_2atmpS1180 = _M0Lm5indexS447;
      _M0L6_2atmpS1177 = _M0L6_2atmpS1180 + 2;
      _M0L6_2atmpS1179 = 48 + _M0L1cS462;
      _M0L6_2atmpS1178 = _M0L6_2atmpS1179 & 0xff;
      if (
        _M0L6_2atmpS1177 < 0
        || _M0L6_2atmpS1177 >= Moonbit_array_length(_M0L6resultS446)
      ) {
        #line 578 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS446[_M0L6_2atmpS1177] = _M0L6_2atmpS1178;
      _M0L6_2atmpS1181 = _M0Lm5indexS447;
      _M0Lm5indexS447 = _M0L6_2atmpS1181 + 3;
    } else {
      int32_t _M0L6_2atmpS1186 = _M0Lm3expS452;
      if (_M0L6_2atmpS1186 >= 10) {
        int32_t _M0L6_2atmpS1196 = _M0Lm3expS452;
        int32_t _M0L1aS463 = _M0L6_2atmpS1196 / 10;
        int32_t _M0L6_2atmpS1195 = _M0Lm3expS452;
        int32_t _M0L1bS464 = _M0L6_2atmpS1195 % 10;
        int32_t _M0L6_2atmpS1187 = _M0Lm5indexS447;
        int32_t _M0L6_2atmpS1189 = 48 + _M0L1aS463;
        int32_t _M0L6_2atmpS1188 = _M0L6_2atmpS1189 & 0xff;
        int32_t _M0L6_2atmpS1193;
        int32_t _M0L6_2atmpS1190;
        int32_t _M0L6_2atmpS1192;
        int32_t _M0L6_2atmpS1191;
        int32_t _M0L6_2atmpS1194;
        if (
          _M0L6_2atmpS1187 < 0
          || _M0L6_2atmpS1187 >= Moonbit_array_length(_M0L6resultS446)
        ) {
          #line 583 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
          moonbit_panic();
        }
        _M0L6resultS446[_M0L6_2atmpS1187] = _M0L6_2atmpS1188;
        _M0L6_2atmpS1193 = _M0Lm5indexS447;
        _M0L6_2atmpS1190 = _M0L6_2atmpS1193 + 1;
        _M0L6_2atmpS1192 = 48 + _M0L1bS464;
        _M0L6_2atmpS1191 = _M0L6_2atmpS1192 & 0xff;
        if (
          _M0L6_2atmpS1190 < 0
          || _M0L6_2atmpS1190 >= Moonbit_array_length(_M0L6resultS446)
        ) {
          #line 584 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
          moonbit_panic();
        }
        _M0L6resultS446[_M0L6_2atmpS1190] = _M0L6_2atmpS1191;
        _M0L6_2atmpS1194 = _M0Lm5indexS447;
        _M0Lm5indexS447 = _M0L6_2atmpS1194 + 2;
      } else {
        int32_t _M0L6_2atmpS1197 = _M0Lm5indexS447;
        int32_t _M0L6_2atmpS1200 = _M0Lm3expS452;
        int32_t _M0L6_2atmpS1199 = 48 + _M0L6_2atmpS1200;
        int32_t _M0L6_2atmpS1198 = _M0L6_2atmpS1199 & 0xff;
        int32_t _M0L6_2atmpS1201;
        if (
          _M0L6_2atmpS1197 < 0
          || _M0L6_2atmpS1197 >= Moonbit_array_length(_M0L6resultS446)
        ) {
          #line 587 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
          moonbit_panic();
        }
        _M0L6resultS446[_M0L6_2atmpS1197] = _M0L6_2atmpS1198;
        _M0L6_2atmpS1201 = _M0Lm5indexS447;
        _M0Lm5indexS447 = _M0L6_2atmpS1201 + 1;
      }
    }
    _M0L6_2atmpS1202 = _M0Lm5indexS447;
    #line 590 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _result_1435
    = _M0FPB19string__from__bytes(_M0L6resultS446, 0, _M0L6_2atmpS1202);
    moonbit_decref(_M0L6resultS446);
    return _result_1435;
  } else {
    int32_t _M0L6_2atmpS1211 = _M0Lm3expS452;
    int32_t _M0L6_2atmpS1274;
    moonbit_string_t _result_1441;
    if (_M0L6_2atmpS1211 < 0) {
      int32_t _M0L6_2atmpS1212 = _M0Lm5indexS447;
      int32_t _M0L6_2atmpS1214;
      int32_t _M0L6_2atmpS1213;
      int32_t _M0L6_2atmpS1215;
      int32_t _M0L1iS465;
      int32_t _M0L6_2atmpS1230;
      int32_t _M0L6_2atmpS1232;
      int32_t _M0L6_2atmpS1231;
      int32_t _M0L7currentS467;
      int32_t _M0L1iS468;
      uint64_t _M0L6outputS469;
      if (
        _M0L6_2atmpS1212 < 0
        || _M0L6_2atmpS1212 >= Moonbit_array_length(_M0L6resultS446)
      ) {
        #line 595 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS446[_M0L6_2atmpS1212] = 48;
      _M0L6_2atmpS1214 = _M0Lm5indexS447;
      _M0L6_2atmpS1213 = _M0L6_2atmpS1214 + 1;
      if (
        _M0L6_2atmpS1213 < 0
        || _M0L6_2atmpS1213 >= Moonbit_array_length(_M0L6resultS446)
      ) {
        #line 596 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS446[_M0L6_2atmpS1213] = 46;
      _M0L6_2atmpS1215 = _M0Lm5indexS447;
      _M0Lm5indexS447 = _M0L6_2atmpS1215 + 2;
      _M0L1iS465 = -1;
      while (1) {
        int32_t _M0L6_2atmpS1216 = _M0Lm3expS452;
        if (_M0L1iS465 > _M0L6_2atmpS1216) {
          int32_t _M0L6_2atmpS1219 = _M0Lm5indexS447;
          int32_t _M0L6_2atmpS1218 = _M0L6_2atmpS1219 - _M0L1iS465;
          int32_t _M0L6_2atmpS1217 = _M0L6_2atmpS1218 - 1;
          int32_t _M0L6_2atmpS1220;
          if (
            _M0L6_2atmpS1217 < 0
            || _M0L6_2atmpS1217 >= Moonbit_array_length(_M0L6resultS446)
          ) {
            #line 599 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
            moonbit_panic();
          }
          _M0L6resultS446[_M0L6_2atmpS1217] = 48;
          _M0L6_2atmpS1220 = _M0L1iS465 - 1;
          _M0L1iS465 = _M0L6_2atmpS1220;
          continue;
        }
        break;
      }
      _M0L6_2atmpS1230 = _M0Lm5indexS447;
      _M0L6_2atmpS1232 = _M0Lm3expS452;
      _M0L6_2atmpS1231 = -1 - _M0L6_2atmpS1232;
      _M0L7currentS467 = _M0L6_2atmpS1230 + _M0L6_2atmpS1231;
      _M0L1iS468 = 0;
      _M0L6outputS469 = _M0L6outputS449;
      while (1) {
        if (_M0L1iS468 < _M0L7olengthS451) {
          int32_t _M0L6_2atmpS1227 = _M0L7currentS467 + _M0L7olengthS451;
          int32_t _M0L6_2atmpS1226 = _M0L6_2atmpS1227 - _M0L1iS468;
          int32_t _M0L6_2atmpS1221 = _M0L6_2atmpS1226 - 1;
          uint64_t _M0L6_2atmpS1225 = _M0L6outputS469 % 10ull;
          int32_t _M0L6_2atmpS1224 = (int32_t)_M0L6_2atmpS1225;
          int32_t _M0L6_2atmpS1223 = 48 + _M0L6_2atmpS1224;
          int32_t _M0L6_2atmpS1222 = _M0L6_2atmpS1223 & 0xff;
          int32_t _M0L6_2atmpS1228;
          uint64_t _M0L6_2atmpS1229;
          if (
            _M0L6_2atmpS1221 < 0
            || _M0L6_2atmpS1221 >= Moonbit_array_length(_M0L6resultS446)
          ) {
            #line 603 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
            moonbit_panic();
          }
          _M0L6resultS446[_M0L6_2atmpS1221] = _M0L6_2atmpS1222;
          _M0L6_2atmpS1228 = _M0L1iS468 + 1;
          _M0L6_2atmpS1229 = _M0L6outputS469 / 10ull;
          _M0L1iS468 = _M0L6_2atmpS1228;
          _M0L6outputS469 = _M0L6_2atmpS1229;
          continue;
        }
        break;
      }
      _M0Lm5indexS447 = _M0L7currentS467 + _M0L7olengthS451;
    } else {
      int32_t _M0L6_2atmpS1234 = _M0Lm3expS452;
      int32_t _M0L6_2atmpS1233 = _M0L6_2atmpS1234 + 1;
      if (_M0L6_2atmpS1233 >= _M0L7olengthS451) {
        int32_t _M0L1iS471 = 0;
        uint64_t _M0L6outputS472 = _M0L6outputS449;
        int32_t _M0L6_2atmpS1245;
        int32_t _M0L6_2atmpS1250;
        int32_t _M0L7_2abindS474;
        int32_t _M0L1iS475;
        int32_t _M0L6_2atmpS1251;
        int32_t _M0L6_2atmpS1254;
        int32_t _M0L6_2atmpS1253;
        int32_t _M0L6_2atmpS1252;
        while (1) {
          if (_M0L1iS471 < _M0L7olengthS451) {
            int32_t _M0L6_2atmpS1242 = _M0Lm5indexS447;
            int32_t _M0L6_2atmpS1241 = _M0L6_2atmpS1242 + _M0L7olengthS451;
            int32_t _M0L6_2atmpS1240 = _M0L6_2atmpS1241 - _M0L1iS471;
            int32_t _M0L6_2atmpS1235 = _M0L6_2atmpS1240 - 1;
            uint64_t _M0L6_2atmpS1239 = _M0L6outputS472 % 10ull;
            int32_t _M0L6_2atmpS1238 = (int32_t)_M0L6_2atmpS1239;
            int32_t _M0L6_2atmpS1237 = 48 + _M0L6_2atmpS1238;
            int32_t _M0L6_2atmpS1236 = _M0L6_2atmpS1237 & 0xff;
            int32_t _M0L6_2atmpS1243;
            uint64_t _M0L6_2atmpS1244;
            if (
              _M0L6_2atmpS1235 < 0
              || _M0L6_2atmpS1235 >= Moonbit_array_length(_M0L6resultS446)
            ) {
              #line 610 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
              moonbit_panic();
            }
            _M0L6resultS446[_M0L6_2atmpS1235] = _M0L6_2atmpS1236;
            _M0L6_2atmpS1243 = _M0L1iS471 + 1;
            _M0L6_2atmpS1244 = _M0L6outputS472 / 10ull;
            _M0L1iS471 = _M0L6_2atmpS1243;
            _M0L6outputS472 = _M0L6_2atmpS1244;
            continue;
          }
          break;
        }
        _M0L6_2atmpS1245 = _M0Lm5indexS447;
        _M0Lm5indexS447 = _M0L6_2atmpS1245 + _M0L7olengthS451;
        _M0L6_2atmpS1250 = _M0Lm3expS452;
        _M0L7_2abindS474 = _M0L6_2atmpS1250 + 1;
        _M0L1iS475 = _M0L7olengthS451;
        while (1) {
          if (_M0L1iS475 < _M0L7_2abindS474) {
            int32_t _M0L6_2atmpS1248 = _M0Lm5indexS447;
            int32_t _M0L6_2atmpS1247 = _M0L6_2atmpS1248 + _M0L1iS475;
            int32_t _M0L6_2atmpS1246 = _M0L6_2atmpS1247 - _M0L7olengthS451;
            int32_t _M0L6_2atmpS1249;
            if (
              _M0L6_2atmpS1246 < 0
              || _M0L6_2atmpS1246 >= Moonbit_array_length(_M0L6resultS446)
            ) {
              #line 615 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
              moonbit_panic();
            }
            _M0L6resultS446[_M0L6_2atmpS1246] = 48;
            _M0L6_2atmpS1249 = _M0L1iS475 + 1;
            _M0L1iS475 = _M0L6_2atmpS1249;
            continue;
          }
          break;
        }
        _M0L6_2atmpS1251 = _M0Lm5indexS447;
        _M0L6_2atmpS1254 = _M0Lm3expS452;
        _M0L6_2atmpS1253 = _M0L6_2atmpS1254 + 1;
        _M0L6_2atmpS1252 = _M0L6_2atmpS1253 - _M0L7olengthS451;
        _M0Lm5indexS447 = _M0L6_2atmpS1251 + _M0L6_2atmpS1252;
      } else {
        int32_t _M0L6_2atmpS1271 = _M0Lm5indexS447;
        int32_t _M0L6_2atmpS1270 = _M0L6_2atmpS1271 + 1;
        int32_t _M0L1iS477 = 0;
        int32_t _M0L7currentS478 = _M0L6_2atmpS1270;
        uint64_t _M0L6outputS479 = _M0L6outputS449;
        int32_t _M0L6_2atmpS1272;
        int32_t _M0L6_2atmpS1273;
        while (1) {
          if (_M0L1iS477 < _M0L7olengthS451) {
            int32_t _M0L6_2atmpS1266 = _M0L7olengthS451 - _M0L1iS477;
            int32_t _M0L6_2atmpS1264 = _M0L6_2atmpS1266 - 1;
            int32_t _M0L6_2atmpS1265 = _M0Lm3expS452;
            int32_t _M0L7currentS480;
            int32_t _M0L6_2atmpS1261;
            int32_t _M0L6_2atmpS1260;
            int32_t _M0L6_2atmpS1255;
            uint64_t _M0L6_2atmpS1259;
            int32_t _M0L6_2atmpS1258;
            int32_t _M0L6_2atmpS1257;
            int32_t _M0L6_2atmpS1256;
            int32_t _M0L6_2atmpS1262;
            uint64_t _M0L6_2atmpS1263;
            if (_M0L6_2atmpS1264 == _M0L6_2atmpS1265) {
              int32_t _M0L6_2atmpS1269 = _M0L7currentS478 + _M0L7olengthS451;
              int32_t _M0L6_2atmpS1268 = _M0L6_2atmpS1269 - _M0L1iS477;
              int32_t _M0L6_2atmpS1267 = _M0L6_2atmpS1268 - 1;
              if (
                _M0L6_2atmpS1267 < 0
                || _M0L6_2atmpS1267 >= Moonbit_array_length(_M0L6resultS446)
              ) {
                #line 622 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
                moonbit_panic();
              }
              _M0L6resultS446[_M0L6_2atmpS1267] = 46;
              _M0L7currentS480 = _M0L7currentS478 - 1;
            } else {
              _M0L7currentS480 = _M0L7currentS478;
            }
            _M0L6_2atmpS1261 = _M0L7currentS480 + _M0L7olengthS451;
            _M0L6_2atmpS1260 = _M0L6_2atmpS1261 - _M0L1iS477;
            _M0L6_2atmpS1255 = _M0L6_2atmpS1260 - 1;
            _M0L6_2atmpS1259 = _M0L6outputS479 % 10ull;
            _M0L6_2atmpS1258 = (int32_t)_M0L6_2atmpS1259;
            _M0L6_2atmpS1257 = 48 + _M0L6_2atmpS1258;
            _M0L6_2atmpS1256 = _M0L6_2atmpS1257 & 0xff;
            if (
              _M0L6_2atmpS1255 < 0
              || _M0L6_2atmpS1255 >= Moonbit_array_length(_M0L6resultS446)
            ) {
              #line 627 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
              moonbit_panic();
            }
            _M0L6resultS446[_M0L6_2atmpS1255] = _M0L6_2atmpS1256;
            _M0L6_2atmpS1262 = _M0L1iS477 + 1;
            _M0L6_2atmpS1263 = _M0L6outputS479 / 10ull;
            _M0L1iS477 = _M0L6_2atmpS1262;
            _M0L7currentS478 = _M0L7currentS480;
            _M0L6outputS479 = _M0L6_2atmpS1263;
            continue;
          }
          break;
        }
        _M0L6_2atmpS1272 = _M0Lm5indexS447;
        _M0L6_2atmpS1273 = _M0L7olengthS451 + 1;
        _M0Lm5indexS447 = _M0L6_2atmpS1272 + _M0L6_2atmpS1273;
      }
    }
    _M0L6_2atmpS1274 = _M0Lm5indexS447;
    #line 632 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _result_1441
    = _M0FPB19string__from__bytes(_M0L6resultS446, 0, _M0L6_2atmpS1274);
    moonbit_decref(_M0L6resultS446);
    return _result_1441;
  }
}

struct _M0TPB17FloatingDecimal64* _M0FPB3d2d(
  uint64_t _M0L12ieeeMantissaS392,
  uint32_t _M0L12ieeeExponentS391
) {
  int32_t _M0Lm2e2S389;
  uint64_t _M0Lm2m2S390;
  uint64_t _M0L6_2atmpS1148;
  uint64_t _M0L6_2atmpS1147;
  int32_t _M0L4evenS393;
  uint64_t _M0L6_2atmpS1146;
  uint64_t _M0L2mvS394;
  int32_t _M0L7mmShiftS395;
  uint64_t _M0Lm2vrS396;
  uint64_t _M0Lm2vpS397;
  uint64_t _M0Lm2vmS398;
  int32_t _M0Lm3e10S399;
  int32_t _M0Lm17vmIsTrailingZerosS400;
  int32_t _M0Lm17vrIsTrailingZerosS401;
  int32_t _M0L6_2atmpS1048;
  int32_t _M0Lm7removedS420;
  int32_t _M0Lm16lastRemovedDigitS421;
  uint64_t _M0Lm6outputS422;
  int32_t _M0L6_2atmpS1144;
  int32_t _M0L6_2atmpS1145;
  int32_t _M0L3expS445;
  uint64_t _M0L6_2atmpS1143;
  struct _M0TPB17FloatingDecimal64* _block_1447;
  #line 347 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0Lm2e2S389 = 0;
  _M0Lm2m2S390 = 0ull;
  if (_M0L12ieeeExponentS391 == 0u) {
    _M0Lm2e2S389 = -1076;
    _M0Lm2m2S390 = _M0L12ieeeMantissaS392;
  } else {
    int32_t _M0L6_2atmpS1047 = *(int32_t*)&_M0L12ieeeExponentS391;
    int32_t _M0L6_2atmpS1046 = _M0L6_2atmpS1047 - 1023;
    int32_t _M0L6_2atmpS1045 = _M0L6_2atmpS1046 - 52;
    _M0Lm2e2S389 = _M0L6_2atmpS1045 - 2;
    _M0Lm2m2S390 = 4503599627370496ull | _M0L12ieeeMantissaS392;
  }
  _M0L6_2atmpS1148 = _M0Lm2m2S390;
  _M0L6_2atmpS1147 = _M0L6_2atmpS1148 & 1ull;
  _M0L4evenS393 = _M0L6_2atmpS1147 == 0ull;
  _M0L6_2atmpS1146 = _M0Lm2m2S390;
  _M0L2mvS394 = 4ull * _M0L6_2atmpS1146;
  _M0L7mmShiftS395
  = _M0L12ieeeMantissaS392 != 0ull || _M0L12ieeeExponentS391 <= 1u;
  _M0Lm2vrS396 = 0ull;
  _M0Lm2vpS397 = 0ull;
  _M0Lm2vmS398 = 0ull;
  _M0Lm3e10S399 = 0;
  _M0Lm17vmIsTrailingZerosS400 = 0;
  _M0Lm17vrIsTrailingZerosS401 = 0;
  _M0L6_2atmpS1048 = _M0Lm2e2S389;
  if (_M0L6_2atmpS1048 >= 0) {
    int32_t _M0L6_2atmpS1070 = _M0Lm2e2S389;
    int32_t _M0L6_2atmpS1066;
    int32_t _M0L6_2atmpS1069;
    int32_t _M0L6_2atmpS1068;
    int32_t _M0L6_2atmpS1067;
    int32_t _M0L1qS402;
    int32_t _M0L6_2atmpS1065;
    int32_t _M0L6_2atmpS1064;
    int32_t _M0L1kS403;
    int32_t _M0L6_2atmpS1063;
    int32_t _M0L6_2atmpS1062;
    int32_t _M0L6_2atmpS1061;
    int32_t _M0L1iS404;
    struct _M0TPB8Pow5Pair _M0L4pow5S405;
    uint64_t _M0L6_2atmpS1060;
    struct _M0TPB19MulShiftAll64Result _M0L7_2abindS406;
    uint64_t _M0L8_2avrOutS407;
    uint64_t _M0L8_2avpOutS408;
    uint64_t _M0L8_2avmOutS409;
    #line 383 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1066 = _M0FPB9log10Pow2(_M0L6_2atmpS1070);
    _M0L6_2atmpS1069 = _M0Lm2e2S389;
    _M0L6_2atmpS1068 = _M0L6_2atmpS1069 > 3;
    #line 383 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1067 = _M0MPC14bool4Bool7to__int(_M0L6_2atmpS1068);
    _M0L1qS402 = _M0L6_2atmpS1066 - _M0L6_2atmpS1067;
    _M0Lm3e10S399 = _M0L1qS402;
    #line 385 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1065 = _M0FPB8pow5bits(_M0L1qS402);
    _M0L6_2atmpS1064 = 125 + _M0L6_2atmpS1065;
    _M0L1kS403 = _M0L6_2atmpS1064 - 1;
    _M0L6_2atmpS1063 = _M0Lm2e2S389;
    _M0L6_2atmpS1062 = -_M0L6_2atmpS1063;
    _M0L6_2atmpS1061 = _M0L6_2atmpS1062 + _M0L1qS402;
    _M0L1iS404 = _M0L6_2atmpS1061 + _M0L1kS403;
    #line 387 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L4pow5S405 = _M0FPB22double__computeInvPow5(_M0L1qS402);
    _M0L6_2atmpS1060 = _M0Lm2m2S390;
    #line 388 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L7_2abindS406
    = _M0FPB13mulShiftAll64(_M0L6_2atmpS1060, _M0L4pow5S405, _M0L1iS404, _M0L7mmShiftS395);
    _M0L8_2avrOutS407 = _M0L7_2abindS406.$0;
    _M0L8_2avpOutS408 = _M0L7_2abindS406.$1;
    _M0L8_2avmOutS409 = _M0L7_2abindS406.$2;
    _M0Lm2vrS396 = _M0L8_2avrOutS407;
    _M0Lm2vpS397 = _M0L8_2avpOutS408;
    _M0Lm2vmS398 = _M0L8_2avmOutS409;
    if (_M0L1qS402 <= 21) {
      int32_t _M0L6_2atmpS1056 = (int32_t)_M0L2mvS394;
      uint64_t _M0L6_2atmpS1059 = _M0L2mvS394 / 5ull;
      int32_t _M0L6_2atmpS1058 = (int32_t)_M0L6_2atmpS1059;
      int32_t _M0L6_2atmpS1057 = 5 * _M0L6_2atmpS1058;
      int32_t _M0L6mvMod5S410 = _M0L6_2atmpS1056 - _M0L6_2atmpS1057;
      if (_M0L6mvMod5S410 == 0) {
        #line 400 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0Lm17vrIsTrailingZerosS401
        = _M0FPB18multipleOfPowerOf5(_M0L2mvS394, _M0L1qS402);
      } else if (_M0L4evenS393) {
        uint64_t _M0L6_2atmpS1050 = _M0L2mvS394 - 1ull;
        uint64_t _M0L6_2atmpS1051;
        uint64_t _M0L6_2atmpS1049;
        #line 406 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0L6_2atmpS1051 = _M0MPC14bool4Bool10to__uint64(_M0L7mmShiftS395);
        _M0L6_2atmpS1049 = _M0L6_2atmpS1050 - _M0L6_2atmpS1051;
        #line 405 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0Lm17vmIsTrailingZerosS400
        = _M0FPB18multipleOfPowerOf5(_M0L6_2atmpS1049, _M0L1qS402);
      } else {
        uint64_t _M0L6_2atmpS1052 = _M0Lm2vpS397;
        uint64_t _M0L6_2atmpS1055 = _M0L2mvS394 + 2ull;
        int32_t _M0L6_2atmpS1054;
        uint64_t _M0L6_2atmpS1053;
        #line 410 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0L6_2atmpS1054
        = _M0FPB18multipleOfPowerOf5(_M0L6_2atmpS1055, _M0L1qS402);
        #line 410 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0L6_2atmpS1053 = _M0MPC14bool4Bool10to__uint64(_M0L6_2atmpS1054);
        _M0Lm2vpS397 = _M0L6_2atmpS1052 - _M0L6_2atmpS1053;
      }
    }
  } else {
    int32_t _M0L6_2atmpS1084 = _M0Lm2e2S389;
    int32_t _M0L6_2atmpS1083 = -_M0L6_2atmpS1084;
    int32_t _M0L6_2atmpS1078;
    int32_t _M0L6_2atmpS1082;
    int32_t _M0L6_2atmpS1081;
    int32_t _M0L6_2atmpS1080;
    int32_t _M0L6_2atmpS1079;
    int32_t _M0L1qS411;
    int32_t _M0L6_2atmpS1071;
    int32_t _M0L6_2atmpS1077;
    int32_t _M0L6_2atmpS1076;
    int32_t _M0L1iS412;
    int32_t _M0L6_2atmpS1075;
    int32_t _M0L1kS413;
    int32_t _M0L1jS414;
    struct _M0TPB8Pow5Pair _M0L4pow5S415;
    uint64_t _M0L6_2atmpS1074;
    struct _M0TPB19MulShiftAll64Result _M0L7_2abindS416;
    uint64_t _M0L8_2avrOutS417;
    uint64_t _M0L8_2avpOutS418;
    uint64_t _M0L8_2avmOutS419;
    #line 415 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1078 = _M0FPB9log10Pow5(_M0L6_2atmpS1083);
    _M0L6_2atmpS1082 = _M0Lm2e2S389;
    _M0L6_2atmpS1081 = -_M0L6_2atmpS1082;
    _M0L6_2atmpS1080 = _M0L6_2atmpS1081 > 1;
    #line 415 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1079 = _M0MPC14bool4Bool7to__int(_M0L6_2atmpS1080);
    _M0L1qS411 = _M0L6_2atmpS1078 - _M0L6_2atmpS1079;
    _M0L6_2atmpS1071 = _M0Lm2e2S389;
    _M0Lm3e10S399 = _M0L1qS411 + _M0L6_2atmpS1071;
    _M0L6_2atmpS1077 = _M0Lm2e2S389;
    _M0L6_2atmpS1076 = -_M0L6_2atmpS1077;
    _M0L1iS412 = _M0L6_2atmpS1076 - _M0L1qS411;
    #line 418 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1075 = _M0FPB8pow5bits(_M0L1iS412);
    _M0L1kS413 = _M0L6_2atmpS1075 - 125;
    _M0L1jS414 = _M0L1qS411 - _M0L1kS413;
    #line 420 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L4pow5S415 = _M0FPB19double__computePow5(_M0L1iS412);
    _M0L6_2atmpS1074 = _M0Lm2m2S390;
    #line 421 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L7_2abindS416
    = _M0FPB13mulShiftAll64(_M0L6_2atmpS1074, _M0L4pow5S415, _M0L1jS414, _M0L7mmShiftS395);
    _M0L8_2avrOutS417 = _M0L7_2abindS416.$0;
    _M0L8_2avpOutS418 = _M0L7_2abindS416.$1;
    _M0L8_2avmOutS419 = _M0L7_2abindS416.$2;
    _M0Lm2vrS396 = _M0L8_2avrOutS417;
    _M0Lm2vpS397 = _M0L8_2avpOutS418;
    _M0Lm2vmS398 = _M0L8_2avmOutS419;
    if (_M0L1qS411 <= 1) {
      _M0Lm17vrIsTrailingZerosS401 = 1;
      if (_M0L4evenS393) {
        int32_t _M0L6_2atmpS1072;
        #line 432 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0L6_2atmpS1072 = _M0MPC14bool4Bool7to__int(_M0L7mmShiftS395);
        _M0Lm17vmIsTrailingZerosS400 = _M0L6_2atmpS1072 == 1;
      } else {
        uint64_t _M0L6_2atmpS1073 = _M0Lm2vpS397;
        _M0Lm2vpS397 = _M0L6_2atmpS1073 - 1ull;
      }
    } else if (_M0L1qS411 < 63) {
      #line 437 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0Lm17vrIsTrailingZerosS401
      = _M0FPB18multipleOfPowerOf2(_M0L2mvS394, _M0L1qS411);
    }
  }
  _M0Lm7removedS420 = 0;
  _M0Lm16lastRemovedDigitS421 = 0;
  _M0Lm6outputS422 = 0ull;
  if (_M0Lm17vmIsTrailingZerosS400 || _M0Lm17vrIsTrailingZerosS401) {
    int32_t _if__result_1444;
    uint64_t _M0L6_2atmpS1114;
    uint64_t _M0L6_2atmpS1120;
    uint64_t _M0L6_2atmpS1121;
    int32_t _if__result_1445;
    int32_t _M0L6_2atmpS1117;
    int64_t _M0L6_2atmpS1116;
    uint64_t _M0L6_2atmpS1115;
    while (1) {
      uint64_t _M0L6_2atmpS1097 = _M0Lm2vpS397;
      uint64_t _M0L7vpDiv10S423 = _M0L6_2atmpS1097 / 10ull;
      uint64_t _M0L6_2atmpS1096 = _M0Lm2vmS398;
      uint64_t _M0L7vmDiv10S424 = _M0L6_2atmpS1096 / 10ull;
      uint64_t _M0L6_2atmpS1095;
      int32_t _M0L6_2atmpS1092;
      int32_t _M0L6_2atmpS1094;
      int32_t _M0L6_2atmpS1093;
      int32_t _M0L7vmMod10S426;
      uint64_t _M0L6_2atmpS1091;
      uint64_t _M0L7vrDiv10S427;
      uint64_t _M0L6_2atmpS1090;
      int32_t _M0L6_2atmpS1087;
      int32_t _M0L6_2atmpS1089;
      int32_t _M0L6_2atmpS1088;
      int32_t _M0L7vrMod10S428;
      int32_t _M0L6_2atmpS1086;
      if (_M0L7vpDiv10S423 <= _M0L7vmDiv10S424) {
        break;
      }
      _M0L6_2atmpS1095 = _M0Lm2vmS398;
      _M0L6_2atmpS1092 = (int32_t)_M0L6_2atmpS1095;
      _M0L6_2atmpS1094 = (int32_t)_M0L7vmDiv10S424;
      _M0L6_2atmpS1093 = 10 * _M0L6_2atmpS1094;
      _M0L7vmMod10S426 = _M0L6_2atmpS1092 - _M0L6_2atmpS1093;
      _M0L6_2atmpS1091 = _M0Lm2vrS396;
      _M0L7vrDiv10S427 = _M0L6_2atmpS1091 / 10ull;
      _M0L6_2atmpS1090 = _M0Lm2vrS396;
      _M0L6_2atmpS1087 = (int32_t)_M0L6_2atmpS1090;
      _M0L6_2atmpS1089 = (int32_t)_M0L7vrDiv10S427;
      _M0L6_2atmpS1088 = 10 * _M0L6_2atmpS1089;
      _M0L7vrMod10S428 = _M0L6_2atmpS1087 - _M0L6_2atmpS1088;
      _M0Lm17vmIsTrailingZerosS400
      = _M0Lm17vmIsTrailingZerosS400 && _M0L7vmMod10S426 == 0;
      if (_M0Lm17vrIsTrailingZerosS401) {
        int32_t _M0L6_2atmpS1085 = _M0Lm16lastRemovedDigitS421;
        _M0Lm17vrIsTrailingZerosS401 = _M0L6_2atmpS1085 == 0;
      } else {
        _M0Lm17vrIsTrailingZerosS401 = 0;
      }
      _M0Lm16lastRemovedDigitS421 = _M0L7vrMod10S428;
      _M0Lm2vrS396 = _M0L7vrDiv10S427;
      _M0Lm2vpS397 = _M0L7vpDiv10S423;
      _M0Lm2vmS398 = _M0L7vmDiv10S424;
      _M0L6_2atmpS1086 = _M0Lm7removedS420;
      _M0Lm7removedS420 = _M0L6_2atmpS1086 + 1;
      continue;
      break;
    }
    if (_M0Lm17vmIsTrailingZerosS400) {
      while (1) {
        uint64_t _M0L6_2atmpS1110 = _M0Lm2vmS398;
        uint64_t _M0L7vmDiv10S429 = _M0L6_2atmpS1110 / 10ull;
        uint64_t _M0L6_2atmpS1109 = _M0Lm2vmS398;
        int32_t _M0L6_2atmpS1106 = (int32_t)_M0L6_2atmpS1109;
        int32_t _M0L6_2atmpS1108 = (int32_t)_M0L7vmDiv10S429;
        int32_t _M0L6_2atmpS1107 = 10 * _M0L6_2atmpS1108;
        int32_t _M0L7vmMod10S430 = _M0L6_2atmpS1106 - _M0L6_2atmpS1107;
        uint64_t _M0L6_2atmpS1105;
        uint64_t _M0L7vpDiv10S432;
        uint64_t _M0L6_2atmpS1104;
        uint64_t _M0L7vrDiv10S433;
        uint64_t _M0L6_2atmpS1103;
        int32_t _M0L6_2atmpS1100;
        int32_t _M0L6_2atmpS1102;
        int32_t _M0L6_2atmpS1101;
        int32_t _M0L7vrMod10S434;
        int32_t _M0L6_2atmpS1099;
        if (_M0L7vmMod10S430 != 0) {
          break;
        }
        _M0L6_2atmpS1105 = _M0Lm2vpS397;
        _M0L7vpDiv10S432 = _M0L6_2atmpS1105 / 10ull;
        _M0L6_2atmpS1104 = _M0Lm2vrS396;
        _M0L7vrDiv10S433 = _M0L6_2atmpS1104 / 10ull;
        _M0L6_2atmpS1103 = _M0Lm2vrS396;
        _M0L6_2atmpS1100 = (int32_t)_M0L6_2atmpS1103;
        _M0L6_2atmpS1102 = (int32_t)_M0L7vrDiv10S433;
        _M0L6_2atmpS1101 = 10 * _M0L6_2atmpS1102;
        _M0L7vrMod10S434 = _M0L6_2atmpS1100 - _M0L6_2atmpS1101;
        if (_M0Lm17vrIsTrailingZerosS401) {
          int32_t _M0L6_2atmpS1098 = _M0Lm16lastRemovedDigitS421;
          _M0Lm17vrIsTrailingZerosS401 = _M0L6_2atmpS1098 == 0;
        } else {
          _M0Lm17vrIsTrailingZerosS401 = 0;
        }
        _M0Lm16lastRemovedDigitS421 = _M0L7vrMod10S434;
        _M0Lm2vrS396 = _M0L7vrDiv10S433;
        _M0Lm2vpS397 = _M0L7vpDiv10S432;
        _M0Lm2vmS398 = _M0L7vmDiv10S429;
        _M0L6_2atmpS1099 = _M0Lm7removedS420;
        _M0Lm7removedS420 = _M0L6_2atmpS1099 + 1;
        continue;
        break;
      }
    }
    if (_M0Lm17vrIsTrailingZerosS401) {
      int32_t _M0L6_2atmpS1113 = _M0Lm16lastRemovedDigitS421;
      if (_M0L6_2atmpS1113 == 5) {
        uint64_t _M0L6_2atmpS1112 = _M0Lm2vrS396;
        uint64_t _M0L6_2atmpS1111 = _M0L6_2atmpS1112 % 2ull;
        _if__result_1444 = _M0L6_2atmpS1111 == 0ull;
      } else {
        _if__result_1444 = 0;
      }
    } else {
      _if__result_1444 = 0;
    }
    if (_if__result_1444) {
      _M0Lm16lastRemovedDigitS421 = 4;
    }
    _M0L6_2atmpS1114 = _M0Lm2vrS396;
    _M0L6_2atmpS1120 = _M0Lm2vrS396;
    _M0L6_2atmpS1121 = _M0Lm2vmS398;
    if (_M0L6_2atmpS1120 == _M0L6_2atmpS1121) {
      if (!_M0L4evenS393) {
        _if__result_1445 = 1;
      } else {
        int32_t _M0L6_2atmpS1119 = _M0Lm17vmIsTrailingZerosS400;
        _if__result_1445 = !_M0L6_2atmpS1119;
      }
    } else {
      _if__result_1445 = 0;
    }
    if (_if__result_1445) {
      _M0L6_2atmpS1117 = 1;
    } else {
      int32_t _M0L6_2atmpS1118 = _M0Lm16lastRemovedDigitS421;
      _M0L6_2atmpS1117 = _M0L6_2atmpS1118 >= 5;
    }
    #line 487 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1116 = _M0MPC14bool4Bool9to__int64(_M0L6_2atmpS1117);
    _M0L6_2atmpS1115 = *(uint64_t*)&_M0L6_2atmpS1116;
    _M0Lm6outputS422 = _M0L6_2atmpS1114 + _M0L6_2atmpS1115;
  } else {
    int32_t _M0Lm7roundUpS435 = 0;
    uint64_t _M0L6_2atmpS1142 = _M0Lm2vpS397;
    uint64_t _M0L8vpDiv100S436 = _M0L6_2atmpS1142 / 100ull;
    uint64_t _M0L6_2atmpS1141 = _M0Lm2vmS398;
    uint64_t _M0L8vmDiv100S437 = _M0L6_2atmpS1141 / 100ull;
    uint64_t _M0L6_2atmpS1136;
    uint64_t _M0L6_2atmpS1139;
    uint64_t _M0L6_2atmpS1140;
    int32_t _M0L6_2atmpS1138;
    uint64_t _M0L6_2atmpS1137;
    if (_M0L8vpDiv100S436 > _M0L8vmDiv100S437) {
      uint64_t _M0L6_2atmpS1127 = _M0Lm2vrS396;
      uint64_t _M0L8vrDiv100S438 = _M0L6_2atmpS1127 / 100ull;
      uint64_t _M0L6_2atmpS1126 = _M0Lm2vrS396;
      int32_t _M0L6_2atmpS1123 = (int32_t)_M0L6_2atmpS1126;
      int32_t _M0L6_2atmpS1125 = (int32_t)_M0L8vrDiv100S438;
      int32_t _M0L6_2atmpS1124 = 100 * _M0L6_2atmpS1125;
      int32_t _M0L8vrMod100S439 = _M0L6_2atmpS1123 - _M0L6_2atmpS1124;
      int32_t _M0L6_2atmpS1122;
      _M0Lm7roundUpS435 = _M0L8vrMod100S439 >= 50;
      _M0Lm2vrS396 = _M0L8vrDiv100S438;
      _M0Lm2vpS397 = _M0L8vpDiv100S436;
      _M0Lm2vmS398 = _M0L8vmDiv100S437;
      _M0L6_2atmpS1122 = _M0Lm7removedS420;
      _M0Lm7removedS420 = _M0L6_2atmpS1122 + 2;
    }
    while (1) {
      uint64_t _M0L6_2atmpS1135 = _M0Lm2vpS397;
      uint64_t _M0L7vpDiv10S440 = _M0L6_2atmpS1135 / 10ull;
      uint64_t _M0L6_2atmpS1134 = _M0Lm2vmS398;
      uint64_t _M0L7vmDiv10S441 = _M0L6_2atmpS1134 / 10ull;
      uint64_t _M0L6_2atmpS1133;
      uint64_t _M0L7vrDiv10S443;
      uint64_t _M0L6_2atmpS1132;
      int32_t _M0L6_2atmpS1129;
      int32_t _M0L6_2atmpS1131;
      int32_t _M0L6_2atmpS1130;
      int32_t _M0L7vrMod10S444;
      int32_t _M0L6_2atmpS1128;
      if (_M0L7vpDiv10S440 <= _M0L7vmDiv10S441) {
        break;
      }
      _M0L6_2atmpS1133 = _M0Lm2vrS396;
      _M0L7vrDiv10S443 = _M0L6_2atmpS1133 / 10ull;
      _M0L6_2atmpS1132 = _M0Lm2vrS396;
      _M0L6_2atmpS1129 = (int32_t)_M0L6_2atmpS1132;
      _M0L6_2atmpS1131 = (int32_t)_M0L7vrDiv10S443;
      _M0L6_2atmpS1130 = 10 * _M0L6_2atmpS1131;
      _M0L7vrMod10S444 = _M0L6_2atmpS1129 - _M0L6_2atmpS1130;
      _M0Lm7roundUpS435 = _M0L7vrMod10S444 >= 5;
      _M0Lm2vrS396 = _M0L7vrDiv10S443;
      _M0Lm2vpS397 = _M0L7vpDiv10S440;
      _M0Lm2vmS398 = _M0L7vmDiv10S441;
      _M0L6_2atmpS1128 = _M0Lm7removedS420;
      _M0Lm7removedS420 = _M0L6_2atmpS1128 + 1;
      continue;
      break;
    }
    _M0L6_2atmpS1136 = _M0Lm2vrS396;
    _M0L6_2atmpS1139 = _M0Lm2vrS396;
    _M0L6_2atmpS1140 = _M0Lm2vmS398;
    _M0L6_2atmpS1138
    = _M0L6_2atmpS1139 == _M0L6_2atmpS1140 || _M0Lm7roundUpS435;
    #line 522 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1137 = _M0MPC14bool4Bool10to__uint64(_M0L6_2atmpS1138);
    _M0Lm6outputS422 = _M0L6_2atmpS1136 + _M0L6_2atmpS1137;
  }
  _M0L6_2atmpS1144 = _M0Lm3e10S399;
  _M0L6_2atmpS1145 = _M0Lm7removedS420;
  _M0L3expS445 = _M0L6_2atmpS1144 + _M0L6_2atmpS1145;
  _M0L6_2atmpS1143 = _M0Lm6outputS422;
  _block_1447
  = (struct _M0TPB17FloatingDecimal64*)moonbit_malloc(sizeof(struct _M0TPB17FloatingDecimal64));
  Moonbit_object_header(_block_1447)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_1447->$0 = _M0L6_2atmpS1143;
  _block_1447->$1 = _M0L3expS445;
  return _block_1447;
}

uint64_t _M0MPC14bool4Bool10to__uint64(int32_t _M0L4selfS388) {
  #line 110 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bool.mbt"
  if (_M0L4selfS388) {
    return 1ull;
  } else {
    return 0ull;
  }
}

int64_t _M0MPC14bool4Bool9to__int64(int32_t _M0L4selfS387) {
  #line 58 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bool.mbt"
  if (_M0L4selfS387) {
    return 1ll;
  } else {
    return 0ll;
  }
}

int32_t _M0MPC14bool4Bool7to__int(int32_t _M0L4selfS386) {
  #line 32 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bool.mbt"
  if (_M0L4selfS386) {
    return 1;
  } else {
    return 0;
  }
}

int32_t _M0FPB17decimal__length17(uint64_t _M0L1vS385) {
  #line 280 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  if (_M0L1vS385 >= 10000000000000000ull) {
    return 17;
  }
  if (_M0L1vS385 >= 1000000000000000ull) {
    return 16;
  }
  if (_M0L1vS385 >= 100000000000000ull) {
    return 15;
  }
  if (_M0L1vS385 >= 10000000000000ull) {
    return 14;
  }
  if (_M0L1vS385 >= 1000000000000ull) {
    return 13;
  }
  if (_M0L1vS385 >= 100000000000ull) {
    return 12;
  }
  if (_M0L1vS385 >= 10000000000ull) {
    return 11;
  }
  if (_M0L1vS385 >= 1000000000ull) {
    return 10;
  }
  if (_M0L1vS385 >= 100000000ull) {
    return 9;
  }
  if (_M0L1vS385 >= 10000000ull) {
    return 8;
  }
  if (_M0L1vS385 >= 1000000ull) {
    return 7;
  }
  if (_M0L1vS385 >= 100000ull) {
    return 6;
  }
  if (_M0L1vS385 >= 10000ull) {
    return 5;
  }
  if (_M0L1vS385 >= 1000ull) {
    return 4;
  }
  if (_M0L1vS385 >= 100ull) {
    return 3;
  }
  if (_M0L1vS385 >= 10ull) {
    return 2;
  }
  return 1;
}

struct _M0TPB8Pow5Pair _M0FPB22double__computeInvPow5(int32_t _M0L1iS368) {
  int32_t _M0L6_2atmpS1044;
  int32_t _M0L6_2atmpS1043;
  int32_t _M0L4baseS367;
  int32_t _M0L5base2S369;
  int32_t _M0L6offsetS370;
  int32_t _M0L6_2atmpS1042;
  uint64_t _M0L4mul0S371;
  int32_t _M0L6_2atmpS1041;
  int32_t _M0L6_2atmpS1040;
  uint64_t _M0L4mul1S372;
  uint64_t _M0L1mS373;
  struct _M0TPB7Umul128 _M0L7_2abindS374;
  uint64_t _M0L7_2alow1S375;
  uint64_t _M0L8_2ahigh1S376;
  struct _M0TPB7Umul128 _M0L7_2abindS377;
  uint64_t _M0L7_2alow0S378;
  uint64_t _M0L8_2ahigh0S379;
  uint64_t _M0L3sumS380;
  uint64_t _M0Lm5high1S381;
  int32_t _M0L6_2atmpS1038;
  int32_t _M0L6_2atmpS1039;
  int32_t _M0L5deltaS382;
  uint64_t _M0L6_2atmpS1037;
  uint64_t _M0L6_2atmpS1029;
  int32_t _M0L6_2atmpS1036;
  uint32_t _M0L6_2atmpS1033;
  int32_t _M0L6_2atmpS1035;
  int32_t _M0L6_2atmpS1034;
  uint32_t _M0L6_2atmpS1032;
  uint32_t _M0L6_2atmpS1031;
  uint64_t _M0L6_2atmpS1030;
  uint64_t _M0L1aS383;
  uint64_t _M0L6_2atmpS1028;
  uint64_t _M0L1bS384;
  #line 239 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1044 = _M0L1iS368 + 26;
  _M0L6_2atmpS1043 = _M0L6_2atmpS1044 - 1;
  _M0L4baseS367 = _M0L6_2atmpS1043 / 26;
  _M0L5base2S369 = _M0L4baseS367 * 26;
  _M0L6offsetS370 = _M0L5base2S369 - _M0L1iS368;
  _M0L6_2atmpS1042 = _M0L4baseS367 * 2;
  #line 243 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4mul0S371
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB26gDOUBLE__POW5__INV__SPLIT2, _M0L6_2atmpS1042);
  _M0L6_2atmpS1041 = _M0L4baseS367 * 2;
  _M0L6_2atmpS1040 = _M0L6_2atmpS1041 + 1;
  #line 244 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4mul1S372
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB26gDOUBLE__POW5__INV__SPLIT2, _M0L6_2atmpS1040);
  if (_M0L6offsetS370 == 0) {
    return (struct _M0TPB8Pow5Pair){.$0 = _M0L4mul0S371, .$1 = _M0L4mul1S372};
  }
  #line 248 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L1mS373
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB20gDOUBLE__POW5__TABLE, _M0L6offsetS370);
  #line 249 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS374 = _M0FPB7umul128(_M0L1mS373, _M0L4mul1S372);
  _M0L7_2alow1S375 = _M0L7_2abindS374.$0;
  _M0L8_2ahigh1S376 = _M0L7_2abindS374.$1;
  #line 250 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS377 = _M0FPB7umul128(_M0L1mS373, _M0L4mul0S371);
  _M0L7_2alow0S378 = _M0L7_2abindS377.$0;
  _M0L8_2ahigh0S379 = _M0L7_2abindS377.$1;
  _M0L3sumS380 = _M0L8_2ahigh0S379 + _M0L7_2alow1S375;
  _M0Lm5high1S381 = _M0L8_2ahigh1S376;
  if (_M0L3sumS380 < _M0L8_2ahigh0S379) {
    uint64_t _M0L6_2atmpS1027 = _M0Lm5high1S381;
    _M0Lm5high1S381 = _M0L6_2atmpS1027 + 1ull;
  }
  #line 256 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1038 = _M0FPB8pow5bits(_M0L5base2S369);
  #line 256 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1039 = _M0FPB8pow5bits(_M0L1iS368);
  _M0L5deltaS382 = _M0L6_2atmpS1038 - _M0L6_2atmpS1039;
  #line 257 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1037
  = _M0FPB13shiftright128(_M0L7_2alow0S378, _M0L3sumS380, _M0L5deltaS382);
  _M0L6_2atmpS1029 = _M0L6_2atmpS1037 + 1ull;
  _M0L6_2atmpS1036 = _M0L1iS368 / 16;
  #line 259 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1033
  = _M0MPC15array13ReadOnlyArray2atGjE(_M0FPB19gPOW5__INV__OFFSETS, _M0L6_2atmpS1036);
  _M0L6_2atmpS1035 = _M0L1iS368 % 16;
  _M0L6_2atmpS1034 = _M0L6_2atmpS1035 << 1;
  _M0L6_2atmpS1032 = _M0L6_2atmpS1033 >> (_M0L6_2atmpS1034 & 31);
  _M0L6_2atmpS1031 = _M0L6_2atmpS1032 & 3u;
  #line 259 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1030 = _M0MPC14uint4UInt10to__uint64(_M0L6_2atmpS1031);
  _M0L1aS383 = _M0L6_2atmpS1029 + _M0L6_2atmpS1030;
  _M0L6_2atmpS1028 = _M0Lm5high1S381;
  #line 260 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L1bS384
  = _M0FPB13shiftright128(_M0L3sumS380, _M0L6_2atmpS1028, _M0L5deltaS382);
  return (struct _M0TPB8Pow5Pair){.$0 = _M0L1aS383, .$1 = _M0L1bS384};
}

struct _M0TPB8Pow5Pair _M0FPB19double__computePow5(int32_t _M0L1iS350) {
  int32_t _M0L4baseS349;
  int32_t _M0L5base2S351;
  int32_t _M0L6offsetS352;
  int32_t _M0L6_2atmpS1026;
  uint64_t _M0L4mul0S353;
  int32_t _M0L6_2atmpS1025;
  int32_t _M0L6_2atmpS1024;
  uint64_t _M0L4mul1S354;
  uint64_t _M0L1mS355;
  struct _M0TPB7Umul128 _M0L7_2abindS356;
  uint64_t _M0L7_2alow1S357;
  uint64_t _M0L8_2ahigh1S358;
  struct _M0TPB7Umul128 _M0L7_2abindS359;
  uint64_t _M0L7_2alow0S360;
  uint64_t _M0L8_2ahigh0S361;
  uint64_t _M0L3sumS362;
  uint64_t _M0Lm5high1S363;
  int32_t _M0L6_2atmpS1022;
  int32_t _M0L6_2atmpS1023;
  int32_t _M0L5deltaS364;
  uint64_t _M0L6_2atmpS1014;
  int32_t _M0L6_2atmpS1021;
  uint32_t _M0L6_2atmpS1018;
  int32_t _M0L6_2atmpS1020;
  int32_t _M0L6_2atmpS1019;
  uint32_t _M0L6_2atmpS1017;
  uint32_t _M0L6_2atmpS1016;
  uint64_t _M0L6_2atmpS1015;
  uint64_t _M0L1aS365;
  uint64_t _M0L6_2atmpS1013;
  uint64_t _M0L1bS366;
  #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4baseS349 = _M0L1iS350 / 26;
  _M0L5base2S351 = _M0L4baseS349 * 26;
  _M0L6offsetS352 = _M0L1iS350 - _M0L5base2S351;
  _M0L6_2atmpS1026 = _M0L4baseS349 * 2;
  #line 217 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4mul0S353
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB21gDOUBLE__POW5__SPLIT2, _M0L6_2atmpS1026);
  _M0L6_2atmpS1025 = _M0L4baseS349 * 2;
  _M0L6_2atmpS1024 = _M0L6_2atmpS1025 + 1;
  #line 218 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4mul1S354
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB21gDOUBLE__POW5__SPLIT2, _M0L6_2atmpS1024);
  if (_M0L6offsetS352 == 0) {
    return (struct _M0TPB8Pow5Pair){.$0 = _M0L4mul0S353, .$1 = _M0L4mul1S354};
  }
  #line 222 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L1mS355
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB20gDOUBLE__POW5__TABLE, _M0L6offsetS352);
  #line 223 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS356 = _M0FPB7umul128(_M0L1mS355, _M0L4mul1S354);
  _M0L7_2alow1S357 = _M0L7_2abindS356.$0;
  _M0L8_2ahigh1S358 = _M0L7_2abindS356.$1;
  #line 224 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS359 = _M0FPB7umul128(_M0L1mS355, _M0L4mul0S353);
  _M0L7_2alow0S360 = _M0L7_2abindS359.$0;
  _M0L8_2ahigh0S361 = _M0L7_2abindS359.$1;
  _M0L3sumS362 = _M0L8_2ahigh0S361 + _M0L7_2alow1S357;
  _M0Lm5high1S363 = _M0L8_2ahigh1S358;
  if (_M0L3sumS362 < _M0L8_2ahigh0S361) {
    uint64_t _M0L6_2atmpS1012 = _M0Lm5high1S363;
    _M0Lm5high1S363 = _M0L6_2atmpS1012 + 1ull;
  }
  #line 230 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1022 = _M0FPB8pow5bits(_M0L1iS350);
  #line 230 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1023 = _M0FPB8pow5bits(_M0L5base2S351);
  _M0L5deltaS364 = _M0L6_2atmpS1022 - _M0L6_2atmpS1023;
  #line 231 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1014
  = _M0FPB13shiftright128(_M0L7_2alow0S360, _M0L3sumS362, _M0L5deltaS364);
  _M0L6_2atmpS1021 = _M0L1iS350 / 16;
  #line 232 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1018
  = _M0MPC15array13ReadOnlyArray2atGjE(_M0FPB14gPOW5__OFFSETS, _M0L6_2atmpS1021);
  _M0L6_2atmpS1020 = _M0L1iS350 % 16;
  _M0L6_2atmpS1019 = _M0L6_2atmpS1020 << 1;
  _M0L6_2atmpS1017 = _M0L6_2atmpS1018 >> (_M0L6_2atmpS1019 & 31);
  _M0L6_2atmpS1016 = _M0L6_2atmpS1017 & 3u;
  #line 232 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1015 = _M0MPC14uint4UInt10to__uint64(_M0L6_2atmpS1016);
  _M0L1aS365 = _M0L6_2atmpS1014 + _M0L6_2atmpS1015;
  _M0L6_2atmpS1013 = _M0Lm5high1S363;
  #line 233 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L1bS366
  = _M0FPB13shiftright128(_M0L3sumS362, _M0L6_2atmpS1013, _M0L5deltaS364);
  return (struct _M0TPB8Pow5Pair){.$0 = _M0L1aS365, .$1 = _M0L1bS366};
}

struct _M0TPB19MulShiftAll64Result _M0FPB13mulShiftAll64(
  uint64_t _M0L1mS323,
  struct _M0TPB8Pow5Pair _M0L3mulS320,
  int32_t _M0L1jS336,
  int32_t _M0L7mmShiftS338
) {
  uint64_t _M0L7_2amul0S319;
  uint64_t _M0L7_2amul1S321;
  uint64_t _M0L1mS322;
  struct _M0TPB7Umul128 _M0L7_2abindS324;
  uint64_t _M0L5_2aloS325;
  uint64_t _M0L6_2atmpS326;
  struct _M0TPB7Umul128 _M0L7_2abindS327;
  uint64_t _M0L6_2alo2S328;
  uint64_t _M0L6_2ahi2S329;
  uint64_t _M0L3midS330;
  uint64_t _M0L6_2atmpS1011;
  uint64_t _M0L2hiS331;
  uint64_t _M0L3lo2S332;
  uint64_t _M0L6_2atmpS1009;
  uint64_t _M0L6_2atmpS1010;
  uint64_t _M0L4mid2S333;
  uint64_t _M0L6_2atmpS1008;
  uint64_t _M0L3hi2S334;
  int32_t _M0L6_2atmpS1007;
  int32_t _M0L6_2atmpS1006;
  uint64_t _M0L2vpS335;
  uint64_t _M0Lm2vmS337;
  int32_t _M0L6_2atmpS1005;
  int32_t _M0L6_2atmpS1004;
  uint64_t _M0L2vrS348;
  uint64_t _M0L6_2atmpS1003;
  #line 129 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2amul0S319 = _M0L3mulS320.$0;
  _M0L7_2amul1S321 = _M0L3mulS320.$1;
  _M0L1mS322 = _M0L1mS323 << 1;
  #line 137 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS324 = _M0FPB7umul128(_M0L1mS322, _M0L7_2amul0S319);
  _M0L5_2aloS325 = _M0L7_2abindS324.$0;
  _M0L6_2atmpS326 = _M0L7_2abindS324.$1;
  #line 138 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS327 = _M0FPB7umul128(_M0L1mS322, _M0L7_2amul1S321);
  _M0L6_2alo2S328 = _M0L7_2abindS327.$0;
  _M0L6_2ahi2S329 = _M0L7_2abindS327.$1;
  _M0L3midS330 = _M0L6_2atmpS326 + _M0L6_2alo2S328;
  if (_M0L3midS330 < _M0L6_2atmpS326) {
    _M0L6_2atmpS1011 = 1ull;
  } else {
    _M0L6_2atmpS1011 = 0ull;
  }
  _M0L2hiS331 = _M0L6_2ahi2S329 + _M0L6_2atmpS1011;
  _M0L3lo2S332 = _M0L5_2aloS325 + _M0L7_2amul0S319;
  _M0L6_2atmpS1009 = _M0L3midS330 + _M0L7_2amul1S321;
  if (_M0L3lo2S332 < _M0L5_2aloS325) {
    _M0L6_2atmpS1010 = 1ull;
  } else {
    _M0L6_2atmpS1010 = 0ull;
  }
  _M0L4mid2S333 = _M0L6_2atmpS1009 + _M0L6_2atmpS1010;
  if (_M0L4mid2S333 < _M0L3midS330) {
    _M0L6_2atmpS1008 = 1ull;
  } else {
    _M0L6_2atmpS1008 = 0ull;
  }
  _M0L3hi2S334 = _M0L2hiS331 + _M0L6_2atmpS1008;
  _M0L6_2atmpS1007 = _M0L1jS336 - 64;
  _M0L6_2atmpS1006 = _M0L6_2atmpS1007 - 1;
  #line 144 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L2vpS335
  = _M0FPB13shiftright128(_M0L4mid2S333, _M0L3hi2S334, _M0L6_2atmpS1006);
  _M0Lm2vmS337 = 0ull;
  if (_M0L7mmShiftS338) {
    uint64_t _M0L3lo3S339 = _M0L5_2aloS325 - _M0L7_2amul0S319;
    uint64_t _M0L6_2atmpS993 = _M0L3midS330 - _M0L7_2amul1S321;
    uint64_t _M0L6_2atmpS994;
    uint64_t _M0L4mid3S340;
    uint64_t _M0L6_2atmpS992;
    uint64_t _M0L3hi3S341;
    int32_t _M0L6_2atmpS991;
    int32_t _M0L6_2atmpS990;
    if (_M0L5_2aloS325 < _M0L3lo3S339) {
      _M0L6_2atmpS994 = 1ull;
    } else {
      _M0L6_2atmpS994 = 0ull;
    }
    _M0L4mid3S340 = _M0L6_2atmpS993 - _M0L6_2atmpS994;
    if (_M0L3midS330 < _M0L4mid3S340) {
      _M0L6_2atmpS992 = 1ull;
    } else {
      _M0L6_2atmpS992 = 0ull;
    }
    _M0L3hi3S341 = _M0L2hiS331 - _M0L6_2atmpS992;
    _M0L6_2atmpS991 = _M0L1jS336 - 64;
    _M0L6_2atmpS990 = _M0L6_2atmpS991 - 1;
    #line 150 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0Lm2vmS337
    = _M0FPB13shiftright128(_M0L4mid3S340, _M0L3hi3S341, _M0L6_2atmpS990);
  } else {
    uint64_t _M0L3lo3S342 = _M0L5_2aloS325 + _M0L5_2aloS325;
    uint64_t _M0L6_2atmpS1001 = _M0L3midS330 + _M0L3midS330;
    uint64_t _M0L6_2atmpS1002;
    uint64_t _M0L4mid3S343;
    uint64_t _M0L6_2atmpS999;
    uint64_t _M0L6_2atmpS1000;
    uint64_t _M0L3hi3S344;
    uint64_t _M0L3lo4S345;
    uint64_t _M0L6_2atmpS997;
    uint64_t _M0L6_2atmpS998;
    uint64_t _M0L4mid4S346;
    uint64_t _M0L6_2atmpS996;
    uint64_t _M0L3hi4S347;
    int32_t _M0L6_2atmpS995;
    if (_M0L3lo3S342 < _M0L5_2aloS325) {
      _M0L6_2atmpS1002 = 1ull;
    } else {
      _M0L6_2atmpS1002 = 0ull;
    }
    _M0L4mid3S343 = _M0L6_2atmpS1001 + _M0L6_2atmpS1002;
    _M0L6_2atmpS999 = _M0L2hiS331 + _M0L2hiS331;
    if (_M0L4mid3S343 < _M0L3midS330) {
      _M0L6_2atmpS1000 = 1ull;
    } else {
      _M0L6_2atmpS1000 = 0ull;
    }
    _M0L3hi3S344 = _M0L6_2atmpS999 + _M0L6_2atmpS1000;
    _M0L3lo4S345 = _M0L3lo3S342 - _M0L7_2amul0S319;
    _M0L6_2atmpS997 = _M0L4mid3S343 - _M0L7_2amul1S321;
    if (_M0L3lo3S342 < _M0L3lo4S345) {
      _M0L6_2atmpS998 = 1ull;
    } else {
      _M0L6_2atmpS998 = 0ull;
    }
    _M0L4mid4S346 = _M0L6_2atmpS997 - _M0L6_2atmpS998;
    if (_M0L4mid3S343 < _M0L4mid4S346) {
      _M0L6_2atmpS996 = 1ull;
    } else {
      _M0L6_2atmpS996 = 0ull;
    }
    _M0L3hi4S347 = _M0L3hi3S344 - _M0L6_2atmpS996;
    _M0L6_2atmpS995 = _M0L1jS336 - 64;
    #line 158 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0Lm2vmS337
    = _M0FPB13shiftright128(_M0L4mid4S346, _M0L3hi4S347, _M0L6_2atmpS995);
  }
  _M0L6_2atmpS1005 = _M0L1jS336 - 64;
  _M0L6_2atmpS1004 = _M0L6_2atmpS1005 - 1;
  #line 160 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L2vrS348
  = _M0FPB13shiftright128(_M0L3midS330, _M0L2hiS331, _M0L6_2atmpS1004);
  _M0L6_2atmpS1003 = _M0Lm2vmS337;
  return (struct _M0TPB19MulShiftAll64Result){.$0 = _M0L2vrS348,
                                                .$1 = _M0L2vpS335,
                                                .$2 = _M0L6_2atmpS1003};
}

int32_t _M0FPB18multipleOfPowerOf2(
  uint64_t _M0L5valueS317,
  int32_t _M0L1pS318
) {
  uint64_t _M0L6_2atmpS989;
  uint64_t _M0L6_2atmpS988;
  uint64_t _M0L6_2atmpS987;
  #line 124 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS989 = 1ull << (_M0L1pS318 & 63);
  _M0L6_2atmpS988 = _M0L6_2atmpS989 - 1ull;
  _M0L6_2atmpS987 = _M0L5valueS317 & _M0L6_2atmpS988;
  return _M0L6_2atmpS987 == 0ull;
}

int32_t _M0FPB18multipleOfPowerOf5(
  uint64_t _M0L5valueS315,
  int32_t _M0L1pS316
) {
  int32_t _M0L6_2atmpS986;
  #line 119 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  #line 120 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS986 = _M0FPB10pow5Factor(_M0L5valueS315);
  return _M0L6_2atmpS986 >= _M0L1pS316;
}

int32_t _M0FPB10pow5Factor(uint64_t _M0L5valueS310) {
  uint64_t _M0L6_2atmpS977;
  uint64_t _M0L6_2atmpS978;
  uint64_t _M0L6_2atmpS979;
  uint64_t _M0L6_2atmpS980;
  uint64_t _M0L6_2atmpS985;
  int32_t _M0L5countS311;
  uint64_t _M0L1vS312;
  #line 94 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS977 = _M0L5valueS310 % 5ull;
  if (_M0L6_2atmpS977 != 0ull) {
    return 0;
  }
  _M0L6_2atmpS978 = _M0L5valueS310 % 25ull;
  if (_M0L6_2atmpS978 != 0ull) {
    return 1;
  }
  _M0L6_2atmpS979 = _M0L5valueS310 % 125ull;
  if (_M0L6_2atmpS979 != 0ull) {
    return 2;
  }
  _M0L6_2atmpS980 = _M0L5valueS310 % 625ull;
  if (_M0L6_2atmpS980 != 0ull) {
    return 3;
  }
  _M0L6_2atmpS985 = _M0L5valueS310 / 625ull;
  _M0L5countS311 = 4;
  _M0L1vS312 = _M0L6_2atmpS985;
  while (1) {
    if (_M0L1vS312 > 0ull) {
      uint64_t _M0L6_2atmpS981 = _M0L1vS312 % 5ull;
      int32_t _M0L6_2atmpS982;
      uint64_t _M0L6_2atmpS983;
      if (_M0L6_2atmpS981 != 0ull) {
        return _M0L5countS311;
      }
      _M0L6_2atmpS982 = _M0L5countS311 + 1;
      _M0L6_2atmpS983 = _M0L1vS312 / 5ull;
      _M0L5countS311 = _M0L6_2atmpS982;
      _M0L1vS312 = _M0L6_2atmpS983;
      continue;
    } else {
      struct _M0TPB13StringBuilder* _M0L18_2astring__builderS314;
      moonbit_string_t _M0L6_2atmpS984;
      int32_t _result_1449;
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0L18_2astring__builderS314
      = _M0MPB13StringBuilder21StringBuilder_2einner(25);
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS314, (moonbit_string_t)moonbit_string_literal_1.data);
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0MPB13StringBuilder13write__objectGmE(_M0L18_2astring__builderS314, _M0L5valueS310);
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0L6_2atmpS984
      = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS314);
      moonbit_decref(_M0L18_2astring__builderS314);
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _result_1449 = _M0FPC15abort5abortGiE(_M0L6_2atmpS984);
      moonbit_decref(_M0L6_2atmpS984);
      return _result_1449;
    }
    break;
  }
}

uint64_t _M0FPB13shiftright128(
  uint64_t _M0L2loS309,
  uint64_t _M0L2hiS307,
  int32_t _M0L4distS308
) {
  int32_t _M0L6_2atmpS976;
  uint64_t _M0L6_2atmpS974;
  uint64_t _M0L6_2atmpS975;
  #line 89 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS976 = 64 - _M0L4distS308;
  _M0L6_2atmpS974 = _M0L2hiS307 << (_M0L6_2atmpS976 & 63);
  _M0L6_2atmpS975 = _M0L2loS309 >> (_M0L4distS308 & 63);
  return _M0L6_2atmpS974 | _M0L6_2atmpS975;
}

struct _M0TPB7Umul128 _M0FPB7umul128(
  uint64_t _M0L1aS297,
  uint64_t _M0L1bS300
) {
  uint64_t _M0L3aLoS296;
  uint64_t _M0L3aHiS298;
  uint64_t _M0L3bLoS299;
  uint64_t _M0L3bHiS301;
  uint64_t _M0L1xS302;
  uint64_t _M0L6_2atmpS972;
  uint64_t _M0L6_2atmpS973;
  uint64_t _M0L1yS303;
  uint64_t _M0L6_2atmpS970;
  uint64_t _M0L6_2atmpS971;
  uint64_t _M0L1zS304;
  uint64_t _M0L6_2atmpS968;
  uint64_t _M0L6_2atmpS969;
  uint64_t _M0L6_2atmpS966;
  uint64_t _M0L6_2atmpS967;
  uint64_t _M0L1wS305;
  uint64_t _M0L2loS306;
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L3aLoS296 = _M0L1aS297 & 4294967295ull;
  _M0L3aHiS298 = _M0L1aS297 >> 32;
  _M0L3bLoS299 = _M0L1bS300 & 4294967295ull;
  _M0L3bHiS301 = _M0L1bS300 >> 32;
  _M0L1xS302 = _M0L3aLoS296 * _M0L3bLoS299;
  _M0L6_2atmpS972 = _M0L3aHiS298 * _M0L3bLoS299;
  _M0L6_2atmpS973 = _M0L1xS302 >> 32;
  _M0L1yS303 = _M0L6_2atmpS972 + _M0L6_2atmpS973;
  _M0L6_2atmpS970 = _M0L3aLoS296 * _M0L3bHiS301;
  _M0L6_2atmpS971 = _M0L1yS303 & 4294967295ull;
  _M0L1zS304 = _M0L6_2atmpS970 + _M0L6_2atmpS971;
  _M0L6_2atmpS968 = _M0L3aHiS298 * _M0L3bHiS301;
  _M0L6_2atmpS969 = _M0L1yS303 >> 32;
  _M0L6_2atmpS966 = _M0L6_2atmpS968 + _M0L6_2atmpS969;
  _M0L6_2atmpS967 = _M0L1zS304 >> 32;
  _M0L1wS305 = _M0L6_2atmpS966 + _M0L6_2atmpS967;
  _M0L2loS306 = _M0L1aS297 * _M0L1bS300;
  return (struct _M0TPB7Umul128){.$0 = _M0L2loS306, .$1 = _M0L1wS305};
}

moonbit_string_t _M0FPB19string__from__bytes(
  moonbit_bytes_t _M0L5bytesS294,
  int32_t _M0L4fromS291,
  int32_t _M0L2toS290
) {
  int32_t _M0L3lenS289;
  int32_t _M0L6_2atmpS965;
  uint16_t* _M0L6bufferS292;
  int32_t _M0L1iS293;
  #line 52 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L3lenS289 = _M0L2toS290 - _M0L4fromS291;
  #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS965 = _M0IPC16uint166UInt16PB7Default7default();
  _M0L6bufferS292
  = (uint16_t*)moonbit_make_string(_M0L3lenS289, _M0L6_2atmpS965);
  _M0L1iS293 = 0;
  while (1) {
    if (_M0L1iS293 < _M0L3lenS289) {
      int32_t _M0L6_2atmpS963 = _M0L4fromS291 + _M0L1iS293;
      int32_t _M0L6_2atmpS962;
      int32_t _M0L6_2atmpS961;
      int32_t _M0L6_2atmpS964;
      if (
        _M0L6_2atmpS963 < 0
        || _M0L6_2atmpS963 >= Moonbit_array_length(_M0L5bytesS294)
      ) {
        #line 56 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6_2atmpS962 = (int32_t)_M0L5bytesS294[_M0L6_2atmpS963];
      _M0L6_2atmpS961 = (uint16_t)_M0L6_2atmpS962;
      if (
        _M0L1iS293 < 0 || _M0L1iS293 >= Moonbit_array_length(_M0L6bufferS292)
      ) {
        #line 56 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6bufferS292[_M0L1iS293] = _M0L6_2atmpS961;
      _M0L6_2atmpS964 = _M0L1iS293 + 1;
      _M0L1iS293 = _M0L6_2atmpS964;
      continue;
    }
    break;
  }
  return _M0L6bufferS292;
}

int32_t _M0FPB9log10Pow2(int32_t _M0L1eS288) {
  int32_t _M0L6_2atmpS960;
  uint32_t _M0L6_2atmpS959;
  uint32_t _M0L6_2atmpS958;
  #line 44 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS960 = _M0L1eS288 * 78913;
  _M0L6_2atmpS959 = *(uint32_t*)&_M0L6_2atmpS960;
  _M0L6_2atmpS958 = _M0L6_2atmpS959 >> 18;
  return *(int32_t*)&_M0L6_2atmpS958;
}

int32_t _M0FPB9log10Pow5(int32_t _M0L1eS287) {
  int32_t _M0L6_2atmpS957;
  uint32_t _M0L6_2atmpS956;
  uint32_t _M0L6_2atmpS955;
  #line 37 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS957 = _M0L1eS287 * 732923;
  _M0L6_2atmpS956 = *(uint32_t*)&_M0L6_2atmpS957;
  _M0L6_2atmpS955 = _M0L6_2atmpS956 >> 20;
  return *(int32_t*)&_M0L6_2atmpS955;
}

moonbit_string_t _M0FPB18copy__special__str(
  int32_t _M0L4signS285,
  int32_t _M0L8exponentS286,
  int32_t _M0L8mantissaS283
) {
  moonbit_string_t _M0L1sS284;
  moonbit_string_t _result_1452;
  #line 23 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  if (_M0L8mantissaS283) {
    return (moonbit_string_t)moonbit_string_literal_2.data;
  }
  if (_M0L4signS285) {
    _M0L1sS284 = (moonbit_string_t)moonbit_string_literal_3.data;
  } else {
    _M0L1sS284 = (moonbit_string_t)moonbit_string_literal_4.data;
  }
  if (_M0L8exponentS286) {
    moonbit_string_t _result_1451;
    #line 29 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _result_1451
    = moonbit_add_string(_M0L1sS284, (moonbit_string_t)moonbit_string_literal_5.data);
    moonbit_decref(_M0L1sS284);
    return _result_1451;
  }
  #line 31 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _result_1452
  = moonbit_add_string(_M0L1sS284, (moonbit_string_t)moonbit_string_literal_6.data);
  moonbit_decref(_M0L1sS284);
  return _result_1452;
}

int32_t _M0FPB8pow5bits(int32_t _M0L1eS282) {
  int32_t _M0L6_2atmpS954;
  uint32_t _M0L6_2atmpS953;
  uint32_t _M0L6_2atmpS952;
  int32_t _M0L6_2atmpS951;
  #line 18 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS954 = _M0L1eS282 * 1217359;
  _M0L6_2atmpS953 = *(uint32_t*)&_M0L6_2atmpS954;
  _M0L6_2atmpS952 = _M0L6_2atmpS953 >> 19;
  _M0L6_2atmpS951 = *(int32_t*)&_M0L6_2atmpS952;
  return _M0L6_2atmpS951 + 1;
}

int32_t _M0MPC16double6Double7to__int(double _M0L4selfS281) {
  #line 43 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_to_int.mbt"
  if (_M0L4selfS281 != _M0L4selfS281) {
    return 0;
  } else if (_M0L4selfS281 >= 0x1.fffffffcp+30) {
    return 2147483647;
  } else if (_M0L4selfS281 <= -0x1p+31) {
    return (int32_t)0x80000000;
  } else {
    return (int32_t)_M0L4selfS281;
  }
}

int64_t _M0MPC16double6Double9to__int64(double _M0L4selfS280) {
  #line 46 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_to_int64_native.mbt"
  if (_M0L4selfS280 != _M0L4selfS280) {
    return 0ll;
  } else if (_M0L4selfS280 >= 0x1p+63) {
    return 9223372036854775807ll;
  } else if (_M0L4selfS280 <= -0x1p+63) {
    return (int64_t)0x8000000000000000ll;
  } else {
    return (int64_t)_M0L4selfS280;
  }
}

struct _M0TPB5ArrayGfE* _M0MPC15array5Array20unsafe__make__uninitGfE(
  int32_t _M0L3lenS278
) {
  float* _M0L6_2atmpS949;
  struct _M0TPB5ArrayGfE* _block_1453;
  #line 30 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS949 = (float*)moonbit_make_float_array_raw(_M0L3lenS278);
  _block_1453
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_block_1453)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 10, 0);
  _block_1453->$0 = _M0L6_2atmpS949;
  _block_1453->$1 = _M0L3lenS278;
  return _block_1453;
}

struct _M0TPB5ArrayGbE* _M0MPC15array5Array20unsafe__make__uninitGbE(
  int32_t _M0L3lenS279
) {
  uint8_t* _M0L6_2atmpS950;
  struct _M0TPB5ArrayGbE* _block_1454;
  #line 30 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS950 = (uint8_t*)moonbit_make_bytes_raw(_M0L3lenS279);
  _block_1454
  = (struct _M0TPB5ArrayGbE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGbE));
  Moonbit_object_header(_block_1454)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 13, 0);
  _block_1454->$0 = _M0L6_2atmpS950;
  _block_1454->$1 = _M0L3lenS279;
  return _block_1454;
}

uint64_t _M0MPC15array13ReadOnlyArray2atGmE(
  uint64_t* _M0L4selfS274,
  int32_t _M0L5indexS275
) {
  uint64_t* _M0L6_2atmpS947;
  #line 38 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\readonlyarray.mbt"
  _M0L6_2atmpS947 = _M0L4selfS274;
  if (
    _M0L5indexS275 < 0
    || _M0L5indexS275 >= Moonbit_array_length(_M0L6_2atmpS947)
  ) {
    #line 40 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\readonlyarray.mbt"
    moonbit_panic();
  }
  return (uint64_t)_M0L6_2atmpS947[_M0L5indexS275];
}

uint32_t _M0MPC15array13ReadOnlyArray2atGjE(
  uint32_t* _M0L4selfS276,
  int32_t _M0L5indexS277
) {
  uint32_t* _M0L6_2atmpS948;
  #line 38 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\readonlyarray.mbt"
  _M0L6_2atmpS948 = _M0L4selfS276;
  if (
    _M0L5indexS277 < 0
    || _M0L5indexS277 >= Moonbit_array_length(_M0L6_2atmpS948)
  ) {
    #line 40 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\readonlyarray.mbt"
    moonbit_panic();
  }
  return (uint32_t)_M0L6_2atmpS948[_M0L5indexS277];
}

moonbit_string_t _M0IPC16uint646UInt64PB4Show10to__string(
  uint64_t _M0L4selfS273
) {
  #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  #line 51 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  return _M0MPC16uint646UInt6418to__string_2einner(_M0L4selfS273, 10);
}

moonbit_string_t _M0IPC13int3IntPB4Show10to__string(int32_t _M0L4selfS272) {
  #line 35 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  #line 36 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  return _M0MPC13int3Int18to__string_2einner(_M0L4selfS272, 10);
}

moonbit_string_t _M0IPC14bool4BoolPB4Show10to__string(int32_t _M0L4selfS271) {
  #line 26 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  if (_M0L4selfS271) {
    return (moonbit_string_t)moonbit_string_literal_7.data;
  } else {
    return (moonbit_string_t)moonbit_string_literal_8.data;
  }
}

uint64_t _M0MPC14uint4UInt10to__uint64(uint32_t _M0L4selfS270) {
  #line 2576 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  return (uint64_t)_M0L4selfS270;
}

float* _M0MPC15array5Array6bufferGfE(struct _M0TPB5ArrayGfE* _M0L4selfS268) {
  float* _M0L8_2afieldS1390;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS1390 = _M0L4selfS268->$0;
  moonbit_incref(_M0L8_2afieldS1390);
  return _M0L8_2afieldS1390;
}

uint8_t* _M0MPC15array5Array6bufferGbE(struct _M0TPB5ArrayGbE* _M0L4selfS269) {
  uint8_t* _M0L8_2afieldS1391;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS1391 = _M0L4selfS269->$0;
  moonbit_incref(_M0L8_2afieldS1391);
  return _M0L8_2afieldS1391;
}

moonbit_string_t _M0IPC16string6StringPB4Show10to__string(
  moonbit_string_t _M0L4selfS267
) {
  #line 222 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  moonbit_incref(_M0L4selfS267);
  return _M0L4selfS267;
}

int32_t _M0IPB13StringBuilderPB6Logger11write__view(
  struct _M0TPB13StringBuilder* _M0L4selfS266,
  struct _M0TPC16string10StringView _M0L3strS264
) {
  int32_t _M0L3endS945;
  int32_t _M0L5startS946;
  int32_t _M0L8str__lenS263;
  int32_t _M0L3lenS944;
  int32_t _M0L8requiredS265;
  uint16_t* _M0L4dataS937;
  int32_t _M0L6_2atmpS936;
  int32_t _if__result_1455;
  uint16_t* _M0L4dataS938;
  int32_t _M0L3lenS939;
  moonbit_string_t _M0L6_2atmpS940;
  int32_t _M0L6_2atmpS941;
  int32_t _M0L3lenS943;
  int32_t _M0L6_2atmpS942;
  #line 158 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L3endS945 = _M0L3strS264.$2;
  _M0L5startS946 = _M0L3strS264.$1;
  _M0L8str__lenS263 = _M0L3endS945 - _M0L5startS946;
  if (_M0L8str__lenS263 == 0) {
    return 0;
  }
  _M0L3lenS944 = _M0L4selfS266->$1;
  _M0L8requiredS265 = _M0L3lenS944 + _M0L8str__lenS263;
  _M0L4dataS937 = _M0L4selfS266->$0;
  _M0L6_2atmpS936 = Moonbit_array_length(_M0L4dataS937);
  if (_M0L8requiredS265 > _M0L6_2atmpS936) {
    _if__result_1455 = 1;
  } else {
    int32_t _M0L3lenS935 = _M0L4selfS266->$1;
    _if__result_1455 = _M0L8requiredS265 < _M0L3lenS935;
  }
  if (_if__result_1455) {
    #line 168 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0MPB13StringBuilder4grow(_M0L4selfS266, _M0L8requiredS265);
  }
  _M0L4dataS938 = _M0L4selfS266->$0;
  _M0L3lenS939 = _M0L4selfS266->$1;
  moonbit_incref(_M0L4dataS938);
  #line 172 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L6_2atmpS940 = _M0MPC16string10StringView4data(_M0L3strS264);
  #line 173 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L6_2atmpS941 = _M0MPC16string10StringView13start__offset(_M0L3strS264);
  #line 170 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0MPC15array10FixedArray26unsafe__blit__from__string(_M0L4dataS938, _M0L3lenS939, _M0L6_2atmpS940, _M0L6_2atmpS941, _M0L8str__lenS263);
  moonbit_decref(_M0L4dataS938);
  moonbit_decref(_M0L6_2atmpS940);
  _M0L3lenS943 = _M0L4selfS266->$1;
  _M0L6_2atmpS942 = _M0L3lenS943 + _M0L8str__lenS263;
  _M0L4selfS266->$1 = _M0L6_2atmpS942;
  return 0;
}

moonbit_string_t _M0MPC16uint646UInt6418to__string_2einner(
  uint64_t _M0L4selfS255,
  int32_t _M0L5radixS254
) {
  uint16_t* _M0L6bufferS256;
  #line 607 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  if (_M0L5radixS254 < 2 || _M0L5radixS254 > 36) {
    #line 611 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_9.data);
  }
  if (_M0L4selfS255 == 0ull) {
    return (moonbit_string_t)moonbit_string_literal_0.data;
  }
  switch (_M0L5radixS254) {
    case 10: {
      int32_t _M0L3lenS257;
      uint16_t* _M0L6bufferS258;
      #line 622 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L3lenS257 = _M0FPB12dec__count64(_M0L4selfS255);
      _M0L6bufferS258 = (uint16_t*)moonbit_make_string(_M0L3lenS257, 0);
      #line 624 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0FPB22int64__to__string__dec(_M0L6bufferS258, _M0L4selfS255, 0, _M0L3lenS257);
      _M0L6bufferS256 = _M0L6bufferS258;
      break;
    }
    
    case 16: {
      int32_t _M0L3lenS259;
      uint16_t* _M0L6bufferS260;
      #line 628 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L3lenS259 = _M0FPB12hex__count64(_M0L4selfS255);
      _M0L6bufferS260 = (uint16_t*)moonbit_make_string(_M0L3lenS259, 0);
      #line 630 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0FPB22int64__to__string__hex(_M0L6bufferS260, _M0L4selfS255, 0, _M0L3lenS259);
      _M0L6bufferS256 = _M0L6bufferS260;
      break;
    }
    default: {
      int32_t _M0L3lenS261;
      uint16_t* _M0L6bufferS262;
      #line 634 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L3lenS261 = _M0FPB14radix__count64(_M0L4selfS255, _M0L5radixS254);
      _M0L6bufferS262 = (uint16_t*)moonbit_make_string(_M0L3lenS261, 0);
      #line 636 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0FPB26int64__to__string__generic(_M0L6bufferS262, _M0L4selfS255, 0, _M0L3lenS261, _M0L5radixS254);
      _M0L6bufferS256 = _M0L6bufferS262;
      break;
    }
  }
  return _M0L6bufferS256;
}

moonbit_string_t _M0MPC15int645Int6418to__string_2einner(
  int64_t _M0L4selfS238,
  int32_t _M0L5radixS237
) {
  int32_t _M0L12is__negativeS239;
  uint64_t _M0L3numS240;
  uint16_t* _M0L6bufferS241;
  #line 548 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  if (_M0L5radixS237 < 2 || _M0L5radixS237 > 36) {
    #line 552 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_9.data);
  }
  if (_M0L4selfS238 == 0ll) {
    return (moonbit_string_t)moonbit_string_literal_0.data;
  }
  _M0L12is__negativeS239 = _M0L4selfS238 < 0ll;
  if (_M0L12is__negativeS239) {
    int64_t _M0L6_2atmpS934 = -_M0L4selfS238;
    _M0L3numS240 = *(uint64_t*)&_M0L6_2atmpS934;
  } else {
    _M0L3numS240 = *(uint64_t*)&_M0L4selfS238;
  }
  switch (_M0L5radixS237) {
    case 10: {
      int32_t _M0L10digit__lenS242;
      int32_t _M0L6_2atmpS931;
      int32_t _M0L10total__lenS243;
      uint16_t* _M0L6bufferS244;
      int32_t _M0L12digit__startS245;
      #line 573 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS242 = _M0FPB12dec__count64(_M0L3numS240);
      if (_M0L12is__negativeS239) {
        _M0L6_2atmpS931 = 1;
      } else {
        _M0L6_2atmpS931 = 0;
      }
      _M0L10total__lenS243 = _M0L10digit__lenS242 + _M0L6_2atmpS931;
      _M0L6bufferS244
      = (uint16_t*)moonbit_make_string(_M0L10total__lenS243, 0);
      if (_M0L12is__negativeS239) {
        _M0L12digit__startS245 = 1;
      } else {
        _M0L12digit__startS245 = 0;
      }
      #line 577 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0FPB22int64__to__string__dec(_M0L6bufferS244, _M0L3numS240, _M0L12digit__startS245, _M0L10total__lenS243);
      _M0L6bufferS241 = _M0L6bufferS244;
      break;
    }
    
    case 16: {
      int32_t _M0L10digit__lenS246;
      int32_t _M0L6_2atmpS932;
      int32_t _M0L10total__lenS247;
      uint16_t* _M0L6bufferS248;
      int32_t _M0L12digit__startS249;
      #line 581 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS246 = _M0FPB12hex__count64(_M0L3numS240);
      if (_M0L12is__negativeS239) {
        _M0L6_2atmpS932 = 1;
      } else {
        _M0L6_2atmpS932 = 0;
      }
      _M0L10total__lenS247 = _M0L10digit__lenS246 + _M0L6_2atmpS932;
      _M0L6bufferS248
      = (uint16_t*)moonbit_make_string(_M0L10total__lenS247, 0);
      if (_M0L12is__negativeS239) {
        _M0L12digit__startS249 = 1;
      } else {
        _M0L12digit__startS249 = 0;
      }
      #line 585 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0FPB22int64__to__string__hex(_M0L6bufferS248, _M0L3numS240, _M0L12digit__startS249, _M0L10total__lenS247);
      _M0L6bufferS241 = _M0L6bufferS248;
      break;
    }
    default: {
      int32_t _M0L10digit__lenS250;
      int32_t _M0L6_2atmpS933;
      int32_t _M0L10total__lenS251;
      uint16_t* _M0L6bufferS252;
      int32_t _M0L12digit__startS253;
      #line 589 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS250
      = _M0FPB14radix__count64(_M0L3numS240, _M0L5radixS237);
      if (_M0L12is__negativeS239) {
        _M0L6_2atmpS933 = 1;
      } else {
        _M0L6_2atmpS933 = 0;
      }
      _M0L10total__lenS251 = _M0L10digit__lenS250 + _M0L6_2atmpS933;
      _M0L6bufferS252
      = (uint16_t*)moonbit_make_string(_M0L10total__lenS251, 0);
      if (_M0L12is__negativeS239) {
        _M0L12digit__startS253 = 1;
      } else {
        _M0L12digit__startS253 = 0;
      }
      #line 593 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0FPB26int64__to__string__generic(_M0L6bufferS252, _M0L3numS240, _M0L12digit__startS253, _M0L10total__lenS251, _M0L5radixS237);
      _M0L6bufferS241 = _M0L6bufferS252;
      break;
    }
  }
  if (_M0L12is__negativeS239) {
    _M0L6bufferS241[0] = 45;
  }
  return _M0L6bufferS241;
}

int32_t _M0FPB22int64__to__string__dec(
  uint16_t* _M0L6bufferS223,
  uint64_t _M0L3numS235,
  int32_t _M0L12digit__startS224,
  int32_t _M0L10total__lenS236
) {
  int32_t _M0L6_2atmpS930;
  uint64_t _M0L3numS213;
  int32_t _M0L6offsetS214;
  #line 493 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L6_2atmpS930 = _M0L10total__lenS236 - _M0L12digit__startS224;
  _M0L3numS213 = _M0L3numS235;
  _M0L6offsetS214 = _M0L6_2atmpS930;
  while (1) {
    if (_M0L3numS213 >= 10000ull) {
      uint64_t _M0L1tS215 = _M0L3numS213 / 10000ull;
      uint64_t _M0L6_2atmpS907 = _M0L3numS213 % 10000ull;
      int32_t _M0L1rS216 = (int32_t)_M0L6_2atmpS907;
      int32_t _M0L2d1S217 = _M0L1rS216 / 100;
      int32_t _M0L2d2S218 = _M0L1rS216 % 100;
      int32_t _M0L6_2atmpS906 = _M0L2d1S217 / 10;
      int32_t _M0L6_2atmpS905 = 48 + _M0L6_2atmpS906;
      int32_t _M0L6d1__hiS219 = (uint16_t)_M0L6_2atmpS905;
      int32_t _M0L6_2atmpS904 = _M0L2d1S217 % 10;
      int32_t _M0L6_2atmpS903 = 48 + _M0L6_2atmpS904;
      int32_t _M0L6d1__loS220 = (uint16_t)_M0L6_2atmpS903;
      int32_t _M0L6_2atmpS902 = _M0L2d2S218 / 10;
      int32_t _M0L6_2atmpS901 = 48 + _M0L6_2atmpS902;
      int32_t _M0L6d2__hiS221 = (uint16_t)_M0L6_2atmpS901;
      int32_t _M0L6_2atmpS900 = _M0L2d2S218 % 10;
      int32_t _M0L6_2atmpS899 = 48 + _M0L6_2atmpS900;
      int32_t _M0L6d2__loS222 = (uint16_t)_M0L6_2atmpS899;
      int32_t _M0L6_2atmpS891 = _M0L12digit__startS224 + _M0L6offsetS214;
      int32_t _M0L6_2atmpS890 = _M0L6_2atmpS891 - 4;
      int32_t _M0L6_2atmpS893;
      int32_t _M0L6_2atmpS892;
      int32_t _M0L6_2atmpS895;
      int32_t _M0L6_2atmpS894;
      int32_t _M0L6_2atmpS897;
      int32_t _M0L6_2atmpS896;
      int32_t _M0L6_2atmpS898;
      _M0L6bufferS223[_M0L6_2atmpS890] = _M0L6d1__hiS219;
      _M0L6_2atmpS893 = _M0L12digit__startS224 + _M0L6offsetS214;
      _M0L6_2atmpS892 = _M0L6_2atmpS893 - 3;
      _M0L6bufferS223[_M0L6_2atmpS892] = _M0L6d1__loS220;
      _M0L6_2atmpS895 = _M0L12digit__startS224 + _M0L6offsetS214;
      _M0L6_2atmpS894 = _M0L6_2atmpS895 - 2;
      _M0L6bufferS223[_M0L6_2atmpS894] = _M0L6d2__hiS221;
      _M0L6_2atmpS897 = _M0L12digit__startS224 + _M0L6offsetS214;
      _M0L6_2atmpS896 = _M0L6_2atmpS897 - 1;
      _M0L6bufferS223[_M0L6_2atmpS896] = _M0L6d2__loS222;
      _M0L6_2atmpS898 = _M0L6offsetS214 - 4;
      _M0L3numS213 = _M0L1tS215;
      _M0L6offsetS214 = _M0L6_2atmpS898;
      continue;
    } else {
      int32_t _M0L6_2atmpS929 = (int32_t)_M0L3numS213;
      int32_t _M0L9remainingS226 = _M0L6_2atmpS929;
      int32_t _M0L6offsetS227 = _M0L6offsetS214;
      while (1) {
        if (_M0L9remainingS226 >= 100) {
          int32_t _M0L1tS228 = _M0L9remainingS226 / 100;
          int32_t _M0L1dS229 = _M0L9remainingS226 % 100;
          int32_t _M0L6_2atmpS916 = _M0L1dS229 / 10;
          int32_t _M0L6_2atmpS915 = 48 + _M0L6_2atmpS916;
          int32_t _M0L5d__hiS230 = (uint16_t)_M0L6_2atmpS915;
          int32_t _M0L6_2atmpS914 = _M0L1dS229 % 10;
          int32_t _M0L6_2atmpS913 = 48 + _M0L6_2atmpS914;
          int32_t _M0L5d__loS231 = (uint16_t)_M0L6_2atmpS913;
          int32_t _M0L6_2atmpS909 = _M0L12digit__startS224 + _M0L6offsetS227;
          int32_t _M0L6_2atmpS908 = _M0L6_2atmpS909 - 2;
          int32_t _M0L6_2atmpS911;
          int32_t _M0L6_2atmpS910;
          int32_t _M0L6_2atmpS912;
          _M0L6bufferS223[_M0L6_2atmpS908] = _M0L5d__hiS230;
          _M0L6_2atmpS911 = _M0L12digit__startS224 + _M0L6offsetS227;
          _M0L6_2atmpS910 = _M0L6_2atmpS911 - 1;
          _M0L6bufferS223[_M0L6_2atmpS910] = _M0L5d__loS231;
          _M0L6_2atmpS912 = _M0L6offsetS227 - 2;
          _M0L9remainingS226 = _M0L1tS228;
          _M0L6offsetS227 = _M0L6_2atmpS912;
          continue;
        } else if (_M0L9remainingS226 >= 10) {
          int32_t _M0L6_2atmpS924 = _M0L9remainingS226 / 10;
          int32_t _M0L6_2atmpS923 = 48 + _M0L6_2atmpS924;
          int32_t _M0L5d__hiS233 = (uint16_t)_M0L6_2atmpS923;
          int32_t _M0L6_2atmpS922 = _M0L9remainingS226 % 10;
          int32_t _M0L6_2atmpS921 = 48 + _M0L6_2atmpS922;
          int32_t _M0L5d__loS234 = (uint16_t)_M0L6_2atmpS921;
          int32_t _M0L6_2atmpS918 = _M0L12digit__startS224 + _M0L6offsetS227;
          int32_t _M0L6_2atmpS917 = _M0L6_2atmpS918 - 2;
          int32_t _M0L6_2atmpS920;
          int32_t _M0L6_2atmpS919;
          _M0L6bufferS223[_M0L6_2atmpS917] = _M0L5d__hiS233;
          _M0L6_2atmpS920 = _M0L12digit__startS224 + _M0L6offsetS227;
          _M0L6_2atmpS919 = _M0L6_2atmpS920 - 1;
          _M0L6bufferS223[_M0L6_2atmpS919] = _M0L5d__loS234;
        } else {
          int32_t _M0L6_2atmpS928 = _M0L12digit__startS224 + _M0L6offsetS227;
          int32_t _M0L6_2atmpS925 = _M0L6_2atmpS928 - 1;
          int32_t _M0L6_2atmpS927 = 48 + _M0L9remainingS226;
          int32_t _M0L6_2atmpS926 = (uint16_t)_M0L6_2atmpS927;
          _M0L6bufferS223[_M0L6_2atmpS925] = _M0L6_2atmpS926;
        }
        break;
      }
    }
    break;
  }
  return 0;
}

int32_t _M0FPB26int64__to__string__generic(
  uint16_t* _M0L6bufferS203,
  uint64_t _M0L3numS207,
  int32_t _M0L12digit__startS204,
  int32_t _M0L10total__lenS206,
  int32_t _M0L5radixS197
) {
  uint64_t _M0L4baseS196;
  int32_t _M0L6_2atmpS875;
  int32_t _M0L6_2atmpS874;
  #line 462 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  #line 470 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L4baseS196 = _M0MPC13int3Int10to__uint64(_M0L5radixS197);
  _M0L6_2atmpS875 = _M0L5radixS197 - 1;
  _M0L6_2atmpS874 = _M0L5radixS197 & _M0L6_2atmpS875;
  if (_M0L6_2atmpS874 == 0) {
    int32_t _M0L5shiftS198;
    uint64_t _M0L4maskS199;
    int32_t _M0L6_2atmpS882;
    int32_t _M0L6offsetS200;
    uint64_t _M0L1nS201;
    #line 473 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0L5shiftS198 = moonbit_ctz32(_M0L5radixS197);
    _M0L4maskS199 = _M0L4baseS196 - 1ull;
    _M0L6_2atmpS882 = _M0L10total__lenS206 - _M0L12digit__startS204;
    _M0L6offsetS200 = _M0L6_2atmpS882;
    _M0L1nS201 = _M0L3numS207;
    while (1) {
      if (_M0L1nS201 > 0ull) {
        uint64_t _M0L6_2atmpS881 = _M0L1nS201 & _M0L4maskS199;
        int32_t _M0L5digitS202 = (int32_t)_M0L6_2atmpS881;
        int32_t _M0L6_2atmpS878 = _M0L12digit__startS204 + _M0L6offsetS200;
        int32_t _M0L6_2atmpS876 = _M0L6_2atmpS878 - 1;
        int32_t _M0L6_2atmpS877 =
          ((moonbit_string_t)moonbit_string_literal_10.data)[_M0L5digitS202];
        int32_t _M0L6_2atmpS879;
        uint64_t _M0L6_2atmpS880;
        _M0L6bufferS203[_M0L6_2atmpS876] = _M0L6_2atmpS877;
        _M0L6_2atmpS879 = _M0L6offsetS200 - 1;
        _M0L6_2atmpS880 = _M0L1nS201 >> (_M0L5shiftS198 & 63);
        _M0L6offsetS200 = _M0L6_2atmpS879;
        _M0L1nS201 = _M0L6_2atmpS880;
        continue;
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS889 = _M0L10total__lenS206 - _M0L12digit__startS204;
    int32_t _M0L6offsetS208 = _M0L6_2atmpS889;
    uint64_t _M0L1nS209 = _M0L3numS207;
    while (1) {
      if (_M0L1nS209 > 0ull) {
        uint64_t _M0L1qS210 = _M0L1nS209 / _M0L4baseS196;
        uint64_t _M0L6_2atmpS888 = _M0L1qS210 * _M0L4baseS196;
        uint64_t _M0L6_2atmpS887 = _M0L1nS209 - _M0L6_2atmpS888;
        int32_t _M0L5digitS211 = (int32_t)_M0L6_2atmpS887;
        int32_t _M0L6_2atmpS885 = _M0L12digit__startS204 + _M0L6offsetS208;
        int32_t _M0L6_2atmpS883 = _M0L6_2atmpS885 - 1;
        int32_t _M0L6_2atmpS884 =
          ((moonbit_string_t)moonbit_string_literal_10.data)[_M0L5digitS211];
        int32_t _M0L6_2atmpS886;
        _M0L6bufferS203[_M0L6_2atmpS883] = _M0L6_2atmpS884;
        _M0L6_2atmpS886 = _M0L6offsetS208 - 1;
        _M0L6offsetS208 = _M0L6_2atmpS886;
        _M0L1nS209 = _M0L1qS210;
        continue;
      }
      break;
    }
  }
  return 0;
}

int32_t _M0FPB22int64__to__string__hex(
  uint16_t* _M0L6bufferS190,
  uint64_t _M0L3numS195,
  int32_t _M0L12digit__startS191,
  int32_t _M0L10total__lenS194
) {
  int32_t _M0L6_2atmpS873;
  int32_t _M0L6offsetS185;
  uint64_t _M0L1nS186;
  #line 434 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L6_2atmpS873 = _M0L10total__lenS194 - _M0L12digit__startS191;
  _M0L6offsetS185 = _M0L6_2atmpS873;
  _M0L1nS186 = _M0L3numS195;
  while (1) {
    if (_M0L6offsetS185 >= 2) {
      uint64_t _M0L6_2atmpS870 = _M0L1nS186 & 255ull;
      int32_t _M0L9byte__valS187 = (int32_t)_M0L6_2atmpS870;
      int32_t _M0L2hiS188 = _M0L9byte__valS187 / 16;
      int32_t _M0L2loS189 = _M0L9byte__valS187 % 16;
      int32_t _M0L6_2atmpS864 = _M0L12digit__startS191 + _M0L6offsetS185;
      int32_t _M0L6_2atmpS862 = _M0L6_2atmpS864 - 2;
      int32_t _M0L6_2atmpS863 =
        ((moonbit_string_t)moonbit_string_literal_10.data)[_M0L2hiS188];
      int32_t _M0L6_2atmpS867;
      int32_t _M0L6_2atmpS865;
      int32_t _M0L6_2atmpS866;
      int32_t _M0L6_2atmpS868;
      uint64_t _M0L6_2atmpS869;
      _M0L6bufferS190[_M0L6_2atmpS862] = _M0L6_2atmpS863;
      _M0L6_2atmpS867 = _M0L12digit__startS191 + _M0L6offsetS185;
      _M0L6_2atmpS865 = _M0L6_2atmpS867 - 1;
      _M0L6_2atmpS866
      = ((moonbit_string_t)moonbit_string_literal_10.data)[
        _M0L2loS189
      ];
      _M0L6bufferS190[_M0L6_2atmpS865] = _M0L6_2atmpS866;
      _M0L6_2atmpS868 = _M0L6offsetS185 - 2;
      _M0L6_2atmpS869 = _M0L1nS186 >> 8;
      _M0L6offsetS185 = _M0L6_2atmpS868;
      _M0L1nS186 = _M0L6_2atmpS869;
      continue;
    } else if (_M0L6offsetS185 == 1) {
      uint64_t _M0L6_2atmpS872 = _M0L1nS186 & 15ull;
      int32_t _M0L6nibbleS193 = (int32_t)_M0L6_2atmpS872;
      int32_t _M0L6_2atmpS871 =
        ((moonbit_string_t)moonbit_string_literal_10.data)[_M0L6nibbleS193];
      _M0L6bufferS190[_M0L12digit__startS191] = _M0L6_2atmpS871;
    }
    break;
  }
  return 0;
}

int32_t _M0FPB14radix__count64(
  uint64_t _M0L5valueS179,
  int32_t _M0L5radixS181
) {
  uint64_t _M0L4baseS180;
  uint64_t _M0L3numS182;
  int32_t _M0L5countS183;
  #line 419 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  if (_M0L5valueS179 == 0ull) {
    return 1;
  }
  #line 424 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L4baseS180 = _M0MPC13int3Int10to__uint64(_M0L5radixS181);
  _M0L3numS182 = _M0L5valueS179;
  _M0L5countS183 = 0;
  while (1) {
    if (_M0L3numS182 > 0ull) {
      uint64_t _M0L6_2atmpS860 = _M0L3numS182 / _M0L4baseS180;
      int32_t _M0L6_2atmpS861 = _M0L5countS183 + 1;
      _M0L3numS182 = _M0L6_2atmpS860;
      _M0L5countS183 = _M0L6_2atmpS861;
      continue;
    } else {
      return _M0L5countS183;
    }
    break;
  }
}

int32_t _M0FPB12hex__count64(uint64_t _M0L5valueS177) {
  #line 407 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  if (_M0L5valueS177 == 0ull) {
    return 1;
  } else {
    int32_t _M0L14leading__zerosS178;
    int32_t _M0L6_2atmpS859;
    int32_t _M0L6_2atmpS858;
    #line 412 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0L14leading__zerosS178 = moonbit_clz64(_M0L5valueS177);
    _M0L6_2atmpS859 = 63 - _M0L14leading__zerosS178;
    _M0L6_2atmpS858 = _M0L6_2atmpS859 / 4;
    return _M0L6_2atmpS858 + 1;
  }
}

int32_t _M0FPB12dec__count64(uint64_t _M0L5valueS176) {
  #line 343 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  if (_M0L5valueS176 >= 10000000000ull) {
    if (_M0L5valueS176 >= 100000000000000ull) {
      if (_M0L5valueS176 >= 10000000000000000ull) {
        if (_M0L5valueS176 >= 1000000000000000000ull) {
          if (_M0L5valueS176 >= 10000000000000000000ull) {
            return 20;
          } else {
            return 19;
          }
        } else if (_M0L5valueS176 >= 100000000000000000ull) {
          return 18;
        } else {
          return 17;
        }
      } else if (_M0L5valueS176 >= 1000000000000000ull) {
        return 16;
      } else {
        return 15;
      }
    } else if (_M0L5valueS176 >= 1000000000000ull) {
      if (_M0L5valueS176 >= 10000000000000ull) {
        return 14;
      } else {
        return 13;
      }
    } else if (_M0L5valueS176 >= 100000000000ull) {
      return 12;
    } else {
      return 11;
    }
  } else if (_M0L5valueS176 >= 100000ull) {
    if (_M0L5valueS176 >= 10000000ull) {
      if (_M0L5valueS176 >= 1000000000ull) {
        return 10;
      } else if (_M0L5valueS176 >= 100000000ull) {
        return 9;
      } else {
        return 8;
      }
    } else if (_M0L5valueS176 >= 1000000ull) {
      return 7;
    } else {
      return 6;
    }
  } else if (_M0L5valueS176 >= 1000ull) {
    if (_M0L5valueS176 >= 10000ull) {
      return 5;
    } else {
      return 4;
    }
  } else if (_M0L5valueS176 >= 100ull) {
    return 3;
  } else if (_M0L5valueS176 >= 10ull) {
    return 2;
  } else {
    return 1;
  }
}

moonbit_string_t _M0MPC13int3Int18to__string_2einner(
  int32_t _M0L4selfS160,
  int32_t _M0L5radixS159
) {
  int32_t _M0L12is__negativeS161;
  uint32_t _M0L3numS162;
  uint16_t* _M0L6bufferS163;
  #line 209 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  if (_M0L5radixS159 < 2 || _M0L5radixS159 > 36) {
    #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_9.data);
  }
  if (_M0L4selfS160 == 0) {
    return (moonbit_string_t)moonbit_string_literal_0.data;
  }
  _M0L12is__negativeS161 = _M0L4selfS160 < 0;
  if (_M0L12is__negativeS161) {
    int32_t _M0L6_2atmpS857 = -_M0L4selfS160;
    _M0L3numS162 = *(uint32_t*)&_M0L6_2atmpS857;
  } else {
    _M0L3numS162 = *(uint32_t*)&_M0L4selfS160;
  }
  switch (_M0L5radixS159) {
    case 10: {
      int32_t _M0L10digit__lenS164;
      int32_t _M0L6_2atmpS854;
      int32_t _M0L10total__lenS165;
      uint16_t* _M0L6bufferS166;
      int32_t _M0L12digit__startS167;
      #line 235 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS164 = _M0FPB12dec__count32(_M0L3numS162);
      if (_M0L12is__negativeS161) {
        _M0L6_2atmpS854 = 1;
      } else {
        _M0L6_2atmpS854 = 0;
      }
      _M0L10total__lenS165 = _M0L10digit__lenS164 + _M0L6_2atmpS854;
      _M0L6bufferS166
      = (uint16_t*)moonbit_make_string(_M0L10total__lenS165, 0);
      if (_M0L12is__negativeS161) {
        _M0L12digit__startS167 = 1;
      } else {
        _M0L12digit__startS167 = 0;
      }
      #line 239 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0FPB20int__to__string__dec(_M0L6bufferS166, _M0L3numS162, _M0L12digit__startS167, _M0L10total__lenS165);
      _M0L6bufferS163 = _M0L6bufferS166;
      break;
    }
    
    case 16: {
      int32_t _M0L10digit__lenS168;
      int32_t _M0L6_2atmpS855;
      int32_t _M0L10total__lenS169;
      uint16_t* _M0L6bufferS170;
      int32_t _M0L12digit__startS171;
      #line 243 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS168 = _M0FPB12hex__count32(_M0L3numS162);
      if (_M0L12is__negativeS161) {
        _M0L6_2atmpS855 = 1;
      } else {
        _M0L6_2atmpS855 = 0;
      }
      _M0L10total__lenS169 = _M0L10digit__lenS168 + _M0L6_2atmpS855;
      _M0L6bufferS170
      = (uint16_t*)moonbit_make_string(_M0L10total__lenS169, 0);
      if (_M0L12is__negativeS161) {
        _M0L12digit__startS171 = 1;
      } else {
        _M0L12digit__startS171 = 0;
      }
      #line 247 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0FPB20int__to__string__hex(_M0L6bufferS170, _M0L3numS162, _M0L12digit__startS171, _M0L10total__lenS169);
      _M0L6bufferS163 = _M0L6bufferS170;
      break;
    }
    default: {
      int32_t _M0L10digit__lenS172;
      int32_t _M0L6_2atmpS856;
      int32_t _M0L10total__lenS173;
      uint16_t* _M0L6bufferS174;
      int32_t _M0L12digit__startS175;
      #line 251 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS172
      = _M0FPB14radix__count32(_M0L3numS162, _M0L5radixS159);
      if (_M0L12is__negativeS161) {
        _M0L6_2atmpS856 = 1;
      } else {
        _M0L6_2atmpS856 = 0;
      }
      _M0L10total__lenS173 = _M0L10digit__lenS172 + _M0L6_2atmpS856;
      _M0L6bufferS174
      = (uint16_t*)moonbit_make_string(_M0L10total__lenS173, 0);
      if (_M0L12is__negativeS161) {
        _M0L12digit__startS175 = 1;
      } else {
        _M0L12digit__startS175 = 0;
      }
      #line 255 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0FPB24int__to__string__generic(_M0L6bufferS174, _M0L3numS162, _M0L12digit__startS175, _M0L10total__lenS173, _M0L5radixS159);
      _M0L6bufferS163 = _M0L6bufferS174;
      break;
    }
  }
  if (_M0L12is__negativeS161) {
    _M0L6bufferS163[0] = 45;
  }
  return _M0L6bufferS163;
}

int32_t _M0FPB14radix__count32(
  uint32_t _M0L5valueS153,
  int32_t _M0L5radixS155
) {
  uint32_t _M0L4baseS154;
  uint32_t _M0L3numS156;
  int32_t _M0L5countS157;
  #line 189 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  if (_M0L5valueS153 == 0u) {
    return 1;
  }
  _M0L4baseS154 = *(uint32_t*)&_M0L5radixS155;
  _M0L3numS156 = _M0L5valueS153;
  _M0L5countS157 = 0;
  while (1) {
    if (_M0L3numS156 > 0u) {
      uint32_t _M0L6_2atmpS852 = _M0L3numS156 / _M0L4baseS154;
      int32_t _M0L6_2atmpS853 = _M0L5countS157 + 1;
      _M0L3numS156 = _M0L6_2atmpS852;
      _M0L5countS157 = _M0L6_2atmpS853;
      continue;
    } else {
      return _M0L5countS157;
    }
    break;
  }
}

int32_t _M0FPB12hex__count32(uint32_t _M0L5valueS151) {
  #line 177 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  if (_M0L5valueS151 == 0u) {
    return 1;
  } else {
    int32_t _M0L14leading__zerosS152;
    int32_t _M0L6_2atmpS851;
    int32_t _M0L6_2atmpS850;
    #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0L14leading__zerosS152 = moonbit_clz32(_M0L5valueS151);
    _M0L6_2atmpS851 = 31 - _M0L14leading__zerosS152;
    _M0L6_2atmpS850 = _M0L6_2atmpS851 / 4;
    return _M0L6_2atmpS850 + 1;
  }
}

int32_t _M0FPB12dec__count32(uint32_t _M0L5valueS150) {
  #line 143 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  if (_M0L5valueS150 >= 100000u) {
    if (_M0L5valueS150 >= 10000000u) {
      if (_M0L5valueS150 >= 1000000000u) {
        return 10;
      } else if (_M0L5valueS150 >= 100000000u) {
        return 9;
      } else {
        return 8;
      }
    } else if (_M0L5valueS150 >= 1000000u) {
      return 7;
    } else {
      return 6;
    }
  } else if (_M0L5valueS150 >= 1000u) {
    if (_M0L5valueS150 >= 10000u) {
      return 5;
    } else {
      return 4;
    }
  } else if (_M0L5valueS150 >= 100u) {
    return 3;
  } else if (_M0L5valueS150 >= 10u) {
    return 2;
  } else {
    return 1;
  }
}

int32_t _M0FPB20int__to__string__dec(
  uint16_t* _M0L6bufferS136,
  uint32_t _M0L3numS148,
  int32_t _M0L12digit__startS137,
  int32_t _M0L10total__lenS149
) {
  int32_t _M0L6_2atmpS849;
  uint32_t _M0L3numS126;
  int32_t _M0L6offsetS127;
  #line 88 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L6_2atmpS849 = _M0L10total__lenS149 - _M0L12digit__startS137;
  _M0L3numS126 = _M0L3numS148;
  _M0L6offsetS127 = _M0L6_2atmpS849;
  while (1) {
    if (_M0L3numS126 >= 10000u) {
      uint32_t _M0L1tS128 = _M0L3numS126 / 10000u;
      uint32_t _M0L6_2atmpS826 = _M0L3numS126 % 10000u;
      int32_t _M0L1rS129 = *(int32_t*)&_M0L6_2atmpS826;
      int32_t _M0L2d1S130 = _M0L1rS129 / 100;
      int32_t _M0L2d2S131 = _M0L1rS129 % 100;
      int32_t _M0L6_2atmpS825 = _M0L2d1S130 / 10;
      int32_t _M0L6_2atmpS824 = 48 + _M0L6_2atmpS825;
      int32_t _M0L6d1__hiS132 = (uint16_t)_M0L6_2atmpS824;
      int32_t _M0L6_2atmpS823 = _M0L2d1S130 % 10;
      int32_t _M0L6_2atmpS822 = 48 + _M0L6_2atmpS823;
      int32_t _M0L6d1__loS133 = (uint16_t)_M0L6_2atmpS822;
      int32_t _M0L6_2atmpS821 = _M0L2d2S131 / 10;
      int32_t _M0L6_2atmpS820 = 48 + _M0L6_2atmpS821;
      int32_t _M0L6d2__hiS134 = (uint16_t)_M0L6_2atmpS820;
      int32_t _M0L6_2atmpS819 = _M0L2d2S131 % 10;
      int32_t _M0L6_2atmpS818 = 48 + _M0L6_2atmpS819;
      int32_t _M0L6d2__loS135 = (uint16_t)_M0L6_2atmpS818;
      int32_t _M0L6_2atmpS810 = _M0L12digit__startS137 + _M0L6offsetS127;
      int32_t _M0L6_2atmpS809 = _M0L6_2atmpS810 - 4;
      int32_t _M0L6_2atmpS812;
      int32_t _M0L6_2atmpS811;
      int32_t _M0L6_2atmpS814;
      int32_t _M0L6_2atmpS813;
      int32_t _M0L6_2atmpS816;
      int32_t _M0L6_2atmpS815;
      int32_t _M0L6_2atmpS817;
      _M0L6bufferS136[_M0L6_2atmpS809] = _M0L6d1__hiS132;
      _M0L6_2atmpS812 = _M0L12digit__startS137 + _M0L6offsetS127;
      _M0L6_2atmpS811 = _M0L6_2atmpS812 - 3;
      _M0L6bufferS136[_M0L6_2atmpS811] = _M0L6d1__loS133;
      _M0L6_2atmpS814 = _M0L12digit__startS137 + _M0L6offsetS127;
      _M0L6_2atmpS813 = _M0L6_2atmpS814 - 2;
      _M0L6bufferS136[_M0L6_2atmpS813] = _M0L6d2__hiS134;
      _M0L6_2atmpS816 = _M0L12digit__startS137 + _M0L6offsetS127;
      _M0L6_2atmpS815 = _M0L6_2atmpS816 - 1;
      _M0L6bufferS136[_M0L6_2atmpS815] = _M0L6d2__loS135;
      _M0L6_2atmpS817 = _M0L6offsetS127 - 4;
      _M0L3numS126 = _M0L1tS128;
      _M0L6offsetS127 = _M0L6_2atmpS817;
      continue;
    } else {
      int32_t _M0L6_2atmpS848 = *(int32_t*)&_M0L3numS126;
      int32_t _M0L9remainingS139 = _M0L6_2atmpS848;
      int32_t _M0L6offsetS140 = _M0L6offsetS127;
      while (1) {
        if (_M0L9remainingS139 >= 100) {
          int32_t _M0L1tS141 = _M0L9remainingS139 / 100;
          int32_t _M0L1dS142 = _M0L9remainingS139 % 100;
          int32_t _M0L6_2atmpS835 = _M0L1dS142 / 10;
          int32_t _M0L6_2atmpS834 = 48 + _M0L6_2atmpS835;
          int32_t _M0L5d__hiS143 = (uint16_t)_M0L6_2atmpS834;
          int32_t _M0L6_2atmpS833 = _M0L1dS142 % 10;
          int32_t _M0L6_2atmpS832 = 48 + _M0L6_2atmpS833;
          int32_t _M0L5d__loS144 = (uint16_t)_M0L6_2atmpS832;
          int32_t _M0L6_2atmpS828 = _M0L12digit__startS137 + _M0L6offsetS140;
          int32_t _M0L6_2atmpS827 = _M0L6_2atmpS828 - 2;
          int32_t _M0L6_2atmpS830;
          int32_t _M0L6_2atmpS829;
          int32_t _M0L6_2atmpS831;
          _M0L6bufferS136[_M0L6_2atmpS827] = _M0L5d__hiS143;
          _M0L6_2atmpS830 = _M0L12digit__startS137 + _M0L6offsetS140;
          _M0L6_2atmpS829 = _M0L6_2atmpS830 - 1;
          _M0L6bufferS136[_M0L6_2atmpS829] = _M0L5d__loS144;
          _M0L6_2atmpS831 = _M0L6offsetS140 - 2;
          _M0L9remainingS139 = _M0L1tS141;
          _M0L6offsetS140 = _M0L6_2atmpS831;
          continue;
        } else if (_M0L9remainingS139 >= 10) {
          int32_t _M0L6_2atmpS843 = _M0L9remainingS139 / 10;
          int32_t _M0L6_2atmpS842 = 48 + _M0L6_2atmpS843;
          int32_t _M0L5d__hiS146 = (uint16_t)_M0L6_2atmpS842;
          int32_t _M0L6_2atmpS841 = _M0L9remainingS139 % 10;
          int32_t _M0L6_2atmpS840 = 48 + _M0L6_2atmpS841;
          int32_t _M0L5d__loS147 = (uint16_t)_M0L6_2atmpS840;
          int32_t _M0L6_2atmpS837 = _M0L12digit__startS137 + _M0L6offsetS140;
          int32_t _M0L6_2atmpS836 = _M0L6_2atmpS837 - 2;
          int32_t _M0L6_2atmpS839;
          int32_t _M0L6_2atmpS838;
          _M0L6bufferS136[_M0L6_2atmpS836] = _M0L5d__hiS146;
          _M0L6_2atmpS839 = _M0L12digit__startS137 + _M0L6offsetS140;
          _M0L6_2atmpS838 = _M0L6_2atmpS839 - 1;
          _M0L6bufferS136[_M0L6_2atmpS838] = _M0L5d__loS147;
        } else {
          int32_t _M0L6_2atmpS847 = _M0L12digit__startS137 + _M0L6offsetS140;
          int32_t _M0L6_2atmpS844 = _M0L6_2atmpS847 - 1;
          int32_t _M0L6_2atmpS846 = 48 + _M0L9remainingS139;
          int32_t _M0L6_2atmpS845 = (uint16_t)_M0L6_2atmpS846;
          _M0L6bufferS136[_M0L6_2atmpS844] = _M0L6_2atmpS845;
        }
        break;
      }
    }
    break;
  }
  return 0;
}

int32_t _M0FPB24int__to__string__generic(
  uint16_t* _M0L6bufferS116,
  uint32_t _M0L3numS120,
  int32_t _M0L12digit__startS117,
  int32_t _M0L10total__lenS119,
  int32_t _M0L5radixS110
) {
  uint32_t _M0L4baseS109;
  int32_t _M0L6_2atmpS794;
  int32_t _M0L6_2atmpS793;
  #line 57 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L4baseS109 = *(uint32_t*)&_M0L5radixS110;
  _M0L6_2atmpS794 = _M0L5radixS110 - 1;
  _M0L6_2atmpS793 = _M0L5radixS110 & _M0L6_2atmpS794;
  if (_M0L6_2atmpS793 == 0) {
    int32_t _M0L5shiftS111;
    uint32_t _M0L4maskS112;
    int32_t _M0L6_2atmpS801;
    int32_t _M0L6offsetS113;
    uint32_t _M0L1nS114;
    #line 68 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0L5shiftS111 = moonbit_ctz32(_M0L5radixS110);
    _M0L4maskS112 = _M0L4baseS109 - 1u;
    _M0L6_2atmpS801 = _M0L10total__lenS119 - _M0L12digit__startS117;
    _M0L6offsetS113 = _M0L6_2atmpS801;
    _M0L1nS114 = _M0L3numS120;
    while (1) {
      if (_M0L1nS114 > 0u) {
        uint32_t _M0L6_2atmpS800 = _M0L1nS114 & _M0L4maskS112;
        int32_t _M0L5digitS115 = *(int32_t*)&_M0L6_2atmpS800;
        int32_t _M0L6_2atmpS797 = _M0L12digit__startS117 + _M0L6offsetS113;
        int32_t _M0L6_2atmpS795 = _M0L6_2atmpS797 - 1;
        int32_t _M0L6_2atmpS796 =
          ((moonbit_string_t)moonbit_string_literal_10.data)[_M0L5digitS115];
        int32_t _M0L6_2atmpS798;
        uint32_t _M0L6_2atmpS799;
        _M0L6bufferS116[_M0L6_2atmpS795] = _M0L6_2atmpS796;
        _M0L6_2atmpS798 = _M0L6offsetS113 - 1;
        _M0L6_2atmpS799 = _M0L1nS114 >> (_M0L5shiftS111 & 31);
        _M0L6offsetS113 = _M0L6_2atmpS798;
        _M0L1nS114 = _M0L6_2atmpS799;
        continue;
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS808 = _M0L10total__lenS119 - _M0L12digit__startS117;
    int32_t _M0L6offsetS121 = _M0L6_2atmpS808;
    uint32_t _M0L1nS122 = _M0L3numS120;
    while (1) {
      if (_M0L1nS122 > 0u) {
        uint32_t _M0L1qS123 = _M0L1nS122 / _M0L4baseS109;
        uint32_t _M0L6_2atmpS807 = _M0L1qS123 * _M0L4baseS109;
        uint32_t _M0L6_2atmpS806 = _M0L1nS122 - _M0L6_2atmpS807;
        int32_t _M0L5digitS124 = *(int32_t*)&_M0L6_2atmpS806;
        int32_t _M0L6_2atmpS804 = _M0L12digit__startS117 + _M0L6offsetS121;
        int32_t _M0L6_2atmpS802 = _M0L6_2atmpS804 - 1;
        int32_t _M0L6_2atmpS803 =
          ((moonbit_string_t)moonbit_string_literal_10.data)[_M0L5digitS124];
        int32_t _M0L6_2atmpS805;
        _M0L6bufferS116[_M0L6_2atmpS802] = _M0L6_2atmpS803;
        _M0L6_2atmpS805 = _M0L6offsetS121 - 1;
        _M0L6offsetS121 = _M0L6_2atmpS805;
        _M0L1nS122 = _M0L1qS123;
        continue;
      }
      break;
    }
  }
  return 0;
}

int32_t _M0FPB20int__to__string__hex(
  uint16_t* _M0L6bufferS103,
  uint32_t _M0L3numS108,
  int32_t _M0L12digit__startS104,
  int32_t _M0L10total__lenS107
) {
  int32_t _M0L6_2atmpS792;
  int32_t _M0L6offsetS98;
  uint32_t _M0L1nS99;
  #line 29 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L6_2atmpS792 = _M0L10total__lenS107 - _M0L12digit__startS104;
  _M0L6offsetS98 = _M0L6_2atmpS792;
  _M0L1nS99 = _M0L3numS108;
  while (1) {
    if (_M0L6offsetS98 >= 2) {
      uint32_t _M0L6_2atmpS789 = _M0L1nS99 & 255u;
      int32_t _M0L9byte__valS100 = *(int32_t*)&_M0L6_2atmpS789;
      int32_t _M0L2hiS101 = _M0L9byte__valS100 / 16;
      int32_t _M0L2loS102 = _M0L9byte__valS100 % 16;
      int32_t _M0L6_2atmpS783 = _M0L12digit__startS104 + _M0L6offsetS98;
      int32_t _M0L6_2atmpS781 = _M0L6_2atmpS783 - 2;
      int32_t _M0L6_2atmpS782 =
        ((moonbit_string_t)moonbit_string_literal_10.data)[_M0L2hiS101];
      int32_t _M0L6_2atmpS786;
      int32_t _M0L6_2atmpS784;
      int32_t _M0L6_2atmpS785;
      int32_t _M0L6_2atmpS787;
      uint32_t _M0L6_2atmpS788;
      _M0L6bufferS103[_M0L6_2atmpS781] = _M0L6_2atmpS782;
      _M0L6_2atmpS786 = _M0L12digit__startS104 + _M0L6offsetS98;
      _M0L6_2atmpS784 = _M0L6_2atmpS786 - 1;
      _M0L6_2atmpS785
      = ((moonbit_string_t)moonbit_string_literal_10.data)[
        _M0L2loS102
      ];
      _M0L6bufferS103[_M0L6_2atmpS784] = _M0L6_2atmpS785;
      _M0L6_2atmpS787 = _M0L6offsetS98 - 2;
      _M0L6_2atmpS788 = _M0L1nS99 >> 8;
      _M0L6offsetS98 = _M0L6_2atmpS787;
      _M0L1nS99 = _M0L6_2atmpS788;
      continue;
    } else if (_M0L6offsetS98 == 1) {
      uint32_t _M0L6_2atmpS791 = _M0L1nS99 & 15u;
      int32_t _M0L6nibbleS106 = *(int32_t*)&_M0L6_2atmpS791;
      int32_t _M0L6_2atmpS790 =
        ((moonbit_string_t)moonbit_string_literal_10.data)[_M0L6nibbleS106];
      _M0L6bufferS103[_M0L12digit__startS104] = _M0L6_2atmpS790;
    }
    break;
  }
  return 0;
}

int32_t _M0IP016_24default__implPB4Show6outputGiE(
  int32_t _M0L4selfS91,
  struct _M0TPB6Logger _M0L6loggerS90
) {
  moonbit_string_t _M0L6_2atmpS777;
  #line 159 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  #line 160 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS777 = _M0IPC13int3IntPB4Show10to__string(_M0L4selfS91);
  #line 160 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6loggerS90.$0->$method_0(_M0L6loggerS90.$1, _M0L6_2atmpS777);
  moonbit_decref(_M0L6_2atmpS777);
  return 0;
}

int32_t _M0IP016_24default__implPB4Show6outputGfE(
  float _M0L4selfS93,
  struct _M0TPB6Logger _M0L6loggerS92
) {
  moonbit_string_t _M0L6_2atmpS778;
  #line 159 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  #line 160 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS778 = _M0IPC15float5FloatPB4Show10to__string(_M0L4selfS93);
  #line 160 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6loggerS92.$0->$method_0(_M0L6loggerS92.$1, _M0L6_2atmpS778);
  moonbit_decref(_M0L6_2atmpS778);
  return 0;
}

int32_t _M0IP016_24default__implPB4Show6outputGbE(
  int32_t _M0L4selfS95,
  struct _M0TPB6Logger _M0L6loggerS94
) {
  moonbit_string_t _M0L6_2atmpS779;
  #line 159 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  #line 160 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS779 = _M0IPC14bool4BoolPB4Show10to__string(_M0L4selfS95);
  #line 160 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6loggerS94.$0->$method_0(_M0L6loggerS94.$1, _M0L6_2atmpS779);
  moonbit_decref(_M0L6_2atmpS779);
  return 0;
}

int32_t _M0IP016_24default__implPB4Show6outputGmE(
  uint64_t _M0L4selfS97,
  struct _M0TPB6Logger _M0L6loggerS96
) {
  moonbit_string_t _M0L6_2atmpS780;
  #line 159 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  #line 160 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS780 = _M0IPC16uint646UInt64PB4Show10to__string(_M0L4selfS97);
  #line 160 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6loggerS96.$0->$method_0(_M0L6loggerS96.$1, _M0L6_2atmpS780);
  moonbit_decref(_M0L6_2atmpS780);
  return 0;
}

int32_t _M0MPC16string10StringView13start__offset(
  struct _M0TPC16string10StringView _M0L4selfS89
) {
  #line 99 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
  return _M0L4selfS89.$1;
}

moonbit_string_t _M0MPC16string10StringView4data(
  struct _M0TPC16string10StringView _M0L4selfS88
) {
  moonbit_string_t _M0L8_2afieldS1392;
  #line 92 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
  _M0L8_2afieldS1392 = _M0L4selfS88.$0;
  moonbit_incref(_M0L8_2afieldS1392);
  return _M0L8_2afieldS1392;
}

int32_t _M0IP016_24default__implPB6Logger16write__substringGRPB13StringBuilderE(
  struct _M0TPB13StringBuilder* _M0L4selfS84,
  moonbit_string_t _M0L5valueS85,
  int32_t _M0L5startS86,
  int32_t _M0L3lenS87
) {
  int32_t _M0L6_2atmpS776;
  int64_t _M0L6_2atmpS775;
  struct _M0TPC16string10StringView _M0L6_2atmpS774;
  #line 122 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS776 = _M0L5startS86 + _M0L3lenS87;
  _M0L6_2atmpS775 = (int64_t)_M0L6_2atmpS776;
  #line 123 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS774
  = _M0MPC16string6String11sub_2einner(_M0L5valueS85, _M0L5startS86, _M0L6_2atmpS775);
  #line 123 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0IPB13StringBuilderPB6Logger11write__view(_M0L4selfS84, _M0L6_2atmpS774);
  moonbit_decref(_M0L6_2atmpS774.$0);
  return 0;
}

struct _M0TPC16string10StringView _M0MPC16string6String11sub_2einner(
  moonbit_string_t _M0L4selfS76,
  int32_t _M0L5startS83,
  int64_t _M0L3endS80
) {
  int32_t _M0L3lenS75;
  int32_t _M0L3endS79;
  int32_t _M0L3endS77;
  #line 923 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
  _M0L3lenS75 = Moonbit_array_length(_M0L4selfS76);
  if (_M0L3endS80 == 4294967296ll) {
    _M0L3endS79 = _M0L3lenS75;
    goto join_78;
  } else {
    int64_t _M0L7_2aSomeS81 = _M0L3endS80;
    int32_t _M0L6_2aendS82 = (int32_t)_M0L7_2aSomeS81;
    _M0L3endS79 = _M0L6_2aendS82;
    goto join_78;
  }
  goto joinlet_1468;
  join_78:;
  _M0L3endS77 = _M0L3endS79;
  joinlet_1468:;
  if (
    _M0L5startS83 >= 0
    && _M0L5startS83 <= _M0L3endS77
    && _M0L3endS77 <= _M0L3lenS75
  ) {
    if (_M0L5startS83 < _M0L3lenS75) {
      int32_t _M0L6_2atmpS771 = _M0L4selfS76[_M0L5startS83];
      int32_t _M0L6_2atmpS770;
      #line 931 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
      _M0L6_2atmpS770
      = _M0MPC16uint166UInt1623is__trailing__surrogate(_M0L6_2atmpS771);
      if (!_M0L6_2atmpS770) {
        
      } else {
        #line 931 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
        moonbit_panic();
      }
    }
    if (_M0L3endS77 < _M0L3lenS75) {
      int32_t _M0L6_2atmpS773 = _M0L4selfS76[_M0L3endS77];
      int32_t _M0L6_2atmpS772;
      #line 934 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
      _M0L6_2atmpS772
      = _M0MPC16uint166UInt1623is__trailing__surrogate(_M0L6_2atmpS773);
      if (!_M0L6_2atmpS772) {
        
      } else {
        #line 934 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
        moonbit_panic();
      }
    }
    moonbit_incref(_M0L4selfS76);
    return (struct _M0TPC16string10StringView){.$0 = _M0L4selfS76,
                                                 .$1 = _M0L5startS83,
                                                 .$2 = _M0L3endS77};
  } else {
    #line 929 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
    moonbit_panic();
  }
}

int32_t _M0IP016_24default__implPB6Logger5writeGRPB13StringBuilderE(
  struct _M0TPB13StringBuilder* _M0L4selfS74,
  struct _M0TPB4Show _M0L4showS73
) {
  struct _M0TPB6Logger _M0L6_2atmpS769;
  #line 116 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  moonbit_incref(_M0L4selfS74);
  _M0L6_2atmpS769
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS74
  };
  #line 117 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L4showS73.$0->$method_0(_M0L4showS73.$1, _M0L6_2atmpS769);
  if (_M0L6_2atmpS769.$1) {
    moonbit_decref(_M0L6_2atmpS769.$1);
  }
  return 0;
}

int32_t _M0IP016_24default__implPB6Logger28write__string__interpolationGRPB13StringBuilderE(
  struct _M0TPB13StringBuilder* _M0L4selfS72,
  struct _M0TPB4Show _M0L4showS71
) {
  struct _M0TPB6Logger _M0L6_2atmpS768;
  #line 111 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  moonbit_incref(_M0L4selfS72);
  _M0L6_2atmpS768
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS72
  };
  #line 112 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L4showS71.$0->$method_0(_M0L4showS71.$1, _M0L6_2atmpS768);
  if (_M0L6_2atmpS768.$1) {
    moonbit_decref(_M0L6_2atmpS768.$1);
  }
  return 0;
}

uint64_t _M0MPC13int3Int10to__uint64(int32_t _M0L4selfS70) {
  int64_t _M0L6_2atmpS767;
  #line 989 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS767 = (int64_t)_M0L4selfS70;
  return *(uint64_t*)&_M0L6_2atmpS767;
}

int32_t _M0IPC16uint166UInt16PB7Default7default() {
  #line 201 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uint16_char.mbt"
  return 0;
}

int32_t _M0MPC16uint166UInt1623is__trailing__surrogate(int32_t _M0L4selfS69) {
  #line 58 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uint16_char.mbt"
  return _M0L4selfS69 >= 56320 && _M0L4selfS69 <= 57343;
}

int32_t _M0IPB13StringBuilderPB6Logger13write__string(
  struct _M0TPB13StringBuilder* _M0L4selfS68,
  moonbit_string_t _M0L3strS66
) {
  int32_t _M0L8str__lenS65;
  int32_t _M0L3lenS766;
  int32_t _M0L8requiredS67;
  uint16_t* _M0L4dataS761;
  int32_t _M0L6_2atmpS760;
  int32_t _if__result_1469;
  uint16_t* _M0L4dataS762;
  int32_t _M0L3lenS763;
  int32_t _M0L3lenS765;
  int32_t _M0L6_2atmpS764;
  #line 105 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L8str__lenS65 = Moonbit_array_length(_M0L3strS66);
  if (_M0L8str__lenS65 == 0) {
    return 0;
  }
  _M0L3lenS766 = _M0L4selfS68->$1;
  _M0L8requiredS67 = _M0L3lenS766 + _M0L8str__lenS65;
  _M0L4dataS761 = _M0L4selfS68->$0;
  _M0L6_2atmpS760 = Moonbit_array_length(_M0L4dataS761);
  if (_M0L8requiredS67 > _M0L6_2atmpS760) {
    _if__result_1469 = 1;
  } else {
    int32_t _M0L3lenS759 = _M0L4selfS68->$1;
    _if__result_1469 = _M0L8requiredS67 < _M0L3lenS759;
  }
  if (_if__result_1469) {
    #line 112 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0MPB13StringBuilder4grow(_M0L4selfS68, _M0L8requiredS67);
  }
  _M0L4dataS762 = _M0L4selfS68->$0;
  _M0L3lenS763 = _M0L4selfS68->$1;
  moonbit_incref(_M0L4dataS762);
  #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0MPC15array10FixedArray26unsafe__blit__from__string(_M0L4dataS762, _M0L3lenS763, _M0L3strS66, 0, _M0L8str__lenS65);
  moonbit_decref(_M0L4dataS762);
  _M0L3lenS765 = _M0L4selfS68->$1;
  _M0L6_2atmpS764 = _M0L3lenS765 + _M0L8str__lenS65;
  _M0L4selfS68->$1 = _M0L6_2atmpS764;
  return 0;
}

int32_t _M0MPC15array10FixedArray26unsafe__blit__from__string(
  uint16_t* _M0L4selfS61,
  int32_t _M0L11dst__offsetS64,
  moonbit_string_t _M0L3strS62,
  int32_t _M0L11str__offsetS57,
  int32_t _M0L3lenS58
) {
  int32_t _M0L16end__str__offsetS56;
  int32_t _M0L1iS59;
  int32_t _M0L1jS60;
  #line 90 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L16end__str__offsetS56 = _M0L11str__offsetS57 + _M0L3lenS58;
  _M0L1iS59 = _M0L11str__offsetS57;
  _M0L1jS60 = _M0L11dst__offsetS64;
  while (1) {
    if (_M0L1iS59 < _M0L16end__str__offsetS56) {
      int32_t _M0L6_2atmpS756 = _M0L3strS62[_M0L1iS59];
      int32_t _M0L6_2atmpS757;
      int32_t _M0L6_2atmpS758;
      _M0L4selfS61[_M0L1jS60] = _M0L6_2atmpS756;
      _M0L6_2atmpS757 = _M0L1iS59 + 1;
      _M0L6_2atmpS758 = _M0L1jS60 + 1;
      _M0L1iS59 = _M0L6_2atmpS757;
      _M0L1jS60 = _M0L6_2atmpS758;
      continue;
    }
    break;
  }
  return 0;
}

int32_t _M0IPB13StringBuilderPB6Logger11write__char(
  struct _M0TPB13StringBuilder* _M0L4selfS54,
  int32_t _M0L2chS53
) {
  uint32_t _M0L4codeS52;
  #line 120 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  #line 121 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L4codeS52 = _M0MPC14char4Char8to__uint(_M0L2chS53);
  if (_M0L4codeS52 <= 65535u) {
    int32_t _M0L3lenS727 = _M0L4selfS54->$1;
    uint16_t* _M0L4dataS729 = _M0L4selfS54->$0;
    int32_t _M0L6_2atmpS728 = Moonbit_array_length(_M0L4dataS729);
    uint16_t* _M0L4dataS732;
    int32_t _M0L3lenS733;
    int32_t _M0L6_2atmpS734;
    int32_t _M0L3lenS736;
    int32_t _M0L6_2atmpS735;
    if (_M0L3lenS727 >= _M0L6_2atmpS728) {
      int32_t _M0L3lenS731 = _M0L4selfS54->$1;
      int32_t _M0L6_2atmpS730 = _M0L3lenS731 + 1;
      #line 124 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0MPB13StringBuilder4grow(_M0L4selfS54, _M0L6_2atmpS730);
    }
    _M0L4dataS732 = _M0L4selfS54->$0;
    _M0L3lenS733 = _M0L4selfS54->$1;
    moonbit_incref(_M0L4dataS732);
    #line 126 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0L6_2atmpS734 = _M0MPC14uint4UInt10to__uint16(_M0L4codeS52);
    if (
      _M0L3lenS733 < 0 || _M0L3lenS733 >= Moonbit_array_length(_M0L4dataS732)
    ) {
      #line 126 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      moonbit_panic();
    }
    _M0L4dataS732[_M0L3lenS733] = _M0L6_2atmpS734;
    moonbit_decref(_M0L4dataS732);
    _M0L3lenS736 = _M0L4selfS54->$1;
    _M0L6_2atmpS735 = _M0L3lenS736 + 1;
    _M0L4selfS54->$1 = _M0L6_2atmpS735;
  } else if (_M0L4codeS52 <= 1114111u) {
    uint16_t* _M0L4dataS740 = _M0L4selfS54->$0;
    int32_t _M0L6_2atmpS738 = Moonbit_array_length(_M0L4dataS740);
    int32_t _M0L3lenS739 = _M0L4selfS54->$1;
    int32_t _M0L6_2atmpS737 = _M0L6_2atmpS738 - _M0L3lenS739;
    uint32_t _M0L4codeS55;
    uint16_t* _M0L4dataS743;
    int32_t _M0L3lenS744;
    uint32_t _M0L6_2atmpS747;
    uint32_t _M0L6_2atmpS746;
    int32_t _M0L6_2atmpS745;
    uint16_t* _M0L4dataS748;
    int32_t _M0L3lenS753;
    int32_t _M0L6_2atmpS749;
    uint32_t _M0L6_2atmpS752;
    uint32_t _M0L6_2atmpS751;
    int32_t _M0L6_2atmpS750;
    int32_t _M0L3lenS755;
    int32_t _M0L6_2atmpS754;
    if (_M0L6_2atmpS737 < 2) {
      int32_t _M0L3lenS742 = _M0L4selfS54->$1;
      int32_t _M0L6_2atmpS741 = _M0L3lenS742 + 2;
      #line 130 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0MPB13StringBuilder4grow(_M0L4selfS54, _M0L6_2atmpS741);
    }
    _M0L4codeS55 = _M0L4codeS52 - 65536u;
    _M0L4dataS743 = _M0L4selfS54->$0;
    _M0L3lenS744 = _M0L4selfS54->$1;
    _M0L6_2atmpS747 = _M0L4codeS55 >> 10;
    _M0L6_2atmpS746 = 55296u + _M0L6_2atmpS747;
    moonbit_incref(_M0L4dataS743);
    #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0L6_2atmpS745 = _M0MPC14uint4UInt10to__uint16(_M0L6_2atmpS746);
    if (
      _M0L3lenS744 < 0 || _M0L3lenS744 >= Moonbit_array_length(_M0L4dataS743)
    ) {
      #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      moonbit_panic();
    }
    _M0L4dataS743[_M0L3lenS744] = _M0L6_2atmpS745;
    moonbit_decref(_M0L4dataS743);
    _M0L4dataS748 = _M0L4selfS54->$0;
    _M0L3lenS753 = _M0L4selfS54->$1;
    _M0L6_2atmpS749 = _M0L3lenS753 + 1;
    _M0L6_2atmpS752 = _M0L4codeS55 & 1023u;
    _M0L6_2atmpS751 = 56320u + _M0L6_2atmpS752;
    moonbit_incref(_M0L4dataS748);
    #line 134 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0L6_2atmpS750 = _M0MPC14uint4UInt10to__uint16(_M0L6_2atmpS751);
    if (
      _M0L6_2atmpS749 < 0
      || _M0L6_2atmpS749 >= Moonbit_array_length(_M0L4dataS748)
    ) {
      #line 134 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      moonbit_panic();
    }
    _M0L4dataS748[_M0L6_2atmpS749] = _M0L6_2atmpS750;
    moonbit_decref(_M0L4dataS748);
    _M0L3lenS755 = _M0L4selfS54->$1;
    _M0L6_2atmpS754 = _M0L3lenS755 + 2;
    _M0L4selfS54->$1 = _M0L6_2atmpS754;
  } else {
    #line 137 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_11.data);
  }
  return 0;
}

int32_t _M0MPB13StringBuilder4grow(
  struct _M0TPB13StringBuilder* _M0L4selfS49,
  int32_t _M0L8requiredS50
) {
  uint16_t* _M0L4dataS726;
  int32_t _M0L6_2atmpS724;
  int32_t _M0L3lenS725;
  int32_t _M0L13new__capacityS48;
  uint16_t* _M0L4dataS721;
  int32_t _M0L6_2atmpS722;
  int32_t _M0L3lenS723;
  uint16_t* _M0L9new__dataS51;
  uint16_t* _M0L6_2aoldS1393;
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L4dataS726 = _M0L4selfS49->$0;
  _M0L6_2atmpS724 = Moonbit_array_length(_M0L4dataS726);
  _M0L3lenS725 = _M0L4selfS49->$1;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L13new__capacityS48
  = _M0FPB31stringbuilder__growth__capacity(_M0L6_2atmpS724, _M0L3lenS725, _M0L8requiredS50);
  _M0L4dataS721 = _M0L4selfS49->$0;
  moonbit_incref(_M0L4dataS721);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L6_2atmpS722 = _M0IPC16uint166UInt16PB7Default7default();
  _M0L3lenS723 = _M0L4selfS49->$1;
  #line 80 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L9new__dataS51
  = _M0MPC15array10FixedArray23make__and__blit_2einnerGkE(_M0L4dataS721, _M0L13new__capacityS48, _M0L6_2atmpS722, _M0L3lenS723, 0, 0);
  _M0L6_2aoldS1393 = _M0L4selfS49->$0;
  moonbit_decref(_M0L6_2aoldS1393);
  _M0L4selfS49->$0 = _M0L9new__dataS51;
  return 0;
}

int32_t _M0FPB31stringbuilder__growth__capacity(
  int32_t _M0L7currentS47,
  int32_t _M0L3lenS43,
  int32_t _M0L8requiredS42
) {
  int32_t _M0L5spaceS44;
  #line 48 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  if (_M0L8requiredS42 < _M0L3lenS43) {
    #line 55 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_12.data);
  }
  _M0L5spaceS44 = _M0L7currentS47;
  while (1) {
    if (_M0L5spaceS44 < _M0L8requiredS42) {
      int32_t _M0L4nextS45 = _M0L5spaceS44 * 2;
      if (_M0L4nextS45 <= _M0L5spaceS44) {
        return _M0L8requiredS42;
      }
      _M0L5spaceS44 = _M0L4nextS45;
      continue;
    } else {
      return _M0L5spaceS44;
    }
    break;
  }
}

int32_t _M0MPC14uint4UInt10to__uint16(uint32_t _M0L4selfS41) {
  int32_t _M0L6_2atmpS720;
  #line 2785 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS720 = *(int32_t*)&_M0L4selfS41;
  return (uint16_t)_M0L6_2atmpS720;
}

uint32_t _M0MPC14char4Char8to__uint(int32_t _M0L4selfS40) {
  int32_t _M0L6_2atmpS719;
  #line 1335 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS719 = _M0L4selfS40;
  return *(uint32_t*)&_M0L6_2atmpS719;
}

moonbit_string_t _M0MPB13StringBuilder10to__string(
  struct _M0TPB13StringBuilder* _M0L4selfS38
) {
  int32_t _M0L3lenS710;
  #line 181 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L3lenS710 = _M0L4selfS38->$1;
  if (_M0L3lenS710 == 0) {
    return (moonbit_string_t)moonbit_string_literal_4.data;
  } else {
    int32_t _M0L3lenS711 = _M0L4selfS38->$1;
    uint16_t* _M0L4dataS713 = _M0L4selfS38->$0;
    int32_t _M0L6_2atmpS712 = Moonbit_array_length(_M0L4dataS713);
    if (_M0L3lenS711 == _M0L6_2atmpS712) {
      uint16_t* _M0L4dataS714 = _M0L4selfS38->$0;
      moonbit_incref(_M0L4dataS714);
      return _M0L4dataS714;
    } else {
      uint16_t* _M0L4dataS715 = _M0L4selfS38->$0;
      int32_t _M0L3lenS716 = _M0L4selfS38->$1;
      int32_t _M0L6_2atmpS717;
      int32_t _M0L3lenS718;
      uint16_t* _M0L4dataS39;
      moonbit_incref(_M0L4dataS715);
      #line 190 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0L6_2atmpS717 = _M0IPC16uint166UInt16PB7Default7default();
      _M0L3lenS718 = _M0L4selfS38->$1;
      #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0L4dataS39
      = _M0MPC15array10FixedArray23make__and__blit_2einnerGkE(_M0L4dataS715, _M0L3lenS716, _M0L6_2atmpS717, _M0L3lenS718, 0, 0);
      return _M0L4dataS39;
    }
  }
}

uint16_t* _M0MPC15array10FixedArray23make__and__blit_2einnerGkE(
  uint16_t* _M0L3srcS35,
  int32_t _M0L13allocate__lenS31,
  int32_t _M0L4initS36,
  int32_t _M0L3lenS32,
  int32_t _M0L11src__offsetS33,
  int32_t _M0L11dst__offsetS34
) {
  int32_t _if__result_1472;
  #line 97 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
  if (_M0L13allocate__lenS31 >= 0) {
    if (_M0L3lenS32 >= 0) {
      if (_M0L11src__offsetS33 >= 0) {
        if (_M0L11dst__offsetS34 >= 0) {
          int32_t _M0L6_2atmpS706 = _M0L11src__offsetS33 + _M0L3lenS32;
          int32_t _M0L6_2atmpS707 = Moonbit_array_length(_M0L3srcS35);
          if (_M0L6_2atmpS706 <= _M0L6_2atmpS707) {
            int32_t _M0L6_2atmpS705 = _M0L11dst__offsetS34 + _M0L3lenS32;
            _if__result_1472 = _M0L6_2atmpS705 <= _M0L13allocate__lenS31;
          } else {
            _if__result_1472 = 0;
          }
        } else {
          _if__result_1472 = 0;
        }
      } else {
        _if__result_1472 = 0;
      }
    } else {
      _if__result_1472 = 0;
    }
  } else {
    _if__result_1472 = 0;
  }
  if (_if__result_1472) {
    #line 116 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    return _M0MPC15array10FixedArray23unsafe__make__and__blitGkE(_M0L3srcS35, _M0L13allocate__lenS31, _M0L4initS36, _M0L11src__offsetS33, _M0L11dst__offsetS34, _M0L3lenS32);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS37;
    int32_t _M0L6_2atmpS709;
    moonbit_string_t _M0L6_2atmpS708;
    uint16_t* _result_1473;
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0L18_2astring__builderS37
    = _M0MPB13StringBuilder21StringBuilder_2einner(89);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS37, (moonbit_string_t)moonbit_string_literal_13.data);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS37, _M0L13allocate__lenS31);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS37, (moonbit_string_t)moonbit_string_literal_14.data);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS37, _M0L11src__offsetS33);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS37, (moonbit_string_t)moonbit_string_literal_15.data);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS37, _M0L11dst__offsetS34);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS37, (moonbit_string_t)moonbit_string_literal_16.data);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS37, _M0L3lenS32);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS37, (moonbit_string_t)moonbit_string_literal_17.data);
    _M0L6_2atmpS709 = Moonbit_array_length(_M0L3srcS35);
    moonbit_decref(_M0L3srcS35);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS37, _M0L6_2atmpS709);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0L6_2atmpS708
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS37);
    moonbit_decref(_M0L18_2astring__builderS37);
    #line 112 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _result_1473 = _M0FPC15abort5abortGAkE(_M0L6_2atmpS708);
    moonbit_decref(_M0L6_2atmpS708);
    return _result_1473;
  }
}

uint16_t* _M0MPC15array10FixedArray23unsafe__make__and__blitGkE(
  uint16_t* _M0L3srcS28,
  int32_t _M0L13allocate__lenS25,
  int32_t _M0L4initS26,
  int32_t _M0L11src__offsetS29,
  int32_t _M0L11dst__offsetS27,
  int32_t _M0L9blit__lenS30
) {
  uint16_t* _M0L3dstS24;
  #line 79 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
  _M0L3dstS24
  = (uint16_t*)moonbit_make_string(_M0L13allocate__lenS25, _M0L4initS26);
  moonbit_incref(_M0L3dstS24);
  #line 90 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
  moonbit_unsafe_val_array_blit(_M0L3dstS24, _M0L11dst__offsetS27, _M0L3srcS28, _M0L11src__offsetS29, _M0L9blit__lenS30, sizeof(uint16_t));
  return _M0L3dstS24;
}

struct _M0TPB13StringBuilder* _M0MPB13StringBuilder21StringBuilder_2einner(
  int32_t _M0L10size__hintS22
) {
  int32_t _M0L7initialS21;
  uint16_t* _M0L4dataS23;
  struct _M0TPB13StringBuilder* _block_1474;
  #line 32 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  if (_M0L10size__hintS22 < 1) {
    _M0L7initialS21 = 1;
  } else {
    int32_t _M0L6_2atmpS704 = _M0L10size__hintS22 + 1;
    _M0L7initialS21 = _M0L6_2atmpS704 / 2;
  }
  _M0L4dataS23 = (uint16_t*)moonbit_make_string(_M0L7initialS21, 0);
  _block_1474
  = (struct _M0TPB13StringBuilder*)moonbit_malloc(sizeof(struct _M0TPB13StringBuilder));
  Moonbit_object_header(_block_1474)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 16, 0);
  _block_1474->$0 = _M0L4dataS23;
  _block_1474->$1 = 0;
  return _block_1474;
}

int32_t _M0MPB13StringBuilder13write__objectGiE(
  struct _M0TPB13StringBuilder* _M0L4selfS14,
  int32_t _M0L3objS13
) {
  struct _M0TPB6Logger _M0L6_2atmpS700;
  #line 17 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  moonbit_incref(_M0L4selfS14);
  _M0L6_2atmpS700
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS14
  };
  #line 23 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  _M0IP016_24default__implPB4Show6outputGiE(_M0L3objS13, _M0L6_2atmpS700);
  if (_M0L6_2atmpS700.$1) {
    moonbit_decref(_M0L6_2atmpS700.$1);
  }
  return 0;
}

int32_t _M0MPB13StringBuilder13write__objectGfE(
  struct _M0TPB13StringBuilder* _M0L4selfS16,
  float _M0L3objS15
) {
  struct _M0TPB6Logger _M0L6_2atmpS701;
  #line 17 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  moonbit_incref(_M0L4selfS16);
  _M0L6_2atmpS701
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS16
  };
  #line 23 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  _M0IP016_24default__implPB4Show6outputGfE(_M0L3objS15, _M0L6_2atmpS701);
  if (_M0L6_2atmpS701.$1) {
    moonbit_decref(_M0L6_2atmpS701.$1);
  }
  return 0;
}

int32_t _M0MPB13StringBuilder13write__objectGbE(
  struct _M0TPB13StringBuilder* _M0L4selfS18,
  int32_t _M0L3objS17
) {
  struct _M0TPB6Logger _M0L6_2atmpS702;
  #line 17 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  moonbit_incref(_M0L4selfS18);
  _M0L6_2atmpS702
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS18
  };
  #line 23 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  _M0IP016_24default__implPB4Show6outputGbE(_M0L3objS17, _M0L6_2atmpS702);
  if (_M0L6_2atmpS702.$1) {
    moonbit_decref(_M0L6_2atmpS702.$1);
  }
  return 0;
}

int32_t _M0MPB13StringBuilder13write__objectGmE(
  struct _M0TPB13StringBuilder* _M0L4selfS20,
  uint64_t _M0L3objS19
) {
  struct _M0TPB6Logger _M0L6_2atmpS703;
  #line 17 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  moonbit_incref(_M0L4selfS20);
  _M0L6_2atmpS703
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS20
  };
  #line 23 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  _M0IP016_24default__implPB4Show6outputGmE(_M0L3objS19, _M0L6_2atmpS703);
  if (_M0L6_2atmpS703.$1) {
    moonbit_decref(_M0L6_2atmpS703.$1);
  }
  return 0;
}

int32_t _M0MPC15array10FixedArray12unsafe__blitGkE(
  uint16_t* _M0L3dstS4,
  int32_t _M0L11dst__offsetS6,
  uint16_t* _M0L3srcS5,
  int32_t _M0L11src__offsetS7,
  int32_t _M0L3lenS9
) {
  #line 38 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
  if (_M0L3dstS4 == _M0L3srcS5 && _M0L11dst__offsetS6 < _M0L11src__offsetS7) {
    int32_t _M0L1iS8 = 0;
    while (1) {
      if (_M0L1iS8 < _M0L3lenS9) {
        int32_t _M0L6_2atmpS691 = _M0L11dst__offsetS6 + _M0L1iS8;
        int32_t _M0L6_2atmpS693 = _M0L11src__offsetS7 + _M0L1iS8;
        int32_t _M0L6_2atmpS692;
        int32_t _M0L6_2atmpS694;
        if (
          _M0L6_2atmpS693 < 0
          || _M0L6_2atmpS693 >= Moonbit_array_length(_M0L3srcS5)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS692 = (int32_t)_M0L3srcS5[_M0L6_2atmpS693];
        if (
          _M0L6_2atmpS691 < 0
          || _M0L6_2atmpS691 >= Moonbit_array_length(_M0L3dstS4)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS4[_M0L6_2atmpS691] = _M0L6_2atmpS692;
        _M0L6_2atmpS694 = _M0L1iS8 + 1;
        _M0L1iS8 = _M0L6_2atmpS694;
        continue;
      } else {
        moonbit_decref(_M0L3srcS5);
        moonbit_decref(_M0L3dstS4);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS699 = _M0L3lenS9 - 1;
    int32_t _M0L1iS11 = _M0L6_2atmpS699;
    while (1) {
      if (_M0L1iS11 >= 0) {
        int32_t _M0L6_2atmpS695 = _M0L11dst__offsetS6 + _M0L1iS11;
        int32_t _M0L6_2atmpS697 = _M0L11src__offsetS7 + _M0L1iS11;
        int32_t _M0L6_2atmpS696;
        int32_t _M0L6_2atmpS698;
        if (
          _M0L6_2atmpS697 < 0
          || _M0L6_2atmpS697 >= Moonbit_array_length(_M0L3srcS5)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS696 = (int32_t)_M0L3srcS5[_M0L6_2atmpS697];
        if (
          _M0L6_2atmpS695 < 0
          || _M0L6_2atmpS695 >= Moonbit_array_length(_M0L3dstS4)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS4[_M0L6_2atmpS695] = _M0L6_2atmpS696;
        _M0L6_2atmpS698 = _M0L1iS11 - 1;
        _M0L1iS11 = _M0L6_2atmpS698;
        continue;
      } else {
        moonbit_decref(_M0L3srcS5);
        moonbit_decref(_M0L3dstS4);
      }
      break;
    }
  }
  return 0;
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

int32_t _M0FPC15abort5abortGiE(moonbit_string_t _M0L3msgS3) {
  #line 47 "C:\\Users\\31379\\.moon\\lib\\core\\abort\\abort.mbt"
  #line 49 "C:\\Users\\31379\\.moon\\lib\\core\\abort\\abort.mbt"
  moonbit_println(_M0L3msgS3);
  #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\abort\\abort.mbt"
  moonbit_panic();
}

int32_t _M0IP016_24default__implPB6Logger61write_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLoggerGRPB13StringBuilderE(
  void* _M0L11_2aobj__ptrS642,
  struct _M0TPB4Show _M0L8_2aparamS641
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS640 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS642;
  _M0IP016_24default__implPB6Logger5writeGRPB13StringBuilderE(_M0L7_2aselfS640, _M0L8_2aparamS641);
  return 0;
}

int32_t _M0IP016_24default__implPB6Logger84write__string__interpolation_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLoggerGRPB13StringBuilderE(
  void* _M0L11_2aobj__ptrS639,
  struct _M0TPB4Show _M0L8_2aparamS638
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS637 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS639;
  _M0IP016_24default__implPB6Logger28write__string__interpolationGRPB13StringBuilderE(_M0L7_2aselfS637, _M0L8_2aparamS638);
  return 0;
}

int32_t _M0IPB13StringBuilderPB6Logger67write__char_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLogger(
  void* _M0L11_2aobj__ptrS636,
  int32_t _M0L8_2aparamS635
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS634 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS636;
  _M0IPB13StringBuilderPB6Logger11write__char(_M0L7_2aselfS634, _M0L8_2aparamS635);
  return 0;
}

int32_t _M0IPB13StringBuilderPB6Logger67write__view_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLogger(
  void* _M0L11_2aobj__ptrS633,
  struct _M0TPC16string10StringView _M0L8_2aparamS632
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS631 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS633;
  _M0IPB13StringBuilderPB6Logger11write__view(_M0L7_2aselfS631, _M0L8_2aparamS632);
  return 0;
}

int32_t _M0IP016_24default__implPB6Logger72write__substring_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLoggerGRPB13StringBuilderE(
  void* _M0L11_2aobj__ptrS630,
  moonbit_string_t _M0L8_2aparamS627,
  int32_t _M0L8_2aparamS628,
  int32_t _M0L8_2aparamS629
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS626 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS630;
  _M0IP016_24default__implPB6Logger16write__substringGRPB13StringBuilderE(_M0L7_2aselfS626, _M0L8_2aparamS627, _M0L8_2aparamS628, _M0L8_2aparamS629);
  return 0;
}

int32_t _M0IPB13StringBuilderPB6Logger69write__string_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLogger(
  void* _M0L11_2aobj__ptrS625,
  moonbit_string_t _M0L8_2aparamS624
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS623 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS625;
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L7_2aselfS623, _M0L8_2aparamS624);
  return 0;
}

void moonbit_init() {
  moonbit_layout_table = moonbit_layout_table_data;
}

int main(int argc, char** argv) {
  int32_t _M0L4n__eS603;
  int32_t _M0L5n__pvS604;
  int32_t _M0L6n__sstS605;
  int64_t _M0L6_2atmpS689;
  struct _M0TP26RiantR8snn__mbt19ExtendedIFParameter* _M0L6_2atmpS690;
  struct _M0TP26RiantR8snn__mbt10ExtendedIF* _M0L4p__eS606;
  struct _M0TP26RiantR8snn__mbt19ExtendedIFParameter* _M0L6_2atmpS688;
  struct _M0TP26RiantR8snn__mbt10ExtendedIF* _M0L5p__pvS607;
  struct _M0TP26RiantR8snn__mbt19ExtendedIFParameter* _M0L6_2atmpS687;
  struct _M0TP26RiantR8snn__mbt10ExtendedIF* _M0L6p__sstS608;
  struct _M0TPB13StringBuilder* _M0L18_2astring__builderS609;
  moonbit_string_t _M0L6_2atmpS643;
  struct _M0TPB13StringBuilder* _M0L18_2astring__builderS610;
  moonbit_string_t _M0L6_2atmpS644;
  struct _M0TPB13StringBuilder* _M0L18_2astring__builderS611;
  moonbit_string_t _M0L6_2atmpS645;
  struct _M0TP26RiantR8snn__mbt10ExtendedIF* _M0L6_2atmpS1394;
  struct _M0TPB5ArrayGfE* _M0L6g__excS647;
  int32_t _M0L6_2atmpS646;
  struct _M0TPB5ArrayGfE* _M0L5g__pvS649;
  int32_t _M0L6_2atmpS648;
  struct _M0TPB5ArrayGfE* _M0L6g__sstS651;
  int32_t _M0L6_2atmpS650;
  struct _M0TPB5ArrayGfE* _M0L1iS653;
  int32_t _M0L6_2atmpS652;
  float _M0L2dtS612;
  struct _M0TPB8MutLocalGiE* _M0L4stepS613;
  struct _M0TPB8MutLocalGiE* _M0L12spike__countS614;
  struct _M0TPB13StringBuilder* _M0L18_2astring__builderS618;
  struct _M0TPB5ArrayGfE* _M0L1vS669;
  float _M0L6_2atmpS668;
  moonbit_string_t _M0L6_2atmpS667;
  struct _M0TPB13StringBuilder* _M0L18_2astring__builderS619;
  struct _M0TPB5ArrayGfE* _M0L6g__excS672;
  float _M0L6_2atmpS671;
  struct _M0TPB5ArrayGfE* _M0L5g__pvS674;
  float _M0L6_2atmpS673;
  struct _M0TPB5ArrayGfE* _M0L6g__sstS676;
  float _M0L6_2atmpS675;
  moonbit_string_t _M0L6_2atmpS670;
  struct _M0TPB13StringBuilder* _M0L18_2astring__builderS620;
  struct _M0TPB5ArrayGbE* _M0L4fireS679;
  int32_t _M0L6_2acntS1395;
  int32_t _M0L6_2atmpS678;
  moonbit_string_t _M0L6_2atmpS677;
  struct _M0TPB13StringBuilder* _M0L18_2astring__builderS621;
  int32_t _M0L3valS681;
  moonbit_string_t _M0L6_2atmpS680;
  struct _M0TPB13StringBuilder* _M0L18_2astring__builderS622;
  struct _M0TPB5ArrayGfE* _M0L1vS684;
  int32_t _M0L6_2acntS1404;
  float _M0L6_2atmpS683;
  struct _M0TPB5ArrayGfE* _M0L1vS686;
  int32_t _M0L6_2acntS1413;
  float _M0L6_2atmpS685;
  moonbit_string_t _M0L6_2atmpS682;
  moonbit_runtime_init(argc, argv);
  moonbit_init();
  _M0L4n__eS603 = 20;
  _M0L5n__pvS604 = 5;
  _M0L6n__sstS605 = 5;
  _M0L6_2atmpS689 = (int64_t)_M0L4n__eS603;
  _M0L6_2atmpS690 = 0;
  #line 19 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_extended\\main.mbt"
  _M0L4p__eS606
  = _M0MP26RiantR8snn__mbt10ExtendedIF3new(_M0L6_2atmpS689, _M0L6_2atmpS690);
  if (_M0L6_2atmpS690) {
    moonbit_decref(_M0L6_2atmpS690);
  }
  #line 22 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_extended\\main.mbt"
  _M0L6_2atmpS688
  = _M0MP26RiantR8snn__mbt19ExtendedIFParameter6custom(0x1.9p+7f, -0x1.9p+5f, -0x1.ep+5f, -0x1.18p+6f, 0x1.4p+3f, 0x1.8p+1f, 0x1.ep+3f, -0x1.2cp+6f, 0x0p+0f, 0x1.8p+1f, 0x0p+0f);
  #line 20 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_extended\\main.mbt"
  _M0L5p__pvS607
  = _M0MP26RiantR8snn__mbt10ExtendedIF11new_2einner(_M0L5n__pvS604, _M0L6_2atmpS688);
  moonbit_decref(_M0L6_2atmpS688);
  #line 30 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_extended\\main.mbt"
  _M0L6_2atmpS687
  = _M0MP26RiantR8snn__mbt19ExtendedIFParameter6custom(0x1.68p+7f, -0x1.68p+5f, -0x1.b8p+5f, -0x1.04p+6f, 0x1.8p+3f, 0x1.8p+2f, 0x1.ep+4f, -0x1.2cp+6f, 0x0p+0f, 0x1p+2f, 0x1p-1f);
  #line 28 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_extended\\main.mbt"
  _M0L6p__sstS608
  = _M0MP26RiantR8snn__mbt10ExtendedIF11new_2einner(_M0L6n__sstS605, _M0L6_2atmpS687);
  moonbit_decref(_M0L6_2atmpS687);
  #line 36 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_extended\\main.mbt"
  _M0FPB7printlnGsE((moonbit_string_t)moonbit_string_literal_18.data);
  #line 37 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_extended\\main.mbt"
  _M0L18_2astring__builderS609
  = _M0MPB13StringBuilder21StringBuilder_2einner(44);
  #line 37 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_extended\\main.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS609, (moonbit_string_t)moonbit_string_literal_19.data);
  #line 37 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_extended\\main.mbt"
  _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS609, _M0L4n__eS603);
  #line 37 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_extended\\main.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS609, (moonbit_string_t)moonbit_string_literal_20.data);
  #line 37 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_extended\\main.mbt"
  _M0L6_2atmpS643
  = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS609);
  moonbit_decref(_M0L18_2astring__builderS609);
  #line 37 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_extended\\main.mbt"
  _M0FPB7printlnGsE(_M0L6_2atmpS643);
  moonbit_decref(_M0L6_2atmpS643);
  #line 38 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_extended\\main.mbt"
  _M0L18_2astring__builderS610
  = _M0MPB13StringBuilder21StringBuilder_2einner(45);
  #line 38 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_extended\\main.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS610, (moonbit_string_t)moonbit_string_literal_21.data);
  #line 38 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_extended\\main.mbt"
  _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS610, _M0L5n__pvS604);
  #line 38 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_extended\\main.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS610, (moonbit_string_t)moonbit_string_literal_22.data);
  #line 38 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_extended\\main.mbt"
  _M0L6_2atmpS644
  = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS610);
  moonbit_decref(_M0L18_2astring__builderS610);
  #line 38 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_extended\\main.mbt"
  _M0FPB7printlnGsE(_M0L6_2atmpS644);
  moonbit_decref(_M0L6_2atmpS644);
  #line 39 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_extended\\main.mbt"
  _M0L18_2astring__builderS611
  = _M0MPB13StringBuilder21StringBuilder_2einner(53);
  #line 39 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_extended\\main.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS611, (moonbit_string_t)moonbit_string_literal_23.data);
  #line 39 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_extended\\main.mbt"
  _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS611, _M0L6n__sstS605);
  #line 39 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_extended\\main.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS611, (moonbit_string_t)moonbit_string_literal_24.data);
  #line 39 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_extended\\main.mbt"
  _M0L6_2atmpS645
  = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS611);
  moonbit_decref(_M0L18_2astring__builderS611);
  #line 39 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_extended\\main.mbt"
  _M0FPB7printlnGsE(_M0L6_2atmpS645);
  moonbit_decref(_M0L6_2atmpS645);
  _M0L6_2atmpS1394 = _M0L4p__eS606;
  _M0L6g__excS647 = _M0L4p__eS606->$3;
  moonbit_incref(_M0L6g__excS647);
  #line 43 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_extended\\main.mbt"
  _M0L6_2atmpS646 = _M0MPC15array5Array3setGfE(_M0L6g__excS647, 0, 0x1.4p+3f);
  moonbit_decref(_M0L6g__excS647);
  _M0L5g__pvS649 = _M0L4p__eS606->$4;
  moonbit_incref(_M0L5g__pvS649);
  #line 45 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_extended\\main.mbt"
  _M0L6_2atmpS648 = _M0MPC15array5Array3setGfE(_M0L5g__pvS649, 0, 0x1p+2f);
  moonbit_decref(_M0L5g__pvS649);
  _M0L6g__sstS651 = _M0L4p__eS606->$5;
  moonbit_incref(_M0L6g__sstS651);
  #line 47 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_extended\\main.mbt"
  _M0L6_2atmpS650 = _M0MPC15array5Array3setGfE(_M0L6g__sstS651, 0, 0x1.8p+1f);
  moonbit_decref(_M0L6g__sstS651);
  _M0L1iS653 = _M0L4p__eS606->$8;
  moonbit_incref(_M0L1iS653);
  #line 49 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_extended\\main.mbt"
  _M0L6_2atmpS652 = _M0MPC15array5Array3setGfE(_M0L1iS653, 0, 0x1.9p+9f);
  moonbit_decref(_M0L1iS653);
  _M0L2dtS612 = 0x1p-3f;
  _M0L4stepS613
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L4stepS613)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L4stepS613->$0 = 0;
  _M0L12spike__countS614
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L12spike__countS614)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L12spike__countS614->$0 = 0;
  while (1) {
    int32_t _M0L3valS654 = _M0L4stepS613->$0;
    if (_M0L3valS654 < 800) {
      struct _M0TP26RiantR8snn__mbt19ExtendedIFParameter* _M0L5paramS655 =
        _M0L4p__eS606->$1;
      struct _M0TP26RiantR8snn__mbt19ExtendedIFParameter* _M0L5paramS656;
      struct _M0TP26RiantR8snn__mbt19ExtendedIFParameter* _M0L5paramS657;
      struct _M0TPB8MutLocalGiE* _M0L1kS615;
      int32_t _M0L3valS666;
      int32_t _M0L6_2atmpS665;
      moonbit_incref(_M0L5paramS655);
      #line 55 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_extended\\main.mbt"
      _M0FP26RiantR8snn__mbt23integrate__extended__if(_M0L4p__eS606, _M0L5paramS655, _M0L2dtS612);
      moonbit_decref(_M0L5paramS655);
      _M0L5paramS656 = _M0L5p__pvS607->$1;
      moonbit_incref(_M0L5paramS656);
      #line 56 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_extended\\main.mbt"
      _M0FP26RiantR8snn__mbt23integrate__extended__if(_M0L5p__pvS607, _M0L5paramS656, _M0L2dtS612);
      moonbit_decref(_M0L5paramS656);
      _M0L5paramS657 = _M0L6p__sstS608->$1;
      moonbit_incref(_M0L5paramS657);
      #line 57 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_extended\\main.mbt"
      _M0FP26RiantR8snn__mbt23integrate__extended__if(_M0L6p__sstS608, _M0L5paramS657, _M0L2dtS612);
      moonbit_decref(_M0L5paramS657);
      _M0L1kS615
      = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
      Moonbit_object_header(_M0L1kS615)->meta
      = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
      _M0L1kS615->$0 = 0;
      while (1) {
        int32_t _M0L3valS658 = _M0L1kS615->$0;
        if (_M0L3valS658 < _M0L4n__eS603) {
          struct _M0TPB5ArrayGbE* _M0L4fireS659 = _M0L4p__eS606->$7;
          int32_t _M0L3valS660 = _M0L1kS615->$0;
          int32_t _result_1479;
          int32_t _M0L3valS664;
          int32_t _M0L6_2atmpS663;
          moonbit_incref(_M0L4fireS659);
          #line 61 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_extended\\main.mbt"
          _result_1479
          = _M0MPC15array5Array2atGbE(_M0L4fireS659, _M0L3valS660);
          moonbit_decref(_M0L4fireS659);
          if (_result_1479) {
            int32_t _M0L3valS662 = _M0L12spike__countS614->$0;
            int32_t _M0L6_2atmpS661 = _M0L3valS662 + 1;
            _M0L12spike__countS614->$0 = _M0L6_2atmpS661;
          }
          _M0L3valS664 = _M0L1kS615->$0;
          _M0L6_2atmpS663 = _M0L3valS664 + 1;
          _M0L1kS615->$0 = _M0L6_2atmpS663;
          continue;
        } else {
          moonbit_decref(_M0L1kS615);
        }
        break;
      }
      _M0L3valS666 = _M0L4stepS613->$0;
      _M0L6_2atmpS665 = _M0L3valS666 + 1;
      _M0L4stepS613->$0 = _M0L6_2atmpS665;
      continue;
    } else {
      moonbit_decref(_M0L4stepS613);
    }
    break;
  }
  #line 68 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_extended\\main.mbt"
  _M0FPB7printlnGsE((moonbit_string_t)moonbit_string_literal_25.data);
  #line 71 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_extended\\main.mbt"
  _M0L18_2astring__builderS618
  = _M0MPB13StringBuilder21StringBuilder_2einner(17);
  #line 71 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_extended\\main.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS618, (moonbit_string_t)moonbit_string_literal_26.data);
  _M0L1vS669 = _M0L4p__eS606->$2;
  moonbit_incref(_M0L1vS669);
  #line 71 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_extended\\main.mbt"
  _M0L6_2atmpS668 = _M0MPC15array5Array2atGfE(_M0L1vS669, 0);
  moonbit_decref(_M0L1vS669);
  #line 71 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_extended\\main.mbt"
  _M0MPB13StringBuilder13write__objectGfE(_M0L18_2astring__builderS618, _M0L6_2atmpS668);
  #line 71 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_extended\\main.mbt"
  _M0L6_2atmpS667
  = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS618);
  moonbit_decref(_M0L18_2astring__builderS618);
  #line 71 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_extended\\main.mbt"
  _M0FPB7printlnGsE(_M0L6_2atmpS667);
  moonbit_decref(_M0L6_2atmpS667);
  #line 73 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_extended\\main.mbt"
  _M0L18_2astring__builderS619
  = _M0MPB13StringBuilder21StringBuilder_2einner(34);
  #line 73 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_extended\\main.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS619, (moonbit_string_t)moonbit_string_literal_27.data);
  _M0L6g__excS672 = _M0L4p__eS606->$3;
  moonbit_incref(_M0L6g__excS672);
  #line 73 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_extended\\main.mbt"
  _M0L6_2atmpS671 = _M0MPC15array5Array2atGfE(_M0L6g__excS672, 0);
  moonbit_decref(_M0L6g__excS672);
  #line 73 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_extended\\main.mbt"
  _M0MPB13StringBuilder13write__objectGfE(_M0L18_2astring__builderS619, _M0L6_2atmpS671);
  #line 73 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_extended\\main.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS619, (moonbit_string_t)moonbit_string_literal_28.data);
  _M0L5g__pvS674 = _M0L4p__eS606->$4;
  moonbit_incref(_M0L5g__pvS674);
  #line 73 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_extended\\main.mbt"
  _M0L6_2atmpS673 = _M0MPC15array5Array2atGfE(_M0L5g__pvS674, 0);
  moonbit_decref(_M0L5g__pvS674);
  #line 73 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_extended\\main.mbt"
  _M0MPB13StringBuilder13write__objectGfE(_M0L18_2astring__builderS619, _M0L6_2atmpS673);
  #line 73 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_extended\\main.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS619, (moonbit_string_t)moonbit_string_literal_29.data);
  _M0L6g__sstS676 = _M0L4p__eS606->$5;
  moonbit_incref(_M0L6g__sstS676);
  #line 73 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_extended\\main.mbt"
  _M0L6_2atmpS675 = _M0MPC15array5Array2atGfE(_M0L6g__sstS676, 0);
  moonbit_decref(_M0L6g__sstS676);
  #line 73 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_extended\\main.mbt"
  _M0MPB13StringBuilder13write__objectGfE(_M0L18_2astring__builderS619, _M0L6_2atmpS675);
  #line 73 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_extended\\main.mbt"
  _M0L6_2atmpS670
  = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS619);
  moonbit_decref(_M0L18_2astring__builderS619);
  #line 72 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_extended\\main.mbt"
  _M0FPB7printlnGsE(_M0L6_2atmpS670);
  moonbit_decref(_M0L6_2atmpS670);
  #line 75 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_extended\\main.mbt"
  _M0L18_2astring__builderS620
  = _M0MPB13StringBuilder21StringBuilder_2einner(14);
  #line 75 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_extended\\main.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS620, (moonbit_string_t)moonbit_string_literal_30.data);
  _M0L4fireS679 = _M0L4p__eS606->$7;
  _M0L6_2acntS1395 = Moonbit_rc_count(Moonbit_object_header(_M0L4p__eS606));
  if (_M0L6_2acntS1395 > 1) {
    int32_t _M0L11_2anew__cntS1403 = _M0L6_2acntS1395 - 1;
    Moonbit_set_rc_count(Moonbit_object_header(_M0L4p__eS606), _M0L11_2anew__cntS1403);
    moonbit_incref(_M0L4fireS679);
  } else if (_M0L6_2acntS1395 == 1) {
    struct _M0TPB5ArrayGfE* _M0L8_2afieldS1402 = _M0L4p__eS606->$8;
    struct _M0TPB5ArrayGfE* _M0L8_2afieldS1401;
    struct _M0TPB5ArrayGfE* _M0L8_2afieldS1400;
    struct _M0TPB5ArrayGfE* _M0L8_2afieldS1399;
    struct _M0TPB5ArrayGfE* _M0L8_2afieldS1398;
    struct _M0TPB5ArrayGfE* _M0L8_2afieldS1397;
    struct _M0TP26RiantR8snn__mbt19ExtendedIFParameter* _M0L8_2afieldS1396;
    moonbit_decref(_M0L8_2afieldS1402);
    _M0L8_2afieldS1401 = _M0L4p__eS606->$6;
    moonbit_decref(_M0L8_2afieldS1401);
    _M0L8_2afieldS1400 = _M0L4p__eS606->$5;
    moonbit_decref(_M0L8_2afieldS1400);
    _M0L8_2afieldS1399 = _M0L4p__eS606->$4;
    moonbit_decref(_M0L8_2afieldS1399);
    _M0L8_2afieldS1398 = _M0L4p__eS606->$3;
    moonbit_decref(_M0L8_2afieldS1398);
    _M0L8_2afieldS1397 = _M0L4p__eS606->$2;
    moonbit_decref(_M0L8_2afieldS1397);
    _M0L8_2afieldS1396 = _M0L4p__eS606->$1;
    moonbit_decref(_M0L8_2afieldS1396);
    #line 75 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_extended\\main.mbt"
    moonbit_free(_M0L4p__eS606);
  }
  #line 75 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_extended\\main.mbt"
  _M0L6_2atmpS678 = _M0MPC15array5Array2atGbE(_M0L4fireS679, 0);
  moonbit_decref(_M0L4fireS679);
  #line 75 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_extended\\main.mbt"
  _M0MPB13StringBuilder13write__objectGbE(_M0L18_2astring__builderS620, _M0L6_2atmpS678);
  #line 75 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_extended\\main.mbt"
  _M0L6_2atmpS677
  = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS620);
  moonbit_decref(_M0L18_2astring__builderS620);
  #line 75 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_extended\\main.mbt"
  _M0FPB7printlnGsE(_M0L6_2atmpS677);
  moonbit_decref(_M0L6_2atmpS677);
  #line 76 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_extended\\main.mbt"
  _M0L18_2astring__builderS621
  = _M0MPB13StringBuilder21StringBuilder_2einner(28);
  #line 76 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_extended\\main.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS621, (moonbit_string_t)moonbit_string_literal_31.data);
  _M0L3valS681 = _M0L12spike__countS614->$0;
  moonbit_decref(_M0L12spike__countS614);
  #line 76 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_extended\\main.mbt"
  _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS621, _M0L3valS681);
  #line 76 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_extended\\main.mbt"
  _M0L6_2atmpS680
  = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS621);
  moonbit_decref(_M0L18_2astring__builderS621);
  #line 76 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_extended\\main.mbt"
  _M0FPB7printlnGsE(_M0L6_2atmpS680);
  moonbit_decref(_M0L6_2atmpS680);
  #line 77 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_extended\\main.mbt"
  _M0L18_2astring__builderS622
  = _M0MPB13StringBuilder21StringBuilder_2einner(25);
  #line 77 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_extended\\main.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS622, (moonbit_string_t)moonbit_string_literal_32.data);
  _M0L1vS684 = _M0L5p__pvS607->$2;
  _M0L6_2acntS1404 = Moonbit_rc_count(Moonbit_object_header(_M0L5p__pvS607));
  if (_M0L6_2acntS1404 > 1) {
    int32_t _M0L11_2anew__cntS1412 = _M0L6_2acntS1404 - 1;
    Moonbit_set_rc_count(Moonbit_object_header(_M0L5p__pvS607), _M0L11_2anew__cntS1412);
    moonbit_incref(_M0L1vS684);
  } else if (_M0L6_2acntS1404 == 1) {
    struct _M0TPB5ArrayGfE* _M0L8_2afieldS1411 = _M0L5p__pvS607->$8;
    struct _M0TPB5ArrayGbE* _M0L8_2afieldS1410;
    struct _M0TPB5ArrayGfE* _M0L8_2afieldS1409;
    struct _M0TPB5ArrayGfE* _M0L8_2afieldS1408;
    struct _M0TPB5ArrayGfE* _M0L8_2afieldS1407;
    struct _M0TPB5ArrayGfE* _M0L8_2afieldS1406;
    struct _M0TP26RiantR8snn__mbt19ExtendedIFParameter* _M0L8_2afieldS1405;
    moonbit_decref(_M0L8_2afieldS1411);
    _M0L8_2afieldS1410 = _M0L5p__pvS607->$7;
    moonbit_decref(_M0L8_2afieldS1410);
    _M0L8_2afieldS1409 = _M0L5p__pvS607->$6;
    moonbit_decref(_M0L8_2afieldS1409);
    _M0L8_2afieldS1408 = _M0L5p__pvS607->$5;
    moonbit_decref(_M0L8_2afieldS1408);
    _M0L8_2afieldS1407 = _M0L5p__pvS607->$4;
    moonbit_decref(_M0L8_2afieldS1407);
    _M0L8_2afieldS1406 = _M0L5p__pvS607->$3;
    moonbit_decref(_M0L8_2afieldS1406);
    _M0L8_2afieldS1405 = _M0L5p__pvS607->$1;
    moonbit_decref(_M0L8_2afieldS1405);
    #line 77 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_extended\\main.mbt"
    moonbit_free(_M0L5p__pvS607);
  }
  #line 77 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_extended\\main.mbt"
  _M0L6_2atmpS683 = _M0MPC15array5Array2atGfE(_M0L1vS684, 0);
  moonbit_decref(_M0L1vS684);
  #line 77 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_extended\\main.mbt"
  _M0MPB13StringBuilder13write__objectGfE(_M0L18_2astring__builderS622, _M0L6_2atmpS683);
  #line 77 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_extended\\main.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS622, (moonbit_string_t)moonbit_string_literal_33.data);
  _M0L1vS686 = _M0L6p__sstS608->$2;
  _M0L6_2acntS1413 = Moonbit_rc_count(Moonbit_object_header(_M0L6p__sstS608));
  if (_M0L6_2acntS1413 > 1) {
    int32_t _M0L11_2anew__cntS1421 = _M0L6_2acntS1413 - 1;
    Moonbit_set_rc_count(Moonbit_object_header(_M0L6p__sstS608), _M0L11_2anew__cntS1421);
    moonbit_incref(_M0L1vS686);
  } else if (_M0L6_2acntS1413 == 1) {
    struct _M0TPB5ArrayGfE* _M0L8_2afieldS1420 = _M0L6p__sstS608->$8;
    struct _M0TPB5ArrayGbE* _M0L8_2afieldS1419;
    struct _M0TPB5ArrayGfE* _M0L8_2afieldS1418;
    struct _M0TPB5ArrayGfE* _M0L8_2afieldS1417;
    struct _M0TPB5ArrayGfE* _M0L8_2afieldS1416;
    struct _M0TPB5ArrayGfE* _M0L8_2afieldS1415;
    struct _M0TP26RiantR8snn__mbt19ExtendedIFParameter* _M0L8_2afieldS1414;
    moonbit_decref(_M0L8_2afieldS1420);
    _M0L8_2afieldS1419 = _M0L6p__sstS608->$7;
    moonbit_decref(_M0L8_2afieldS1419);
    _M0L8_2afieldS1418 = _M0L6p__sstS608->$6;
    moonbit_decref(_M0L8_2afieldS1418);
    _M0L8_2afieldS1417 = _M0L6p__sstS608->$5;
    moonbit_decref(_M0L8_2afieldS1417);
    _M0L8_2afieldS1416 = _M0L6p__sstS608->$4;
    moonbit_decref(_M0L8_2afieldS1416);
    _M0L8_2afieldS1415 = _M0L6p__sstS608->$3;
    moonbit_decref(_M0L8_2afieldS1415);
    _M0L8_2afieldS1414 = _M0L6p__sstS608->$1;
    moonbit_decref(_M0L8_2afieldS1414);
    #line 77 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_extended\\main.mbt"
    moonbit_free(_M0L6p__sstS608);
  }
  #line 77 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_extended\\main.mbt"
  _M0L6_2atmpS685 = _M0MPC15array5Array2atGfE(_M0L1vS686, 0);
  moonbit_decref(_M0L1vS686);
  #line 77 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_extended\\main.mbt"
  _M0MPB13StringBuilder13write__objectGfE(_M0L18_2astring__builderS622, _M0L6_2atmpS685);
  #line 77 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_extended\\main.mbt"
  _M0L6_2atmpS682
  = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS622);
  moonbit_decref(_M0L18_2astring__builderS622);
  #line 77 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_extended\\main.mbt"
  _M0FPB7printlnGsE(_M0L6_2atmpS682);
  moonbit_decref(_M0L6_2atmpS682);
  return 0;
}