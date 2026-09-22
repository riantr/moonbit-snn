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

struct _M0TUdiE;

struct _M0TPB5ArrayGbE;

struct _M0TPB5ArrayGRPB5ArrayGfEE;

struct _M0BTPB6Logger;

struct _M0BTPB4Show;

struct _M0TP26RiantR8snn__mbt7Monitor;

struct _M0TPC16string10StringView;

struct _M0TP26RiantR8snn__mbt2IF;

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

int32_t _M0MP26RiantR8snn__mbt15SparseMatrixCSR3nnz(
  struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR*
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

int32_t _M0MPC15array5Array3setGRPB5ArrayGfEE(
  struct _M0TPB5ArrayGRPB5ArrayGfEE*,
  int32_t,
  struct _M0TPB5ArrayGfE*
);

int32_t _M0MPC15array5Array3setGiE(struct _M0TPB5ArrayGiE*, int32_t, int32_t);

int32_t _M0MPC15array5Array3setGbE(struct _M0TPB5ArrayGbE*, int32_t, int32_t);

void* _M0MPC15array5Array3popGfE(struct _M0TPB5ArrayGfE*);

int64_t _M0MPC15array5Array3popGiE(struct _M0TPB5ArrayGiE*);

struct _M0TP26RiantR8snn__mbt7Monitor* _M0MPC15array5Array2atGRP26RiantR8snn__mbt7MonitorE(
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt7MonitorE*,
  int32_t
);

float _M0MPC15array5Array2atGfE(struct _M0TPB5ArrayGfE*, int32_t);

struct _M0TPB5ArrayGfE* _M0MPC15array5Array2atGRPB5ArrayGfEE(
  struct _M0TPB5ArrayGRPB5ArrayGfEE*,
  int32_t
);

int32_t _M0MPC15array5Array2atGiE(struct _M0TPB5ArrayGiE*, int32_t);

int32_t _M0MPC15array5Array2atGbE(struct _M0TPB5ArrayGbE*, int32_t);

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

int32_t _M0MPC15array5Array4pushGiE(struct _M0TPB5ArrayGiE*, int32_t);

int32_t _M0MPC15array5Array4pushGfE(struct _M0TPB5ArrayGfE*, float);

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

struct _M0TP26RiantR8snn__mbt7Monitor** _M0MPC15array5Array6bufferGRP26RiantR8snn__mbt7MonitorE(
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt7MonitorE*
);

float* _M0MPC15array5Array6bufferGfE(struct _M0TPB5ArrayGfE*);

struct _M0TPB5ArrayGfE** _M0MPC15array5Array6bufferGRPB5ArrayGfEE(
  struct _M0TPB5ArrayGRPB5ArrayGfEE*
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

int32_t _M0IP016_24default__implPB4Show6outputGmE(
  uint64_t,
  struct _M0TPB6Logger
);

int32_t _M0IP016_24default__implPB4Show6outputGiE(
  int32_t,
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

int32_t _M0MPB13StringBuilder13write__objectGmE(
  struct _M0TPB13StringBuilder*,
  uint64_t
);

int32_t _M0MPB13StringBuilder13write__objectGiE(
  struct _M0TPB13StringBuilder*,
  int32_t
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

int32_t _M0FPC15abort5abortGiE(moonbit_string_t);

int32_t* _M0FPC15abort5abortGRPB18UninitializedArrayGiEE(moonbit_string_t);

float* _M0FPC15abort5abortGRPB18UninitializedArrayGfEE(moonbit_string_t);

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

struct { int32_t rc; uint32_t meta; uint16_t const data[26]; 
} const moonbit_string_literal_21 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 25, 105, 102, 
    95, 110, 101, 116, 46, 109, 98, 116, 58, 32, 110, 101, 116, 119, 
    111, 114, 107, 32, 98, 117, 105, 108, 116, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[1]; 
} const moonbit_string_literal_0 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 0, 0};

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

struct { int32_t rc; uint32_t meta; uint16_t const data[16]; 
} const moonbit_string_literal_28 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 15, 32, 32, 
    118, 91, 48, 93, 32, 40, 105, 110, 104, 41, 32, 61, 32, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[19]; 
} const moonbit_string_literal_24 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 18, 32, 32, 
    73, 69, 32, 99, 111, 110, 110, 101, 99, 116, 105, 111, 110, 115, 
    58, 32, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[2]; 
} const moonbit_string_literal_4 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 1, 48, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[36]; 
} const moonbit_string_literal_26 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 35, 105, 102, 
    95, 110, 101, 116, 46, 109, 98, 116, 58, 32, 115, 105, 109, 117, 
    108, 97, 116, 105, 111, 110, 32, 100, 111, 110, 101, 32, 40, 49, 
    48, 48, 109, 115, 41, 0
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

struct { int32_t rc; uint32_t meta; uint16_t const data[19]; 
} const moonbit_string_literal_22 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 18, 32, 32, 
    69, 69, 32, 99, 111, 110, 110, 101, 99, 116, 105, 111, 110, 115, 
    58, 32, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[3]; 
} const moonbit_string_literal_1 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 2, 103, 101, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[27]; 
} const moonbit_string_literal_30 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 26, 32, 32, 
    105, 110, 104, 91, 48, 93, 32, 115, 112, 105, 107, 101, 115, 32, 
    105, 110, 32, 49, 48, 48, 109, 115, 58, 32, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[3]; 
} const moonbit_string_literal_20 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 2, 103, 105, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[27]; 
} const moonbit_string_literal_29 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 26, 32, 32, 
    101, 120, 99, 91, 48, 93, 32, 115, 112, 105, 107, 101, 115, 32, 105, 
    110, 32, 49, 48, 48, 109, 115, 58, 32, 0
  };

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

struct { int32_t rc; uint32_t meta; uint16_t const data[19]; 
} const moonbit_string_literal_23 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 18, 32, 32, 
    69, 73, 32, 99, 111, 110, 110, 101, 99, 116, 105, 111, 110, 115, 
    58, 32, 0
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

struct { int32_t rc; uint32_t meta; uint16_t const data[19]; 
} const moonbit_string_literal_25 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 18, 32, 32, 
    73, 73, 32, 99, 111, 110, 110, 101, 99, 116, 105, 111, 110, 115, 
    58, 32, 0
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

struct { int32_t rc; uint32_t meta; uint16_t const data[16]; 
} const moonbit_string_literal_27 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 15, 32, 32, 
    118, 91, 48, 93, 32, 40, 101, 120, 99, 41, 32, 61, 32, 0
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

struct moonbit_object const moonbit_constant_constructor_0 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_REGULAR),
    Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0)
  };

uint32_t const moonbit_layout_table_data[74] =
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
    sizeof(struct _M0TPB5ArrayGRPB5ArrayGfEE) / 4, 1,
    offsetof(struct _M0TPB5ArrayGRPB5ArrayGfEE, $0) / 4 * 2,
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

double _M0FPB18double__max__value;

double _M0FPB18double__min__value;

double _M0FPC16double14not__a__number;

double _M0FPC16double13neg__infinity;

double _M0FPC16double13min__positive;

float _M0FP26RiantR8snn__mbt2ms = 0x1p+0f;

struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0MP26RiantR8snn__mbt14SpikingSynapse6random(
  struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS982,
  struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS983,
  moonbit_string_t _M0L3symS988,
  float _M0L2muS984,
  float _M0L5sigmaS985,
  float _M0L1pS986,
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS987
) {
  int32_t _M0L1nS2192;
  int32_t _M0L1nS2193;
  struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS981;
  float* _M0L6_2atmpS2191;
  struct _M0TPB5ArrayGfE* _M0L6_2atmpS2182;
  float* _M0L6_2atmpS2190;
  struct _M0TPB5ArrayGfE* _M0L6_2atmpS2183;
  float* _M0L6_2atmpS2189;
  struct _M0TPB5ArrayGfE* _M0L6_2atmpS2184;
  int32_t* _M0L6_2atmpS2188;
  struct _M0TPB5ArrayGiE* _M0L6_2atmpS2185;
  float* _M0L6_2atmpS2187;
  struct _M0TPB5ArrayGfE* _M0L6_2atmpS2186;
  struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _block_2249;
  #line 131 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
  _M0L1nS2192 = _M0L3preS982->$2;
  _M0L1nS2193 = _M0L4postS983->$2;
  #line 140 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
  _M0L6matrixS981
  = _M0MP26RiantR8snn__mbt15SparseMatrixCSR6random(_M0L1nS2192, _M0L1nS2193, _M0L2muS984, _M0L5sigmaS985, _M0L1pS986, _M0L3rngS987);
  _M0L6_2atmpS2191 = moonbit_empty_float_array;
  _M0L6_2atmpS2182
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_M0L6_2atmpS2182)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 0, 0);
  _M0L6_2atmpS2182->$0 = _M0L6_2atmpS2191;
  _M0L6_2atmpS2182->$1 = 0;
  _M0L6_2atmpS2190 = moonbit_empty_float_array;
  _M0L6_2atmpS2183
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_M0L6_2atmpS2183)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 0, 0);
  _M0L6_2atmpS2183->$0 = _M0L6_2atmpS2190;
  _M0L6_2atmpS2183->$1 = 0;
  _M0L6_2atmpS2189 = moonbit_empty_float_array;
  _M0L6_2atmpS2184
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_M0L6_2atmpS2184)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 0, 0);
  _M0L6_2atmpS2184->$0 = _M0L6_2atmpS2189;
  _M0L6_2atmpS2184->$1 = 0;
  _M0L6_2atmpS2188 = (int32_t*)moonbit_empty_int32_array;
  _M0L6_2atmpS2185
  = (struct _M0TPB5ArrayGiE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGiE));
  Moonbit_object_header(_M0L6_2atmpS2185)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 3, 0);
  _M0L6_2atmpS2185->$0 = _M0L6_2atmpS2188;
  _M0L6_2atmpS2185->$1 = 0;
  _M0L6_2atmpS2187 = moonbit_empty_float_array;
  _M0L6_2atmpS2186
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_M0L6_2atmpS2186)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 0, 0);
  _M0L6_2atmpS2186->$0 = _M0L6_2atmpS2187;
  _M0L6_2atmpS2186->$1 = 0;
  moonbit_incref(_M0L3preS982);
  moonbit_incref(_M0L4postS983);
  moonbit_incref(_M0L3symS988);
  _block_2249
  = (struct _M0TP26RiantR8snn__mbt14SpikingSynapse*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt14SpikingSynapse));
  Moonbit_object_header(_block_2249)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 6, 0);
  _block_2249->$0 = _M0L3preS982;
  _block_2249->$1 = _M0L4postS983;
  _block_2249->$2 = _M0L3symS988;
  _block_2249->$3 = (moonbit_string_t)moonbit_string_literal_0.data;
  _block_2249->$4 = _M0L6matrixS981;
  _block_2249->$5 = _M0L6_2atmpS2182;
  _block_2249->$6 = _M0L6_2atmpS2183;
  _block_2249->$7 = _M0L6_2atmpS2184;
  _block_2249->$8 = _M0L6_2atmpS2185;
  _block_2249->$9 = _M0L6_2atmpS2186;
  return _block_2249;
}

struct _M0TP26RiantR8snn__mbt11IFParameter* _M0MP26RiantR8snn__mbt11IFParameter8with__el(
  float _M0L2elS980
) {
  float _M0L1cS978;
  float _M0L2glS979;
  struct _M0TP26RiantR8snn__mbt11IFParameter* _block_2250;
  #line 49 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L1cS978 = -0x1p+0f;
  _M0L2glS979 = -0x1p+0f;
  _block_2250
  = (struct _M0TP26RiantR8snn__mbt11IFParameter*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt11IFParameter));
  Moonbit_object_header(_block_2250)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_2250->$0 = _M0L1cS978;
  _block_2250->$1 = _M0L2glS979;
  _block_2250->$2 = 0x1.ep+3f;
  _block_2250->$3 = -0x1.9p+5f;
  _block_2250->$4 = -0x1.ep+5f;
  _block_2250->$5 = _M0L2elS980;
  _block_2250->$6 = 0x1.eb851eb851eb8p-5f;
  _block_2250->$7 = 0x1p+1f;
  _block_2250->$8 = 0x0p+0f;
  _block_2250->$9 = 0x0p+0f;
  _block_2250->$10 = 0x0p+0f;
  return _block_2250;
}

struct _M0TP26RiantR8snn__mbt2IF* _M0MP26RiantR8snn__mbt2IF3new(
  int32_t _M0L1nS952,
  struct _M0TP26RiantR8snn__mbt11IFParameter* _M0L5paramS954,
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS957
) {
  struct _M0TPB5ArrayGfE* _M0L1vS951;
  float _M0L2vtS2180;
  float _M0L2vrS2181;
  float _M0L6spreadS953;
  int32_t _M0L7_2abindS955;
  int32_t _M0L1kS956;
  struct _M0TPB5ArrayGfE* _M0L1wS959;
  struct _M0TPB5ArrayGbE* _M0L4fireS960;
  struct _M0TPB5ArrayGiE* _M0L4tabsS961;
  struct _M0TPB5ArrayGfE* _M0L1iS962;
  struct _M0TPB5ArrayGfE* _M0L9syn__currS963;
  struct _M0TPB5ArrayGfE* _M0L2geS964;
  struct _M0TPB5ArrayGfE* _M0L2giS965;
  struct _M0TPB5ArrayGfE* _M0L2heS966;
  struct _M0TPB5ArrayGfE* _M0L2hiS967;
  struct _M0TPB5ArrayGfE* _M0L3gluS968;
  struct _M0TPB5ArrayGfE* _M0L4gabaS969;
  struct _M0TPB5ArrayGfE* _M0L7gsyn__eS970;
  struct _M0TPB5ArrayGfE* _M0L7gsyn__iS971;
  float _M0L4e__eS972;
  float _M0L4e__iS973;
  float _M0L3treS974;
  float _M0L3tdeS975;
  float _M0L3triS976;
  float _M0L3tdiS977;
  struct _M0TP26RiantR8snn__mbt9PostSpike* _M0L6_2atmpS2179;
  struct _M0TP26RiantR8snn__mbt2IF* _block_2252;
  #line 113 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  #line 114 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L1vS951 = _M0MPC15array5Array4makeGfE(_M0L1nS952, 0x0p+0f);
  _M0L2vtS2180 = _M0L5paramS954->$3;
  _M0L2vrS2181 = _M0L5paramS954->$4;
  _M0L6spreadS953 = _M0L2vtS2180 - _M0L2vrS2181;
  _M0L7_2abindS955 = 0;
  _M0L1kS956 = _M0L7_2abindS955;
  while (1) {
    if (_M0L1kS956 < _M0L1nS952) {
      float _M0L2vrS2175 = _M0L5paramS954->$4;
      float _M0L6_2atmpS2177;
      float _M0L6_2atmpS2176;
      float _M0L6_2atmpS2174;
      int32_t _M0L6_2atmpS2178;
      #line 117 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2177 = _M0FP26RiantR8snn__mbt9next__f32(_M0L3rngS957);
      _M0L6_2atmpS2176 = _M0L6_2atmpS2177 * _M0L6spreadS953;
      _M0L6_2atmpS2174 = _M0L2vrS2175 + _M0L6_2atmpS2176;
      #line 117 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS951, _M0L1kS956, _M0L6_2atmpS2174);
      _M0L6_2atmpS2178 = _M0L1kS956 + 1;
      _M0L1kS956 = _M0L6_2atmpS2178;
      continue;
    }
    break;
  }
  #line 119 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L1wS959 = _M0MPC15array5Array4makeGfE(_M0L1nS952, 0x0p+0f);
  #line 120 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L4fireS960 = _M0MPC15array5Array4makeGbE(_M0L1nS952, 0);
  #line 121 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L4tabsS961 = _M0MPC15array5Array4makeGiE(_M0L1nS952, 0);
  #line 122 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L1iS962 = _M0MPC15array5Array4makeGfE(_M0L1nS952, 0x0p+0f);
  #line 123 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L9syn__currS963 = _M0MPC15array5Array4makeGfE(_M0L1nS952, 0x0p+0f);
  #line 124 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L2geS964 = _M0MPC15array5Array4makeGfE(_M0L1nS952, 0x0p+0f);
  #line 125 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L2giS965 = _M0MPC15array5Array4makeGfE(_M0L1nS952, 0x0p+0f);
  #line 126 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L2heS966 = _M0MPC15array5Array4makeGfE(_M0L1nS952, 0x0p+0f);
  #line 127 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L2hiS967 = _M0MPC15array5Array4makeGfE(_M0L1nS952, 0x0p+0f);
  #line 128 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L3gluS968 = _M0MPC15array5Array4makeGfE(_M0L1nS952, 0x0p+0f);
  #line 129 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L4gabaS969 = _M0MPC15array5Array4makeGfE(_M0L1nS952, 0x0p+0f);
  #line 130 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L7gsyn__eS970 = _M0MPC15array5Array4makeGfE(_M0L1nS952, 0x1p+0f);
  #line 131 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L7gsyn__iS971 = _M0MPC15array5Array4makeGfE(_M0L1nS952, 0x1p+0f);
  _M0L4e__eS972 = 0x0p+0f;
  _M0L4e__iS973 = -0x1.2cp+6f;
  _M0L3treS974 = 0x1p+0f;
  _M0L3tdeS975 = 0x1.8p+2f;
  _M0L3triS976 = 0x1p-1f;
  _M0L3tdiS977 = 0x1p+1f;
  #line 138 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L6_2atmpS2179 = _M0MP26RiantR8snn__mbt9PostSpike3new();
  moonbit_incref(_M0L5paramS954);
  _block_2252
  = (struct _M0TP26RiantR8snn__mbt2IF*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt2IF));
  Moonbit_object_header(_block_2252)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 18, 0);
  _block_2252->$0 = _M0L5paramS954;
  _block_2252->$1 = _M0L6_2atmpS2179;
  _block_2252->$2 = _M0L1nS952;
  _block_2252->$3 = _M0L1vS951;
  _block_2252->$4 = _M0L1wS959;
  _block_2252->$5 = _M0L4fireS960;
  _block_2252->$6 = _M0L4tabsS961;
  _block_2252->$7 = _M0L1iS962;
  _block_2252->$8 = _M0L9syn__currS963;
  _block_2252->$9 = _M0L2geS964;
  _block_2252->$10 = _M0L2giS965;
  _block_2252->$11 = _M0L2heS966;
  _block_2252->$12 = _M0L2hiS967;
  _block_2252->$13 = _M0L3gluS968;
  _block_2252->$14 = _M0L4gabaS969;
  _block_2252->$15 = _M0L7gsyn__eS970;
  _block_2252->$16 = _M0L7gsyn__iS971;
  _block_2252->$17 = _M0L4e__eS972;
  _block_2252->$18 = _M0L4e__iS973;
  _block_2252->$19 = _M0L3treS974;
  _block_2252->$20 = _M0L3tdeS975;
  _block_2252->$21 = _M0L3triS976;
  _block_2252->$22 = _M0L3tdiS977;
  return _block_2252;
}

struct _M0TP26RiantR8snn__mbt9PostSpike* _M0MP26RiantR8snn__mbt9PostSpike3new(
  
) {
  struct _M0TP26RiantR8snn__mbt9PostSpike* _block_2253;
  #line 75 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _block_2253
  = (struct _M0TP26RiantR8snn__mbt9PostSpike*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt9PostSpike));
  Moonbit_object_header(_block_2253)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_2253->$0 = 0x1p+1f;
  return _block_2253;
}

struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0MP26RiantR8snn__mbt7Xoshiro7default(
  
) {
  #line 56 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  #line 57 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  return _M0MP26RiantR8snn__mbt7Xoshiro3new(0ull);
}

int32_t _M0FP26RiantR8snn__mbt8sim__for(
  struct _M0TP26RiantR8snn__mbt5Model* _M0L5modelS947,
  float _M0L8durationS946
) {
  float _M0L2dtS943;
  struct _M0TP26RiantR8snn__mbt4Time* _M0L4timeS944;
  float _M0L6_2atmpS2173;
  int32_t _M0L5stepsS945;
  int32_t _M0L7_2abindS948;
  int32_t _M0L2__S949;
  #line 123 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
  _M0L2dtS943 = 0x1p-3f;
  #line 125 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
  _M0L4timeS944 = _M0MP26RiantR8snn__mbt4Time3new();
  #line 126 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
  _M0FP26RiantR8snn__mbt7set__dt(_M0L4timeS944, _M0L2dtS943);
  _M0L6_2atmpS2173 = _M0L8durationS946 / _M0L2dtS943;
  #line 127 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
  _M0L5stepsS945 = _M0MPC15float5Float7to__int(_M0L6_2atmpS2173);
  #line 128 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
  _M0FP26RiantR8snn__mbt12record__zero(_M0L5modelS947);
  _M0L7_2abindS948 = 0;
  _M0L2__S949 = _M0L7_2abindS948;
  while (1) {
    if (_M0L2__S949 < _M0L5stepsS945) {
      int32_t _M0L6_2atmpS2172;
      #line 130 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
      _M0FP26RiantR8snn__mbt11step__model(_M0L5modelS947, _M0L4timeS944);
      _M0L6_2atmpS2172 = _M0L2__S949 + 1;
      _M0L2__S949 = _M0L6_2atmpS2172;
      continue;
    } else {
      moonbit_decref(_M0L4timeS944);
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt11step__model(
  struct _M0TP26RiantR8snn__mbt5Model* _M0L5modelS918,
  struct _M0TP26RiantR8snn__mbt4Time* _M0L4timeS916
) {
  float _M0L6t__nowS915;
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt14SpikingSynapseE* _M0L7_2abindS917;
  int32_t _M0L7_2abindS919;
  struct _M0TP26RiantR8snn__mbt14SpikingSynapse** _M0L7_2abindS920;
  int32_t _M0L2__S921;
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt14SpikingSynapseE* _M0L7_2abindS924;
  int32_t _M0L7_2abindS925;
  struct _M0TP26RiantR8snn__mbt14SpikingSynapse** _M0L7_2abindS926;
  int32_t _M0L2__S927;
  float _M0L2dtS930;
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt2IFE* _M0L7_2abindS931;
  int32_t _M0L7_2abindS932;
  struct _M0TP26RiantR8snn__mbt2IF** _M0L7_2abindS933;
  int32_t _M0L2__S934;
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt7MonitorE* _M0L7_2abindS937;
  int32_t _M0L7_2abindS938;
  struct _M0TP26RiantR8snn__mbt7Monitor** _M0L7_2abindS939;
  int32_t _M0L2__S940;
  #line 95 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
  #line 97 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
  _M0L6t__nowS915 = _M0FP26RiantR8snn__mbt9get__time(_M0L4timeS916);
  _M0L7_2abindS917 = _M0L5modelS918->$1;
  _M0L7_2abindS919 = _M0L7_2abindS917->$1;
  _M0L7_2abindS920 = _M0L7_2abindS917->$0;
  moonbit_incref(_M0L7_2abindS920);
  _M0L2__S921 = 0;
  while (1) {
    if (_M0L2__S921 < _M0L7_2abindS919) {
      struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0L1cS922 =
        (struct _M0TP26RiantR8snn__mbt14SpikingSynapse*)_M0L7_2abindS920[
          _M0L2__S921
        ];
      int32_t _M0L6_2atmpS2166;
      moonbit_incref(_M0L1cS922);
      #line 99 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
      _M0FP26RiantR8snn__mbt25deliver__pending__synapse(_M0L1cS922, _M0L6t__nowS915);
      moonbit_decref(_M0L1cS922);
      _M0L6_2atmpS2166 = _M0L2__S921 + 1;
      _M0L2__S921 = _M0L6_2atmpS2166;
      continue;
    } else {
      moonbit_decref(_M0L7_2abindS920);
    }
    break;
  }
  _M0L7_2abindS924 = _M0L5modelS918->$1;
  _M0L7_2abindS925 = _M0L7_2abindS924->$1;
  _M0L7_2abindS926 = _M0L7_2abindS924->$0;
  moonbit_incref(_M0L7_2abindS926);
  _M0L2__S927 = 0;
  while (1) {
    if (_M0L2__S927 < _M0L7_2abindS925) {
      struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0L1cS928 =
        (struct _M0TP26RiantR8snn__mbt14SpikingSynapse*)_M0L7_2abindS926[
          _M0L2__S927
        ];
      int32_t _M0L6_2atmpS2167;
      moonbit_incref(_M0L1cS928);
      #line 104 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
      _M0FP26RiantR8snn__mbt16forward__synapse(_M0L1cS928, _M0L6t__nowS915);
      moonbit_decref(_M0L1cS928);
      _M0L6_2atmpS2167 = _M0L2__S927 + 1;
      _M0L2__S927 = _M0L6_2atmpS2167;
      continue;
    } else {
      moonbit_decref(_M0L7_2abindS926);
    }
    break;
  }
  _M0L2dtS930 = _M0L4timeS916->$2;
  _M0L7_2abindS931 = _M0L5modelS918->$0;
  _M0L7_2abindS932 = _M0L7_2abindS931->$1;
  _M0L7_2abindS933 = _M0L7_2abindS931->$0;
  moonbit_incref(_M0L7_2abindS933);
  _M0L2__S934 = 0;
  while (1) {
    if (_M0L2__S934 < _M0L7_2abindS932) {
      struct _M0TP26RiantR8snn__mbt2IF* _M0L1pS935 =
        (struct _M0TP26RiantR8snn__mbt2IF*)_M0L7_2abindS933[_M0L2__S934];
      int32_t _M0L6_2atmpS2168;
      moonbit_incref(_M0L1pS935);
      #line 109 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
      _M0FP26RiantR8snn__mbt14step__synapses(_M0L1pS935, _M0L2dtS930);
      #line 110 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
      _M0FP26RiantR8snn__mbt17synaptic__current(_M0L1pS935);
      #line 111 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
      _M0FP26RiantR8snn__mbt12step__neuron(_M0L1pS935, _M0L2dtS930);
      moonbit_decref(_M0L1pS935);
      _M0L6_2atmpS2168 = _M0L2__S934 + 1;
      _M0L2__S934 = _M0L6_2atmpS2168;
      continue;
    } else {
      moonbit_decref(_M0L7_2abindS933);
    }
    break;
  }
  _M0L7_2abindS937 = _M0L5modelS918->$2;
  _M0L7_2abindS938 = _M0L7_2abindS937->$1;
  _M0L7_2abindS939 = _M0L7_2abindS937->$0;
  moonbit_incref(_M0L7_2abindS939);
  _M0L2__S940 = 0;
  while (1) {
    if (_M0L2__S940 < _M0L7_2abindS938) {
      struct _M0TP26RiantR8snn__mbt7Monitor* _M0L1mS941 =
        (struct _M0TP26RiantR8snn__mbt7Monitor*)_M0L7_2abindS939[_M0L2__S940];
      struct _M0TPB5ArrayGfE* _M0L1tS2170 = _M0L4timeS916->$0;
      float _M0L6_2atmpS2169;
      int32_t _M0L6_2atmpS2171;
      moonbit_incref(_M0L1mS941);
      #line 115 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
      _M0L6_2atmpS2169 = _M0MPC15array5Array2atGfE(_M0L1tS2170, 0);
      #line 115 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
      _M0FP26RiantR8snn__mbt11record__one(_M0L1mS941, _M0L6_2atmpS2169);
      moonbit_decref(_M0L1mS941);
      _M0L6_2atmpS2171 = _M0L2__S940 + 1;
      _M0L2__S940 = _M0L6_2atmpS2171;
      continue;
    } else {
      moonbit_decref(_M0L7_2abindS939);
    }
    break;
  }
  #line 118 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
  _M0FP26RiantR8snn__mbt12update__time(_M0L4timeS916, _M0L2dtS930);
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt16forward__synapse(
  struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0L1cS891,
  float _M0L6t__nowS902
) {
  struct _M0TPB5ArrayGfE* _M0L6delaysS2165;
  int32_t _M0L6_2atmpS2164;
  int32_t _M0L10use__delayS890;
  struct _M0TPB5ArrayGfE* _M0L3rhoS2163;
  int32_t _M0L6_2atmpS2162;
  int32_t _M0L8use__rhoS892;
  #line 246 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
  _M0L6delaysS2165 = _M0L1cS891->$5;
  #line 247 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
  _M0L6_2atmpS2164 = _M0MPC15array5Array6lengthGfE(_M0L6delaysS2165);
  _M0L10use__delayS890 = _M0L6_2atmpS2164 > 0;
  _M0L3rhoS2163 = _M0L1cS891->$6;
  #line 248 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
  _M0L6_2atmpS2162 = _M0MPC15array5Array6lengthGfE(_M0L3rhoS2163);
  _M0L8use__rhoS892 = _M0L6_2atmpS2162 > 0;
  if (_M0L10use__delayS890) {
    struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS2125 = _M0L1cS891->$0;
    struct _M0TPB5ArrayGbE* _M0L4fireS2124 = _M0L3preS2125->$5;
    int32_t _M0L6n__preS893;
    struct _M0TPB8MutLocalGiE* _M0L1jS894;
    #line 251 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
    _M0L6n__preS893 = _M0MPC15array5Array6lengthGbE(_M0L4fireS2124);
    _M0L1jS894
    = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
    Moonbit_object_header(_M0L1jS894)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
    _M0L1jS894->$0 = 0;
    while (1) {
      int32_t _M0L3valS2093 = _M0L1jS894->$0;
      if (_M0L3valS2093 < _M0L6n__preS893) {
        struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS2096 = _M0L1cS891->$0;
        struct _M0TPB5ArrayGbE* _M0L4fireS2094 = _M0L3preS2096->$5;
        int32_t _M0L3valS2095 = _M0L1jS894->$0;
        int32_t _M0L3valS2123;
        int32_t _M0L6_2atmpS2122;
        #line 254 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
        if (_M0MPC15array5Array2atGbE(_M0L4fireS2094, _M0L3valS2095)) {
          struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS2121 =
            _M0L1cS891->$4;
          struct _M0TPB5ArrayGiE* _M0L6rowptrS2119 = _M0L6matrixS2121->$2;
          int32_t _M0L3valS2120 = _M0L1jS894->$0;
          int32_t _M0L5startS895;
          struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS2118;
          struct _M0TPB5ArrayGiE* _M0L6rowptrS2115;
          int32_t _M0L3valS2117;
          int32_t _M0L6_2atmpS2116;
          int32_t _M0L3endS896;
          struct _M0TPB8MutLocalGiE* _M0L1sS897;
          #line 255 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
          _M0L5startS895
          = _M0MPC15array5Array2atGiE(_M0L6rowptrS2119, _M0L3valS2120);
          _M0L6matrixS2118 = _M0L1cS891->$4;
          _M0L6rowptrS2115 = _M0L6matrixS2118->$2;
          _M0L3valS2117 = _M0L1jS894->$0;
          _M0L6_2atmpS2116 = _M0L3valS2117 + 1;
          #line 256 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
          _M0L3endS896
          = _M0MPC15array5Array2atGiE(_M0L6rowptrS2115, _M0L6_2atmpS2116);
          _M0L1sS897
          = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
          Moonbit_object_header(_M0L1sS897)->meta
          = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
          _M0L1sS897->$0 = _M0L5startS895;
          while (1) {
            int32_t _M0L3valS2097 = _M0L1sS897->$0;
            if (_M0L3valS2097 < _M0L3endS896) {
              struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS2114 =
                _M0L1cS891->$4;
              struct _M0TPB5ArrayGiE* _M0L6colptrS2112 = _M0L6matrixS2114->$3;
              int32_t _M0L3valS2113 = _M0L1sS897->$0;
              int32_t _M0L9post__idxS898;
              struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS2111;
              struct _M0TPB5ArrayGfE* _M0L4valsS2109;
              int32_t _M0L3valS2110;
              float _M0L1wS899;
              struct _M0TPB5ArrayGfE* _M0L6delaysS2107;
              int32_t _M0L3valS2108;
              float _M0L1dS900;
              float _M0L9w__scaledS901;
              int32_t _M0L3valS2103;
              int32_t _M0L6_2atmpS2102;
              #line 259 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
              _M0L9post__idxS898
              = _M0MPC15array5Array2atGiE(_M0L6colptrS2112, _M0L3valS2113);
              _M0L6matrixS2111 = _M0L1cS891->$4;
              _M0L4valsS2109 = _M0L6matrixS2111->$4;
              _M0L3valS2110 = _M0L1sS897->$0;
              #line 260 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
              _M0L1wS899
              = _M0MPC15array5Array2atGfE(_M0L4valsS2109, _M0L3valS2110);
              _M0L6delaysS2107 = _M0L1cS891->$5;
              _M0L3valS2108 = _M0L1sS897->$0;
              #line 261 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
              _M0L1dS900
              = _M0MPC15array5Array2atGfE(_M0L6delaysS2107, _M0L3valS2108);
              if (_M0L8use__rhoS892) {
                struct _M0TPB5ArrayGfE* _M0L3rhoS2105 = _M0L1cS891->$6;
                int32_t _M0L3valS2106 = _M0L1sS897->$0;
                float _M0L6_2atmpS2104;
                #line 263 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
                _M0L6_2atmpS2104
                = _M0MPC15array5Array2atGfE(_M0L3rhoS2105, _M0L3valS2106);
                _M0L9w__scaledS901 = _M0L1wS899 * _M0L6_2atmpS2104;
              } else {
                _M0L9w__scaledS901 = _M0L1wS899;
              }
              if (_M0L1dS900 == 0x0p+0f) {
                #line 266 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
                _M0FP26RiantR8snn__mbt13apply__weight(_M0L1cS891, _M0L9post__idxS898, _M0L9w__scaledS901);
              } else {
                struct _M0TPB5ArrayGfE* _M0L14pending__timesS2098 =
                  _M0L1cS891->$7;
                float _M0L6_2atmpS2099 = _M0L6t__nowS902 + _M0L1dS900;
                struct _M0TPB5ArrayGiE* _M0L14pending__postsS2100;
                struct _M0TPB5ArrayGfE* _M0L16pending__weightsS2101;
                #line 269 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
                _M0MPC15array5Array4pushGfE(_M0L14pending__timesS2098, _M0L6_2atmpS2099);
                _M0L14pending__postsS2100 = _M0L1cS891->$8;
                #line 270 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
                _M0MPC15array5Array4pushGiE(_M0L14pending__postsS2100, _M0L9post__idxS898);
                _M0L16pending__weightsS2101 = _M0L1cS891->$9;
                #line 271 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
                _M0MPC15array5Array4pushGfE(_M0L16pending__weightsS2101, _M0L9w__scaledS901);
              }
              _M0L3valS2103 = _M0L1sS897->$0;
              _M0L6_2atmpS2102 = _M0L3valS2103 + 1;
              _M0L1sS897->$0 = _M0L6_2atmpS2102;
              continue;
            } else {
              moonbit_decref(_M0L1sS897);
            }
            break;
          }
        }
        _M0L3valS2123 = _M0L1jS894->$0;
        _M0L6_2atmpS2122 = _M0L3valS2123 + 1;
        _M0L1jS894->$0 = _M0L6_2atmpS2122;
        continue;
      } else {
        moonbit_decref(_M0L1jS894);
      }
      break;
    }
  } else {
    moonbit_string_t _M0L3symS2159 = _M0L1cS891->$2;
    struct _M0TPB5ArrayGfE* _M0L6targetS905;
    #line 280 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
    if (
      _M0L3symS2159 == (moonbit_string_t)moonbit_string_literal_1.data
      || Moonbit_array_length(_M0L3symS2159)
         == Moonbit_array_length((moonbit_string_t)moonbit_string_literal_1.data)
         && 0
            == memcmp(_M0L3symS2159, (moonbit_string_t)moonbit_string_literal_1.data, Moonbit_array_length(_M0L3symS2159) * 2)
    ) {
      struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS2160 = _M0L1cS891->$1;
      struct _M0TPB5ArrayGfE* _M0L8_2afieldS2194 = _M0L4postS2160->$13;
      moonbit_incref(_M0L8_2afieldS2194);
      _M0L6targetS905 = _M0L8_2afieldS2194;
    } else {
      struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS2161 = _M0L1cS891->$1;
      struct _M0TPB5ArrayGfE* _M0L8_2afieldS2195 = _M0L4postS2161->$14;
      moonbit_incref(_M0L8_2afieldS2195);
      _M0L6targetS905 = _M0L8_2afieldS2195;
    }
    if (_M0L8use__rhoS892) {
      struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS2155 = _M0L1cS891->$0;
      struct _M0TPB5ArrayGbE* _M0L4fireS2154 = _M0L3preS2155->$5;
      int32_t _M0L6n__preS906;
      struct _M0TPB8MutLocalGiE* _M0L1jS907;
      #line 283 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
      _M0L6n__preS906 = _M0MPC15array5Array6lengthGbE(_M0L4fireS2154);
      _M0L1jS907
      = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
      Moonbit_object_header(_M0L1jS907)->meta
      = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
      _M0L1jS907->$0 = 0;
      while (1) {
        int32_t _M0L3valS2126 = _M0L1jS907->$0;
        if (_M0L3valS2126 < _M0L6n__preS906) {
          struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS2129 = _M0L1cS891->$0;
          struct _M0TPB5ArrayGbE* _M0L4fireS2127 = _M0L3preS2129->$5;
          int32_t _M0L3valS2128 = _M0L1jS907->$0;
          int32_t _M0L3valS2153;
          int32_t _M0L6_2atmpS2152;
          #line 286 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
          if (_M0MPC15array5Array2atGbE(_M0L4fireS2127, _M0L3valS2128)) {
            struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS2151 =
              _M0L1cS891->$4;
            struct _M0TPB5ArrayGiE* _M0L6rowptrS2149 = _M0L6matrixS2151->$2;
            int32_t _M0L3valS2150 = _M0L1jS907->$0;
            int32_t _M0L5startS908;
            struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS2148;
            struct _M0TPB5ArrayGiE* _M0L6rowptrS2145;
            int32_t _M0L3valS2147;
            int32_t _M0L6_2atmpS2146;
            int32_t _M0L3endS909;
            struct _M0TPB8MutLocalGiE* _M0L1sS910;
            #line 287 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
            _M0L5startS908
            = _M0MPC15array5Array2atGiE(_M0L6rowptrS2149, _M0L3valS2150);
            _M0L6matrixS2148 = _M0L1cS891->$4;
            _M0L6rowptrS2145 = _M0L6matrixS2148->$2;
            _M0L3valS2147 = _M0L1jS907->$0;
            _M0L6_2atmpS2146 = _M0L3valS2147 + 1;
            #line 288 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
            _M0L3endS909
            = _M0MPC15array5Array2atGiE(_M0L6rowptrS2145, _M0L6_2atmpS2146);
            _M0L1sS910
            = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
            Moonbit_object_header(_M0L1sS910)->meta
            = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
            _M0L1sS910->$0 = _M0L5startS908;
            while (1) {
              int32_t _M0L3valS2130 = _M0L1sS910->$0;
              if (_M0L3valS2130 < _M0L3endS909) {
                struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS2144 =
                  _M0L1cS891->$4;
                struct _M0TPB5ArrayGiE* _M0L6colptrS2142 =
                  _M0L6matrixS2144->$3;
                int32_t _M0L3valS2143 = _M0L1sS910->$0;
                int32_t _M0L9post__idxS911;
                struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS2141;
                struct _M0TPB5ArrayGfE* _M0L4valsS2139;
                int32_t _M0L3valS2140;
                float _M0L6_2atmpS2135;
                struct _M0TPB5ArrayGfE* _M0L3rhoS2137;
                int32_t _M0L3valS2138;
                float _M0L6_2atmpS2136;
                float _M0L9w__scaledS912;
                float _M0L6_2atmpS2132;
                float _M0L6_2atmpS2131;
                int32_t _M0L3valS2134;
                int32_t _M0L6_2atmpS2133;
                #line 291 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
                _M0L9post__idxS911
                = _M0MPC15array5Array2atGiE(_M0L6colptrS2142, _M0L3valS2143);
                _M0L6matrixS2141 = _M0L1cS891->$4;
                _M0L4valsS2139 = _M0L6matrixS2141->$4;
                _M0L3valS2140 = _M0L1sS910->$0;
                #line 292 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
                _M0L6_2atmpS2135
                = _M0MPC15array5Array2atGfE(_M0L4valsS2139, _M0L3valS2140);
                _M0L3rhoS2137 = _M0L1cS891->$6;
                _M0L3valS2138 = _M0L1sS910->$0;
                #line 292 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
                _M0L6_2atmpS2136
                = _M0MPC15array5Array2atGfE(_M0L3rhoS2137, _M0L3valS2138);
                _M0L9w__scaledS912 = _M0L6_2atmpS2135 * _M0L6_2atmpS2136;
                #line 293 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
                _M0L6_2atmpS2132
                = _M0MPC15array5Array2atGfE(_M0L6targetS905, _M0L9post__idxS911);
                _M0L6_2atmpS2131 = _M0L6_2atmpS2132 + _M0L9w__scaledS912;
                #line 293 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
                _M0MPC15array5Array3setGfE(_M0L6targetS905, _M0L9post__idxS911, _M0L6_2atmpS2131);
                _M0L3valS2134 = _M0L1sS910->$0;
                _M0L6_2atmpS2133 = _M0L3valS2134 + 1;
                _M0L1sS910->$0 = _M0L6_2atmpS2133;
                continue;
              } else {
                moonbit_decref(_M0L1sS910);
              }
              break;
            }
          }
          _M0L3valS2153 = _M0L1jS907->$0;
          _M0L6_2atmpS2152 = _M0L3valS2153 + 1;
          _M0L1jS907->$0 = _M0L6_2atmpS2152;
          continue;
        } else {
          moonbit_decref(_M0L1jS907);
          moonbit_decref(_M0L6targetS905);
        }
        break;
      }
    } else {
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS2156 =
        _M0L1cS891->$4;
      struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS2158 = _M0L1cS891->$0;
      struct _M0TPB5ArrayGbE* _M0L4fireS2157 = _M0L3preS2158->$5;
      #line 300 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
      _M0MP26RiantR8snn__mbt15SparseMatrixCSR7forward(_M0L6matrixS2156, _M0L4fireS2157, _M0L6targetS905);
      moonbit_decref(_M0L6targetS905);
    }
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt25deliver__pending__synapse(
  struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0L1cS883,
  float _M0L6t__nowS886
) {
  struct _M0TPB5ArrayGfE* _M0L14pending__timesS2092;
  int32_t _M0L1nS882;
  struct _M0TPB8MutLocalGiE* _M0L4keptS884;
  struct _M0TPB8MutLocalGiE* _M0L1kS885;
  int32_t _M0L3valS2091;
  int32_t _M0L6_2atmpS2090;
  struct _M0TPB8MutLocalGiE* _M0L4dropS888;
  #line 312 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
  _M0L14pending__timesS2092 = _M0L1cS883->$7;
  #line 313 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
  _M0L1nS882 = _M0MPC15array5Array6lengthGfE(_M0L14pending__timesS2092);
  if (_M0L1nS882 == 0) {
    return 0;
  }
  _M0L4keptS884
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L4keptS884)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L4keptS884->$0 = 0;
  _M0L1kS885
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1kS885)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1kS885->$0 = 0;
  while (1) {
    int32_t _M0L3valS2053 = _M0L1kS885->$0;
    if (_M0L3valS2053 < _M0L1nS882) {
      struct _M0TPB5ArrayGfE* _M0L14pending__timesS2055 = _M0L1cS883->$7;
      int32_t _M0L3valS2056 = _M0L1kS885->$0;
      float _M0L6_2atmpS2054;
      int32_t _M0L3valS2083;
      int32_t _M0L6_2atmpS2082;
      #line 320 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
      _M0L6_2atmpS2054
      = _M0MPC15array5Array2atGfE(_M0L14pending__timesS2055, _M0L3valS2056);
      if (_M0L6_2atmpS2054 <= _M0L6t__nowS886) {
        struct _M0TPB5ArrayGiE* _M0L14pending__postsS2061 = _M0L1cS883->$8;
        int32_t _M0L3valS2062 = _M0L1kS885->$0;
        int32_t _M0L6_2atmpS2057;
        struct _M0TPB5ArrayGfE* _M0L16pending__weightsS2059;
        int32_t _M0L3valS2060;
        float _M0L6_2atmpS2058;
        #line 322 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
        _M0L6_2atmpS2057
        = _M0MPC15array5Array2atGiE(_M0L14pending__postsS2061, _M0L3valS2062);
        _M0L16pending__weightsS2059 = _M0L1cS883->$9;
        _M0L3valS2060 = _M0L1kS885->$0;
        #line 322 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
        _M0L6_2atmpS2058
        = _M0MPC15array5Array2atGfE(_M0L16pending__weightsS2059, _M0L3valS2060);
        #line 322 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
        _M0FP26RiantR8snn__mbt13apply__weight(_M0L1cS883, _M0L6_2atmpS2057, _M0L6_2atmpS2058);
      } else {
        int32_t _M0L3valS2063 = _M0L4keptS884->$0;
        int32_t _M0L3valS2064 = _M0L1kS885->$0;
        int32_t _M0L3valS2081;
        int32_t _M0L6_2atmpS2080;
        if (_M0L3valS2063 != _M0L3valS2064) {
          struct _M0TPB5ArrayGfE* _M0L14pending__timesS2065 = _M0L1cS883->$7;
          int32_t _M0L3valS2066 = _M0L4keptS884->$0;
          struct _M0TPB5ArrayGfE* _M0L14pending__timesS2068 = _M0L1cS883->$7;
          int32_t _M0L3valS2069 = _M0L1kS885->$0;
          float _M0L6_2atmpS2067;
          struct _M0TPB5ArrayGiE* _M0L14pending__postsS2070;
          int32_t _M0L3valS2071;
          struct _M0TPB5ArrayGiE* _M0L14pending__postsS2073;
          int32_t _M0L3valS2074;
          int32_t _M0L6_2atmpS2072;
          struct _M0TPB5ArrayGfE* _M0L16pending__weightsS2075;
          int32_t _M0L3valS2076;
          struct _M0TPB5ArrayGfE* _M0L16pending__weightsS2078;
          int32_t _M0L3valS2079;
          float _M0L6_2atmpS2077;
          #line 326 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
          _M0L6_2atmpS2067
          = _M0MPC15array5Array2atGfE(_M0L14pending__timesS2068, _M0L3valS2069);
          #line 326 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
          _M0MPC15array5Array3setGfE(_M0L14pending__timesS2065, _M0L3valS2066, _M0L6_2atmpS2067);
          _M0L14pending__postsS2070 = _M0L1cS883->$8;
          _M0L3valS2071 = _M0L4keptS884->$0;
          _M0L14pending__postsS2073 = _M0L1cS883->$8;
          _M0L3valS2074 = _M0L1kS885->$0;
          #line 327 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
          _M0L6_2atmpS2072
          = _M0MPC15array5Array2atGiE(_M0L14pending__postsS2073, _M0L3valS2074);
          #line 327 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
          _M0MPC15array5Array3setGiE(_M0L14pending__postsS2070, _M0L3valS2071, _M0L6_2atmpS2072);
          _M0L16pending__weightsS2075 = _M0L1cS883->$9;
          _M0L3valS2076 = _M0L4keptS884->$0;
          _M0L16pending__weightsS2078 = _M0L1cS883->$9;
          _M0L3valS2079 = _M0L1kS885->$0;
          #line 328 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
          _M0L6_2atmpS2077
          = _M0MPC15array5Array2atGfE(_M0L16pending__weightsS2078, _M0L3valS2079);
          #line 328 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
          _M0MPC15array5Array3setGfE(_M0L16pending__weightsS2075, _M0L3valS2076, _M0L6_2atmpS2077);
        }
        _M0L3valS2081 = _M0L4keptS884->$0;
        _M0L6_2atmpS2080 = _M0L3valS2081 + 1;
        _M0L4keptS884->$0 = _M0L6_2atmpS2080;
      }
      _M0L3valS2083 = _M0L1kS885->$0;
      _M0L6_2atmpS2082 = _M0L3valS2083 + 1;
      _M0L1kS885->$0 = _M0L6_2atmpS2082;
      continue;
    } else {
      moonbit_decref(_M0L1kS885);
    }
    break;
  }
  _M0L3valS2091 = _M0L4keptS884->$0;
  moonbit_decref(_M0L4keptS884);
  _M0L6_2atmpS2090 = _M0L1nS882 - _M0L3valS2091;
  _M0L4dropS888
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L4dropS888)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L4dropS888->$0 = _M0L6_2atmpS2090;
  while (1) {
    int32_t _M0L3valS2084 = _M0L4dropS888->$0;
    if (_M0L3valS2084 > 0) {
      struct _M0TPB5ArrayGfE* _M0L14pending__timesS2085 = _M0L1cS883->$7;
      void* _M0L6_2atmpS2197;
      struct _M0TPB5ArrayGiE* _M0L14pending__postsS2086;
      struct _M0TPB5ArrayGfE* _M0L16pending__weightsS2087;
      void* _M0L6_2atmpS2196;
      int32_t _M0L3valS2089;
      int32_t _M0L6_2atmpS2088;
      #line 337 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
      _M0L6_2atmpS2197
      = _M0MPC15array5Array3popGfE(_M0L14pending__timesS2085);
      moonbit_decref(_M0L6_2atmpS2197);
      _M0L14pending__postsS2086 = _M0L1cS883->$8;
      #line 338 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
      _M0MPC15array5Array3popGiE(_M0L14pending__postsS2086);
      _M0L16pending__weightsS2087 = _M0L1cS883->$9;
      #line 339 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
      _M0L6_2atmpS2196
      = _M0MPC15array5Array3popGfE(_M0L16pending__weightsS2087);
      moonbit_decref(_M0L6_2atmpS2196);
      _M0L3valS2089 = _M0L4dropS888->$0;
      _M0L6_2atmpS2088 = _M0L3valS2089 - 1;
      _M0L4dropS888->$0 = _M0L6_2atmpS2088;
      continue;
    } else {
      moonbit_decref(_M0L4dropS888);
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt13apply__weight(
  struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0L1cS879,
  int32_t _M0L9post__idxS880,
  float _M0L1wS881
) {
  moonbit_string_t _M0L3symS2040;
  #line 346 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
  _M0L3symS2040 = _M0L1cS879->$2;
  #line 347 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
  if (
    _M0L3symS2040 == (moonbit_string_t)moonbit_string_literal_1.data
    || Moonbit_array_length(_M0L3symS2040)
       == Moonbit_array_length((moonbit_string_t)moonbit_string_literal_1.data)
       && 0
          == memcmp(_M0L3symS2040, (moonbit_string_t)moonbit_string_literal_1.data, Moonbit_array_length(_M0L3symS2040) * 2)
  ) {
    struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS2046 = _M0L1cS879->$1;
    struct _M0TPB5ArrayGfE* _M0L3gluS2041 = _M0L4postS2046->$13;
    struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS2045 = _M0L1cS879->$1;
    struct _M0TPB5ArrayGfE* _M0L3gluS2044 = _M0L4postS2045->$13;
    float _M0L6_2atmpS2043;
    float _M0L6_2atmpS2042;
    #line 348 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
    _M0L6_2atmpS2043
    = _M0MPC15array5Array2atGfE(_M0L3gluS2044, _M0L9post__idxS880);
    _M0L6_2atmpS2042 = _M0L6_2atmpS2043 + _M0L1wS881;
    #line 348 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
    _M0MPC15array5Array3setGfE(_M0L3gluS2041, _M0L9post__idxS880, _M0L6_2atmpS2042);
  } else {
    struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS2052 = _M0L1cS879->$1;
    struct _M0TPB5ArrayGfE* _M0L4gabaS2047 = _M0L4postS2052->$14;
    struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS2051 = _M0L1cS879->$1;
    struct _M0TPB5ArrayGfE* _M0L4gabaS2050 = _M0L4postS2051->$14;
    float _M0L6_2atmpS2049;
    float _M0L6_2atmpS2048;
    #line 350 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
    _M0L6_2atmpS2049
    = _M0MPC15array5Array2atGfE(_M0L4gabaS2050, _M0L9post__idxS880);
    _M0L6_2atmpS2048 = _M0L6_2atmpS2049 + _M0L1wS881;
    #line 350 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
    _M0MPC15array5Array3setGfE(_M0L4gabaS2047, _M0L9post__idxS880, _M0L6_2atmpS2048);
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt12record__zero(
  struct _M0TP26RiantR8snn__mbt5Model* _M0L5modelS873
) {
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt7MonitorE* _M0L7_2abindS872;
  int32_t _M0L7_2abindS874;
  struct _M0TP26RiantR8snn__mbt7Monitor** _M0L7_2abindS875;
  int32_t _M0L2__S876;
  #line 87 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
  _M0L7_2abindS872 = _M0L5modelS873->$2;
  _M0L7_2abindS874 = _M0L7_2abindS872->$1;
  _M0L7_2abindS875 = _M0L7_2abindS872->$0;
  moonbit_incref(_M0L7_2abindS875);
  _M0L2__S876 = 0;
  while (1) {
    if (_M0L2__S876 < _M0L7_2abindS874) {
      struct _M0TP26RiantR8snn__mbt7Monitor* _M0L1mS877 =
        (struct _M0TP26RiantR8snn__mbt7Monitor*)_M0L7_2abindS875[_M0L2__S876];
      int32_t _M0L6_2atmpS2039;
      moonbit_incref(_M0L1mS877);
      #line 89 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
      _M0FP26RiantR8snn__mbt11record__one(_M0L1mS877, 0x0p+0f);
      moonbit_decref(_M0L1mS877);
      _M0L6_2atmpS2039 = _M0L2__S876 + 1;
      _M0L2__S876 = _M0L6_2atmpS2039;
      continue;
    } else {
      moonbit_decref(_M0L7_2abindS875);
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt11record__one(
  struct _M0TP26RiantR8snn__mbt7Monitor* _M0L1mS869,
  float _M0L1tS871
) {
  int32_t _M0L11step__countS2025;
  int32_t _M0L6_2atmpS2024;
  int32_t _M0L11step__countS2027;
  int32_t _M0L9rec__stepS2028;
  int32_t _M0L6_2atmpS2026;
  moonbit_string_t _M0L3symS2031;
  float _M0L1vS870;
  struct _M0TPB5ArrayGfE* _M0L4dataS2029;
  struct _M0TPB5ArrayGfE* _M0L5timesS2030;
  #line 61 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
  _M0L11step__countS2025 = _M0L1mS869->$6;
  _M0L6_2atmpS2024 = _M0L11step__countS2025 + 1;
  _M0L1mS869->$6 = _M0L6_2atmpS2024;
  _M0L11step__countS2027 = _M0L1mS869->$6;
  _M0L9rec__stepS2028 = _M0L1mS869->$5;
  _M0L6_2atmpS2026 = _M0L11step__countS2027 % _M0L9rec__stepS2028;
  if (_M0L6_2atmpS2026 != 0) {
    return 0;
  }
  _M0L3symS2031 = _M0L1mS869->$1;
  #line 66 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
  if (
    _M0L3symS2031 == (moonbit_string_t)moonbit_string_literal_2.data
    || Moonbit_array_length(_M0L3symS2031)
       == Moonbit_array_length((moonbit_string_t)moonbit_string_literal_2.data)
       && 0
          == memcmp(_M0L3symS2031, (moonbit_string_t)moonbit_string_literal_2.data, Moonbit_array_length(_M0L3symS2031) * 2)
  ) {
    struct _M0TP26RiantR8snn__mbt2IF* _M0L3popS2034 = _M0L1mS869->$0;
    struct _M0TPB5ArrayGfE* _M0L1vS2032 = _M0L3popS2034->$3;
    int32_t _M0L6neuronS2033 = _M0L1mS869->$4;
    #line 67 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
    _M0L1vS870 = _M0MPC15array5Array2atGfE(_M0L1vS2032, _M0L6neuronS2033);
  } else {
    moonbit_string_t _M0L3symS2035 = _M0L1mS869->$1;
    #line 68 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
    if (
      _M0L3symS2035 == (moonbit_string_t)moonbit_string_literal_3.data
      || Moonbit_array_length(_M0L3symS2035)
         == Moonbit_array_length((moonbit_string_t)moonbit_string_literal_3.data)
         && 0
            == memcmp(_M0L3symS2035, (moonbit_string_t)moonbit_string_literal_3.data, Moonbit_array_length(_M0L3symS2035) * 2)
    ) {
      struct _M0TP26RiantR8snn__mbt2IF* _M0L3popS2038 = _M0L1mS869->$0;
      struct _M0TPB5ArrayGbE* _M0L4fireS2036 = _M0L3popS2038->$5;
      int32_t _M0L6neuronS2037 = _M0L1mS869->$4;
      #line 69 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS2036, _M0L6neuronS2037)) {
        _M0L1vS870 = 0x1p+0f;
      } else {
        _M0L1vS870 = 0x0p+0f;
      }
    } else {
      _M0L1vS870 = 0x0p+0f;
    }
  }
  _M0L4dataS2029 = _M0L1mS869->$2;
  #line 73 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
  _M0MPC15array5Array4pushGfE(_M0L4dataS2029, _M0L1vS870);
  _M0L5timesS2030 = _M0L1mS869->$3;
  #line 74 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
  _M0MPC15array5Array4pushGfE(_M0L5timesS2030, _M0L1tS871);
  return 0;
}

struct _M0TP26RiantR8snn__mbt7Monitor* _M0MP26RiantR8snn__mbt7Monitor9new__fire(
  struct _M0TP26RiantR8snn__mbt2IF* _M0L3popS867,
  int32_t _M0L6neuronS868
) {
  float* _M0L6_2atmpS2023;
  struct _M0TPB5ArrayGfE* _M0L6_2atmpS2020;
  float* _M0L6_2atmpS2022;
  struct _M0TPB5ArrayGfE* _M0L6_2atmpS2021;
  struct _M0TP26RiantR8snn__mbt7Monitor* _block_2266;
  #line 55 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
  _M0L6_2atmpS2023 = moonbit_empty_float_array;
  _M0L6_2atmpS2020
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_M0L6_2atmpS2020)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 0, 0);
  _M0L6_2atmpS2020->$0 = _M0L6_2atmpS2023;
  _M0L6_2atmpS2020->$1 = 0;
  _M0L6_2atmpS2022 = moonbit_empty_float_array;
  _M0L6_2atmpS2021
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_M0L6_2atmpS2021)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 0, 0);
  _M0L6_2atmpS2021->$0 = _M0L6_2atmpS2022;
  _M0L6_2atmpS2021->$1 = 0;
  moonbit_incref(_M0L3popS867);
  _block_2266
  = (struct _M0TP26RiantR8snn__mbt7Monitor*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt7Monitor));
  Moonbit_object_header(_block_2266)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 36, 0);
  _block_2266->$0 = _M0L3popS867;
  _block_2266->$1 = (moonbit_string_t)moonbit_string_literal_3.data;
  _block_2266->$2 = _M0L6_2atmpS2020;
  _block_2266->$3 = _M0L6_2atmpS2021;
  _block_2266->$4 = _M0L6neuronS868;
  _block_2266->$5 = 1;
  _block_2266->$6 = 0;
  return _block_2266;
}

int32_t _M0FP26RiantR8snn__mbt17synaptic__current(
  struct _M0TP26RiantR8snn__mbt2IF* _M0L1pS863
) {
  int32_t _M0L1nS862;
  int32_t _M0L7_2abindS864;
  int32_t _M0L1iS865;
  #line 165 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L1nS862 = _M0L1pS863->$2;
  _M0L7_2abindS864 = 0;
  _M0L1iS865 = _M0L7_2abindS864;
  while (1) {
    if (_M0L1iS865 < _M0L1nS862) {
      struct _M0TPB5ArrayGfE* _M0L9syn__currS1997 = _M0L1pS863->$8;
      struct _M0TPB5ArrayGfE* _M0L2geS2018 = _M0L1pS863->$9;
      float _M0L6_2atmpS2013;
      struct _M0TPB5ArrayGfE* _M0L1vS2017;
      float _M0L6_2atmpS2015;
      float _M0L4e__eS2016;
      float _M0L6_2atmpS2014;
      float _M0L6_2atmpS2010;
      struct _M0TPB5ArrayGfE* _M0L7gsyn__eS2012;
      float _M0L6_2atmpS2011;
      float _M0L6_2atmpS1999;
      struct _M0TPB5ArrayGfE* _M0L2giS2009;
      float _M0L6_2atmpS2004;
      struct _M0TPB5ArrayGfE* _M0L1vS2008;
      float _M0L6_2atmpS2006;
      float _M0L4e__iS2007;
      float _M0L6_2atmpS2005;
      float _M0L6_2atmpS2001;
      struct _M0TPB5ArrayGfE* _M0L7gsyn__iS2003;
      float _M0L6_2atmpS2002;
      float _M0L6_2atmpS2000;
      float _M0L6_2atmpS1998;
      int32_t _M0L6_2atmpS2019;
      #line 168 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2013 = _M0MPC15array5Array2atGfE(_M0L2geS2018, _M0L1iS865);
      _M0L1vS2017 = _M0L1pS863->$3;
      #line 168 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2015 = _M0MPC15array5Array2atGfE(_M0L1vS2017, _M0L1iS865);
      _M0L4e__eS2016 = _M0L1pS863->$17;
      _M0L6_2atmpS2014 = _M0L6_2atmpS2015 - _M0L4e__eS2016;
      _M0L6_2atmpS2010 = _M0L6_2atmpS2013 * _M0L6_2atmpS2014;
      _M0L7gsyn__eS2012 = _M0L1pS863->$15;
      #line 168 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2011
      = _M0MPC15array5Array2atGfE(_M0L7gsyn__eS2012, _M0L1iS865);
      _M0L6_2atmpS1999 = _M0L6_2atmpS2010 * _M0L6_2atmpS2011;
      _M0L2giS2009 = _M0L1pS863->$10;
      #line 169 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2004 = _M0MPC15array5Array2atGfE(_M0L2giS2009, _M0L1iS865);
      _M0L1vS2008 = _M0L1pS863->$3;
      #line 169 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2006 = _M0MPC15array5Array2atGfE(_M0L1vS2008, _M0L1iS865);
      _M0L4e__iS2007 = _M0L1pS863->$18;
      _M0L6_2atmpS2005 = _M0L6_2atmpS2006 - _M0L4e__iS2007;
      _M0L6_2atmpS2001 = _M0L6_2atmpS2004 * _M0L6_2atmpS2005;
      _M0L7gsyn__iS2003 = _M0L1pS863->$16;
      #line 169 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2002
      = _M0MPC15array5Array2atGfE(_M0L7gsyn__iS2003, _M0L1iS865);
      _M0L6_2atmpS2000 = _M0L6_2atmpS2001 * _M0L6_2atmpS2002;
      _M0L6_2atmpS1998 = _M0L6_2atmpS1999 + _M0L6_2atmpS2000;
      #line 168 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L9syn__currS1997, _M0L1iS865, _M0L6_2atmpS1998);
      _M0L6_2atmpS2019 = _M0L1iS865 + 1;
      _M0L1iS865 = _M0L6_2atmpS2019;
      continue;
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt14step__synapses(
  struct _M0TP26RiantR8snn__mbt2IF* _M0L1pS854,
  float _M0L2dtS857
) {
  int32_t _M0L1nS853;
  int32_t _M0L7_2abindS855;
  int32_t _M0L1iS856;
  int32_t _M0L7_2abindS859;
  int32_t _M0L1iS860;
  #line 146 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L1nS853 = _M0L1pS854->$2;
  _M0L7_2abindS855 = 0;
  _M0L1iS856 = _M0L7_2abindS855;
  while (1) {
    if (_M0L1iS856 < _M0L1nS853) {
      struct _M0TPB5ArrayGfE* _M0L2heS1935 = _M0L1pS854->$11;
      struct _M0TPB5ArrayGfE* _M0L2heS1940 = _M0L1pS854->$11;
      float _M0L6_2atmpS1937;
      struct _M0TPB5ArrayGfE* _M0L3gluS1939;
      float _M0L6_2atmpS1938;
      float _M0L6_2atmpS1936;
      struct _M0TPB5ArrayGfE* _M0L2hiS1941;
      struct _M0TPB5ArrayGfE* _M0L2hiS1946;
      float _M0L6_2atmpS1943;
      struct _M0TPB5ArrayGfE* _M0L4gabaS1945;
      float _M0L6_2atmpS1944;
      float _M0L6_2atmpS1942;
      struct _M0TPB5ArrayGfE* _M0L2geS1947;
      struct _M0TPB5ArrayGfE* _M0L2geS1959;
      float _M0L6_2atmpS1949;
      struct _M0TPB5ArrayGfE* _M0L2geS1958;
      float _M0L6_2atmpS1957;
      float _M0L6_2atmpS1955;
      float _M0L3tdeS1956;
      float _M0L6_2atmpS1952;
      struct _M0TPB5ArrayGfE* _M0L2heS1954;
      float _M0L6_2atmpS1953;
      float _M0L6_2atmpS1951;
      float _M0L6_2atmpS1950;
      float _M0L6_2atmpS1948;
      struct _M0TPB5ArrayGfE* _M0L2heS1960;
      struct _M0TPB5ArrayGfE* _M0L2heS1969;
      float _M0L6_2atmpS1962;
      struct _M0TPB5ArrayGfE* _M0L2heS1968;
      float _M0L6_2atmpS1967;
      float _M0L6_2atmpS1965;
      float _M0L3treS1966;
      float _M0L6_2atmpS1964;
      float _M0L6_2atmpS1963;
      float _M0L6_2atmpS1961;
      struct _M0TPB5ArrayGfE* _M0L2giS1970;
      struct _M0TPB5ArrayGfE* _M0L2giS1982;
      float _M0L6_2atmpS1972;
      struct _M0TPB5ArrayGfE* _M0L2giS1981;
      float _M0L6_2atmpS1980;
      float _M0L6_2atmpS1978;
      float _M0L3tdiS1979;
      float _M0L6_2atmpS1975;
      struct _M0TPB5ArrayGfE* _M0L2hiS1977;
      float _M0L6_2atmpS1976;
      float _M0L6_2atmpS1974;
      float _M0L6_2atmpS1973;
      float _M0L6_2atmpS1971;
      struct _M0TPB5ArrayGfE* _M0L2hiS1983;
      struct _M0TPB5ArrayGfE* _M0L2hiS1992;
      float _M0L6_2atmpS1985;
      struct _M0TPB5ArrayGfE* _M0L2hiS1991;
      float _M0L6_2atmpS1990;
      float _M0L6_2atmpS1988;
      float _M0L3triS1989;
      float _M0L6_2atmpS1987;
      float _M0L6_2atmpS1986;
      float _M0L6_2atmpS1984;
      int32_t _M0L6_2atmpS1993;
      #line 149 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1937 = _M0MPC15array5Array2atGfE(_M0L2heS1940, _M0L1iS856);
      _M0L3gluS1939 = _M0L1pS854->$13;
      #line 149 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1938 = _M0MPC15array5Array2atGfE(_M0L3gluS1939, _M0L1iS856);
      _M0L6_2atmpS1936 = _M0L6_2atmpS1937 + _M0L6_2atmpS1938;
      #line 149 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L2heS1935, _M0L1iS856, _M0L6_2atmpS1936);
      _M0L2hiS1941 = _M0L1pS854->$12;
      _M0L2hiS1946 = _M0L1pS854->$12;
      #line 150 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1943 = _M0MPC15array5Array2atGfE(_M0L2hiS1946, _M0L1iS856);
      _M0L4gabaS1945 = _M0L1pS854->$14;
      #line 150 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1944
      = _M0MPC15array5Array2atGfE(_M0L4gabaS1945, _M0L1iS856);
      _M0L6_2atmpS1942 = _M0L6_2atmpS1943 + _M0L6_2atmpS1944;
      #line 150 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L2hiS1941, _M0L1iS856, _M0L6_2atmpS1942);
      _M0L2geS1947 = _M0L1pS854->$9;
      _M0L2geS1959 = _M0L1pS854->$9;
      #line 151 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1949 = _M0MPC15array5Array2atGfE(_M0L2geS1959, _M0L1iS856);
      _M0L2geS1958 = _M0L1pS854->$9;
      #line 151 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1957 = _M0MPC15array5Array2atGfE(_M0L2geS1958, _M0L1iS856);
      _M0L6_2atmpS1955 = -_M0L6_2atmpS1957;
      _M0L3tdeS1956 = _M0L1pS854->$20;
      _M0L6_2atmpS1952 = _M0L6_2atmpS1955 / _M0L3tdeS1956;
      _M0L2heS1954 = _M0L1pS854->$11;
      #line 151 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1953 = _M0MPC15array5Array2atGfE(_M0L2heS1954, _M0L1iS856);
      _M0L6_2atmpS1951 = _M0L6_2atmpS1952 + _M0L6_2atmpS1953;
      _M0L6_2atmpS1950 = _M0L2dtS857 * _M0L6_2atmpS1951;
      _M0L6_2atmpS1948 = _M0L6_2atmpS1949 + _M0L6_2atmpS1950;
      #line 151 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L2geS1947, _M0L1iS856, _M0L6_2atmpS1948);
      _M0L2heS1960 = _M0L1pS854->$11;
      _M0L2heS1969 = _M0L1pS854->$11;
      #line 152 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1962 = _M0MPC15array5Array2atGfE(_M0L2heS1969, _M0L1iS856);
      _M0L2heS1968 = _M0L1pS854->$11;
      #line 152 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1967 = _M0MPC15array5Array2atGfE(_M0L2heS1968, _M0L1iS856);
      _M0L6_2atmpS1965 = -_M0L6_2atmpS1967;
      _M0L3treS1966 = _M0L1pS854->$19;
      _M0L6_2atmpS1964 = _M0L6_2atmpS1965 / _M0L3treS1966;
      _M0L6_2atmpS1963 = _M0L2dtS857 * _M0L6_2atmpS1964;
      _M0L6_2atmpS1961 = _M0L6_2atmpS1962 + _M0L6_2atmpS1963;
      #line 152 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L2heS1960, _M0L1iS856, _M0L6_2atmpS1961);
      _M0L2giS1970 = _M0L1pS854->$10;
      _M0L2giS1982 = _M0L1pS854->$10;
      #line 153 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1972 = _M0MPC15array5Array2atGfE(_M0L2giS1982, _M0L1iS856);
      _M0L2giS1981 = _M0L1pS854->$10;
      #line 153 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1980 = _M0MPC15array5Array2atGfE(_M0L2giS1981, _M0L1iS856);
      _M0L6_2atmpS1978 = -_M0L6_2atmpS1980;
      _M0L3tdiS1979 = _M0L1pS854->$22;
      _M0L6_2atmpS1975 = _M0L6_2atmpS1978 / _M0L3tdiS1979;
      _M0L2hiS1977 = _M0L1pS854->$12;
      #line 153 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1976 = _M0MPC15array5Array2atGfE(_M0L2hiS1977, _M0L1iS856);
      _M0L6_2atmpS1974 = _M0L6_2atmpS1975 + _M0L6_2atmpS1976;
      _M0L6_2atmpS1973 = _M0L2dtS857 * _M0L6_2atmpS1974;
      _M0L6_2atmpS1971 = _M0L6_2atmpS1972 + _M0L6_2atmpS1973;
      #line 153 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L2giS1970, _M0L1iS856, _M0L6_2atmpS1971);
      _M0L2hiS1983 = _M0L1pS854->$12;
      _M0L2hiS1992 = _M0L1pS854->$12;
      #line 154 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1985 = _M0MPC15array5Array2atGfE(_M0L2hiS1992, _M0L1iS856);
      _M0L2hiS1991 = _M0L1pS854->$12;
      #line 154 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1990 = _M0MPC15array5Array2atGfE(_M0L2hiS1991, _M0L1iS856);
      _M0L6_2atmpS1988 = -_M0L6_2atmpS1990;
      _M0L3triS1989 = _M0L1pS854->$21;
      _M0L6_2atmpS1987 = _M0L6_2atmpS1988 / _M0L3triS1989;
      _M0L6_2atmpS1986 = _M0L2dtS857 * _M0L6_2atmpS1987;
      _M0L6_2atmpS1984 = _M0L6_2atmpS1985 + _M0L6_2atmpS1986;
      #line 154 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L2hiS1983, _M0L1iS856, _M0L6_2atmpS1984);
      _M0L6_2atmpS1993 = _M0L1iS856 + 1;
      _M0L1iS856 = _M0L6_2atmpS1993;
      continue;
    }
    break;
  }
  _M0L7_2abindS859 = 0;
  _M0L1iS860 = _M0L7_2abindS859;
  while (1) {
    if (_M0L1iS860 < _M0L1nS853) {
      struct _M0TPB5ArrayGfE* _M0L3gluS1994 = _M0L1pS854->$13;
      struct _M0TPB5ArrayGfE* _M0L4gabaS1995;
      int32_t _M0L6_2atmpS1996;
      #line 157 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L3gluS1994, _M0L1iS860, 0x0p+0f);
      _M0L4gabaS1995 = _M0L1pS854->$14;
      #line 158 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L4gabaS1995, _M0L1iS860, 0x0p+0f);
      _M0L6_2atmpS1996 = _M0L1iS860 + 1;
      _M0L1iS860 = _M0L6_2atmpS1996;
      continue;
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt12step__neuron(
  struct _M0TP26RiantR8snn__mbt2IF* _M0L1pS839,
  float _M0L2dtS848
) {
  int32_t _M0L1nS838;
  struct _M0TP26RiantR8snn__mbt11IFParameter* _M0L3p__S840;
  float _M0L2tmS841;
  float _M0L2elS842;
  float _M0L1rS843;
  float _M0L2vtS844;
  float _M0L2vrS845;
  struct _M0TP26RiantR8snn__mbt9PostSpike* _M0L5spikeS1934;
  float _M0L11tabs__constS846;
  float _M0L6_2atmpS1933;
  int32_t _M0L11tabs__stepsS847;
  int32_t _M0L7_2abindS849;
  int32_t _M0L1iS850;
  #line 176 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L1nS838 = _M0L1pS839->$2;
  _M0L3p__S840 = _M0L1pS839->$0;
  _M0L2tmS841 = _M0L3p__S840->$2;
  _M0L2elS842 = _M0L3p__S840->$5;
  _M0L1rS843 = _M0L3p__S840->$6;
  _M0L2vtS844 = _M0L3p__S840->$3;
  _M0L2vrS845 = _M0L3p__S840->$4;
  _M0L5spikeS1934 = _M0L1pS839->$1;
  _M0L11tabs__constS846 = _M0L5spikeS1934->$0;
  _M0L6_2atmpS1933 = _M0L11tabs__constS846 / _M0L2dtS848;
  #line 185 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L11tabs__stepsS847 = _M0MPC15float5Float7to__int(_M0L6_2atmpS1933);
  _M0L7_2abindS849 = 0;
  _M0L1iS850 = _M0L7_2abindS849;
  while (1) {
    if (_M0L1iS850 < _M0L1nS838) {
      struct _M0TPB5ArrayGiE* _M0L4tabsS1893 = _M0L1pS839->$6;
      int32_t _M0L6_2atmpS1892;
      struct _M0TPB5ArrayGfE* _M0L1vS1899;
      struct _M0TPB5ArrayGfE* _M0L1vS1920;
      float _M0L6_2atmpS1901;
      float _M0L6_2atmpS1903;
      struct _M0TPB5ArrayGfE* _M0L1vS1919;
      float _M0L6_2atmpS1918;
      float _M0L6_2atmpS1917;
      float _M0L6_2atmpS1909;
      struct _M0TPB5ArrayGfE* _M0L1wS1916;
      float _M0L6_2atmpS1915;
      float _M0L6_2atmpS1912;
      struct _M0TPB5ArrayGfE* _M0L1iS1914;
      float _M0L6_2atmpS1913;
      float _M0L6_2atmpS1911;
      float _M0L6_2atmpS1910;
      float _M0L6_2atmpS1905;
      struct _M0TPB5ArrayGfE* _M0L9syn__currS1908;
      float _M0L6_2atmpS1907;
      float _M0L6_2atmpS1906;
      float _M0L6_2atmpS1904;
      float _M0L6_2atmpS1902;
      float _M0L6_2atmpS1900;
      struct _M0TPB5ArrayGbE* _M0L4fireS1921;
      struct _M0TPB5ArrayGfE* _M0L1vS1924;
      float _M0L6_2atmpS1923;
      int32_t _M0L6_2atmpS1922;
      struct _M0TPB5ArrayGfE* _M0L1vS1925;
      struct _M0TPB5ArrayGbE* _M0L4fireS1927;
      float _M0L6_2atmpS1926;
      struct _M0TPB5ArrayGiE* _M0L4tabsS1929;
      struct _M0TPB5ArrayGbE* _M0L4fireS1931;
      int32_t _M0L6_2atmpS1930;
      int32_t _M0L6_2atmpS1891;
      #line 188 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1892
      = _M0MPC15array5Array2atGiE(_M0L4tabsS1893, _M0L1iS850);
      if (_M0L6_2atmpS1892 > 0) {
        struct _M0TPB5ArrayGbE* _M0L4fireS1894 = _M0L1pS839->$5;
        struct _M0TPB5ArrayGiE* _M0L4tabsS1895;
        struct _M0TPB5ArrayGiE* _M0L4tabsS1898;
        int32_t _M0L6_2atmpS1897;
        int32_t _M0L6_2atmpS1896;
        #line 189 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
        _M0MPC15array5Array3setGbE(_M0L4fireS1894, _M0L1iS850, 0);
        _M0L4tabsS1895 = _M0L1pS839->$6;
        _M0L4tabsS1898 = _M0L1pS839->$6;
        #line 190 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
        _M0L6_2atmpS1897
        = _M0MPC15array5Array2atGiE(_M0L4tabsS1898, _M0L1iS850);
        _M0L6_2atmpS1896 = _M0L6_2atmpS1897 - 1;
        #line 190 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
        _M0MPC15array5Array3setGiE(_M0L4tabsS1895, _M0L1iS850, _M0L6_2atmpS1896);
        goto join_851;
      }
      _M0L1vS1899 = _M0L1pS839->$3;
      _M0L1vS1920 = _M0L1pS839->$3;
      #line 193 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1901 = _M0MPC15array5Array2atGfE(_M0L1vS1920, _M0L1iS850);
      _M0L6_2atmpS1903 = _M0L2dtS848 / _M0L2tmS841;
      _M0L1vS1919 = _M0L1pS839->$3;
      #line 194 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1918 = _M0MPC15array5Array2atGfE(_M0L1vS1919, _M0L1iS850);
      _M0L6_2atmpS1917 = _M0L6_2atmpS1918 - _M0L2elS842;
      _M0L6_2atmpS1909 = -_M0L6_2atmpS1917;
      _M0L1wS1916 = _M0L1pS839->$4;
      #line 194 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1915 = _M0MPC15array5Array2atGfE(_M0L1wS1916, _M0L1iS850);
      _M0L6_2atmpS1912 = -_M0L6_2atmpS1915;
      _M0L1iS1914 = _M0L1pS839->$7;
      #line 194 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1913 = _M0MPC15array5Array2atGfE(_M0L1iS1914, _M0L1iS850);
      _M0L6_2atmpS1911 = _M0L6_2atmpS1912 + _M0L6_2atmpS1913;
      _M0L6_2atmpS1910 = _M0L1rS843 * _M0L6_2atmpS1911;
      _M0L6_2atmpS1905 = _M0L6_2atmpS1909 + _M0L6_2atmpS1910;
      _M0L9syn__currS1908 = _M0L1pS839->$8;
      #line 194 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1907
      = _M0MPC15array5Array2atGfE(_M0L9syn__currS1908, _M0L1iS850);
      _M0L6_2atmpS1906 = _M0L1rS843 * _M0L6_2atmpS1907;
      _M0L6_2atmpS1904 = _M0L6_2atmpS1905 - _M0L6_2atmpS1906;
      _M0L6_2atmpS1902 = _M0L6_2atmpS1903 * _M0L6_2atmpS1904;
      _M0L6_2atmpS1900 = _M0L6_2atmpS1901 + _M0L6_2atmpS1902;
      #line 193 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS1899, _M0L1iS850, _M0L6_2atmpS1900);
      _M0L4fireS1921 = _M0L1pS839->$5;
      _M0L1vS1924 = _M0L1pS839->$3;
      #line 195 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1923 = _M0MPC15array5Array2atGfE(_M0L1vS1924, _M0L1iS850);
      _M0L6_2atmpS1922 = _M0L6_2atmpS1923 > _M0L2vtS844;
      #line 195 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGbE(_M0L4fireS1921, _M0L1iS850, _M0L6_2atmpS1922);
      _M0L1vS1925 = _M0L1pS839->$3;
      _M0L4fireS1927 = _M0L1pS839->$5;
      #line 196 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS1927, _M0L1iS850)) {
        _M0L6_2atmpS1926 = _M0L2vrS845;
      } else {
        struct _M0TPB5ArrayGfE* _M0L1vS1928 = _M0L1pS839->$3;
        #line 196 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
        _M0L6_2atmpS1926 = _M0MPC15array5Array2atGfE(_M0L1vS1928, _M0L1iS850);
      }
      #line 196 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS1925, _M0L1iS850, _M0L6_2atmpS1926);
      _M0L4tabsS1929 = _M0L1pS839->$6;
      _M0L4fireS1931 = _M0L1pS839->$5;
      #line 197 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS1931, _M0L1iS850)) {
        _M0L6_2atmpS1930 = _M0L11tabs__stepsS847;
      } else {
        struct _M0TPB5ArrayGiE* _M0L4tabsS1932 = _M0L1pS839->$6;
        #line 197 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
        _M0L6_2atmpS1930
        = _M0MPC15array5Array2atGiE(_M0L4tabsS1932, _M0L1iS850);
      }
      #line 197 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGiE(_M0L4tabsS1929, _M0L1iS850, _M0L6_2atmpS1930);
      goto join_851;
      goto joinlet_2271;
      join_851:;
      _M0L6_2atmpS1891 = _M0L1iS850 + 1;
      _M0L1iS850 = _M0L6_2atmpS1891;
      continue;
      joinlet_2271:;
    }
    break;
  }
  return 0;
}

int32_t _M0MP26RiantR8snn__mbt15SparseMatrixCSR7forward(
  struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L1mS826,
  struct _M0TPB5ArrayGbE* _M0L9pre__fireS829,
  struct _M0TPB5ArrayGfE* _M0L7post__gS835
) {
  int32_t _M0L4rowsS825;
  int32_t _M0L7_2abindS827;
  int32_t _M0L1iS828;
  #line 287 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  _M0L4rowsS825 = _M0L1mS826->$0;
  _M0L7_2abindS827 = 0;
  _M0L1iS828 = _M0L7_2abindS827;
  while (1) {
    if (_M0L1iS828 < _M0L4rowsS825) {
      int32_t _M0L6_2atmpS1890;
      #line 294 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L9pre__fireS829, _M0L1iS828)) {
        struct _M0TPB5ArrayGiE* _M0L6rowptrS1889 = _M0L1mS826->$2;
        int32_t _M0L5startS830;
        struct _M0TPB5ArrayGiE* _M0L6rowptrS1887;
        int32_t _M0L6_2atmpS1888;
        int32_t _M0L3endS831;
        int32_t _M0L1kS832;
        #line 295 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
        _M0L5startS830
        = _M0MPC15array5Array2atGiE(_M0L6rowptrS1889, _M0L1iS828);
        _M0L6rowptrS1887 = _M0L1mS826->$2;
        _M0L6_2atmpS1888 = _M0L1iS828 + 1;
        #line 296 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
        _M0L3endS831
        = _M0MPC15array5Array2atGiE(_M0L6rowptrS1887, _M0L6_2atmpS1888);
        _M0L1kS832 = _M0L5startS830;
        while (1) {
          if (_M0L1kS832 < _M0L3endS831) {
            struct _M0TPB5ArrayGiE* _M0L6colptrS1885 = _M0L1mS826->$3;
            int32_t _M0L9post__idxS833;
            struct _M0TPB5ArrayGfE* _M0L4valsS1884;
            float _M0L1wS834;
            float _M0L6_2atmpS1883;
            float _M0L6_2atmpS1882;
            int32_t _M0L6_2atmpS1886;
            #line 298 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
            _M0L9post__idxS833
            = _M0MPC15array5Array2atGiE(_M0L6colptrS1885, _M0L1kS832);
            _M0L4valsS1884 = _M0L1mS826->$4;
            #line 299 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
            _M0L1wS834
            = _M0MPC15array5Array2atGfE(_M0L4valsS1884, _M0L1kS832);
            #line 300 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
            _M0L6_2atmpS1883
            = _M0MPC15array5Array2atGfE(_M0L7post__gS835, _M0L9post__idxS833);
            _M0L6_2atmpS1882 = _M0L6_2atmpS1883 + _M0L1wS834;
            #line 300 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
            _M0MPC15array5Array3setGfE(_M0L7post__gS835, _M0L9post__idxS833, _M0L6_2atmpS1882);
            _M0L6_2atmpS1886 = _M0L1kS832 + 1;
            _M0L1kS832 = _M0L6_2atmpS1886;
            continue;
          }
          break;
        }
      }
      _M0L6_2atmpS1890 = _M0L1iS828 + 1;
      _M0L1iS828 = _M0L6_2atmpS1890;
      continue;
    }
    break;
  }
  return 0;
}

struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0MP26RiantR8snn__mbt15SparseMatrixCSR6random(
  int32_t _M0L4rowsS819,
  int32_t _M0L4colsS820,
  float _M0L2muS821,
  float _M0L5sigmaS822,
  float _M0L1pS823,
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS824
) {
  #line 94 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  #line 103 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  return _M0MP26RiantR8snn__mbt15SparseMatrixCSR18random__with__rule(_M0L4rowsS819, _M0L4colsS820, _M0L2muS821, _M0L5sigmaS822, _M0L1pS823, 0, _M0L3rngS824);
}

struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0MP26RiantR8snn__mbt15SparseMatrixCSR18random__with__rule(
  int32_t _M0L4rowsS733,
  int32_t _M0L4colsS737,
  float _M0L2muS743,
  float _M0L5sigmaS744,
  float _M0L1pS756,
  int32_t _M0L4ruleS750,
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS746
) {
  float* _M0L6_2atmpS1881;
  struct _M0TPB5ArrayGfE* _M0L6_2atmpS1880;
  struct _M0TPB5ArrayGRPB5ArrayGfEE* _M0L5denseS732;
  int32_t _M0L7_2abindS734;
  int32_t _M0L1iS735;
  int32_t _M0L6_2atmpS1879;
  struct _M0TPB5ArrayGiE* _M0L6rowptrS809;
  int32_t* _M0L6_2atmpS1878;
  struct _M0TPB5ArrayGiE* _M0L6colptrS810;
  float* _M0L6_2atmpS1877;
  struct _M0TPB5ArrayGfE* _M0L4valsS811;
  int32_t _M0L7_2abindS812;
  int32_t _M0L1iS813;
  int32_t _M0L6_2atmpS1876;
  struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _block_2293;
  #line 109 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  _M0L6_2atmpS1881 = moonbit_empty_float_array;
  _M0L6_2atmpS1880
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_M0L6_2atmpS1880)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 0, 0);
  _M0L6_2atmpS1880->$0 = _M0L6_2atmpS1881;
  _M0L6_2atmpS1880->$1 = 0;
  #line 120 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  _M0L5denseS732
  = _M0MPC15array5Array4makeGRPB5ArrayGfEE(_M0L4rowsS733, _M0L6_2atmpS1880);
  _M0L7_2abindS734 = 0;
  _M0L1iS735 = _M0L7_2abindS734;
  while (1) {
    if (_M0L1iS735 < _M0L4rowsS733) {
      struct _M0TPB5ArrayGfE* _M0L3rowS736;
      int32_t _M0L7_2abindS738;
      int32_t _M0L1jS739;
      int32_t _M0L6_2atmpS1834;
      #line 122 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
      _M0L3rowS736 = _M0MPC15array5Array4makeGfE(_M0L4colsS737, 0x0p+0f);
      _M0L7_2abindS738 = 0;
      _M0L1jS739 = _M0L7_2abindS738;
      while (1) {
        if (_M0L1jS739 < _M0L4colsS737) {
          double _M0L2z1S741;
          struct _M0TUddE* _M0L7_2abindS745;
          double _M0L5_2az1S747;
          float _M0L6_2atmpS1832;
          float _M0L6_2atmpS1831;
          float _M0L1wS742;
          int32_t _M0L6_2atmpS1833;
          #line 131 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
          _M0L7_2abindS745
          = _M0FP26RiantR8snn__mbt11box__muller(_M0L3rngS746);
          _M0L5_2az1S747 = _M0L7_2abindS745->$0;
          moonbit_decref(_M0L7_2abindS745);
          _M0L2z1S741 = _M0L5_2az1S747;
          goto join_740;
          goto joinlet_2276;
          join_740:;
          _M0L6_2atmpS1832 = (float)_M0L2z1S741;
          _M0L6_2atmpS1831 = _M0L5sigmaS744 * _M0L6_2atmpS1832;
          _M0L1wS742 = _M0L2muS743 + _M0L6_2atmpS1831;
          #line 133 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
          _M0MPC15array5Array3setGfE(_M0L3rowS736, _M0L1jS739, _M0L1wS742);
          joinlet_2276:;
          _M0L6_2atmpS1833 = _M0L1jS739 + 1;
          _M0L1jS739 = _M0L6_2atmpS1833;
          continue;
        }
        break;
      }
      #line 135 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
      _M0MPC15array5Array3setGRPB5ArrayGfEE(_M0L5denseS732, _M0L1iS735, _M0L3rowS736);
      _M0L6_2atmpS1834 = _M0L1iS735 + 1;
      _M0L1iS735 = _M0L6_2atmpS1834;
      continue;
    }
    break;
  }
  switch (_M0L4ruleS750) {
    case 0: {
      int32_t _M0L7_2abindS751 = 0;
      int32_t _M0L1iS752 = _M0L7_2abindS751;
      while (1) {
        if (_M0L1iS752 < _M0L4rowsS733) {
          int32_t _M0L7_2abindS753 = 0;
          int32_t _M0L1jS754 = _M0L7_2abindS753;
          int32_t _M0L6_2atmpS1837;
          while (1) {
            if (_M0L1jS754 < _M0L4colsS737) {
              float _M0L1uS755;
              int32_t _M0L6_2atmpS1836;
              #line 143 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
              _M0L1uS755 = _M0FP26RiantR8snn__mbt9next__f32(_M0L3rngS746);
              if (_M0L1uS755 >= _M0L1pS756) {
                struct _M0TPB5ArrayGfE* _M0L6_2atmpS1835;
                #line 145 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS1835
                = _M0MPC15array5Array2atGRPB5ArrayGfEE(_M0L5denseS732, _M0L1iS752);
                #line 145 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array3setGfE(_M0L6_2atmpS1835, _M0L1jS754, 0x0p+0f);
                moonbit_decref(_M0L6_2atmpS1835);
              }
              _M0L6_2atmpS1836 = _M0L1jS754 + 1;
              _M0L1jS754 = _M0L6_2atmpS1836;
              continue;
            }
            break;
          }
          _M0L6_2atmpS1837 = _M0L1iS752 + 1;
          _M0L1iS752 = _M0L6_2atmpS1837;
          continue;
        }
        break;
      }
      break;
    }
    
    case 1: {
      float _M0L6_2atmpS1854 = (float)_M0L4rowsS733;
      float _M0L6_2atmpS1853 = _M0L6_2atmpS1854 * _M0L1pS756;
      int32_t _M0L7n__keepS759;
      #line 154 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
      _M0L7n__keepS759 = _M0MPC15float5Float7to__int(_M0L6_2atmpS1853);
      if (_M0L7n__keepS759 > 0 && _M0L7n__keepS759 <= _M0L4rowsS733) {
        int32_t _M0L7_2abindS760 = 0;
        int32_t _M0L1jS761 = _M0L7_2abindS760;
        while (1) {
          if (_M0L1jS761 < _M0L4colsS737) {
            int32_t* _M0L6_2atmpS1848 = (int32_t*)moonbit_empty_int32_array;
            struct _M0TPB5ArrayGiE* _M0L8pre__idxS762 =
              (struct _M0TPB5ArrayGiE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGiE));
            int32_t _M0L7_2abindS763;
            int32_t _M0L1kS764;
            int32_t _M0L7n__dropS766;
            int32_t _M0L7_2abindS767;
            int32_t _M0L1kS768;
            int32_t _M0L7_2abindS774;
            int32_t _M0L1kS775;
            int32_t _M0L6_2atmpS1849;
            Moonbit_object_header(_M0L8pre__idxS762)->meta
            = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 3, 0);
            _M0L8pre__idxS762->$0 = _M0L6_2atmpS1848;
            _M0L8pre__idxS762->$1 = 0;
            _M0L7_2abindS763 = 0;
            _M0L1kS764 = _M0L7_2abindS763;
            while (1) {
              if (_M0L1kS764 < _M0L4rowsS733) {
                int32_t _M0L6_2atmpS1838;
                #line 161 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array4pushGiE(_M0L8pre__idxS762, _M0L1kS764);
                _M0L6_2atmpS1838 = _M0L1kS764 + 1;
                _M0L1kS764 = _M0L6_2atmpS1838;
                continue;
              }
              break;
            }
            _M0L7n__dropS766 = _M0L4rowsS733 - _M0L7n__keepS759;
            _M0L7_2abindS767 = 0;
            _M0L1kS768 = _M0L7_2abindS767;
            while (1) {
              if (_M0L1kS768 < _M0L7n__dropS766) {
                float _M0L1uS769;
                int32_t _M0L6_2atmpS1843;
                float _M0L6_2atmpS1842;
                float _M0L6_2atmpS1841;
                int32_t _M0L6_2atmpS1840;
                int32_t _M0L6r__idxS770;
                int32_t _M0L10r__clampedS771;
                int32_t _M0L3tmpS772;
                int32_t _M0L6_2atmpS1839;
                int32_t _M0L6_2atmpS1844;
                #line 167 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L1uS769 = _M0FP26RiantR8snn__mbt9next__f32(_M0L3rngS746);
                _M0L6_2atmpS1843 = _M0L4rowsS733 - _M0L1kS768;
                _M0L6_2atmpS1842 = (float)_M0L6_2atmpS1843;
                _M0L6_2atmpS1841 = _M0L6_2atmpS1842 * _M0L1uS769;
                #line 168 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS1840
                = _M0MPC15float5Float7to__int(_M0L6_2atmpS1841);
                _M0L6r__idxS770 = _M0L1kS768 + _M0L6_2atmpS1840;
                if (_M0L6r__idxS770 >= _M0L4rowsS733) {
                  _M0L10r__clampedS771 = _M0L4rowsS733 - 1;
                } else {
                  _M0L10r__clampedS771 = _M0L6r__idxS770;
                }
                #line 171 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L3tmpS772
                = _M0MPC15array5Array2atGiE(_M0L8pre__idxS762, _M0L1kS768);
                #line 172 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS1839
                = _M0MPC15array5Array2atGiE(_M0L8pre__idxS762, _M0L10r__clampedS771);
                #line 172 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array3setGiE(_M0L8pre__idxS762, _M0L1kS768, _M0L6_2atmpS1839);
                #line 173 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array3setGiE(_M0L8pre__idxS762, _M0L10r__clampedS771, _M0L3tmpS772);
                _M0L6_2atmpS1844 = _M0L1kS768 + 1;
                _M0L1kS768 = _M0L6_2atmpS1844;
                continue;
              }
              break;
            }
            _M0L7_2abindS774 = 0;
            _M0L1kS775 = _M0L7_2abindS774;
            while (1) {
              if (_M0L1kS775 < _M0L7n__dropS766) {
                int32_t _M0L6_2atmpS1846;
                struct _M0TPB5ArrayGfE* _M0L6_2atmpS1845;
                int32_t _M0L6_2atmpS1847;
                #line 176 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS1846
                = _M0MPC15array5Array2atGiE(_M0L8pre__idxS762, _M0L1kS775);
                #line 176 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS1845
                = _M0MPC15array5Array2atGRPB5ArrayGfEE(_M0L5denseS732, _M0L6_2atmpS1846);
                #line 176 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array3setGfE(_M0L6_2atmpS1845, _M0L1jS761, 0x0p+0f);
                moonbit_decref(_M0L6_2atmpS1845);
                _M0L6_2atmpS1847 = _M0L1kS775 + 1;
                _M0L1kS775 = _M0L6_2atmpS1847;
                continue;
              } else {
                moonbit_decref(_M0L8pre__idxS762);
              }
              break;
            }
            _M0L6_2atmpS1849 = _M0L1jS761 + 1;
            _M0L1jS761 = _M0L6_2atmpS1849;
            continue;
          }
          break;
        }
      } else if (_M0L7n__keepS759 == 0) {
        int32_t _M0L7_2abindS778 = 0;
        int32_t _M0L1iS779 = _M0L7_2abindS778;
        while (1) {
          if (_M0L1iS779 < _M0L4rowsS733) {
            int32_t _M0L7_2abindS780 = 0;
            int32_t _M0L1jS781 = _M0L7_2abindS780;
            int32_t _M0L6_2atmpS1852;
            while (1) {
              if (_M0L1jS781 < _M0L4colsS737) {
                struct _M0TPB5ArrayGfE* _M0L6_2atmpS1850;
                int32_t _M0L6_2atmpS1851;
                #line 183 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS1850
                = _M0MPC15array5Array2atGRPB5ArrayGfEE(_M0L5denseS732, _M0L1iS779);
                #line 183 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array3setGfE(_M0L6_2atmpS1850, _M0L1jS781, 0x0p+0f);
                moonbit_decref(_M0L6_2atmpS1850);
                _M0L6_2atmpS1851 = _M0L1jS781 + 1;
                _M0L1jS781 = _M0L6_2atmpS1851;
                continue;
              }
              break;
            }
            _M0L6_2atmpS1852 = _M0L1iS779 + 1;
            _M0L1iS779 = _M0L6_2atmpS1852;
            continue;
          }
          break;
        }
      }
      break;
    }
    default: {
      float _M0L6_2atmpS1871 = (float)_M0L4colsS737;
      float _M0L6_2atmpS1870 = _M0L6_2atmpS1871 * _M0L1pS756;
      int32_t _M0L7n__keepS784;
      #line 191 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
      _M0L7n__keepS784 = _M0MPC15float5Float7to__int(_M0L6_2atmpS1870);
      if (_M0L7n__keepS784 > 0 && _M0L7n__keepS784 <= _M0L4colsS737) {
        int32_t _M0L7_2abindS785 = 0;
        int32_t _M0L1iS786 = _M0L7_2abindS785;
        while (1) {
          if (_M0L1iS786 < _M0L4rowsS733) {
            int32_t* _M0L6_2atmpS1865 = (int32_t*)moonbit_empty_int32_array;
            struct _M0TPB5ArrayGiE* _M0L9post__idxS787 =
              (struct _M0TPB5ArrayGiE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGiE));
            int32_t _M0L7_2abindS788;
            int32_t _M0L1kS789;
            int32_t _M0L7n__dropS791;
            int32_t _M0L7_2abindS792;
            int32_t _M0L1kS793;
            int32_t _M0L7_2abindS799;
            int32_t _M0L1kS800;
            int32_t _M0L6_2atmpS1866;
            Moonbit_object_header(_M0L9post__idxS787)->meta
            = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 3, 0);
            _M0L9post__idxS787->$0 = _M0L6_2atmpS1865;
            _M0L9post__idxS787->$1 = 0;
            _M0L7_2abindS788 = 0;
            _M0L1kS789 = _M0L7_2abindS788;
            while (1) {
              if (_M0L1kS789 < _M0L4colsS737) {
                int32_t _M0L6_2atmpS1855;
                #line 196 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array4pushGiE(_M0L9post__idxS787, _M0L1kS789);
                _M0L6_2atmpS1855 = _M0L1kS789 + 1;
                _M0L1kS789 = _M0L6_2atmpS1855;
                continue;
              }
              break;
            }
            _M0L7n__dropS791 = _M0L4colsS737 - _M0L7n__keepS784;
            _M0L7_2abindS792 = 0;
            _M0L1kS793 = _M0L7_2abindS792;
            while (1) {
              if (_M0L1kS793 < _M0L7n__dropS791) {
                float _M0L1uS794;
                int32_t _M0L6_2atmpS1860;
                float _M0L6_2atmpS1859;
                float _M0L6_2atmpS1858;
                int32_t _M0L6_2atmpS1857;
                int32_t _M0L6r__idxS795;
                int32_t _M0L10r__clampedS796;
                int32_t _M0L3tmpS797;
                int32_t _M0L6_2atmpS1856;
                int32_t _M0L6_2atmpS1861;
                #line 200 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L1uS794 = _M0FP26RiantR8snn__mbt9next__f32(_M0L3rngS746);
                _M0L6_2atmpS1860 = _M0L4colsS737 - _M0L1kS793;
                _M0L6_2atmpS1859 = (float)_M0L6_2atmpS1860;
                _M0L6_2atmpS1858 = _M0L6_2atmpS1859 * _M0L1uS794;
                #line 201 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS1857
                = _M0MPC15float5Float7to__int(_M0L6_2atmpS1858);
                _M0L6r__idxS795 = _M0L1kS793 + _M0L6_2atmpS1857;
                if (_M0L6r__idxS795 >= _M0L4colsS737) {
                  _M0L10r__clampedS796 = _M0L4colsS737 - 1;
                } else {
                  _M0L10r__clampedS796 = _M0L6r__idxS795;
                }
                #line 203 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L3tmpS797
                = _M0MPC15array5Array2atGiE(_M0L9post__idxS787, _M0L1kS793);
                #line 204 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS1856
                = _M0MPC15array5Array2atGiE(_M0L9post__idxS787, _M0L10r__clampedS796);
                #line 204 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array3setGiE(_M0L9post__idxS787, _M0L1kS793, _M0L6_2atmpS1856);
                #line 205 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array3setGiE(_M0L9post__idxS787, _M0L10r__clampedS796, _M0L3tmpS797);
                _M0L6_2atmpS1861 = _M0L1kS793 + 1;
                _M0L1kS793 = _M0L6_2atmpS1861;
                continue;
              }
              break;
            }
            _M0L7_2abindS799 = 0;
            _M0L1kS800 = _M0L7_2abindS799;
            while (1) {
              if (_M0L1kS800 < _M0L7n__dropS791) {
                struct _M0TPB5ArrayGfE* _M0L6_2atmpS1862;
                int32_t _M0L6_2atmpS1863;
                int32_t _M0L6_2atmpS1864;
                #line 208 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS1862
                = _M0MPC15array5Array2atGRPB5ArrayGfEE(_M0L5denseS732, _M0L1iS786);
                #line 208 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS1863
                = _M0MPC15array5Array2atGiE(_M0L9post__idxS787, _M0L1kS800);
                #line 208 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array3setGfE(_M0L6_2atmpS1862, _M0L6_2atmpS1863, 0x0p+0f);
                moonbit_decref(_M0L6_2atmpS1862);
                _M0L6_2atmpS1864 = _M0L1kS800 + 1;
                _M0L1kS800 = _M0L6_2atmpS1864;
                continue;
              } else {
                moonbit_decref(_M0L9post__idxS787);
              }
              break;
            }
            _M0L6_2atmpS1866 = _M0L1iS786 + 1;
            _M0L1iS786 = _M0L6_2atmpS1866;
            continue;
          }
          break;
        }
      } else if (_M0L7n__keepS784 == 0) {
        int32_t _M0L7_2abindS803 = 0;
        int32_t _M0L1iS804 = _M0L7_2abindS803;
        while (1) {
          if (_M0L1iS804 < _M0L4rowsS733) {
            int32_t _M0L7_2abindS805 = 0;
            int32_t _M0L1jS806 = _M0L7_2abindS805;
            int32_t _M0L6_2atmpS1869;
            while (1) {
              if (_M0L1jS806 < _M0L4colsS737) {
                struct _M0TPB5ArrayGfE* _M0L6_2atmpS1867;
                int32_t _M0L6_2atmpS1868;
                #line 214 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS1867
                = _M0MPC15array5Array2atGRPB5ArrayGfEE(_M0L5denseS732, _M0L1iS804);
                #line 214 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array3setGfE(_M0L6_2atmpS1867, _M0L1jS806, 0x0p+0f);
                moonbit_decref(_M0L6_2atmpS1867);
                _M0L6_2atmpS1868 = _M0L1jS806 + 1;
                _M0L1jS806 = _M0L6_2atmpS1868;
                continue;
              }
              break;
            }
            _M0L6_2atmpS1869 = _M0L1iS804 + 1;
            _M0L1iS804 = _M0L6_2atmpS1869;
            continue;
          }
          break;
        }
      }
      break;
    }
  }
  _M0L6_2atmpS1879 = _M0L4rowsS733 + 1;
  #line 221 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  _M0L6rowptrS809 = _M0MPC15array5Array4makeGiE(_M0L6_2atmpS1879, 0);
  _M0L6_2atmpS1878 = (int32_t*)moonbit_empty_int32_array;
  _M0L6colptrS810
  = (struct _M0TPB5ArrayGiE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGiE));
  Moonbit_object_header(_M0L6colptrS810)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 3, 0);
  _M0L6colptrS810->$0 = _M0L6_2atmpS1878;
  _M0L6colptrS810->$1 = 0;
  _M0L6_2atmpS1877 = moonbit_empty_float_array;
  _M0L4valsS811
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_M0L4valsS811)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 0, 0);
  _M0L4valsS811->$0 = _M0L6_2atmpS1877;
  _M0L4valsS811->$1 = 0;
  _M0L7_2abindS812 = 0;
  _M0L1iS813 = _M0L7_2abindS812;
  while (1) {
    if (_M0L1iS813 < _M0L4rowsS733) {
      int32_t _M0L6_2atmpS1872;
      int32_t _M0L7_2abindS814;
      int32_t _M0L1jS815;
      int32_t _M0L6_2atmpS1875;
      #line 225 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
      _M0L6_2atmpS1872 = _M0MPC15array5Array6lengthGfE(_M0L4valsS811);
      #line 225 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
      _M0MPC15array5Array3setGiE(_M0L6rowptrS809, _M0L1iS813, _M0L6_2atmpS1872);
      _M0L7_2abindS814 = 0;
      _M0L1jS815 = _M0L7_2abindS814;
      while (1) {
        if (_M0L1jS815 < _M0L4colsS737) {
          struct _M0TPB5ArrayGfE* _M0L6_2atmpS1873;
          float _M0L1vS816;
          int32_t _M0L6_2atmpS1874;
          #line 227 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
          _M0L6_2atmpS1873
          = _M0MPC15array5Array2atGRPB5ArrayGfEE(_M0L5denseS732, _M0L1iS813);
          #line 227 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
          _M0L1vS816
          = _M0MPC15array5Array2atGfE(_M0L6_2atmpS1873, _M0L1jS815);
          moonbit_decref(_M0L6_2atmpS1873);
          if (_M0L1vS816 != 0x0p+0f) {
            #line 229 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
            _M0MPC15array5Array4pushGiE(_M0L6colptrS810, _M0L1jS815);
            #line 230 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
            _M0MPC15array5Array4pushGfE(_M0L4valsS811, _M0L1vS816);
          }
          _M0L6_2atmpS1874 = _M0L1jS815 + 1;
          _M0L1jS815 = _M0L6_2atmpS1874;
          continue;
        }
        break;
      }
      _M0L6_2atmpS1875 = _M0L1iS813 + 1;
      _M0L1iS813 = _M0L6_2atmpS1875;
      continue;
    } else {
      moonbit_decref(_M0L5denseS732);
    }
    break;
  }
  #line 234 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  _M0L6_2atmpS1876 = _M0MPC15array5Array6lengthGfE(_M0L4valsS811);
  #line 234 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  _M0MPC15array5Array3setGiE(_M0L6rowptrS809, _M0L4rowsS733, _M0L6_2atmpS1876);
  _block_2293
  = (struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR));
  Moonbit_object_header(_block_2293)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 42, 0);
  _block_2293->$0 = _M0L4rowsS733;
  _block_2293->$1 = _M0L4colsS737;
  _block_2293->$2 = _M0L6rowptrS809;
  _block_2293->$3 = _M0L6colptrS810;
  _block_2293->$4 = _M0L4valsS811;
  return _block_2293;
}

int32_t _M0MP26RiantR8snn__mbt15SparseMatrixCSR3nnz(
  struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L1mS731
) {
  struct _M0TPB5ArrayGfE* _M0L4valsS1830;
  #line 34 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  _M0L4valsS1830 = _M0L1mS731->$4;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  return _M0MPC15array5Array6lengthGfE(_M0L4valsS1830);
}

struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0MP26RiantR8snn__mbt7Xoshiro3new(
  uint64_t _M0L4seedS729
) {
  struct _M0TUmmmmE* _M0L1sS728;
  uint64_t _M0L6_2atmpS1829;
  struct _M0TUmmmmE* _M0L1tS730;
  uint64_t _M0L6_2atmpS1825;
  uint64_t _M0L6_2atmpS1826;
  uint64_t _M0L6_2atmpS1827;
  uint64_t _M0L6_2atmpS1828;
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _block_2294;
  #line 48 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  #line 49 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L1sS728 = _M0FP26RiantR8snn__mbt10splitmix64(_M0L4seedS729);
  _M0L6_2atmpS1829 = _M0L1sS728->$3;
  #line 50 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L1tS730 = _M0FP26RiantR8snn__mbt10splitmix64(_M0L6_2atmpS1829);
  _M0L6_2atmpS1825 = _M0L1sS728->$0;
  _M0L6_2atmpS1826 = _M0L1sS728->$1;
  _M0L6_2atmpS1827 = _M0L1sS728->$2;
  moonbit_decref(_M0L1sS728);
  _M0L6_2atmpS1828 = _M0L1tS730->$0;
  moonbit_decref(_M0L1tS730);
  _block_2294
  = (struct _M0TP26RiantR8snn__mbt7Xoshiro*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt7Xoshiro));
  Moonbit_object_header(_block_2294)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_2294->$0 = _M0L6_2atmpS1825;
  _block_2294->$1 = _M0L6_2atmpS1826;
  _block_2294->$2 = _M0L6_2atmpS1827;
  _block_2294->$3 = _M0L6_2atmpS1828;
  return _block_2294;
}

struct _M0TUmmmmE* _M0FP26RiantR8snn__mbt10splitmix64(uint64_t _M0L4seedS720) {
  uint64_t _M0L2s1S719;
  uint64_t _M0L2z1S721;
  uint64_t _M0L2s2S722;
  uint64_t _M0L2z2S723;
  uint64_t _M0L2s3S724;
  uint64_t _M0L2z3S725;
  uint64_t _M0L2s4S726;
  uint64_t _M0L2z4S727;
  struct _M0TUmmmmE* _block_2295;
  #line 62 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2s1S719 = _M0L4seedS720 + 11400714819323198485ull;
  #line 64 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2z1S721 = _M0FP26RiantR8snn__mbt5mix64(_M0L2s1S719);
  _M0L2s2S722 = _M0L2s1S719 + 11400714819323198485ull;
  #line 66 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2z2S723 = _M0FP26RiantR8snn__mbt5mix64(_M0L2s2S722);
  _M0L2s3S724 = _M0L2s2S722 + 11400714819323198485ull;
  #line 68 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2z3S725 = _M0FP26RiantR8snn__mbt5mix64(_M0L2s3S724);
  _M0L2s4S726 = _M0L2s3S724 + 11400714819323198485ull;
  #line 70 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2z4S727 = _M0FP26RiantR8snn__mbt5mix64(_M0L2s4S726);
  _block_2295 = (struct _M0TUmmmmE*)moonbit_malloc(sizeof(struct _M0TUmmmmE));
  Moonbit_object_header(_block_2295)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_2295->$0 = _M0L2z1S721;
  _block_2295->$1 = _M0L2z2S723;
  _block_2295->$2 = _M0L2z3S725;
  _block_2295->$3 = _M0L2z4S727;
  return _block_2295;
}

uint64_t _M0FP26RiantR8snn__mbt5mix64(uint64_t _M0L1zS717) {
  uint64_t _M0L6_2atmpS1824;
  uint64_t _M0L6_2atmpS1823;
  uint64_t _M0L1zS716;
  uint64_t _M0L6_2atmpS1822;
  uint64_t _M0L6_2atmpS1821;
  uint64_t _M0L1zS718;
  uint64_t _M0L6_2atmpS1820;
  #line 75 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L6_2atmpS1824 = _M0L1zS717 >> 30;
  _M0L6_2atmpS1823 = _M0L1zS717 ^ _M0L6_2atmpS1824;
  _M0L1zS716 = _M0L6_2atmpS1823 * 13787848793156543929ull;
  _M0L6_2atmpS1822 = _M0L1zS716 >> 27;
  _M0L6_2atmpS1821 = _M0L1zS716 ^ _M0L6_2atmpS1822;
  _M0L1zS718 = _M0L6_2atmpS1821 * 10723151780598845931ull;
  _M0L6_2atmpS1820 = _M0L1zS718 >> 31;
  return _M0L1zS718 ^ _M0L6_2atmpS1820;
}

struct _M0TUddE* _M0FP26RiantR8snn__mbt11box__muller(
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS711
) {
  double _M0L2u1S710;
  double _M0L8u1__safeS712;
  double _M0L2u2S713;
  double _M0L6_2atmpS1819;
  double _M0L6_2atmpS1818;
  double _M0L1rS714;
  double _M0L5thetaS715;
  double _M0L6_2atmpS1817;
  double _M0L6_2atmpS1814;
  double _M0L6_2atmpS1816;
  double _M0L6_2atmpS1815;
  struct _M0TUddE* _block_2296;
  #line 126 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  #line 127 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  _M0L2u1S710 = _M0FP26RiantR8snn__mbt9next__f64(_M0L3rngS711);
  if (_M0L2u1S710 < 0x1.203af9ee75616p-50) {
    _M0L8u1__safeS712 = 0x1.203af9ee75616p-50;
  } else {
    _M0L8u1__safeS712 = _M0L2u1S710;
  }
  #line 130 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  _M0L2u2S713 = _M0FP26RiantR8snn__mbt9next__f64(_M0L3rngS711);
  #line 131 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  _M0L6_2atmpS1819 = _M0FPC14math2ln(_M0L8u1__safeS712);
  _M0L6_2atmpS1818 = -0x1p+1 * _M0L6_2atmpS1819;
  #line 131 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  _M0L1rS714 = sqrt(_M0L6_2atmpS1818);
  _M0L5thetaS715 = 0x1.921fb54442d18p+2 * _M0L2u2S713;
  #line 133 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  _M0L6_2atmpS1817 = _M0FPC14math3cos(_M0L5thetaS715);
  _M0L6_2atmpS1814 = _M0L1rS714 * _M0L6_2atmpS1817;
  #line 133 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  _M0L6_2atmpS1816 = _M0FPC14math3sin(_M0L5thetaS715);
  _M0L6_2atmpS1815 = _M0L1rS714 * _M0L6_2atmpS1816;
  _block_2296 = (struct _M0TUddE*)moonbit_malloc(sizeof(struct _M0TUddE));
  Moonbit_object_header(_block_2296)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_2296->$0 = _M0L6_2atmpS1814;
  _block_2296->$1 = _M0L6_2atmpS1815;
  return _block_2296;
}

double _M0FP26RiantR8snn__mbt9next__f64(
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L1rS708
) {
  uint64_t _M0L1uS707;
  uint64_t _M0L4bitsS709;
  double _M0L6_2atmpS1813;
  #line 118 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  #line 119 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L1uS707 = _M0FP26RiantR8snn__mbt9next__u64(_M0L1rS708);
  _M0L4bitsS709 = _M0L1uS707 >> 11;
  _M0L6_2atmpS1813 = (double)_M0L4bitsS709;
  return _M0L6_2atmpS1813 * 0x1p-53;
}

int32_t _M0FP26RiantR8snn__mbt12update__time(
  struct _M0TP26RiantR8snn__mbt4Time* _M0L1tS705,
  float _M0L2dtS706
) {
  struct _M0TPB5ArrayGfE* _M0L1tS1805;
  struct _M0TPB5ArrayGfE* _M0L1tS1808;
  float _M0L6_2atmpS1807;
  float _M0L6_2atmpS1806;
  struct _M0TPB5ArrayGiE* _M0L2ttS1809;
  struct _M0TPB5ArrayGiE* _M0L2ttS1812;
  int32_t _M0L6_2atmpS1811;
  int32_t _M0L6_2atmpS1810;
  #line 89 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\time.mbt"
  _M0L1tS1805 = _M0L1tS705->$0;
  _M0L1tS1808 = _M0L1tS705->$0;
  #line 90 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\time.mbt"
  _M0L6_2atmpS1807 = _M0MPC15array5Array2atGfE(_M0L1tS1808, 0);
  _M0L6_2atmpS1806 = _M0L6_2atmpS1807 + _M0L2dtS706;
  #line 90 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\time.mbt"
  _M0MPC15array5Array3setGfE(_M0L1tS1805, 0, _M0L6_2atmpS1806);
  _M0L2ttS1809 = _M0L1tS705->$1;
  _M0L2ttS1812 = _M0L1tS705->$1;
  #line 91 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\time.mbt"
  _M0L6_2atmpS1811 = _M0MPC15array5Array2atGiE(_M0L2ttS1812, 0);
  _M0L6_2atmpS1810 = _M0L6_2atmpS1811 + 1;
  #line 91 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\time.mbt"
  _M0MPC15array5Array3setGiE(_M0L2ttS1809, 0, _M0L6_2atmpS1810);
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt7set__dt(
  struct _M0TP26RiantR8snn__mbt4Time* _M0L1tS703,
  float _M0L1vS704
) {
  #line 80 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\time.mbt"
  _M0L1tS703->$2 = _M0L1vS704;
  return 0;
}

float _M0FP26RiantR8snn__mbt9get__time(
  struct _M0TP26RiantR8snn__mbt4Time* _M0L1tS702
) {
  struct _M0TPB5ArrayGfE* _M0L1tS1804;
  #line 49 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\time.mbt"
  _M0L1tS1804 = _M0L1tS702->$0;
  #line 50 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\time.mbt"
  return _M0MPC15array5Array2atGfE(_M0L1tS1804, 0);
}

struct _M0TP26RiantR8snn__mbt4Time* _M0MP26RiantR8snn__mbt4Time3new() {
  float* _M0L6_2atmpS1803;
  struct _M0TPB5ArrayGfE* _M0L6_2atmpS1800;
  int32_t* _M0L6_2atmpS1802;
  struct _M0TPB5ArrayGiE* _M0L6_2atmpS1801;
  struct _M0TP26RiantR8snn__mbt4Time* _block_2297;
  #line 34 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\time.mbt"
  _M0L6_2atmpS1803 = (float*)moonbit_make_float_array_raw(1);
  _M0L6_2atmpS1803[0] = 0x0p+0f;
  _M0L6_2atmpS1800
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_M0L6_2atmpS1800)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 0, 0);
  _M0L6_2atmpS1800->$0 = _M0L6_2atmpS1803;
  _M0L6_2atmpS1800->$1 = 1;
  _M0L6_2atmpS1802 = (int32_t*)moonbit_make_int32_array_raw(1);
  _M0L6_2atmpS1802[0] = 0;
  _M0L6_2atmpS1801
  = (struct _M0TPB5ArrayGiE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGiE));
  Moonbit_object_header(_M0L6_2atmpS1801)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 3, 0);
  _M0L6_2atmpS1801->$0 = _M0L6_2atmpS1802;
  _M0L6_2atmpS1801->$1 = 1;
  _block_2297
  = (struct _M0TP26RiantR8snn__mbt4Time*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt4Time));
  Moonbit_object_header(_block_2297)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 47, 0);
  _block_2297->$0 = _M0L6_2atmpS1800;
  _block_2297->$1 = _M0L6_2atmpS1801;
  _block_2297->$2 = 0x1p-3f;
  return _block_2297;
}

float _M0FP26RiantR8snn__mbt9next__f32(
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L1rS700
) {
  uint32_t _M0L1uS699;
  uint32_t _M0L4bitsS701;
  double _M0L6_2atmpS1799;
  double _M0L6_2atmpS1798;
  #line 128 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  #line 129 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L1uS699 = _M0FP26RiantR8snn__mbt9next__u32(_M0L1rS700);
  _M0L4bitsS701 = _M0L1uS699 >> 8;
  _M0L6_2atmpS1799 = (double)_M0L4bitsS701;
  _M0L6_2atmpS1798 = _M0L6_2atmpS1799 * 0x1p-24;
  return (float)_M0L6_2atmpS1798;
}

uint32_t _M0FP26RiantR8snn__mbt9next__u32(
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L1rS698
) {
  uint64_t _M0L1uS697;
  uint64_t _M0L6_2atmpS1797;
  #line 110 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  #line 111 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L1uS697 = _M0FP26RiantR8snn__mbt9next__u64(_M0L1rS698);
  _M0L6_2atmpS1797 = _M0L1uS697 >> 32;
  return (uint32_t)_M0L6_2atmpS1797;
}

uint64_t _M0FP26RiantR8snn__mbt9next__u64(
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L1rS690
) {
  uint64_t _M0L2s0S689;
  uint64_t _M0L2s1S691;
  uint64_t _M0L2s2S692;
  uint64_t _M0L2s3S693;
  uint64_t _M0L3tmpS694;
  uint64_t _M0L6_2atmpS1796;
  uint64_t _M0L3resS695;
  uint64_t _M0L1tS696;
  uint64_t _M0L6_2atmpS1786;
  uint64_t _M0L6_2atmpS1787;
  uint64_t _M0L2s2S1789;
  uint64_t _M0L6_2atmpS1788;
  uint64_t _M0L2s3S1791;
  uint64_t _M0L6_2atmpS1790;
  uint64_t _M0L2s2S1793;
  uint64_t _M0L6_2atmpS1792;
  uint64_t _M0L2s3S1795;
  uint64_t _M0L6_2atmpS1794;
  #line 90 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2s0S689 = _M0L1rS690->$0;
  _M0L2s1S691 = _M0L1rS690->$1;
  _M0L2s2S692 = _M0L1rS690->$2;
  _M0L2s3S693 = _M0L1rS690->$3;
  _M0L3tmpS694 = _M0L2s0S689 + _M0L2s3S693;
  #line 96 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L6_2atmpS1796 = _M0FP26RiantR8snn__mbt4rotl(_M0L3tmpS694, 23);
  _M0L3resS695 = _M0L6_2atmpS1796 + _M0L2s0S689;
  _M0L1tS696 = _M0L2s1S691 << 17;
  _M0L6_2atmpS1786 = _M0L2s2S692 ^ _M0L2s0S689;
  _M0L1rS690->$2 = _M0L6_2atmpS1786;
  _M0L6_2atmpS1787 = _M0L2s3S693 ^ _M0L2s1S691;
  _M0L1rS690->$3 = _M0L6_2atmpS1787;
  _M0L2s2S1789 = _M0L1rS690->$2;
  _M0L6_2atmpS1788 = _M0L2s1S691 ^ _M0L2s2S1789;
  _M0L1rS690->$1 = _M0L6_2atmpS1788;
  _M0L2s3S1791 = _M0L1rS690->$3;
  _M0L6_2atmpS1790 = _M0L2s0S689 ^ _M0L2s3S1791;
  _M0L1rS690->$0 = _M0L6_2atmpS1790;
  _M0L2s2S1793 = _M0L1rS690->$2;
  _M0L6_2atmpS1792 = _M0L2s2S1793 ^ _M0L1tS696;
  _M0L1rS690->$2 = _M0L6_2atmpS1792;
  _M0L2s3S1795 = _M0L1rS690->$3;
  #line 103 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L6_2atmpS1794 = _M0FP26RiantR8snn__mbt4rotl(_M0L2s3S1795, 45);
  _M0L1rS690->$3 = _M0L6_2atmpS1794;
  return _M0L3resS695;
}

uint64_t _M0FP26RiantR8snn__mbt4rotl(uint64_t _M0L1xS687, int32_t _M0L1kS688) {
  uint64_t _M0L6_2atmpS1783;
  int32_t _M0L6_2atmpS1785;
  uint64_t _M0L6_2atmpS1784;
  #line 83 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L6_2atmpS1783 = _M0L1xS687 << (_M0L1kS688 & 63);
  _M0L6_2atmpS1785 = 64 - _M0L1kS688;
  _M0L6_2atmpS1784 = _M0L1xS687 >> (_M0L6_2atmpS1785 & 63);
  return _M0L6_2atmpS1783 | _M0L6_2atmpS1784;
}

int32_t _M0MP26RiantR8snn__mbt7Monitor13count__spikes(
  struct _M0TP26RiantR8snn__mbt7Monitor* _M0L1mS680
) {
  struct _M0TPB5ArrayGfE* _M0L4dataS1782;
  int32_t _M0L1nS679;
  struct _M0TPB8MutLocalGiE* _M0L5countS681;
  struct _M0TPB8MutLocalGfE* _M0L4prevS682;
  int32_t _M0L7_2abindS683;
  int32_t _M0L1iS684;
  int32_t _result_2299;
  #line 17 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\vecplot.mbt"
  _M0L4dataS1782 = _M0L1mS680->$2;
  #line 18 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\vecplot.mbt"
  _M0L1nS679 = _M0MPC15array5Array6lengthGfE(_M0L4dataS1782);
  if (_M0L1nS679 == 0) {
    return 0;
  }
  _M0L5countS681
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L5countS681)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L5countS681->$0 = 0;
  _M0L4prevS682
  = (struct _M0TPB8MutLocalGfE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGfE));
  Moonbit_object_header(_M0L4prevS682)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L4prevS682->$0 = 0x0p+0f;
  _M0L7_2abindS683 = 0;
  _M0L1iS684 = _M0L7_2abindS683;
  while (1) {
    if (_M0L1iS684 < _M0L1nS679) {
      struct _M0TPB5ArrayGfE* _M0L4dataS1780 = _M0L1mS680->$2;
      float _M0L3curS685;
      float _M0L3valS1777;
      int32_t _M0L6_2atmpS1781;
      #line 25 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\vecplot.mbt"
      _M0L3curS685 = _M0MPC15array5Array2atGfE(_M0L4dataS1780, _M0L1iS684);
      _M0L3valS1777 = _M0L4prevS682->$0;
      if (_M0L3valS1777 < 0x1p-1f && _M0L3curS685 >= 0x1p-1f) {
        int32_t _M0L3valS1779 = _M0L5countS681->$0;
        int32_t _M0L6_2atmpS1778 = _M0L3valS1779 + 1;
        _M0L5countS681->$0 = _M0L6_2atmpS1778;
      }
      _M0L4prevS682->$0 = _M0L3curS685;
      _M0L6_2atmpS1781 = _M0L1iS684 + 1;
      _M0L1iS684 = _M0L6_2atmpS1781;
      continue;
    } else {
      moonbit_decref(_M0L4prevS682);
    }
    break;
  }
  _result_2299 = _M0L5countS681->$0;
  moonbit_decref(_M0L5countS681);
  return _result_2299;
}

double _M0FPC14math2ln(double _M0L1xS665) {
  struct _M0TUdiE* _M0L7_2abindS666;
  double _M0L5_2af1S667;
  int32_t _M0L5_2akiS668;
  double _M0L1fS670;
  double _M0L1kS671;
  double _M0L6_2atmpS1770;
  double _M0L1sS672;
  double _M0L2s2S673;
  double _M0L2s4S674;
  double _M0L6_2atmpS1769;
  double _M0L6_2atmpS1768;
  double _M0L6_2atmpS1767;
  double _M0L6_2atmpS1766;
  double _M0L6_2atmpS1765;
  double _M0L6_2atmpS1764;
  double _M0L2t1S675;
  double _M0L6_2atmpS1763;
  double _M0L6_2atmpS1762;
  double _M0L6_2atmpS1761;
  double _M0L6_2atmpS1760;
  double _M0L2t2S676;
  double _M0L1rS677;
  double _M0L6_2atmpS1759;
  double _M0L4hfsqS678;
  double _M0L6_2atmpS1752;
  double _M0L6_2atmpS1758;
  double _M0L6_2atmpS1756;
  double _M0L6_2atmpS1757;
  double _M0L6_2atmpS1755;
  double _M0L6_2atmpS1754;
  double _M0L6_2atmpS1753;
  #line 55 "C:\\Users\\31379\\.moon\\lib\\core\\math\\log_double_nonjs.mbt"
  if (_M0L1xS665 < 0x0p+0) {
    return _M0FPC16double14not__a__number;
  } else {
    #line 65 "C:\\Users\\31379\\.moon\\lib\\core\\math\\log_double_nonjs.mbt"
    #line 65 "C:\\Users\\31379\\.moon\\lib\\core\\math\\log_double_nonjs.mbt"
    if (
      _M0MPC16double6Double7is__nan(_M0L1xS665)
      || _M0MPC16double6Double7is__inf(_M0L1xS665)
    ) {
      return _M0L1xS665;
    } else if (_M0L1xS665 == 0x0p+0) {
      return _M0FPC16double13neg__infinity;
    }
  }
  #line 70 "C:\\Users\\31379\\.moon\\lib\\core\\math\\log_double_nonjs.mbt"
  _M0L7_2abindS666 = _M0FPC14math5frexp(_M0L1xS665);
  _M0L5_2af1S667 = _M0L7_2abindS666->$0;
  _M0L5_2akiS668 = _M0L7_2abindS666->$1;
  moonbit_decref(_M0L7_2abindS666);
  if (_M0L5_2af1S667 < 0x1.6a09e667f3bcdp-1) {
    double _M0L6_2atmpS1774 = _M0L5_2af1S667 * 0x1p+1;
    double _M0L6_2atmpS1771 = _M0L6_2atmpS1774 - 0x1p+0;
    int32_t _M0L6_2atmpS1773 = _M0L5_2akiS668 - 1;
    double _M0L6_2atmpS1772 = (double)_M0L6_2atmpS1773;
    _M0L1fS670 = _M0L6_2atmpS1771;
    _M0L1kS671 = _M0L6_2atmpS1772;
    goto join_669;
  } else {
    double _M0L6_2atmpS1775 = _M0L5_2af1S667 - 0x1p+0;
    double _M0L6_2atmpS1776 = (double)_M0L5_2akiS668;
    _M0L1fS670 = _M0L6_2atmpS1775;
    _M0L1kS671 = _M0L6_2atmpS1776;
    goto join_669;
  }
  join_669:;
  _M0L6_2atmpS1770 = 0x1p+1 + _M0L1fS670;
  _M0L1sS672 = _M0L1fS670 / _M0L6_2atmpS1770;
  _M0L2s2S673 = _M0L1sS672 * _M0L1sS672;
  _M0L2s4S674 = _M0L2s2S673 * _M0L2s2S673;
  _M0L6_2atmpS1769 = _M0L2s4S674 * 0x1.2f112df3e5244p-3;
  _M0L6_2atmpS1768 = 0x1.7466496cb03dep-3 + _M0L6_2atmpS1769;
  _M0L6_2atmpS1767 = _M0L2s4S674 * _M0L6_2atmpS1768;
  _M0L6_2atmpS1766 = 0x1.2492494229359p-2 + _M0L6_2atmpS1767;
  _M0L6_2atmpS1765 = _M0L2s4S674 * _M0L6_2atmpS1766;
  _M0L6_2atmpS1764 = 0x1.5555555555593p-1 + _M0L6_2atmpS1765;
  _M0L2t1S675 = _M0L2s2S673 * _M0L6_2atmpS1764;
  _M0L6_2atmpS1763 = _M0L2s4S674 * 0x1.39a09d078c69fp-3;
  _M0L6_2atmpS1762 = 0x1.c71c51d8e78afp-3 + _M0L6_2atmpS1763;
  _M0L6_2atmpS1761 = _M0L2s4S674 * _M0L6_2atmpS1762;
  _M0L6_2atmpS1760 = 0x1.999999997fa04p-2 + _M0L6_2atmpS1761;
  _M0L2t2S676 = _M0L2s4S674 * _M0L6_2atmpS1760;
  _M0L1rS677 = _M0L2t1S675 + _M0L2t2S676;
  _M0L6_2atmpS1759 = 0x1p-1 * _M0L1fS670;
  _M0L4hfsqS678 = _M0L6_2atmpS1759 * _M0L1fS670;
  _M0L6_2atmpS1752 = _M0L1kS671 * 0x1.62e42feep-1;
  _M0L6_2atmpS1758 = _M0L4hfsqS678 + _M0L1rS677;
  _M0L6_2atmpS1756 = _M0L1sS672 * _M0L6_2atmpS1758;
  _M0L6_2atmpS1757 = _M0L1kS671 * 0x1.a39ef35793c76p-33;
  _M0L6_2atmpS1755 = _M0L6_2atmpS1756 + _M0L6_2atmpS1757;
  _M0L6_2atmpS1754 = _M0L4hfsqS678 - _M0L6_2atmpS1755;
  _M0L6_2atmpS1753 = _M0L6_2atmpS1754 - _M0L1fS670;
  return _M0L6_2atmpS1752 - _M0L6_2atmpS1753;
}

struct _M0TUdiE* _M0FPC14math5frexp(double _M0L1fS658) {
  struct _M0TUdiE* _M0L7_2abindS659;
  double _M0L10_2anorm__fS660;
  int32_t _M0L6_2aexpS661;
  uint64_t _M0L1uS662;
  uint64_t _M0L6_2atmpS1751;
  uint64_t _M0L6_2atmpS1750;
  int32_t _M0L6_2atmpS1749;
  int32_t _M0L6_2atmpS1748;
  int32_t _M0L3expS663;
  uint64_t _M0L6_2atmpS1747;
  uint64_t _M0L6_2atmpS1746;
  uint64_t _M0L6_2atmpS1745;
  double _M0L4fracS664;
  struct _M0TUdiE* _block_2302;
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\math\\utils.mbt"
  #line 134 "C:\\Users\\31379\\.moon\\lib\\core\\math\\utils.mbt"
  #line 134 "C:\\Users\\31379\\.moon\\lib\\core\\math\\utils.mbt"
  if (
    _M0L1fS658 == 0x0p+0
    || _M0MPC16double6Double7is__inf(_M0L1fS658)
    || _M0MPC16double6Double7is__nan(_M0L1fS658)
  ) {
    struct _M0TUdiE* _block_2301 =
      (struct _M0TUdiE*)moonbit_malloc(sizeof(struct _M0TUdiE));
    Moonbit_object_header(_block_2301)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
    _block_2301->$0 = _M0L1fS658;
    _block_2301->$1 = 0;
    return _block_2301;
  }
  #line 137 "C:\\Users\\31379\\.moon\\lib\\core\\math\\utils.mbt"
  _M0L7_2abindS659 = _M0FPC14math9normalize(_M0L1fS658);
  _M0L10_2anorm__fS660 = _M0L7_2abindS659->$0;
  _M0L6_2aexpS661 = _M0L7_2abindS659->$1;
  moonbit_decref(_M0L7_2abindS659);
  _M0L1uS662 = *(int64_t*)&_M0L10_2anorm__fS660;
  _M0L6_2atmpS1751 = _M0L1uS662 >> 52;
  _M0L6_2atmpS1750 = _M0L6_2atmpS1751 & 2047ull;
  _M0L6_2atmpS1749 = (int32_t)_M0L6_2atmpS1750;
  _M0L6_2atmpS1748 = _M0L6_2aexpS661 + _M0L6_2atmpS1749;
  _M0L3expS663 = _M0L6_2atmpS1748 - 1022;
  _M0L6_2atmpS1747 = ~9218868437227405312ull;
  _M0L6_2atmpS1746 = _M0L1uS662 & _M0L6_2atmpS1747;
  _M0L6_2atmpS1745 = _M0L6_2atmpS1746 | 4602678819172646912ull;
  _M0L4fracS664 = *(double*)&_M0L6_2atmpS1745;
  _block_2302 = (struct _M0TUdiE*)moonbit_malloc(sizeof(struct _M0TUdiE));
  Moonbit_object_header(_block_2302)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_2302->$0 = _M0L4fracS664;
  _block_2302->$1 = _M0L3expS663;
  return _block_2302;
}

struct _M0TUdiE* _M0FPC14math9normalize(double _M0L1fS657) {
  double _M0L6_2atmpS1742;
  struct _M0TUdiE* _block_2304;
  #line 125 "C:\\Users\\31379\\.moon\\lib\\core\\math\\utils.mbt"
  #line 126 "C:\\Users\\31379\\.moon\\lib\\core\\math\\utils.mbt"
  _M0L6_2atmpS1742 = fabs(_M0L1fS657);
  if (_M0L6_2atmpS1742 < _M0FPC16double13min__positive) {
    double _M0L6_2atmpS1744 = (double)4503599627370496ll;
    double _M0L6_2atmpS1743 = _M0L1fS657 * _M0L6_2atmpS1744;
    struct _M0TUdiE* _block_2303 =
      (struct _M0TUdiE*)moonbit_malloc(sizeof(struct _M0TUdiE));
    Moonbit_object_header(_block_2303)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
    _block_2303->$0 = _M0L6_2atmpS1743;
    _block_2303->$1 = -52;
    return _block_2303;
  }
  _block_2304 = (struct _M0TUdiE*)moonbit_malloc(sizeof(struct _M0TUdiE));
  Moonbit_object_header(_block_2304)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_2304->$0 = _M0L1fS657;
  _block_2304->$1 = 0;
  return _block_2304;
}

moonbit_string_t _M0IPC15float5FloatPB4Show10to__string(float _M0L4selfS656) {
  double _M0L6_2atmpS1741;
  #line 16 "C:\\Users\\31379\\.moon\\lib\\core\\float\\methods.mbt"
  _M0L6_2atmpS1741 = (double)_M0L4selfS656;
  #line 17 "C:\\Users\\31379\\.moon\\lib\\core\\float\\methods.mbt"
  return _M0MPC16double6Double10to__string(_M0L6_2atmpS1741);
}

int32_t _M0MPC15float5Float7to__int(float _M0L4selfS655) {
  #line 43 "C:\\Users\\31379\\.moon\\lib\\core\\float\\to_int.mbt"
  if (_M0L4selfS655 != _M0L4selfS655) {
    return 0;
  } else if (_M0L4selfS655 >= 0x1.fffffffcp+30f) {
    return 2147483647;
  } else if (_M0L4selfS655 <= -0x1p+31f) {
    return (int32_t)0x80000000;
  } else {
    return (int32_t)_M0L4selfS655;
  }
}

struct _M0TPB5ArrayGfE* _M0MPC15array5Array4makeGfE(
  int32_t _M0L3lenS636,
  float _M0L4elemS638
) {
  struct _M0TPB5ArrayGfE* _M0L3arrS635;
  int32_t _M0L1iS637;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3arrS635 = _M0MPC15array5Array20unsafe__make__uninitGfE(_M0L3lenS636);
  _M0L1iS637 = 0;
  while (1) {
    if (_M0L1iS637 < _M0L3lenS636) {
      float* _M0L3bufS1733 = _M0L3arrS635->$0;
      int32_t _M0L6_2atmpS1734;
      _M0L3bufS1733[_M0L1iS637] = _M0L4elemS638;
      _M0L6_2atmpS1734 = _M0L1iS637 + 1;
      _M0L1iS637 = _M0L6_2atmpS1734;
      continue;
    }
    break;
  }
  return _M0L3arrS635;
}

struct _M0TPB5ArrayGbE* _M0MPC15array5Array4makeGbE(
  int32_t _M0L3lenS641,
  int32_t _M0L4elemS643
) {
  struct _M0TPB5ArrayGbE* _M0L3arrS640;
  int32_t _M0L1iS642;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3arrS640 = _M0MPC15array5Array20unsafe__make__uninitGbE(_M0L3lenS641);
  _M0L1iS642 = 0;
  while (1) {
    if (_M0L1iS642 < _M0L3lenS641) {
      uint8_t* _M0L3bufS1735 = _M0L3arrS640->$0;
      int32_t _M0L6_2atmpS1736;
      _M0L3bufS1735[_M0L1iS642] = _M0L4elemS643;
      _M0L6_2atmpS1736 = _M0L1iS642 + 1;
      _M0L1iS642 = _M0L6_2atmpS1736;
      continue;
    }
    break;
  }
  return _M0L3arrS640;
}

struct _M0TPB5ArrayGiE* _M0MPC15array5Array4makeGiE(
  int32_t _M0L3lenS646,
  int32_t _M0L4elemS648
) {
  struct _M0TPB5ArrayGiE* _M0L3arrS645;
  int32_t _M0L1iS647;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3arrS645 = _M0MPC15array5Array20unsafe__make__uninitGiE(_M0L3lenS646);
  _M0L1iS647 = 0;
  while (1) {
    if (_M0L1iS647 < _M0L3lenS646) {
      int32_t* _M0L3bufS1737 = _M0L3arrS645->$0;
      int32_t _M0L6_2atmpS1738;
      _M0L3bufS1737[_M0L1iS647] = _M0L4elemS648;
      _M0L6_2atmpS1738 = _M0L1iS647 + 1;
      _M0L1iS647 = _M0L6_2atmpS1738;
      continue;
    }
    break;
  }
  return _M0L3arrS645;
}

struct _M0TPB5ArrayGRPB5ArrayGfEE* _M0MPC15array5Array4makeGRPB5ArrayGfEE(
  int32_t _M0L3lenS651,
  struct _M0TPB5ArrayGfE* _M0L4elemS653
) {
  struct _M0TPB5ArrayGRPB5ArrayGfEE* _M0L3arrS650;
  int32_t _M0L1iS652;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3arrS650
  = _M0MPC15array5Array20unsafe__make__uninitGRPB5ArrayGfEE(_M0L3lenS651);
  _M0L1iS652 = 0;
  while (1) {
    if (_M0L1iS652 < _M0L3lenS651) {
      struct _M0TPB5ArrayGfE** _M0L3bufS1739 = _M0L3arrS650->$0;
      struct _M0TPB5ArrayGfE* _M0L6_2aoldS2198 =
        (struct _M0TPB5ArrayGfE*)_M0L3bufS1739[_M0L1iS652];
      int32_t _M0L6_2atmpS1740;
      moonbit_incref(_M0L4elemS653);
      if (_M0L6_2aoldS2198) {
        moonbit_decref(_M0L6_2aoldS2198);
      }
      _M0L3bufS1739[_M0L1iS652] = _M0L4elemS653;
      _M0L6_2atmpS1740 = _M0L1iS652 + 1;
      _M0L1iS652 = _M0L6_2atmpS1740;
      continue;
    } else {
      moonbit_decref(_M0L4elemS653);
    }
    break;
  }
  return _M0L3arrS650;
}

int32_t _M0MPC15array5Array3setGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS620,
  int32_t _M0L5indexS621,
  float _M0L5valueS622
) {
  int32_t _M0L3lenS619;
  #line 262 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS619 = _M0L4selfS620->$1;
  if (_M0L5indexS621 >= 0 && _M0L5indexS621 < _M0L3lenS619) {
    float* _M0L6_2atmpS1729;
    #line 268 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS1729 = _M0MPC15array5Array6bufferGfE(_M0L4selfS620);
    _M0L6_2atmpS1729[_M0L5indexS621] = _M0L5valueS622;
    moonbit_decref(_M0L6_2atmpS1729);
  } else {
    #line 267 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
  return 0;
}

int32_t _M0MPC15array5Array3setGRPB5ArrayGfEE(
  struct _M0TPB5ArrayGRPB5ArrayGfEE* _M0L4selfS624,
  int32_t _M0L5indexS625,
  struct _M0TPB5ArrayGfE* _M0L5valueS626
) {
  int32_t _M0L3lenS623;
  #line 262 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS623 = _M0L4selfS624->$1;
  if (_M0L5indexS625 >= 0 && _M0L5indexS625 < _M0L3lenS623) {
    struct _M0TPB5ArrayGfE** _M0L6_2atmpS1730;
    struct _M0TPB5ArrayGfE* _M0L6_2aoldS2199;
    #line 268 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS1730
    = _M0MPC15array5Array6bufferGRPB5ArrayGfEE(_M0L4selfS624);
    _M0L6_2aoldS2199
    = (struct _M0TPB5ArrayGfE*)_M0L6_2atmpS1730[_M0L5indexS625];
    if (_M0L6_2aoldS2199) {
      moonbit_decref(_M0L6_2aoldS2199);
    }
    _M0L6_2atmpS1730[_M0L5indexS625] = _M0L5valueS626;
    moonbit_decref(_M0L6_2atmpS1730);
  } else {
    moonbit_decref(_M0L5valueS626);
    #line 267 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
  return 0;
}

int32_t _M0MPC15array5Array3setGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS628,
  int32_t _M0L5indexS629,
  int32_t _M0L5valueS630
) {
  int32_t _M0L3lenS627;
  #line 262 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS627 = _M0L4selfS628->$1;
  if (_M0L5indexS629 >= 0 && _M0L5indexS629 < _M0L3lenS627) {
    int32_t* _M0L6_2atmpS1731;
    #line 268 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS1731 = _M0MPC15array5Array6bufferGiE(_M0L4selfS628);
    _M0L6_2atmpS1731[_M0L5indexS629] = _M0L5valueS630;
    moonbit_decref(_M0L6_2atmpS1731);
  } else {
    #line 267 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
  return 0;
}

int32_t _M0MPC15array5Array3setGbE(
  struct _M0TPB5ArrayGbE* _M0L4selfS632,
  int32_t _M0L5indexS633,
  int32_t _M0L5valueS634
) {
  int32_t _M0L3lenS631;
  #line 262 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS631 = _M0L4selfS632->$1;
  if (_M0L5indexS633 >= 0 && _M0L5indexS633 < _M0L3lenS631) {
    uint8_t* _M0L6_2atmpS1732;
    #line 268 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS1732 = _M0MPC15array5Array6bufferGbE(_M0L4selfS632);
    _M0L6_2atmpS1732[_M0L5indexS633] = _M0L5valueS634;
    moonbit_decref(_M0L6_2atmpS1732);
  } else {
    #line 267 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
  return 0;
}

void* _M0MPC15array5Array3popGfE(struct _M0TPB5ArrayGfE* _M0L4selfS612) {
  int32_t _M0L3lenS611;
  #line 592 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L3lenS611 = _M0L4selfS612->$1;
  if (_M0L3lenS611 == 0) {
    return (struct moonbit_object*)&moonbit_constant_constructor_0 + 1;
  } else {
    int32_t _M0L5indexS613 = _M0L3lenS611 - 1;
    float* _M0L3bufS1727 = _M0L4selfS612->$0;
    float _M0L1vS614 = (float)_M0L3bufS1727[_M0L5indexS613];
    void* _block_2309;
    _M0L4selfS612->$1 = _M0L5indexS613;
    _block_2309
    = (void*)moonbit_malloc(sizeof(struct _M0DTPC16option6OptionGfE4Some));
    Moonbit_object_header(_block_2309)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 1);
    ((struct _M0DTPC16option6OptionGfE4Some*)_block_2309)->$0 = _M0L1vS614;
    return _block_2309;
  }
}

int64_t _M0MPC15array5Array3popGiE(struct _M0TPB5ArrayGiE* _M0L4selfS616) {
  int32_t _M0L3lenS615;
  #line 592 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L3lenS615 = _M0L4selfS616->$1;
  if (_M0L3lenS615 == 0) {
    return 4294967296ll;
  } else {
    int32_t _M0L5indexS617 = _M0L3lenS615 - 1;
    int32_t* _M0L3bufS1728 = _M0L4selfS616->$0;
    int32_t _M0L1vS618 = (int32_t)_M0L3bufS1728[_M0L5indexS617];
    _M0L4selfS616->$1 = _M0L5indexS617;
    return (int64_t)_M0L1vS618;
  }
}

struct _M0TP26RiantR8snn__mbt7Monitor* _M0MPC15array5Array2atGRP26RiantR8snn__mbt7MonitorE(
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt7MonitorE* _M0L4selfS597,
  int32_t _M0L5indexS598
) {
  int32_t _M0L3lenS596;
  #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS596 = _M0L4selfS597->$1;
  if (_M0L5indexS598 >= 0 && _M0L5indexS598 < _M0L3lenS596) {
    struct _M0TP26RiantR8snn__mbt7Monitor** _M0L6_2atmpS1722;
    struct _M0TP26RiantR8snn__mbt7Monitor* _M0L6_2atmpS2200;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS1722
    = _M0MPC15array5Array6bufferGRP26RiantR8snn__mbt7MonitorE(_M0L4selfS597);
    _M0L6_2atmpS2200
    = (struct _M0TP26RiantR8snn__mbt7Monitor*)_M0L6_2atmpS1722[
        _M0L5indexS598
      ];
    if (_M0L6_2atmpS2200) {
      moonbit_incref(_M0L6_2atmpS2200);
    }
    moonbit_decref(_M0L6_2atmpS1722);
    return _M0L6_2atmpS2200;
  } else {
    #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
}

float _M0MPC15array5Array2atGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS600,
  int32_t _M0L5indexS601
) {
  int32_t _M0L3lenS599;
  #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS599 = _M0L4selfS600->$1;
  if (_M0L5indexS601 >= 0 && _M0L5indexS601 < _M0L3lenS599) {
    float* _M0L6_2atmpS1723;
    float _result_2310;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS1723 = _M0MPC15array5Array6bufferGfE(_M0L4selfS600);
    _result_2310 = (float)_M0L6_2atmpS1723[_M0L5indexS601];
    moonbit_decref(_M0L6_2atmpS1723);
    return _result_2310;
  } else {
    #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
}

struct _M0TPB5ArrayGfE* _M0MPC15array5Array2atGRPB5ArrayGfEE(
  struct _M0TPB5ArrayGRPB5ArrayGfEE* _M0L4selfS603,
  int32_t _M0L5indexS604
) {
  int32_t _M0L3lenS602;
  #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS602 = _M0L4selfS603->$1;
  if (_M0L5indexS604 >= 0 && _M0L5indexS604 < _M0L3lenS602) {
    struct _M0TPB5ArrayGfE** _M0L6_2atmpS1724;
    struct _M0TPB5ArrayGfE* _M0L6_2atmpS2201;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS1724
    = _M0MPC15array5Array6bufferGRPB5ArrayGfEE(_M0L4selfS603);
    _M0L6_2atmpS2201
    = (struct _M0TPB5ArrayGfE*)_M0L6_2atmpS1724[_M0L5indexS604];
    if (_M0L6_2atmpS2201) {
      moonbit_incref(_M0L6_2atmpS2201);
    }
    moonbit_decref(_M0L6_2atmpS1724);
    return _M0L6_2atmpS2201;
  } else {
    #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
}

int32_t _M0MPC15array5Array2atGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS606,
  int32_t _M0L5indexS607
) {
  int32_t _M0L3lenS605;
  #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS605 = _M0L4selfS606->$1;
  if (_M0L5indexS607 >= 0 && _M0L5indexS607 < _M0L3lenS605) {
    int32_t* _M0L6_2atmpS1725;
    int32_t _result_2311;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS1725 = _M0MPC15array5Array6bufferGiE(_M0L4selfS606);
    _result_2311 = (int32_t)_M0L6_2atmpS1725[_M0L5indexS607];
    moonbit_decref(_M0L6_2atmpS1725);
    return _result_2311;
  } else {
    #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
}

int32_t _M0MPC15array5Array2atGbE(
  struct _M0TPB5ArrayGbE* _M0L4selfS609,
  int32_t _M0L5indexS610
) {
  int32_t _M0L3lenS608;
  #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS608 = _M0L4selfS609->$1;
  if (_M0L5indexS610 >= 0 && _M0L5indexS610 < _M0L3lenS608) {
    uint8_t* _M0L6_2atmpS1726;
    int32_t _result_2312;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS1726 = _M0MPC15array5Array6bufferGbE(_M0L4selfS609);
    _result_2312 = (int32_t)_M0L6_2atmpS1726[_M0L5indexS610];
    moonbit_decref(_M0L6_2atmpS1726);
    return _result_2312;
  } else {
    #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
}

int32_t _M0FPB7printlnGsE(moonbit_string_t _M0L5inputS595) {
  moonbit_string_t _M0L6_2atmpS1721;
  #line 36 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\console.mbt"
  #line 37 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\console.mbt"
  _M0L6_2atmpS1721 = _M0IPC16string6StringPB4Show10to__string(_M0L5inputS595);
  #line 37 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\console.mbt"
  moonbit_println(_M0L6_2atmpS1721);
  moonbit_decref(_M0L6_2atmpS1721);
  return 0;
}

moonbit_string_t _M0MPC16double6Double10to__string(double _M0L4selfS594) {
  #line 296 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double.mbt"
  #line 298 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double.mbt"
  return _M0FPB15ryu__to__string(_M0L4selfS594);
}

int32_t _M0MPC16double6Double7is__inf(double _M0L4selfS593) {
  #line 221 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double.mbt"
  return _M0L4selfS593 > _M0FPB18double__max__value
         || _M0L4selfS593 < _M0FPB18double__min__value;
}

int32_t _M0MPC16double6Double7is__nan(double _M0L4selfS592) {
  #line 196 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double.mbt"
  return _M0L4selfS592 != _M0L4selfS592;
}

moonbit_string_t _M0FPB15ryu__to__string(double _M0L3valS577) {
  uint64_t _M0L4bitsS580;
  uint64_t _M0L6_2atmpS1720;
  uint64_t _M0L6_2atmpS1719;
  int32_t _M0L8ieeeSignS581;
  uint64_t _M0L12ieeeMantissaS582;
  uint64_t _M0L6_2atmpS1718;
  uint64_t _M0L6_2atmpS1717;
  int32_t _M0L12ieeeExponentS583;
  struct _M0TPB17FloatingDecimal64* _M0L7_2abindS584;
  struct _M0TPB17FloatingDecimal64* _M0L1vS585;
  moonbit_string_t _result_2314;
  #line 668 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  if (_M0L3valS577 == 0x0p+0) {
    return (moonbit_string_t)moonbit_string_literal_4.data;
  }
  if (_M0L3valS577 >= -0x1p+53 && _M0L3valS577 <= 0x1p+53) {
    if (_M0L3valS577 >= -0x1p+31 && _M0L3valS577 <= 0x1.fffffffcp+30) {
      int32_t _M0L1iS578;
      double _M0L6_2atmpS1706;
      #line 683 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0L1iS578 = _M0MPC16double6Double7to__int(_M0L3valS577);
      _M0L6_2atmpS1706 = (double)_M0L1iS578;
      if (_M0L6_2atmpS1706 == _M0L3valS577) {
        #line 685 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        return _M0MPC13int3Int18to__string_2einner(_M0L1iS578, 10);
      }
    } else {
      int64_t _M0L1iS579;
      double _M0L6_2atmpS1707;
      #line 688 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0L1iS579 = _M0MPC16double6Double9to__int64(_M0L3valS577);
      _M0L6_2atmpS1707 = (double)_M0L1iS579;
      if (_M0L6_2atmpS1707 == _M0L3valS577) {
        #line 690 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        return _M0MPC15int645Int6418to__string_2einner(_M0L1iS579, 10);
      }
    }
  }
  _M0L4bitsS580 = *(int64_t*)&_M0L3valS577;
  _M0L6_2atmpS1720 = _M0L4bitsS580 >> 63;
  _M0L6_2atmpS1719 = _M0L6_2atmpS1720 & 1ull;
  _M0L8ieeeSignS581 = _M0L6_2atmpS1719 != 0ull;
  _M0L12ieeeMantissaS582 = _M0L4bitsS580 & 4503599627370495ull;
  _M0L6_2atmpS1718 = _M0L4bitsS580 >> 52;
  _M0L6_2atmpS1717 = _M0L6_2atmpS1718 & 2047ull;
  _M0L12ieeeExponentS583 = (int32_t)_M0L6_2atmpS1717;
  if (
    _M0L12ieeeExponentS583 == 2047
    || _M0L12ieeeExponentS583 == 0 && _M0L12ieeeMantissaS582 == 0ull
  ) {
    int32_t _M0L6_2atmpS1708 = _M0L12ieeeExponentS583 != 0;
    int32_t _M0L6_2atmpS1709 = _M0L12ieeeMantissaS582 != 0ull;
    #line 707 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    return _M0FPB18copy__special__str(_M0L8ieeeSignS581, _M0L6_2atmpS1708, _M0L6_2atmpS1709);
  }
  #line 709 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS584
  = _M0FPB15d2d__small__int(_M0L12ieeeMantissaS582, _M0L12ieeeExponentS583);
  if (_M0L7_2abindS584 == 0) {
    uint32_t _M0L6_2atmpS1710;
    if (_M0L7_2abindS584) {
      moonbit_decref(_M0L7_2abindS584);
    }
    _M0L6_2atmpS1710 = *(uint32_t*)&_M0L12ieeeExponentS583;
    #line 719 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L1vS585 = _M0FPB3d2d(_M0L12ieeeMantissaS582, _M0L6_2atmpS1710);
  } else {
    struct _M0TPB17FloatingDecimal64* _M0L7_2aSomeS586 = _M0L7_2abindS584;
    struct _M0TPB17FloatingDecimal64* _M0L4_2afS587 = _M0L7_2aSomeS586;
    struct _M0TPB17FloatingDecimal64* _M0L1xS588 = _M0L4_2afS587;
    while (1) {
      uint64_t _M0L8mantissaS1716 = _M0L1xS588->$0;
      uint64_t _M0L1qS589 = _M0L8mantissaS1716 / 10ull;
      uint64_t _M0L8mantissaS1714 = _M0L1xS588->$0;
      uint64_t _M0L6_2atmpS1715 = 10ull * _M0L1qS589;
      uint64_t _M0L1rS590 = _M0L8mantissaS1714 - _M0L6_2atmpS1715;
      int32_t _M0L8exponentS1713;
      int32_t _M0L6_2atmpS1712;
      struct _M0TPB17FloatingDecimal64* _M0L6_2atmpS1711;
      if (_M0L1rS590 != 0ull) {
        _M0L1vS585 = _M0L1xS588;
        break;
      }
      _M0L8exponentS1713 = _M0L1xS588->$1;
      moonbit_decref(_M0L1xS588);
      _M0L6_2atmpS1712 = _M0L8exponentS1713 + 1;
      _M0L6_2atmpS1711
      = (struct _M0TPB17FloatingDecimal64*)moonbit_malloc(sizeof(struct _M0TPB17FloatingDecimal64));
      Moonbit_object_header(_M0L6_2atmpS1711)->meta
      = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
      _M0L6_2atmpS1711->$0 = _M0L1qS589;
      _M0L6_2atmpS1711->$1 = _M0L6_2atmpS1712;
      _M0L1xS588 = _M0L6_2atmpS1711;
      continue;
      break;
    }
  }
  #line 721 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _result_2314 = _M0FPB9to__chars(_M0L1vS585, _M0L8ieeeSignS581);
  moonbit_decref(_M0L1vS585);
  return _result_2314;
}

struct _M0TPB17FloatingDecimal64* _M0FPB15d2d__small__int(
  uint64_t _M0L12ieeeMantissaS572,
  int32_t _M0L12ieeeExponentS574
) {
  uint64_t _M0L2m2S571;
  int32_t _M0L6_2atmpS1705;
  int32_t _M0L2e2S573;
  int32_t _M0L6_2atmpS1704;
  uint64_t _M0L6_2atmpS1703;
  uint64_t _M0L4maskS575;
  uint64_t _M0L8fractionS576;
  int32_t _M0L6_2atmpS1702;
  uint64_t _M0L6_2atmpS1701;
  struct _M0TPB17FloatingDecimal64* _M0L6_2atmpS1700;
  #line 637 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L2m2S571 = 4503599627370496ull | _M0L12ieeeMantissaS572;
  _M0L6_2atmpS1705 = _M0L12ieeeExponentS574 - 1023;
  _M0L2e2S573 = _M0L6_2atmpS1705 - 52;
  if (_M0L2e2S573 > 0) {
    return 0;
  }
  if (_M0L2e2S573 < -52) {
    return 0;
  }
  _M0L6_2atmpS1704 = -_M0L2e2S573;
  _M0L6_2atmpS1703 = 1ull << (_M0L6_2atmpS1704 & 63);
  _M0L4maskS575 = _M0L6_2atmpS1703 - 1ull;
  _M0L8fractionS576 = _M0L2m2S571 & _M0L4maskS575;
  if (_M0L8fractionS576 != 0ull) {
    return 0;
  }
  _M0L6_2atmpS1702 = -_M0L2e2S573;
  _M0L6_2atmpS1701 = _M0L2m2S571 >> (_M0L6_2atmpS1702 & 63);
  _M0L6_2atmpS1700
  = (struct _M0TPB17FloatingDecimal64*)moonbit_malloc(sizeof(struct _M0TPB17FloatingDecimal64));
  Moonbit_object_header(_M0L6_2atmpS1700)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L6_2atmpS1700->$0 = _M0L6_2atmpS1701;
  _M0L6_2atmpS1700->$1 = 0;
  return _M0L6_2atmpS1700;
}

moonbit_string_t _M0FPB9to__chars(
  struct _M0TPB17FloatingDecimal64* _M0L1vS539,
  int32_t _M0L4signS537
) {
  moonbit_bytes_t _M0L6resultS535;
  int32_t _M0Lm5indexS536;
  uint64_t _M0L6outputS538;
  int32_t _M0L7olengthS540;
  int32_t _M0L8exponentS1699;
  int32_t _M0L6_2atmpS1698;
  int32_t _M0Lm3expS541;
  int32_t _M0L6_2atmpS1697;
  int32_t _M0L6_2atmpS1695;
  int32_t _M0L18scientificNotationS542;
  #line 530 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6resultS535 = (moonbit_bytes_t)moonbit_make_bytes(25, 0);
  _M0Lm5indexS536 = 0;
  if (_M0L4signS537) {
    int32_t _M0L6_2atmpS1569 = _M0Lm5indexS536;
    int32_t _M0L6_2atmpS1570;
    if (
      _M0L6_2atmpS1569 < 0
      || _M0L6_2atmpS1569 >= Moonbit_array_length(_M0L6resultS535)
    ) {
      #line 535 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      moonbit_panic();
    }
    _M0L6resultS535[_M0L6_2atmpS1569] = 45;
    _M0L6_2atmpS1570 = _M0Lm5indexS536;
    _M0Lm5indexS536 = _M0L6_2atmpS1570 + 1;
  }
  _M0L6outputS538 = _M0L1vS539->$0;
  #line 539 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7olengthS540 = _M0FPB17decimal__length17(_M0L6outputS538);
  _M0L8exponentS1699 = _M0L1vS539->$1;
  _M0L6_2atmpS1698 = _M0L8exponentS1699 + _M0L7olengthS540;
  _M0Lm3expS541 = _M0L6_2atmpS1698 - 1;
  _M0L6_2atmpS1697 = _M0Lm3expS541;
  if (_M0L6_2atmpS1697 >= -6) {
    int32_t _M0L6_2atmpS1696 = _M0Lm3expS541;
    _M0L6_2atmpS1695 = _M0L6_2atmpS1696 < 21;
  } else {
    _M0L6_2atmpS1695 = 0;
  }
  _M0L18scientificNotationS542 = !_M0L6_2atmpS1695;
  if (_M0L18scientificNotationS542) {
    int32_t _M0L7_2abindS543 = _M0L7olengthS540 - 1;
    uint64_t _M0L6outputS544;
    int32_t _M0L1iS545 = 0;
    uint64_t _M0L6outputS546 = _M0L6outputS538;
    int32_t _M0L6_2atmpS1571;
    int32_t _M0L6_2atmpS1575;
    int32_t _M0L6_2atmpS1574;
    int32_t _M0L6_2atmpS1573;
    int32_t _M0L6_2atmpS1572;
    int32_t _M0L6_2atmpS1579;
    int32_t _M0L6_2atmpS1580;
    int32_t _M0L6_2atmpS1581;
    int32_t _M0L6_2atmpS1582;
    int32_t _M0L6_2atmpS1583;
    int32_t _M0L6_2atmpS1589;
    int32_t _M0L6_2atmpS1622;
    moonbit_string_t _result_2316;
    while (1) {
      if (_M0L1iS545 < _M0L7_2abindS543) {
        uint64_t _M0L1cS547 = _M0L6outputS546 % 10ull;
        int32_t _M0L6_2atmpS1628 = _M0Lm5indexS536;
        int32_t _M0L6_2atmpS1627 = _M0L6_2atmpS1628 + _M0L7olengthS540;
        int32_t _M0L6_2atmpS1623 = _M0L6_2atmpS1627 - _M0L1iS545;
        int32_t _M0L6_2atmpS1626 = (int32_t)_M0L1cS547;
        int32_t _M0L6_2atmpS1625 = 48 + _M0L6_2atmpS1626;
        int32_t _M0L6_2atmpS1624 = _M0L6_2atmpS1625 & 0xff;
        int32_t _M0L6_2atmpS1629;
        uint64_t _M0L6_2atmpS1630;
        if (
          _M0L6_2atmpS1623 < 0
          || _M0L6_2atmpS1623 >= Moonbit_array_length(_M0L6resultS535)
        ) {
          #line 547 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
          moonbit_panic();
        }
        _M0L6resultS535[_M0L6_2atmpS1623] = _M0L6_2atmpS1624;
        _M0L6_2atmpS1629 = _M0L1iS545 + 1;
        _M0L6_2atmpS1630 = _M0L6outputS546 / 10ull;
        _M0L1iS545 = _M0L6_2atmpS1629;
        _M0L6outputS546 = _M0L6_2atmpS1630;
        continue;
      } else {
        _M0L6outputS544 = _M0L6outputS546;
      }
      break;
    }
    _M0L6_2atmpS1571 = _M0Lm5indexS536;
    _M0L6_2atmpS1575 = (int32_t)_M0L6outputS544;
    _M0L6_2atmpS1574 = _M0L6_2atmpS1575 % 10;
    _M0L6_2atmpS1573 = 48 + _M0L6_2atmpS1574;
    _M0L6_2atmpS1572 = _M0L6_2atmpS1573 & 0xff;
    if (
      _M0L6_2atmpS1571 < 0
      || _M0L6_2atmpS1571 >= Moonbit_array_length(_M0L6resultS535)
    ) {
      #line 552 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      moonbit_panic();
    }
    _M0L6resultS535[_M0L6_2atmpS1571] = _M0L6_2atmpS1572;
    if (_M0L7olengthS540 > 1) {
      int32_t _M0L6_2atmpS1577 = _M0Lm5indexS536;
      int32_t _M0L6_2atmpS1576 = _M0L6_2atmpS1577 + 1;
      if (
        _M0L6_2atmpS1576 < 0
        || _M0L6_2atmpS1576 >= Moonbit_array_length(_M0L6resultS535)
      ) {
        #line 554 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS535[_M0L6_2atmpS1576] = 46;
    } else {
      int32_t _M0L6_2atmpS1578 = _M0Lm5indexS536;
      _M0Lm5indexS536 = _M0L6_2atmpS1578 - 1;
    }
    _M0L6_2atmpS1579 = _M0Lm5indexS536;
    _M0L6_2atmpS1580 = _M0L7olengthS540 + 1;
    _M0Lm5indexS536 = _M0L6_2atmpS1579 + _M0L6_2atmpS1580;
    _M0L6_2atmpS1581 = _M0Lm5indexS536;
    if (
      _M0L6_2atmpS1581 < 0
      || _M0L6_2atmpS1581 >= Moonbit_array_length(_M0L6resultS535)
    ) {
      #line 562 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      moonbit_panic();
    }
    _M0L6resultS535[_M0L6_2atmpS1581] = 101;
    _M0L6_2atmpS1582 = _M0Lm5indexS536;
    _M0Lm5indexS536 = _M0L6_2atmpS1582 + 1;
    _M0L6_2atmpS1583 = _M0Lm3expS541;
    if (_M0L6_2atmpS1583 < 0) {
      int32_t _M0L6_2atmpS1584 = _M0Lm5indexS536;
      int32_t _M0L6_2atmpS1585;
      int32_t _M0L6_2atmpS1586;
      if (
        _M0L6_2atmpS1584 < 0
        || _M0L6_2atmpS1584 >= Moonbit_array_length(_M0L6resultS535)
      ) {
        #line 565 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS535[_M0L6_2atmpS1584] = 45;
      _M0L6_2atmpS1585 = _M0Lm5indexS536;
      _M0Lm5indexS536 = _M0L6_2atmpS1585 + 1;
      _M0L6_2atmpS1586 = _M0Lm3expS541;
      _M0Lm3expS541 = -_M0L6_2atmpS1586;
    } else {
      int32_t _M0L6_2atmpS1587 = _M0Lm5indexS536;
      int32_t _M0L6_2atmpS1588;
      if (
        _M0L6_2atmpS1587 < 0
        || _M0L6_2atmpS1587 >= Moonbit_array_length(_M0L6resultS535)
      ) {
        #line 569 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS535[_M0L6_2atmpS1587] = 43;
      _M0L6_2atmpS1588 = _M0Lm5indexS536;
      _M0Lm5indexS536 = _M0L6_2atmpS1588 + 1;
    }
    _M0L6_2atmpS1589 = _M0Lm3expS541;
    if (_M0L6_2atmpS1589 >= 100) {
      int32_t _M0L6_2atmpS1605 = _M0Lm3expS541;
      int32_t _M0L1aS549 = _M0L6_2atmpS1605 / 100;
      int32_t _M0L6_2atmpS1604 = _M0Lm3expS541;
      int32_t _M0L6_2atmpS1603 = _M0L6_2atmpS1604 / 10;
      int32_t _M0L1bS550 = _M0L6_2atmpS1603 % 10;
      int32_t _M0L6_2atmpS1602 = _M0Lm3expS541;
      int32_t _M0L1cS551 = _M0L6_2atmpS1602 % 10;
      int32_t _M0L6_2atmpS1590 = _M0Lm5indexS536;
      int32_t _M0L6_2atmpS1592 = 48 + _M0L1aS549;
      int32_t _M0L6_2atmpS1591 = _M0L6_2atmpS1592 & 0xff;
      int32_t _M0L6_2atmpS1596;
      int32_t _M0L6_2atmpS1593;
      int32_t _M0L6_2atmpS1595;
      int32_t _M0L6_2atmpS1594;
      int32_t _M0L6_2atmpS1600;
      int32_t _M0L6_2atmpS1597;
      int32_t _M0L6_2atmpS1599;
      int32_t _M0L6_2atmpS1598;
      int32_t _M0L6_2atmpS1601;
      if (
        _M0L6_2atmpS1590 < 0
        || _M0L6_2atmpS1590 >= Moonbit_array_length(_M0L6resultS535)
      ) {
        #line 576 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS535[_M0L6_2atmpS1590] = _M0L6_2atmpS1591;
      _M0L6_2atmpS1596 = _M0Lm5indexS536;
      _M0L6_2atmpS1593 = _M0L6_2atmpS1596 + 1;
      _M0L6_2atmpS1595 = 48 + _M0L1bS550;
      _M0L6_2atmpS1594 = _M0L6_2atmpS1595 & 0xff;
      if (
        _M0L6_2atmpS1593 < 0
        || _M0L6_2atmpS1593 >= Moonbit_array_length(_M0L6resultS535)
      ) {
        #line 577 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS535[_M0L6_2atmpS1593] = _M0L6_2atmpS1594;
      _M0L6_2atmpS1600 = _M0Lm5indexS536;
      _M0L6_2atmpS1597 = _M0L6_2atmpS1600 + 2;
      _M0L6_2atmpS1599 = 48 + _M0L1cS551;
      _M0L6_2atmpS1598 = _M0L6_2atmpS1599 & 0xff;
      if (
        _M0L6_2atmpS1597 < 0
        || _M0L6_2atmpS1597 >= Moonbit_array_length(_M0L6resultS535)
      ) {
        #line 578 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS535[_M0L6_2atmpS1597] = _M0L6_2atmpS1598;
      _M0L6_2atmpS1601 = _M0Lm5indexS536;
      _M0Lm5indexS536 = _M0L6_2atmpS1601 + 3;
    } else {
      int32_t _M0L6_2atmpS1606 = _M0Lm3expS541;
      if (_M0L6_2atmpS1606 >= 10) {
        int32_t _M0L6_2atmpS1616 = _M0Lm3expS541;
        int32_t _M0L1aS552 = _M0L6_2atmpS1616 / 10;
        int32_t _M0L6_2atmpS1615 = _M0Lm3expS541;
        int32_t _M0L1bS553 = _M0L6_2atmpS1615 % 10;
        int32_t _M0L6_2atmpS1607 = _M0Lm5indexS536;
        int32_t _M0L6_2atmpS1609 = 48 + _M0L1aS552;
        int32_t _M0L6_2atmpS1608 = _M0L6_2atmpS1609 & 0xff;
        int32_t _M0L6_2atmpS1613;
        int32_t _M0L6_2atmpS1610;
        int32_t _M0L6_2atmpS1612;
        int32_t _M0L6_2atmpS1611;
        int32_t _M0L6_2atmpS1614;
        if (
          _M0L6_2atmpS1607 < 0
          || _M0L6_2atmpS1607 >= Moonbit_array_length(_M0L6resultS535)
        ) {
          #line 583 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
          moonbit_panic();
        }
        _M0L6resultS535[_M0L6_2atmpS1607] = _M0L6_2atmpS1608;
        _M0L6_2atmpS1613 = _M0Lm5indexS536;
        _M0L6_2atmpS1610 = _M0L6_2atmpS1613 + 1;
        _M0L6_2atmpS1612 = 48 + _M0L1bS553;
        _M0L6_2atmpS1611 = _M0L6_2atmpS1612 & 0xff;
        if (
          _M0L6_2atmpS1610 < 0
          || _M0L6_2atmpS1610 >= Moonbit_array_length(_M0L6resultS535)
        ) {
          #line 584 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
          moonbit_panic();
        }
        _M0L6resultS535[_M0L6_2atmpS1610] = _M0L6_2atmpS1611;
        _M0L6_2atmpS1614 = _M0Lm5indexS536;
        _M0Lm5indexS536 = _M0L6_2atmpS1614 + 2;
      } else {
        int32_t _M0L6_2atmpS1617 = _M0Lm5indexS536;
        int32_t _M0L6_2atmpS1620 = _M0Lm3expS541;
        int32_t _M0L6_2atmpS1619 = 48 + _M0L6_2atmpS1620;
        int32_t _M0L6_2atmpS1618 = _M0L6_2atmpS1619 & 0xff;
        int32_t _M0L6_2atmpS1621;
        if (
          _M0L6_2atmpS1617 < 0
          || _M0L6_2atmpS1617 >= Moonbit_array_length(_M0L6resultS535)
        ) {
          #line 587 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
          moonbit_panic();
        }
        _M0L6resultS535[_M0L6_2atmpS1617] = _M0L6_2atmpS1618;
        _M0L6_2atmpS1621 = _M0Lm5indexS536;
        _M0Lm5indexS536 = _M0L6_2atmpS1621 + 1;
      }
    }
    _M0L6_2atmpS1622 = _M0Lm5indexS536;
    #line 590 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _result_2316
    = _M0FPB19string__from__bytes(_M0L6resultS535, 0, _M0L6_2atmpS1622);
    moonbit_decref(_M0L6resultS535);
    return _result_2316;
  } else {
    int32_t _M0L6_2atmpS1631 = _M0Lm3expS541;
    int32_t _M0L6_2atmpS1694;
    moonbit_string_t _result_2322;
    if (_M0L6_2atmpS1631 < 0) {
      int32_t _M0L6_2atmpS1632 = _M0Lm5indexS536;
      int32_t _M0L6_2atmpS1634;
      int32_t _M0L6_2atmpS1633;
      int32_t _M0L6_2atmpS1635;
      int32_t _M0L1iS554;
      int32_t _M0L6_2atmpS1650;
      int32_t _M0L6_2atmpS1652;
      int32_t _M0L6_2atmpS1651;
      int32_t _M0L7currentS556;
      int32_t _M0L1iS557;
      uint64_t _M0L6outputS558;
      if (
        _M0L6_2atmpS1632 < 0
        || _M0L6_2atmpS1632 >= Moonbit_array_length(_M0L6resultS535)
      ) {
        #line 595 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS535[_M0L6_2atmpS1632] = 48;
      _M0L6_2atmpS1634 = _M0Lm5indexS536;
      _M0L6_2atmpS1633 = _M0L6_2atmpS1634 + 1;
      if (
        _M0L6_2atmpS1633 < 0
        || _M0L6_2atmpS1633 >= Moonbit_array_length(_M0L6resultS535)
      ) {
        #line 596 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS535[_M0L6_2atmpS1633] = 46;
      _M0L6_2atmpS1635 = _M0Lm5indexS536;
      _M0Lm5indexS536 = _M0L6_2atmpS1635 + 2;
      _M0L1iS554 = -1;
      while (1) {
        int32_t _M0L6_2atmpS1636 = _M0Lm3expS541;
        if (_M0L1iS554 > _M0L6_2atmpS1636) {
          int32_t _M0L6_2atmpS1639 = _M0Lm5indexS536;
          int32_t _M0L6_2atmpS1638 = _M0L6_2atmpS1639 - _M0L1iS554;
          int32_t _M0L6_2atmpS1637 = _M0L6_2atmpS1638 - 1;
          int32_t _M0L6_2atmpS1640;
          if (
            _M0L6_2atmpS1637 < 0
            || _M0L6_2atmpS1637 >= Moonbit_array_length(_M0L6resultS535)
          ) {
            #line 599 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
            moonbit_panic();
          }
          _M0L6resultS535[_M0L6_2atmpS1637] = 48;
          _M0L6_2atmpS1640 = _M0L1iS554 - 1;
          _M0L1iS554 = _M0L6_2atmpS1640;
          continue;
        }
        break;
      }
      _M0L6_2atmpS1650 = _M0Lm5indexS536;
      _M0L6_2atmpS1652 = _M0Lm3expS541;
      _M0L6_2atmpS1651 = -1 - _M0L6_2atmpS1652;
      _M0L7currentS556 = _M0L6_2atmpS1650 + _M0L6_2atmpS1651;
      _M0L1iS557 = 0;
      _M0L6outputS558 = _M0L6outputS538;
      while (1) {
        if (_M0L1iS557 < _M0L7olengthS540) {
          int32_t _M0L6_2atmpS1647 = _M0L7currentS556 + _M0L7olengthS540;
          int32_t _M0L6_2atmpS1646 = _M0L6_2atmpS1647 - _M0L1iS557;
          int32_t _M0L6_2atmpS1641 = _M0L6_2atmpS1646 - 1;
          uint64_t _M0L6_2atmpS1645 = _M0L6outputS558 % 10ull;
          int32_t _M0L6_2atmpS1644 = (int32_t)_M0L6_2atmpS1645;
          int32_t _M0L6_2atmpS1643 = 48 + _M0L6_2atmpS1644;
          int32_t _M0L6_2atmpS1642 = _M0L6_2atmpS1643 & 0xff;
          int32_t _M0L6_2atmpS1648;
          uint64_t _M0L6_2atmpS1649;
          if (
            _M0L6_2atmpS1641 < 0
            || _M0L6_2atmpS1641 >= Moonbit_array_length(_M0L6resultS535)
          ) {
            #line 603 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
            moonbit_panic();
          }
          _M0L6resultS535[_M0L6_2atmpS1641] = _M0L6_2atmpS1642;
          _M0L6_2atmpS1648 = _M0L1iS557 + 1;
          _M0L6_2atmpS1649 = _M0L6outputS558 / 10ull;
          _M0L1iS557 = _M0L6_2atmpS1648;
          _M0L6outputS558 = _M0L6_2atmpS1649;
          continue;
        }
        break;
      }
      _M0Lm5indexS536 = _M0L7currentS556 + _M0L7olengthS540;
    } else {
      int32_t _M0L6_2atmpS1654 = _M0Lm3expS541;
      int32_t _M0L6_2atmpS1653 = _M0L6_2atmpS1654 + 1;
      if (_M0L6_2atmpS1653 >= _M0L7olengthS540) {
        int32_t _M0L1iS560 = 0;
        uint64_t _M0L6outputS561 = _M0L6outputS538;
        int32_t _M0L6_2atmpS1665;
        int32_t _M0L6_2atmpS1670;
        int32_t _M0L7_2abindS563;
        int32_t _M0L1iS564;
        int32_t _M0L6_2atmpS1671;
        int32_t _M0L6_2atmpS1674;
        int32_t _M0L6_2atmpS1673;
        int32_t _M0L6_2atmpS1672;
        while (1) {
          if (_M0L1iS560 < _M0L7olengthS540) {
            int32_t _M0L6_2atmpS1662 = _M0Lm5indexS536;
            int32_t _M0L6_2atmpS1661 = _M0L6_2atmpS1662 + _M0L7olengthS540;
            int32_t _M0L6_2atmpS1660 = _M0L6_2atmpS1661 - _M0L1iS560;
            int32_t _M0L6_2atmpS1655 = _M0L6_2atmpS1660 - 1;
            uint64_t _M0L6_2atmpS1659 = _M0L6outputS561 % 10ull;
            int32_t _M0L6_2atmpS1658 = (int32_t)_M0L6_2atmpS1659;
            int32_t _M0L6_2atmpS1657 = 48 + _M0L6_2atmpS1658;
            int32_t _M0L6_2atmpS1656 = _M0L6_2atmpS1657 & 0xff;
            int32_t _M0L6_2atmpS1663;
            uint64_t _M0L6_2atmpS1664;
            if (
              _M0L6_2atmpS1655 < 0
              || _M0L6_2atmpS1655 >= Moonbit_array_length(_M0L6resultS535)
            ) {
              #line 610 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
              moonbit_panic();
            }
            _M0L6resultS535[_M0L6_2atmpS1655] = _M0L6_2atmpS1656;
            _M0L6_2atmpS1663 = _M0L1iS560 + 1;
            _M0L6_2atmpS1664 = _M0L6outputS561 / 10ull;
            _M0L1iS560 = _M0L6_2atmpS1663;
            _M0L6outputS561 = _M0L6_2atmpS1664;
            continue;
          }
          break;
        }
        _M0L6_2atmpS1665 = _M0Lm5indexS536;
        _M0Lm5indexS536 = _M0L6_2atmpS1665 + _M0L7olengthS540;
        _M0L6_2atmpS1670 = _M0Lm3expS541;
        _M0L7_2abindS563 = _M0L6_2atmpS1670 + 1;
        _M0L1iS564 = _M0L7olengthS540;
        while (1) {
          if (_M0L1iS564 < _M0L7_2abindS563) {
            int32_t _M0L6_2atmpS1668 = _M0Lm5indexS536;
            int32_t _M0L6_2atmpS1667 = _M0L6_2atmpS1668 + _M0L1iS564;
            int32_t _M0L6_2atmpS1666 = _M0L6_2atmpS1667 - _M0L7olengthS540;
            int32_t _M0L6_2atmpS1669;
            if (
              _M0L6_2atmpS1666 < 0
              || _M0L6_2atmpS1666 >= Moonbit_array_length(_M0L6resultS535)
            ) {
              #line 615 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
              moonbit_panic();
            }
            _M0L6resultS535[_M0L6_2atmpS1666] = 48;
            _M0L6_2atmpS1669 = _M0L1iS564 + 1;
            _M0L1iS564 = _M0L6_2atmpS1669;
            continue;
          }
          break;
        }
        _M0L6_2atmpS1671 = _M0Lm5indexS536;
        _M0L6_2atmpS1674 = _M0Lm3expS541;
        _M0L6_2atmpS1673 = _M0L6_2atmpS1674 + 1;
        _M0L6_2atmpS1672 = _M0L6_2atmpS1673 - _M0L7olengthS540;
        _M0Lm5indexS536 = _M0L6_2atmpS1671 + _M0L6_2atmpS1672;
      } else {
        int32_t _M0L6_2atmpS1691 = _M0Lm5indexS536;
        int32_t _M0L6_2atmpS1690 = _M0L6_2atmpS1691 + 1;
        int32_t _M0L1iS566 = 0;
        int32_t _M0L7currentS567 = _M0L6_2atmpS1690;
        uint64_t _M0L6outputS568 = _M0L6outputS538;
        int32_t _M0L6_2atmpS1692;
        int32_t _M0L6_2atmpS1693;
        while (1) {
          if (_M0L1iS566 < _M0L7olengthS540) {
            int32_t _M0L6_2atmpS1686 = _M0L7olengthS540 - _M0L1iS566;
            int32_t _M0L6_2atmpS1684 = _M0L6_2atmpS1686 - 1;
            int32_t _M0L6_2atmpS1685 = _M0Lm3expS541;
            int32_t _M0L7currentS569;
            int32_t _M0L6_2atmpS1681;
            int32_t _M0L6_2atmpS1680;
            int32_t _M0L6_2atmpS1675;
            uint64_t _M0L6_2atmpS1679;
            int32_t _M0L6_2atmpS1678;
            int32_t _M0L6_2atmpS1677;
            int32_t _M0L6_2atmpS1676;
            int32_t _M0L6_2atmpS1682;
            uint64_t _M0L6_2atmpS1683;
            if (_M0L6_2atmpS1684 == _M0L6_2atmpS1685) {
              int32_t _M0L6_2atmpS1689 = _M0L7currentS567 + _M0L7olengthS540;
              int32_t _M0L6_2atmpS1688 = _M0L6_2atmpS1689 - _M0L1iS566;
              int32_t _M0L6_2atmpS1687 = _M0L6_2atmpS1688 - 1;
              if (
                _M0L6_2atmpS1687 < 0
                || _M0L6_2atmpS1687 >= Moonbit_array_length(_M0L6resultS535)
              ) {
                #line 622 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
                moonbit_panic();
              }
              _M0L6resultS535[_M0L6_2atmpS1687] = 46;
              _M0L7currentS569 = _M0L7currentS567 - 1;
            } else {
              _M0L7currentS569 = _M0L7currentS567;
            }
            _M0L6_2atmpS1681 = _M0L7currentS569 + _M0L7olengthS540;
            _M0L6_2atmpS1680 = _M0L6_2atmpS1681 - _M0L1iS566;
            _M0L6_2atmpS1675 = _M0L6_2atmpS1680 - 1;
            _M0L6_2atmpS1679 = _M0L6outputS568 % 10ull;
            _M0L6_2atmpS1678 = (int32_t)_M0L6_2atmpS1679;
            _M0L6_2atmpS1677 = 48 + _M0L6_2atmpS1678;
            _M0L6_2atmpS1676 = _M0L6_2atmpS1677 & 0xff;
            if (
              _M0L6_2atmpS1675 < 0
              || _M0L6_2atmpS1675 >= Moonbit_array_length(_M0L6resultS535)
            ) {
              #line 627 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
              moonbit_panic();
            }
            _M0L6resultS535[_M0L6_2atmpS1675] = _M0L6_2atmpS1676;
            _M0L6_2atmpS1682 = _M0L1iS566 + 1;
            _M0L6_2atmpS1683 = _M0L6outputS568 / 10ull;
            _M0L1iS566 = _M0L6_2atmpS1682;
            _M0L7currentS567 = _M0L7currentS569;
            _M0L6outputS568 = _M0L6_2atmpS1683;
            continue;
          }
          break;
        }
        _M0L6_2atmpS1692 = _M0Lm5indexS536;
        _M0L6_2atmpS1693 = _M0L7olengthS540 + 1;
        _M0Lm5indexS536 = _M0L6_2atmpS1692 + _M0L6_2atmpS1693;
      }
    }
    _M0L6_2atmpS1694 = _M0Lm5indexS536;
    #line 632 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _result_2322
    = _M0FPB19string__from__bytes(_M0L6resultS535, 0, _M0L6_2atmpS1694);
    moonbit_decref(_M0L6resultS535);
    return _result_2322;
  }
}

struct _M0TPB17FloatingDecimal64* _M0FPB3d2d(
  uint64_t _M0L12ieeeMantissaS481,
  uint32_t _M0L12ieeeExponentS480
) {
  int32_t _M0Lm2e2S478;
  uint64_t _M0Lm2m2S479;
  uint64_t _M0L6_2atmpS1568;
  uint64_t _M0L6_2atmpS1567;
  int32_t _M0L4evenS482;
  uint64_t _M0L6_2atmpS1566;
  uint64_t _M0L2mvS483;
  int32_t _M0L7mmShiftS484;
  uint64_t _M0Lm2vrS485;
  uint64_t _M0Lm2vpS486;
  uint64_t _M0Lm2vmS487;
  int32_t _M0Lm3e10S488;
  int32_t _M0Lm17vmIsTrailingZerosS489;
  int32_t _M0Lm17vrIsTrailingZerosS490;
  int32_t _M0L6_2atmpS1468;
  int32_t _M0Lm7removedS509;
  int32_t _M0Lm16lastRemovedDigitS510;
  uint64_t _M0Lm6outputS511;
  int32_t _M0L6_2atmpS1564;
  int32_t _M0L6_2atmpS1565;
  int32_t _M0L3expS534;
  uint64_t _M0L6_2atmpS1563;
  struct _M0TPB17FloatingDecimal64* _block_2328;
  #line 347 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0Lm2e2S478 = 0;
  _M0Lm2m2S479 = 0ull;
  if (_M0L12ieeeExponentS480 == 0u) {
    _M0Lm2e2S478 = -1076;
    _M0Lm2m2S479 = _M0L12ieeeMantissaS481;
  } else {
    int32_t _M0L6_2atmpS1467 = *(int32_t*)&_M0L12ieeeExponentS480;
    int32_t _M0L6_2atmpS1466 = _M0L6_2atmpS1467 - 1023;
    int32_t _M0L6_2atmpS1465 = _M0L6_2atmpS1466 - 52;
    _M0Lm2e2S478 = _M0L6_2atmpS1465 - 2;
    _M0Lm2m2S479 = 4503599627370496ull | _M0L12ieeeMantissaS481;
  }
  _M0L6_2atmpS1568 = _M0Lm2m2S479;
  _M0L6_2atmpS1567 = _M0L6_2atmpS1568 & 1ull;
  _M0L4evenS482 = _M0L6_2atmpS1567 == 0ull;
  _M0L6_2atmpS1566 = _M0Lm2m2S479;
  _M0L2mvS483 = 4ull * _M0L6_2atmpS1566;
  _M0L7mmShiftS484
  = _M0L12ieeeMantissaS481 != 0ull || _M0L12ieeeExponentS480 <= 1u;
  _M0Lm2vrS485 = 0ull;
  _M0Lm2vpS486 = 0ull;
  _M0Lm2vmS487 = 0ull;
  _M0Lm3e10S488 = 0;
  _M0Lm17vmIsTrailingZerosS489 = 0;
  _M0Lm17vrIsTrailingZerosS490 = 0;
  _M0L6_2atmpS1468 = _M0Lm2e2S478;
  if (_M0L6_2atmpS1468 >= 0) {
    int32_t _M0L6_2atmpS1490 = _M0Lm2e2S478;
    int32_t _M0L6_2atmpS1486;
    int32_t _M0L6_2atmpS1489;
    int32_t _M0L6_2atmpS1488;
    int32_t _M0L6_2atmpS1487;
    int32_t _M0L1qS491;
    int32_t _M0L6_2atmpS1485;
    int32_t _M0L6_2atmpS1484;
    int32_t _M0L1kS492;
    int32_t _M0L6_2atmpS1483;
    int32_t _M0L6_2atmpS1482;
    int32_t _M0L6_2atmpS1481;
    int32_t _M0L1iS493;
    struct _M0TPB8Pow5Pair _M0L4pow5S494;
    uint64_t _M0L6_2atmpS1480;
    struct _M0TPB19MulShiftAll64Result _M0L7_2abindS495;
    uint64_t _M0L8_2avrOutS496;
    uint64_t _M0L8_2avpOutS497;
    uint64_t _M0L8_2avmOutS498;
    #line 383 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1486 = _M0FPB9log10Pow2(_M0L6_2atmpS1490);
    _M0L6_2atmpS1489 = _M0Lm2e2S478;
    _M0L6_2atmpS1488 = _M0L6_2atmpS1489 > 3;
    #line 383 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1487 = _M0MPC14bool4Bool7to__int(_M0L6_2atmpS1488);
    _M0L1qS491 = _M0L6_2atmpS1486 - _M0L6_2atmpS1487;
    _M0Lm3e10S488 = _M0L1qS491;
    #line 385 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1485 = _M0FPB8pow5bits(_M0L1qS491);
    _M0L6_2atmpS1484 = 125 + _M0L6_2atmpS1485;
    _M0L1kS492 = _M0L6_2atmpS1484 - 1;
    _M0L6_2atmpS1483 = _M0Lm2e2S478;
    _M0L6_2atmpS1482 = -_M0L6_2atmpS1483;
    _M0L6_2atmpS1481 = _M0L6_2atmpS1482 + _M0L1qS491;
    _M0L1iS493 = _M0L6_2atmpS1481 + _M0L1kS492;
    #line 387 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L4pow5S494 = _M0FPB22double__computeInvPow5(_M0L1qS491);
    _M0L6_2atmpS1480 = _M0Lm2m2S479;
    #line 388 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L7_2abindS495
    = _M0FPB13mulShiftAll64(_M0L6_2atmpS1480, _M0L4pow5S494, _M0L1iS493, _M0L7mmShiftS484);
    _M0L8_2avrOutS496 = _M0L7_2abindS495.$0;
    _M0L8_2avpOutS497 = _M0L7_2abindS495.$1;
    _M0L8_2avmOutS498 = _M0L7_2abindS495.$2;
    _M0Lm2vrS485 = _M0L8_2avrOutS496;
    _M0Lm2vpS486 = _M0L8_2avpOutS497;
    _M0Lm2vmS487 = _M0L8_2avmOutS498;
    if (_M0L1qS491 <= 21) {
      int32_t _M0L6_2atmpS1476 = (int32_t)_M0L2mvS483;
      uint64_t _M0L6_2atmpS1479 = _M0L2mvS483 / 5ull;
      int32_t _M0L6_2atmpS1478 = (int32_t)_M0L6_2atmpS1479;
      int32_t _M0L6_2atmpS1477 = 5 * _M0L6_2atmpS1478;
      int32_t _M0L6mvMod5S499 = _M0L6_2atmpS1476 - _M0L6_2atmpS1477;
      if (_M0L6mvMod5S499 == 0) {
        #line 400 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0Lm17vrIsTrailingZerosS490
        = _M0FPB18multipleOfPowerOf5(_M0L2mvS483, _M0L1qS491);
      } else if (_M0L4evenS482) {
        uint64_t _M0L6_2atmpS1470 = _M0L2mvS483 - 1ull;
        uint64_t _M0L6_2atmpS1471;
        uint64_t _M0L6_2atmpS1469;
        #line 406 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0L6_2atmpS1471 = _M0MPC14bool4Bool10to__uint64(_M0L7mmShiftS484);
        _M0L6_2atmpS1469 = _M0L6_2atmpS1470 - _M0L6_2atmpS1471;
        #line 405 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0Lm17vmIsTrailingZerosS489
        = _M0FPB18multipleOfPowerOf5(_M0L6_2atmpS1469, _M0L1qS491);
      } else {
        uint64_t _M0L6_2atmpS1472 = _M0Lm2vpS486;
        uint64_t _M0L6_2atmpS1475 = _M0L2mvS483 + 2ull;
        int32_t _M0L6_2atmpS1474;
        uint64_t _M0L6_2atmpS1473;
        #line 410 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0L6_2atmpS1474
        = _M0FPB18multipleOfPowerOf5(_M0L6_2atmpS1475, _M0L1qS491);
        #line 410 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0L6_2atmpS1473 = _M0MPC14bool4Bool10to__uint64(_M0L6_2atmpS1474);
        _M0Lm2vpS486 = _M0L6_2atmpS1472 - _M0L6_2atmpS1473;
      }
    }
  } else {
    int32_t _M0L6_2atmpS1504 = _M0Lm2e2S478;
    int32_t _M0L6_2atmpS1503 = -_M0L6_2atmpS1504;
    int32_t _M0L6_2atmpS1498;
    int32_t _M0L6_2atmpS1502;
    int32_t _M0L6_2atmpS1501;
    int32_t _M0L6_2atmpS1500;
    int32_t _M0L6_2atmpS1499;
    int32_t _M0L1qS500;
    int32_t _M0L6_2atmpS1491;
    int32_t _M0L6_2atmpS1497;
    int32_t _M0L6_2atmpS1496;
    int32_t _M0L1iS501;
    int32_t _M0L6_2atmpS1495;
    int32_t _M0L1kS502;
    int32_t _M0L1jS503;
    struct _M0TPB8Pow5Pair _M0L4pow5S504;
    uint64_t _M0L6_2atmpS1494;
    struct _M0TPB19MulShiftAll64Result _M0L7_2abindS505;
    uint64_t _M0L8_2avrOutS506;
    uint64_t _M0L8_2avpOutS507;
    uint64_t _M0L8_2avmOutS508;
    #line 415 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1498 = _M0FPB9log10Pow5(_M0L6_2atmpS1503);
    _M0L6_2atmpS1502 = _M0Lm2e2S478;
    _M0L6_2atmpS1501 = -_M0L6_2atmpS1502;
    _M0L6_2atmpS1500 = _M0L6_2atmpS1501 > 1;
    #line 415 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1499 = _M0MPC14bool4Bool7to__int(_M0L6_2atmpS1500);
    _M0L1qS500 = _M0L6_2atmpS1498 - _M0L6_2atmpS1499;
    _M0L6_2atmpS1491 = _M0Lm2e2S478;
    _M0Lm3e10S488 = _M0L1qS500 + _M0L6_2atmpS1491;
    _M0L6_2atmpS1497 = _M0Lm2e2S478;
    _M0L6_2atmpS1496 = -_M0L6_2atmpS1497;
    _M0L1iS501 = _M0L6_2atmpS1496 - _M0L1qS500;
    #line 418 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1495 = _M0FPB8pow5bits(_M0L1iS501);
    _M0L1kS502 = _M0L6_2atmpS1495 - 125;
    _M0L1jS503 = _M0L1qS500 - _M0L1kS502;
    #line 420 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L4pow5S504 = _M0FPB19double__computePow5(_M0L1iS501);
    _M0L6_2atmpS1494 = _M0Lm2m2S479;
    #line 421 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L7_2abindS505
    = _M0FPB13mulShiftAll64(_M0L6_2atmpS1494, _M0L4pow5S504, _M0L1jS503, _M0L7mmShiftS484);
    _M0L8_2avrOutS506 = _M0L7_2abindS505.$0;
    _M0L8_2avpOutS507 = _M0L7_2abindS505.$1;
    _M0L8_2avmOutS508 = _M0L7_2abindS505.$2;
    _M0Lm2vrS485 = _M0L8_2avrOutS506;
    _M0Lm2vpS486 = _M0L8_2avpOutS507;
    _M0Lm2vmS487 = _M0L8_2avmOutS508;
    if (_M0L1qS500 <= 1) {
      _M0Lm17vrIsTrailingZerosS490 = 1;
      if (_M0L4evenS482) {
        int32_t _M0L6_2atmpS1492;
        #line 432 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0L6_2atmpS1492 = _M0MPC14bool4Bool7to__int(_M0L7mmShiftS484);
        _M0Lm17vmIsTrailingZerosS489 = _M0L6_2atmpS1492 == 1;
      } else {
        uint64_t _M0L6_2atmpS1493 = _M0Lm2vpS486;
        _M0Lm2vpS486 = _M0L6_2atmpS1493 - 1ull;
      }
    } else if (_M0L1qS500 < 63) {
      #line 437 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0Lm17vrIsTrailingZerosS490
      = _M0FPB18multipleOfPowerOf2(_M0L2mvS483, _M0L1qS500);
    }
  }
  _M0Lm7removedS509 = 0;
  _M0Lm16lastRemovedDigitS510 = 0;
  _M0Lm6outputS511 = 0ull;
  if (_M0Lm17vmIsTrailingZerosS489 || _M0Lm17vrIsTrailingZerosS490) {
    int32_t _if__result_2325;
    uint64_t _M0L6_2atmpS1534;
    uint64_t _M0L6_2atmpS1540;
    uint64_t _M0L6_2atmpS1541;
    int32_t _if__result_2326;
    int32_t _M0L6_2atmpS1537;
    int64_t _M0L6_2atmpS1536;
    uint64_t _M0L6_2atmpS1535;
    while (1) {
      uint64_t _M0L6_2atmpS1517 = _M0Lm2vpS486;
      uint64_t _M0L7vpDiv10S512 = _M0L6_2atmpS1517 / 10ull;
      uint64_t _M0L6_2atmpS1516 = _M0Lm2vmS487;
      uint64_t _M0L7vmDiv10S513 = _M0L6_2atmpS1516 / 10ull;
      uint64_t _M0L6_2atmpS1515;
      int32_t _M0L6_2atmpS1512;
      int32_t _M0L6_2atmpS1514;
      int32_t _M0L6_2atmpS1513;
      int32_t _M0L7vmMod10S515;
      uint64_t _M0L6_2atmpS1511;
      uint64_t _M0L7vrDiv10S516;
      uint64_t _M0L6_2atmpS1510;
      int32_t _M0L6_2atmpS1507;
      int32_t _M0L6_2atmpS1509;
      int32_t _M0L6_2atmpS1508;
      int32_t _M0L7vrMod10S517;
      int32_t _M0L6_2atmpS1506;
      if (_M0L7vpDiv10S512 <= _M0L7vmDiv10S513) {
        break;
      }
      _M0L6_2atmpS1515 = _M0Lm2vmS487;
      _M0L6_2atmpS1512 = (int32_t)_M0L6_2atmpS1515;
      _M0L6_2atmpS1514 = (int32_t)_M0L7vmDiv10S513;
      _M0L6_2atmpS1513 = 10 * _M0L6_2atmpS1514;
      _M0L7vmMod10S515 = _M0L6_2atmpS1512 - _M0L6_2atmpS1513;
      _M0L6_2atmpS1511 = _M0Lm2vrS485;
      _M0L7vrDiv10S516 = _M0L6_2atmpS1511 / 10ull;
      _M0L6_2atmpS1510 = _M0Lm2vrS485;
      _M0L6_2atmpS1507 = (int32_t)_M0L6_2atmpS1510;
      _M0L6_2atmpS1509 = (int32_t)_M0L7vrDiv10S516;
      _M0L6_2atmpS1508 = 10 * _M0L6_2atmpS1509;
      _M0L7vrMod10S517 = _M0L6_2atmpS1507 - _M0L6_2atmpS1508;
      _M0Lm17vmIsTrailingZerosS489
      = _M0Lm17vmIsTrailingZerosS489 && _M0L7vmMod10S515 == 0;
      if (_M0Lm17vrIsTrailingZerosS490) {
        int32_t _M0L6_2atmpS1505 = _M0Lm16lastRemovedDigitS510;
        _M0Lm17vrIsTrailingZerosS490 = _M0L6_2atmpS1505 == 0;
      } else {
        _M0Lm17vrIsTrailingZerosS490 = 0;
      }
      _M0Lm16lastRemovedDigitS510 = _M0L7vrMod10S517;
      _M0Lm2vrS485 = _M0L7vrDiv10S516;
      _M0Lm2vpS486 = _M0L7vpDiv10S512;
      _M0Lm2vmS487 = _M0L7vmDiv10S513;
      _M0L6_2atmpS1506 = _M0Lm7removedS509;
      _M0Lm7removedS509 = _M0L6_2atmpS1506 + 1;
      continue;
      break;
    }
    if (_M0Lm17vmIsTrailingZerosS489) {
      while (1) {
        uint64_t _M0L6_2atmpS1530 = _M0Lm2vmS487;
        uint64_t _M0L7vmDiv10S518 = _M0L6_2atmpS1530 / 10ull;
        uint64_t _M0L6_2atmpS1529 = _M0Lm2vmS487;
        int32_t _M0L6_2atmpS1526 = (int32_t)_M0L6_2atmpS1529;
        int32_t _M0L6_2atmpS1528 = (int32_t)_M0L7vmDiv10S518;
        int32_t _M0L6_2atmpS1527 = 10 * _M0L6_2atmpS1528;
        int32_t _M0L7vmMod10S519 = _M0L6_2atmpS1526 - _M0L6_2atmpS1527;
        uint64_t _M0L6_2atmpS1525;
        uint64_t _M0L7vpDiv10S521;
        uint64_t _M0L6_2atmpS1524;
        uint64_t _M0L7vrDiv10S522;
        uint64_t _M0L6_2atmpS1523;
        int32_t _M0L6_2atmpS1520;
        int32_t _M0L6_2atmpS1522;
        int32_t _M0L6_2atmpS1521;
        int32_t _M0L7vrMod10S523;
        int32_t _M0L6_2atmpS1519;
        if (_M0L7vmMod10S519 != 0) {
          break;
        }
        _M0L6_2atmpS1525 = _M0Lm2vpS486;
        _M0L7vpDiv10S521 = _M0L6_2atmpS1525 / 10ull;
        _M0L6_2atmpS1524 = _M0Lm2vrS485;
        _M0L7vrDiv10S522 = _M0L6_2atmpS1524 / 10ull;
        _M0L6_2atmpS1523 = _M0Lm2vrS485;
        _M0L6_2atmpS1520 = (int32_t)_M0L6_2atmpS1523;
        _M0L6_2atmpS1522 = (int32_t)_M0L7vrDiv10S522;
        _M0L6_2atmpS1521 = 10 * _M0L6_2atmpS1522;
        _M0L7vrMod10S523 = _M0L6_2atmpS1520 - _M0L6_2atmpS1521;
        if (_M0Lm17vrIsTrailingZerosS490) {
          int32_t _M0L6_2atmpS1518 = _M0Lm16lastRemovedDigitS510;
          _M0Lm17vrIsTrailingZerosS490 = _M0L6_2atmpS1518 == 0;
        } else {
          _M0Lm17vrIsTrailingZerosS490 = 0;
        }
        _M0Lm16lastRemovedDigitS510 = _M0L7vrMod10S523;
        _M0Lm2vrS485 = _M0L7vrDiv10S522;
        _M0Lm2vpS486 = _M0L7vpDiv10S521;
        _M0Lm2vmS487 = _M0L7vmDiv10S518;
        _M0L6_2atmpS1519 = _M0Lm7removedS509;
        _M0Lm7removedS509 = _M0L6_2atmpS1519 + 1;
        continue;
        break;
      }
    }
    if (_M0Lm17vrIsTrailingZerosS490) {
      int32_t _M0L6_2atmpS1533 = _M0Lm16lastRemovedDigitS510;
      if (_M0L6_2atmpS1533 == 5) {
        uint64_t _M0L6_2atmpS1532 = _M0Lm2vrS485;
        uint64_t _M0L6_2atmpS1531 = _M0L6_2atmpS1532 % 2ull;
        _if__result_2325 = _M0L6_2atmpS1531 == 0ull;
      } else {
        _if__result_2325 = 0;
      }
    } else {
      _if__result_2325 = 0;
    }
    if (_if__result_2325) {
      _M0Lm16lastRemovedDigitS510 = 4;
    }
    _M0L6_2atmpS1534 = _M0Lm2vrS485;
    _M0L6_2atmpS1540 = _M0Lm2vrS485;
    _M0L6_2atmpS1541 = _M0Lm2vmS487;
    if (_M0L6_2atmpS1540 == _M0L6_2atmpS1541) {
      if (!_M0L4evenS482) {
        _if__result_2326 = 1;
      } else {
        int32_t _M0L6_2atmpS1539 = _M0Lm17vmIsTrailingZerosS489;
        _if__result_2326 = !_M0L6_2atmpS1539;
      }
    } else {
      _if__result_2326 = 0;
    }
    if (_if__result_2326) {
      _M0L6_2atmpS1537 = 1;
    } else {
      int32_t _M0L6_2atmpS1538 = _M0Lm16lastRemovedDigitS510;
      _M0L6_2atmpS1537 = _M0L6_2atmpS1538 >= 5;
    }
    #line 487 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1536 = _M0MPC14bool4Bool9to__int64(_M0L6_2atmpS1537);
    _M0L6_2atmpS1535 = *(uint64_t*)&_M0L6_2atmpS1536;
    _M0Lm6outputS511 = _M0L6_2atmpS1534 + _M0L6_2atmpS1535;
  } else {
    int32_t _M0Lm7roundUpS524 = 0;
    uint64_t _M0L6_2atmpS1562 = _M0Lm2vpS486;
    uint64_t _M0L8vpDiv100S525 = _M0L6_2atmpS1562 / 100ull;
    uint64_t _M0L6_2atmpS1561 = _M0Lm2vmS487;
    uint64_t _M0L8vmDiv100S526 = _M0L6_2atmpS1561 / 100ull;
    uint64_t _M0L6_2atmpS1556;
    uint64_t _M0L6_2atmpS1559;
    uint64_t _M0L6_2atmpS1560;
    int32_t _M0L6_2atmpS1558;
    uint64_t _M0L6_2atmpS1557;
    if (_M0L8vpDiv100S525 > _M0L8vmDiv100S526) {
      uint64_t _M0L6_2atmpS1547 = _M0Lm2vrS485;
      uint64_t _M0L8vrDiv100S527 = _M0L6_2atmpS1547 / 100ull;
      uint64_t _M0L6_2atmpS1546 = _M0Lm2vrS485;
      int32_t _M0L6_2atmpS1543 = (int32_t)_M0L6_2atmpS1546;
      int32_t _M0L6_2atmpS1545 = (int32_t)_M0L8vrDiv100S527;
      int32_t _M0L6_2atmpS1544 = 100 * _M0L6_2atmpS1545;
      int32_t _M0L8vrMod100S528 = _M0L6_2atmpS1543 - _M0L6_2atmpS1544;
      int32_t _M0L6_2atmpS1542;
      _M0Lm7roundUpS524 = _M0L8vrMod100S528 >= 50;
      _M0Lm2vrS485 = _M0L8vrDiv100S527;
      _M0Lm2vpS486 = _M0L8vpDiv100S525;
      _M0Lm2vmS487 = _M0L8vmDiv100S526;
      _M0L6_2atmpS1542 = _M0Lm7removedS509;
      _M0Lm7removedS509 = _M0L6_2atmpS1542 + 2;
    }
    while (1) {
      uint64_t _M0L6_2atmpS1555 = _M0Lm2vpS486;
      uint64_t _M0L7vpDiv10S529 = _M0L6_2atmpS1555 / 10ull;
      uint64_t _M0L6_2atmpS1554 = _M0Lm2vmS487;
      uint64_t _M0L7vmDiv10S530 = _M0L6_2atmpS1554 / 10ull;
      uint64_t _M0L6_2atmpS1553;
      uint64_t _M0L7vrDiv10S532;
      uint64_t _M0L6_2atmpS1552;
      int32_t _M0L6_2atmpS1549;
      int32_t _M0L6_2atmpS1551;
      int32_t _M0L6_2atmpS1550;
      int32_t _M0L7vrMod10S533;
      int32_t _M0L6_2atmpS1548;
      if (_M0L7vpDiv10S529 <= _M0L7vmDiv10S530) {
        break;
      }
      _M0L6_2atmpS1553 = _M0Lm2vrS485;
      _M0L7vrDiv10S532 = _M0L6_2atmpS1553 / 10ull;
      _M0L6_2atmpS1552 = _M0Lm2vrS485;
      _M0L6_2atmpS1549 = (int32_t)_M0L6_2atmpS1552;
      _M0L6_2atmpS1551 = (int32_t)_M0L7vrDiv10S532;
      _M0L6_2atmpS1550 = 10 * _M0L6_2atmpS1551;
      _M0L7vrMod10S533 = _M0L6_2atmpS1549 - _M0L6_2atmpS1550;
      _M0Lm7roundUpS524 = _M0L7vrMod10S533 >= 5;
      _M0Lm2vrS485 = _M0L7vrDiv10S532;
      _M0Lm2vpS486 = _M0L7vpDiv10S529;
      _M0Lm2vmS487 = _M0L7vmDiv10S530;
      _M0L6_2atmpS1548 = _M0Lm7removedS509;
      _M0Lm7removedS509 = _M0L6_2atmpS1548 + 1;
      continue;
      break;
    }
    _M0L6_2atmpS1556 = _M0Lm2vrS485;
    _M0L6_2atmpS1559 = _M0Lm2vrS485;
    _M0L6_2atmpS1560 = _M0Lm2vmS487;
    _M0L6_2atmpS1558
    = _M0L6_2atmpS1559 == _M0L6_2atmpS1560 || _M0Lm7roundUpS524;
    #line 522 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1557 = _M0MPC14bool4Bool10to__uint64(_M0L6_2atmpS1558);
    _M0Lm6outputS511 = _M0L6_2atmpS1556 + _M0L6_2atmpS1557;
  }
  _M0L6_2atmpS1564 = _M0Lm3e10S488;
  _M0L6_2atmpS1565 = _M0Lm7removedS509;
  _M0L3expS534 = _M0L6_2atmpS1564 + _M0L6_2atmpS1565;
  _M0L6_2atmpS1563 = _M0Lm6outputS511;
  _block_2328
  = (struct _M0TPB17FloatingDecimal64*)moonbit_malloc(sizeof(struct _M0TPB17FloatingDecimal64));
  Moonbit_object_header(_block_2328)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_2328->$0 = _M0L6_2atmpS1563;
  _block_2328->$1 = _M0L3expS534;
  return _block_2328;
}

uint64_t _M0MPC14bool4Bool10to__uint64(int32_t _M0L4selfS477) {
  #line 110 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bool.mbt"
  if (_M0L4selfS477) {
    return 1ull;
  } else {
    return 0ull;
  }
}

int64_t _M0MPC14bool4Bool9to__int64(int32_t _M0L4selfS476) {
  #line 58 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bool.mbt"
  if (_M0L4selfS476) {
    return 1ll;
  } else {
    return 0ll;
  }
}

int32_t _M0MPC14bool4Bool7to__int(int32_t _M0L4selfS475) {
  #line 32 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bool.mbt"
  if (_M0L4selfS475) {
    return 1;
  } else {
    return 0;
  }
}

int32_t _M0FPB17decimal__length17(uint64_t _M0L1vS474) {
  #line 280 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  if (_M0L1vS474 >= 10000000000000000ull) {
    return 17;
  }
  if (_M0L1vS474 >= 1000000000000000ull) {
    return 16;
  }
  if (_M0L1vS474 >= 100000000000000ull) {
    return 15;
  }
  if (_M0L1vS474 >= 10000000000000ull) {
    return 14;
  }
  if (_M0L1vS474 >= 1000000000000ull) {
    return 13;
  }
  if (_M0L1vS474 >= 100000000000ull) {
    return 12;
  }
  if (_M0L1vS474 >= 10000000000ull) {
    return 11;
  }
  if (_M0L1vS474 >= 1000000000ull) {
    return 10;
  }
  if (_M0L1vS474 >= 100000000ull) {
    return 9;
  }
  if (_M0L1vS474 >= 10000000ull) {
    return 8;
  }
  if (_M0L1vS474 >= 1000000ull) {
    return 7;
  }
  if (_M0L1vS474 >= 100000ull) {
    return 6;
  }
  if (_M0L1vS474 >= 10000ull) {
    return 5;
  }
  if (_M0L1vS474 >= 1000ull) {
    return 4;
  }
  if (_M0L1vS474 >= 100ull) {
    return 3;
  }
  if (_M0L1vS474 >= 10ull) {
    return 2;
  }
  return 1;
}

struct _M0TPB8Pow5Pair _M0FPB22double__computeInvPow5(int32_t _M0L1iS457) {
  int32_t _M0L6_2atmpS1464;
  int32_t _M0L6_2atmpS1463;
  int32_t _M0L4baseS456;
  int32_t _M0L5base2S458;
  int32_t _M0L6offsetS459;
  int32_t _M0L6_2atmpS1462;
  uint64_t _M0L4mul0S460;
  int32_t _M0L6_2atmpS1461;
  int32_t _M0L6_2atmpS1460;
  uint64_t _M0L4mul1S461;
  uint64_t _M0L1mS462;
  struct _M0TPB7Umul128 _M0L7_2abindS463;
  uint64_t _M0L7_2alow1S464;
  uint64_t _M0L8_2ahigh1S465;
  struct _M0TPB7Umul128 _M0L7_2abindS466;
  uint64_t _M0L7_2alow0S467;
  uint64_t _M0L8_2ahigh0S468;
  uint64_t _M0L3sumS469;
  uint64_t _M0Lm5high1S470;
  int32_t _M0L6_2atmpS1458;
  int32_t _M0L6_2atmpS1459;
  int32_t _M0L5deltaS471;
  uint64_t _M0L6_2atmpS1457;
  uint64_t _M0L6_2atmpS1449;
  int32_t _M0L6_2atmpS1456;
  uint32_t _M0L6_2atmpS1453;
  int32_t _M0L6_2atmpS1455;
  int32_t _M0L6_2atmpS1454;
  uint32_t _M0L6_2atmpS1452;
  uint32_t _M0L6_2atmpS1451;
  uint64_t _M0L6_2atmpS1450;
  uint64_t _M0L1aS472;
  uint64_t _M0L6_2atmpS1448;
  uint64_t _M0L1bS473;
  #line 239 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1464 = _M0L1iS457 + 26;
  _M0L6_2atmpS1463 = _M0L6_2atmpS1464 - 1;
  _M0L4baseS456 = _M0L6_2atmpS1463 / 26;
  _M0L5base2S458 = _M0L4baseS456 * 26;
  _M0L6offsetS459 = _M0L5base2S458 - _M0L1iS457;
  _M0L6_2atmpS1462 = _M0L4baseS456 * 2;
  #line 243 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4mul0S460
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB26gDOUBLE__POW5__INV__SPLIT2, _M0L6_2atmpS1462);
  _M0L6_2atmpS1461 = _M0L4baseS456 * 2;
  _M0L6_2atmpS1460 = _M0L6_2atmpS1461 + 1;
  #line 244 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4mul1S461
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB26gDOUBLE__POW5__INV__SPLIT2, _M0L6_2atmpS1460);
  if (_M0L6offsetS459 == 0) {
    return (struct _M0TPB8Pow5Pair){.$0 = _M0L4mul0S460, .$1 = _M0L4mul1S461};
  }
  #line 248 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L1mS462
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB20gDOUBLE__POW5__TABLE, _M0L6offsetS459);
  #line 249 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS463 = _M0FPB7umul128(_M0L1mS462, _M0L4mul1S461);
  _M0L7_2alow1S464 = _M0L7_2abindS463.$0;
  _M0L8_2ahigh1S465 = _M0L7_2abindS463.$1;
  #line 250 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS466 = _M0FPB7umul128(_M0L1mS462, _M0L4mul0S460);
  _M0L7_2alow0S467 = _M0L7_2abindS466.$0;
  _M0L8_2ahigh0S468 = _M0L7_2abindS466.$1;
  _M0L3sumS469 = _M0L8_2ahigh0S468 + _M0L7_2alow1S464;
  _M0Lm5high1S470 = _M0L8_2ahigh1S465;
  if (_M0L3sumS469 < _M0L8_2ahigh0S468) {
    uint64_t _M0L6_2atmpS1447 = _M0Lm5high1S470;
    _M0Lm5high1S470 = _M0L6_2atmpS1447 + 1ull;
  }
  #line 256 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1458 = _M0FPB8pow5bits(_M0L5base2S458);
  #line 256 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1459 = _M0FPB8pow5bits(_M0L1iS457);
  _M0L5deltaS471 = _M0L6_2atmpS1458 - _M0L6_2atmpS1459;
  #line 257 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1457
  = _M0FPB13shiftright128(_M0L7_2alow0S467, _M0L3sumS469, _M0L5deltaS471);
  _M0L6_2atmpS1449 = _M0L6_2atmpS1457 + 1ull;
  _M0L6_2atmpS1456 = _M0L1iS457 / 16;
  #line 259 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1453
  = _M0MPC15array13ReadOnlyArray2atGjE(_M0FPB19gPOW5__INV__OFFSETS, _M0L6_2atmpS1456);
  _M0L6_2atmpS1455 = _M0L1iS457 % 16;
  _M0L6_2atmpS1454 = _M0L6_2atmpS1455 << 1;
  _M0L6_2atmpS1452 = _M0L6_2atmpS1453 >> (_M0L6_2atmpS1454 & 31);
  _M0L6_2atmpS1451 = _M0L6_2atmpS1452 & 3u;
  #line 259 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1450 = _M0MPC14uint4UInt10to__uint64(_M0L6_2atmpS1451);
  _M0L1aS472 = _M0L6_2atmpS1449 + _M0L6_2atmpS1450;
  _M0L6_2atmpS1448 = _M0Lm5high1S470;
  #line 260 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L1bS473
  = _M0FPB13shiftright128(_M0L3sumS469, _M0L6_2atmpS1448, _M0L5deltaS471);
  return (struct _M0TPB8Pow5Pair){.$0 = _M0L1aS472, .$1 = _M0L1bS473};
}

struct _M0TPB8Pow5Pair _M0FPB19double__computePow5(int32_t _M0L1iS439) {
  int32_t _M0L4baseS438;
  int32_t _M0L5base2S440;
  int32_t _M0L6offsetS441;
  int32_t _M0L6_2atmpS1446;
  uint64_t _M0L4mul0S442;
  int32_t _M0L6_2atmpS1445;
  int32_t _M0L6_2atmpS1444;
  uint64_t _M0L4mul1S443;
  uint64_t _M0L1mS444;
  struct _M0TPB7Umul128 _M0L7_2abindS445;
  uint64_t _M0L7_2alow1S446;
  uint64_t _M0L8_2ahigh1S447;
  struct _M0TPB7Umul128 _M0L7_2abindS448;
  uint64_t _M0L7_2alow0S449;
  uint64_t _M0L8_2ahigh0S450;
  uint64_t _M0L3sumS451;
  uint64_t _M0Lm5high1S452;
  int32_t _M0L6_2atmpS1442;
  int32_t _M0L6_2atmpS1443;
  int32_t _M0L5deltaS453;
  uint64_t _M0L6_2atmpS1434;
  int32_t _M0L6_2atmpS1441;
  uint32_t _M0L6_2atmpS1438;
  int32_t _M0L6_2atmpS1440;
  int32_t _M0L6_2atmpS1439;
  uint32_t _M0L6_2atmpS1437;
  uint32_t _M0L6_2atmpS1436;
  uint64_t _M0L6_2atmpS1435;
  uint64_t _M0L1aS454;
  uint64_t _M0L6_2atmpS1433;
  uint64_t _M0L1bS455;
  #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4baseS438 = _M0L1iS439 / 26;
  _M0L5base2S440 = _M0L4baseS438 * 26;
  _M0L6offsetS441 = _M0L1iS439 - _M0L5base2S440;
  _M0L6_2atmpS1446 = _M0L4baseS438 * 2;
  #line 217 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4mul0S442
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB21gDOUBLE__POW5__SPLIT2, _M0L6_2atmpS1446);
  _M0L6_2atmpS1445 = _M0L4baseS438 * 2;
  _M0L6_2atmpS1444 = _M0L6_2atmpS1445 + 1;
  #line 218 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4mul1S443
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB21gDOUBLE__POW5__SPLIT2, _M0L6_2atmpS1444);
  if (_M0L6offsetS441 == 0) {
    return (struct _M0TPB8Pow5Pair){.$0 = _M0L4mul0S442, .$1 = _M0L4mul1S443};
  }
  #line 222 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L1mS444
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB20gDOUBLE__POW5__TABLE, _M0L6offsetS441);
  #line 223 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS445 = _M0FPB7umul128(_M0L1mS444, _M0L4mul1S443);
  _M0L7_2alow1S446 = _M0L7_2abindS445.$0;
  _M0L8_2ahigh1S447 = _M0L7_2abindS445.$1;
  #line 224 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS448 = _M0FPB7umul128(_M0L1mS444, _M0L4mul0S442);
  _M0L7_2alow0S449 = _M0L7_2abindS448.$0;
  _M0L8_2ahigh0S450 = _M0L7_2abindS448.$1;
  _M0L3sumS451 = _M0L8_2ahigh0S450 + _M0L7_2alow1S446;
  _M0Lm5high1S452 = _M0L8_2ahigh1S447;
  if (_M0L3sumS451 < _M0L8_2ahigh0S450) {
    uint64_t _M0L6_2atmpS1432 = _M0Lm5high1S452;
    _M0Lm5high1S452 = _M0L6_2atmpS1432 + 1ull;
  }
  #line 230 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1442 = _M0FPB8pow5bits(_M0L1iS439);
  #line 230 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1443 = _M0FPB8pow5bits(_M0L5base2S440);
  _M0L5deltaS453 = _M0L6_2atmpS1442 - _M0L6_2atmpS1443;
  #line 231 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1434
  = _M0FPB13shiftright128(_M0L7_2alow0S449, _M0L3sumS451, _M0L5deltaS453);
  _M0L6_2atmpS1441 = _M0L1iS439 / 16;
  #line 232 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1438
  = _M0MPC15array13ReadOnlyArray2atGjE(_M0FPB14gPOW5__OFFSETS, _M0L6_2atmpS1441);
  _M0L6_2atmpS1440 = _M0L1iS439 % 16;
  _M0L6_2atmpS1439 = _M0L6_2atmpS1440 << 1;
  _M0L6_2atmpS1437 = _M0L6_2atmpS1438 >> (_M0L6_2atmpS1439 & 31);
  _M0L6_2atmpS1436 = _M0L6_2atmpS1437 & 3u;
  #line 232 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1435 = _M0MPC14uint4UInt10to__uint64(_M0L6_2atmpS1436);
  _M0L1aS454 = _M0L6_2atmpS1434 + _M0L6_2atmpS1435;
  _M0L6_2atmpS1433 = _M0Lm5high1S452;
  #line 233 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L1bS455
  = _M0FPB13shiftright128(_M0L3sumS451, _M0L6_2atmpS1433, _M0L5deltaS453);
  return (struct _M0TPB8Pow5Pair){.$0 = _M0L1aS454, .$1 = _M0L1bS455};
}

struct _M0TPB19MulShiftAll64Result _M0FPB13mulShiftAll64(
  uint64_t _M0L1mS412,
  struct _M0TPB8Pow5Pair _M0L3mulS409,
  int32_t _M0L1jS425,
  int32_t _M0L7mmShiftS427
) {
  uint64_t _M0L7_2amul0S408;
  uint64_t _M0L7_2amul1S410;
  uint64_t _M0L1mS411;
  struct _M0TPB7Umul128 _M0L7_2abindS413;
  uint64_t _M0L5_2aloS414;
  uint64_t _M0L6_2atmpS415;
  struct _M0TPB7Umul128 _M0L7_2abindS416;
  uint64_t _M0L6_2alo2S417;
  uint64_t _M0L6_2ahi2S418;
  uint64_t _M0L3midS419;
  uint64_t _M0L6_2atmpS1431;
  uint64_t _M0L2hiS420;
  uint64_t _M0L3lo2S421;
  uint64_t _M0L6_2atmpS1429;
  uint64_t _M0L6_2atmpS1430;
  uint64_t _M0L4mid2S422;
  uint64_t _M0L6_2atmpS1428;
  uint64_t _M0L3hi2S423;
  int32_t _M0L6_2atmpS1427;
  int32_t _M0L6_2atmpS1426;
  uint64_t _M0L2vpS424;
  uint64_t _M0Lm2vmS426;
  int32_t _M0L6_2atmpS1425;
  int32_t _M0L6_2atmpS1424;
  uint64_t _M0L2vrS437;
  uint64_t _M0L6_2atmpS1423;
  #line 129 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2amul0S408 = _M0L3mulS409.$0;
  _M0L7_2amul1S410 = _M0L3mulS409.$1;
  _M0L1mS411 = _M0L1mS412 << 1;
  #line 137 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS413 = _M0FPB7umul128(_M0L1mS411, _M0L7_2amul0S408);
  _M0L5_2aloS414 = _M0L7_2abindS413.$0;
  _M0L6_2atmpS415 = _M0L7_2abindS413.$1;
  #line 138 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS416 = _M0FPB7umul128(_M0L1mS411, _M0L7_2amul1S410);
  _M0L6_2alo2S417 = _M0L7_2abindS416.$0;
  _M0L6_2ahi2S418 = _M0L7_2abindS416.$1;
  _M0L3midS419 = _M0L6_2atmpS415 + _M0L6_2alo2S417;
  if (_M0L3midS419 < _M0L6_2atmpS415) {
    _M0L6_2atmpS1431 = 1ull;
  } else {
    _M0L6_2atmpS1431 = 0ull;
  }
  _M0L2hiS420 = _M0L6_2ahi2S418 + _M0L6_2atmpS1431;
  _M0L3lo2S421 = _M0L5_2aloS414 + _M0L7_2amul0S408;
  _M0L6_2atmpS1429 = _M0L3midS419 + _M0L7_2amul1S410;
  if (_M0L3lo2S421 < _M0L5_2aloS414) {
    _M0L6_2atmpS1430 = 1ull;
  } else {
    _M0L6_2atmpS1430 = 0ull;
  }
  _M0L4mid2S422 = _M0L6_2atmpS1429 + _M0L6_2atmpS1430;
  if (_M0L4mid2S422 < _M0L3midS419) {
    _M0L6_2atmpS1428 = 1ull;
  } else {
    _M0L6_2atmpS1428 = 0ull;
  }
  _M0L3hi2S423 = _M0L2hiS420 + _M0L6_2atmpS1428;
  _M0L6_2atmpS1427 = _M0L1jS425 - 64;
  _M0L6_2atmpS1426 = _M0L6_2atmpS1427 - 1;
  #line 144 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L2vpS424
  = _M0FPB13shiftright128(_M0L4mid2S422, _M0L3hi2S423, _M0L6_2atmpS1426);
  _M0Lm2vmS426 = 0ull;
  if (_M0L7mmShiftS427) {
    uint64_t _M0L3lo3S428 = _M0L5_2aloS414 - _M0L7_2amul0S408;
    uint64_t _M0L6_2atmpS1413 = _M0L3midS419 - _M0L7_2amul1S410;
    uint64_t _M0L6_2atmpS1414;
    uint64_t _M0L4mid3S429;
    uint64_t _M0L6_2atmpS1412;
    uint64_t _M0L3hi3S430;
    int32_t _M0L6_2atmpS1411;
    int32_t _M0L6_2atmpS1410;
    if (_M0L5_2aloS414 < _M0L3lo3S428) {
      _M0L6_2atmpS1414 = 1ull;
    } else {
      _M0L6_2atmpS1414 = 0ull;
    }
    _M0L4mid3S429 = _M0L6_2atmpS1413 - _M0L6_2atmpS1414;
    if (_M0L3midS419 < _M0L4mid3S429) {
      _M0L6_2atmpS1412 = 1ull;
    } else {
      _M0L6_2atmpS1412 = 0ull;
    }
    _M0L3hi3S430 = _M0L2hiS420 - _M0L6_2atmpS1412;
    _M0L6_2atmpS1411 = _M0L1jS425 - 64;
    _M0L6_2atmpS1410 = _M0L6_2atmpS1411 - 1;
    #line 150 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0Lm2vmS426
    = _M0FPB13shiftright128(_M0L4mid3S429, _M0L3hi3S430, _M0L6_2atmpS1410);
  } else {
    uint64_t _M0L3lo3S431 = _M0L5_2aloS414 + _M0L5_2aloS414;
    uint64_t _M0L6_2atmpS1421 = _M0L3midS419 + _M0L3midS419;
    uint64_t _M0L6_2atmpS1422;
    uint64_t _M0L4mid3S432;
    uint64_t _M0L6_2atmpS1419;
    uint64_t _M0L6_2atmpS1420;
    uint64_t _M0L3hi3S433;
    uint64_t _M0L3lo4S434;
    uint64_t _M0L6_2atmpS1417;
    uint64_t _M0L6_2atmpS1418;
    uint64_t _M0L4mid4S435;
    uint64_t _M0L6_2atmpS1416;
    uint64_t _M0L3hi4S436;
    int32_t _M0L6_2atmpS1415;
    if (_M0L3lo3S431 < _M0L5_2aloS414) {
      _M0L6_2atmpS1422 = 1ull;
    } else {
      _M0L6_2atmpS1422 = 0ull;
    }
    _M0L4mid3S432 = _M0L6_2atmpS1421 + _M0L6_2atmpS1422;
    _M0L6_2atmpS1419 = _M0L2hiS420 + _M0L2hiS420;
    if (_M0L4mid3S432 < _M0L3midS419) {
      _M0L6_2atmpS1420 = 1ull;
    } else {
      _M0L6_2atmpS1420 = 0ull;
    }
    _M0L3hi3S433 = _M0L6_2atmpS1419 + _M0L6_2atmpS1420;
    _M0L3lo4S434 = _M0L3lo3S431 - _M0L7_2amul0S408;
    _M0L6_2atmpS1417 = _M0L4mid3S432 - _M0L7_2amul1S410;
    if (_M0L3lo3S431 < _M0L3lo4S434) {
      _M0L6_2atmpS1418 = 1ull;
    } else {
      _M0L6_2atmpS1418 = 0ull;
    }
    _M0L4mid4S435 = _M0L6_2atmpS1417 - _M0L6_2atmpS1418;
    if (_M0L4mid3S432 < _M0L4mid4S435) {
      _M0L6_2atmpS1416 = 1ull;
    } else {
      _M0L6_2atmpS1416 = 0ull;
    }
    _M0L3hi4S436 = _M0L3hi3S433 - _M0L6_2atmpS1416;
    _M0L6_2atmpS1415 = _M0L1jS425 - 64;
    #line 158 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0Lm2vmS426
    = _M0FPB13shiftright128(_M0L4mid4S435, _M0L3hi4S436, _M0L6_2atmpS1415);
  }
  _M0L6_2atmpS1425 = _M0L1jS425 - 64;
  _M0L6_2atmpS1424 = _M0L6_2atmpS1425 - 1;
  #line 160 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L2vrS437
  = _M0FPB13shiftright128(_M0L3midS419, _M0L2hiS420, _M0L6_2atmpS1424);
  _M0L6_2atmpS1423 = _M0Lm2vmS426;
  return (struct _M0TPB19MulShiftAll64Result){.$0 = _M0L2vrS437,
                                                .$1 = _M0L2vpS424,
                                                .$2 = _M0L6_2atmpS1423};
}

int32_t _M0FPB18multipleOfPowerOf2(
  uint64_t _M0L5valueS406,
  int32_t _M0L1pS407
) {
  uint64_t _M0L6_2atmpS1409;
  uint64_t _M0L6_2atmpS1408;
  uint64_t _M0L6_2atmpS1407;
  #line 124 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1409 = 1ull << (_M0L1pS407 & 63);
  _M0L6_2atmpS1408 = _M0L6_2atmpS1409 - 1ull;
  _M0L6_2atmpS1407 = _M0L5valueS406 & _M0L6_2atmpS1408;
  return _M0L6_2atmpS1407 == 0ull;
}

int32_t _M0FPB18multipleOfPowerOf5(
  uint64_t _M0L5valueS404,
  int32_t _M0L1pS405
) {
  int32_t _M0L6_2atmpS1406;
  #line 119 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  #line 120 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1406 = _M0FPB10pow5Factor(_M0L5valueS404);
  return _M0L6_2atmpS1406 >= _M0L1pS405;
}

int32_t _M0FPB10pow5Factor(uint64_t _M0L5valueS399) {
  uint64_t _M0L6_2atmpS1397;
  uint64_t _M0L6_2atmpS1398;
  uint64_t _M0L6_2atmpS1399;
  uint64_t _M0L6_2atmpS1400;
  uint64_t _M0L6_2atmpS1405;
  int32_t _M0L5countS400;
  uint64_t _M0L1vS401;
  #line 94 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1397 = _M0L5valueS399 % 5ull;
  if (_M0L6_2atmpS1397 != 0ull) {
    return 0;
  }
  _M0L6_2atmpS1398 = _M0L5valueS399 % 25ull;
  if (_M0L6_2atmpS1398 != 0ull) {
    return 1;
  }
  _M0L6_2atmpS1399 = _M0L5valueS399 % 125ull;
  if (_M0L6_2atmpS1399 != 0ull) {
    return 2;
  }
  _M0L6_2atmpS1400 = _M0L5valueS399 % 625ull;
  if (_M0L6_2atmpS1400 != 0ull) {
    return 3;
  }
  _M0L6_2atmpS1405 = _M0L5valueS399 / 625ull;
  _M0L5countS400 = 4;
  _M0L1vS401 = _M0L6_2atmpS1405;
  while (1) {
    if (_M0L1vS401 > 0ull) {
      uint64_t _M0L6_2atmpS1401 = _M0L1vS401 % 5ull;
      int32_t _M0L6_2atmpS1402;
      uint64_t _M0L6_2atmpS1403;
      if (_M0L6_2atmpS1401 != 0ull) {
        return _M0L5countS400;
      }
      _M0L6_2atmpS1402 = _M0L5countS400 + 1;
      _M0L6_2atmpS1403 = _M0L1vS401 / 5ull;
      _M0L5countS400 = _M0L6_2atmpS1402;
      _M0L1vS401 = _M0L6_2atmpS1403;
      continue;
    } else {
      struct _M0TPB13StringBuilder* _M0L18_2astring__builderS403;
      moonbit_string_t _M0L6_2atmpS1404;
      int32_t _result_2330;
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0L18_2astring__builderS403
      = _M0MPB13StringBuilder21StringBuilder_2einner(25);
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS403, (moonbit_string_t)moonbit_string_literal_5.data);
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0MPB13StringBuilder13write__objectGmE(_M0L18_2astring__builderS403, _M0L5valueS399);
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0L6_2atmpS1404
      = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS403);
      moonbit_decref(_M0L18_2astring__builderS403);
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _result_2330 = _M0FPC15abort5abortGiE(_M0L6_2atmpS1404);
      moonbit_decref(_M0L6_2atmpS1404);
      return _result_2330;
    }
    break;
  }
}

uint64_t _M0FPB13shiftright128(
  uint64_t _M0L2loS398,
  uint64_t _M0L2hiS396,
  int32_t _M0L4distS397
) {
  int32_t _M0L6_2atmpS1396;
  uint64_t _M0L6_2atmpS1394;
  uint64_t _M0L6_2atmpS1395;
  #line 89 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1396 = 64 - _M0L4distS397;
  _M0L6_2atmpS1394 = _M0L2hiS396 << (_M0L6_2atmpS1396 & 63);
  _M0L6_2atmpS1395 = _M0L2loS398 >> (_M0L4distS397 & 63);
  return _M0L6_2atmpS1394 | _M0L6_2atmpS1395;
}

struct _M0TPB7Umul128 _M0FPB7umul128(
  uint64_t _M0L1aS386,
  uint64_t _M0L1bS389
) {
  uint64_t _M0L3aLoS385;
  uint64_t _M0L3aHiS387;
  uint64_t _M0L3bLoS388;
  uint64_t _M0L3bHiS390;
  uint64_t _M0L1xS391;
  uint64_t _M0L6_2atmpS1392;
  uint64_t _M0L6_2atmpS1393;
  uint64_t _M0L1yS392;
  uint64_t _M0L6_2atmpS1390;
  uint64_t _M0L6_2atmpS1391;
  uint64_t _M0L1zS393;
  uint64_t _M0L6_2atmpS1388;
  uint64_t _M0L6_2atmpS1389;
  uint64_t _M0L6_2atmpS1386;
  uint64_t _M0L6_2atmpS1387;
  uint64_t _M0L1wS394;
  uint64_t _M0L2loS395;
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L3aLoS385 = _M0L1aS386 & 4294967295ull;
  _M0L3aHiS387 = _M0L1aS386 >> 32;
  _M0L3bLoS388 = _M0L1bS389 & 4294967295ull;
  _M0L3bHiS390 = _M0L1bS389 >> 32;
  _M0L1xS391 = _M0L3aLoS385 * _M0L3bLoS388;
  _M0L6_2atmpS1392 = _M0L3aHiS387 * _M0L3bLoS388;
  _M0L6_2atmpS1393 = _M0L1xS391 >> 32;
  _M0L1yS392 = _M0L6_2atmpS1392 + _M0L6_2atmpS1393;
  _M0L6_2atmpS1390 = _M0L3aLoS385 * _M0L3bHiS390;
  _M0L6_2atmpS1391 = _M0L1yS392 & 4294967295ull;
  _M0L1zS393 = _M0L6_2atmpS1390 + _M0L6_2atmpS1391;
  _M0L6_2atmpS1388 = _M0L3aHiS387 * _M0L3bHiS390;
  _M0L6_2atmpS1389 = _M0L1yS392 >> 32;
  _M0L6_2atmpS1386 = _M0L6_2atmpS1388 + _M0L6_2atmpS1389;
  _M0L6_2atmpS1387 = _M0L1zS393 >> 32;
  _M0L1wS394 = _M0L6_2atmpS1386 + _M0L6_2atmpS1387;
  _M0L2loS395 = _M0L1aS386 * _M0L1bS389;
  return (struct _M0TPB7Umul128){.$0 = _M0L2loS395, .$1 = _M0L1wS394};
}

moonbit_string_t _M0FPB19string__from__bytes(
  moonbit_bytes_t _M0L5bytesS383,
  int32_t _M0L4fromS380,
  int32_t _M0L2toS379
) {
  int32_t _M0L3lenS378;
  int32_t _M0L6_2atmpS1385;
  uint16_t* _M0L6bufferS381;
  int32_t _M0L1iS382;
  #line 52 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L3lenS378 = _M0L2toS379 - _M0L4fromS380;
  #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1385 = _M0IPC16uint166UInt16PB7Default7default();
  _M0L6bufferS381
  = (uint16_t*)moonbit_make_string(_M0L3lenS378, _M0L6_2atmpS1385);
  _M0L1iS382 = 0;
  while (1) {
    if (_M0L1iS382 < _M0L3lenS378) {
      int32_t _M0L6_2atmpS1383 = _M0L4fromS380 + _M0L1iS382;
      int32_t _M0L6_2atmpS1382;
      int32_t _M0L6_2atmpS1381;
      int32_t _M0L6_2atmpS1384;
      if (
        _M0L6_2atmpS1383 < 0
        || _M0L6_2atmpS1383 >= Moonbit_array_length(_M0L5bytesS383)
      ) {
        #line 56 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6_2atmpS1382 = (int32_t)_M0L5bytesS383[_M0L6_2atmpS1383];
      _M0L6_2atmpS1381 = (uint16_t)_M0L6_2atmpS1382;
      if (
        _M0L1iS382 < 0 || _M0L1iS382 >= Moonbit_array_length(_M0L6bufferS381)
      ) {
        #line 56 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6bufferS381[_M0L1iS382] = _M0L6_2atmpS1381;
      _M0L6_2atmpS1384 = _M0L1iS382 + 1;
      _M0L1iS382 = _M0L6_2atmpS1384;
      continue;
    }
    break;
  }
  return _M0L6bufferS381;
}

int32_t _M0FPB9log10Pow2(int32_t _M0L1eS377) {
  int32_t _M0L6_2atmpS1380;
  uint32_t _M0L6_2atmpS1379;
  uint32_t _M0L6_2atmpS1378;
  #line 44 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1380 = _M0L1eS377 * 78913;
  _M0L6_2atmpS1379 = *(uint32_t*)&_M0L6_2atmpS1380;
  _M0L6_2atmpS1378 = _M0L6_2atmpS1379 >> 18;
  return *(int32_t*)&_M0L6_2atmpS1378;
}

int32_t _M0FPB9log10Pow5(int32_t _M0L1eS376) {
  int32_t _M0L6_2atmpS1377;
  uint32_t _M0L6_2atmpS1376;
  uint32_t _M0L6_2atmpS1375;
  #line 37 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1377 = _M0L1eS376 * 732923;
  _M0L6_2atmpS1376 = *(uint32_t*)&_M0L6_2atmpS1377;
  _M0L6_2atmpS1375 = _M0L6_2atmpS1376 >> 20;
  return *(int32_t*)&_M0L6_2atmpS1375;
}

moonbit_string_t _M0FPB18copy__special__str(
  int32_t _M0L4signS374,
  int32_t _M0L8exponentS375,
  int32_t _M0L8mantissaS372
) {
  moonbit_string_t _M0L1sS373;
  moonbit_string_t _result_2333;
  #line 23 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  if (_M0L8mantissaS372) {
    return (moonbit_string_t)moonbit_string_literal_6.data;
  }
  if (_M0L4signS374) {
    _M0L1sS373 = (moonbit_string_t)moonbit_string_literal_7.data;
  } else {
    _M0L1sS373 = (moonbit_string_t)moonbit_string_literal_0.data;
  }
  if (_M0L8exponentS375) {
    moonbit_string_t _result_2332;
    #line 29 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _result_2332
    = moonbit_add_string(_M0L1sS373, (moonbit_string_t)moonbit_string_literal_8.data);
    moonbit_decref(_M0L1sS373);
    return _result_2332;
  }
  #line 31 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _result_2333
  = moonbit_add_string(_M0L1sS373, (moonbit_string_t)moonbit_string_literal_9.data);
  moonbit_decref(_M0L1sS373);
  return _result_2333;
}

int32_t _M0FPB8pow5bits(int32_t _M0L1eS371) {
  int32_t _M0L6_2atmpS1374;
  uint32_t _M0L6_2atmpS1373;
  uint32_t _M0L6_2atmpS1372;
  int32_t _M0L6_2atmpS1371;
  #line 18 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1374 = _M0L1eS371 * 1217359;
  _M0L6_2atmpS1373 = *(uint32_t*)&_M0L6_2atmpS1374;
  _M0L6_2atmpS1372 = _M0L6_2atmpS1373 >> 19;
  _M0L6_2atmpS1371 = *(int32_t*)&_M0L6_2atmpS1372;
  return _M0L6_2atmpS1371 + 1;
}

int32_t _M0MPC16double6Double7to__int(double _M0L4selfS370) {
  #line 43 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_to_int.mbt"
  if (_M0L4selfS370 != _M0L4selfS370) {
    return 0;
  } else if (_M0L4selfS370 >= 0x1.fffffffcp+30) {
    return 2147483647;
  } else if (_M0L4selfS370 <= -0x1p+31) {
    return (int32_t)0x80000000;
  } else {
    return (int32_t)_M0L4selfS370;
  }
}

int64_t _M0MPC16double6Double9to__int64(double _M0L4selfS369) {
  #line 46 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_to_int64_native.mbt"
  if (_M0L4selfS369 != _M0L4selfS369) {
    return 0ll;
  } else if (_M0L4selfS369 >= 0x1p+63) {
    return 9223372036854775807ll;
  } else if (_M0L4selfS369 <= -0x1p+63) {
    return (int64_t)0x8000000000000000ll;
  } else {
    return (int64_t)_M0L4selfS369;
  }
}

struct _M0TPB5ArrayGfE* _M0MPC15array5Array20unsafe__make__uninitGfE(
  int32_t _M0L3lenS365
) {
  float* _M0L6_2atmpS1367;
  struct _M0TPB5ArrayGfE* _block_2334;
  #line 30 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1367 = (float*)moonbit_make_float_array_raw(_M0L3lenS365);
  _block_2334
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_block_2334)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 0, 0);
  _block_2334->$0 = _M0L6_2atmpS1367;
  _block_2334->$1 = _M0L3lenS365;
  return _block_2334;
}

struct _M0TPB5ArrayGbE* _M0MPC15array5Array20unsafe__make__uninitGbE(
  int32_t _M0L3lenS366
) {
  uint8_t* _M0L6_2atmpS1368;
  struct _M0TPB5ArrayGbE* _block_2335;
  #line 30 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1368 = (uint8_t*)moonbit_make_bytes_raw(_M0L3lenS366);
  _block_2335
  = (struct _M0TPB5ArrayGbE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGbE));
  Moonbit_object_header(_block_2335)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 51, 0);
  _block_2335->$0 = _M0L6_2atmpS1368;
  _block_2335->$1 = _M0L3lenS366;
  return _block_2335;
}

struct _M0TPB5ArrayGiE* _M0MPC15array5Array20unsafe__make__uninitGiE(
  int32_t _M0L3lenS367
) {
  int32_t* _M0L6_2atmpS1369;
  struct _M0TPB5ArrayGiE* _block_2336;
  #line 30 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1369 = (int32_t*)moonbit_make_int32_array_raw(_M0L3lenS367);
  _block_2336
  = (struct _M0TPB5ArrayGiE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGiE));
  Moonbit_object_header(_block_2336)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 3, 0);
  _block_2336->$0 = _M0L6_2atmpS1369;
  _block_2336->$1 = _M0L3lenS367;
  return _block_2336;
}

struct _M0TPB5ArrayGRPB5ArrayGfEE* _M0MPC15array5Array20unsafe__make__uninitGRPB5ArrayGfEE(
  int32_t _M0L3lenS368
) {
  struct _M0TPB5ArrayGfE** _M0L6_2atmpS1370;
  struct _M0TPB5ArrayGRPB5ArrayGfEE* _block_2337;
  #line 30 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1370
  = (struct _M0TPB5ArrayGfE**)moonbit_make_ref_array(_M0L3lenS368, 0);
  _block_2337
  = (struct _M0TPB5ArrayGRPB5ArrayGfEE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGRPB5ArrayGfEE));
  Moonbit_object_header(_block_2337)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 54, 0);
  _block_2337->$0 = _M0L6_2atmpS1370;
  _block_2337->$1 = _M0L3lenS368;
  return _block_2337;
}

uint64_t _M0MPC15array13ReadOnlyArray2atGmE(
  uint64_t* _M0L4selfS361,
  int32_t _M0L5indexS362
) {
  uint64_t* _M0L6_2atmpS1365;
  #line 38 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\readonlyarray.mbt"
  _M0L6_2atmpS1365 = _M0L4selfS361;
  if (
    _M0L5indexS362 < 0
    || _M0L5indexS362 >= Moonbit_array_length(_M0L6_2atmpS1365)
  ) {
    #line 40 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\readonlyarray.mbt"
    moonbit_panic();
  }
  return (uint64_t)_M0L6_2atmpS1365[_M0L5indexS362];
}

uint32_t _M0MPC15array13ReadOnlyArray2atGjE(
  uint32_t* _M0L4selfS363,
  int32_t _M0L5indexS364
) {
  uint32_t* _M0L6_2atmpS1366;
  #line 38 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\readonlyarray.mbt"
  _M0L6_2atmpS1366 = _M0L4selfS363;
  if (
    _M0L5indexS364 < 0
    || _M0L5indexS364 >= Moonbit_array_length(_M0L6_2atmpS1366)
  ) {
    #line 40 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\readonlyarray.mbt"
    moonbit_panic();
  }
  return (uint32_t)_M0L6_2atmpS1366[_M0L5indexS364];
}

moonbit_string_t _M0IPC16uint646UInt64PB4Show10to__string(
  uint64_t _M0L4selfS360
) {
  #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  #line 51 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  return _M0MPC16uint646UInt6418to__string_2einner(_M0L4selfS360, 10);
}

moonbit_string_t _M0IPC13int3IntPB4Show10to__string(int32_t _M0L4selfS359) {
  #line 35 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  #line 36 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  return _M0MPC13int3Int18to__string_2einner(_M0L4selfS359, 10);
}

uint64_t _M0MPC14uint4UInt10to__uint64(uint32_t _M0L4selfS358) {
  #line 2576 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  return (uint64_t)_M0L4selfS358;
}

int32_t _M0MPC15array5Array4pushGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS352,
  int32_t _M0L5valueS354
) {
  int32_t _M0L3lenS1351;
  int32_t* _M0L6_2atmpS1353;
  int32_t _M0L6_2atmpS1352;
  int32_t _M0L6lengthS353;
  int32_t* _M0L3bufS1356;
  int32_t _M0L6_2atmpS1357;
  #line 406 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L3lenS1351 = _M0L4selfS352->$1;
  #line 408 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1353 = _M0MPC15array5Array6bufferGiE(_M0L4selfS352);
  _M0L6_2atmpS1352 = Moonbit_array_length(_M0L6_2atmpS1353);
  moonbit_decref(_M0L6_2atmpS1353);
  if (_M0L3lenS1351 == _M0L6_2atmpS1352) {
    int32_t _M0L3lenS1355 = _M0L4selfS352->$1;
    int32_t _M0L6_2atmpS1354 = _M0L3lenS1355 + 1;
    #line 409 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0MPC15array5Array7reallocGiE(_M0L4selfS352, _M0L6_2atmpS1354);
  }
  _M0L6lengthS353 = _M0L4selfS352->$1;
  _M0L3bufS1356 = _M0L4selfS352->$0;
  _M0L3bufS1356[_M0L6lengthS353] = _M0L5valueS354;
  _M0L6_2atmpS1357 = _M0L6lengthS353 + 1;
  _M0L4selfS352->$1 = _M0L6_2atmpS1357;
  return 0;
}

int32_t _M0MPC15array5Array4pushGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS355,
  float _M0L5valueS357
) {
  int32_t _M0L3lenS1358;
  float* _M0L6_2atmpS1360;
  int32_t _M0L6_2atmpS1359;
  int32_t _M0L6lengthS356;
  float* _M0L3bufS1363;
  int32_t _M0L6_2atmpS1364;
  #line 406 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L3lenS1358 = _M0L4selfS355->$1;
  #line 408 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1360 = _M0MPC15array5Array6bufferGfE(_M0L4selfS355);
  _M0L6_2atmpS1359 = Moonbit_array_length(_M0L6_2atmpS1360);
  moonbit_decref(_M0L6_2atmpS1360);
  if (_M0L3lenS1358 == _M0L6_2atmpS1359) {
    int32_t _M0L3lenS1362 = _M0L4selfS355->$1;
    int32_t _M0L6_2atmpS1361 = _M0L3lenS1362 + 1;
    #line 409 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0MPC15array5Array7reallocGfE(_M0L4selfS355, _M0L6_2atmpS1361);
  }
  _M0L6lengthS356 = _M0L4selfS355->$1;
  _M0L3bufS1363 = _M0L4selfS355->$0;
  _M0L3bufS1363[_M0L6lengthS356] = _M0L5valueS357;
  _M0L6_2atmpS1364 = _M0L6lengthS356 + 1;
  _M0L4selfS355->$1 = _M0L6_2atmpS1364;
  return 0;
}

int32_t _M0MPC15array5Array7reallocGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS345,
  int32_t _M0L8requiredS347
) {
  int32_t _M0L8old__capS344;
  int32_t _M0L3lenS1349;
  int32_t _M0L8new__capS346;
  #line 302 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  #line 304 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__capS344 = _M0MPC15array5Array8capacityGiE(_M0L4selfS345);
  _M0L3lenS1349 = _M0L4selfS345->$1;
  #line 305 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__capS346
  = _M0FPB23array__growth__capacity(_M0L8old__capS344, _M0L3lenS1349, _M0L8requiredS347);
  #line 306 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0MPC15array5Array14resize__bufferGiE(_M0L4selfS345, _M0L8new__capS346);
  return 0;
}

int32_t _M0MPC15array5Array7reallocGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS349,
  int32_t _M0L8requiredS351
) {
  int32_t _M0L8old__capS348;
  int32_t _M0L3lenS1350;
  int32_t _M0L8new__capS350;
  #line 302 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  #line 304 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__capS348 = _M0MPC15array5Array8capacityGfE(_M0L4selfS349);
  _M0L3lenS1350 = _M0L4selfS349->$1;
  #line 305 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__capS350
  = _M0FPB23array__growth__capacity(_M0L8old__capS348, _M0L3lenS1350, _M0L8requiredS351);
  #line 306 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0MPC15array5Array14resize__bufferGfE(_M0L4selfS349, _M0L8new__capS350);
  return 0;
}

int32_t _M0MPC15array5Array14resize__bufferGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS333,
  int32_t _M0L13new__capacityS336
) {
  int32_t* _M0L8old__bufS332;
  int32_t _M0L3lenS334;
  int32_t _M0L9copy__lenS335;
  int32_t* _M0L8new__bufS337;
  int32_t* _M0L6_2aoldS2202;
  #line 242 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__bufS332 = _M0L4selfS333->$0;
  _M0L3lenS334 = _M0L4selfS333->$1;
  if (_M0L3lenS334 < _M0L13new__capacityS336) {
    _M0L9copy__lenS335 = _M0L3lenS334;
  } else {
    _M0L9copy__lenS335 = _M0L13new__capacityS336;
  }
  moonbit_incref(_M0L8old__bufS332);
  #line 249 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__bufS337
  = _M0MPB18UninitializedArray23make__and__blit_2einnerGiE(_M0L8old__bufS332, _M0L13new__capacityS336, _M0L9copy__lenS335, 0, 0);
  _M0L6_2aoldS2202 = _M0L4selfS333->$0;
  moonbit_decref(_M0L6_2aoldS2202);
  _M0L4selfS333->$0 = _M0L8new__bufS337;
  return 0;
}

int32_t _M0MPC15array5Array14resize__bufferGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS339,
  int32_t _M0L13new__capacityS342
) {
  float* _M0L8old__bufS338;
  int32_t _M0L3lenS340;
  int32_t _M0L9copy__lenS341;
  float* _M0L8new__bufS343;
  float* _M0L6_2aoldS2203;
  #line 242 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__bufS338 = _M0L4selfS339->$0;
  _M0L3lenS340 = _M0L4selfS339->$1;
  if (_M0L3lenS340 < _M0L13new__capacityS342) {
    _M0L9copy__lenS341 = _M0L3lenS340;
  } else {
    _M0L9copy__lenS341 = _M0L13new__capacityS342;
  }
  moonbit_incref(_M0L8old__bufS338);
  #line 249 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__bufS343
  = _M0MPB18UninitializedArray23make__and__blit_2einnerGfE(_M0L8old__bufS338, _M0L13new__capacityS342, _M0L9copy__lenS341, 0, 0);
  _M0L6_2aoldS2203 = _M0L4selfS339->$0;
  moonbit_decref(_M0L6_2aoldS2203);
  _M0L4selfS339->$0 = _M0L8new__bufS343;
  return 0;
}

int32_t _M0MPC15array5Array8capacityGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS330
) {
  int32_t* _M0L6_2atmpS1347;
  int32_t _result_2338;
  #line 132 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L6_2atmpS1347 = _M0MPC15array5Array6bufferGiE(_M0L4selfS330);
  _result_2338 = Moonbit_array_length(_M0L6_2atmpS1347);
  moonbit_decref(_M0L6_2atmpS1347);
  return _result_2338;
}

int32_t _M0MPC15array5Array8capacityGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS331
) {
  float* _M0L6_2atmpS1348;
  int32_t _result_2339;
  #line 132 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L6_2atmpS1348 = _M0MPC15array5Array6bufferGfE(_M0L4selfS331);
  _result_2339 = Moonbit_array_length(_M0L6_2atmpS1348);
  moonbit_decref(_M0L6_2atmpS1348);
  return _result_2339;
}

int32_t _M0FPB23array__growth__capacity(
  int32_t _M0L7currentS326,
  int32_t _M0L3lenS324,
  int32_t _M0L8requiredS323
) {
  int32_t _M0L5startS325;
  int32_t _M0L5spaceS327;
  #line 200 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  if (_M0L8requiredS323 < _M0L3lenS324) {
    #line 203 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_10.data);
  }
  if (_M0L7currentS326 == 0) {
    _M0L5startS325 = 8;
  } else {
    _M0L5startS325 = _M0L7currentS326;
  }
  _M0L5spaceS327 = _M0L5startS325;
  while (1) {
    if (_M0L5spaceS327 < _M0L8requiredS323) {
      int32_t _M0L4nextS328 = _M0L5spaceS327 * 2;
      if (_M0L4nextS328 <= _M0L5spaceS327) {
        return _M0L8requiredS323;
      }
      _M0L5spaceS327 = _M0L4nextS328;
      continue;
    } else {
      return _M0L5spaceS327;
    }
    break;
  }
}

int32_t _M0MPC15array5Array6lengthGfE(struct _M0TPB5ArrayGfE* _M0L4selfS321) {
  #line 147 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  return _M0L4selfS321->$1;
}

int32_t _M0MPC15array5Array6lengthGbE(struct _M0TPB5ArrayGbE* _M0L4selfS322) {
  #line 147 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  return _M0L4selfS322->$1;
}

struct _M0TP26RiantR8snn__mbt7Monitor** _M0MPC15array5Array6bufferGRP26RiantR8snn__mbt7MonitorE(
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt7MonitorE* _M0L4selfS316
) {
  struct _M0TP26RiantR8snn__mbt7Monitor** _M0L8_2afieldS2204;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS2204 = _M0L4selfS316->$0;
  moonbit_incref(_M0L8_2afieldS2204);
  return _M0L8_2afieldS2204;
}

float* _M0MPC15array5Array6bufferGfE(struct _M0TPB5ArrayGfE* _M0L4selfS317) {
  float* _M0L8_2afieldS2205;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS2205 = _M0L4selfS317->$0;
  moonbit_incref(_M0L8_2afieldS2205);
  return _M0L8_2afieldS2205;
}

struct _M0TPB5ArrayGfE** _M0MPC15array5Array6bufferGRPB5ArrayGfEE(
  struct _M0TPB5ArrayGRPB5ArrayGfEE* _M0L4selfS318
) {
  struct _M0TPB5ArrayGfE** _M0L8_2afieldS2206;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS2206 = _M0L4selfS318->$0;
  moonbit_incref(_M0L8_2afieldS2206);
  return _M0L8_2afieldS2206;
}

int32_t* _M0MPC15array5Array6bufferGiE(struct _M0TPB5ArrayGiE* _M0L4selfS319) {
  int32_t* _M0L8_2afieldS2207;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS2207 = _M0L4selfS319->$0;
  moonbit_incref(_M0L8_2afieldS2207);
  return _M0L8_2afieldS2207;
}

uint8_t* _M0MPC15array5Array6bufferGbE(struct _M0TPB5ArrayGbE* _M0L4selfS320) {
  uint8_t* _M0L8_2afieldS2208;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS2208 = _M0L4selfS320->$0;
  moonbit_incref(_M0L8_2afieldS2208);
  return _M0L8_2afieldS2208;
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
  int32_t _M0L3endS1345;
  int32_t _M0L5startS1346;
  int32_t _M0L8str__lenS311;
  int32_t _M0L3lenS1344;
  int32_t _M0L8requiredS313;
  uint16_t* _M0L4dataS1337;
  int32_t _M0L6_2atmpS1336;
  int32_t _if__result_2341;
  uint16_t* _M0L4dataS1338;
  int32_t _M0L3lenS1339;
  moonbit_string_t _M0L6_2atmpS1340;
  int32_t _M0L6_2atmpS1341;
  int32_t _M0L3lenS1343;
  int32_t _M0L6_2atmpS1342;
  #line 158 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L3endS1345 = _M0L3strS312.$2;
  _M0L5startS1346 = _M0L3strS312.$1;
  _M0L8str__lenS311 = _M0L3endS1345 - _M0L5startS1346;
  if (_M0L8str__lenS311 == 0) {
    return 0;
  }
  _M0L3lenS1344 = _M0L4selfS314->$1;
  _M0L8requiredS313 = _M0L3lenS1344 + _M0L8str__lenS311;
  _M0L4dataS1337 = _M0L4selfS314->$0;
  _M0L6_2atmpS1336 = Moonbit_array_length(_M0L4dataS1337);
  if (_M0L8requiredS313 > _M0L6_2atmpS1336) {
    _if__result_2341 = 1;
  } else {
    int32_t _M0L3lenS1335 = _M0L4selfS314->$1;
    _if__result_2341 = _M0L8requiredS313 < _M0L3lenS1335;
  }
  if (_if__result_2341) {
    #line 168 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0MPB13StringBuilder4grow(_M0L4selfS314, _M0L8requiredS313);
  }
  _M0L4dataS1338 = _M0L4selfS314->$0;
  _M0L3lenS1339 = _M0L4selfS314->$1;
  moonbit_incref(_M0L4dataS1338);
  #line 172 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L6_2atmpS1340 = _M0MPC16string10StringView4data(_M0L3strS312);
  #line 173 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L6_2atmpS1341 = _M0MPC16string10StringView13start__offset(_M0L3strS312);
  #line 170 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0MPC15array10FixedArray26unsafe__blit__from__string(_M0L4dataS1338, _M0L3lenS1339, _M0L6_2atmpS1340, _M0L6_2atmpS1341, _M0L8str__lenS311);
  moonbit_decref(_M0L4dataS1338);
  moonbit_decref(_M0L6_2atmpS1340);
  _M0L3lenS1343 = _M0L4selfS314->$1;
  _M0L6_2atmpS1342 = _M0L3lenS1343 + _M0L8str__lenS311;
  _M0L4selfS314->$1 = _M0L6_2atmpS1342;
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
    int64_t _M0L6_2atmpS1334 = -_M0L4selfS286;
    _M0L3numS288 = *(uint64_t*)&_M0L6_2atmpS1334;
  } else {
    _M0L3numS288 = *(uint64_t*)&_M0L4selfS286;
  }
  switch (_M0L5radixS285) {
    case 10: {
      int32_t _M0L10digit__lenS290;
      int32_t _M0L6_2atmpS1331;
      int32_t _M0L10total__lenS291;
      uint16_t* _M0L6bufferS292;
      int32_t _M0L12digit__startS293;
      #line 573 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS290 = _M0FPB12dec__count64(_M0L3numS288);
      if (_M0L12is__negativeS287) {
        _M0L6_2atmpS1331 = 1;
      } else {
        _M0L6_2atmpS1331 = 0;
      }
      _M0L10total__lenS291 = _M0L10digit__lenS290 + _M0L6_2atmpS1331;
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
      int32_t _M0L6_2atmpS1332;
      int32_t _M0L10total__lenS295;
      uint16_t* _M0L6bufferS296;
      int32_t _M0L12digit__startS297;
      #line 581 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS294 = _M0FPB12hex__count64(_M0L3numS288);
      if (_M0L12is__negativeS287) {
        _M0L6_2atmpS1332 = 1;
      } else {
        _M0L6_2atmpS1332 = 0;
      }
      _M0L10total__lenS295 = _M0L10digit__lenS294 + _M0L6_2atmpS1332;
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
      int32_t _M0L6_2atmpS1333;
      int32_t _M0L10total__lenS299;
      uint16_t* _M0L6bufferS300;
      int32_t _M0L12digit__startS301;
      #line 589 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS298
      = _M0FPB14radix__count64(_M0L3numS288, _M0L5radixS285);
      if (_M0L12is__negativeS287) {
        _M0L6_2atmpS1333 = 1;
      } else {
        _M0L6_2atmpS1333 = 0;
      }
      _M0L10total__lenS299 = _M0L10digit__lenS298 + _M0L6_2atmpS1333;
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
  int32_t _M0L6_2atmpS1330;
  uint64_t _M0L3numS261;
  int32_t _M0L6offsetS262;
  #line 493 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L6_2atmpS1330 = _M0L10total__lenS284 - _M0L12digit__startS272;
  _M0L3numS261 = _M0L3numS283;
  _M0L6offsetS262 = _M0L6_2atmpS1330;
  while (1) {
    if (_M0L3numS261 >= 10000ull) {
      uint64_t _M0L1tS263 = _M0L3numS261 / 10000ull;
      uint64_t _M0L6_2atmpS1307 = _M0L3numS261 % 10000ull;
      int32_t _M0L1rS264 = (int32_t)_M0L6_2atmpS1307;
      int32_t _M0L2d1S265 = _M0L1rS264 / 100;
      int32_t _M0L2d2S266 = _M0L1rS264 % 100;
      int32_t _M0L6_2atmpS1306 = _M0L2d1S265 / 10;
      int32_t _M0L6_2atmpS1305 = 48 + _M0L6_2atmpS1306;
      int32_t _M0L6d1__hiS267 = (uint16_t)_M0L6_2atmpS1305;
      int32_t _M0L6_2atmpS1304 = _M0L2d1S265 % 10;
      int32_t _M0L6_2atmpS1303 = 48 + _M0L6_2atmpS1304;
      int32_t _M0L6d1__loS268 = (uint16_t)_M0L6_2atmpS1303;
      int32_t _M0L6_2atmpS1302 = _M0L2d2S266 / 10;
      int32_t _M0L6_2atmpS1301 = 48 + _M0L6_2atmpS1302;
      int32_t _M0L6d2__hiS269 = (uint16_t)_M0L6_2atmpS1301;
      int32_t _M0L6_2atmpS1300 = _M0L2d2S266 % 10;
      int32_t _M0L6_2atmpS1299 = 48 + _M0L6_2atmpS1300;
      int32_t _M0L6d2__loS270 = (uint16_t)_M0L6_2atmpS1299;
      int32_t _M0L6_2atmpS1291 = _M0L12digit__startS272 + _M0L6offsetS262;
      int32_t _M0L6_2atmpS1290 = _M0L6_2atmpS1291 - 4;
      int32_t _M0L6_2atmpS1293;
      int32_t _M0L6_2atmpS1292;
      int32_t _M0L6_2atmpS1295;
      int32_t _M0L6_2atmpS1294;
      int32_t _M0L6_2atmpS1297;
      int32_t _M0L6_2atmpS1296;
      int32_t _M0L6_2atmpS1298;
      _M0L6bufferS271[_M0L6_2atmpS1290] = _M0L6d1__hiS267;
      _M0L6_2atmpS1293 = _M0L12digit__startS272 + _M0L6offsetS262;
      _M0L6_2atmpS1292 = _M0L6_2atmpS1293 - 3;
      _M0L6bufferS271[_M0L6_2atmpS1292] = _M0L6d1__loS268;
      _M0L6_2atmpS1295 = _M0L12digit__startS272 + _M0L6offsetS262;
      _M0L6_2atmpS1294 = _M0L6_2atmpS1295 - 2;
      _M0L6bufferS271[_M0L6_2atmpS1294] = _M0L6d2__hiS269;
      _M0L6_2atmpS1297 = _M0L12digit__startS272 + _M0L6offsetS262;
      _M0L6_2atmpS1296 = _M0L6_2atmpS1297 - 1;
      _M0L6bufferS271[_M0L6_2atmpS1296] = _M0L6d2__loS270;
      _M0L6_2atmpS1298 = _M0L6offsetS262 - 4;
      _M0L3numS261 = _M0L1tS263;
      _M0L6offsetS262 = _M0L6_2atmpS1298;
      continue;
    } else {
      int32_t _M0L6_2atmpS1329 = (int32_t)_M0L3numS261;
      int32_t _M0L9remainingS274 = _M0L6_2atmpS1329;
      int32_t _M0L6offsetS275 = _M0L6offsetS262;
      while (1) {
        if (_M0L9remainingS274 >= 100) {
          int32_t _M0L1tS276 = _M0L9remainingS274 / 100;
          int32_t _M0L1dS277 = _M0L9remainingS274 % 100;
          int32_t _M0L6_2atmpS1316 = _M0L1dS277 / 10;
          int32_t _M0L6_2atmpS1315 = 48 + _M0L6_2atmpS1316;
          int32_t _M0L5d__hiS278 = (uint16_t)_M0L6_2atmpS1315;
          int32_t _M0L6_2atmpS1314 = _M0L1dS277 % 10;
          int32_t _M0L6_2atmpS1313 = 48 + _M0L6_2atmpS1314;
          int32_t _M0L5d__loS279 = (uint16_t)_M0L6_2atmpS1313;
          int32_t _M0L6_2atmpS1309 = _M0L12digit__startS272 + _M0L6offsetS275;
          int32_t _M0L6_2atmpS1308 = _M0L6_2atmpS1309 - 2;
          int32_t _M0L6_2atmpS1311;
          int32_t _M0L6_2atmpS1310;
          int32_t _M0L6_2atmpS1312;
          _M0L6bufferS271[_M0L6_2atmpS1308] = _M0L5d__hiS278;
          _M0L6_2atmpS1311 = _M0L12digit__startS272 + _M0L6offsetS275;
          _M0L6_2atmpS1310 = _M0L6_2atmpS1311 - 1;
          _M0L6bufferS271[_M0L6_2atmpS1310] = _M0L5d__loS279;
          _M0L6_2atmpS1312 = _M0L6offsetS275 - 2;
          _M0L9remainingS274 = _M0L1tS276;
          _M0L6offsetS275 = _M0L6_2atmpS1312;
          continue;
        } else if (_M0L9remainingS274 >= 10) {
          int32_t _M0L6_2atmpS1324 = _M0L9remainingS274 / 10;
          int32_t _M0L6_2atmpS1323 = 48 + _M0L6_2atmpS1324;
          int32_t _M0L5d__hiS281 = (uint16_t)_M0L6_2atmpS1323;
          int32_t _M0L6_2atmpS1322 = _M0L9remainingS274 % 10;
          int32_t _M0L6_2atmpS1321 = 48 + _M0L6_2atmpS1322;
          int32_t _M0L5d__loS282 = (uint16_t)_M0L6_2atmpS1321;
          int32_t _M0L6_2atmpS1318 = _M0L12digit__startS272 + _M0L6offsetS275;
          int32_t _M0L6_2atmpS1317 = _M0L6_2atmpS1318 - 2;
          int32_t _M0L6_2atmpS1320;
          int32_t _M0L6_2atmpS1319;
          _M0L6bufferS271[_M0L6_2atmpS1317] = _M0L5d__hiS281;
          _M0L6_2atmpS1320 = _M0L12digit__startS272 + _M0L6offsetS275;
          _M0L6_2atmpS1319 = _M0L6_2atmpS1320 - 1;
          _M0L6bufferS271[_M0L6_2atmpS1319] = _M0L5d__loS282;
        } else {
          int32_t _M0L6_2atmpS1328 = _M0L12digit__startS272 + _M0L6offsetS275;
          int32_t _M0L6_2atmpS1325 = _M0L6_2atmpS1328 - 1;
          int32_t _M0L6_2atmpS1327 = 48 + _M0L9remainingS274;
          int32_t _M0L6_2atmpS1326 = (uint16_t)_M0L6_2atmpS1327;
          _M0L6bufferS271[_M0L6_2atmpS1325] = _M0L6_2atmpS1326;
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
  int32_t _M0L6_2atmpS1275;
  int32_t _M0L6_2atmpS1274;
  #line 462 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  #line 470 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L4baseS244 = _M0MPC13int3Int10to__uint64(_M0L5radixS245);
  _M0L6_2atmpS1275 = _M0L5radixS245 - 1;
  _M0L6_2atmpS1274 = _M0L5radixS245 & _M0L6_2atmpS1275;
  if (_M0L6_2atmpS1274 == 0) {
    int32_t _M0L5shiftS246;
    uint64_t _M0L4maskS247;
    int32_t _M0L6_2atmpS1282;
    int32_t _M0L6offsetS248;
    uint64_t _M0L1nS249;
    #line 473 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0L5shiftS246 = moonbit_ctz32(_M0L5radixS245);
    _M0L4maskS247 = _M0L4baseS244 - 1ull;
    _M0L6_2atmpS1282 = _M0L10total__lenS254 - _M0L12digit__startS252;
    _M0L6offsetS248 = _M0L6_2atmpS1282;
    _M0L1nS249 = _M0L3numS255;
    while (1) {
      if (_M0L1nS249 > 0ull) {
        uint64_t _M0L6_2atmpS1281 = _M0L1nS249 & _M0L4maskS247;
        int32_t _M0L5digitS250 = (int32_t)_M0L6_2atmpS1281;
        int32_t _M0L6_2atmpS1278 = _M0L12digit__startS252 + _M0L6offsetS248;
        int32_t _M0L6_2atmpS1276 = _M0L6_2atmpS1278 - 1;
        int32_t _M0L6_2atmpS1277 =
          ((moonbit_string_t)moonbit_string_literal_12.data)[_M0L5digitS250];
        int32_t _M0L6_2atmpS1279;
        uint64_t _M0L6_2atmpS1280;
        _M0L6bufferS251[_M0L6_2atmpS1276] = _M0L6_2atmpS1277;
        _M0L6_2atmpS1279 = _M0L6offsetS248 - 1;
        _M0L6_2atmpS1280 = _M0L1nS249 >> (_M0L5shiftS246 & 63);
        _M0L6offsetS248 = _M0L6_2atmpS1279;
        _M0L1nS249 = _M0L6_2atmpS1280;
        continue;
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1289 = _M0L10total__lenS254 - _M0L12digit__startS252;
    int32_t _M0L6offsetS256 = _M0L6_2atmpS1289;
    uint64_t _M0L1nS257 = _M0L3numS255;
    while (1) {
      if (_M0L1nS257 > 0ull) {
        uint64_t _M0L1qS258 = _M0L1nS257 / _M0L4baseS244;
        uint64_t _M0L6_2atmpS1288 = _M0L1qS258 * _M0L4baseS244;
        uint64_t _M0L6_2atmpS1287 = _M0L1nS257 - _M0L6_2atmpS1288;
        int32_t _M0L5digitS259 = (int32_t)_M0L6_2atmpS1287;
        int32_t _M0L6_2atmpS1285 = _M0L12digit__startS252 + _M0L6offsetS256;
        int32_t _M0L6_2atmpS1283 = _M0L6_2atmpS1285 - 1;
        int32_t _M0L6_2atmpS1284 =
          ((moonbit_string_t)moonbit_string_literal_12.data)[_M0L5digitS259];
        int32_t _M0L6_2atmpS1286;
        _M0L6bufferS251[_M0L6_2atmpS1283] = _M0L6_2atmpS1284;
        _M0L6_2atmpS1286 = _M0L6offsetS256 - 1;
        _M0L6offsetS256 = _M0L6_2atmpS1286;
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
  int32_t _M0L6_2atmpS1273;
  int32_t _M0L6offsetS233;
  uint64_t _M0L1nS234;
  #line 434 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L6_2atmpS1273 = _M0L10total__lenS242 - _M0L12digit__startS239;
  _M0L6offsetS233 = _M0L6_2atmpS1273;
  _M0L1nS234 = _M0L3numS243;
  while (1) {
    if (_M0L6offsetS233 >= 2) {
      uint64_t _M0L6_2atmpS1270 = _M0L1nS234 & 255ull;
      int32_t _M0L9byte__valS235 = (int32_t)_M0L6_2atmpS1270;
      int32_t _M0L2hiS236 = _M0L9byte__valS235 / 16;
      int32_t _M0L2loS237 = _M0L9byte__valS235 % 16;
      int32_t _M0L6_2atmpS1264 = _M0L12digit__startS239 + _M0L6offsetS233;
      int32_t _M0L6_2atmpS1262 = _M0L6_2atmpS1264 - 2;
      int32_t _M0L6_2atmpS1263 =
        ((moonbit_string_t)moonbit_string_literal_12.data)[_M0L2hiS236];
      int32_t _M0L6_2atmpS1267;
      int32_t _M0L6_2atmpS1265;
      int32_t _M0L6_2atmpS1266;
      int32_t _M0L6_2atmpS1268;
      uint64_t _M0L6_2atmpS1269;
      _M0L6bufferS238[_M0L6_2atmpS1262] = _M0L6_2atmpS1263;
      _M0L6_2atmpS1267 = _M0L12digit__startS239 + _M0L6offsetS233;
      _M0L6_2atmpS1265 = _M0L6_2atmpS1267 - 1;
      _M0L6_2atmpS1266
      = ((moonbit_string_t)moonbit_string_literal_12.data)[
        _M0L2loS237
      ];
      _M0L6bufferS238[_M0L6_2atmpS1265] = _M0L6_2atmpS1266;
      _M0L6_2atmpS1268 = _M0L6offsetS233 - 2;
      _M0L6_2atmpS1269 = _M0L1nS234 >> 8;
      _M0L6offsetS233 = _M0L6_2atmpS1268;
      _M0L1nS234 = _M0L6_2atmpS1269;
      continue;
    } else if (_M0L6offsetS233 == 1) {
      uint64_t _M0L6_2atmpS1272 = _M0L1nS234 & 15ull;
      int32_t _M0L6nibbleS241 = (int32_t)_M0L6_2atmpS1272;
      int32_t _M0L6_2atmpS1271 =
        ((moonbit_string_t)moonbit_string_literal_12.data)[_M0L6nibbleS241];
      _M0L6bufferS238[_M0L12digit__startS239] = _M0L6_2atmpS1271;
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
      uint64_t _M0L6_2atmpS1260 = _M0L3numS230 / _M0L4baseS228;
      int32_t _M0L6_2atmpS1261 = _M0L5countS231 + 1;
      _M0L3numS230 = _M0L6_2atmpS1260;
      _M0L5countS231 = _M0L6_2atmpS1261;
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
    int32_t _M0L6_2atmpS1259;
    int32_t _M0L6_2atmpS1258;
    #line 412 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0L14leading__zerosS226 = moonbit_clz64(_M0L5valueS225);
    _M0L6_2atmpS1259 = 63 - _M0L14leading__zerosS226;
    _M0L6_2atmpS1258 = _M0L6_2atmpS1259 / 4;
    return _M0L6_2atmpS1258 + 1;
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
    int32_t _M0L6_2atmpS1257 = -_M0L4selfS208;
    _M0L3numS210 = *(uint32_t*)&_M0L6_2atmpS1257;
  } else {
    _M0L3numS210 = *(uint32_t*)&_M0L4selfS208;
  }
  switch (_M0L5radixS207) {
    case 10: {
      int32_t _M0L10digit__lenS212;
      int32_t _M0L6_2atmpS1254;
      int32_t _M0L10total__lenS213;
      uint16_t* _M0L6bufferS214;
      int32_t _M0L12digit__startS215;
      #line 235 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS212 = _M0FPB12dec__count32(_M0L3numS210);
      if (_M0L12is__negativeS209) {
        _M0L6_2atmpS1254 = 1;
      } else {
        _M0L6_2atmpS1254 = 0;
      }
      _M0L10total__lenS213 = _M0L10digit__lenS212 + _M0L6_2atmpS1254;
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
      int32_t _M0L6_2atmpS1255;
      int32_t _M0L10total__lenS217;
      uint16_t* _M0L6bufferS218;
      int32_t _M0L12digit__startS219;
      #line 243 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS216 = _M0FPB12hex__count32(_M0L3numS210);
      if (_M0L12is__negativeS209) {
        _M0L6_2atmpS1255 = 1;
      } else {
        _M0L6_2atmpS1255 = 0;
      }
      _M0L10total__lenS217 = _M0L10digit__lenS216 + _M0L6_2atmpS1255;
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
      int32_t _M0L6_2atmpS1256;
      int32_t _M0L10total__lenS221;
      uint16_t* _M0L6bufferS222;
      int32_t _M0L12digit__startS223;
      #line 251 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS220
      = _M0FPB14radix__count32(_M0L3numS210, _M0L5radixS207);
      if (_M0L12is__negativeS209) {
        _M0L6_2atmpS1256 = 1;
      } else {
        _M0L6_2atmpS1256 = 0;
      }
      _M0L10total__lenS221 = _M0L10digit__lenS220 + _M0L6_2atmpS1256;
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
      uint32_t _M0L6_2atmpS1252 = _M0L3numS204 / _M0L4baseS202;
      int32_t _M0L6_2atmpS1253 = _M0L5countS205 + 1;
      _M0L3numS204 = _M0L6_2atmpS1252;
      _M0L5countS205 = _M0L6_2atmpS1253;
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
    int32_t _M0L6_2atmpS1251;
    int32_t _M0L6_2atmpS1250;
    #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0L14leading__zerosS200 = moonbit_clz32(_M0L5valueS199);
    _M0L6_2atmpS1251 = 31 - _M0L14leading__zerosS200;
    _M0L6_2atmpS1250 = _M0L6_2atmpS1251 / 4;
    return _M0L6_2atmpS1250 + 1;
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
  int32_t _M0L6_2atmpS1249;
  uint32_t _M0L3numS174;
  int32_t _M0L6offsetS175;
  #line 88 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L6_2atmpS1249 = _M0L10total__lenS197 - _M0L12digit__startS185;
  _M0L3numS174 = _M0L3numS196;
  _M0L6offsetS175 = _M0L6_2atmpS1249;
  while (1) {
    if (_M0L3numS174 >= 10000u) {
      uint32_t _M0L1tS176 = _M0L3numS174 / 10000u;
      uint32_t _M0L6_2atmpS1226 = _M0L3numS174 % 10000u;
      int32_t _M0L1rS177 = *(int32_t*)&_M0L6_2atmpS1226;
      int32_t _M0L2d1S178 = _M0L1rS177 / 100;
      int32_t _M0L2d2S179 = _M0L1rS177 % 100;
      int32_t _M0L6_2atmpS1225 = _M0L2d1S178 / 10;
      int32_t _M0L6_2atmpS1224 = 48 + _M0L6_2atmpS1225;
      int32_t _M0L6d1__hiS180 = (uint16_t)_M0L6_2atmpS1224;
      int32_t _M0L6_2atmpS1223 = _M0L2d1S178 % 10;
      int32_t _M0L6_2atmpS1222 = 48 + _M0L6_2atmpS1223;
      int32_t _M0L6d1__loS181 = (uint16_t)_M0L6_2atmpS1222;
      int32_t _M0L6_2atmpS1221 = _M0L2d2S179 / 10;
      int32_t _M0L6_2atmpS1220 = 48 + _M0L6_2atmpS1221;
      int32_t _M0L6d2__hiS182 = (uint16_t)_M0L6_2atmpS1220;
      int32_t _M0L6_2atmpS1219 = _M0L2d2S179 % 10;
      int32_t _M0L6_2atmpS1218 = 48 + _M0L6_2atmpS1219;
      int32_t _M0L6d2__loS183 = (uint16_t)_M0L6_2atmpS1218;
      int32_t _M0L6_2atmpS1210 = _M0L12digit__startS185 + _M0L6offsetS175;
      int32_t _M0L6_2atmpS1209 = _M0L6_2atmpS1210 - 4;
      int32_t _M0L6_2atmpS1212;
      int32_t _M0L6_2atmpS1211;
      int32_t _M0L6_2atmpS1214;
      int32_t _M0L6_2atmpS1213;
      int32_t _M0L6_2atmpS1216;
      int32_t _M0L6_2atmpS1215;
      int32_t _M0L6_2atmpS1217;
      _M0L6bufferS184[_M0L6_2atmpS1209] = _M0L6d1__hiS180;
      _M0L6_2atmpS1212 = _M0L12digit__startS185 + _M0L6offsetS175;
      _M0L6_2atmpS1211 = _M0L6_2atmpS1212 - 3;
      _M0L6bufferS184[_M0L6_2atmpS1211] = _M0L6d1__loS181;
      _M0L6_2atmpS1214 = _M0L12digit__startS185 + _M0L6offsetS175;
      _M0L6_2atmpS1213 = _M0L6_2atmpS1214 - 2;
      _M0L6bufferS184[_M0L6_2atmpS1213] = _M0L6d2__hiS182;
      _M0L6_2atmpS1216 = _M0L12digit__startS185 + _M0L6offsetS175;
      _M0L6_2atmpS1215 = _M0L6_2atmpS1216 - 1;
      _M0L6bufferS184[_M0L6_2atmpS1215] = _M0L6d2__loS183;
      _M0L6_2atmpS1217 = _M0L6offsetS175 - 4;
      _M0L3numS174 = _M0L1tS176;
      _M0L6offsetS175 = _M0L6_2atmpS1217;
      continue;
    } else {
      int32_t _M0L6_2atmpS1248 = *(int32_t*)&_M0L3numS174;
      int32_t _M0L9remainingS187 = _M0L6_2atmpS1248;
      int32_t _M0L6offsetS188 = _M0L6offsetS175;
      while (1) {
        if (_M0L9remainingS187 >= 100) {
          int32_t _M0L1tS189 = _M0L9remainingS187 / 100;
          int32_t _M0L1dS190 = _M0L9remainingS187 % 100;
          int32_t _M0L6_2atmpS1235 = _M0L1dS190 / 10;
          int32_t _M0L6_2atmpS1234 = 48 + _M0L6_2atmpS1235;
          int32_t _M0L5d__hiS191 = (uint16_t)_M0L6_2atmpS1234;
          int32_t _M0L6_2atmpS1233 = _M0L1dS190 % 10;
          int32_t _M0L6_2atmpS1232 = 48 + _M0L6_2atmpS1233;
          int32_t _M0L5d__loS192 = (uint16_t)_M0L6_2atmpS1232;
          int32_t _M0L6_2atmpS1228 = _M0L12digit__startS185 + _M0L6offsetS188;
          int32_t _M0L6_2atmpS1227 = _M0L6_2atmpS1228 - 2;
          int32_t _M0L6_2atmpS1230;
          int32_t _M0L6_2atmpS1229;
          int32_t _M0L6_2atmpS1231;
          _M0L6bufferS184[_M0L6_2atmpS1227] = _M0L5d__hiS191;
          _M0L6_2atmpS1230 = _M0L12digit__startS185 + _M0L6offsetS188;
          _M0L6_2atmpS1229 = _M0L6_2atmpS1230 - 1;
          _M0L6bufferS184[_M0L6_2atmpS1229] = _M0L5d__loS192;
          _M0L6_2atmpS1231 = _M0L6offsetS188 - 2;
          _M0L9remainingS187 = _M0L1tS189;
          _M0L6offsetS188 = _M0L6_2atmpS1231;
          continue;
        } else if (_M0L9remainingS187 >= 10) {
          int32_t _M0L6_2atmpS1243 = _M0L9remainingS187 / 10;
          int32_t _M0L6_2atmpS1242 = 48 + _M0L6_2atmpS1243;
          int32_t _M0L5d__hiS194 = (uint16_t)_M0L6_2atmpS1242;
          int32_t _M0L6_2atmpS1241 = _M0L9remainingS187 % 10;
          int32_t _M0L6_2atmpS1240 = 48 + _M0L6_2atmpS1241;
          int32_t _M0L5d__loS195 = (uint16_t)_M0L6_2atmpS1240;
          int32_t _M0L6_2atmpS1237 = _M0L12digit__startS185 + _M0L6offsetS188;
          int32_t _M0L6_2atmpS1236 = _M0L6_2atmpS1237 - 2;
          int32_t _M0L6_2atmpS1239;
          int32_t _M0L6_2atmpS1238;
          _M0L6bufferS184[_M0L6_2atmpS1236] = _M0L5d__hiS194;
          _M0L6_2atmpS1239 = _M0L12digit__startS185 + _M0L6offsetS188;
          _M0L6_2atmpS1238 = _M0L6_2atmpS1239 - 1;
          _M0L6bufferS184[_M0L6_2atmpS1238] = _M0L5d__loS195;
        } else {
          int32_t _M0L6_2atmpS1247 = _M0L12digit__startS185 + _M0L6offsetS188;
          int32_t _M0L6_2atmpS1244 = _M0L6_2atmpS1247 - 1;
          int32_t _M0L6_2atmpS1246 = 48 + _M0L9remainingS187;
          int32_t _M0L6_2atmpS1245 = (uint16_t)_M0L6_2atmpS1246;
          _M0L6bufferS184[_M0L6_2atmpS1244] = _M0L6_2atmpS1245;
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
  int32_t _M0L6_2atmpS1194;
  int32_t _M0L6_2atmpS1193;
  #line 57 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L4baseS157 = *(uint32_t*)&_M0L5radixS158;
  _M0L6_2atmpS1194 = _M0L5radixS158 - 1;
  _M0L6_2atmpS1193 = _M0L5radixS158 & _M0L6_2atmpS1194;
  if (_M0L6_2atmpS1193 == 0) {
    int32_t _M0L5shiftS159;
    uint32_t _M0L4maskS160;
    int32_t _M0L6_2atmpS1201;
    int32_t _M0L6offsetS161;
    uint32_t _M0L1nS162;
    #line 68 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0L5shiftS159 = moonbit_ctz32(_M0L5radixS158);
    _M0L4maskS160 = _M0L4baseS157 - 1u;
    _M0L6_2atmpS1201 = _M0L10total__lenS167 - _M0L12digit__startS165;
    _M0L6offsetS161 = _M0L6_2atmpS1201;
    _M0L1nS162 = _M0L3numS168;
    while (1) {
      if (_M0L1nS162 > 0u) {
        uint32_t _M0L6_2atmpS1200 = _M0L1nS162 & _M0L4maskS160;
        int32_t _M0L5digitS163 = *(int32_t*)&_M0L6_2atmpS1200;
        int32_t _M0L6_2atmpS1197 = _M0L12digit__startS165 + _M0L6offsetS161;
        int32_t _M0L6_2atmpS1195 = _M0L6_2atmpS1197 - 1;
        int32_t _M0L6_2atmpS1196 =
          ((moonbit_string_t)moonbit_string_literal_12.data)[_M0L5digitS163];
        int32_t _M0L6_2atmpS1198;
        uint32_t _M0L6_2atmpS1199;
        _M0L6bufferS164[_M0L6_2atmpS1195] = _M0L6_2atmpS1196;
        _M0L6_2atmpS1198 = _M0L6offsetS161 - 1;
        _M0L6_2atmpS1199 = _M0L1nS162 >> (_M0L5shiftS159 & 31);
        _M0L6offsetS161 = _M0L6_2atmpS1198;
        _M0L1nS162 = _M0L6_2atmpS1199;
        continue;
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1208 = _M0L10total__lenS167 - _M0L12digit__startS165;
    int32_t _M0L6offsetS169 = _M0L6_2atmpS1208;
    uint32_t _M0L1nS170 = _M0L3numS168;
    while (1) {
      if (_M0L1nS170 > 0u) {
        uint32_t _M0L1qS171 = _M0L1nS170 / _M0L4baseS157;
        uint32_t _M0L6_2atmpS1207 = _M0L1qS171 * _M0L4baseS157;
        uint32_t _M0L6_2atmpS1206 = _M0L1nS170 - _M0L6_2atmpS1207;
        int32_t _M0L5digitS172 = *(int32_t*)&_M0L6_2atmpS1206;
        int32_t _M0L6_2atmpS1204 = _M0L12digit__startS165 + _M0L6offsetS169;
        int32_t _M0L6_2atmpS1202 = _M0L6_2atmpS1204 - 1;
        int32_t _M0L6_2atmpS1203 =
          ((moonbit_string_t)moonbit_string_literal_12.data)[_M0L5digitS172];
        int32_t _M0L6_2atmpS1205;
        _M0L6bufferS164[_M0L6_2atmpS1202] = _M0L6_2atmpS1203;
        _M0L6_2atmpS1205 = _M0L6offsetS169 - 1;
        _M0L6offsetS169 = _M0L6_2atmpS1205;
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
  int32_t _M0L6_2atmpS1192;
  int32_t _M0L6offsetS146;
  uint32_t _M0L1nS147;
  #line 29 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L6_2atmpS1192 = _M0L10total__lenS155 - _M0L12digit__startS152;
  _M0L6offsetS146 = _M0L6_2atmpS1192;
  _M0L1nS147 = _M0L3numS156;
  while (1) {
    if (_M0L6offsetS146 >= 2) {
      uint32_t _M0L6_2atmpS1189 = _M0L1nS147 & 255u;
      int32_t _M0L9byte__valS148 = *(int32_t*)&_M0L6_2atmpS1189;
      int32_t _M0L2hiS149 = _M0L9byte__valS148 / 16;
      int32_t _M0L2loS150 = _M0L9byte__valS148 % 16;
      int32_t _M0L6_2atmpS1183 = _M0L12digit__startS152 + _M0L6offsetS146;
      int32_t _M0L6_2atmpS1181 = _M0L6_2atmpS1183 - 2;
      int32_t _M0L6_2atmpS1182 =
        ((moonbit_string_t)moonbit_string_literal_12.data)[_M0L2hiS149];
      int32_t _M0L6_2atmpS1186;
      int32_t _M0L6_2atmpS1184;
      int32_t _M0L6_2atmpS1185;
      int32_t _M0L6_2atmpS1187;
      uint32_t _M0L6_2atmpS1188;
      _M0L6bufferS151[_M0L6_2atmpS1181] = _M0L6_2atmpS1182;
      _M0L6_2atmpS1186 = _M0L12digit__startS152 + _M0L6offsetS146;
      _M0L6_2atmpS1184 = _M0L6_2atmpS1186 - 1;
      _M0L6_2atmpS1185
      = ((moonbit_string_t)moonbit_string_literal_12.data)[
        _M0L2loS150
      ];
      _M0L6bufferS151[_M0L6_2atmpS1184] = _M0L6_2atmpS1185;
      _M0L6_2atmpS1187 = _M0L6offsetS146 - 2;
      _M0L6_2atmpS1188 = _M0L1nS147 >> 8;
      _M0L6offsetS146 = _M0L6_2atmpS1187;
      _M0L1nS147 = _M0L6_2atmpS1188;
      continue;
    } else if (_M0L6offsetS146 == 1) {
      uint32_t _M0L6_2atmpS1191 = _M0L1nS147 & 15u;
      int32_t _M0L6nibbleS154 = *(int32_t*)&_M0L6_2atmpS1191;
      int32_t _M0L6_2atmpS1190 =
        ((moonbit_string_t)moonbit_string_literal_12.data)[_M0L6nibbleS154];
      _M0L6bufferS151[_M0L12digit__startS152] = _M0L6_2atmpS1190;
    }
    break;
  }
  return 0;
}

int32_t _M0IP016_24default__implPB4Show6outputGmE(
  uint64_t _M0L4selfS143,
  struct _M0TPB6Logger _M0L6loggerS142
) {
  moonbit_string_t _M0L6_2atmpS1179;
  #line 159 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  #line 160 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS1179 = _M0IPC16uint646UInt64PB4Show10to__string(_M0L4selfS143);
  #line 160 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6loggerS142.$0->$method_0(_M0L6loggerS142.$1, _M0L6_2atmpS1179);
  moonbit_decref(_M0L6_2atmpS1179);
  return 0;
}

int32_t _M0IP016_24default__implPB4Show6outputGiE(
  int32_t _M0L4selfS145,
  struct _M0TPB6Logger _M0L6loggerS144
) {
  moonbit_string_t _M0L6_2atmpS1180;
  #line 159 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  #line 160 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS1180 = _M0IPC13int3IntPB4Show10to__string(_M0L4selfS145);
  #line 160 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6loggerS144.$0->$method_0(_M0L6loggerS144.$1, _M0L6_2atmpS1180);
  moonbit_decref(_M0L6_2atmpS1180);
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
  moonbit_string_t _M0L8_2afieldS2209;
  #line 92 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
  _M0L8_2afieldS2209 = _M0L4selfS140.$0;
  moonbit_incref(_M0L8_2afieldS2209);
  return _M0L8_2afieldS2209;
}

int32_t _M0IP016_24default__implPB6Logger16write__substringGRPB13StringBuilderE(
  struct _M0TPB13StringBuilder* _M0L4selfS136,
  moonbit_string_t _M0L5valueS137,
  int32_t _M0L5startS138,
  int32_t _M0L3lenS139
) {
  int32_t _M0L6_2atmpS1178;
  int64_t _M0L6_2atmpS1177;
  struct _M0TPC16string10StringView _M0L6_2atmpS1176;
  #line 122 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS1178 = _M0L5startS138 + _M0L3lenS139;
  _M0L6_2atmpS1177 = (int64_t)_M0L6_2atmpS1178;
  #line 123 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS1176
  = _M0MPC16string6String11sub_2einner(_M0L5valueS137, _M0L5startS138, _M0L6_2atmpS1177);
  #line 123 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0IPB13StringBuilderPB6Logger11write__view(_M0L4selfS136, _M0L6_2atmpS1176);
  moonbit_decref(_M0L6_2atmpS1176.$0);
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
  goto joinlet_2354;
  join_130:;
  _M0L3endS129 = _M0L3endS131;
  joinlet_2354:;
  if (
    _M0L5startS135 >= 0
    && _M0L5startS135 <= _M0L3endS129
    && _M0L3endS129 <= _M0L3lenS127
  ) {
    if (_M0L5startS135 < _M0L3lenS127) {
      int32_t _M0L6_2atmpS1173 = _M0L4selfS128[_M0L5startS135];
      int32_t _M0L6_2atmpS1172;
      #line 931 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
      _M0L6_2atmpS1172
      = _M0MPC16uint166UInt1623is__trailing__surrogate(_M0L6_2atmpS1173);
      if (!_M0L6_2atmpS1172) {
        
      } else {
        #line 931 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
        moonbit_panic();
      }
    }
    if (_M0L3endS129 < _M0L3lenS127) {
      int32_t _M0L6_2atmpS1175 = _M0L4selfS128[_M0L3endS129];
      int32_t _M0L6_2atmpS1174;
      #line 934 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
      _M0L6_2atmpS1174
      = _M0MPC16uint166UInt1623is__trailing__surrogate(_M0L6_2atmpS1175);
      if (!_M0L6_2atmpS1174) {
        
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
  struct _M0TPB6Logger _M0L6_2atmpS1171;
  #line 116 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  moonbit_incref(_M0L4selfS126);
  _M0L6_2atmpS1171
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS126
  };
  #line 117 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L4showS125.$0->$method_0(_M0L4showS125.$1, _M0L6_2atmpS1171);
  if (_M0L6_2atmpS1171.$1) {
    moonbit_decref(_M0L6_2atmpS1171.$1);
  }
  return 0;
}

int32_t _M0IP016_24default__implPB6Logger28write__string__interpolationGRPB13StringBuilderE(
  struct _M0TPB13StringBuilder* _M0L4selfS124,
  struct _M0TPB4Show _M0L4showS123
) {
  struct _M0TPB6Logger _M0L6_2atmpS1170;
  #line 111 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  moonbit_incref(_M0L4selfS124);
  _M0L6_2atmpS1170
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS124
  };
  #line 112 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L4showS123.$0->$method_0(_M0L4showS123.$1, _M0L6_2atmpS1170);
  if (_M0L6_2atmpS1170.$1) {
    moonbit_decref(_M0L6_2atmpS1170.$1);
  }
  return 0;
}

uint64_t _M0MPC13int3Int10to__uint64(int32_t _M0L4selfS122) {
  int64_t _M0L6_2atmpS1169;
  #line 989 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS1169 = (int64_t)_M0L4selfS122;
  return *(uint64_t*)&_M0L6_2atmpS1169;
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
  int32_t _M0L3lenS1168;
  int32_t _M0L8requiredS119;
  uint16_t* _M0L4dataS1163;
  int32_t _M0L6_2atmpS1162;
  int32_t _if__result_2355;
  uint16_t* _M0L4dataS1164;
  int32_t _M0L3lenS1165;
  int32_t _M0L3lenS1167;
  int32_t _M0L6_2atmpS1166;
  #line 105 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L8str__lenS117 = Moonbit_array_length(_M0L3strS118);
  if (_M0L8str__lenS117 == 0) {
    return 0;
  }
  _M0L3lenS1168 = _M0L4selfS120->$1;
  _M0L8requiredS119 = _M0L3lenS1168 + _M0L8str__lenS117;
  _M0L4dataS1163 = _M0L4selfS120->$0;
  _M0L6_2atmpS1162 = Moonbit_array_length(_M0L4dataS1163);
  if (_M0L8requiredS119 > _M0L6_2atmpS1162) {
    _if__result_2355 = 1;
  } else {
    int32_t _M0L3lenS1161 = _M0L4selfS120->$1;
    _if__result_2355 = _M0L8requiredS119 < _M0L3lenS1161;
  }
  if (_if__result_2355) {
    #line 112 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0MPB13StringBuilder4grow(_M0L4selfS120, _M0L8requiredS119);
  }
  _M0L4dataS1164 = _M0L4selfS120->$0;
  _M0L3lenS1165 = _M0L4selfS120->$1;
  moonbit_incref(_M0L4dataS1164);
  #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0MPC15array10FixedArray26unsafe__blit__from__string(_M0L4dataS1164, _M0L3lenS1165, _M0L3strS118, 0, _M0L8str__lenS117);
  moonbit_decref(_M0L4dataS1164);
  _M0L3lenS1167 = _M0L4selfS120->$1;
  _M0L6_2atmpS1166 = _M0L3lenS1167 + _M0L8str__lenS117;
  _M0L4selfS120->$1 = _M0L6_2atmpS1166;
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
      int32_t _M0L6_2atmpS1158 = _M0L3strS114[_M0L1iS111];
      int32_t _M0L6_2atmpS1159;
      int32_t _M0L6_2atmpS1160;
      _M0L4selfS113[_M0L1jS112] = _M0L6_2atmpS1158;
      _M0L6_2atmpS1159 = _M0L1iS111 + 1;
      _M0L6_2atmpS1160 = _M0L1jS112 + 1;
      _M0L1iS111 = _M0L6_2atmpS1159;
      _M0L1jS112 = _M0L6_2atmpS1160;
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
    int32_t _M0L3lenS1129 = _M0L4selfS106->$1;
    uint16_t* _M0L4dataS1131 = _M0L4selfS106->$0;
    int32_t _M0L6_2atmpS1130 = Moonbit_array_length(_M0L4dataS1131);
    uint16_t* _M0L4dataS1134;
    int32_t _M0L3lenS1135;
    int32_t _M0L6_2atmpS1136;
    int32_t _M0L3lenS1138;
    int32_t _M0L6_2atmpS1137;
    if (_M0L3lenS1129 >= _M0L6_2atmpS1130) {
      int32_t _M0L3lenS1133 = _M0L4selfS106->$1;
      int32_t _M0L6_2atmpS1132 = _M0L3lenS1133 + 1;
      #line 124 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0MPB13StringBuilder4grow(_M0L4selfS106, _M0L6_2atmpS1132);
    }
    _M0L4dataS1134 = _M0L4selfS106->$0;
    _M0L3lenS1135 = _M0L4selfS106->$1;
    moonbit_incref(_M0L4dataS1134);
    #line 126 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0L6_2atmpS1136 = _M0MPC14uint4UInt10to__uint16(_M0L4codeS104);
    if (
      _M0L3lenS1135 < 0
      || _M0L3lenS1135 >= Moonbit_array_length(_M0L4dataS1134)
    ) {
      #line 126 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      moonbit_panic();
    }
    _M0L4dataS1134[_M0L3lenS1135] = _M0L6_2atmpS1136;
    moonbit_decref(_M0L4dataS1134);
    _M0L3lenS1138 = _M0L4selfS106->$1;
    _M0L6_2atmpS1137 = _M0L3lenS1138 + 1;
    _M0L4selfS106->$1 = _M0L6_2atmpS1137;
  } else if (_M0L4codeS104 <= 1114111u) {
    uint16_t* _M0L4dataS1142 = _M0L4selfS106->$0;
    int32_t _M0L6_2atmpS1140 = Moonbit_array_length(_M0L4dataS1142);
    int32_t _M0L3lenS1141 = _M0L4selfS106->$1;
    int32_t _M0L6_2atmpS1139 = _M0L6_2atmpS1140 - _M0L3lenS1141;
    uint32_t _M0L4codeS107;
    uint16_t* _M0L4dataS1145;
    int32_t _M0L3lenS1146;
    uint32_t _M0L6_2atmpS1149;
    uint32_t _M0L6_2atmpS1148;
    int32_t _M0L6_2atmpS1147;
    uint16_t* _M0L4dataS1150;
    int32_t _M0L3lenS1155;
    int32_t _M0L6_2atmpS1151;
    uint32_t _M0L6_2atmpS1154;
    uint32_t _M0L6_2atmpS1153;
    int32_t _M0L6_2atmpS1152;
    int32_t _M0L3lenS1157;
    int32_t _M0L6_2atmpS1156;
    if (_M0L6_2atmpS1139 < 2) {
      int32_t _M0L3lenS1144 = _M0L4selfS106->$1;
      int32_t _M0L6_2atmpS1143 = _M0L3lenS1144 + 2;
      #line 130 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0MPB13StringBuilder4grow(_M0L4selfS106, _M0L6_2atmpS1143);
    }
    _M0L4codeS107 = _M0L4codeS104 - 65536u;
    _M0L4dataS1145 = _M0L4selfS106->$0;
    _M0L3lenS1146 = _M0L4selfS106->$1;
    _M0L6_2atmpS1149 = _M0L4codeS107 >> 10;
    _M0L6_2atmpS1148 = 55296u + _M0L6_2atmpS1149;
    moonbit_incref(_M0L4dataS1145);
    #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0L6_2atmpS1147 = _M0MPC14uint4UInt10to__uint16(_M0L6_2atmpS1148);
    if (
      _M0L3lenS1146 < 0
      || _M0L3lenS1146 >= Moonbit_array_length(_M0L4dataS1145)
    ) {
      #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      moonbit_panic();
    }
    _M0L4dataS1145[_M0L3lenS1146] = _M0L6_2atmpS1147;
    moonbit_decref(_M0L4dataS1145);
    _M0L4dataS1150 = _M0L4selfS106->$0;
    _M0L3lenS1155 = _M0L4selfS106->$1;
    _M0L6_2atmpS1151 = _M0L3lenS1155 + 1;
    _M0L6_2atmpS1154 = _M0L4codeS107 & 1023u;
    _M0L6_2atmpS1153 = 56320u + _M0L6_2atmpS1154;
    moonbit_incref(_M0L4dataS1150);
    #line 134 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0L6_2atmpS1152 = _M0MPC14uint4UInt10to__uint16(_M0L6_2atmpS1153);
    if (
      _M0L6_2atmpS1151 < 0
      || _M0L6_2atmpS1151 >= Moonbit_array_length(_M0L4dataS1150)
    ) {
      #line 134 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      moonbit_panic();
    }
    _M0L4dataS1150[_M0L6_2atmpS1151] = _M0L6_2atmpS1152;
    moonbit_decref(_M0L4dataS1150);
    _M0L3lenS1157 = _M0L4selfS106->$1;
    _M0L6_2atmpS1156 = _M0L3lenS1157 + 2;
    _M0L4selfS106->$1 = _M0L6_2atmpS1156;
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
  uint16_t* _M0L4dataS1128;
  int32_t _M0L6_2atmpS1126;
  int32_t _M0L3lenS1127;
  int32_t _M0L13new__capacityS100;
  uint16_t* _M0L4dataS1123;
  int32_t _M0L6_2atmpS1124;
  int32_t _M0L3lenS1125;
  uint16_t* _M0L9new__dataS103;
  uint16_t* _M0L6_2aoldS2210;
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L4dataS1128 = _M0L4selfS101->$0;
  _M0L6_2atmpS1126 = Moonbit_array_length(_M0L4dataS1128);
  _M0L3lenS1127 = _M0L4selfS101->$1;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L13new__capacityS100
  = _M0FPB31stringbuilder__growth__capacity(_M0L6_2atmpS1126, _M0L3lenS1127, _M0L8requiredS102);
  _M0L4dataS1123 = _M0L4selfS101->$0;
  moonbit_incref(_M0L4dataS1123);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L6_2atmpS1124 = _M0IPC16uint166UInt16PB7Default7default();
  _M0L3lenS1125 = _M0L4selfS101->$1;
  #line 80 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L9new__dataS103
  = _M0MPC15array10FixedArray23make__and__blit_2einnerGkE(_M0L4dataS1123, _M0L13new__capacityS100, _M0L6_2atmpS1124, _M0L3lenS1125, 0, 0);
  _M0L6_2aoldS2210 = _M0L4selfS101->$0;
  moonbit_decref(_M0L6_2aoldS2210);
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
  int32_t _M0L6_2atmpS1122;
  #line 2785 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS1122 = *(int32_t*)&_M0L4selfS93;
  return (uint16_t)_M0L6_2atmpS1122;
}

uint32_t _M0MPC14char4Char8to__uint(int32_t _M0L4selfS92) {
  int32_t _M0L6_2atmpS1121;
  #line 1335 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS1121 = _M0L4selfS92;
  return *(uint32_t*)&_M0L6_2atmpS1121;
}

moonbit_string_t _M0MPB13StringBuilder10to__string(
  struct _M0TPB13StringBuilder* _M0L4selfS90
) {
  int32_t _M0L3lenS1112;
  #line 181 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L3lenS1112 = _M0L4selfS90->$1;
  if (_M0L3lenS1112 == 0) {
    return (moonbit_string_t)moonbit_string_literal_0.data;
  } else {
    int32_t _M0L3lenS1113 = _M0L4selfS90->$1;
    uint16_t* _M0L4dataS1115 = _M0L4selfS90->$0;
    int32_t _M0L6_2atmpS1114 = Moonbit_array_length(_M0L4dataS1115);
    if (_M0L3lenS1113 == _M0L6_2atmpS1114) {
      uint16_t* _M0L4dataS1116 = _M0L4selfS90->$0;
      moonbit_incref(_M0L4dataS1116);
      return _M0L4dataS1116;
    } else {
      uint16_t* _M0L4dataS1117 = _M0L4selfS90->$0;
      int32_t _M0L3lenS1118 = _M0L4selfS90->$1;
      int32_t _M0L6_2atmpS1119;
      int32_t _M0L3lenS1120;
      uint16_t* _M0L4dataS91;
      moonbit_incref(_M0L4dataS1117);
      #line 190 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0L6_2atmpS1119 = _M0IPC16uint166UInt16PB7Default7default();
      _M0L3lenS1120 = _M0L4selfS90->$1;
      #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0L4dataS91
      = _M0MPC15array10FixedArray23make__and__blit_2einnerGkE(_M0L4dataS1117, _M0L3lenS1118, _M0L6_2atmpS1119, _M0L3lenS1120, 0, 0);
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
  int32_t _if__result_2358;
  #line 97 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
  if (_M0L13allocate__lenS83 >= 0) {
    if (_M0L3lenS84 >= 0) {
      if (_M0L11src__offsetS85 >= 0) {
        if (_M0L11dst__offsetS86 >= 0) {
          int32_t _M0L6_2atmpS1108 = _M0L11src__offsetS85 + _M0L3lenS84;
          int32_t _M0L6_2atmpS1109 = Moonbit_array_length(_M0L3srcS87);
          if (_M0L6_2atmpS1108 <= _M0L6_2atmpS1109) {
            int32_t _M0L6_2atmpS1107 = _M0L11dst__offsetS86 + _M0L3lenS84;
            _if__result_2358 = _M0L6_2atmpS1107 <= _M0L13allocate__lenS83;
          } else {
            _if__result_2358 = 0;
          }
        } else {
          _if__result_2358 = 0;
        }
      } else {
        _if__result_2358 = 0;
      }
    } else {
      _if__result_2358 = 0;
    }
  } else {
    _if__result_2358 = 0;
  }
  if (_if__result_2358) {
    #line 116 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    return _M0MPC15array10FixedArray23unsafe__make__and__blitGkE(_M0L3srcS87, _M0L13allocate__lenS83, _M0L4initS88, _M0L11src__offsetS85, _M0L11dst__offsetS86, _M0L3lenS84);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS89;
    int32_t _M0L6_2atmpS1111;
    moonbit_string_t _M0L6_2atmpS1110;
    uint16_t* _result_2359;
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
    _M0L6_2atmpS1111 = Moonbit_array_length(_M0L3srcS87);
    moonbit_decref(_M0L3srcS87);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS89, _M0L6_2atmpS1111);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0L6_2atmpS1110
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS89);
    moonbit_decref(_M0L18_2astring__builderS89);
    #line 112 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _result_2359 = _M0FPC15abort5abortGAkE(_M0L6_2atmpS1110);
    moonbit_decref(_M0L6_2atmpS1110);
    return _result_2359;
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
  struct _M0TPB13StringBuilder* _block_2360;
  #line 32 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  if (_M0L10size__hintS74 < 1) {
    _M0L7initialS73 = 1;
  } else {
    int32_t _M0L6_2atmpS1106 = _M0L10size__hintS74 + 1;
    _M0L7initialS73 = _M0L6_2atmpS1106 / 2;
  }
  _M0L4dataS75 = (uint16_t*)moonbit_make_string(_M0L7initialS73, 0);
  _block_2360
  = (struct _M0TPB13StringBuilder*)moonbit_malloc(sizeof(struct _M0TPB13StringBuilder));
  Moonbit_object_header(_block_2360)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 57, 0);
  _block_2360->$0 = _M0L4dataS75;
  _block_2360->$1 = 0;
  return _block_2360;
}

int32_t* _M0MPB18UninitializedArray23make__and__blit_2einnerGiE(
  int32_t* _M0L3srcS65,
  int32_t _M0L13allocate__lenS61,
  int32_t _M0L3lenS62,
  int32_t _M0L11src__offsetS63,
  int32_t _M0L11dst__offsetS64
) {
  int32_t _if__result_2361;
  #line 167 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  if (_M0L13allocate__lenS61 >= 0) {
    if (_M0L3lenS62 >= 0) {
      if (_M0L11src__offsetS63 >= 0) {
        if (_M0L11dst__offsetS64 >= 0) {
          int32_t _M0L6_2atmpS1097 = _M0L11src__offsetS63 + _M0L3lenS62;
          int32_t _M0L6_2atmpS1098;
          #line 179 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
          _M0L6_2atmpS1098
          = _M0MPB18UninitializedArray6lengthGiE(_M0L3srcS65);
          if (_M0L6_2atmpS1097 <= _M0L6_2atmpS1098) {
            int32_t _M0L6_2atmpS1096 = _M0L11dst__offsetS64 + _M0L3lenS62;
            _if__result_2361 = _M0L6_2atmpS1096 <= _M0L13allocate__lenS61;
          } else {
            _if__result_2361 = 0;
          }
        } else {
          _if__result_2361 = 0;
        }
      } else {
        _if__result_2361 = 0;
      }
    } else {
      _if__result_2361 = 0;
    }
  } else {
    _if__result_2361 = 0;
  }
  if (_if__result_2361) {
    #line 185 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    return _M0MPB18UninitializedArray23unsafe__make__and__blitGiE(_M0L3srcS65, _M0L13allocate__lenS61, _M0L11src__offsetS63, _M0L11dst__offsetS64, _M0L3lenS62);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS66;
    int32_t _M0L6_2atmpS1100;
    moonbit_string_t _M0L6_2atmpS1099;
    int32_t* _result_2362;
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
    _M0L6_2atmpS1100 = _M0MPB18UninitializedArray6lengthGiE(_M0L3srcS65);
    moonbit_decref(_M0L3srcS65);
    #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS66, _M0L6_2atmpS1100);
    #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS1099
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS66);
    moonbit_decref(_M0L18_2astring__builderS66);
    #line 181 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _result_2362
    = _M0FPC15abort5abortGRPB18UninitializedArrayGiEE(_M0L6_2atmpS1099);
    moonbit_decref(_M0L6_2atmpS1099);
    return _result_2362;
  }
}

float* _M0MPB18UninitializedArray23make__and__blit_2einnerGfE(
  float* _M0L3srcS71,
  int32_t _M0L13allocate__lenS67,
  int32_t _M0L3lenS68,
  int32_t _M0L11src__offsetS69,
  int32_t _M0L11dst__offsetS70
) {
  int32_t _if__result_2363;
  #line 167 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  if (_M0L13allocate__lenS67 >= 0) {
    if (_M0L3lenS68 >= 0) {
      if (_M0L11src__offsetS69 >= 0) {
        if (_M0L11dst__offsetS70 >= 0) {
          int32_t _M0L6_2atmpS1102 = _M0L11src__offsetS69 + _M0L3lenS68;
          int32_t _M0L6_2atmpS1103;
          #line 179 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
          _M0L6_2atmpS1103
          = _M0MPB18UninitializedArray6lengthGfE(_M0L3srcS71);
          if (_M0L6_2atmpS1102 <= _M0L6_2atmpS1103) {
            int32_t _M0L6_2atmpS1101 = _M0L11dst__offsetS70 + _M0L3lenS68;
            _if__result_2363 = _M0L6_2atmpS1101 <= _M0L13allocate__lenS67;
          } else {
            _if__result_2363 = 0;
          }
        } else {
          _if__result_2363 = 0;
        }
      } else {
        _if__result_2363 = 0;
      }
    } else {
      _if__result_2363 = 0;
    }
  } else {
    _if__result_2363 = 0;
  }
  if (_if__result_2363) {
    #line 185 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    return _M0MPB18UninitializedArray23unsafe__make__and__blitGfE(_M0L3srcS71, _M0L13allocate__lenS67, _M0L11src__offsetS69, _M0L11dst__offsetS70, _M0L3lenS68);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS72;
    int32_t _M0L6_2atmpS1105;
    moonbit_string_t _M0L6_2atmpS1104;
    float* _result_2364;
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
    _M0L6_2atmpS1105 = _M0MPB18UninitializedArray6lengthGfE(_M0L3srcS71);
    moonbit_decref(_M0L3srcS71);
    #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS72, _M0L6_2atmpS1105);
    #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS1104
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS72);
    moonbit_decref(_M0L18_2astring__builderS72);
    #line 181 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _result_2364
    = _M0FPC15abort5abortGRPB18UninitializedArrayGfEE(_M0L6_2atmpS1104);
    moonbit_decref(_M0L6_2atmpS1104);
    return _result_2364;
  }
}

int32_t _M0MPB13StringBuilder13write__objectGmE(
  struct _M0TPB13StringBuilder* _M0L4selfS58,
  uint64_t _M0L3objS57
) {
  struct _M0TPB6Logger _M0L6_2atmpS1094;
  #line 17 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  moonbit_incref(_M0L4selfS58);
  _M0L6_2atmpS1094
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS58
  };
  #line 23 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  _M0IP016_24default__implPB4Show6outputGmE(_M0L3objS57, _M0L6_2atmpS1094);
  if (_M0L6_2atmpS1094.$1) {
    moonbit_decref(_M0L6_2atmpS1094.$1);
  }
  return 0;
}

int32_t _M0MPB13StringBuilder13write__objectGiE(
  struct _M0TPB13StringBuilder* _M0L4selfS60,
  int32_t _M0L3objS59
) {
  struct _M0TPB6Logger _M0L6_2atmpS1095;
  #line 17 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  moonbit_incref(_M0L4selfS60);
  _M0L6_2atmpS1095
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS60
  };
  #line 23 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  _M0IP016_24default__implPB4Show6outputGiE(_M0L3objS59, _M0L6_2atmpS1095);
  if (_M0L6_2atmpS1095.$1) {
    moonbit_decref(_M0L6_2atmpS1095.$1);
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
        int32_t _M0L6_2atmpS1067 = _M0L11dst__offsetS10 + _M0L1iS12;
        int32_t _M0L6_2atmpS1069 = _M0L11src__offsetS11 + _M0L1iS12;
        int32_t _M0L6_2atmpS1068;
        int32_t _M0L6_2atmpS1070;
        if (
          _M0L6_2atmpS1069 < 0
          || _M0L6_2atmpS1069 >= Moonbit_array_length(_M0L3srcS9)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1068 = (int32_t)_M0L3srcS9[_M0L6_2atmpS1069];
        if (
          _M0L6_2atmpS1067 < 0
          || _M0L6_2atmpS1067 >= Moonbit_array_length(_M0L3dstS8)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS8[_M0L6_2atmpS1067] = _M0L6_2atmpS1068;
        _M0L6_2atmpS1070 = _M0L1iS12 + 1;
        _M0L1iS12 = _M0L6_2atmpS1070;
        continue;
      } else {
        moonbit_decref(_M0L3srcS9);
        moonbit_decref(_M0L3dstS8);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1075 = _M0L3lenS13 - 1;
    int32_t _M0L1iS15 = _M0L6_2atmpS1075;
    while (1) {
      if (_M0L1iS15 >= 0) {
        int32_t _M0L6_2atmpS1071 = _M0L11dst__offsetS10 + _M0L1iS15;
        int32_t _M0L6_2atmpS1073 = _M0L11src__offsetS11 + _M0L1iS15;
        int32_t _M0L6_2atmpS1072;
        int32_t _M0L6_2atmpS1074;
        if (
          _M0L6_2atmpS1073 < 0
          || _M0L6_2atmpS1073 >= Moonbit_array_length(_M0L3srcS9)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1072 = (int32_t)_M0L3srcS9[_M0L6_2atmpS1073];
        if (
          _M0L6_2atmpS1071 < 0
          || _M0L6_2atmpS1071 >= Moonbit_array_length(_M0L3dstS8)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS8[_M0L6_2atmpS1071] = _M0L6_2atmpS1072;
        _M0L6_2atmpS1074 = _M0L1iS15 - 1;
        _M0L1iS15 = _M0L6_2atmpS1074;
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
        int32_t _M0L6_2atmpS1076 = _M0L11dst__offsetS19 + _M0L1iS21;
        int32_t _M0L6_2atmpS1078 = _M0L11src__offsetS20 + _M0L1iS21;
        float _M0L6_2atmpS1077;
        int32_t _M0L6_2atmpS1079;
        if (
          _M0L6_2atmpS1078 < 0
          || _M0L6_2atmpS1078 >= Moonbit_array_length(_M0L3srcS18)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1077 = (float)_M0L3srcS18[_M0L6_2atmpS1078];
        if (
          _M0L6_2atmpS1076 < 0
          || _M0L6_2atmpS1076 >= Moonbit_array_length(_M0L3dstS17)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS17[_M0L6_2atmpS1076] = _M0L6_2atmpS1077;
        _M0L6_2atmpS1079 = _M0L1iS21 + 1;
        _M0L1iS21 = _M0L6_2atmpS1079;
        continue;
      } else {
        moonbit_decref(_M0L3srcS18);
        moonbit_decref(_M0L3dstS17);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1084 = _M0L3lenS22 - 1;
    int32_t _M0L1iS24 = _M0L6_2atmpS1084;
    while (1) {
      if (_M0L1iS24 >= 0) {
        int32_t _M0L6_2atmpS1080 = _M0L11dst__offsetS19 + _M0L1iS24;
        int32_t _M0L6_2atmpS1082 = _M0L11src__offsetS20 + _M0L1iS24;
        float _M0L6_2atmpS1081;
        int32_t _M0L6_2atmpS1083;
        if (
          _M0L6_2atmpS1082 < 0
          || _M0L6_2atmpS1082 >= Moonbit_array_length(_M0L3srcS18)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1081 = (float)_M0L3srcS18[_M0L6_2atmpS1082];
        if (
          _M0L6_2atmpS1080 < 0
          || _M0L6_2atmpS1080 >= Moonbit_array_length(_M0L3dstS17)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS17[_M0L6_2atmpS1080] = _M0L6_2atmpS1081;
        _M0L6_2atmpS1083 = _M0L1iS24 - 1;
        _M0L1iS24 = _M0L6_2atmpS1083;
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
        int32_t _M0L6_2atmpS1085 = _M0L11dst__offsetS28 + _M0L1iS30;
        int32_t _M0L6_2atmpS1087 = _M0L11src__offsetS29 + _M0L1iS30;
        int32_t _M0L6_2atmpS1086;
        int32_t _M0L6_2atmpS1088;
        if (
          _M0L6_2atmpS1087 < 0
          || _M0L6_2atmpS1087 >= Moonbit_array_length(_M0L3srcS27)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1086 = (int32_t)_M0L3srcS27[_M0L6_2atmpS1087];
        if (
          _M0L6_2atmpS1085 < 0
          || _M0L6_2atmpS1085 >= Moonbit_array_length(_M0L3dstS26)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS26[_M0L6_2atmpS1085] = _M0L6_2atmpS1086;
        _M0L6_2atmpS1088 = _M0L1iS30 + 1;
        _M0L1iS30 = _M0L6_2atmpS1088;
        continue;
      } else {
        moonbit_decref(_M0L3srcS27);
        moonbit_decref(_M0L3dstS26);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1093 = _M0L3lenS31 - 1;
    int32_t _M0L1iS33 = _M0L6_2atmpS1093;
    while (1) {
      if (_M0L1iS33 >= 0) {
        int32_t _M0L6_2atmpS1089 = _M0L11dst__offsetS28 + _M0L1iS33;
        int32_t _M0L6_2atmpS1091 = _M0L11src__offsetS29 + _M0L1iS33;
        int32_t _M0L6_2atmpS1090;
        int32_t _M0L6_2atmpS1092;
        if (
          _M0L6_2atmpS1091 < 0
          || _M0L6_2atmpS1091 >= Moonbit_array_length(_M0L3srcS27)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1090 = (int32_t)_M0L3srcS27[_M0L6_2atmpS1091];
        if (
          _M0L6_2atmpS1089 < 0
          || _M0L6_2atmpS1089 >= Moonbit_array_length(_M0L3dstS26)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS26[_M0L6_2atmpS1089] = _M0L6_2atmpS1090;
        _M0L6_2atmpS1092 = _M0L1iS33 - 1;
        _M0L1iS33 = _M0L6_2atmpS1092;
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

int32_t _M0FPC15abort5abortGiE(moonbit_string_t _M0L3msgS2) {
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

uint16_t* _M0FPC15abort5abortGAkE(moonbit_string_t _M0L3msgS5) {
  #line 47 "C:\\Users\\31379\\.moon\\lib\\core\\abort\\abort.mbt"
  #line 49 "C:\\Users\\31379\\.moon\\lib\\core\\abort\\abort.mbt"
  moonbit_println(_M0L3msgS5);
  #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\abort\\abort.mbt"
  moonbit_panic();
}

int32_t _M0IP016_24default__implPB6Logger61write_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLoggerGRPB13StringBuilderE(
  void* _M0L11_2aobj__ptrS1025,
  struct _M0TPB4Show _M0L8_2aparamS1024
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS1023 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS1025;
  _M0IP016_24default__implPB6Logger5writeGRPB13StringBuilderE(_M0L7_2aselfS1023, _M0L8_2aparamS1024);
  return 0;
}

int32_t _M0IP016_24default__implPB6Logger84write__string__interpolation_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLoggerGRPB13StringBuilderE(
  void* _M0L11_2aobj__ptrS1022,
  struct _M0TPB4Show _M0L8_2aparamS1021
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS1020 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS1022;
  _M0IP016_24default__implPB6Logger28write__string__interpolationGRPB13StringBuilderE(_M0L7_2aselfS1020, _M0L8_2aparamS1021);
  return 0;
}

int32_t _M0IPB13StringBuilderPB6Logger67write__char_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLogger(
  void* _M0L11_2aobj__ptrS1019,
  int32_t _M0L8_2aparamS1018
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS1017 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS1019;
  _M0IPB13StringBuilderPB6Logger11write__char(_M0L7_2aselfS1017, _M0L8_2aparamS1018);
  return 0;
}

int32_t _M0IPB13StringBuilderPB6Logger67write__view_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLogger(
  void* _M0L11_2aobj__ptrS1016,
  struct _M0TPC16string10StringView _M0L8_2aparamS1015
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS1014 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS1016;
  _M0IPB13StringBuilderPB6Logger11write__view(_M0L7_2aselfS1014, _M0L8_2aparamS1015);
  return 0;
}

int32_t _M0IP016_24default__implPB6Logger72write__substring_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLoggerGRPB13StringBuilderE(
  void* _M0L11_2aobj__ptrS1013,
  moonbit_string_t _M0L8_2aparamS1010,
  int32_t _M0L8_2aparamS1011,
  int32_t _M0L8_2aparamS1012
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS1009 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS1013;
  _M0IP016_24default__implPB6Logger16write__substringGRPB13StringBuilderE(_M0L7_2aselfS1009, _M0L8_2aparamS1010, _M0L8_2aparamS1011, _M0L8_2aparamS1012);
  return 0;
}

int32_t _M0IPB13StringBuilderPB6Logger69write__string_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLogger(
  void* _M0L11_2aobj__ptrS1008,
  moonbit_string_t _M0L8_2aparamS1007
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS1006 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS1008;
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L7_2aselfS1006, _M0L8_2aparamS1007);
  return 0;
}

void moonbit_init() {
  moonbit_layout_table = moonbit_layout_table_data;
  int64_t _tmp_2371 = 9218868437227405311ll;
  int64_t _tmp_2372;
  int64_t _tmp_2373;
  int64_t _tmp_2374;
  int64_t _tmp_2375;
  _M0FPB18double__max__value = *(double*)&_tmp_2371;
  _tmp_2372 = -4503599627370497ll;
  _M0FPB18double__min__value = *(double*)&_tmp_2372;
  _tmp_2373 = 9221120237041090561ll;
  _M0FPC16double14not__a__number = *(double*)&_tmp_2373;
  _tmp_2374 = -4503599627370496ll;
  _M0FPC16double13neg__infinity = *(double*)&_tmp_2374;
  _tmp_2375 = 4503599627370496ll;
  _M0FPC16double13min__positive = *(double*)&_tmp_2375;
}

int main(int argc, char** argv) {
  int32_t _M0L6n__excS989;
  int32_t _M0L6n__inhS990;
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS992;
  struct _M0TP26RiantR8snn__mbt11IFParameter* _M0L10exc__paramS993;
  struct _M0TP26RiantR8snn__mbt2IF* _M0L8exc__popS994;
  struct _M0TP26RiantR8snn__mbt11IFParameter* _M0L10inh__paramS995;
  struct _M0TP26RiantR8snn__mbt2IF* _M0L8inh__popS996;
  struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0L2eeS997;
  struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0L2eiS998;
  struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0L2ieS999;
  struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0L2iiS1000;
  struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS1029;
  int32_t _M0L6_2atmpS1028;
  moonbit_string_t _M0L6_2atmpS1027;
  moonbit_string_t _M0L6_2atmpS1026;
  struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS1033;
  int32_t _M0L6_2atmpS1032;
  moonbit_string_t _M0L6_2atmpS1031;
  moonbit_string_t _M0L6_2atmpS1030;
  struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS1037;
  int32_t _M0L6_2atmpS1036;
  moonbit_string_t _M0L6_2atmpS1035;
  moonbit_string_t _M0L6_2atmpS1034;
  struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS1041;
  int32_t _M0L6_2atmpS1040;
  moonbit_string_t _M0L6_2atmpS1039;
  moonbit_string_t _M0L6_2atmpS1038;
  struct _M0TP26RiantR8snn__mbt2IF** _M0L6_2atmpS1066;
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt2IFE* _M0L6_2atmpS1059;
  struct _M0TP26RiantR8snn__mbt14SpikingSynapse** _M0L6_2atmpS1065;
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt14SpikingSynapseE* _M0L6_2atmpS1060;
  struct _M0TP26RiantR8snn__mbt7Monitor* _M0L6_2atmpS1063;
  struct _M0TP26RiantR8snn__mbt7Monitor* _M0L6_2atmpS1064;
  struct _M0TP26RiantR8snn__mbt7Monitor** _M0L6_2atmpS1062;
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt7MonitorE* _M0L6_2atmpS1061;
  struct _M0TP26RiantR8snn__mbt5Model* _M0L5modelS1001;
  float _M0L6_2atmpS1042;
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt7MonitorE* _M0L8monitorsS1058;
  struct _M0TP26RiantR8snn__mbt7Monitor* _M0L6_2atmpS1057;
  int32_t _M0L9e__spikesS1002;
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt7MonitorE* _M0L8monitorsS1056;
  int32_t _M0L6_2acntS2211;
  struct _M0TP26RiantR8snn__mbt7Monitor* _M0L6_2atmpS1055;
  int32_t _M0L9i__spikesS1003;
  struct _M0TPB5ArrayGfE* _M0L1vS1046;
  int32_t _M0L6_2acntS2215;
  float _M0L6_2atmpS1045;
  moonbit_string_t _M0L6_2atmpS1044;
  moonbit_string_t _M0L6_2atmpS1043;
  struct _M0TPB5ArrayGfE* _M0L1vS1050;
  int32_t _M0L6_2acntS2232;
  float _M0L6_2atmpS1049;
  moonbit_string_t _M0L6_2atmpS1048;
  moonbit_string_t _M0L6_2atmpS1047;
  moonbit_string_t _M0L6_2atmpS1052;
  moonbit_string_t _M0L6_2atmpS1051;
  moonbit_string_t _M0L6_2atmpS1054;
  moonbit_string_t _M0L6_2atmpS1053;
  moonbit_runtime_init(argc, argv);
  moonbit_init();
  _M0L6n__excS989 = 32;
  _M0L6n__inhS990 = 8;
  #line 22 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_net\\main.mbt"
  _M0L3rngS992 = _M0MP26RiantR8snn__mbt7Xoshiro7default();
  #line 25 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_net\\main.mbt"
  _M0L10exc__paramS993
  = _M0MP26RiantR8snn__mbt11IFParameter8with__el(-0x1.88p+5f);
  #line 26 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_net\\main.mbt"
  _M0L8exc__popS994
  = _M0MP26RiantR8snn__mbt2IF3new(_M0L6n__excS989, _M0L10exc__paramS993, _M0L3rngS992);
  moonbit_decref(_M0L10exc__paramS993);
  #line 27 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_net\\main.mbt"
  _M0L10inh__paramS995
  = _M0MP26RiantR8snn__mbt11IFParameter8with__el(-0x1.ep+5f);
  #line 28 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_net\\main.mbt"
  _M0L8inh__popS996
  = _M0MP26RiantR8snn__mbt2IF3new(_M0L6n__inhS990, _M0L10inh__paramS995, _M0L3rngS992);
  moonbit_decref(_M0L10inh__paramS995);
  #line 31 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_net\\main.mbt"
  _M0L2eeS997
  = _M0MP26RiantR8snn__mbt14SpikingSynapse6random(_M0L8exc__popS994, _M0L8exc__popS994, (moonbit_string_t)moonbit_string_literal_1.data, 0x1.999999999999ap-3f, 0x0p+0f, 0x1.999999999999ap-3f, _M0L3rngS992);
  #line 32 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_net\\main.mbt"
  _M0L2eiS998
  = _M0MP26RiantR8snn__mbt14SpikingSynapse6random(_M0L8exc__popS994, _M0L8inh__popS996, (moonbit_string_t)moonbit_string_literal_1.data, 0x1p-1f, 0x0p+0f, 0x1.999999999999ap-3f, _M0L3rngS992);
  #line 33 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_net\\main.mbt"
  _M0L2ieS999
  = _M0MP26RiantR8snn__mbt14SpikingSynapse6random(_M0L8inh__popS996, _M0L8exc__popS994, (moonbit_string_t)moonbit_string_literal_20.data, 0x1p+1f, 0x0p+0f, 0x1.999999999999ap-3f, _M0L3rngS992);
  #line 34 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_net\\main.mbt"
  _M0L2iiS1000
  = _M0MP26RiantR8snn__mbt14SpikingSynapse6random(_M0L8inh__popS996, _M0L8inh__popS996, (moonbit_string_t)moonbit_string_literal_20.data, 0x1p+1f, 0x0p+0f, 0x1.999999999999ap-3f, _M0L3rngS992);
  moonbit_decref(_M0L3rngS992);
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_net\\main.mbt"
  _M0FPB7printlnGsE((moonbit_string_t)moonbit_string_literal_21.data);
  _M0L6matrixS1029 = _M0L2eeS997->$4;
  moonbit_incref(_M0L6matrixS1029);
  #line 36 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_net\\main.mbt"
  _M0L6_2atmpS1028
  = _M0MP26RiantR8snn__mbt15SparseMatrixCSR3nnz(_M0L6matrixS1029);
  moonbit_decref(_M0L6matrixS1029);
  #line 36 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_net\\main.mbt"
  _M0L6_2atmpS1027
  = _M0MPC13int3Int18to__string_2einner(_M0L6_2atmpS1028, 10);
  #line 36 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_net\\main.mbt"
  _M0L6_2atmpS1026
  = moonbit_add_string((moonbit_string_t)moonbit_string_literal_22.data, _M0L6_2atmpS1027);
  moonbit_decref(_M0L6_2atmpS1027);
  #line 36 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_net\\main.mbt"
  _M0FPB7printlnGsE(_M0L6_2atmpS1026);
  moonbit_decref(_M0L6_2atmpS1026);
  _M0L6matrixS1033 = _M0L2eiS998->$4;
  moonbit_incref(_M0L6matrixS1033);
  #line 37 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_net\\main.mbt"
  _M0L6_2atmpS1032
  = _M0MP26RiantR8snn__mbt15SparseMatrixCSR3nnz(_M0L6matrixS1033);
  moonbit_decref(_M0L6matrixS1033);
  #line 37 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_net\\main.mbt"
  _M0L6_2atmpS1031
  = _M0MPC13int3Int18to__string_2einner(_M0L6_2atmpS1032, 10);
  #line 37 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_net\\main.mbt"
  _M0L6_2atmpS1030
  = moonbit_add_string((moonbit_string_t)moonbit_string_literal_23.data, _M0L6_2atmpS1031);
  moonbit_decref(_M0L6_2atmpS1031);
  #line 37 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_net\\main.mbt"
  _M0FPB7printlnGsE(_M0L6_2atmpS1030);
  moonbit_decref(_M0L6_2atmpS1030);
  _M0L6matrixS1037 = _M0L2ieS999->$4;
  moonbit_incref(_M0L6matrixS1037);
  #line 38 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_net\\main.mbt"
  _M0L6_2atmpS1036
  = _M0MP26RiantR8snn__mbt15SparseMatrixCSR3nnz(_M0L6matrixS1037);
  moonbit_decref(_M0L6matrixS1037);
  #line 38 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_net\\main.mbt"
  _M0L6_2atmpS1035
  = _M0MPC13int3Int18to__string_2einner(_M0L6_2atmpS1036, 10);
  #line 38 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_net\\main.mbt"
  _M0L6_2atmpS1034
  = moonbit_add_string((moonbit_string_t)moonbit_string_literal_24.data, _M0L6_2atmpS1035);
  moonbit_decref(_M0L6_2atmpS1035);
  #line 38 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_net\\main.mbt"
  _M0FPB7printlnGsE(_M0L6_2atmpS1034);
  moonbit_decref(_M0L6_2atmpS1034);
  _M0L6matrixS1041 = _M0L2iiS1000->$4;
  moonbit_incref(_M0L6matrixS1041);
  #line 39 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_net\\main.mbt"
  _M0L6_2atmpS1040
  = _M0MP26RiantR8snn__mbt15SparseMatrixCSR3nnz(_M0L6matrixS1041);
  moonbit_decref(_M0L6matrixS1041);
  #line 39 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_net\\main.mbt"
  _M0L6_2atmpS1039
  = _M0MPC13int3Int18to__string_2einner(_M0L6_2atmpS1040, 10);
  #line 39 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_net\\main.mbt"
  _M0L6_2atmpS1038
  = moonbit_add_string((moonbit_string_t)moonbit_string_literal_25.data, _M0L6_2atmpS1039);
  moonbit_decref(_M0L6_2atmpS1039);
  #line 39 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_net\\main.mbt"
  _M0FPB7printlnGsE(_M0L6_2atmpS1038);
  moonbit_decref(_M0L6_2atmpS1038);
  moonbit_incref(_M0L8exc__popS994);
  moonbit_incref(_M0L8inh__popS996);
  _M0L6_2atmpS1066
  = (struct _M0TP26RiantR8snn__mbt2IF**)moonbit_make_ref_array_raw(2);
  _M0L6_2atmpS1066[0] = _M0L8exc__popS994;
  _M0L6_2atmpS1066[1] = _M0L8inh__popS996;
  _M0L6_2atmpS1059
  = (struct _M0TPB5ArrayGRP26RiantR8snn__mbt2IFE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGRP26RiantR8snn__mbt2IFE));
  Moonbit_object_header(_M0L6_2atmpS1059)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 60, 0);
  _M0L6_2atmpS1059->$0 = _M0L6_2atmpS1066;
  _M0L6_2atmpS1059->$1 = 2;
  _M0L6_2atmpS1065
  = (struct _M0TP26RiantR8snn__mbt14SpikingSynapse**)moonbit_make_ref_array_raw(4);
  _M0L6_2atmpS1065[0] = _M0L2eeS997;
  _M0L6_2atmpS1065[1] = _M0L2eiS998;
  _M0L6_2atmpS1065[2] = _M0L2ieS999;
  _M0L6_2atmpS1065[3] = _M0L2iiS1000;
  _M0L6_2atmpS1060
  = (struct _M0TPB5ArrayGRP26RiantR8snn__mbt14SpikingSynapseE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGRP26RiantR8snn__mbt14SpikingSynapseE));
  Moonbit_object_header(_M0L6_2atmpS1060)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 63, 0);
  _M0L6_2atmpS1060->$0 = _M0L6_2atmpS1065;
  _M0L6_2atmpS1060->$1 = 4;
  #line 47 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_net\\main.mbt"
  _M0L6_2atmpS1063
  = _M0MP26RiantR8snn__mbt7Monitor9new__fire(_M0L8exc__popS994, 0);
  #line 48 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_net\\main.mbt"
  _M0L6_2atmpS1064
  = _M0MP26RiantR8snn__mbt7Monitor9new__fire(_M0L8inh__popS996, 0);
  _M0L6_2atmpS1062
  = (struct _M0TP26RiantR8snn__mbt7Monitor**)moonbit_make_ref_array_raw(2);
  _M0L6_2atmpS1062[0] = _M0L6_2atmpS1063;
  _M0L6_2atmpS1062[1] = _M0L6_2atmpS1064;
  _M0L6_2atmpS1061
  = (struct _M0TPB5ArrayGRP26RiantR8snn__mbt7MonitorE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGRP26RiantR8snn__mbt7MonitorE));
  Moonbit_object_header(_M0L6_2atmpS1061)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 66, 0);
  _M0L6_2atmpS1061->$0 = _M0L6_2atmpS1062;
  _M0L6_2atmpS1061->$1 = 2;
  _M0L5modelS1001
  = (struct _M0TP26RiantR8snn__mbt5Model*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt5Model));
  Moonbit_object_header(_M0L5modelS1001)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 69, 0);
  _M0L5modelS1001->$0 = _M0L6_2atmpS1059;
  _M0L5modelS1001->$1 = _M0L6_2atmpS1060;
  _M0L5modelS1001->$2 = _M0L6_2atmpS1061;
  _M0L6_2atmpS1042 = 0x1.9p+6f * _M0FP26RiantR8snn__mbt2ms;
  #line 51 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_net\\main.mbt"
  _M0FP26RiantR8snn__mbt8sim__for(_M0L5modelS1001, _M0L6_2atmpS1042);
  _M0L8monitorsS1058 = _M0L5modelS1001->$2;
  moonbit_incref(_M0L8monitorsS1058);
  #line 54 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_net\\main.mbt"
  _M0L6_2atmpS1057
  = _M0MPC15array5Array2atGRP26RiantR8snn__mbt7MonitorE(_M0L8monitorsS1058, 0);
  moonbit_decref(_M0L8monitorsS1058);
  #line 54 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_net\\main.mbt"
  _M0L9e__spikesS1002
  = _M0MP26RiantR8snn__mbt7Monitor13count__spikes(_M0L6_2atmpS1057);
  moonbit_decref(_M0L6_2atmpS1057);
  _M0L8monitorsS1056 = _M0L5modelS1001->$2;
  _M0L6_2acntS2211 = Moonbit_rc_count(Moonbit_object_header(_M0L5modelS1001));
  if (_M0L6_2acntS2211 > 1) {
    int32_t _M0L11_2anew__cntS2214 = _M0L6_2acntS2211 - 1;
    Moonbit_set_rc_count(Moonbit_object_header(_M0L5modelS1001), _M0L11_2anew__cntS2214);
    moonbit_incref(_M0L8monitorsS1056);
  } else if (_M0L6_2acntS2211 == 1) {
    struct _M0TPB5ArrayGRP26RiantR8snn__mbt14SpikingSynapseE* _M0L8_2afieldS2213 =
      _M0L5modelS1001->$1;
    struct _M0TPB5ArrayGRP26RiantR8snn__mbt2IFE* _M0L8_2afieldS2212;
    moonbit_decref(_M0L8_2afieldS2213);
    _M0L8_2afieldS2212 = _M0L5modelS1001->$0;
    moonbit_decref(_M0L8_2afieldS2212);
    #line 54 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_net\\main.mbt"
    moonbit_free(_M0L5modelS1001);
  }
  #line 55 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_net\\main.mbt"
  _M0L6_2atmpS1055
  = _M0MPC15array5Array2atGRP26RiantR8snn__mbt7MonitorE(_M0L8monitorsS1056, 1);
  moonbit_decref(_M0L8monitorsS1056);
  #line 55 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_net\\main.mbt"
  _M0L9i__spikesS1003
  = _M0MP26RiantR8snn__mbt7Monitor13count__spikes(_M0L6_2atmpS1055);
  moonbit_decref(_M0L6_2atmpS1055);
  #line 56 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_net\\main.mbt"
  _M0FPB7printlnGsE((moonbit_string_t)moonbit_string_literal_26.data);
  _M0L1vS1046 = _M0L8exc__popS994->$3;
  _M0L6_2acntS2215
  = Moonbit_rc_count(Moonbit_object_header(_M0L8exc__popS994));
  if (_M0L6_2acntS2215 > 1) {
    int32_t _M0L11_2anew__cntS2231 = _M0L6_2acntS2215 - 1;
    Moonbit_set_rc_count(Moonbit_object_header(_M0L8exc__popS994), _M0L11_2anew__cntS2231);
    moonbit_incref(_M0L1vS1046);
  } else if (_M0L6_2acntS2215 == 1) {
    struct _M0TPB5ArrayGfE* _M0L8_2afieldS2230 = _M0L8exc__popS994->$16;
    struct _M0TPB5ArrayGfE* _M0L8_2afieldS2229;
    struct _M0TPB5ArrayGfE* _M0L8_2afieldS2228;
    struct _M0TPB5ArrayGfE* _M0L8_2afieldS2227;
    struct _M0TPB5ArrayGfE* _M0L8_2afieldS2226;
    struct _M0TPB5ArrayGfE* _M0L8_2afieldS2225;
    struct _M0TPB5ArrayGfE* _M0L8_2afieldS2224;
    struct _M0TPB5ArrayGfE* _M0L8_2afieldS2223;
    struct _M0TPB5ArrayGfE* _M0L8_2afieldS2222;
    struct _M0TPB5ArrayGfE* _M0L8_2afieldS2221;
    struct _M0TPB5ArrayGiE* _M0L8_2afieldS2220;
    struct _M0TPB5ArrayGbE* _M0L8_2afieldS2219;
    struct _M0TPB5ArrayGfE* _M0L8_2afieldS2218;
    struct _M0TP26RiantR8snn__mbt9PostSpike* _M0L8_2afieldS2217;
    struct _M0TP26RiantR8snn__mbt11IFParameter* _M0L8_2afieldS2216;
    moonbit_decref(_M0L8_2afieldS2230);
    _M0L8_2afieldS2229 = _M0L8exc__popS994->$15;
    moonbit_decref(_M0L8_2afieldS2229);
    _M0L8_2afieldS2228 = _M0L8exc__popS994->$14;
    moonbit_decref(_M0L8_2afieldS2228);
    _M0L8_2afieldS2227 = _M0L8exc__popS994->$13;
    moonbit_decref(_M0L8_2afieldS2227);
    _M0L8_2afieldS2226 = _M0L8exc__popS994->$12;
    moonbit_decref(_M0L8_2afieldS2226);
    _M0L8_2afieldS2225 = _M0L8exc__popS994->$11;
    moonbit_decref(_M0L8_2afieldS2225);
    _M0L8_2afieldS2224 = _M0L8exc__popS994->$10;
    moonbit_decref(_M0L8_2afieldS2224);
    _M0L8_2afieldS2223 = _M0L8exc__popS994->$9;
    moonbit_decref(_M0L8_2afieldS2223);
    _M0L8_2afieldS2222 = _M0L8exc__popS994->$8;
    moonbit_decref(_M0L8_2afieldS2222);
    _M0L8_2afieldS2221 = _M0L8exc__popS994->$7;
    moonbit_decref(_M0L8_2afieldS2221);
    _M0L8_2afieldS2220 = _M0L8exc__popS994->$6;
    moonbit_decref(_M0L8_2afieldS2220);
    _M0L8_2afieldS2219 = _M0L8exc__popS994->$5;
    moonbit_decref(_M0L8_2afieldS2219);
    _M0L8_2afieldS2218 = _M0L8exc__popS994->$4;
    moonbit_decref(_M0L8_2afieldS2218);
    _M0L8_2afieldS2217 = _M0L8exc__popS994->$1;
    moonbit_decref(_M0L8_2afieldS2217);
    _M0L8_2afieldS2216 = _M0L8exc__popS994->$0;
    moonbit_decref(_M0L8_2afieldS2216);
    #line 55 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_net\\main.mbt"
    moonbit_free(_M0L8exc__popS994);
  }
  #line 57 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_net\\main.mbt"
  _M0L6_2atmpS1045 = _M0MPC15array5Array2atGfE(_M0L1vS1046, 0);
  moonbit_decref(_M0L1vS1046);
  #line 57 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_net\\main.mbt"
  _M0L6_2atmpS1044 = _M0IPC15float5FloatPB4Show10to__string(_M0L6_2atmpS1045);
  #line 57 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_net\\main.mbt"
  _M0L6_2atmpS1043
  = moonbit_add_string((moonbit_string_t)moonbit_string_literal_27.data, _M0L6_2atmpS1044);
  moonbit_decref(_M0L6_2atmpS1044);
  #line 57 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_net\\main.mbt"
  _M0FPB7printlnGsE(_M0L6_2atmpS1043);
  moonbit_decref(_M0L6_2atmpS1043);
  _M0L1vS1050 = _M0L8inh__popS996->$3;
  _M0L6_2acntS2232
  = Moonbit_rc_count(Moonbit_object_header(_M0L8inh__popS996));
  if (_M0L6_2acntS2232 > 1) {
    int32_t _M0L11_2anew__cntS2248 = _M0L6_2acntS2232 - 1;
    Moonbit_set_rc_count(Moonbit_object_header(_M0L8inh__popS996), _M0L11_2anew__cntS2248);
    moonbit_incref(_M0L1vS1050);
  } else if (_M0L6_2acntS2232 == 1) {
    struct _M0TPB5ArrayGfE* _M0L8_2afieldS2247 = _M0L8inh__popS996->$16;
    struct _M0TPB5ArrayGfE* _M0L8_2afieldS2246;
    struct _M0TPB5ArrayGfE* _M0L8_2afieldS2245;
    struct _M0TPB5ArrayGfE* _M0L8_2afieldS2244;
    struct _M0TPB5ArrayGfE* _M0L8_2afieldS2243;
    struct _M0TPB5ArrayGfE* _M0L8_2afieldS2242;
    struct _M0TPB5ArrayGfE* _M0L8_2afieldS2241;
    struct _M0TPB5ArrayGfE* _M0L8_2afieldS2240;
    struct _M0TPB5ArrayGfE* _M0L8_2afieldS2239;
    struct _M0TPB5ArrayGfE* _M0L8_2afieldS2238;
    struct _M0TPB5ArrayGiE* _M0L8_2afieldS2237;
    struct _M0TPB5ArrayGbE* _M0L8_2afieldS2236;
    struct _M0TPB5ArrayGfE* _M0L8_2afieldS2235;
    struct _M0TP26RiantR8snn__mbt9PostSpike* _M0L8_2afieldS2234;
    struct _M0TP26RiantR8snn__mbt11IFParameter* _M0L8_2afieldS2233;
    moonbit_decref(_M0L8_2afieldS2247);
    _M0L8_2afieldS2246 = _M0L8inh__popS996->$15;
    moonbit_decref(_M0L8_2afieldS2246);
    _M0L8_2afieldS2245 = _M0L8inh__popS996->$14;
    moonbit_decref(_M0L8_2afieldS2245);
    _M0L8_2afieldS2244 = _M0L8inh__popS996->$13;
    moonbit_decref(_M0L8_2afieldS2244);
    _M0L8_2afieldS2243 = _M0L8inh__popS996->$12;
    moonbit_decref(_M0L8_2afieldS2243);
    _M0L8_2afieldS2242 = _M0L8inh__popS996->$11;
    moonbit_decref(_M0L8_2afieldS2242);
    _M0L8_2afieldS2241 = _M0L8inh__popS996->$10;
    moonbit_decref(_M0L8_2afieldS2241);
    _M0L8_2afieldS2240 = _M0L8inh__popS996->$9;
    moonbit_decref(_M0L8_2afieldS2240);
    _M0L8_2afieldS2239 = _M0L8inh__popS996->$8;
    moonbit_decref(_M0L8_2afieldS2239);
    _M0L8_2afieldS2238 = _M0L8inh__popS996->$7;
    moonbit_decref(_M0L8_2afieldS2238);
    _M0L8_2afieldS2237 = _M0L8inh__popS996->$6;
    moonbit_decref(_M0L8_2afieldS2237);
    _M0L8_2afieldS2236 = _M0L8inh__popS996->$5;
    moonbit_decref(_M0L8_2afieldS2236);
    _M0L8_2afieldS2235 = _M0L8inh__popS996->$4;
    moonbit_decref(_M0L8_2afieldS2235);
    _M0L8_2afieldS2234 = _M0L8inh__popS996->$1;
    moonbit_decref(_M0L8_2afieldS2234);
    _M0L8_2afieldS2233 = _M0L8inh__popS996->$0;
    moonbit_decref(_M0L8_2afieldS2233);
    #line 55 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_net\\main.mbt"
    moonbit_free(_M0L8inh__popS996);
  }
  #line 58 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_net\\main.mbt"
  _M0L6_2atmpS1049 = _M0MPC15array5Array2atGfE(_M0L1vS1050, 0);
  moonbit_decref(_M0L1vS1050);
  #line 58 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_net\\main.mbt"
  _M0L6_2atmpS1048 = _M0IPC15float5FloatPB4Show10to__string(_M0L6_2atmpS1049);
  #line 58 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_net\\main.mbt"
  _M0L6_2atmpS1047
  = moonbit_add_string((moonbit_string_t)moonbit_string_literal_28.data, _M0L6_2atmpS1048);
  moonbit_decref(_M0L6_2atmpS1048);
  #line 58 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_net\\main.mbt"
  _M0FPB7printlnGsE(_M0L6_2atmpS1047);
  moonbit_decref(_M0L6_2atmpS1047);
  #line 59 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_net\\main.mbt"
  _M0L6_2atmpS1052
  = _M0MPC13int3Int18to__string_2einner(_M0L9e__spikesS1002, 10);
  #line 59 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_net\\main.mbt"
  _M0L6_2atmpS1051
  = moonbit_add_string((moonbit_string_t)moonbit_string_literal_29.data, _M0L6_2atmpS1052);
  moonbit_decref(_M0L6_2atmpS1052);
  #line 59 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_net\\main.mbt"
  _M0FPB7printlnGsE(_M0L6_2atmpS1051);
  moonbit_decref(_M0L6_2atmpS1051);
  #line 60 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_net\\main.mbt"
  _M0L6_2atmpS1054
  = _M0MPC13int3Int18to__string_2einner(_M0L9i__spikesS1003, 10);
  #line 60 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_net\\main.mbt"
  _M0L6_2atmpS1053
  = moonbit_add_string((moonbit_string_t)moonbit_string_literal_30.data, _M0L6_2atmpS1054);
  moonbit_decref(_M0L6_2atmpS1054);
  #line 60 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_net\\main.mbt"
  _M0FPB7printlnGsE(_M0L6_2atmpS1053);
  moonbit_decref(_M0L6_2atmpS1053);
  return 0;
}