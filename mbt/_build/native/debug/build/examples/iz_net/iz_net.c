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

struct _M0TPB4Show;

struct _M0TPB13StringBuilder;

struct _M0TPB5ArrayGfE;

struct _M0TPB17FloatingDecimal64;

struct _M0TP26RiantR8snn__mbt11IZParameter;

struct _M0TUdiE;

struct _M0TPB5ArrayGbE;

struct _M0TPB5ArrayGRPB5ArrayGfEE;

struct _M0BTPB6Logger;

struct _M0BTPB4Show;

struct _M0TP26RiantR8snn__mbt16SpikingSynapseIZ;

struct _M0TPC16string10StringView;

struct _M0KTPB6LoggerTPB13StringBuilder;

struct _M0TPB6Logger;

struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR;

struct _M0TP26RiantR8snn__mbt7Xoshiro;

struct _M0TUmmmmE;

struct _M0TP26RiantR8snn__mbt2IZ;

struct _M0TPB5ArrayGiE;

struct _M0TPB19MulShiftAll64Result;

struct _M0TUddE;

struct _M0TPB7Umul128;

struct _M0TPB8Pow5Pair;

struct _M0TPB8MutLocalGiE {
  int32_t $0;
  
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

struct _M0TP26RiantR8snn__mbt11IZParameter {
  float $0;
  float $1;
  float $2;
  float $3;
  float $4;
  float $5;
  float $6;
  float $7;
  
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

struct _M0TP26RiantR8snn__mbt16SpikingSynapseIZ {
  struct _M0TP26RiantR8snn__mbt2IZ* $0;
  struct _M0TP26RiantR8snn__mbt2IZ* $1;
  moonbit_string_t $2;
  struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* $3;
  
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

struct _M0TP26RiantR8snn__mbt2IZ {
  struct _M0TP26RiantR8snn__mbt11IZParameter* $0;
  int32_t $1;
  struct _M0TPB5ArrayGfE* $2;
  struct _M0TPB5ArrayGfE* $3;
  struct _M0TPB5ArrayGbE* $4;
  struct _M0TPB5ArrayGfE* $5;
  struct _M0TPB5ArrayGfE* $6;
  struct _M0TPB5ArrayGfE* $7;
  
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

int32_t _M0FP26RiantR8snn__mbt20forward__iz__synapse(
  struct _M0TP26RiantR8snn__mbt16SpikingSynapseIZ*
);

struct _M0TP26RiantR8snn__mbt16SpikingSynapseIZ* _M0MP26RiantR8snn__mbt16SpikingSynapseIZ6random(
  struct _M0TP26RiantR8snn__mbt2IZ*,
  struct _M0TP26RiantR8snn__mbt2IZ*,
  moonbit_string_t,
  float,
  float,
  float,
  struct _M0TP26RiantR8snn__mbt7Xoshiro*
);

struct _M0TP26RiantR8snn__mbt2IZ* _M0MP26RiantR8snn__mbt2IZ3new(
  int32_t,
  struct _M0TP26RiantR8snn__mbt11IZParameter*,
  struct _M0TP26RiantR8snn__mbt7Xoshiro*
);

struct _M0TP26RiantR8snn__mbt11IZParameter* _M0MP26RiantR8snn__mbt11IZParameter2fs(
  
);

struct _M0TP26RiantR8snn__mbt11IZParameter* _M0MP26RiantR8snn__mbt11IZParameter2rs(
  
);

int32_t _M0FP26RiantR8snn__mbt8step__iz(
  struct _M0TP26RiantR8snn__mbt2IZ*,
  float
);

struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0MP26RiantR8snn__mbt7Xoshiro7default(
  
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

double _M0FPC14math2ln(double);

#define _M0FPC14math3cos cos

#define _M0FPC14math3sin sin

struct _M0TUdiE* _M0FPC14math5frexp(double);

struct _M0TUdiE* _M0FPC14math9normalize(double);

moonbit_string_t _M0IPC15float5FloatPB4Show10to__string(float);

int32_t _M0MPC15float5Float7to__int(float);

struct _M0TPB5ArrayGfE* _M0MPC15array5Array4makeGfE(int32_t, float);

struct _M0TPB5ArrayGbE* _M0MPC15array5Array4makeGbE(int32_t, int32_t);

struct _M0TPB5ArrayGRPB5ArrayGfEE* _M0MPC15array5Array4makeGRPB5ArrayGfEE(
  int32_t,
  struct _M0TPB5ArrayGfE*
);

struct _M0TPB5ArrayGiE* _M0MPC15array5Array4makeGiE(int32_t, int32_t);

int32_t _M0MPC15array5Array3setGfE(struct _M0TPB5ArrayGfE*, int32_t, float);

int32_t _M0MPC15array5Array3setGbE(struct _M0TPB5ArrayGbE*, int32_t, int32_t);

int32_t _M0MPC15array5Array3setGRPB5ArrayGfEE(
  struct _M0TPB5ArrayGRPB5ArrayGfEE*,
  int32_t,
  struct _M0TPB5ArrayGfE*
);

int32_t _M0MPC15array5Array3setGiE(struct _M0TPB5ArrayGiE*, int32_t, int32_t);

float _M0MPC15array5Array2atGfE(struct _M0TPB5ArrayGfE*, int32_t);

int32_t _M0MPC15array5Array2atGbE(struct _M0TPB5ArrayGbE*, int32_t);

int32_t _M0MPC15array5Array2atGiE(struct _M0TPB5ArrayGiE*, int32_t);

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

struct _M0TPB5ArrayGRPB5ArrayGfEE* _M0MPC15array5Array20unsafe__make__uninitGRPB5ArrayGfEE(
  int32_t
);

struct _M0TPB5ArrayGiE* _M0MPC15array5Array20unsafe__make__uninitGiE(int32_t);

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

struct { int32_t rc; uint32_t meta; uint16_t const data[1]; 
} const moonbit_string_literal_5 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 0, 0};

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
} const moonbit_string_literal_4 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 1, 45, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[31]; 
} const moonbit_string_literal_9 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 30, 114, 97, 
    100, 105, 120, 32, 109, 117, 115, 116, 32, 98, 101, 32, 98, 101, 
    116, 119, 101, 101, 110, 32, 50, 32, 97, 110, 100, 32, 51, 54, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[9]; 
} const moonbit_string_literal_6 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 8, 73, 110, 
    102, 105, 110, 105, 116, 121, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[4]; 
} const moonbit_string_literal_3 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 3, 78, 97, 78, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[19]; 
} const moonbit_string_literal_25 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 18, 32, 32, 
    73, 69, 32, 99, 111, 110, 110, 101, 99, 116, 105, 111, 110, 115, 
    58, 32, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[6]; 
} const moonbit_string_literal_20 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 5, 32, 32, 
    69, 58, 32, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[2]; 
} const moonbit_string_literal_1 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 1, 48, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[9]; 
} const moonbit_string_literal_16 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 8, 44, 32, 
    108, 101, 110, 32, 61, 32, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[37]; 
} const moonbit_string_literal_13 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 36, 98, 111, 
    117, 110, 100, 115, 32, 99, 104, 101, 99, 107, 32, 102, 97, 105, 
    108, 101, 100, 58, 32, 97, 108, 108, 111, 99, 97, 116, 101, 95, 108, 
    101, 110, 32, 61, 32, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[12]; 
} const moonbit_string_literal_28 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 11, 32, 32, 
    69, 32, 118, 91, 48, 93, 32, 61, 32, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[19]; 
} const moonbit_string_literal_23 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 18, 32, 32, 
    69, 69, 32, 99, 111, 110, 110, 101, 99, 116, 105, 111, 110, 115, 
    58, 32, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[3]; 
} const moonbit_string_literal_0 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 2, 103, 101, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[3]; 
} const moonbit_string_literal_18 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 2, 103, 105, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[12]; 
} const moonbit_string_literal_29 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 11, 32, 32, 
    73, 32, 118, 91, 48, 93, 32, 61, 32, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[37]; 
} const moonbit_string_literal_10 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 36, 48, 49, 
    50, 51, 52, 53, 54, 55, 56, 57, 97, 98, 99, 100, 101, 102, 103, 104, 
    105, 106, 107, 108, 109, 110, 111, 112, 113, 114, 115, 116, 117, 
    118, 119, 120, 121, 122, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[36]; 
} const moonbit_string_literal_30 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 35, 32, 32, 
    69, 32, 116, 111, 116, 97, 108, 32, 115, 112, 105, 107, 101, 115, 
    32, 97, 99, 114, 111, 115, 115, 32, 97, 108, 108, 32, 115, 116, 101, 
    112, 115, 58, 32, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[16]; 
} const moonbit_string_literal_17 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 15, 44, 32, 
    115, 114, 99, 46, 108, 101, 110, 103, 116, 104, 32, 61, 32, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[19]; 
} const moonbit_string_literal_24 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 18, 32, 32, 
    69, 73, 32, 99, 111, 110, 110, 101, 99, 116, 105, 111, 110, 115, 
    58, 32, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[16]; 
} const moonbit_string_literal_14 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 15, 44, 32, 
    115, 114, 99, 95, 111, 102, 102, 115, 101, 116, 32, 61, 32, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[19]; 
} const moonbit_string_literal_26 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 18, 32, 32, 
    73, 73, 32, 99, 111, 110, 110, 101, 99, 116, 105, 111, 110, 115, 
    58, 32, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[24]; 
} const moonbit_string_literal_8 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 23, 65, 114, 
    114, 97, 121, 32, 99, 97, 112, 97, 99, 105, 116, 121, 32, 111, 118, 
    101, 114, 102, 108, 111, 119, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[33]; 
} const moonbit_string_literal_27 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 32, 105, 122, 
    95, 110, 101, 116, 46, 109, 98, 116, 58, 32, 115, 105, 109, 117, 
    108, 97, 116, 105, 111, 110, 32, 100, 111, 110, 101, 32, 40, 49, 
    115, 41, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[26]; 
} const moonbit_string_literal_2 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 25, 73, 108, 
    108, 101, 103, 97, 108, 65, 114, 103, 117, 109, 101, 110, 116, 69, 
    120, 99, 101, 112, 116, 105, 111, 110, 32, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[36]; 
} const moonbit_string_literal_31 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 35, 32, 32, 
    73, 32, 116, 111, 116, 97, 108, 32, 115, 112, 105, 107, 101, 115, 
    32, 97, 99, 114, 111, 115, 115, 32, 97, 108, 108, 32, 115, 116, 101, 
    112, 115, 58, 32, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[12]; 
} const moonbit_string_literal_22 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 11, 32, 73, 
    90, 32, 110, 101, 117, 114, 111, 110, 115, 0
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
} const moonbit_string_literal_7 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 3, 48, 46, 48, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[60]; 
} const moonbit_string_literal_19 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 59, 105, 122, 
    95, 110, 101, 116, 46, 109, 98, 116, 58, 32, 110, 101, 116, 119, 
    111, 114, 107, 32, 98, 117, 105, 108, 116, 32, 40, 118, 48, 46, 55, 
    46, 49, 58, 32, 119, 105, 116, 104, 32, 73, 90, 32, 83, 112, 105, 
    107, 105, 110, 103, 83, 121, 110, 97, 112, 115, 101, 115, 41, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[17]; 
} const moonbit_string_literal_21 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 16, 32, 73, 
    90, 32, 110, 101, 117, 114, 111, 110, 115, 44, 32, 73, 58, 32, 0
  };

uint32_t const moonbit_layout_table_data[35] =
  {
    sizeof(struct _M0TP26RiantR8snn__mbt16SpikingSynapseIZ) / 4, 4,
    offsetof(struct _M0TP26RiantR8snn__mbt16SpikingSynapseIZ, $0) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt16SpikingSynapseIZ, $1) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt16SpikingSynapseIZ, $2) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt16SpikingSynapseIZ, $3) / 4 * 2,
    sizeof(struct _M0TP26RiantR8snn__mbt2IZ) / 4, 7,
    offsetof(struct _M0TP26RiantR8snn__mbt2IZ, $0) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt2IZ, $2) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt2IZ, $3) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt2IZ, $4) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt2IZ, $5) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt2IZ, $6) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt2IZ, $7) / 4 * 2,
    sizeof(struct _M0TPB5ArrayGfE) / 4, 1,
    offsetof(struct _M0TPB5ArrayGfE, $0) / 4 * 2,
    sizeof(struct _M0TPB5ArrayGiE) / 4, 1,
    offsetof(struct _M0TPB5ArrayGiE, $0) / 4 * 2,
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

int32_t _M0FP26RiantR8snn__mbt20forward__iz__synapse(
  struct _M0TP26RiantR8snn__mbt16SpikingSynapseIZ* _M0L1cS865
) {
  moonbit_string_t _M0L3symS1891;
  struct _M0TPB5ArrayGfE* _M0L6targetS864;
  struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS1888;
  struct _M0TP26RiantR8snn__mbt2IZ* _M0L3preS1890;
  struct _M0TPB5ArrayGbE* _M0L4fireS1889;
  #line 57 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking_iz.mbt"
  _M0L3symS1891 = _M0L1cS865->$2;
  #line 58 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking_iz.mbt"
  if (
    _M0L3symS1891 == (moonbit_string_t)moonbit_string_literal_0.data
    || Moonbit_array_length(_M0L3symS1891)
       == Moonbit_array_length((moonbit_string_t)moonbit_string_literal_0.data)
       && 0
          == memcmp(_M0L3symS1891, (moonbit_string_t)moonbit_string_literal_0.data, Moonbit_array_length(_M0L3symS1891) * 2)
  ) {
    struct _M0TP26RiantR8snn__mbt2IZ* _M0L4postS1892 = _M0L1cS865->$1;
    struct _M0TPB5ArrayGfE* _M0L8_2afieldS1894 = _M0L4postS1892->$6;
    moonbit_incref(_M0L8_2afieldS1894);
    _M0L6targetS864 = _M0L8_2afieldS1894;
  } else {
    struct _M0TP26RiantR8snn__mbt2IZ* _M0L4postS1893 = _M0L1cS865->$1;
    struct _M0TPB5ArrayGfE* _M0L8_2afieldS1895 = _M0L4postS1893->$7;
    moonbit_incref(_M0L8_2afieldS1895);
    _M0L6targetS864 = _M0L8_2afieldS1895;
  }
  _M0L6matrixS1888 = _M0L1cS865->$3;
  _M0L3preS1890 = _M0L1cS865->$0;
  _M0L4fireS1889 = _M0L3preS1890->$4;
  #line 59 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking_iz.mbt"
  _M0MP26RiantR8snn__mbt15SparseMatrixCSR7forward(_M0L6matrixS1888, _M0L4fireS1889, _M0L6targetS864);
  moonbit_decref(_M0L6targetS864);
  return 0;
}

struct _M0TP26RiantR8snn__mbt16SpikingSynapseIZ* _M0MP26RiantR8snn__mbt16SpikingSynapseIZ6random(
  struct _M0TP26RiantR8snn__mbt2IZ* _M0L3preS857,
  struct _M0TP26RiantR8snn__mbt2IZ* _M0L4postS858,
  moonbit_string_t _M0L3symS863,
  float _M0L2muS859,
  float _M0L5sigmaS860,
  float _M0L1pS861,
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS862
) {
  int32_t _M0L1nS1886;
  int32_t _M0L1nS1887;
  struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS856;
  struct _M0TP26RiantR8snn__mbt16SpikingSynapseIZ* _block_1924;
  #line 34 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking_iz.mbt"
  _M0L1nS1886 = _M0L3preS857->$1;
  _M0L1nS1887 = _M0L4postS858->$1;
  #line 43 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking_iz.mbt"
  _M0L6matrixS856
  = _M0MP26RiantR8snn__mbt15SparseMatrixCSR6random(_M0L1nS1886, _M0L1nS1887, _M0L2muS859, _M0L5sigmaS860, _M0L1pS861, _M0L3rngS862);
  moonbit_incref(_M0L3preS857);
  moonbit_incref(_M0L4postS858);
  moonbit_incref(_M0L3symS863);
  _block_1924
  = (struct _M0TP26RiantR8snn__mbt16SpikingSynapseIZ*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt16SpikingSynapseIZ));
  Moonbit_object_header(_block_1924)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 0, 0);
  _block_1924->$0 = _M0L3preS857;
  _block_1924->$1 = _M0L4postS858;
  _block_1924->$2 = _M0L3symS863;
  _block_1924->$3 = _M0L6matrixS856;
  return _block_1924;
}

struct _M0TP26RiantR8snn__mbt2IZ* _M0MP26RiantR8snn__mbt2IZ3new(
  int32_t _M0L1nS845,
  struct _M0TP26RiantR8snn__mbt11IZParameter* _M0L5paramS849,
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS855
) {
  struct _M0TPB5ArrayGfE* _M0L1vS844;
  struct _M0TPB5ArrayGfE* _M0L1uS846;
  int32_t _M0L7_2abindS847;
  int32_t _M0L1kS848;
  struct _M0TPB5ArrayGbE* _M0L4fireS851;
  struct _M0TPB5ArrayGfE* _M0L1iS852;
  struct _M0TPB5ArrayGfE* _M0L2geS853;
  struct _M0TPB5ArrayGfE* _M0L2giS854;
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L6_2atmpS1896;
  struct _M0TP26RiantR8snn__mbt2IZ* _block_1926;
  #line 83 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  #line 85 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  _M0L1vS844 = _M0MPC15array5Array4makeGfE(_M0L1nS845, -0x1.04p+6f);
  #line 87 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  _M0L1uS846 = _M0MPC15array5Array4makeGfE(_M0L1nS845, 0x0p+0f);
  _M0L7_2abindS847 = 0;
  _M0L1kS848 = _M0L7_2abindS847;
  while (1) {
    if (_M0L1kS848 < _M0L1nS845) {
      float _M0L1bS1883 = _M0L5paramS849->$1;
      float _M0L6_2atmpS1884;
      float _M0L6_2atmpS1882;
      int32_t _M0L6_2atmpS1885;
      #line 89 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L6_2atmpS1884 = _M0MPC15array5Array2atGfE(_M0L1vS844, _M0L1kS848);
      _M0L6_2atmpS1882 = _M0L1bS1883 * _M0L6_2atmpS1884;
      #line 89 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0MPC15array5Array3setGfE(_M0L1uS846, _M0L1kS848, _M0L6_2atmpS1882);
      _M0L6_2atmpS1885 = _M0L1kS848 + 1;
      _M0L1kS848 = _M0L6_2atmpS1885;
      continue;
    }
    break;
  }
  #line 91 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  _M0L4fireS851 = _M0MPC15array5Array4makeGbE(_M0L1nS845, 0);
  #line 92 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  _M0L1iS852 = _M0MPC15array5Array4makeGfE(_M0L1nS845, 0x0p+0f);
  #line 106 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  _M0L2geS853 = _M0MPC15array5Array4makeGfE(_M0L1nS845, 0x0p+0f);
  #line 107 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  _M0L2giS854 = _M0MPC15array5Array4makeGfE(_M0L1nS845, 0x0p+0f);
  _M0L6_2atmpS1896 = _M0L3rngS855;
  moonbit_incref(_M0L5paramS849);
  _block_1926
  = (struct _M0TP26RiantR8snn__mbt2IZ*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt2IZ));
  Moonbit_object_header(_block_1926)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 6, 0);
  _block_1926->$0 = _M0L5paramS849;
  _block_1926->$1 = _M0L1nS845;
  _block_1926->$2 = _M0L1vS844;
  _block_1926->$3 = _M0L1uS846;
  _block_1926->$4 = _M0L4fireS851;
  _block_1926->$5 = _M0L1iS852;
  _block_1926->$6 = _M0L2geS853;
  _block_1926->$7 = _M0L2giS854;
  return _block_1926;
}

struct _M0TP26RiantR8snn__mbt11IZParameter* _M0MP26RiantR8snn__mbt11IZParameter2fs(
  
) {
  struct _M0TP26RiantR8snn__mbt11IZParameter* _block_1927;
  #line 63 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  _block_1927
  = (struct _M0TP26RiantR8snn__mbt11IZParameter*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt11IZParameter));
  Moonbit_object_header(_block_1927)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_1927->$0 = 0x1.999999999999ap-4f;
  _block_1927->$1 = 0x1.999999999999ap-3f;
  _block_1927->$2 = -0x1.04p+6f;
  _block_1927->$3 = 0x1p+1f;
  _block_1927->$4 = 0x1.4p+2f;
  _block_1927->$5 = 0x1.4p+3f;
  _block_1927->$6 = 0x0p+0f;
  _block_1927->$7 = -0x1.4p+6f;
  return _block_1927;
}

struct _M0TP26RiantR8snn__mbt11IZParameter* _M0MP26RiantR8snn__mbt11IZParameter2rs(
  
) {
  struct _M0TP26RiantR8snn__mbt11IZParameter* _block_1928;
  #line 55 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  _block_1928
  = (struct _M0TP26RiantR8snn__mbt11IZParameter*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt11IZParameter));
  Moonbit_object_header(_block_1928)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_1928->$0 = 0x1.47ae147ae147bp-6f;
  _block_1928->$1 = 0x1.999999999999ap-3f;
  _block_1928->$2 = -0x1.04p+6f;
  _block_1928->$3 = 0x1p+3f;
  _block_1928->$4 = 0x1.4p+2f;
  _block_1928->$5 = 0x1.4p+3f;
  _block_1928->$6 = 0x0p+0f;
  _block_1928->$7 = -0x1.4p+6f;
  return _block_1928;
}

int32_t _M0FP26RiantR8snn__mbt8step__iz(
  struct _M0TP26RiantR8snn__mbt2IZ* _M0L1pS813,
  float _M0L2dtS825
) {
  int32_t _M0L1nS812;
  struct _M0TP26RiantR8snn__mbt11IZParameter* _M0L3p__S814;
  float _M0L1aS815;
  float _M0L1bS816;
  float _M0L1cS817;
  float _M0L1dS818;
  float _M0L6tau__eS819;
  float _M0L6tau__iS820;
  float _M0L4e__eS821;
  float _M0L4e__iS822;
  int32_t _M0L7_2abindS823;
  int32_t _M0L1iS824;
  int32_t _M0L7_2abindS827;
  int32_t _M0L1iS828;
  int32_t _M0L7_2abindS834;
  int32_t _M0L1iS835;
  int32_t _M0L7_2abindS838;
  int32_t _M0L1iS839;
  int32_t _M0L7_2abindS841;
  int32_t _M0L1iS842;
  #line 139 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  _M0L1nS812 = _M0L1pS813->$1;
  _M0L3p__S814 = _M0L1pS813->$0;
  _M0L1aS815 = _M0L3p__S814->$0;
  _M0L1bS816 = _M0L3p__S814->$1;
  _M0L1cS817 = _M0L3p__S814->$2;
  _M0L1dS818 = _M0L3p__S814->$3;
  _M0L6tau__eS819 = _M0L3p__S814->$4;
  _M0L6tau__iS820 = _M0L3p__S814->$5;
  _M0L4e__eS821 = _M0L3p__S814->$6;
  _M0L4e__iS822 = _M0L3p__S814->$7;
  _M0L7_2abindS823 = 0;
  _M0L1iS824 = _M0L7_2abindS823;
  while (1) {
    if (_M0L1iS824 < _M0L1nS812) {
      struct _M0TPB5ArrayGfE* _M0L2geS1790 = _M0L1pS813->$6;
      struct _M0TPB5ArrayGfE* _M0L2geS1798 = _M0L1pS813->$6;
      float _M0L6_2atmpS1792;
      struct _M0TPB5ArrayGfE* _M0L2geS1797;
      float _M0L6_2atmpS1796;
      float _M0L6_2atmpS1795;
      float _M0L6_2atmpS1794;
      float _M0L6_2atmpS1793;
      float _M0L6_2atmpS1791;
      struct _M0TPB5ArrayGfE* _M0L2giS1799;
      struct _M0TPB5ArrayGfE* _M0L2giS1807;
      float _M0L6_2atmpS1801;
      struct _M0TPB5ArrayGfE* _M0L2giS1806;
      float _M0L6_2atmpS1805;
      float _M0L6_2atmpS1804;
      float _M0L6_2atmpS1803;
      float _M0L6_2atmpS1802;
      float _M0L6_2atmpS1800;
      int32_t _M0L6_2atmpS1808;
      #line 152 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L6_2atmpS1792 = _M0MPC15array5Array2atGfE(_M0L2geS1798, _M0L1iS824);
      _M0L2geS1797 = _M0L1pS813->$6;
      #line 152 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L6_2atmpS1796 = _M0MPC15array5Array2atGfE(_M0L2geS1797, _M0L1iS824);
      _M0L6_2atmpS1795 = -_M0L6_2atmpS1796;
      _M0L6_2atmpS1794 = _M0L2dtS825 * _M0L6_2atmpS1795;
      _M0L6_2atmpS1793 = _M0L6_2atmpS1794 / _M0L6tau__eS819;
      _M0L6_2atmpS1791 = _M0L6_2atmpS1792 + _M0L6_2atmpS1793;
      #line 152 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0MPC15array5Array3setGfE(_M0L2geS1790, _M0L1iS824, _M0L6_2atmpS1791);
      _M0L2giS1799 = _M0L1pS813->$7;
      _M0L2giS1807 = _M0L1pS813->$7;
      #line 153 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L6_2atmpS1801 = _M0MPC15array5Array2atGfE(_M0L2giS1807, _M0L1iS824);
      _M0L2giS1806 = _M0L1pS813->$7;
      #line 153 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L6_2atmpS1805 = _M0MPC15array5Array2atGfE(_M0L2giS1806, _M0L1iS824);
      _M0L6_2atmpS1804 = -_M0L6_2atmpS1805;
      _M0L6_2atmpS1803 = _M0L2dtS825 * _M0L6_2atmpS1804;
      _M0L6_2atmpS1802 = _M0L6_2atmpS1803 / _M0L6tau__iS820;
      _M0L6_2atmpS1800 = _M0L6_2atmpS1801 + _M0L6_2atmpS1802;
      #line 153 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0MPC15array5Array3setGfE(_M0L2giS1799, _M0L1iS824, _M0L6_2atmpS1800);
      _M0L6_2atmpS1808 = _M0L1iS824 + 1;
      _M0L1iS824 = _M0L6_2atmpS1808;
      continue;
    }
    break;
  }
  _M0L7_2abindS827 = 0;
  _M0L1iS828 = _M0L7_2abindS827;
  while (1) {
    if (_M0L1iS828 < _M0L1nS812) {
      struct _M0TPB5ArrayGfE* _M0L1vS1834 = _M0L1pS813->$2;
      float _M0L1vS829;
      struct _M0TPB5ArrayGfE* _M0L1uS1833;
      float _M0L1uS830;
      struct _M0TPB5ArrayGfE* _M0L1iS1832;
      float _M0L2iiS831;
      struct _M0TPB5ArrayGfE* _M0L1vS1809;
      float _M0L6_2atmpS1812;
      float _M0L6_2atmpS1819;
      float _M0L6_2atmpS1817;
      float _M0L6_2atmpS1818;
      float _M0L6_2atmpS1816;
      float _M0L6_2atmpS1815;
      float _M0L6_2atmpS1814;
      float _M0L6_2atmpS1813;
      float _M0L6_2atmpS1811;
      float _M0L6_2atmpS1810;
      struct _M0TPB5ArrayGfE* _M0L1vS1831;
      float _M0L2v2S832;
      struct _M0TPB5ArrayGfE* _M0L1vS1820;
      float _M0L6_2atmpS1823;
      float _M0L6_2atmpS1830;
      float _M0L6_2atmpS1828;
      float _M0L6_2atmpS1829;
      float _M0L6_2atmpS1827;
      float _M0L6_2atmpS1826;
      float _M0L6_2atmpS1825;
      float _M0L6_2atmpS1824;
      float _M0L6_2atmpS1822;
      float _M0L6_2atmpS1821;
      int32_t _M0L6_2atmpS1835;
      #line 157 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L1vS829 = _M0MPC15array5Array2atGfE(_M0L1vS1834, _M0L1iS828);
      _M0L1uS1833 = _M0L1pS813->$3;
      #line 158 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L1uS830 = _M0MPC15array5Array2atGfE(_M0L1uS1833, _M0L1iS828);
      _M0L1iS1832 = _M0L1pS813->$5;
      #line 159 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L2iiS831 = _M0MPC15array5Array2atGfE(_M0L1iS1832, _M0L1iS828);
      _M0L1vS1809 = _M0L1pS813->$2;
      _M0L6_2atmpS1812 = 0x1p-1f * _M0L2dtS825;
      _M0L6_2atmpS1819 = 0x1.47ae147ae147bp-5f * _M0L1vS829;
      _M0L6_2atmpS1817 = _M0L6_2atmpS1819 * _M0L1vS829;
      _M0L6_2atmpS1818 = 0x1.4p+2f * _M0L1vS829;
      _M0L6_2atmpS1816 = _M0L6_2atmpS1817 + _M0L6_2atmpS1818;
      _M0L6_2atmpS1815 = _M0L6_2atmpS1816 + 0x1.18p+7f;
      _M0L6_2atmpS1814 = _M0L6_2atmpS1815 - _M0L1uS830;
      _M0L6_2atmpS1813 = _M0L6_2atmpS1814 + _M0L2iiS831;
      _M0L6_2atmpS1811 = _M0L6_2atmpS1812 * _M0L6_2atmpS1813;
      _M0L6_2atmpS1810 = _M0L1vS829 + _M0L6_2atmpS1811;
      #line 160 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS1809, _M0L1iS828, _M0L6_2atmpS1810);
      _M0L1vS1831 = _M0L1pS813->$2;
      #line 161 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L2v2S832 = _M0MPC15array5Array2atGfE(_M0L1vS1831, _M0L1iS828);
      _M0L1vS1820 = _M0L1pS813->$2;
      _M0L6_2atmpS1823 = 0x1p-1f * _M0L2dtS825;
      _M0L6_2atmpS1830 = 0x1.47ae147ae147bp-5f * _M0L2v2S832;
      _M0L6_2atmpS1828 = _M0L6_2atmpS1830 * _M0L2v2S832;
      _M0L6_2atmpS1829 = 0x1.4p+2f * _M0L2v2S832;
      _M0L6_2atmpS1827 = _M0L6_2atmpS1828 + _M0L6_2atmpS1829;
      _M0L6_2atmpS1826 = _M0L6_2atmpS1827 + 0x1.18p+7f;
      _M0L6_2atmpS1825 = _M0L6_2atmpS1826 - _M0L1uS830;
      _M0L6_2atmpS1824 = _M0L6_2atmpS1825 + _M0L2iiS831;
      _M0L6_2atmpS1822 = _M0L6_2atmpS1823 * _M0L6_2atmpS1824;
      _M0L6_2atmpS1821 = _M0L2v2S832 + _M0L6_2atmpS1822;
      #line 162 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS1820, _M0L1iS828, _M0L6_2atmpS1821);
      _M0L6_2atmpS1835 = _M0L1iS828 + 1;
      _M0L1iS828 = _M0L6_2atmpS1835;
      continue;
    }
    break;
  }
  _M0L7_2abindS834 = 0;
  _M0L1iS835 = _M0L7_2abindS834;
  while (1) {
    if (_M0L1iS835 < _M0L1nS812) {
      struct _M0TPB5ArrayGfE* _M0L1vS1846 = _M0L1pS813->$2;
      float _M0L1vS836;
      struct _M0TPB5ArrayGfE* _M0L1uS1836;
      struct _M0TPB5ArrayGfE* _M0L1uS1845;
      float _M0L6_2atmpS1838;
      float _M0L6_2atmpS1840;
      float _M0L6_2atmpS1842;
      struct _M0TPB5ArrayGfE* _M0L1uS1844;
      float _M0L6_2atmpS1843;
      float _M0L6_2atmpS1841;
      float _M0L6_2atmpS1839;
      float _M0L6_2atmpS1837;
      int32_t _M0L6_2atmpS1847;
      #line 165 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L1vS836 = _M0MPC15array5Array2atGfE(_M0L1vS1846, _M0L1iS835);
      _M0L1uS1836 = _M0L1pS813->$3;
      _M0L1uS1845 = _M0L1pS813->$3;
      #line 166 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L6_2atmpS1838 = _M0MPC15array5Array2atGfE(_M0L1uS1845, _M0L1iS835);
      _M0L6_2atmpS1840 = _M0L2dtS825 * _M0L1aS815;
      _M0L6_2atmpS1842 = _M0L1bS816 * _M0L1vS836;
      _M0L1uS1844 = _M0L1pS813->$3;
      #line 166 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L6_2atmpS1843 = _M0MPC15array5Array2atGfE(_M0L1uS1844, _M0L1iS835);
      _M0L6_2atmpS1841 = _M0L6_2atmpS1842 - _M0L6_2atmpS1843;
      _M0L6_2atmpS1839 = _M0L6_2atmpS1840 * _M0L6_2atmpS1841;
      _M0L6_2atmpS1837 = _M0L6_2atmpS1838 + _M0L6_2atmpS1839;
      #line 166 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0MPC15array5Array3setGfE(_M0L1uS1836, _M0L1iS835, _M0L6_2atmpS1837);
      _M0L6_2atmpS1847 = _M0L1iS835 + 1;
      _M0L1iS835 = _M0L6_2atmpS1847;
      continue;
    }
    break;
  }
  _M0L7_2abindS838 = 0;
  _M0L1iS839 = _M0L7_2abindS838;
  while (1) {
    if (_M0L1iS839 < _M0L1nS812) {
      struct _M0TPB5ArrayGfE* _M0L1vS1848 = _M0L1pS813->$2;
      struct _M0TPB5ArrayGfE* _M0L1vS1865 = _M0L1pS813->$2;
      float _M0L6_2atmpS1850;
      struct _M0TPB5ArrayGfE* _M0L2geS1864;
      float _M0L6_2atmpS1860;
      struct _M0TPB5ArrayGfE* _M0L1vS1863;
      float _M0L6_2atmpS1862;
      float _M0L6_2atmpS1861;
      float _M0L6_2atmpS1853;
      struct _M0TPB5ArrayGfE* _M0L2giS1859;
      float _M0L6_2atmpS1855;
      struct _M0TPB5ArrayGfE* _M0L1vS1858;
      float _M0L6_2atmpS1857;
      float _M0L6_2atmpS1856;
      float _M0L6_2atmpS1854;
      float _M0L6_2atmpS1852;
      float _M0L6_2atmpS1851;
      float _M0L6_2atmpS1849;
      int32_t _M0L6_2atmpS1866;
      #line 169 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L6_2atmpS1850 = _M0MPC15array5Array2atGfE(_M0L1vS1865, _M0L1iS839);
      _M0L2geS1864 = _M0L1pS813->$6;
      #line 169 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L6_2atmpS1860 = _M0MPC15array5Array2atGfE(_M0L2geS1864, _M0L1iS839);
      _M0L1vS1863 = _M0L1pS813->$2;
      #line 169 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L6_2atmpS1862 = _M0MPC15array5Array2atGfE(_M0L1vS1863, _M0L1iS839);
      _M0L6_2atmpS1861 = _M0L4e__eS821 - _M0L6_2atmpS1862;
      _M0L6_2atmpS1853 = _M0L6_2atmpS1860 * _M0L6_2atmpS1861;
      _M0L2giS1859 = _M0L1pS813->$7;
      #line 169 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L6_2atmpS1855 = _M0MPC15array5Array2atGfE(_M0L2giS1859, _M0L1iS839);
      _M0L1vS1858 = _M0L1pS813->$2;
      #line 169 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L6_2atmpS1857 = _M0MPC15array5Array2atGfE(_M0L1vS1858, _M0L1iS839);
      _M0L6_2atmpS1856 = _M0L4e__iS822 - _M0L6_2atmpS1857;
      _M0L6_2atmpS1854 = _M0L6_2atmpS1855 * _M0L6_2atmpS1856;
      _M0L6_2atmpS1852 = _M0L6_2atmpS1853 + _M0L6_2atmpS1854;
      _M0L6_2atmpS1851 = _M0L2dtS825 * _M0L6_2atmpS1852;
      _M0L6_2atmpS1849 = _M0L6_2atmpS1850 + _M0L6_2atmpS1851;
      #line 169 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS1848, _M0L1iS839, _M0L6_2atmpS1849);
      _M0L6_2atmpS1866 = _M0L1iS839 + 1;
      _M0L1iS839 = _M0L6_2atmpS1866;
      continue;
    }
    break;
  }
  _M0L7_2abindS841 = 0;
  _M0L1iS842 = _M0L7_2abindS841;
  while (1) {
    if (_M0L1iS842 < _M0L1nS812) {
      struct _M0TPB5ArrayGbE* _M0L4fireS1867 = _M0L1pS813->$4;
      struct _M0TPB5ArrayGfE* _M0L1vS1870 = _M0L1pS813->$2;
      float _M0L6_2atmpS1869;
      int32_t _M0L6_2atmpS1868;
      struct _M0TPB5ArrayGfE* _M0L1vS1871;
      struct _M0TPB5ArrayGbE* _M0L4fireS1873;
      float _M0L6_2atmpS1872;
      struct _M0TPB5ArrayGfE* _M0L1uS1875;
      struct _M0TPB5ArrayGfE* _M0L1uS1880;
      float _M0L6_2atmpS1877;
      struct _M0TPB5ArrayGbE* _M0L4fireS1879;
      float _M0L6_2atmpS1878;
      float _M0L6_2atmpS1876;
      int32_t _M0L6_2atmpS1881;
      #line 173 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L6_2atmpS1869 = _M0MPC15array5Array2atGfE(_M0L1vS1870, _M0L1iS842);
      _M0L6_2atmpS1868 = _M0L6_2atmpS1869 > 0x1.ep+4f;
      #line 173 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0MPC15array5Array3setGbE(_M0L4fireS1867, _M0L1iS842, _M0L6_2atmpS1868);
      _M0L1vS1871 = _M0L1pS813->$2;
      _M0L4fireS1873 = _M0L1pS813->$4;
      #line 174 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS1873, _M0L1iS842)) {
        _M0L6_2atmpS1872 = _M0L1cS817;
      } else {
        struct _M0TPB5ArrayGfE* _M0L1vS1874 = _M0L1pS813->$2;
        #line 174 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
        _M0L6_2atmpS1872 = _M0MPC15array5Array2atGfE(_M0L1vS1874, _M0L1iS842);
      }
      #line 174 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS1871, _M0L1iS842, _M0L6_2atmpS1872);
      _M0L1uS1875 = _M0L1pS813->$3;
      _M0L1uS1880 = _M0L1pS813->$3;
      #line 175 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L6_2atmpS1877 = _M0MPC15array5Array2atGfE(_M0L1uS1880, _M0L1iS842);
      _M0L4fireS1879 = _M0L1pS813->$4;
      #line 175 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS1879, _M0L1iS842)) {
        _M0L6_2atmpS1878 = _M0L1dS818;
      } else {
        _M0L6_2atmpS1878 = 0x0p+0f;
      }
      _M0L6_2atmpS1876 = _M0L6_2atmpS1877 + _M0L6_2atmpS1878;
      #line 175 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0MPC15array5Array3setGfE(_M0L1uS1875, _M0L1iS842, _M0L6_2atmpS1876);
      _M0L6_2atmpS1881 = _M0L1iS842 + 1;
      _M0L1iS842 = _M0L6_2atmpS1881;
      continue;
    }
    break;
  }
  return 0;
}

struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0MP26RiantR8snn__mbt7Xoshiro7default(
  
) {
  #line 56 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  #line 57 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  return _M0MP26RiantR8snn__mbt7Xoshiro3new(0ull);
}

int32_t _M0MP26RiantR8snn__mbt15SparseMatrixCSR7forward(
  struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L1mS800,
  struct _M0TPB5ArrayGbE* _M0L9pre__fireS803,
  struct _M0TPB5ArrayGfE* _M0L7post__gS809
) {
  int32_t _M0L4rowsS799;
  int32_t _M0L7_2abindS801;
  int32_t _M0L1iS802;
  #line 287 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  _M0L4rowsS799 = _M0L1mS800->$0;
  _M0L7_2abindS801 = 0;
  _M0L1iS802 = _M0L7_2abindS801;
  while (1) {
    if (_M0L1iS802 < _M0L4rowsS799) {
      int32_t _M0L6_2atmpS1789;
      #line 294 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L9pre__fireS803, _M0L1iS802)) {
        struct _M0TPB5ArrayGiE* _M0L6rowptrS1788 = _M0L1mS800->$2;
        int32_t _M0L5startS804;
        struct _M0TPB5ArrayGiE* _M0L6rowptrS1786;
        int32_t _M0L6_2atmpS1787;
        int32_t _M0L3endS805;
        int32_t _M0L1kS806;
        #line 295 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
        _M0L5startS804
        = _M0MPC15array5Array2atGiE(_M0L6rowptrS1788, _M0L1iS802);
        _M0L6rowptrS1786 = _M0L1mS800->$2;
        _M0L6_2atmpS1787 = _M0L1iS802 + 1;
        #line 296 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
        _M0L3endS805
        = _M0MPC15array5Array2atGiE(_M0L6rowptrS1786, _M0L6_2atmpS1787);
        _M0L1kS806 = _M0L5startS804;
        while (1) {
          if (_M0L1kS806 < _M0L3endS805) {
            struct _M0TPB5ArrayGiE* _M0L6colptrS1784 = _M0L1mS800->$3;
            int32_t _M0L9post__idxS807;
            struct _M0TPB5ArrayGfE* _M0L4valsS1783;
            float _M0L1wS808;
            float _M0L6_2atmpS1782;
            float _M0L6_2atmpS1781;
            int32_t _M0L6_2atmpS1785;
            #line 298 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
            _M0L9post__idxS807
            = _M0MPC15array5Array2atGiE(_M0L6colptrS1784, _M0L1kS806);
            _M0L4valsS1783 = _M0L1mS800->$4;
            #line 299 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
            _M0L1wS808
            = _M0MPC15array5Array2atGfE(_M0L4valsS1783, _M0L1kS806);
            #line 300 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
            _M0L6_2atmpS1782
            = _M0MPC15array5Array2atGfE(_M0L7post__gS809, _M0L9post__idxS807);
            _M0L6_2atmpS1781 = _M0L6_2atmpS1782 + _M0L1wS808;
            #line 300 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
            _M0MPC15array5Array3setGfE(_M0L7post__gS809, _M0L9post__idxS807, _M0L6_2atmpS1781);
            _M0L6_2atmpS1785 = _M0L1kS806 + 1;
            _M0L1kS806 = _M0L6_2atmpS1785;
            continue;
          }
          break;
        }
      }
      _M0L6_2atmpS1789 = _M0L1iS802 + 1;
      _M0L1iS802 = _M0L6_2atmpS1789;
      continue;
    }
    break;
  }
  return 0;
}

struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0MP26RiantR8snn__mbt15SparseMatrixCSR6random(
  int32_t _M0L4rowsS793,
  int32_t _M0L4colsS794,
  float _M0L2muS795,
  float _M0L5sigmaS796,
  float _M0L1pS797,
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS798
) {
  #line 94 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  #line 103 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  return _M0MP26RiantR8snn__mbt15SparseMatrixCSR18random__with__rule(_M0L4rowsS793, _M0L4colsS794, _M0L2muS795, _M0L5sigmaS796, _M0L1pS797, 0, _M0L3rngS798);
}

struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0MP26RiantR8snn__mbt15SparseMatrixCSR18random__with__rule(
  int32_t _M0L4rowsS707,
  int32_t _M0L4colsS711,
  float _M0L2muS717,
  float _M0L5sigmaS718,
  float _M0L1pS730,
  int32_t _M0L4ruleS724,
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS720
) {
  float* _M0L6_2atmpS1780;
  struct _M0TPB5ArrayGfE* _M0L6_2atmpS1779;
  struct _M0TPB5ArrayGRPB5ArrayGfEE* _M0L5denseS706;
  int32_t _M0L7_2abindS708;
  int32_t _M0L1iS709;
  int32_t _M0L6_2atmpS1778;
  struct _M0TPB5ArrayGiE* _M0L6rowptrS783;
  int32_t* _M0L6_2atmpS1777;
  struct _M0TPB5ArrayGiE* _M0L6colptrS784;
  float* _M0L6_2atmpS1776;
  struct _M0TPB5ArrayGfE* _M0L4valsS785;
  int32_t _M0L7_2abindS786;
  int32_t _M0L1iS787;
  int32_t _M0L6_2atmpS1775;
  struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _block_1955;
  #line 109 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  _M0L6_2atmpS1780 = moonbit_empty_float_array;
  _M0L6_2atmpS1779
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_M0L6_2atmpS1779)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 15, 0);
  _M0L6_2atmpS1779->$0 = _M0L6_2atmpS1780;
  _M0L6_2atmpS1779->$1 = 0;
  #line 120 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  _M0L5denseS706
  = _M0MPC15array5Array4makeGRPB5ArrayGfEE(_M0L4rowsS707, _M0L6_2atmpS1779);
  _M0L7_2abindS708 = 0;
  _M0L1iS709 = _M0L7_2abindS708;
  while (1) {
    if (_M0L1iS709 < _M0L4rowsS707) {
      struct _M0TPB5ArrayGfE* _M0L3rowS710;
      int32_t _M0L7_2abindS712;
      int32_t _M0L1jS713;
      int32_t _M0L6_2atmpS1733;
      #line 122 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
      _M0L3rowS710 = _M0MPC15array5Array4makeGfE(_M0L4colsS711, 0x0p+0f);
      _M0L7_2abindS712 = 0;
      _M0L1jS713 = _M0L7_2abindS712;
      while (1) {
        if (_M0L1jS713 < _M0L4colsS711) {
          double _M0L2z1S715;
          struct _M0TUddE* _M0L7_2abindS719;
          double _M0L5_2az1S721;
          float _M0L6_2atmpS1731;
          float _M0L6_2atmpS1730;
          float _M0L1wS716;
          int32_t _M0L6_2atmpS1732;
          #line 131 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
          _M0L7_2abindS719
          = _M0FP26RiantR8snn__mbt11box__muller(_M0L3rngS720);
          _M0L5_2az1S721 = _M0L7_2abindS719->$0;
          moonbit_decref(_M0L7_2abindS719);
          _M0L2z1S715 = _M0L5_2az1S721;
          goto join_714;
          goto joinlet_1938;
          join_714:;
          _M0L6_2atmpS1731 = (float)_M0L2z1S715;
          _M0L6_2atmpS1730 = _M0L5sigmaS718 * _M0L6_2atmpS1731;
          _M0L1wS716 = _M0L2muS717 + _M0L6_2atmpS1730;
          #line 133 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
          _M0MPC15array5Array3setGfE(_M0L3rowS710, _M0L1jS713, _M0L1wS716);
          joinlet_1938:;
          _M0L6_2atmpS1732 = _M0L1jS713 + 1;
          _M0L1jS713 = _M0L6_2atmpS1732;
          continue;
        }
        break;
      }
      #line 135 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
      _M0MPC15array5Array3setGRPB5ArrayGfEE(_M0L5denseS706, _M0L1iS709, _M0L3rowS710);
      _M0L6_2atmpS1733 = _M0L1iS709 + 1;
      _M0L1iS709 = _M0L6_2atmpS1733;
      continue;
    }
    break;
  }
  switch (_M0L4ruleS724) {
    case 0: {
      int32_t _M0L7_2abindS725 = 0;
      int32_t _M0L1iS726 = _M0L7_2abindS725;
      while (1) {
        if (_M0L1iS726 < _M0L4rowsS707) {
          int32_t _M0L7_2abindS727 = 0;
          int32_t _M0L1jS728 = _M0L7_2abindS727;
          int32_t _M0L6_2atmpS1736;
          while (1) {
            if (_M0L1jS728 < _M0L4colsS711) {
              float _M0L1uS729;
              int32_t _M0L6_2atmpS1735;
              #line 143 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
              _M0L1uS729 = _M0FP26RiantR8snn__mbt9next__f32(_M0L3rngS720);
              if (_M0L1uS729 >= _M0L1pS730) {
                struct _M0TPB5ArrayGfE* _M0L6_2atmpS1734;
                #line 145 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS1734
                = _M0MPC15array5Array2atGRPB5ArrayGfEE(_M0L5denseS706, _M0L1iS726);
                #line 145 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array3setGfE(_M0L6_2atmpS1734, _M0L1jS728, 0x0p+0f);
                moonbit_decref(_M0L6_2atmpS1734);
              }
              _M0L6_2atmpS1735 = _M0L1jS728 + 1;
              _M0L1jS728 = _M0L6_2atmpS1735;
              continue;
            }
            break;
          }
          _M0L6_2atmpS1736 = _M0L1iS726 + 1;
          _M0L1iS726 = _M0L6_2atmpS1736;
          continue;
        }
        break;
      }
      break;
    }
    
    case 1: {
      float _M0L6_2atmpS1753 = (float)_M0L4rowsS707;
      float _M0L6_2atmpS1752 = _M0L6_2atmpS1753 * _M0L1pS730;
      int32_t _M0L7n__keepS733;
      #line 154 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
      _M0L7n__keepS733 = _M0MPC15float5Float7to__int(_M0L6_2atmpS1752);
      if (_M0L7n__keepS733 > 0 && _M0L7n__keepS733 <= _M0L4rowsS707) {
        int32_t _M0L7_2abindS734 = 0;
        int32_t _M0L1jS735 = _M0L7_2abindS734;
        while (1) {
          if (_M0L1jS735 < _M0L4colsS711) {
            int32_t* _M0L6_2atmpS1747 = (int32_t*)moonbit_empty_int32_array;
            struct _M0TPB5ArrayGiE* _M0L8pre__idxS736 =
              (struct _M0TPB5ArrayGiE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGiE));
            int32_t _M0L7_2abindS737;
            int32_t _M0L1kS738;
            int32_t _M0L7n__dropS740;
            int32_t _M0L7_2abindS741;
            int32_t _M0L1kS742;
            int32_t _M0L7_2abindS748;
            int32_t _M0L1kS749;
            int32_t _M0L6_2atmpS1748;
            Moonbit_object_header(_M0L8pre__idxS736)->meta
            = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 18, 0);
            _M0L8pre__idxS736->$0 = _M0L6_2atmpS1747;
            _M0L8pre__idxS736->$1 = 0;
            _M0L7_2abindS737 = 0;
            _M0L1kS738 = _M0L7_2abindS737;
            while (1) {
              if (_M0L1kS738 < _M0L4rowsS707) {
                int32_t _M0L6_2atmpS1737;
                #line 161 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array4pushGiE(_M0L8pre__idxS736, _M0L1kS738);
                _M0L6_2atmpS1737 = _M0L1kS738 + 1;
                _M0L1kS738 = _M0L6_2atmpS1737;
                continue;
              }
              break;
            }
            _M0L7n__dropS740 = _M0L4rowsS707 - _M0L7n__keepS733;
            _M0L7_2abindS741 = 0;
            _M0L1kS742 = _M0L7_2abindS741;
            while (1) {
              if (_M0L1kS742 < _M0L7n__dropS740) {
                float _M0L1uS743;
                int32_t _M0L6_2atmpS1742;
                float _M0L6_2atmpS1741;
                float _M0L6_2atmpS1740;
                int32_t _M0L6_2atmpS1739;
                int32_t _M0L6r__idxS744;
                int32_t _M0L10r__clampedS745;
                int32_t _M0L3tmpS746;
                int32_t _M0L6_2atmpS1738;
                int32_t _M0L6_2atmpS1743;
                #line 167 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L1uS743 = _M0FP26RiantR8snn__mbt9next__f32(_M0L3rngS720);
                _M0L6_2atmpS1742 = _M0L4rowsS707 - _M0L1kS742;
                _M0L6_2atmpS1741 = (float)_M0L6_2atmpS1742;
                _M0L6_2atmpS1740 = _M0L6_2atmpS1741 * _M0L1uS743;
                #line 168 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS1739
                = _M0MPC15float5Float7to__int(_M0L6_2atmpS1740);
                _M0L6r__idxS744 = _M0L1kS742 + _M0L6_2atmpS1739;
                if (_M0L6r__idxS744 >= _M0L4rowsS707) {
                  _M0L10r__clampedS745 = _M0L4rowsS707 - 1;
                } else {
                  _M0L10r__clampedS745 = _M0L6r__idxS744;
                }
                #line 171 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L3tmpS746
                = _M0MPC15array5Array2atGiE(_M0L8pre__idxS736, _M0L1kS742);
                #line 172 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS1738
                = _M0MPC15array5Array2atGiE(_M0L8pre__idxS736, _M0L10r__clampedS745);
                #line 172 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array3setGiE(_M0L8pre__idxS736, _M0L1kS742, _M0L6_2atmpS1738);
                #line 173 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array3setGiE(_M0L8pre__idxS736, _M0L10r__clampedS745, _M0L3tmpS746);
                _M0L6_2atmpS1743 = _M0L1kS742 + 1;
                _M0L1kS742 = _M0L6_2atmpS1743;
                continue;
              }
              break;
            }
            _M0L7_2abindS748 = 0;
            _M0L1kS749 = _M0L7_2abindS748;
            while (1) {
              if (_M0L1kS749 < _M0L7n__dropS740) {
                int32_t _M0L6_2atmpS1745;
                struct _M0TPB5ArrayGfE* _M0L6_2atmpS1744;
                int32_t _M0L6_2atmpS1746;
                #line 176 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS1745
                = _M0MPC15array5Array2atGiE(_M0L8pre__idxS736, _M0L1kS749);
                #line 176 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS1744
                = _M0MPC15array5Array2atGRPB5ArrayGfEE(_M0L5denseS706, _M0L6_2atmpS1745);
                #line 176 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array3setGfE(_M0L6_2atmpS1744, _M0L1jS735, 0x0p+0f);
                moonbit_decref(_M0L6_2atmpS1744);
                _M0L6_2atmpS1746 = _M0L1kS749 + 1;
                _M0L1kS749 = _M0L6_2atmpS1746;
                continue;
              } else {
                moonbit_decref(_M0L8pre__idxS736);
              }
              break;
            }
            _M0L6_2atmpS1748 = _M0L1jS735 + 1;
            _M0L1jS735 = _M0L6_2atmpS1748;
            continue;
          }
          break;
        }
      } else if (_M0L7n__keepS733 == 0) {
        int32_t _M0L7_2abindS752 = 0;
        int32_t _M0L1iS753 = _M0L7_2abindS752;
        while (1) {
          if (_M0L1iS753 < _M0L4rowsS707) {
            int32_t _M0L7_2abindS754 = 0;
            int32_t _M0L1jS755 = _M0L7_2abindS754;
            int32_t _M0L6_2atmpS1751;
            while (1) {
              if (_M0L1jS755 < _M0L4colsS711) {
                struct _M0TPB5ArrayGfE* _M0L6_2atmpS1749;
                int32_t _M0L6_2atmpS1750;
                #line 183 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS1749
                = _M0MPC15array5Array2atGRPB5ArrayGfEE(_M0L5denseS706, _M0L1iS753);
                #line 183 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array3setGfE(_M0L6_2atmpS1749, _M0L1jS755, 0x0p+0f);
                moonbit_decref(_M0L6_2atmpS1749);
                _M0L6_2atmpS1750 = _M0L1jS755 + 1;
                _M0L1jS755 = _M0L6_2atmpS1750;
                continue;
              }
              break;
            }
            _M0L6_2atmpS1751 = _M0L1iS753 + 1;
            _M0L1iS753 = _M0L6_2atmpS1751;
            continue;
          }
          break;
        }
      }
      break;
    }
    default: {
      float _M0L6_2atmpS1770 = (float)_M0L4colsS711;
      float _M0L6_2atmpS1769 = _M0L6_2atmpS1770 * _M0L1pS730;
      int32_t _M0L7n__keepS758;
      #line 191 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
      _M0L7n__keepS758 = _M0MPC15float5Float7to__int(_M0L6_2atmpS1769);
      if (_M0L7n__keepS758 > 0 && _M0L7n__keepS758 <= _M0L4colsS711) {
        int32_t _M0L7_2abindS759 = 0;
        int32_t _M0L1iS760 = _M0L7_2abindS759;
        while (1) {
          if (_M0L1iS760 < _M0L4rowsS707) {
            int32_t* _M0L6_2atmpS1764 = (int32_t*)moonbit_empty_int32_array;
            struct _M0TPB5ArrayGiE* _M0L9post__idxS761 =
              (struct _M0TPB5ArrayGiE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGiE));
            int32_t _M0L7_2abindS762;
            int32_t _M0L1kS763;
            int32_t _M0L7n__dropS765;
            int32_t _M0L7_2abindS766;
            int32_t _M0L1kS767;
            int32_t _M0L7_2abindS773;
            int32_t _M0L1kS774;
            int32_t _M0L6_2atmpS1765;
            Moonbit_object_header(_M0L9post__idxS761)->meta
            = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 18, 0);
            _M0L9post__idxS761->$0 = _M0L6_2atmpS1764;
            _M0L9post__idxS761->$1 = 0;
            _M0L7_2abindS762 = 0;
            _M0L1kS763 = _M0L7_2abindS762;
            while (1) {
              if (_M0L1kS763 < _M0L4colsS711) {
                int32_t _M0L6_2atmpS1754;
                #line 196 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array4pushGiE(_M0L9post__idxS761, _M0L1kS763);
                _M0L6_2atmpS1754 = _M0L1kS763 + 1;
                _M0L1kS763 = _M0L6_2atmpS1754;
                continue;
              }
              break;
            }
            _M0L7n__dropS765 = _M0L4colsS711 - _M0L7n__keepS758;
            _M0L7_2abindS766 = 0;
            _M0L1kS767 = _M0L7_2abindS766;
            while (1) {
              if (_M0L1kS767 < _M0L7n__dropS765) {
                float _M0L1uS768;
                int32_t _M0L6_2atmpS1759;
                float _M0L6_2atmpS1758;
                float _M0L6_2atmpS1757;
                int32_t _M0L6_2atmpS1756;
                int32_t _M0L6r__idxS769;
                int32_t _M0L10r__clampedS770;
                int32_t _M0L3tmpS771;
                int32_t _M0L6_2atmpS1755;
                int32_t _M0L6_2atmpS1760;
                #line 200 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L1uS768 = _M0FP26RiantR8snn__mbt9next__f32(_M0L3rngS720);
                _M0L6_2atmpS1759 = _M0L4colsS711 - _M0L1kS767;
                _M0L6_2atmpS1758 = (float)_M0L6_2atmpS1759;
                _M0L6_2atmpS1757 = _M0L6_2atmpS1758 * _M0L1uS768;
                #line 201 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS1756
                = _M0MPC15float5Float7to__int(_M0L6_2atmpS1757);
                _M0L6r__idxS769 = _M0L1kS767 + _M0L6_2atmpS1756;
                if (_M0L6r__idxS769 >= _M0L4colsS711) {
                  _M0L10r__clampedS770 = _M0L4colsS711 - 1;
                } else {
                  _M0L10r__clampedS770 = _M0L6r__idxS769;
                }
                #line 203 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L3tmpS771
                = _M0MPC15array5Array2atGiE(_M0L9post__idxS761, _M0L1kS767);
                #line 204 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS1755
                = _M0MPC15array5Array2atGiE(_M0L9post__idxS761, _M0L10r__clampedS770);
                #line 204 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array3setGiE(_M0L9post__idxS761, _M0L1kS767, _M0L6_2atmpS1755);
                #line 205 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array3setGiE(_M0L9post__idxS761, _M0L10r__clampedS770, _M0L3tmpS771);
                _M0L6_2atmpS1760 = _M0L1kS767 + 1;
                _M0L1kS767 = _M0L6_2atmpS1760;
                continue;
              }
              break;
            }
            _M0L7_2abindS773 = 0;
            _M0L1kS774 = _M0L7_2abindS773;
            while (1) {
              if (_M0L1kS774 < _M0L7n__dropS765) {
                struct _M0TPB5ArrayGfE* _M0L6_2atmpS1761;
                int32_t _M0L6_2atmpS1762;
                int32_t _M0L6_2atmpS1763;
                #line 208 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS1761
                = _M0MPC15array5Array2atGRPB5ArrayGfEE(_M0L5denseS706, _M0L1iS760);
                #line 208 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS1762
                = _M0MPC15array5Array2atGiE(_M0L9post__idxS761, _M0L1kS774);
                #line 208 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array3setGfE(_M0L6_2atmpS1761, _M0L6_2atmpS1762, 0x0p+0f);
                moonbit_decref(_M0L6_2atmpS1761);
                _M0L6_2atmpS1763 = _M0L1kS774 + 1;
                _M0L1kS774 = _M0L6_2atmpS1763;
                continue;
              } else {
                moonbit_decref(_M0L9post__idxS761);
              }
              break;
            }
            _M0L6_2atmpS1765 = _M0L1iS760 + 1;
            _M0L1iS760 = _M0L6_2atmpS1765;
            continue;
          }
          break;
        }
      } else if (_M0L7n__keepS758 == 0) {
        int32_t _M0L7_2abindS777 = 0;
        int32_t _M0L1iS778 = _M0L7_2abindS777;
        while (1) {
          if (_M0L1iS778 < _M0L4rowsS707) {
            int32_t _M0L7_2abindS779 = 0;
            int32_t _M0L1jS780 = _M0L7_2abindS779;
            int32_t _M0L6_2atmpS1768;
            while (1) {
              if (_M0L1jS780 < _M0L4colsS711) {
                struct _M0TPB5ArrayGfE* _M0L6_2atmpS1766;
                int32_t _M0L6_2atmpS1767;
                #line 214 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS1766
                = _M0MPC15array5Array2atGRPB5ArrayGfEE(_M0L5denseS706, _M0L1iS778);
                #line 214 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array3setGfE(_M0L6_2atmpS1766, _M0L1jS780, 0x0p+0f);
                moonbit_decref(_M0L6_2atmpS1766);
                _M0L6_2atmpS1767 = _M0L1jS780 + 1;
                _M0L1jS780 = _M0L6_2atmpS1767;
                continue;
              }
              break;
            }
            _M0L6_2atmpS1768 = _M0L1iS778 + 1;
            _M0L1iS778 = _M0L6_2atmpS1768;
            continue;
          }
          break;
        }
      }
      break;
    }
  }
  _M0L6_2atmpS1778 = _M0L4rowsS707 + 1;
  #line 221 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  _M0L6rowptrS783 = _M0MPC15array5Array4makeGiE(_M0L6_2atmpS1778, 0);
  _M0L6_2atmpS1777 = (int32_t*)moonbit_empty_int32_array;
  _M0L6colptrS784
  = (struct _M0TPB5ArrayGiE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGiE));
  Moonbit_object_header(_M0L6colptrS784)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 18, 0);
  _M0L6colptrS784->$0 = _M0L6_2atmpS1777;
  _M0L6colptrS784->$1 = 0;
  _M0L6_2atmpS1776 = moonbit_empty_float_array;
  _M0L4valsS785
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_M0L4valsS785)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 15, 0);
  _M0L4valsS785->$0 = _M0L6_2atmpS1776;
  _M0L4valsS785->$1 = 0;
  _M0L7_2abindS786 = 0;
  _M0L1iS787 = _M0L7_2abindS786;
  while (1) {
    if (_M0L1iS787 < _M0L4rowsS707) {
      int32_t _M0L6_2atmpS1771;
      int32_t _M0L7_2abindS788;
      int32_t _M0L1jS789;
      int32_t _M0L6_2atmpS1774;
      #line 225 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
      _M0L6_2atmpS1771 = _M0MPC15array5Array6lengthGfE(_M0L4valsS785);
      #line 225 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
      _M0MPC15array5Array3setGiE(_M0L6rowptrS783, _M0L1iS787, _M0L6_2atmpS1771);
      _M0L7_2abindS788 = 0;
      _M0L1jS789 = _M0L7_2abindS788;
      while (1) {
        if (_M0L1jS789 < _M0L4colsS711) {
          struct _M0TPB5ArrayGfE* _M0L6_2atmpS1772;
          float _M0L1vS790;
          int32_t _M0L6_2atmpS1773;
          #line 227 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
          _M0L6_2atmpS1772
          = _M0MPC15array5Array2atGRPB5ArrayGfEE(_M0L5denseS706, _M0L1iS787);
          #line 227 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
          _M0L1vS790
          = _M0MPC15array5Array2atGfE(_M0L6_2atmpS1772, _M0L1jS789);
          moonbit_decref(_M0L6_2atmpS1772);
          if (_M0L1vS790 != 0x0p+0f) {
            #line 229 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
            _M0MPC15array5Array4pushGiE(_M0L6colptrS784, _M0L1jS789);
            #line 230 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
            _M0MPC15array5Array4pushGfE(_M0L4valsS785, _M0L1vS790);
          }
          _M0L6_2atmpS1773 = _M0L1jS789 + 1;
          _M0L1jS789 = _M0L6_2atmpS1773;
          continue;
        }
        break;
      }
      _M0L6_2atmpS1774 = _M0L1iS787 + 1;
      _M0L1iS787 = _M0L6_2atmpS1774;
      continue;
    } else {
      moonbit_decref(_M0L5denseS706);
    }
    break;
  }
  #line 234 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  _M0L6_2atmpS1775 = _M0MPC15array5Array6lengthGfE(_M0L4valsS785);
  #line 234 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  _M0MPC15array5Array3setGiE(_M0L6rowptrS783, _M0L4rowsS707, _M0L6_2atmpS1775);
  _block_1955
  = (struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR));
  Moonbit_object_header(_block_1955)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 21, 0);
  _block_1955->$0 = _M0L4rowsS707;
  _block_1955->$1 = _M0L4colsS711;
  _block_1955->$2 = _M0L6rowptrS783;
  _block_1955->$3 = _M0L6colptrS784;
  _block_1955->$4 = _M0L4valsS785;
  return _block_1955;
}

int32_t _M0MP26RiantR8snn__mbt15SparseMatrixCSR3nnz(
  struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L1mS705
) {
  struct _M0TPB5ArrayGfE* _M0L4valsS1729;
  #line 34 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  _M0L4valsS1729 = _M0L1mS705->$4;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  return _M0MPC15array5Array6lengthGfE(_M0L4valsS1729);
}

struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0MP26RiantR8snn__mbt7Xoshiro3new(
  uint64_t _M0L4seedS703
) {
  struct _M0TUmmmmE* _M0L1sS702;
  uint64_t _M0L6_2atmpS1728;
  struct _M0TUmmmmE* _M0L1tS704;
  uint64_t _M0L6_2atmpS1724;
  uint64_t _M0L6_2atmpS1725;
  uint64_t _M0L6_2atmpS1726;
  uint64_t _M0L6_2atmpS1727;
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _block_1956;
  #line 48 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  #line 49 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L1sS702 = _M0FP26RiantR8snn__mbt10splitmix64(_M0L4seedS703);
  _M0L6_2atmpS1728 = _M0L1sS702->$3;
  #line 50 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L1tS704 = _M0FP26RiantR8snn__mbt10splitmix64(_M0L6_2atmpS1728);
  _M0L6_2atmpS1724 = _M0L1sS702->$0;
  _M0L6_2atmpS1725 = _M0L1sS702->$1;
  _M0L6_2atmpS1726 = _M0L1sS702->$2;
  moonbit_decref(_M0L1sS702);
  _M0L6_2atmpS1727 = _M0L1tS704->$0;
  moonbit_decref(_M0L1tS704);
  _block_1956
  = (struct _M0TP26RiantR8snn__mbt7Xoshiro*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt7Xoshiro));
  Moonbit_object_header(_block_1956)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_1956->$0 = _M0L6_2atmpS1724;
  _block_1956->$1 = _M0L6_2atmpS1725;
  _block_1956->$2 = _M0L6_2atmpS1726;
  _block_1956->$3 = _M0L6_2atmpS1727;
  return _block_1956;
}

struct _M0TUmmmmE* _M0FP26RiantR8snn__mbt10splitmix64(uint64_t _M0L4seedS694) {
  uint64_t _M0L2s1S693;
  uint64_t _M0L2z1S695;
  uint64_t _M0L2s2S696;
  uint64_t _M0L2z2S697;
  uint64_t _M0L2s3S698;
  uint64_t _M0L2z3S699;
  uint64_t _M0L2s4S700;
  uint64_t _M0L2z4S701;
  struct _M0TUmmmmE* _block_1957;
  #line 62 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2s1S693 = _M0L4seedS694 + 11400714819323198485ull;
  #line 64 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2z1S695 = _M0FP26RiantR8snn__mbt5mix64(_M0L2s1S693);
  _M0L2s2S696 = _M0L2s1S693 + 11400714819323198485ull;
  #line 66 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2z2S697 = _M0FP26RiantR8snn__mbt5mix64(_M0L2s2S696);
  _M0L2s3S698 = _M0L2s2S696 + 11400714819323198485ull;
  #line 68 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2z3S699 = _M0FP26RiantR8snn__mbt5mix64(_M0L2s3S698);
  _M0L2s4S700 = _M0L2s3S698 + 11400714819323198485ull;
  #line 70 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2z4S701 = _M0FP26RiantR8snn__mbt5mix64(_M0L2s4S700);
  _block_1957 = (struct _M0TUmmmmE*)moonbit_malloc(sizeof(struct _M0TUmmmmE));
  Moonbit_object_header(_block_1957)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_1957->$0 = _M0L2z1S695;
  _block_1957->$1 = _M0L2z2S697;
  _block_1957->$2 = _M0L2z3S699;
  _block_1957->$3 = _M0L2z4S701;
  return _block_1957;
}

uint64_t _M0FP26RiantR8snn__mbt5mix64(uint64_t _M0L1zS691) {
  uint64_t _M0L6_2atmpS1723;
  uint64_t _M0L6_2atmpS1722;
  uint64_t _M0L1zS690;
  uint64_t _M0L6_2atmpS1721;
  uint64_t _M0L6_2atmpS1720;
  uint64_t _M0L1zS692;
  uint64_t _M0L6_2atmpS1719;
  #line 75 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L6_2atmpS1723 = _M0L1zS691 >> 30;
  _M0L6_2atmpS1722 = _M0L1zS691 ^ _M0L6_2atmpS1723;
  _M0L1zS690 = _M0L6_2atmpS1722 * 13787848793156543929ull;
  _M0L6_2atmpS1721 = _M0L1zS690 >> 27;
  _M0L6_2atmpS1720 = _M0L1zS690 ^ _M0L6_2atmpS1721;
  _M0L1zS692 = _M0L6_2atmpS1720 * 10723151780598845931ull;
  _M0L6_2atmpS1719 = _M0L1zS692 >> 31;
  return _M0L1zS692 ^ _M0L6_2atmpS1719;
}

struct _M0TUddE* _M0FP26RiantR8snn__mbt11box__muller(
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS685
) {
  double _M0L2u1S684;
  double _M0L8u1__safeS686;
  double _M0L2u2S687;
  double _M0L6_2atmpS1718;
  double _M0L6_2atmpS1717;
  double _M0L1rS688;
  double _M0L5thetaS689;
  double _M0L6_2atmpS1716;
  double _M0L6_2atmpS1713;
  double _M0L6_2atmpS1715;
  double _M0L6_2atmpS1714;
  struct _M0TUddE* _block_1958;
  #line 126 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  #line 127 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  _M0L2u1S684 = _M0FP26RiantR8snn__mbt9next__f64(_M0L3rngS685);
  if (_M0L2u1S684 < 0x1.203af9ee75616p-50) {
    _M0L8u1__safeS686 = 0x1.203af9ee75616p-50;
  } else {
    _M0L8u1__safeS686 = _M0L2u1S684;
  }
  #line 130 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  _M0L2u2S687 = _M0FP26RiantR8snn__mbt9next__f64(_M0L3rngS685);
  #line 131 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  _M0L6_2atmpS1718 = _M0FPC14math2ln(_M0L8u1__safeS686);
  _M0L6_2atmpS1717 = -0x1p+1 * _M0L6_2atmpS1718;
  #line 131 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  _M0L1rS688 = sqrt(_M0L6_2atmpS1717);
  _M0L5thetaS689 = 0x1.921fb54442d18p+2 * _M0L2u2S687;
  #line 133 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  _M0L6_2atmpS1716 = _M0FPC14math3cos(_M0L5thetaS689);
  _M0L6_2atmpS1713 = _M0L1rS688 * _M0L6_2atmpS1716;
  #line 133 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  _M0L6_2atmpS1715 = _M0FPC14math3sin(_M0L5thetaS689);
  _M0L6_2atmpS1714 = _M0L1rS688 * _M0L6_2atmpS1715;
  _block_1958 = (struct _M0TUddE*)moonbit_malloc(sizeof(struct _M0TUddE));
  Moonbit_object_header(_block_1958)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_1958->$0 = _M0L6_2atmpS1713;
  _block_1958->$1 = _M0L6_2atmpS1714;
  return _block_1958;
}

double _M0FP26RiantR8snn__mbt9next__f64(
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L1rS682
) {
  uint64_t _M0L1uS681;
  uint64_t _M0L4bitsS683;
  double _M0L6_2atmpS1712;
  #line 118 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  #line 119 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L1uS681 = _M0FP26RiantR8snn__mbt9next__u64(_M0L1rS682);
  _M0L4bitsS683 = _M0L1uS681 >> 11;
  _M0L6_2atmpS1712 = (double)_M0L4bitsS683;
  return _M0L6_2atmpS1712 * 0x1p-53;
}

float _M0FP26RiantR8snn__mbt9next__f32(
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L1rS679
) {
  uint32_t _M0L1uS678;
  uint32_t _M0L4bitsS680;
  double _M0L6_2atmpS1711;
  double _M0L6_2atmpS1710;
  #line 128 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  #line 129 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L1uS678 = _M0FP26RiantR8snn__mbt9next__u32(_M0L1rS679);
  _M0L4bitsS680 = _M0L1uS678 >> 8;
  _M0L6_2atmpS1711 = (double)_M0L4bitsS680;
  _M0L6_2atmpS1710 = _M0L6_2atmpS1711 * 0x1p-24;
  return (float)_M0L6_2atmpS1710;
}

uint32_t _M0FP26RiantR8snn__mbt9next__u32(
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L1rS677
) {
  uint64_t _M0L1uS676;
  uint64_t _M0L6_2atmpS1709;
  #line 110 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  #line 111 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L1uS676 = _M0FP26RiantR8snn__mbt9next__u64(_M0L1rS677);
  _M0L6_2atmpS1709 = _M0L1uS676 >> 32;
  return (uint32_t)_M0L6_2atmpS1709;
}

uint64_t _M0FP26RiantR8snn__mbt9next__u64(
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L1rS669
) {
  uint64_t _M0L2s0S668;
  uint64_t _M0L2s1S670;
  uint64_t _M0L2s2S671;
  uint64_t _M0L2s3S672;
  uint64_t _M0L3tmpS673;
  uint64_t _M0L6_2atmpS1708;
  uint64_t _M0L3resS674;
  uint64_t _M0L1tS675;
  uint64_t _M0L6_2atmpS1698;
  uint64_t _M0L6_2atmpS1699;
  uint64_t _M0L2s2S1701;
  uint64_t _M0L6_2atmpS1700;
  uint64_t _M0L2s3S1703;
  uint64_t _M0L6_2atmpS1702;
  uint64_t _M0L2s2S1705;
  uint64_t _M0L6_2atmpS1704;
  uint64_t _M0L2s3S1707;
  uint64_t _M0L6_2atmpS1706;
  #line 90 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2s0S668 = _M0L1rS669->$0;
  _M0L2s1S670 = _M0L1rS669->$1;
  _M0L2s2S671 = _M0L1rS669->$2;
  _M0L2s3S672 = _M0L1rS669->$3;
  _M0L3tmpS673 = _M0L2s0S668 + _M0L2s3S672;
  #line 96 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L6_2atmpS1708 = _M0FP26RiantR8snn__mbt4rotl(_M0L3tmpS673, 23);
  _M0L3resS674 = _M0L6_2atmpS1708 + _M0L2s0S668;
  _M0L1tS675 = _M0L2s1S670 << 17;
  _M0L6_2atmpS1698 = _M0L2s2S671 ^ _M0L2s0S668;
  _M0L1rS669->$2 = _M0L6_2atmpS1698;
  _M0L6_2atmpS1699 = _M0L2s3S672 ^ _M0L2s1S670;
  _M0L1rS669->$3 = _M0L6_2atmpS1699;
  _M0L2s2S1701 = _M0L1rS669->$2;
  _M0L6_2atmpS1700 = _M0L2s1S670 ^ _M0L2s2S1701;
  _M0L1rS669->$1 = _M0L6_2atmpS1700;
  _M0L2s3S1703 = _M0L1rS669->$3;
  _M0L6_2atmpS1702 = _M0L2s0S668 ^ _M0L2s3S1703;
  _M0L1rS669->$0 = _M0L6_2atmpS1702;
  _M0L2s2S1705 = _M0L1rS669->$2;
  _M0L6_2atmpS1704 = _M0L2s2S1705 ^ _M0L1tS675;
  _M0L1rS669->$2 = _M0L6_2atmpS1704;
  _M0L2s3S1707 = _M0L1rS669->$3;
  #line 103 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L6_2atmpS1706 = _M0FP26RiantR8snn__mbt4rotl(_M0L2s3S1707, 45);
  _M0L1rS669->$3 = _M0L6_2atmpS1706;
  return _M0L3resS674;
}

uint64_t _M0FP26RiantR8snn__mbt4rotl(uint64_t _M0L1xS666, int32_t _M0L1kS667) {
  uint64_t _M0L6_2atmpS1695;
  int32_t _M0L6_2atmpS1697;
  uint64_t _M0L6_2atmpS1696;
  #line 83 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L6_2atmpS1695 = _M0L1xS666 << (_M0L1kS667 & 63);
  _M0L6_2atmpS1697 = 64 - _M0L1kS667;
  _M0L6_2atmpS1696 = _M0L1xS666 >> (_M0L6_2atmpS1697 & 63);
  return _M0L6_2atmpS1695 | _M0L6_2atmpS1696;
}

double _M0FPC14math2ln(double _M0L1xS652) {
  struct _M0TUdiE* _M0L7_2abindS653;
  double _M0L5_2af1S654;
  int32_t _M0L5_2akiS655;
  double _M0L1fS657;
  double _M0L1kS658;
  double _M0L6_2atmpS1688;
  double _M0L1sS659;
  double _M0L2s2S660;
  double _M0L2s4S661;
  double _M0L6_2atmpS1687;
  double _M0L6_2atmpS1686;
  double _M0L6_2atmpS1685;
  double _M0L6_2atmpS1684;
  double _M0L6_2atmpS1683;
  double _M0L6_2atmpS1682;
  double _M0L2t1S662;
  double _M0L6_2atmpS1681;
  double _M0L6_2atmpS1680;
  double _M0L6_2atmpS1679;
  double _M0L6_2atmpS1678;
  double _M0L2t2S663;
  double _M0L1rS664;
  double _M0L6_2atmpS1677;
  double _M0L4hfsqS665;
  double _M0L6_2atmpS1670;
  double _M0L6_2atmpS1676;
  double _M0L6_2atmpS1674;
  double _M0L6_2atmpS1675;
  double _M0L6_2atmpS1673;
  double _M0L6_2atmpS1672;
  double _M0L6_2atmpS1671;
  #line 55 "C:\\Users\\31379\\.moon\\lib\\core\\math\\log_double_nonjs.mbt"
  if (_M0L1xS652 < 0x0p+0) {
    return _M0FPC16double14not__a__number;
  } else {
    #line 65 "C:\\Users\\31379\\.moon\\lib\\core\\math\\log_double_nonjs.mbt"
    #line 65 "C:\\Users\\31379\\.moon\\lib\\core\\math\\log_double_nonjs.mbt"
    if (
      _M0MPC16double6Double7is__nan(_M0L1xS652)
      || _M0MPC16double6Double7is__inf(_M0L1xS652)
    ) {
      return _M0L1xS652;
    } else if (_M0L1xS652 == 0x0p+0) {
      return _M0FPC16double13neg__infinity;
    }
  }
  #line 70 "C:\\Users\\31379\\.moon\\lib\\core\\math\\log_double_nonjs.mbt"
  _M0L7_2abindS653 = _M0FPC14math5frexp(_M0L1xS652);
  _M0L5_2af1S654 = _M0L7_2abindS653->$0;
  _M0L5_2akiS655 = _M0L7_2abindS653->$1;
  moonbit_decref(_M0L7_2abindS653);
  if (_M0L5_2af1S654 < 0x1.6a09e667f3bcdp-1) {
    double _M0L6_2atmpS1692 = _M0L5_2af1S654 * 0x1p+1;
    double _M0L6_2atmpS1689 = _M0L6_2atmpS1692 - 0x1p+0;
    int32_t _M0L6_2atmpS1691 = _M0L5_2akiS655 - 1;
    double _M0L6_2atmpS1690 = (double)_M0L6_2atmpS1691;
    _M0L1fS657 = _M0L6_2atmpS1689;
    _M0L1kS658 = _M0L6_2atmpS1690;
    goto join_656;
  } else {
    double _M0L6_2atmpS1693 = _M0L5_2af1S654 - 0x1p+0;
    double _M0L6_2atmpS1694 = (double)_M0L5_2akiS655;
    _M0L1fS657 = _M0L6_2atmpS1693;
    _M0L1kS658 = _M0L6_2atmpS1694;
    goto join_656;
  }
  join_656:;
  _M0L6_2atmpS1688 = 0x1p+1 + _M0L1fS657;
  _M0L1sS659 = _M0L1fS657 / _M0L6_2atmpS1688;
  _M0L2s2S660 = _M0L1sS659 * _M0L1sS659;
  _M0L2s4S661 = _M0L2s2S660 * _M0L2s2S660;
  _M0L6_2atmpS1687 = _M0L2s4S661 * 0x1.2f112df3e5244p-3;
  _M0L6_2atmpS1686 = 0x1.7466496cb03dep-3 + _M0L6_2atmpS1687;
  _M0L6_2atmpS1685 = _M0L2s4S661 * _M0L6_2atmpS1686;
  _M0L6_2atmpS1684 = 0x1.2492494229359p-2 + _M0L6_2atmpS1685;
  _M0L6_2atmpS1683 = _M0L2s4S661 * _M0L6_2atmpS1684;
  _M0L6_2atmpS1682 = 0x1.5555555555593p-1 + _M0L6_2atmpS1683;
  _M0L2t1S662 = _M0L2s2S660 * _M0L6_2atmpS1682;
  _M0L6_2atmpS1681 = _M0L2s4S661 * 0x1.39a09d078c69fp-3;
  _M0L6_2atmpS1680 = 0x1.c71c51d8e78afp-3 + _M0L6_2atmpS1681;
  _M0L6_2atmpS1679 = _M0L2s4S661 * _M0L6_2atmpS1680;
  _M0L6_2atmpS1678 = 0x1.999999997fa04p-2 + _M0L6_2atmpS1679;
  _M0L2t2S663 = _M0L2s4S661 * _M0L6_2atmpS1678;
  _M0L1rS664 = _M0L2t1S662 + _M0L2t2S663;
  _M0L6_2atmpS1677 = 0x1p-1 * _M0L1fS657;
  _M0L4hfsqS665 = _M0L6_2atmpS1677 * _M0L1fS657;
  _M0L6_2atmpS1670 = _M0L1kS658 * 0x1.62e42feep-1;
  _M0L6_2atmpS1676 = _M0L4hfsqS665 + _M0L1rS664;
  _M0L6_2atmpS1674 = _M0L1sS659 * _M0L6_2atmpS1676;
  _M0L6_2atmpS1675 = _M0L1kS658 * 0x1.a39ef35793c76p-33;
  _M0L6_2atmpS1673 = _M0L6_2atmpS1674 + _M0L6_2atmpS1675;
  _M0L6_2atmpS1672 = _M0L4hfsqS665 - _M0L6_2atmpS1673;
  _M0L6_2atmpS1671 = _M0L6_2atmpS1672 - _M0L1fS657;
  return _M0L6_2atmpS1670 - _M0L6_2atmpS1671;
}

struct _M0TUdiE* _M0FPC14math5frexp(double _M0L1fS645) {
  struct _M0TUdiE* _M0L7_2abindS646;
  double _M0L10_2anorm__fS647;
  int32_t _M0L6_2aexpS648;
  uint64_t _M0L1uS649;
  uint64_t _M0L6_2atmpS1669;
  uint64_t _M0L6_2atmpS1668;
  int32_t _M0L6_2atmpS1667;
  int32_t _M0L6_2atmpS1666;
  int32_t _M0L3expS650;
  uint64_t _M0L6_2atmpS1665;
  uint64_t _M0L6_2atmpS1664;
  uint64_t _M0L6_2atmpS1663;
  double _M0L4fracS651;
  struct _M0TUdiE* _block_1961;
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\math\\utils.mbt"
  #line 134 "C:\\Users\\31379\\.moon\\lib\\core\\math\\utils.mbt"
  #line 134 "C:\\Users\\31379\\.moon\\lib\\core\\math\\utils.mbt"
  if (
    _M0L1fS645 == 0x0p+0
    || _M0MPC16double6Double7is__inf(_M0L1fS645)
    || _M0MPC16double6Double7is__nan(_M0L1fS645)
  ) {
    struct _M0TUdiE* _block_1960 =
      (struct _M0TUdiE*)moonbit_malloc(sizeof(struct _M0TUdiE));
    Moonbit_object_header(_block_1960)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
    _block_1960->$0 = _M0L1fS645;
    _block_1960->$1 = 0;
    return _block_1960;
  }
  #line 137 "C:\\Users\\31379\\.moon\\lib\\core\\math\\utils.mbt"
  _M0L7_2abindS646 = _M0FPC14math9normalize(_M0L1fS645);
  _M0L10_2anorm__fS647 = _M0L7_2abindS646->$0;
  _M0L6_2aexpS648 = _M0L7_2abindS646->$1;
  moonbit_decref(_M0L7_2abindS646);
  _M0L1uS649 = *(int64_t*)&_M0L10_2anorm__fS647;
  _M0L6_2atmpS1669 = _M0L1uS649 >> 52;
  _M0L6_2atmpS1668 = _M0L6_2atmpS1669 & 2047ull;
  _M0L6_2atmpS1667 = (int32_t)_M0L6_2atmpS1668;
  _M0L6_2atmpS1666 = _M0L6_2aexpS648 + _M0L6_2atmpS1667;
  _M0L3expS650 = _M0L6_2atmpS1666 - 1022;
  _M0L6_2atmpS1665 = ~9218868437227405312ull;
  _M0L6_2atmpS1664 = _M0L1uS649 & _M0L6_2atmpS1665;
  _M0L6_2atmpS1663 = _M0L6_2atmpS1664 | 4602678819172646912ull;
  _M0L4fracS651 = *(double*)&_M0L6_2atmpS1663;
  _block_1961 = (struct _M0TUdiE*)moonbit_malloc(sizeof(struct _M0TUdiE));
  Moonbit_object_header(_block_1961)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_1961->$0 = _M0L4fracS651;
  _block_1961->$1 = _M0L3expS650;
  return _block_1961;
}

struct _M0TUdiE* _M0FPC14math9normalize(double _M0L1fS644) {
  double _M0L6_2atmpS1660;
  struct _M0TUdiE* _block_1963;
  #line 125 "C:\\Users\\31379\\.moon\\lib\\core\\math\\utils.mbt"
  #line 126 "C:\\Users\\31379\\.moon\\lib\\core\\math\\utils.mbt"
  _M0L6_2atmpS1660 = fabs(_M0L1fS644);
  if (_M0L6_2atmpS1660 < _M0FPC16double13min__positive) {
    double _M0L6_2atmpS1662 = (double)4503599627370496ll;
    double _M0L6_2atmpS1661 = _M0L1fS644 * _M0L6_2atmpS1662;
    struct _M0TUdiE* _block_1962 =
      (struct _M0TUdiE*)moonbit_malloc(sizeof(struct _M0TUdiE));
    Moonbit_object_header(_block_1962)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
    _block_1962->$0 = _M0L6_2atmpS1661;
    _block_1962->$1 = -52;
    return _block_1962;
  }
  _block_1963 = (struct _M0TUdiE*)moonbit_malloc(sizeof(struct _M0TUdiE));
  Moonbit_object_header(_block_1963)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_1963->$0 = _M0L1fS644;
  _block_1963->$1 = 0;
  return _block_1963;
}

moonbit_string_t _M0IPC15float5FloatPB4Show10to__string(float _M0L4selfS643) {
  double _M0L6_2atmpS1659;
  #line 16 "C:\\Users\\31379\\.moon\\lib\\core\\float\\methods.mbt"
  _M0L6_2atmpS1659 = (double)_M0L4selfS643;
  #line 17 "C:\\Users\\31379\\.moon\\lib\\core\\float\\methods.mbt"
  return _M0MPC16double6Double10to__string(_M0L6_2atmpS1659);
}

int32_t _M0MPC15float5Float7to__int(float _M0L4selfS642) {
  #line 43 "C:\\Users\\31379\\.moon\\lib\\core\\float\\to_int.mbt"
  if (_M0L4selfS642 != _M0L4selfS642) {
    return 0;
  } else if (_M0L4selfS642 >= 0x1.fffffffcp+30f) {
    return 2147483647;
  } else if (_M0L4selfS642 <= -0x1p+31f) {
    return (int32_t)0x80000000;
  } else {
    return (int32_t)_M0L4selfS642;
  }
}

struct _M0TPB5ArrayGfE* _M0MPC15array5Array4makeGfE(
  int32_t _M0L3lenS623,
  float _M0L4elemS625
) {
  struct _M0TPB5ArrayGfE* _M0L3arrS622;
  int32_t _M0L1iS624;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3arrS622 = _M0MPC15array5Array20unsafe__make__uninitGfE(_M0L3lenS623);
  _M0L1iS624 = 0;
  while (1) {
    if (_M0L1iS624 < _M0L3lenS623) {
      float* _M0L3bufS1651 = _M0L3arrS622->$0;
      int32_t _M0L6_2atmpS1652;
      _M0L3bufS1651[_M0L1iS624] = _M0L4elemS625;
      _M0L6_2atmpS1652 = _M0L1iS624 + 1;
      _M0L1iS624 = _M0L6_2atmpS1652;
      continue;
    }
    break;
  }
  return _M0L3arrS622;
}

struct _M0TPB5ArrayGbE* _M0MPC15array5Array4makeGbE(
  int32_t _M0L3lenS628,
  int32_t _M0L4elemS630
) {
  struct _M0TPB5ArrayGbE* _M0L3arrS627;
  int32_t _M0L1iS629;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3arrS627 = _M0MPC15array5Array20unsafe__make__uninitGbE(_M0L3lenS628);
  _M0L1iS629 = 0;
  while (1) {
    if (_M0L1iS629 < _M0L3lenS628) {
      uint8_t* _M0L3bufS1653 = _M0L3arrS627->$0;
      int32_t _M0L6_2atmpS1654;
      _M0L3bufS1653[_M0L1iS629] = _M0L4elemS630;
      _M0L6_2atmpS1654 = _M0L1iS629 + 1;
      _M0L1iS629 = _M0L6_2atmpS1654;
      continue;
    }
    break;
  }
  return _M0L3arrS627;
}

struct _M0TPB5ArrayGRPB5ArrayGfEE* _M0MPC15array5Array4makeGRPB5ArrayGfEE(
  int32_t _M0L3lenS633,
  struct _M0TPB5ArrayGfE* _M0L4elemS635
) {
  struct _M0TPB5ArrayGRPB5ArrayGfEE* _M0L3arrS632;
  int32_t _M0L1iS634;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3arrS632
  = _M0MPC15array5Array20unsafe__make__uninitGRPB5ArrayGfEE(_M0L3lenS633);
  _M0L1iS634 = 0;
  while (1) {
    if (_M0L1iS634 < _M0L3lenS633) {
      struct _M0TPB5ArrayGfE** _M0L3bufS1655 = _M0L3arrS632->$0;
      struct _M0TPB5ArrayGfE* _M0L6_2aoldS1897 =
        (struct _M0TPB5ArrayGfE*)_M0L3bufS1655[_M0L1iS634];
      int32_t _M0L6_2atmpS1656;
      moonbit_incref(_M0L4elemS635);
      if (_M0L6_2aoldS1897) {
        moonbit_decref(_M0L6_2aoldS1897);
      }
      _M0L3bufS1655[_M0L1iS634] = _M0L4elemS635;
      _M0L6_2atmpS1656 = _M0L1iS634 + 1;
      _M0L1iS634 = _M0L6_2atmpS1656;
      continue;
    } else {
      moonbit_decref(_M0L4elemS635);
    }
    break;
  }
  return _M0L3arrS632;
}

struct _M0TPB5ArrayGiE* _M0MPC15array5Array4makeGiE(
  int32_t _M0L3lenS638,
  int32_t _M0L4elemS640
) {
  struct _M0TPB5ArrayGiE* _M0L3arrS637;
  int32_t _M0L1iS639;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3arrS637 = _M0MPC15array5Array20unsafe__make__uninitGiE(_M0L3lenS638);
  _M0L1iS639 = 0;
  while (1) {
    if (_M0L1iS639 < _M0L3lenS638) {
      int32_t* _M0L3bufS1657 = _M0L3arrS637->$0;
      int32_t _M0L6_2atmpS1658;
      _M0L3bufS1657[_M0L1iS639] = _M0L4elemS640;
      _M0L6_2atmpS1658 = _M0L1iS639 + 1;
      _M0L1iS639 = _M0L6_2atmpS1658;
      continue;
    }
    break;
  }
  return _M0L3arrS637;
}

int32_t _M0MPC15array5Array3setGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS607,
  int32_t _M0L5indexS608,
  float _M0L5valueS609
) {
  int32_t _M0L3lenS606;
  #line 262 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS606 = _M0L4selfS607->$1;
  if (_M0L5indexS608 >= 0 && _M0L5indexS608 < _M0L3lenS606) {
    float* _M0L6_2atmpS1647;
    #line 268 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS1647 = _M0MPC15array5Array6bufferGfE(_M0L4selfS607);
    _M0L6_2atmpS1647[_M0L5indexS608] = _M0L5valueS609;
    moonbit_decref(_M0L6_2atmpS1647);
  } else {
    #line 267 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
  return 0;
}

int32_t _M0MPC15array5Array3setGbE(
  struct _M0TPB5ArrayGbE* _M0L4selfS611,
  int32_t _M0L5indexS612,
  int32_t _M0L5valueS613
) {
  int32_t _M0L3lenS610;
  #line 262 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS610 = _M0L4selfS611->$1;
  if (_M0L5indexS612 >= 0 && _M0L5indexS612 < _M0L3lenS610) {
    uint8_t* _M0L6_2atmpS1648;
    #line 268 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS1648 = _M0MPC15array5Array6bufferGbE(_M0L4selfS611);
    _M0L6_2atmpS1648[_M0L5indexS612] = _M0L5valueS613;
    moonbit_decref(_M0L6_2atmpS1648);
  } else {
    #line 267 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
  return 0;
}

int32_t _M0MPC15array5Array3setGRPB5ArrayGfEE(
  struct _M0TPB5ArrayGRPB5ArrayGfEE* _M0L4selfS615,
  int32_t _M0L5indexS616,
  struct _M0TPB5ArrayGfE* _M0L5valueS617
) {
  int32_t _M0L3lenS614;
  #line 262 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS614 = _M0L4selfS615->$1;
  if (_M0L5indexS616 >= 0 && _M0L5indexS616 < _M0L3lenS614) {
    struct _M0TPB5ArrayGfE** _M0L6_2atmpS1649;
    struct _M0TPB5ArrayGfE* _M0L6_2aoldS1898;
    #line 268 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS1649
    = _M0MPC15array5Array6bufferGRPB5ArrayGfEE(_M0L4selfS615);
    _M0L6_2aoldS1898
    = (struct _M0TPB5ArrayGfE*)_M0L6_2atmpS1649[_M0L5indexS616];
    if (_M0L6_2aoldS1898) {
      moonbit_decref(_M0L6_2aoldS1898);
    }
    _M0L6_2atmpS1649[_M0L5indexS616] = _M0L5valueS617;
    moonbit_decref(_M0L6_2atmpS1649);
  } else {
    moonbit_decref(_M0L5valueS617);
    #line 267 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
  return 0;
}

int32_t _M0MPC15array5Array3setGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS619,
  int32_t _M0L5indexS620,
  int32_t _M0L5valueS621
) {
  int32_t _M0L3lenS618;
  #line 262 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS618 = _M0L4selfS619->$1;
  if (_M0L5indexS620 >= 0 && _M0L5indexS620 < _M0L3lenS618) {
    int32_t* _M0L6_2atmpS1650;
    #line 268 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS1650 = _M0MPC15array5Array6bufferGiE(_M0L4selfS619);
    _M0L6_2atmpS1650[_M0L5indexS620] = _M0L5valueS621;
    moonbit_decref(_M0L6_2atmpS1650);
  } else {
    #line 267 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
  return 0;
}

float _M0MPC15array5Array2atGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS595,
  int32_t _M0L5indexS596
) {
  int32_t _M0L3lenS594;
  #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS594 = _M0L4selfS595->$1;
  if (_M0L5indexS596 >= 0 && _M0L5indexS596 < _M0L3lenS594) {
    float* _M0L6_2atmpS1643;
    float _result_1968;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS1643 = _M0MPC15array5Array6bufferGfE(_M0L4selfS595);
    _result_1968 = (float)_M0L6_2atmpS1643[_M0L5indexS596];
    moonbit_decref(_M0L6_2atmpS1643);
    return _result_1968;
  } else {
    #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
}

int32_t _M0MPC15array5Array2atGbE(
  struct _M0TPB5ArrayGbE* _M0L4selfS598,
  int32_t _M0L5indexS599
) {
  int32_t _M0L3lenS597;
  #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS597 = _M0L4selfS598->$1;
  if (_M0L5indexS599 >= 0 && _M0L5indexS599 < _M0L3lenS597) {
    uint8_t* _M0L6_2atmpS1644;
    int32_t _result_1969;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS1644 = _M0MPC15array5Array6bufferGbE(_M0L4selfS598);
    _result_1969 = (int32_t)_M0L6_2atmpS1644[_M0L5indexS599];
    moonbit_decref(_M0L6_2atmpS1644);
    return _result_1969;
  } else {
    #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
}

int32_t _M0MPC15array5Array2atGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS601,
  int32_t _M0L5indexS602
) {
  int32_t _M0L3lenS600;
  #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS600 = _M0L4selfS601->$1;
  if (_M0L5indexS602 >= 0 && _M0L5indexS602 < _M0L3lenS600) {
    int32_t* _M0L6_2atmpS1645;
    int32_t _result_1970;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS1645 = _M0MPC15array5Array6bufferGiE(_M0L4selfS601);
    _result_1970 = (int32_t)_M0L6_2atmpS1645[_M0L5indexS602];
    moonbit_decref(_M0L6_2atmpS1645);
    return _result_1970;
  } else {
    #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
}

struct _M0TPB5ArrayGfE* _M0MPC15array5Array2atGRPB5ArrayGfEE(
  struct _M0TPB5ArrayGRPB5ArrayGfEE* _M0L4selfS604,
  int32_t _M0L5indexS605
) {
  int32_t _M0L3lenS603;
  #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS603 = _M0L4selfS604->$1;
  if (_M0L5indexS605 >= 0 && _M0L5indexS605 < _M0L3lenS603) {
    struct _M0TPB5ArrayGfE** _M0L6_2atmpS1646;
    struct _M0TPB5ArrayGfE* _M0L6_2atmpS1899;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS1646
    = _M0MPC15array5Array6bufferGRPB5ArrayGfEE(_M0L4selfS604);
    _M0L6_2atmpS1899
    = (struct _M0TPB5ArrayGfE*)_M0L6_2atmpS1646[_M0L5indexS605];
    if (_M0L6_2atmpS1899) {
      moonbit_incref(_M0L6_2atmpS1899);
    }
    moonbit_decref(_M0L6_2atmpS1646);
    return _M0L6_2atmpS1899;
  } else {
    #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
}

int32_t _M0FPB7printlnGsE(moonbit_string_t _M0L5inputS593) {
  moonbit_string_t _M0L6_2atmpS1642;
  #line 36 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\console.mbt"
  #line 37 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\console.mbt"
  _M0L6_2atmpS1642 = _M0IPC16string6StringPB4Show10to__string(_M0L5inputS593);
  #line 37 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\console.mbt"
  moonbit_println(_M0L6_2atmpS1642);
  moonbit_decref(_M0L6_2atmpS1642);
  return 0;
}

moonbit_string_t _M0MPC16double6Double10to__string(double _M0L4selfS592) {
  #line 296 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double.mbt"
  #line 298 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double.mbt"
  return _M0FPB15ryu__to__string(_M0L4selfS592);
}

int32_t _M0MPC16double6Double7is__inf(double _M0L4selfS591) {
  #line 221 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double.mbt"
  return _M0L4selfS591 > _M0FPB18double__max__value
         || _M0L4selfS591 < _M0FPB18double__min__value;
}

int32_t _M0MPC16double6Double7is__nan(double _M0L4selfS590) {
  #line 196 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double.mbt"
  return _M0L4selfS590 != _M0L4selfS590;
}

moonbit_string_t _M0FPB15ryu__to__string(double _M0L3valS575) {
  uint64_t _M0L4bitsS578;
  uint64_t _M0L6_2atmpS1641;
  uint64_t _M0L6_2atmpS1640;
  int32_t _M0L8ieeeSignS579;
  uint64_t _M0L12ieeeMantissaS580;
  uint64_t _M0L6_2atmpS1639;
  uint64_t _M0L6_2atmpS1638;
  int32_t _M0L12ieeeExponentS581;
  struct _M0TPB17FloatingDecimal64* _M0L7_2abindS582;
  struct _M0TPB17FloatingDecimal64* _M0L1vS583;
  moonbit_string_t _result_1972;
  #line 668 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  if (_M0L3valS575 == 0x0p+0) {
    return (moonbit_string_t)moonbit_string_literal_1.data;
  }
  if (_M0L3valS575 >= -0x1p+53 && _M0L3valS575 <= 0x1p+53) {
    if (_M0L3valS575 >= -0x1p+31 && _M0L3valS575 <= 0x1.fffffffcp+30) {
      int32_t _M0L1iS576;
      double _M0L6_2atmpS1627;
      #line 683 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0L1iS576 = _M0MPC16double6Double7to__int(_M0L3valS575);
      _M0L6_2atmpS1627 = (double)_M0L1iS576;
      if (_M0L6_2atmpS1627 == _M0L3valS575) {
        #line 685 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        return _M0MPC13int3Int18to__string_2einner(_M0L1iS576, 10);
      }
    } else {
      int64_t _M0L1iS577;
      double _M0L6_2atmpS1628;
      #line 688 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0L1iS577 = _M0MPC16double6Double9to__int64(_M0L3valS575);
      _M0L6_2atmpS1628 = (double)_M0L1iS577;
      if (_M0L6_2atmpS1628 == _M0L3valS575) {
        #line 690 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        return _M0MPC15int645Int6418to__string_2einner(_M0L1iS577, 10);
      }
    }
  }
  _M0L4bitsS578 = *(int64_t*)&_M0L3valS575;
  _M0L6_2atmpS1641 = _M0L4bitsS578 >> 63;
  _M0L6_2atmpS1640 = _M0L6_2atmpS1641 & 1ull;
  _M0L8ieeeSignS579 = _M0L6_2atmpS1640 != 0ull;
  _M0L12ieeeMantissaS580 = _M0L4bitsS578 & 4503599627370495ull;
  _M0L6_2atmpS1639 = _M0L4bitsS578 >> 52;
  _M0L6_2atmpS1638 = _M0L6_2atmpS1639 & 2047ull;
  _M0L12ieeeExponentS581 = (int32_t)_M0L6_2atmpS1638;
  if (
    _M0L12ieeeExponentS581 == 2047
    || _M0L12ieeeExponentS581 == 0 && _M0L12ieeeMantissaS580 == 0ull
  ) {
    int32_t _M0L6_2atmpS1629 = _M0L12ieeeExponentS581 != 0;
    int32_t _M0L6_2atmpS1630 = _M0L12ieeeMantissaS580 != 0ull;
    #line 707 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    return _M0FPB18copy__special__str(_M0L8ieeeSignS579, _M0L6_2atmpS1629, _M0L6_2atmpS1630);
  }
  #line 709 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS582
  = _M0FPB15d2d__small__int(_M0L12ieeeMantissaS580, _M0L12ieeeExponentS581);
  if (_M0L7_2abindS582 == 0) {
    uint32_t _M0L6_2atmpS1631;
    if (_M0L7_2abindS582) {
      moonbit_decref(_M0L7_2abindS582);
    }
    _M0L6_2atmpS1631 = *(uint32_t*)&_M0L12ieeeExponentS581;
    #line 719 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L1vS583 = _M0FPB3d2d(_M0L12ieeeMantissaS580, _M0L6_2atmpS1631);
  } else {
    struct _M0TPB17FloatingDecimal64* _M0L7_2aSomeS584 = _M0L7_2abindS582;
    struct _M0TPB17FloatingDecimal64* _M0L4_2afS585 = _M0L7_2aSomeS584;
    struct _M0TPB17FloatingDecimal64* _M0L1xS586 = _M0L4_2afS585;
    while (1) {
      uint64_t _M0L8mantissaS1637 = _M0L1xS586->$0;
      uint64_t _M0L1qS587 = _M0L8mantissaS1637 / 10ull;
      uint64_t _M0L8mantissaS1635 = _M0L1xS586->$0;
      uint64_t _M0L6_2atmpS1636 = 10ull * _M0L1qS587;
      uint64_t _M0L1rS588 = _M0L8mantissaS1635 - _M0L6_2atmpS1636;
      int32_t _M0L8exponentS1634;
      int32_t _M0L6_2atmpS1633;
      struct _M0TPB17FloatingDecimal64* _M0L6_2atmpS1632;
      if (_M0L1rS588 != 0ull) {
        _M0L1vS583 = _M0L1xS586;
        break;
      }
      _M0L8exponentS1634 = _M0L1xS586->$1;
      moonbit_decref(_M0L1xS586);
      _M0L6_2atmpS1633 = _M0L8exponentS1634 + 1;
      _M0L6_2atmpS1632
      = (struct _M0TPB17FloatingDecimal64*)moonbit_malloc(sizeof(struct _M0TPB17FloatingDecimal64));
      Moonbit_object_header(_M0L6_2atmpS1632)->meta
      = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
      _M0L6_2atmpS1632->$0 = _M0L1qS587;
      _M0L6_2atmpS1632->$1 = _M0L6_2atmpS1633;
      _M0L1xS586 = _M0L6_2atmpS1632;
      continue;
      break;
    }
  }
  #line 721 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _result_1972 = _M0FPB9to__chars(_M0L1vS583, _M0L8ieeeSignS579);
  moonbit_decref(_M0L1vS583);
  return _result_1972;
}

struct _M0TPB17FloatingDecimal64* _M0FPB15d2d__small__int(
  uint64_t _M0L12ieeeMantissaS570,
  int32_t _M0L12ieeeExponentS572
) {
  uint64_t _M0L2m2S569;
  int32_t _M0L6_2atmpS1626;
  int32_t _M0L2e2S571;
  int32_t _M0L6_2atmpS1625;
  uint64_t _M0L6_2atmpS1624;
  uint64_t _M0L4maskS573;
  uint64_t _M0L8fractionS574;
  int32_t _M0L6_2atmpS1623;
  uint64_t _M0L6_2atmpS1622;
  struct _M0TPB17FloatingDecimal64* _M0L6_2atmpS1621;
  #line 637 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L2m2S569 = 4503599627370496ull | _M0L12ieeeMantissaS570;
  _M0L6_2atmpS1626 = _M0L12ieeeExponentS572 - 1023;
  _M0L2e2S571 = _M0L6_2atmpS1626 - 52;
  if (_M0L2e2S571 > 0) {
    return 0;
  }
  if (_M0L2e2S571 < -52) {
    return 0;
  }
  _M0L6_2atmpS1625 = -_M0L2e2S571;
  _M0L6_2atmpS1624 = 1ull << (_M0L6_2atmpS1625 & 63);
  _M0L4maskS573 = _M0L6_2atmpS1624 - 1ull;
  _M0L8fractionS574 = _M0L2m2S569 & _M0L4maskS573;
  if (_M0L8fractionS574 != 0ull) {
    return 0;
  }
  _M0L6_2atmpS1623 = -_M0L2e2S571;
  _M0L6_2atmpS1622 = _M0L2m2S569 >> (_M0L6_2atmpS1623 & 63);
  _M0L6_2atmpS1621
  = (struct _M0TPB17FloatingDecimal64*)moonbit_malloc(sizeof(struct _M0TPB17FloatingDecimal64));
  Moonbit_object_header(_M0L6_2atmpS1621)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L6_2atmpS1621->$0 = _M0L6_2atmpS1622;
  _M0L6_2atmpS1621->$1 = 0;
  return _M0L6_2atmpS1621;
}

moonbit_string_t _M0FPB9to__chars(
  struct _M0TPB17FloatingDecimal64* _M0L1vS537,
  int32_t _M0L4signS535
) {
  moonbit_bytes_t _M0L6resultS533;
  int32_t _M0Lm5indexS534;
  uint64_t _M0L6outputS536;
  int32_t _M0L7olengthS538;
  int32_t _M0L8exponentS1620;
  int32_t _M0L6_2atmpS1619;
  int32_t _M0Lm3expS539;
  int32_t _M0L6_2atmpS1618;
  int32_t _M0L6_2atmpS1616;
  int32_t _M0L18scientificNotationS540;
  #line 530 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6resultS533 = (moonbit_bytes_t)moonbit_make_bytes(25, 0);
  _M0Lm5indexS534 = 0;
  if (_M0L4signS535) {
    int32_t _M0L6_2atmpS1490 = _M0Lm5indexS534;
    int32_t _M0L6_2atmpS1491;
    if (
      _M0L6_2atmpS1490 < 0
      || _M0L6_2atmpS1490 >= Moonbit_array_length(_M0L6resultS533)
    ) {
      #line 535 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      moonbit_panic();
    }
    _M0L6resultS533[_M0L6_2atmpS1490] = 45;
    _M0L6_2atmpS1491 = _M0Lm5indexS534;
    _M0Lm5indexS534 = _M0L6_2atmpS1491 + 1;
  }
  _M0L6outputS536 = _M0L1vS537->$0;
  #line 539 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7olengthS538 = _M0FPB17decimal__length17(_M0L6outputS536);
  _M0L8exponentS1620 = _M0L1vS537->$1;
  _M0L6_2atmpS1619 = _M0L8exponentS1620 + _M0L7olengthS538;
  _M0Lm3expS539 = _M0L6_2atmpS1619 - 1;
  _M0L6_2atmpS1618 = _M0Lm3expS539;
  if (_M0L6_2atmpS1618 >= -6) {
    int32_t _M0L6_2atmpS1617 = _M0Lm3expS539;
    _M0L6_2atmpS1616 = _M0L6_2atmpS1617 < 21;
  } else {
    _M0L6_2atmpS1616 = 0;
  }
  _M0L18scientificNotationS540 = !_M0L6_2atmpS1616;
  if (_M0L18scientificNotationS540) {
    int32_t _M0L7_2abindS541 = _M0L7olengthS538 - 1;
    uint64_t _M0L6outputS542;
    int32_t _M0L1iS543 = 0;
    uint64_t _M0L6outputS544 = _M0L6outputS536;
    int32_t _M0L6_2atmpS1492;
    int32_t _M0L6_2atmpS1496;
    int32_t _M0L6_2atmpS1495;
    int32_t _M0L6_2atmpS1494;
    int32_t _M0L6_2atmpS1493;
    int32_t _M0L6_2atmpS1500;
    int32_t _M0L6_2atmpS1501;
    int32_t _M0L6_2atmpS1502;
    int32_t _M0L6_2atmpS1503;
    int32_t _M0L6_2atmpS1504;
    int32_t _M0L6_2atmpS1510;
    int32_t _M0L6_2atmpS1543;
    moonbit_string_t _result_1974;
    while (1) {
      if (_M0L1iS543 < _M0L7_2abindS541) {
        uint64_t _M0L1cS545 = _M0L6outputS544 % 10ull;
        int32_t _M0L6_2atmpS1549 = _M0Lm5indexS534;
        int32_t _M0L6_2atmpS1548 = _M0L6_2atmpS1549 + _M0L7olengthS538;
        int32_t _M0L6_2atmpS1544 = _M0L6_2atmpS1548 - _M0L1iS543;
        int32_t _M0L6_2atmpS1547 = (int32_t)_M0L1cS545;
        int32_t _M0L6_2atmpS1546 = 48 + _M0L6_2atmpS1547;
        int32_t _M0L6_2atmpS1545 = _M0L6_2atmpS1546 & 0xff;
        int32_t _M0L6_2atmpS1550;
        uint64_t _M0L6_2atmpS1551;
        if (
          _M0L6_2atmpS1544 < 0
          || _M0L6_2atmpS1544 >= Moonbit_array_length(_M0L6resultS533)
        ) {
          #line 547 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
          moonbit_panic();
        }
        _M0L6resultS533[_M0L6_2atmpS1544] = _M0L6_2atmpS1545;
        _M0L6_2atmpS1550 = _M0L1iS543 + 1;
        _M0L6_2atmpS1551 = _M0L6outputS544 / 10ull;
        _M0L1iS543 = _M0L6_2atmpS1550;
        _M0L6outputS544 = _M0L6_2atmpS1551;
        continue;
      } else {
        _M0L6outputS542 = _M0L6outputS544;
      }
      break;
    }
    _M0L6_2atmpS1492 = _M0Lm5indexS534;
    _M0L6_2atmpS1496 = (int32_t)_M0L6outputS542;
    _M0L6_2atmpS1495 = _M0L6_2atmpS1496 % 10;
    _M0L6_2atmpS1494 = 48 + _M0L6_2atmpS1495;
    _M0L6_2atmpS1493 = _M0L6_2atmpS1494 & 0xff;
    if (
      _M0L6_2atmpS1492 < 0
      || _M0L6_2atmpS1492 >= Moonbit_array_length(_M0L6resultS533)
    ) {
      #line 552 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      moonbit_panic();
    }
    _M0L6resultS533[_M0L6_2atmpS1492] = _M0L6_2atmpS1493;
    if (_M0L7olengthS538 > 1) {
      int32_t _M0L6_2atmpS1498 = _M0Lm5indexS534;
      int32_t _M0L6_2atmpS1497 = _M0L6_2atmpS1498 + 1;
      if (
        _M0L6_2atmpS1497 < 0
        || _M0L6_2atmpS1497 >= Moonbit_array_length(_M0L6resultS533)
      ) {
        #line 554 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS533[_M0L6_2atmpS1497] = 46;
    } else {
      int32_t _M0L6_2atmpS1499 = _M0Lm5indexS534;
      _M0Lm5indexS534 = _M0L6_2atmpS1499 - 1;
    }
    _M0L6_2atmpS1500 = _M0Lm5indexS534;
    _M0L6_2atmpS1501 = _M0L7olengthS538 + 1;
    _M0Lm5indexS534 = _M0L6_2atmpS1500 + _M0L6_2atmpS1501;
    _M0L6_2atmpS1502 = _M0Lm5indexS534;
    if (
      _M0L6_2atmpS1502 < 0
      || _M0L6_2atmpS1502 >= Moonbit_array_length(_M0L6resultS533)
    ) {
      #line 562 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      moonbit_panic();
    }
    _M0L6resultS533[_M0L6_2atmpS1502] = 101;
    _M0L6_2atmpS1503 = _M0Lm5indexS534;
    _M0Lm5indexS534 = _M0L6_2atmpS1503 + 1;
    _M0L6_2atmpS1504 = _M0Lm3expS539;
    if (_M0L6_2atmpS1504 < 0) {
      int32_t _M0L6_2atmpS1505 = _M0Lm5indexS534;
      int32_t _M0L6_2atmpS1506;
      int32_t _M0L6_2atmpS1507;
      if (
        _M0L6_2atmpS1505 < 0
        || _M0L6_2atmpS1505 >= Moonbit_array_length(_M0L6resultS533)
      ) {
        #line 565 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS533[_M0L6_2atmpS1505] = 45;
      _M0L6_2atmpS1506 = _M0Lm5indexS534;
      _M0Lm5indexS534 = _M0L6_2atmpS1506 + 1;
      _M0L6_2atmpS1507 = _M0Lm3expS539;
      _M0Lm3expS539 = -_M0L6_2atmpS1507;
    } else {
      int32_t _M0L6_2atmpS1508 = _M0Lm5indexS534;
      int32_t _M0L6_2atmpS1509;
      if (
        _M0L6_2atmpS1508 < 0
        || _M0L6_2atmpS1508 >= Moonbit_array_length(_M0L6resultS533)
      ) {
        #line 569 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS533[_M0L6_2atmpS1508] = 43;
      _M0L6_2atmpS1509 = _M0Lm5indexS534;
      _M0Lm5indexS534 = _M0L6_2atmpS1509 + 1;
    }
    _M0L6_2atmpS1510 = _M0Lm3expS539;
    if (_M0L6_2atmpS1510 >= 100) {
      int32_t _M0L6_2atmpS1526 = _M0Lm3expS539;
      int32_t _M0L1aS547 = _M0L6_2atmpS1526 / 100;
      int32_t _M0L6_2atmpS1525 = _M0Lm3expS539;
      int32_t _M0L6_2atmpS1524 = _M0L6_2atmpS1525 / 10;
      int32_t _M0L1bS548 = _M0L6_2atmpS1524 % 10;
      int32_t _M0L6_2atmpS1523 = _M0Lm3expS539;
      int32_t _M0L1cS549 = _M0L6_2atmpS1523 % 10;
      int32_t _M0L6_2atmpS1511 = _M0Lm5indexS534;
      int32_t _M0L6_2atmpS1513 = 48 + _M0L1aS547;
      int32_t _M0L6_2atmpS1512 = _M0L6_2atmpS1513 & 0xff;
      int32_t _M0L6_2atmpS1517;
      int32_t _M0L6_2atmpS1514;
      int32_t _M0L6_2atmpS1516;
      int32_t _M0L6_2atmpS1515;
      int32_t _M0L6_2atmpS1521;
      int32_t _M0L6_2atmpS1518;
      int32_t _M0L6_2atmpS1520;
      int32_t _M0L6_2atmpS1519;
      int32_t _M0L6_2atmpS1522;
      if (
        _M0L6_2atmpS1511 < 0
        || _M0L6_2atmpS1511 >= Moonbit_array_length(_M0L6resultS533)
      ) {
        #line 576 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS533[_M0L6_2atmpS1511] = _M0L6_2atmpS1512;
      _M0L6_2atmpS1517 = _M0Lm5indexS534;
      _M0L6_2atmpS1514 = _M0L6_2atmpS1517 + 1;
      _M0L6_2atmpS1516 = 48 + _M0L1bS548;
      _M0L6_2atmpS1515 = _M0L6_2atmpS1516 & 0xff;
      if (
        _M0L6_2atmpS1514 < 0
        || _M0L6_2atmpS1514 >= Moonbit_array_length(_M0L6resultS533)
      ) {
        #line 577 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS533[_M0L6_2atmpS1514] = _M0L6_2atmpS1515;
      _M0L6_2atmpS1521 = _M0Lm5indexS534;
      _M0L6_2atmpS1518 = _M0L6_2atmpS1521 + 2;
      _M0L6_2atmpS1520 = 48 + _M0L1cS549;
      _M0L6_2atmpS1519 = _M0L6_2atmpS1520 & 0xff;
      if (
        _M0L6_2atmpS1518 < 0
        || _M0L6_2atmpS1518 >= Moonbit_array_length(_M0L6resultS533)
      ) {
        #line 578 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS533[_M0L6_2atmpS1518] = _M0L6_2atmpS1519;
      _M0L6_2atmpS1522 = _M0Lm5indexS534;
      _M0Lm5indexS534 = _M0L6_2atmpS1522 + 3;
    } else {
      int32_t _M0L6_2atmpS1527 = _M0Lm3expS539;
      if (_M0L6_2atmpS1527 >= 10) {
        int32_t _M0L6_2atmpS1537 = _M0Lm3expS539;
        int32_t _M0L1aS550 = _M0L6_2atmpS1537 / 10;
        int32_t _M0L6_2atmpS1536 = _M0Lm3expS539;
        int32_t _M0L1bS551 = _M0L6_2atmpS1536 % 10;
        int32_t _M0L6_2atmpS1528 = _M0Lm5indexS534;
        int32_t _M0L6_2atmpS1530 = 48 + _M0L1aS550;
        int32_t _M0L6_2atmpS1529 = _M0L6_2atmpS1530 & 0xff;
        int32_t _M0L6_2atmpS1534;
        int32_t _M0L6_2atmpS1531;
        int32_t _M0L6_2atmpS1533;
        int32_t _M0L6_2atmpS1532;
        int32_t _M0L6_2atmpS1535;
        if (
          _M0L6_2atmpS1528 < 0
          || _M0L6_2atmpS1528 >= Moonbit_array_length(_M0L6resultS533)
        ) {
          #line 583 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
          moonbit_panic();
        }
        _M0L6resultS533[_M0L6_2atmpS1528] = _M0L6_2atmpS1529;
        _M0L6_2atmpS1534 = _M0Lm5indexS534;
        _M0L6_2atmpS1531 = _M0L6_2atmpS1534 + 1;
        _M0L6_2atmpS1533 = 48 + _M0L1bS551;
        _M0L6_2atmpS1532 = _M0L6_2atmpS1533 & 0xff;
        if (
          _M0L6_2atmpS1531 < 0
          || _M0L6_2atmpS1531 >= Moonbit_array_length(_M0L6resultS533)
        ) {
          #line 584 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
          moonbit_panic();
        }
        _M0L6resultS533[_M0L6_2atmpS1531] = _M0L6_2atmpS1532;
        _M0L6_2atmpS1535 = _M0Lm5indexS534;
        _M0Lm5indexS534 = _M0L6_2atmpS1535 + 2;
      } else {
        int32_t _M0L6_2atmpS1538 = _M0Lm5indexS534;
        int32_t _M0L6_2atmpS1541 = _M0Lm3expS539;
        int32_t _M0L6_2atmpS1540 = 48 + _M0L6_2atmpS1541;
        int32_t _M0L6_2atmpS1539 = _M0L6_2atmpS1540 & 0xff;
        int32_t _M0L6_2atmpS1542;
        if (
          _M0L6_2atmpS1538 < 0
          || _M0L6_2atmpS1538 >= Moonbit_array_length(_M0L6resultS533)
        ) {
          #line 587 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
          moonbit_panic();
        }
        _M0L6resultS533[_M0L6_2atmpS1538] = _M0L6_2atmpS1539;
        _M0L6_2atmpS1542 = _M0Lm5indexS534;
        _M0Lm5indexS534 = _M0L6_2atmpS1542 + 1;
      }
    }
    _M0L6_2atmpS1543 = _M0Lm5indexS534;
    #line 590 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _result_1974
    = _M0FPB19string__from__bytes(_M0L6resultS533, 0, _M0L6_2atmpS1543);
    moonbit_decref(_M0L6resultS533);
    return _result_1974;
  } else {
    int32_t _M0L6_2atmpS1552 = _M0Lm3expS539;
    int32_t _M0L6_2atmpS1615;
    moonbit_string_t _result_1980;
    if (_M0L6_2atmpS1552 < 0) {
      int32_t _M0L6_2atmpS1553 = _M0Lm5indexS534;
      int32_t _M0L6_2atmpS1555;
      int32_t _M0L6_2atmpS1554;
      int32_t _M0L6_2atmpS1556;
      int32_t _M0L1iS552;
      int32_t _M0L6_2atmpS1571;
      int32_t _M0L6_2atmpS1573;
      int32_t _M0L6_2atmpS1572;
      int32_t _M0L7currentS554;
      int32_t _M0L1iS555;
      uint64_t _M0L6outputS556;
      if (
        _M0L6_2atmpS1553 < 0
        || _M0L6_2atmpS1553 >= Moonbit_array_length(_M0L6resultS533)
      ) {
        #line 595 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS533[_M0L6_2atmpS1553] = 48;
      _M0L6_2atmpS1555 = _M0Lm5indexS534;
      _M0L6_2atmpS1554 = _M0L6_2atmpS1555 + 1;
      if (
        _M0L6_2atmpS1554 < 0
        || _M0L6_2atmpS1554 >= Moonbit_array_length(_M0L6resultS533)
      ) {
        #line 596 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS533[_M0L6_2atmpS1554] = 46;
      _M0L6_2atmpS1556 = _M0Lm5indexS534;
      _M0Lm5indexS534 = _M0L6_2atmpS1556 + 2;
      _M0L1iS552 = -1;
      while (1) {
        int32_t _M0L6_2atmpS1557 = _M0Lm3expS539;
        if (_M0L1iS552 > _M0L6_2atmpS1557) {
          int32_t _M0L6_2atmpS1560 = _M0Lm5indexS534;
          int32_t _M0L6_2atmpS1559 = _M0L6_2atmpS1560 - _M0L1iS552;
          int32_t _M0L6_2atmpS1558 = _M0L6_2atmpS1559 - 1;
          int32_t _M0L6_2atmpS1561;
          if (
            _M0L6_2atmpS1558 < 0
            || _M0L6_2atmpS1558 >= Moonbit_array_length(_M0L6resultS533)
          ) {
            #line 599 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
            moonbit_panic();
          }
          _M0L6resultS533[_M0L6_2atmpS1558] = 48;
          _M0L6_2atmpS1561 = _M0L1iS552 - 1;
          _M0L1iS552 = _M0L6_2atmpS1561;
          continue;
        }
        break;
      }
      _M0L6_2atmpS1571 = _M0Lm5indexS534;
      _M0L6_2atmpS1573 = _M0Lm3expS539;
      _M0L6_2atmpS1572 = -1 - _M0L6_2atmpS1573;
      _M0L7currentS554 = _M0L6_2atmpS1571 + _M0L6_2atmpS1572;
      _M0L1iS555 = 0;
      _M0L6outputS556 = _M0L6outputS536;
      while (1) {
        if (_M0L1iS555 < _M0L7olengthS538) {
          int32_t _M0L6_2atmpS1568 = _M0L7currentS554 + _M0L7olengthS538;
          int32_t _M0L6_2atmpS1567 = _M0L6_2atmpS1568 - _M0L1iS555;
          int32_t _M0L6_2atmpS1562 = _M0L6_2atmpS1567 - 1;
          uint64_t _M0L6_2atmpS1566 = _M0L6outputS556 % 10ull;
          int32_t _M0L6_2atmpS1565 = (int32_t)_M0L6_2atmpS1566;
          int32_t _M0L6_2atmpS1564 = 48 + _M0L6_2atmpS1565;
          int32_t _M0L6_2atmpS1563 = _M0L6_2atmpS1564 & 0xff;
          int32_t _M0L6_2atmpS1569;
          uint64_t _M0L6_2atmpS1570;
          if (
            _M0L6_2atmpS1562 < 0
            || _M0L6_2atmpS1562 >= Moonbit_array_length(_M0L6resultS533)
          ) {
            #line 603 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
            moonbit_panic();
          }
          _M0L6resultS533[_M0L6_2atmpS1562] = _M0L6_2atmpS1563;
          _M0L6_2atmpS1569 = _M0L1iS555 + 1;
          _M0L6_2atmpS1570 = _M0L6outputS556 / 10ull;
          _M0L1iS555 = _M0L6_2atmpS1569;
          _M0L6outputS556 = _M0L6_2atmpS1570;
          continue;
        }
        break;
      }
      _M0Lm5indexS534 = _M0L7currentS554 + _M0L7olengthS538;
    } else {
      int32_t _M0L6_2atmpS1575 = _M0Lm3expS539;
      int32_t _M0L6_2atmpS1574 = _M0L6_2atmpS1575 + 1;
      if (_M0L6_2atmpS1574 >= _M0L7olengthS538) {
        int32_t _M0L1iS558 = 0;
        uint64_t _M0L6outputS559 = _M0L6outputS536;
        int32_t _M0L6_2atmpS1586;
        int32_t _M0L6_2atmpS1591;
        int32_t _M0L7_2abindS561;
        int32_t _M0L1iS562;
        int32_t _M0L6_2atmpS1592;
        int32_t _M0L6_2atmpS1595;
        int32_t _M0L6_2atmpS1594;
        int32_t _M0L6_2atmpS1593;
        while (1) {
          if (_M0L1iS558 < _M0L7olengthS538) {
            int32_t _M0L6_2atmpS1583 = _M0Lm5indexS534;
            int32_t _M0L6_2atmpS1582 = _M0L6_2atmpS1583 + _M0L7olengthS538;
            int32_t _M0L6_2atmpS1581 = _M0L6_2atmpS1582 - _M0L1iS558;
            int32_t _M0L6_2atmpS1576 = _M0L6_2atmpS1581 - 1;
            uint64_t _M0L6_2atmpS1580 = _M0L6outputS559 % 10ull;
            int32_t _M0L6_2atmpS1579 = (int32_t)_M0L6_2atmpS1580;
            int32_t _M0L6_2atmpS1578 = 48 + _M0L6_2atmpS1579;
            int32_t _M0L6_2atmpS1577 = _M0L6_2atmpS1578 & 0xff;
            int32_t _M0L6_2atmpS1584;
            uint64_t _M0L6_2atmpS1585;
            if (
              _M0L6_2atmpS1576 < 0
              || _M0L6_2atmpS1576 >= Moonbit_array_length(_M0L6resultS533)
            ) {
              #line 610 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
              moonbit_panic();
            }
            _M0L6resultS533[_M0L6_2atmpS1576] = _M0L6_2atmpS1577;
            _M0L6_2atmpS1584 = _M0L1iS558 + 1;
            _M0L6_2atmpS1585 = _M0L6outputS559 / 10ull;
            _M0L1iS558 = _M0L6_2atmpS1584;
            _M0L6outputS559 = _M0L6_2atmpS1585;
            continue;
          }
          break;
        }
        _M0L6_2atmpS1586 = _M0Lm5indexS534;
        _M0Lm5indexS534 = _M0L6_2atmpS1586 + _M0L7olengthS538;
        _M0L6_2atmpS1591 = _M0Lm3expS539;
        _M0L7_2abindS561 = _M0L6_2atmpS1591 + 1;
        _M0L1iS562 = _M0L7olengthS538;
        while (1) {
          if (_M0L1iS562 < _M0L7_2abindS561) {
            int32_t _M0L6_2atmpS1589 = _M0Lm5indexS534;
            int32_t _M0L6_2atmpS1588 = _M0L6_2atmpS1589 + _M0L1iS562;
            int32_t _M0L6_2atmpS1587 = _M0L6_2atmpS1588 - _M0L7olengthS538;
            int32_t _M0L6_2atmpS1590;
            if (
              _M0L6_2atmpS1587 < 0
              || _M0L6_2atmpS1587 >= Moonbit_array_length(_M0L6resultS533)
            ) {
              #line 615 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
              moonbit_panic();
            }
            _M0L6resultS533[_M0L6_2atmpS1587] = 48;
            _M0L6_2atmpS1590 = _M0L1iS562 + 1;
            _M0L1iS562 = _M0L6_2atmpS1590;
            continue;
          }
          break;
        }
        _M0L6_2atmpS1592 = _M0Lm5indexS534;
        _M0L6_2atmpS1595 = _M0Lm3expS539;
        _M0L6_2atmpS1594 = _M0L6_2atmpS1595 + 1;
        _M0L6_2atmpS1593 = _M0L6_2atmpS1594 - _M0L7olengthS538;
        _M0Lm5indexS534 = _M0L6_2atmpS1592 + _M0L6_2atmpS1593;
      } else {
        int32_t _M0L6_2atmpS1612 = _M0Lm5indexS534;
        int32_t _M0L6_2atmpS1611 = _M0L6_2atmpS1612 + 1;
        int32_t _M0L1iS564 = 0;
        int32_t _M0L7currentS565 = _M0L6_2atmpS1611;
        uint64_t _M0L6outputS566 = _M0L6outputS536;
        int32_t _M0L6_2atmpS1613;
        int32_t _M0L6_2atmpS1614;
        while (1) {
          if (_M0L1iS564 < _M0L7olengthS538) {
            int32_t _M0L6_2atmpS1607 = _M0L7olengthS538 - _M0L1iS564;
            int32_t _M0L6_2atmpS1605 = _M0L6_2atmpS1607 - 1;
            int32_t _M0L6_2atmpS1606 = _M0Lm3expS539;
            int32_t _M0L7currentS567;
            int32_t _M0L6_2atmpS1602;
            int32_t _M0L6_2atmpS1601;
            int32_t _M0L6_2atmpS1596;
            uint64_t _M0L6_2atmpS1600;
            int32_t _M0L6_2atmpS1599;
            int32_t _M0L6_2atmpS1598;
            int32_t _M0L6_2atmpS1597;
            int32_t _M0L6_2atmpS1603;
            uint64_t _M0L6_2atmpS1604;
            if (_M0L6_2atmpS1605 == _M0L6_2atmpS1606) {
              int32_t _M0L6_2atmpS1610 = _M0L7currentS565 + _M0L7olengthS538;
              int32_t _M0L6_2atmpS1609 = _M0L6_2atmpS1610 - _M0L1iS564;
              int32_t _M0L6_2atmpS1608 = _M0L6_2atmpS1609 - 1;
              if (
                _M0L6_2atmpS1608 < 0
                || _M0L6_2atmpS1608 >= Moonbit_array_length(_M0L6resultS533)
              ) {
                #line 622 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
                moonbit_panic();
              }
              _M0L6resultS533[_M0L6_2atmpS1608] = 46;
              _M0L7currentS567 = _M0L7currentS565 - 1;
            } else {
              _M0L7currentS567 = _M0L7currentS565;
            }
            _M0L6_2atmpS1602 = _M0L7currentS567 + _M0L7olengthS538;
            _M0L6_2atmpS1601 = _M0L6_2atmpS1602 - _M0L1iS564;
            _M0L6_2atmpS1596 = _M0L6_2atmpS1601 - 1;
            _M0L6_2atmpS1600 = _M0L6outputS566 % 10ull;
            _M0L6_2atmpS1599 = (int32_t)_M0L6_2atmpS1600;
            _M0L6_2atmpS1598 = 48 + _M0L6_2atmpS1599;
            _M0L6_2atmpS1597 = _M0L6_2atmpS1598 & 0xff;
            if (
              _M0L6_2atmpS1596 < 0
              || _M0L6_2atmpS1596 >= Moonbit_array_length(_M0L6resultS533)
            ) {
              #line 627 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
              moonbit_panic();
            }
            _M0L6resultS533[_M0L6_2atmpS1596] = _M0L6_2atmpS1597;
            _M0L6_2atmpS1603 = _M0L1iS564 + 1;
            _M0L6_2atmpS1604 = _M0L6outputS566 / 10ull;
            _M0L1iS564 = _M0L6_2atmpS1603;
            _M0L7currentS565 = _M0L7currentS567;
            _M0L6outputS566 = _M0L6_2atmpS1604;
            continue;
          }
          break;
        }
        _M0L6_2atmpS1613 = _M0Lm5indexS534;
        _M0L6_2atmpS1614 = _M0L7olengthS538 + 1;
        _M0Lm5indexS534 = _M0L6_2atmpS1613 + _M0L6_2atmpS1614;
      }
    }
    _M0L6_2atmpS1615 = _M0Lm5indexS534;
    #line 632 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _result_1980
    = _M0FPB19string__from__bytes(_M0L6resultS533, 0, _M0L6_2atmpS1615);
    moonbit_decref(_M0L6resultS533);
    return _result_1980;
  }
}

struct _M0TPB17FloatingDecimal64* _M0FPB3d2d(
  uint64_t _M0L12ieeeMantissaS479,
  uint32_t _M0L12ieeeExponentS478
) {
  int32_t _M0Lm2e2S476;
  uint64_t _M0Lm2m2S477;
  uint64_t _M0L6_2atmpS1489;
  uint64_t _M0L6_2atmpS1488;
  int32_t _M0L4evenS480;
  uint64_t _M0L6_2atmpS1487;
  uint64_t _M0L2mvS481;
  int32_t _M0L7mmShiftS482;
  uint64_t _M0Lm2vrS483;
  uint64_t _M0Lm2vpS484;
  uint64_t _M0Lm2vmS485;
  int32_t _M0Lm3e10S486;
  int32_t _M0Lm17vmIsTrailingZerosS487;
  int32_t _M0Lm17vrIsTrailingZerosS488;
  int32_t _M0L6_2atmpS1389;
  int32_t _M0Lm7removedS507;
  int32_t _M0Lm16lastRemovedDigitS508;
  uint64_t _M0Lm6outputS509;
  int32_t _M0L6_2atmpS1485;
  int32_t _M0L6_2atmpS1486;
  int32_t _M0L3expS532;
  uint64_t _M0L6_2atmpS1484;
  struct _M0TPB17FloatingDecimal64* _block_1986;
  #line 347 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0Lm2e2S476 = 0;
  _M0Lm2m2S477 = 0ull;
  if (_M0L12ieeeExponentS478 == 0u) {
    _M0Lm2e2S476 = -1076;
    _M0Lm2m2S477 = _M0L12ieeeMantissaS479;
  } else {
    int32_t _M0L6_2atmpS1388 = *(int32_t*)&_M0L12ieeeExponentS478;
    int32_t _M0L6_2atmpS1387 = _M0L6_2atmpS1388 - 1023;
    int32_t _M0L6_2atmpS1386 = _M0L6_2atmpS1387 - 52;
    _M0Lm2e2S476 = _M0L6_2atmpS1386 - 2;
    _M0Lm2m2S477 = 4503599627370496ull | _M0L12ieeeMantissaS479;
  }
  _M0L6_2atmpS1489 = _M0Lm2m2S477;
  _M0L6_2atmpS1488 = _M0L6_2atmpS1489 & 1ull;
  _M0L4evenS480 = _M0L6_2atmpS1488 == 0ull;
  _M0L6_2atmpS1487 = _M0Lm2m2S477;
  _M0L2mvS481 = 4ull * _M0L6_2atmpS1487;
  _M0L7mmShiftS482
  = _M0L12ieeeMantissaS479 != 0ull || _M0L12ieeeExponentS478 <= 1u;
  _M0Lm2vrS483 = 0ull;
  _M0Lm2vpS484 = 0ull;
  _M0Lm2vmS485 = 0ull;
  _M0Lm3e10S486 = 0;
  _M0Lm17vmIsTrailingZerosS487 = 0;
  _M0Lm17vrIsTrailingZerosS488 = 0;
  _M0L6_2atmpS1389 = _M0Lm2e2S476;
  if (_M0L6_2atmpS1389 >= 0) {
    int32_t _M0L6_2atmpS1411 = _M0Lm2e2S476;
    int32_t _M0L6_2atmpS1407;
    int32_t _M0L6_2atmpS1410;
    int32_t _M0L6_2atmpS1409;
    int32_t _M0L6_2atmpS1408;
    int32_t _M0L1qS489;
    int32_t _M0L6_2atmpS1406;
    int32_t _M0L6_2atmpS1405;
    int32_t _M0L1kS490;
    int32_t _M0L6_2atmpS1404;
    int32_t _M0L6_2atmpS1403;
    int32_t _M0L6_2atmpS1402;
    int32_t _M0L1iS491;
    struct _M0TPB8Pow5Pair _M0L4pow5S492;
    uint64_t _M0L6_2atmpS1401;
    struct _M0TPB19MulShiftAll64Result _M0L7_2abindS493;
    uint64_t _M0L8_2avrOutS494;
    uint64_t _M0L8_2avpOutS495;
    uint64_t _M0L8_2avmOutS496;
    #line 383 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1407 = _M0FPB9log10Pow2(_M0L6_2atmpS1411);
    _M0L6_2atmpS1410 = _M0Lm2e2S476;
    _M0L6_2atmpS1409 = _M0L6_2atmpS1410 > 3;
    #line 383 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1408 = _M0MPC14bool4Bool7to__int(_M0L6_2atmpS1409);
    _M0L1qS489 = _M0L6_2atmpS1407 - _M0L6_2atmpS1408;
    _M0Lm3e10S486 = _M0L1qS489;
    #line 385 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1406 = _M0FPB8pow5bits(_M0L1qS489);
    _M0L6_2atmpS1405 = 125 + _M0L6_2atmpS1406;
    _M0L1kS490 = _M0L6_2atmpS1405 - 1;
    _M0L6_2atmpS1404 = _M0Lm2e2S476;
    _M0L6_2atmpS1403 = -_M0L6_2atmpS1404;
    _M0L6_2atmpS1402 = _M0L6_2atmpS1403 + _M0L1qS489;
    _M0L1iS491 = _M0L6_2atmpS1402 + _M0L1kS490;
    #line 387 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L4pow5S492 = _M0FPB22double__computeInvPow5(_M0L1qS489);
    _M0L6_2atmpS1401 = _M0Lm2m2S477;
    #line 388 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L7_2abindS493
    = _M0FPB13mulShiftAll64(_M0L6_2atmpS1401, _M0L4pow5S492, _M0L1iS491, _M0L7mmShiftS482);
    _M0L8_2avrOutS494 = _M0L7_2abindS493.$0;
    _M0L8_2avpOutS495 = _M0L7_2abindS493.$1;
    _M0L8_2avmOutS496 = _M0L7_2abindS493.$2;
    _M0Lm2vrS483 = _M0L8_2avrOutS494;
    _M0Lm2vpS484 = _M0L8_2avpOutS495;
    _M0Lm2vmS485 = _M0L8_2avmOutS496;
    if (_M0L1qS489 <= 21) {
      int32_t _M0L6_2atmpS1397 = (int32_t)_M0L2mvS481;
      uint64_t _M0L6_2atmpS1400 = _M0L2mvS481 / 5ull;
      int32_t _M0L6_2atmpS1399 = (int32_t)_M0L6_2atmpS1400;
      int32_t _M0L6_2atmpS1398 = 5 * _M0L6_2atmpS1399;
      int32_t _M0L6mvMod5S497 = _M0L6_2atmpS1397 - _M0L6_2atmpS1398;
      if (_M0L6mvMod5S497 == 0) {
        #line 400 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0Lm17vrIsTrailingZerosS488
        = _M0FPB18multipleOfPowerOf5(_M0L2mvS481, _M0L1qS489);
      } else if (_M0L4evenS480) {
        uint64_t _M0L6_2atmpS1391 = _M0L2mvS481 - 1ull;
        uint64_t _M0L6_2atmpS1392;
        uint64_t _M0L6_2atmpS1390;
        #line 406 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0L6_2atmpS1392 = _M0MPC14bool4Bool10to__uint64(_M0L7mmShiftS482);
        _M0L6_2atmpS1390 = _M0L6_2atmpS1391 - _M0L6_2atmpS1392;
        #line 405 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0Lm17vmIsTrailingZerosS487
        = _M0FPB18multipleOfPowerOf5(_M0L6_2atmpS1390, _M0L1qS489);
      } else {
        uint64_t _M0L6_2atmpS1393 = _M0Lm2vpS484;
        uint64_t _M0L6_2atmpS1396 = _M0L2mvS481 + 2ull;
        int32_t _M0L6_2atmpS1395;
        uint64_t _M0L6_2atmpS1394;
        #line 410 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0L6_2atmpS1395
        = _M0FPB18multipleOfPowerOf5(_M0L6_2atmpS1396, _M0L1qS489);
        #line 410 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0L6_2atmpS1394 = _M0MPC14bool4Bool10to__uint64(_M0L6_2atmpS1395);
        _M0Lm2vpS484 = _M0L6_2atmpS1393 - _M0L6_2atmpS1394;
      }
    }
  } else {
    int32_t _M0L6_2atmpS1425 = _M0Lm2e2S476;
    int32_t _M0L6_2atmpS1424 = -_M0L6_2atmpS1425;
    int32_t _M0L6_2atmpS1419;
    int32_t _M0L6_2atmpS1423;
    int32_t _M0L6_2atmpS1422;
    int32_t _M0L6_2atmpS1421;
    int32_t _M0L6_2atmpS1420;
    int32_t _M0L1qS498;
    int32_t _M0L6_2atmpS1412;
    int32_t _M0L6_2atmpS1418;
    int32_t _M0L6_2atmpS1417;
    int32_t _M0L1iS499;
    int32_t _M0L6_2atmpS1416;
    int32_t _M0L1kS500;
    int32_t _M0L1jS501;
    struct _M0TPB8Pow5Pair _M0L4pow5S502;
    uint64_t _M0L6_2atmpS1415;
    struct _M0TPB19MulShiftAll64Result _M0L7_2abindS503;
    uint64_t _M0L8_2avrOutS504;
    uint64_t _M0L8_2avpOutS505;
    uint64_t _M0L8_2avmOutS506;
    #line 415 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1419 = _M0FPB9log10Pow5(_M0L6_2atmpS1424);
    _M0L6_2atmpS1423 = _M0Lm2e2S476;
    _M0L6_2atmpS1422 = -_M0L6_2atmpS1423;
    _M0L6_2atmpS1421 = _M0L6_2atmpS1422 > 1;
    #line 415 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1420 = _M0MPC14bool4Bool7to__int(_M0L6_2atmpS1421);
    _M0L1qS498 = _M0L6_2atmpS1419 - _M0L6_2atmpS1420;
    _M0L6_2atmpS1412 = _M0Lm2e2S476;
    _M0Lm3e10S486 = _M0L1qS498 + _M0L6_2atmpS1412;
    _M0L6_2atmpS1418 = _M0Lm2e2S476;
    _M0L6_2atmpS1417 = -_M0L6_2atmpS1418;
    _M0L1iS499 = _M0L6_2atmpS1417 - _M0L1qS498;
    #line 418 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1416 = _M0FPB8pow5bits(_M0L1iS499);
    _M0L1kS500 = _M0L6_2atmpS1416 - 125;
    _M0L1jS501 = _M0L1qS498 - _M0L1kS500;
    #line 420 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L4pow5S502 = _M0FPB19double__computePow5(_M0L1iS499);
    _M0L6_2atmpS1415 = _M0Lm2m2S477;
    #line 421 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L7_2abindS503
    = _M0FPB13mulShiftAll64(_M0L6_2atmpS1415, _M0L4pow5S502, _M0L1jS501, _M0L7mmShiftS482);
    _M0L8_2avrOutS504 = _M0L7_2abindS503.$0;
    _M0L8_2avpOutS505 = _M0L7_2abindS503.$1;
    _M0L8_2avmOutS506 = _M0L7_2abindS503.$2;
    _M0Lm2vrS483 = _M0L8_2avrOutS504;
    _M0Lm2vpS484 = _M0L8_2avpOutS505;
    _M0Lm2vmS485 = _M0L8_2avmOutS506;
    if (_M0L1qS498 <= 1) {
      _M0Lm17vrIsTrailingZerosS488 = 1;
      if (_M0L4evenS480) {
        int32_t _M0L6_2atmpS1413;
        #line 432 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0L6_2atmpS1413 = _M0MPC14bool4Bool7to__int(_M0L7mmShiftS482);
        _M0Lm17vmIsTrailingZerosS487 = _M0L6_2atmpS1413 == 1;
      } else {
        uint64_t _M0L6_2atmpS1414 = _M0Lm2vpS484;
        _M0Lm2vpS484 = _M0L6_2atmpS1414 - 1ull;
      }
    } else if (_M0L1qS498 < 63) {
      #line 437 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0Lm17vrIsTrailingZerosS488
      = _M0FPB18multipleOfPowerOf2(_M0L2mvS481, _M0L1qS498);
    }
  }
  _M0Lm7removedS507 = 0;
  _M0Lm16lastRemovedDigitS508 = 0;
  _M0Lm6outputS509 = 0ull;
  if (_M0Lm17vmIsTrailingZerosS487 || _M0Lm17vrIsTrailingZerosS488) {
    int32_t _if__result_1983;
    uint64_t _M0L6_2atmpS1455;
    uint64_t _M0L6_2atmpS1461;
    uint64_t _M0L6_2atmpS1462;
    int32_t _if__result_1984;
    int32_t _M0L6_2atmpS1458;
    int64_t _M0L6_2atmpS1457;
    uint64_t _M0L6_2atmpS1456;
    while (1) {
      uint64_t _M0L6_2atmpS1438 = _M0Lm2vpS484;
      uint64_t _M0L7vpDiv10S510 = _M0L6_2atmpS1438 / 10ull;
      uint64_t _M0L6_2atmpS1437 = _M0Lm2vmS485;
      uint64_t _M0L7vmDiv10S511 = _M0L6_2atmpS1437 / 10ull;
      uint64_t _M0L6_2atmpS1436;
      int32_t _M0L6_2atmpS1433;
      int32_t _M0L6_2atmpS1435;
      int32_t _M0L6_2atmpS1434;
      int32_t _M0L7vmMod10S513;
      uint64_t _M0L6_2atmpS1432;
      uint64_t _M0L7vrDiv10S514;
      uint64_t _M0L6_2atmpS1431;
      int32_t _M0L6_2atmpS1428;
      int32_t _M0L6_2atmpS1430;
      int32_t _M0L6_2atmpS1429;
      int32_t _M0L7vrMod10S515;
      int32_t _M0L6_2atmpS1427;
      if (_M0L7vpDiv10S510 <= _M0L7vmDiv10S511) {
        break;
      }
      _M0L6_2atmpS1436 = _M0Lm2vmS485;
      _M0L6_2atmpS1433 = (int32_t)_M0L6_2atmpS1436;
      _M0L6_2atmpS1435 = (int32_t)_M0L7vmDiv10S511;
      _M0L6_2atmpS1434 = 10 * _M0L6_2atmpS1435;
      _M0L7vmMod10S513 = _M0L6_2atmpS1433 - _M0L6_2atmpS1434;
      _M0L6_2atmpS1432 = _M0Lm2vrS483;
      _M0L7vrDiv10S514 = _M0L6_2atmpS1432 / 10ull;
      _M0L6_2atmpS1431 = _M0Lm2vrS483;
      _M0L6_2atmpS1428 = (int32_t)_M0L6_2atmpS1431;
      _M0L6_2atmpS1430 = (int32_t)_M0L7vrDiv10S514;
      _M0L6_2atmpS1429 = 10 * _M0L6_2atmpS1430;
      _M0L7vrMod10S515 = _M0L6_2atmpS1428 - _M0L6_2atmpS1429;
      _M0Lm17vmIsTrailingZerosS487
      = _M0Lm17vmIsTrailingZerosS487 && _M0L7vmMod10S513 == 0;
      if (_M0Lm17vrIsTrailingZerosS488) {
        int32_t _M0L6_2atmpS1426 = _M0Lm16lastRemovedDigitS508;
        _M0Lm17vrIsTrailingZerosS488 = _M0L6_2atmpS1426 == 0;
      } else {
        _M0Lm17vrIsTrailingZerosS488 = 0;
      }
      _M0Lm16lastRemovedDigitS508 = _M0L7vrMod10S515;
      _M0Lm2vrS483 = _M0L7vrDiv10S514;
      _M0Lm2vpS484 = _M0L7vpDiv10S510;
      _M0Lm2vmS485 = _M0L7vmDiv10S511;
      _M0L6_2atmpS1427 = _M0Lm7removedS507;
      _M0Lm7removedS507 = _M0L6_2atmpS1427 + 1;
      continue;
      break;
    }
    if (_M0Lm17vmIsTrailingZerosS487) {
      while (1) {
        uint64_t _M0L6_2atmpS1451 = _M0Lm2vmS485;
        uint64_t _M0L7vmDiv10S516 = _M0L6_2atmpS1451 / 10ull;
        uint64_t _M0L6_2atmpS1450 = _M0Lm2vmS485;
        int32_t _M0L6_2atmpS1447 = (int32_t)_M0L6_2atmpS1450;
        int32_t _M0L6_2atmpS1449 = (int32_t)_M0L7vmDiv10S516;
        int32_t _M0L6_2atmpS1448 = 10 * _M0L6_2atmpS1449;
        int32_t _M0L7vmMod10S517 = _M0L6_2atmpS1447 - _M0L6_2atmpS1448;
        uint64_t _M0L6_2atmpS1446;
        uint64_t _M0L7vpDiv10S519;
        uint64_t _M0L6_2atmpS1445;
        uint64_t _M0L7vrDiv10S520;
        uint64_t _M0L6_2atmpS1444;
        int32_t _M0L6_2atmpS1441;
        int32_t _M0L6_2atmpS1443;
        int32_t _M0L6_2atmpS1442;
        int32_t _M0L7vrMod10S521;
        int32_t _M0L6_2atmpS1440;
        if (_M0L7vmMod10S517 != 0) {
          break;
        }
        _M0L6_2atmpS1446 = _M0Lm2vpS484;
        _M0L7vpDiv10S519 = _M0L6_2atmpS1446 / 10ull;
        _M0L6_2atmpS1445 = _M0Lm2vrS483;
        _M0L7vrDiv10S520 = _M0L6_2atmpS1445 / 10ull;
        _M0L6_2atmpS1444 = _M0Lm2vrS483;
        _M0L6_2atmpS1441 = (int32_t)_M0L6_2atmpS1444;
        _M0L6_2atmpS1443 = (int32_t)_M0L7vrDiv10S520;
        _M0L6_2atmpS1442 = 10 * _M0L6_2atmpS1443;
        _M0L7vrMod10S521 = _M0L6_2atmpS1441 - _M0L6_2atmpS1442;
        if (_M0Lm17vrIsTrailingZerosS488) {
          int32_t _M0L6_2atmpS1439 = _M0Lm16lastRemovedDigitS508;
          _M0Lm17vrIsTrailingZerosS488 = _M0L6_2atmpS1439 == 0;
        } else {
          _M0Lm17vrIsTrailingZerosS488 = 0;
        }
        _M0Lm16lastRemovedDigitS508 = _M0L7vrMod10S521;
        _M0Lm2vrS483 = _M0L7vrDiv10S520;
        _M0Lm2vpS484 = _M0L7vpDiv10S519;
        _M0Lm2vmS485 = _M0L7vmDiv10S516;
        _M0L6_2atmpS1440 = _M0Lm7removedS507;
        _M0Lm7removedS507 = _M0L6_2atmpS1440 + 1;
        continue;
        break;
      }
    }
    if (_M0Lm17vrIsTrailingZerosS488) {
      int32_t _M0L6_2atmpS1454 = _M0Lm16lastRemovedDigitS508;
      if (_M0L6_2atmpS1454 == 5) {
        uint64_t _M0L6_2atmpS1453 = _M0Lm2vrS483;
        uint64_t _M0L6_2atmpS1452 = _M0L6_2atmpS1453 % 2ull;
        _if__result_1983 = _M0L6_2atmpS1452 == 0ull;
      } else {
        _if__result_1983 = 0;
      }
    } else {
      _if__result_1983 = 0;
    }
    if (_if__result_1983) {
      _M0Lm16lastRemovedDigitS508 = 4;
    }
    _M0L6_2atmpS1455 = _M0Lm2vrS483;
    _M0L6_2atmpS1461 = _M0Lm2vrS483;
    _M0L6_2atmpS1462 = _M0Lm2vmS485;
    if (_M0L6_2atmpS1461 == _M0L6_2atmpS1462) {
      if (!_M0L4evenS480) {
        _if__result_1984 = 1;
      } else {
        int32_t _M0L6_2atmpS1460 = _M0Lm17vmIsTrailingZerosS487;
        _if__result_1984 = !_M0L6_2atmpS1460;
      }
    } else {
      _if__result_1984 = 0;
    }
    if (_if__result_1984) {
      _M0L6_2atmpS1458 = 1;
    } else {
      int32_t _M0L6_2atmpS1459 = _M0Lm16lastRemovedDigitS508;
      _M0L6_2atmpS1458 = _M0L6_2atmpS1459 >= 5;
    }
    #line 487 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1457 = _M0MPC14bool4Bool9to__int64(_M0L6_2atmpS1458);
    _M0L6_2atmpS1456 = *(uint64_t*)&_M0L6_2atmpS1457;
    _M0Lm6outputS509 = _M0L6_2atmpS1455 + _M0L6_2atmpS1456;
  } else {
    int32_t _M0Lm7roundUpS522 = 0;
    uint64_t _M0L6_2atmpS1483 = _M0Lm2vpS484;
    uint64_t _M0L8vpDiv100S523 = _M0L6_2atmpS1483 / 100ull;
    uint64_t _M0L6_2atmpS1482 = _M0Lm2vmS485;
    uint64_t _M0L8vmDiv100S524 = _M0L6_2atmpS1482 / 100ull;
    uint64_t _M0L6_2atmpS1477;
    uint64_t _M0L6_2atmpS1480;
    uint64_t _M0L6_2atmpS1481;
    int32_t _M0L6_2atmpS1479;
    uint64_t _M0L6_2atmpS1478;
    if (_M0L8vpDiv100S523 > _M0L8vmDiv100S524) {
      uint64_t _M0L6_2atmpS1468 = _M0Lm2vrS483;
      uint64_t _M0L8vrDiv100S525 = _M0L6_2atmpS1468 / 100ull;
      uint64_t _M0L6_2atmpS1467 = _M0Lm2vrS483;
      int32_t _M0L6_2atmpS1464 = (int32_t)_M0L6_2atmpS1467;
      int32_t _M0L6_2atmpS1466 = (int32_t)_M0L8vrDiv100S525;
      int32_t _M0L6_2atmpS1465 = 100 * _M0L6_2atmpS1466;
      int32_t _M0L8vrMod100S526 = _M0L6_2atmpS1464 - _M0L6_2atmpS1465;
      int32_t _M0L6_2atmpS1463;
      _M0Lm7roundUpS522 = _M0L8vrMod100S526 >= 50;
      _M0Lm2vrS483 = _M0L8vrDiv100S525;
      _M0Lm2vpS484 = _M0L8vpDiv100S523;
      _M0Lm2vmS485 = _M0L8vmDiv100S524;
      _M0L6_2atmpS1463 = _M0Lm7removedS507;
      _M0Lm7removedS507 = _M0L6_2atmpS1463 + 2;
    }
    while (1) {
      uint64_t _M0L6_2atmpS1476 = _M0Lm2vpS484;
      uint64_t _M0L7vpDiv10S527 = _M0L6_2atmpS1476 / 10ull;
      uint64_t _M0L6_2atmpS1475 = _M0Lm2vmS485;
      uint64_t _M0L7vmDiv10S528 = _M0L6_2atmpS1475 / 10ull;
      uint64_t _M0L6_2atmpS1474;
      uint64_t _M0L7vrDiv10S530;
      uint64_t _M0L6_2atmpS1473;
      int32_t _M0L6_2atmpS1470;
      int32_t _M0L6_2atmpS1472;
      int32_t _M0L6_2atmpS1471;
      int32_t _M0L7vrMod10S531;
      int32_t _M0L6_2atmpS1469;
      if (_M0L7vpDiv10S527 <= _M0L7vmDiv10S528) {
        break;
      }
      _M0L6_2atmpS1474 = _M0Lm2vrS483;
      _M0L7vrDiv10S530 = _M0L6_2atmpS1474 / 10ull;
      _M0L6_2atmpS1473 = _M0Lm2vrS483;
      _M0L6_2atmpS1470 = (int32_t)_M0L6_2atmpS1473;
      _M0L6_2atmpS1472 = (int32_t)_M0L7vrDiv10S530;
      _M0L6_2atmpS1471 = 10 * _M0L6_2atmpS1472;
      _M0L7vrMod10S531 = _M0L6_2atmpS1470 - _M0L6_2atmpS1471;
      _M0Lm7roundUpS522 = _M0L7vrMod10S531 >= 5;
      _M0Lm2vrS483 = _M0L7vrDiv10S530;
      _M0Lm2vpS484 = _M0L7vpDiv10S527;
      _M0Lm2vmS485 = _M0L7vmDiv10S528;
      _M0L6_2atmpS1469 = _M0Lm7removedS507;
      _M0Lm7removedS507 = _M0L6_2atmpS1469 + 1;
      continue;
      break;
    }
    _M0L6_2atmpS1477 = _M0Lm2vrS483;
    _M0L6_2atmpS1480 = _M0Lm2vrS483;
    _M0L6_2atmpS1481 = _M0Lm2vmS485;
    _M0L6_2atmpS1479
    = _M0L6_2atmpS1480 == _M0L6_2atmpS1481 || _M0Lm7roundUpS522;
    #line 522 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1478 = _M0MPC14bool4Bool10to__uint64(_M0L6_2atmpS1479);
    _M0Lm6outputS509 = _M0L6_2atmpS1477 + _M0L6_2atmpS1478;
  }
  _M0L6_2atmpS1485 = _M0Lm3e10S486;
  _M0L6_2atmpS1486 = _M0Lm7removedS507;
  _M0L3expS532 = _M0L6_2atmpS1485 + _M0L6_2atmpS1486;
  _M0L6_2atmpS1484 = _M0Lm6outputS509;
  _block_1986
  = (struct _M0TPB17FloatingDecimal64*)moonbit_malloc(sizeof(struct _M0TPB17FloatingDecimal64));
  Moonbit_object_header(_block_1986)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_1986->$0 = _M0L6_2atmpS1484;
  _block_1986->$1 = _M0L3expS532;
  return _block_1986;
}

uint64_t _M0MPC14bool4Bool10to__uint64(int32_t _M0L4selfS475) {
  #line 110 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bool.mbt"
  if (_M0L4selfS475) {
    return 1ull;
  } else {
    return 0ull;
  }
}

int64_t _M0MPC14bool4Bool9to__int64(int32_t _M0L4selfS474) {
  #line 58 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bool.mbt"
  if (_M0L4selfS474) {
    return 1ll;
  } else {
    return 0ll;
  }
}

int32_t _M0MPC14bool4Bool7to__int(int32_t _M0L4selfS473) {
  #line 32 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bool.mbt"
  if (_M0L4selfS473) {
    return 1;
  } else {
    return 0;
  }
}

int32_t _M0FPB17decimal__length17(uint64_t _M0L1vS472) {
  #line 280 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  if (_M0L1vS472 >= 10000000000000000ull) {
    return 17;
  }
  if (_M0L1vS472 >= 1000000000000000ull) {
    return 16;
  }
  if (_M0L1vS472 >= 100000000000000ull) {
    return 15;
  }
  if (_M0L1vS472 >= 10000000000000ull) {
    return 14;
  }
  if (_M0L1vS472 >= 1000000000000ull) {
    return 13;
  }
  if (_M0L1vS472 >= 100000000000ull) {
    return 12;
  }
  if (_M0L1vS472 >= 10000000000ull) {
    return 11;
  }
  if (_M0L1vS472 >= 1000000000ull) {
    return 10;
  }
  if (_M0L1vS472 >= 100000000ull) {
    return 9;
  }
  if (_M0L1vS472 >= 10000000ull) {
    return 8;
  }
  if (_M0L1vS472 >= 1000000ull) {
    return 7;
  }
  if (_M0L1vS472 >= 100000ull) {
    return 6;
  }
  if (_M0L1vS472 >= 10000ull) {
    return 5;
  }
  if (_M0L1vS472 >= 1000ull) {
    return 4;
  }
  if (_M0L1vS472 >= 100ull) {
    return 3;
  }
  if (_M0L1vS472 >= 10ull) {
    return 2;
  }
  return 1;
}

struct _M0TPB8Pow5Pair _M0FPB22double__computeInvPow5(int32_t _M0L1iS455) {
  int32_t _M0L6_2atmpS1385;
  int32_t _M0L6_2atmpS1384;
  int32_t _M0L4baseS454;
  int32_t _M0L5base2S456;
  int32_t _M0L6offsetS457;
  int32_t _M0L6_2atmpS1383;
  uint64_t _M0L4mul0S458;
  int32_t _M0L6_2atmpS1382;
  int32_t _M0L6_2atmpS1381;
  uint64_t _M0L4mul1S459;
  uint64_t _M0L1mS460;
  struct _M0TPB7Umul128 _M0L7_2abindS461;
  uint64_t _M0L7_2alow1S462;
  uint64_t _M0L8_2ahigh1S463;
  struct _M0TPB7Umul128 _M0L7_2abindS464;
  uint64_t _M0L7_2alow0S465;
  uint64_t _M0L8_2ahigh0S466;
  uint64_t _M0L3sumS467;
  uint64_t _M0Lm5high1S468;
  int32_t _M0L6_2atmpS1379;
  int32_t _M0L6_2atmpS1380;
  int32_t _M0L5deltaS469;
  uint64_t _M0L6_2atmpS1378;
  uint64_t _M0L6_2atmpS1370;
  int32_t _M0L6_2atmpS1377;
  uint32_t _M0L6_2atmpS1374;
  int32_t _M0L6_2atmpS1376;
  int32_t _M0L6_2atmpS1375;
  uint32_t _M0L6_2atmpS1373;
  uint32_t _M0L6_2atmpS1372;
  uint64_t _M0L6_2atmpS1371;
  uint64_t _M0L1aS470;
  uint64_t _M0L6_2atmpS1369;
  uint64_t _M0L1bS471;
  #line 239 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1385 = _M0L1iS455 + 26;
  _M0L6_2atmpS1384 = _M0L6_2atmpS1385 - 1;
  _M0L4baseS454 = _M0L6_2atmpS1384 / 26;
  _M0L5base2S456 = _M0L4baseS454 * 26;
  _M0L6offsetS457 = _M0L5base2S456 - _M0L1iS455;
  _M0L6_2atmpS1383 = _M0L4baseS454 * 2;
  #line 243 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4mul0S458
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB26gDOUBLE__POW5__INV__SPLIT2, _M0L6_2atmpS1383);
  _M0L6_2atmpS1382 = _M0L4baseS454 * 2;
  _M0L6_2atmpS1381 = _M0L6_2atmpS1382 + 1;
  #line 244 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4mul1S459
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB26gDOUBLE__POW5__INV__SPLIT2, _M0L6_2atmpS1381);
  if (_M0L6offsetS457 == 0) {
    return (struct _M0TPB8Pow5Pair){.$0 = _M0L4mul0S458, .$1 = _M0L4mul1S459};
  }
  #line 248 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L1mS460
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB20gDOUBLE__POW5__TABLE, _M0L6offsetS457);
  #line 249 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS461 = _M0FPB7umul128(_M0L1mS460, _M0L4mul1S459);
  _M0L7_2alow1S462 = _M0L7_2abindS461.$0;
  _M0L8_2ahigh1S463 = _M0L7_2abindS461.$1;
  #line 250 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS464 = _M0FPB7umul128(_M0L1mS460, _M0L4mul0S458);
  _M0L7_2alow0S465 = _M0L7_2abindS464.$0;
  _M0L8_2ahigh0S466 = _M0L7_2abindS464.$1;
  _M0L3sumS467 = _M0L8_2ahigh0S466 + _M0L7_2alow1S462;
  _M0Lm5high1S468 = _M0L8_2ahigh1S463;
  if (_M0L3sumS467 < _M0L8_2ahigh0S466) {
    uint64_t _M0L6_2atmpS1368 = _M0Lm5high1S468;
    _M0Lm5high1S468 = _M0L6_2atmpS1368 + 1ull;
  }
  #line 256 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1379 = _M0FPB8pow5bits(_M0L5base2S456);
  #line 256 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1380 = _M0FPB8pow5bits(_M0L1iS455);
  _M0L5deltaS469 = _M0L6_2atmpS1379 - _M0L6_2atmpS1380;
  #line 257 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1378
  = _M0FPB13shiftright128(_M0L7_2alow0S465, _M0L3sumS467, _M0L5deltaS469);
  _M0L6_2atmpS1370 = _M0L6_2atmpS1378 + 1ull;
  _M0L6_2atmpS1377 = _M0L1iS455 / 16;
  #line 259 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1374
  = _M0MPC15array13ReadOnlyArray2atGjE(_M0FPB19gPOW5__INV__OFFSETS, _M0L6_2atmpS1377);
  _M0L6_2atmpS1376 = _M0L1iS455 % 16;
  _M0L6_2atmpS1375 = _M0L6_2atmpS1376 << 1;
  _M0L6_2atmpS1373 = _M0L6_2atmpS1374 >> (_M0L6_2atmpS1375 & 31);
  _M0L6_2atmpS1372 = _M0L6_2atmpS1373 & 3u;
  #line 259 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1371 = _M0MPC14uint4UInt10to__uint64(_M0L6_2atmpS1372);
  _M0L1aS470 = _M0L6_2atmpS1370 + _M0L6_2atmpS1371;
  _M0L6_2atmpS1369 = _M0Lm5high1S468;
  #line 260 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L1bS471
  = _M0FPB13shiftright128(_M0L3sumS467, _M0L6_2atmpS1369, _M0L5deltaS469);
  return (struct _M0TPB8Pow5Pair){.$0 = _M0L1aS470, .$1 = _M0L1bS471};
}

struct _M0TPB8Pow5Pair _M0FPB19double__computePow5(int32_t _M0L1iS437) {
  int32_t _M0L4baseS436;
  int32_t _M0L5base2S438;
  int32_t _M0L6offsetS439;
  int32_t _M0L6_2atmpS1367;
  uint64_t _M0L4mul0S440;
  int32_t _M0L6_2atmpS1366;
  int32_t _M0L6_2atmpS1365;
  uint64_t _M0L4mul1S441;
  uint64_t _M0L1mS442;
  struct _M0TPB7Umul128 _M0L7_2abindS443;
  uint64_t _M0L7_2alow1S444;
  uint64_t _M0L8_2ahigh1S445;
  struct _M0TPB7Umul128 _M0L7_2abindS446;
  uint64_t _M0L7_2alow0S447;
  uint64_t _M0L8_2ahigh0S448;
  uint64_t _M0L3sumS449;
  uint64_t _M0Lm5high1S450;
  int32_t _M0L6_2atmpS1363;
  int32_t _M0L6_2atmpS1364;
  int32_t _M0L5deltaS451;
  uint64_t _M0L6_2atmpS1355;
  int32_t _M0L6_2atmpS1362;
  uint32_t _M0L6_2atmpS1359;
  int32_t _M0L6_2atmpS1361;
  int32_t _M0L6_2atmpS1360;
  uint32_t _M0L6_2atmpS1358;
  uint32_t _M0L6_2atmpS1357;
  uint64_t _M0L6_2atmpS1356;
  uint64_t _M0L1aS452;
  uint64_t _M0L6_2atmpS1354;
  uint64_t _M0L1bS453;
  #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4baseS436 = _M0L1iS437 / 26;
  _M0L5base2S438 = _M0L4baseS436 * 26;
  _M0L6offsetS439 = _M0L1iS437 - _M0L5base2S438;
  _M0L6_2atmpS1367 = _M0L4baseS436 * 2;
  #line 217 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4mul0S440
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB21gDOUBLE__POW5__SPLIT2, _M0L6_2atmpS1367);
  _M0L6_2atmpS1366 = _M0L4baseS436 * 2;
  _M0L6_2atmpS1365 = _M0L6_2atmpS1366 + 1;
  #line 218 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4mul1S441
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB21gDOUBLE__POW5__SPLIT2, _M0L6_2atmpS1365);
  if (_M0L6offsetS439 == 0) {
    return (struct _M0TPB8Pow5Pair){.$0 = _M0L4mul0S440, .$1 = _M0L4mul1S441};
  }
  #line 222 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L1mS442
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB20gDOUBLE__POW5__TABLE, _M0L6offsetS439);
  #line 223 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS443 = _M0FPB7umul128(_M0L1mS442, _M0L4mul1S441);
  _M0L7_2alow1S444 = _M0L7_2abindS443.$0;
  _M0L8_2ahigh1S445 = _M0L7_2abindS443.$1;
  #line 224 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS446 = _M0FPB7umul128(_M0L1mS442, _M0L4mul0S440);
  _M0L7_2alow0S447 = _M0L7_2abindS446.$0;
  _M0L8_2ahigh0S448 = _M0L7_2abindS446.$1;
  _M0L3sumS449 = _M0L8_2ahigh0S448 + _M0L7_2alow1S444;
  _M0Lm5high1S450 = _M0L8_2ahigh1S445;
  if (_M0L3sumS449 < _M0L8_2ahigh0S448) {
    uint64_t _M0L6_2atmpS1353 = _M0Lm5high1S450;
    _M0Lm5high1S450 = _M0L6_2atmpS1353 + 1ull;
  }
  #line 230 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1363 = _M0FPB8pow5bits(_M0L1iS437);
  #line 230 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1364 = _M0FPB8pow5bits(_M0L5base2S438);
  _M0L5deltaS451 = _M0L6_2atmpS1363 - _M0L6_2atmpS1364;
  #line 231 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1355
  = _M0FPB13shiftright128(_M0L7_2alow0S447, _M0L3sumS449, _M0L5deltaS451);
  _M0L6_2atmpS1362 = _M0L1iS437 / 16;
  #line 232 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1359
  = _M0MPC15array13ReadOnlyArray2atGjE(_M0FPB14gPOW5__OFFSETS, _M0L6_2atmpS1362);
  _M0L6_2atmpS1361 = _M0L1iS437 % 16;
  _M0L6_2atmpS1360 = _M0L6_2atmpS1361 << 1;
  _M0L6_2atmpS1358 = _M0L6_2atmpS1359 >> (_M0L6_2atmpS1360 & 31);
  _M0L6_2atmpS1357 = _M0L6_2atmpS1358 & 3u;
  #line 232 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1356 = _M0MPC14uint4UInt10to__uint64(_M0L6_2atmpS1357);
  _M0L1aS452 = _M0L6_2atmpS1355 + _M0L6_2atmpS1356;
  _M0L6_2atmpS1354 = _M0Lm5high1S450;
  #line 233 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L1bS453
  = _M0FPB13shiftright128(_M0L3sumS449, _M0L6_2atmpS1354, _M0L5deltaS451);
  return (struct _M0TPB8Pow5Pair){.$0 = _M0L1aS452, .$1 = _M0L1bS453};
}

struct _M0TPB19MulShiftAll64Result _M0FPB13mulShiftAll64(
  uint64_t _M0L1mS410,
  struct _M0TPB8Pow5Pair _M0L3mulS407,
  int32_t _M0L1jS423,
  int32_t _M0L7mmShiftS425
) {
  uint64_t _M0L7_2amul0S406;
  uint64_t _M0L7_2amul1S408;
  uint64_t _M0L1mS409;
  struct _M0TPB7Umul128 _M0L7_2abindS411;
  uint64_t _M0L5_2aloS412;
  uint64_t _M0L6_2atmpS413;
  struct _M0TPB7Umul128 _M0L7_2abindS414;
  uint64_t _M0L6_2alo2S415;
  uint64_t _M0L6_2ahi2S416;
  uint64_t _M0L3midS417;
  uint64_t _M0L6_2atmpS1352;
  uint64_t _M0L2hiS418;
  uint64_t _M0L3lo2S419;
  uint64_t _M0L6_2atmpS1350;
  uint64_t _M0L6_2atmpS1351;
  uint64_t _M0L4mid2S420;
  uint64_t _M0L6_2atmpS1349;
  uint64_t _M0L3hi2S421;
  int32_t _M0L6_2atmpS1348;
  int32_t _M0L6_2atmpS1347;
  uint64_t _M0L2vpS422;
  uint64_t _M0Lm2vmS424;
  int32_t _M0L6_2atmpS1346;
  int32_t _M0L6_2atmpS1345;
  uint64_t _M0L2vrS435;
  uint64_t _M0L6_2atmpS1344;
  #line 129 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2amul0S406 = _M0L3mulS407.$0;
  _M0L7_2amul1S408 = _M0L3mulS407.$1;
  _M0L1mS409 = _M0L1mS410 << 1;
  #line 137 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS411 = _M0FPB7umul128(_M0L1mS409, _M0L7_2amul0S406);
  _M0L5_2aloS412 = _M0L7_2abindS411.$0;
  _M0L6_2atmpS413 = _M0L7_2abindS411.$1;
  #line 138 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS414 = _M0FPB7umul128(_M0L1mS409, _M0L7_2amul1S408);
  _M0L6_2alo2S415 = _M0L7_2abindS414.$0;
  _M0L6_2ahi2S416 = _M0L7_2abindS414.$1;
  _M0L3midS417 = _M0L6_2atmpS413 + _M0L6_2alo2S415;
  if (_M0L3midS417 < _M0L6_2atmpS413) {
    _M0L6_2atmpS1352 = 1ull;
  } else {
    _M0L6_2atmpS1352 = 0ull;
  }
  _M0L2hiS418 = _M0L6_2ahi2S416 + _M0L6_2atmpS1352;
  _M0L3lo2S419 = _M0L5_2aloS412 + _M0L7_2amul0S406;
  _M0L6_2atmpS1350 = _M0L3midS417 + _M0L7_2amul1S408;
  if (_M0L3lo2S419 < _M0L5_2aloS412) {
    _M0L6_2atmpS1351 = 1ull;
  } else {
    _M0L6_2atmpS1351 = 0ull;
  }
  _M0L4mid2S420 = _M0L6_2atmpS1350 + _M0L6_2atmpS1351;
  if (_M0L4mid2S420 < _M0L3midS417) {
    _M0L6_2atmpS1349 = 1ull;
  } else {
    _M0L6_2atmpS1349 = 0ull;
  }
  _M0L3hi2S421 = _M0L2hiS418 + _M0L6_2atmpS1349;
  _M0L6_2atmpS1348 = _M0L1jS423 - 64;
  _M0L6_2atmpS1347 = _M0L6_2atmpS1348 - 1;
  #line 144 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L2vpS422
  = _M0FPB13shiftright128(_M0L4mid2S420, _M0L3hi2S421, _M0L6_2atmpS1347);
  _M0Lm2vmS424 = 0ull;
  if (_M0L7mmShiftS425) {
    uint64_t _M0L3lo3S426 = _M0L5_2aloS412 - _M0L7_2amul0S406;
    uint64_t _M0L6_2atmpS1334 = _M0L3midS417 - _M0L7_2amul1S408;
    uint64_t _M0L6_2atmpS1335;
    uint64_t _M0L4mid3S427;
    uint64_t _M0L6_2atmpS1333;
    uint64_t _M0L3hi3S428;
    int32_t _M0L6_2atmpS1332;
    int32_t _M0L6_2atmpS1331;
    if (_M0L5_2aloS412 < _M0L3lo3S426) {
      _M0L6_2atmpS1335 = 1ull;
    } else {
      _M0L6_2atmpS1335 = 0ull;
    }
    _M0L4mid3S427 = _M0L6_2atmpS1334 - _M0L6_2atmpS1335;
    if (_M0L3midS417 < _M0L4mid3S427) {
      _M0L6_2atmpS1333 = 1ull;
    } else {
      _M0L6_2atmpS1333 = 0ull;
    }
    _M0L3hi3S428 = _M0L2hiS418 - _M0L6_2atmpS1333;
    _M0L6_2atmpS1332 = _M0L1jS423 - 64;
    _M0L6_2atmpS1331 = _M0L6_2atmpS1332 - 1;
    #line 150 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0Lm2vmS424
    = _M0FPB13shiftright128(_M0L4mid3S427, _M0L3hi3S428, _M0L6_2atmpS1331);
  } else {
    uint64_t _M0L3lo3S429 = _M0L5_2aloS412 + _M0L5_2aloS412;
    uint64_t _M0L6_2atmpS1342 = _M0L3midS417 + _M0L3midS417;
    uint64_t _M0L6_2atmpS1343;
    uint64_t _M0L4mid3S430;
    uint64_t _M0L6_2atmpS1340;
    uint64_t _M0L6_2atmpS1341;
    uint64_t _M0L3hi3S431;
    uint64_t _M0L3lo4S432;
    uint64_t _M0L6_2atmpS1338;
    uint64_t _M0L6_2atmpS1339;
    uint64_t _M0L4mid4S433;
    uint64_t _M0L6_2atmpS1337;
    uint64_t _M0L3hi4S434;
    int32_t _M0L6_2atmpS1336;
    if (_M0L3lo3S429 < _M0L5_2aloS412) {
      _M0L6_2atmpS1343 = 1ull;
    } else {
      _M0L6_2atmpS1343 = 0ull;
    }
    _M0L4mid3S430 = _M0L6_2atmpS1342 + _M0L6_2atmpS1343;
    _M0L6_2atmpS1340 = _M0L2hiS418 + _M0L2hiS418;
    if (_M0L4mid3S430 < _M0L3midS417) {
      _M0L6_2atmpS1341 = 1ull;
    } else {
      _M0L6_2atmpS1341 = 0ull;
    }
    _M0L3hi3S431 = _M0L6_2atmpS1340 + _M0L6_2atmpS1341;
    _M0L3lo4S432 = _M0L3lo3S429 - _M0L7_2amul0S406;
    _M0L6_2atmpS1338 = _M0L4mid3S430 - _M0L7_2amul1S408;
    if (_M0L3lo3S429 < _M0L3lo4S432) {
      _M0L6_2atmpS1339 = 1ull;
    } else {
      _M0L6_2atmpS1339 = 0ull;
    }
    _M0L4mid4S433 = _M0L6_2atmpS1338 - _M0L6_2atmpS1339;
    if (_M0L4mid3S430 < _M0L4mid4S433) {
      _M0L6_2atmpS1337 = 1ull;
    } else {
      _M0L6_2atmpS1337 = 0ull;
    }
    _M0L3hi4S434 = _M0L3hi3S431 - _M0L6_2atmpS1337;
    _M0L6_2atmpS1336 = _M0L1jS423 - 64;
    #line 158 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0Lm2vmS424
    = _M0FPB13shiftright128(_M0L4mid4S433, _M0L3hi4S434, _M0L6_2atmpS1336);
  }
  _M0L6_2atmpS1346 = _M0L1jS423 - 64;
  _M0L6_2atmpS1345 = _M0L6_2atmpS1346 - 1;
  #line 160 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L2vrS435
  = _M0FPB13shiftright128(_M0L3midS417, _M0L2hiS418, _M0L6_2atmpS1345);
  _M0L6_2atmpS1344 = _M0Lm2vmS424;
  return (struct _M0TPB19MulShiftAll64Result){.$0 = _M0L2vrS435,
                                                .$1 = _M0L2vpS422,
                                                .$2 = _M0L6_2atmpS1344};
}

int32_t _M0FPB18multipleOfPowerOf2(
  uint64_t _M0L5valueS404,
  int32_t _M0L1pS405
) {
  uint64_t _M0L6_2atmpS1330;
  uint64_t _M0L6_2atmpS1329;
  uint64_t _M0L6_2atmpS1328;
  #line 124 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1330 = 1ull << (_M0L1pS405 & 63);
  _M0L6_2atmpS1329 = _M0L6_2atmpS1330 - 1ull;
  _M0L6_2atmpS1328 = _M0L5valueS404 & _M0L6_2atmpS1329;
  return _M0L6_2atmpS1328 == 0ull;
}

int32_t _M0FPB18multipleOfPowerOf5(
  uint64_t _M0L5valueS402,
  int32_t _M0L1pS403
) {
  int32_t _M0L6_2atmpS1327;
  #line 119 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  #line 120 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1327 = _M0FPB10pow5Factor(_M0L5valueS402);
  return _M0L6_2atmpS1327 >= _M0L1pS403;
}

int32_t _M0FPB10pow5Factor(uint64_t _M0L5valueS397) {
  uint64_t _M0L6_2atmpS1318;
  uint64_t _M0L6_2atmpS1319;
  uint64_t _M0L6_2atmpS1320;
  uint64_t _M0L6_2atmpS1321;
  uint64_t _M0L6_2atmpS1326;
  int32_t _M0L5countS398;
  uint64_t _M0L1vS399;
  #line 94 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1318 = _M0L5valueS397 % 5ull;
  if (_M0L6_2atmpS1318 != 0ull) {
    return 0;
  }
  _M0L6_2atmpS1319 = _M0L5valueS397 % 25ull;
  if (_M0L6_2atmpS1319 != 0ull) {
    return 1;
  }
  _M0L6_2atmpS1320 = _M0L5valueS397 % 125ull;
  if (_M0L6_2atmpS1320 != 0ull) {
    return 2;
  }
  _M0L6_2atmpS1321 = _M0L5valueS397 % 625ull;
  if (_M0L6_2atmpS1321 != 0ull) {
    return 3;
  }
  _M0L6_2atmpS1326 = _M0L5valueS397 / 625ull;
  _M0L5countS398 = 4;
  _M0L1vS399 = _M0L6_2atmpS1326;
  while (1) {
    if (_M0L1vS399 > 0ull) {
      uint64_t _M0L6_2atmpS1322 = _M0L1vS399 % 5ull;
      int32_t _M0L6_2atmpS1323;
      uint64_t _M0L6_2atmpS1324;
      if (_M0L6_2atmpS1322 != 0ull) {
        return _M0L5countS398;
      }
      _M0L6_2atmpS1323 = _M0L5countS398 + 1;
      _M0L6_2atmpS1324 = _M0L1vS399 / 5ull;
      _M0L5countS398 = _M0L6_2atmpS1323;
      _M0L1vS399 = _M0L6_2atmpS1324;
      continue;
    } else {
      struct _M0TPB13StringBuilder* _M0L18_2astring__builderS401;
      moonbit_string_t _M0L6_2atmpS1325;
      int32_t _result_1988;
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0L18_2astring__builderS401
      = _M0MPB13StringBuilder21StringBuilder_2einner(25);
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS401, (moonbit_string_t)moonbit_string_literal_2.data);
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0MPB13StringBuilder13write__objectGmE(_M0L18_2astring__builderS401, _M0L5valueS397);
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0L6_2atmpS1325
      = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS401);
      moonbit_decref(_M0L18_2astring__builderS401);
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _result_1988 = _M0FPC15abort5abortGiE(_M0L6_2atmpS1325);
      moonbit_decref(_M0L6_2atmpS1325);
      return _result_1988;
    }
    break;
  }
}

uint64_t _M0FPB13shiftright128(
  uint64_t _M0L2loS396,
  uint64_t _M0L2hiS394,
  int32_t _M0L4distS395
) {
  int32_t _M0L6_2atmpS1317;
  uint64_t _M0L6_2atmpS1315;
  uint64_t _M0L6_2atmpS1316;
  #line 89 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1317 = 64 - _M0L4distS395;
  _M0L6_2atmpS1315 = _M0L2hiS394 << (_M0L6_2atmpS1317 & 63);
  _M0L6_2atmpS1316 = _M0L2loS396 >> (_M0L4distS395 & 63);
  return _M0L6_2atmpS1315 | _M0L6_2atmpS1316;
}

struct _M0TPB7Umul128 _M0FPB7umul128(
  uint64_t _M0L1aS384,
  uint64_t _M0L1bS387
) {
  uint64_t _M0L3aLoS383;
  uint64_t _M0L3aHiS385;
  uint64_t _M0L3bLoS386;
  uint64_t _M0L3bHiS388;
  uint64_t _M0L1xS389;
  uint64_t _M0L6_2atmpS1313;
  uint64_t _M0L6_2atmpS1314;
  uint64_t _M0L1yS390;
  uint64_t _M0L6_2atmpS1311;
  uint64_t _M0L6_2atmpS1312;
  uint64_t _M0L1zS391;
  uint64_t _M0L6_2atmpS1309;
  uint64_t _M0L6_2atmpS1310;
  uint64_t _M0L6_2atmpS1307;
  uint64_t _M0L6_2atmpS1308;
  uint64_t _M0L1wS392;
  uint64_t _M0L2loS393;
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L3aLoS383 = _M0L1aS384 & 4294967295ull;
  _M0L3aHiS385 = _M0L1aS384 >> 32;
  _M0L3bLoS386 = _M0L1bS387 & 4294967295ull;
  _M0L3bHiS388 = _M0L1bS387 >> 32;
  _M0L1xS389 = _M0L3aLoS383 * _M0L3bLoS386;
  _M0L6_2atmpS1313 = _M0L3aHiS385 * _M0L3bLoS386;
  _M0L6_2atmpS1314 = _M0L1xS389 >> 32;
  _M0L1yS390 = _M0L6_2atmpS1313 + _M0L6_2atmpS1314;
  _M0L6_2atmpS1311 = _M0L3aLoS383 * _M0L3bHiS388;
  _M0L6_2atmpS1312 = _M0L1yS390 & 4294967295ull;
  _M0L1zS391 = _M0L6_2atmpS1311 + _M0L6_2atmpS1312;
  _M0L6_2atmpS1309 = _M0L3aHiS385 * _M0L3bHiS388;
  _M0L6_2atmpS1310 = _M0L1yS390 >> 32;
  _M0L6_2atmpS1307 = _M0L6_2atmpS1309 + _M0L6_2atmpS1310;
  _M0L6_2atmpS1308 = _M0L1zS391 >> 32;
  _M0L1wS392 = _M0L6_2atmpS1307 + _M0L6_2atmpS1308;
  _M0L2loS393 = _M0L1aS384 * _M0L1bS387;
  return (struct _M0TPB7Umul128){.$0 = _M0L2loS393, .$1 = _M0L1wS392};
}

moonbit_string_t _M0FPB19string__from__bytes(
  moonbit_bytes_t _M0L5bytesS381,
  int32_t _M0L4fromS378,
  int32_t _M0L2toS377
) {
  int32_t _M0L3lenS376;
  int32_t _M0L6_2atmpS1306;
  uint16_t* _M0L6bufferS379;
  int32_t _M0L1iS380;
  #line 52 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L3lenS376 = _M0L2toS377 - _M0L4fromS378;
  #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1306 = _M0IPC16uint166UInt16PB7Default7default();
  _M0L6bufferS379
  = (uint16_t*)moonbit_make_string(_M0L3lenS376, _M0L6_2atmpS1306);
  _M0L1iS380 = 0;
  while (1) {
    if (_M0L1iS380 < _M0L3lenS376) {
      int32_t _M0L6_2atmpS1304 = _M0L4fromS378 + _M0L1iS380;
      int32_t _M0L6_2atmpS1303;
      int32_t _M0L6_2atmpS1302;
      int32_t _M0L6_2atmpS1305;
      if (
        _M0L6_2atmpS1304 < 0
        || _M0L6_2atmpS1304 >= Moonbit_array_length(_M0L5bytesS381)
      ) {
        #line 56 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6_2atmpS1303 = (int32_t)_M0L5bytesS381[_M0L6_2atmpS1304];
      _M0L6_2atmpS1302 = (uint16_t)_M0L6_2atmpS1303;
      if (
        _M0L1iS380 < 0 || _M0L1iS380 >= Moonbit_array_length(_M0L6bufferS379)
      ) {
        #line 56 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6bufferS379[_M0L1iS380] = _M0L6_2atmpS1302;
      _M0L6_2atmpS1305 = _M0L1iS380 + 1;
      _M0L1iS380 = _M0L6_2atmpS1305;
      continue;
    }
    break;
  }
  return _M0L6bufferS379;
}

int32_t _M0FPB9log10Pow2(int32_t _M0L1eS375) {
  int32_t _M0L6_2atmpS1301;
  uint32_t _M0L6_2atmpS1300;
  uint32_t _M0L6_2atmpS1299;
  #line 44 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1301 = _M0L1eS375 * 78913;
  _M0L6_2atmpS1300 = *(uint32_t*)&_M0L6_2atmpS1301;
  _M0L6_2atmpS1299 = _M0L6_2atmpS1300 >> 18;
  return *(int32_t*)&_M0L6_2atmpS1299;
}

int32_t _M0FPB9log10Pow5(int32_t _M0L1eS374) {
  int32_t _M0L6_2atmpS1298;
  uint32_t _M0L6_2atmpS1297;
  uint32_t _M0L6_2atmpS1296;
  #line 37 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1298 = _M0L1eS374 * 732923;
  _M0L6_2atmpS1297 = *(uint32_t*)&_M0L6_2atmpS1298;
  _M0L6_2atmpS1296 = _M0L6_2atmpS1297 >> 20;
  return *(int32_t*)&_M0L6_2atmpS1296;
}

moonbit_string_t _M0FPB18copy__special__str(
  int32_t _M0L4signS372,
  int32_t _M0L8exponentS373,
  int32_t _M0L8mantissaS370
) {
  moonbit_string_t _M0L1sS371;
  moonbit_string_t _result_1991;
  #line 23 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  if (_M0L8mantissaS370) {
    return (moonbit_string_t)moonbit_string_literal_3.data;
  }
  if (_M0L4signS372) {
    _M0L1sS371 = (moonbit_string_t)moonbit_string_literal_4.data;
  } else {
    _M0L1sS371 = (moonbit_string_t)moonbit_string_literal_5.data;
  }
  if (_M0L8exponentS373) {
    moonbit_string_t _result_1990;
    #line 29 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _result_1990
    = moonbit_add_string(_M0L1sS371, (moonbit_string_t)moonbit_string_literal_6.data);
    moonbit_decref(_M0L1sS371);
    return _result_1990;
  }
  #line 31 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _result_1991
  = moonbit_add_string(_M0L1sS371, (moonbit_string_t)moonbit_string_literal_7.data);
  moonbit_decref(_M0L1sS371);
  return _result_1991;
}

int32_t _M0FPB8pow5bits(int32_t _M0L1eS369) {
  int32_t _M0L6_2atmpS1295;
  uint32_t _M0L6_2atmpS1294;
  uint32_t _M0L6_2atmpS1293;
  int32_t _M0L6_2atmpS1292;
  #line 18 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1295 = _M0L1eS369 * 1217359;
  _M0L6_2atmpS1294 = *(uint32_t*)&_M0L6_2atmpS1295;
  _M0L6_2atmpS1293 = _M0L6_2atmpS1294 >> 19;
  _M0L6_2atmpS1292 = *(int32_t*)&_M0L6_2atmpS1293;
  return _M0L6_2atmpS1292 + 1;
}

int32_t _M0MPC16double6Double7to__int(double _M0L4selfS368) {
  #line 43 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_to_int.mbt"
  if (_M0L4selfS368 != _M0L4selfS368) {
    return 0;
  } else if (_M0L4selfS368 >= 0x1.fffffffcp+30) {
    return 2147483647;
  } else if (_M0L4selfS368 <= -0x1p+31) {
    return (int32_t)0x80000000;
  } else {
    return (int32_t)_M0L4selfS368;
  }
}

int64_t _M0MPC16double6Double9to__int64(double _M0L4selfS367) {
  #line 46 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_to_int64_native.mbt"
  if (_M0L4selfS367 != _M0L4selfS367) {
    return 0ll;
  } else if (_M0L4selfS367 >= 0x1p+63) {
    return 9223372036854775807ll;
  } else if (_M0L4selfS367 <= -0x1p+63) {
    return (int64_t)0x8000000000000000ll;
  } else {
    return (int64_t)_M0L4selfS367;
  }
}

struct _M0TPB5ArrayGfE* _M0MPC15array5Array20unsafe__make__uninitGfE(
  int32_t _M0L3lenS363
) {
  float* _M0L6_2atmpS1288;
  struct _M0TPB5ArrayGfE* _block_1992;
  #line 30 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1288 = (float*)moonbit_make_float_array_raw(_M0L3lenS363);
  _block_1992
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_block_1992)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 15, 0);
  _block_1992->$0 = _M0L6_2atmpS1288;
  _block_1992->$1 = _M0L3lenS363;
  return _block_1992;
}

struct _M0TPB5ArrayGbE* _M0MPC15array5Array20unsafe__make__uninitGbE(
  int32_t _M0L3lenS364
) {
  uint8_t* _M0L6_2atmpS1289;
  struct _M0TPB5ArrayGbE* _block_1993;
  #line 30 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1289 = (uint8_t*)moonbit_make_bytes_raw(_M0L3lenS364);
  _block_1993
  = (struct _M0TPB5ArrayGbE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGbE));
  Moonbit_object_header(_block_1993)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 26, 0);
  _block_1993->$0 = _M0L6_2atmpS1289;
  _block_1993->$1 = _M0L3lenS364;
  return _block_1993;
}

struct _M0TPB5ArrayGRPB5ArrayGfEE* _M0MPC15array5Array20unsafe__make__uninitGRPB5ArrayGfEE(
  int32_t _M0L3lenS365
) {
  struct _M0TPB5ArrayGfE** _M0L6_2atmpS1290;
  struct _M0TPB5ArrayGRPB5ArrayGfEE* _block_1994;
  #line 30 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1290
  = (struct _M0TPB5ArrayGfE**)moonbit_make_ref_array(_M0L3lenS365, 0);
  _block_1994
  = (struct _M0TPB5ArrayGRPB5ArrayGfEE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGRPB5ArrayGfEE));
  Moonbit_object_header(_block_1994)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 29, 0);
  _block_1994->$0 = _M0L6_2atmpS1290;
  _block_1994->$1 = _M0L3lenS365;
  return _block_1994;
}

struct _M0TPB5ArrayGiE* _M0MPC15array5Array20unsafe__make__uninitGiE(
  int32_t _M0L3lenS366
) {
  int32_t* _M0L6_2atmpS1291;
  struct _M0TPB5ArrayGiE* _block_1995;
  #line 30 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1291 = (int32_t*)moonbit_make_int32_array_raw(_M0L3lenS366);
  _block_1995
  = (struct _M0TPB5ArrayGiE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGiE));
  Moonbit_object_header(_block_1995)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 18, 0);
  _block_1995->$0 = _M0L6_2atmpS1291;
  _block_1995->$1 = _M0L3lenS366;
  return _block_1995;
}

uint64_t _M0MPC15array13ReadOnlyArray2atGmE(
  uint64_t* _M0L4selfS359,
  int32_t _M0L5indexS360
) {
  uint64_t* _M0L6_2atmpS1286;
  #line 38 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\readonlyarray.mbt"
  _M0L6_2atmpS1286 = _M0L4selfS359;
  if (
    _M0L5indexS360 < 0
    || _M0L5indexS360 >= Moonbit_array_length(_M0L6_2atmpS1286)
  ) {
    #line 40 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\readonlyarray.mbt"
    moonbit_panic();
  }
  return (uint64_t)_M0L6_2atmpS1286[_M0L5indexS360];
}

uint32_t _M0MPC15array13ReadOnlyArray2atGjE(
  uint32_t* _M0L4selfS361,
  int32_t _M0L5indexS362
) {
  uint32_t* _M0L6_2atmpS1287;
  #line 38 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\readonlyarray.mbt"
  _M0L6_2atmpS1287 = _M0L4selfS361;
  if (
    _M0L5indexS362 < 0
    || _M0L5indexS362 >= Moonbit_array_length(_M0L6_2atmpS1287)
  ) {
    #line 40 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\readonlyarray.mbt"
    moonbit_panic();
  }
  return (uint32_t)_M0L6_2atmpS1287[_M0L5indexS362];
}

moonbit_string_t _M0IPC16uint646UInt64PB4Show10to__string(
  uint64_t _M0L4selfS358
) {
  #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  #line 51 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  return _M0MPC16uint646UInt6418to__string_2einner(_M0L4selfS358, 10);
}

moonbit_string_t _M0IPC13int3IntPB4Show10to__string(int32_t _M0L4selfS357) {
  #line 35 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  #line 36 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  return _M0MPC13int3Int18to__string_2einner(_M0L4selfS357, 10);
}

uint64_t _M0MPC14uint4UInt10to__uint64(uint32_t _M0L4selfS356) {
  #line 2576 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  return (uint64_t)_M0L4selfS356;
}

int32_t _M0MPC15array5Array4pushGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS350,
  int32_t _M0L5valueS352
) {
  int32_t _M0L3lenS1272;
  int32_t* _M0L6_2atmpS1274;
  int32_t _M0L6_2atmpS1273;
  int32_t _M0L6lengthS351;
  int32_t* _M0L3bufS1277;
  int32_t _M0L6_2atmpS1278;
  #line 406 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L3lenS1272 = _M0L4selfS350->$1;
  #line 408 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1274 = _M0MPC15array5Array6bufferGiE(_M0L4selfS350);
  _M0L6_2atmpS1273 = Moonbit_array_length(_M0L6_2atmpS1274);
  moonbit_decref(_M0L6_2atmpS1274);
  if (_M0L3lenS1272 == _M0L6_2atmpS1273) {
    int32_t _M0L3lenS1276 = _M0L4selfS350->$1;
    int32_t _M0L6_2atmpS1275 = _M0L3lenS1276 + 1;
    #line 409 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0MPC15array5Array7reallocGiE(_M0L4selfS350, _M0L6_2atmpS1275);
  }
  _M0L6lengthS351 = _M0L4selfS350->$1;
  _M0L3bufS1277 = _M0L4selfS350->$0;
  _M0L3bufS1277[_M0L6lengthS351] = _M0L5valueS352;
  _M0L6_2atmpS1278 = _M0L6lengthS351 + 1;
  _M0L4selfS350->$1 = _M0L6_2atmpS1278;
  return 0;
}

int32_t _M0MPC15array5Array4pushGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS353,
  float _M0L5valueS355
) {
  int32_t _M0L3lenS1279;
  float* _M0L6_2atmpS1281;
  int32_t _M0L6_2atmpS1280;
  int32_t _M0L6lengthS354;
  float* _M0L3bufS1284;
  int32_t _M0L6_2atmpS1285;
  #line 406 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L3lenS1279 = _M0L4selfS353->$1;
  #line 408 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1281 = _M0MPC15array5Array6bufferGfE(_M0L4selfS353);
  _M0L6_2atmpS1280 = Moonbit_array_length(_M0L6_2atmpS1281);
  moonbit_decref(_M0L6_2atmpS1281);
  if (_M0L3lenS1279 == _M0L6_2atmpS1280) {
    int32_t _M0L3lenS1283 = _M0L4selfS353->$1;
    int32_t _M0L6_2atmpS1282 = _M0L3lenS1283 + 1;
    #line 409 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0MPC15array5Array7reallocGfE(_M0L4selfS353, _M0L6_2atmpS1282);
  }
  _M0L6lengthS354 = _M0L4selfS353->$1;
  _M0L3bufS1284 = _M0L4selfS353->$0;
  _M0L3bufS1284[_M0L6lengthS354] = _M0L5valueS355;
  _M0L6_2atmpS1285 = _M0L6lengthS354 + 1;
  _M0L4selfS353->$1 = _M0L6_2atmpS1285;
  return 0;
}

int32_t _M0MPC15array5Array7reallocGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS343,
  int32_t _M0L8requiredS345
) {
  int32_t _M0L8old__capS342;
  int32_t _M0L3lenS1270;
  int32_t _M0L8new__capS344;
  #line 302 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  #line 304 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__capS342 = _M0MPC15array5Array8capacityGiE(_M0L4selfS343);
  _M0L3lenS1270 = _M0L4selfS343->$1;
  #line 305 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__capS344
  = _M0FPB23array__growth__capacity(_M0L8old__capS342, _M0L3lenS1270, _M0L8requiredS345);
  #line 306 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0MPC15array5Array14resize__bufferGiE(_M0L4selfS343, _M0L8new__capS344);
  return 0;
}

int32_t _M0MPC15array5Array7reallocGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS347,
  int32_t _M0L8requiredS349
) {
  int32_t _M0L8old__capS346;
  int32_t _M0L3lenS1271;
  int32_t _M0L8new__capS348;
  #line 302 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  #line 304 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__capS346 = _M0MPC15array5Array8capacityGfE(_M0L4selfS347);
  _M0L3lenS1271 = _M0L4selfS347->$1;
  #line 305 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__capS348
  = _M0FPB23array__growth__capacity(_M0L8old__capS346, _M0L3lenS1271, _M0L8requiredS349);
  #line 306 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0MPC15array5Array14resize__bufferGfE(_M0L4selfS347, _M0L8new__capS348);
  return 0;
}

int32_t _M0MPC15array5Array14resize__bufferGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS331,
  int32_t _M0L13new__capacityS334
) {
  int32_t* _M0L8old__bufS330;
  int32_t _M0L3lenS332;
  int32_t _M0L9copy__lenS333;
  int32_t* _M0L8new__bufS335;
  int32_t* _M0L6_2aoldS1900;
  #line 242 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__bufS330 = _M0L4selfS331->$0;
  _M0L3lenS332 = _M0L4selfS331->$1;
  if (_M0L3lenS332 < _M0L13new__capacityS334) {
    _M0L9copy__lenS333 = _M0L3lenS332;
  } else {
    _M0L9copy__lenS333 = _M0L13new__capacityS334;
  }
  moonbit_incref(_M0L8old__bufS330);
  #line 249 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__bufS335
  = _M0MPB18UninitializedArray23make__and__blit_2einnerGiE(_M0L8old__bufS330, _M0L13new__capacityS334, _M0L9copy__lenS333, 0, 0);
  _M0L6_2aoldS1900 = _M0L4selfS331->$0;
  moonbit_decref(_M0L6_2aoldS1900);
  _M0L4selfS331->$0 = _M0L8new__bufS335;
  return 0;
}

int32_t _M0MPC15array5Array14resize__bufferGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS337,
  int32_t _M0L13new__capacityS340
) {
  float* _M0L8old__bufS336;
  int32_t _M0L3lenS338;
  int32_t _M0L9copy__lenS339;
  float* _M0L8new__bufS341;
  float* _M0L6_2aoldS1901;
  #line 242 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__bufS336 = _M0L4selfS337->$0;
  _M0L3lenS338 = _M0L4selfS337->$1;
  if (_M0L3lenS338 < _M0L13new__capacityS340) {
    _M0L9copy__lenS339 = _M0L3lenS338;
  } else {
    _M0L9copy__lenS339 = _M0L13new__capacityS340;
  }
  moonbit_incref(_M0L8old__bufS336);
  #line 249 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__bufS341
  = _M0MPB18UninitializedArray23make__and__blit_2einnerGfE(_M0L8old__bufS336, _M0L13new__capacityS340, _M0L9copy__lenS339, 0, 0);
  _M0L6_2aoldS1901 = _M0L4selfS337->$0;
  moonbit_decref(_M0L6_2aoldS1901);
  _M0L4selfS337->$0 = _M0L8new__bufS341;
  return 0;
}

int32_t _M0MPC15array5Array8capacityGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS328
) {
  int32_t* _M0L6_2atmpS1268;
  int32_t _result_1996;
  #line 132 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L6_2atmpS1268 = _M0MPC15array5Array6bufferGiE(_M0L4selfS328);
  _result_1996 = Moonbit_array_length(_M0L6_2atmpS1268);
  moonbit_decref(_M0L6_2atmpS1268);
  return _result_1996;
}

int32_t _M0MPC15array5Array8capacityGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS329
) {
  float* _M0L6_2atmpS1269;
  int32_t _result_1997;
  #line 132 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L6_2atmpS1269 = _M0MPC15array5Array6bufferGfE(_M0L4selfS329);
  _result_1997 = Moonbit_array_length(_M0L6_2atmpS1269);
  moonbit_decref(_M0L6_2atmpS1269);
  return _result_1997;
}

int32_t _M0FPB23array__growth__capacity(
  int32_t _M0L7currentS324,
  int32_t _M0L3lenS322,
  int32_t _M0L8requiredS321
) {
  int32_t _M0L5startS323;
  int32_t _M0L5spaceS325;
  #line 200 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  if (_M0L8requiredS321 < _M0L3lenS322) {
    #line 203 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_8.data);
  }
  if (_M0L7currentS324 == 0) {
    _M0L5startS323 = 8;
  } else {
    _M0L5startS323 = _M0L7currentS324;
  }
  _M0L5spaceS325 = _M0L5startS323;
  while (1) {
    if (_M0L5spaceS325 < _M0L8requiredS321) {
      int32_t _M0L4nextS326 = _M0L5spaceS325 * 2;
      if (_M0L4nextS326 <= _M0L5spaceS325) {
        return _M0L8requiredS321;
      }
      _M0L5spaceS325 = _M0L4nextS326;
      continue;
    } else {
      return _M0L5spaceS325;
    }
    break;
  }
}

int32_t _M0MPC15array5Array6lengthGfE(struct _M0TPB5ArrayGfE* _M0L4selfS320) {
  #line 147 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  return _M0L4selfS320->$1;
}

float* _M0MPC15array5Array6bufferGfE(struct _M0TPB5ArrayGfE* _M0L4selfS316) {
  float* _M0L8_2afieldS1902;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS1902 = _M0L4selfS316->$0;
  moonbit_incref(_M0L8_2afieldS1902);
  return _M0L8_2afieldS1902;
}

uint8_t* _M0MPC15array5Array6bufferGbE(struct _M0TPB5ArrayGbE* _M0L4selfS317) {
  uint8_t* _M0L8_2afieldS1903;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS1903 = _M0L4selfS317->$0;
  moonbit_incref(_M0L8_2afieldS1903);
  return _M0L8_2afieldS1903;
}

int32_t* _M0MPC15array5Array6bufferGiE(struct _M0TPB5ArrayGiE* _M0L4selfS318) {
  int32_t* _M0L8_2afieldS1904;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS1904 = _M0L4selfS318->$0;
  moonbit_incref(_M0L8_2afieldS1904);
  return _M0L8_2afieldS1904;
}

struct _M0TPB5ArrayGfE** _M0MPC15array5Array6bufferGRPB5ArrayGfEE(
  struct _M0TPB5ArrayGRPB5ArrayGfEE* _M0L4selfS319
) {
  struct _M0TPB5ArrayGfE** _M0L8_2afieldS1905;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS1905 = _M0L4selfS319->$0;
  moonbit_incref(_M0L8_2afieldS1905);
  return _M0L8_2afieldS1905;
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
  int32_t _M0L3endS1266;
  int32_t _M0L5startS1267;
  int32_t _M0L8str__lenS311;
  int32_t _M0L3lenS1265;
  int32_t _M0L8requiredS313;
  uint16_t* _M0L4dataS1258;
  int32_t _M0L6_2atmpS1257;
  int32_t _if__result_1999;
  uint16_t* _M0L4dataS1259;
  int32_t _M0L3lenS1260;
  moonbit_string_t _M0L6_2atmpS1261;
  int32_t _M0L6_2atmpS1262;
  int32_t _M0L3lenS1264;
  int32_t _M0L6_2atmpS1263;
  #line 158 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L3endS1266 = _M0L3strS312.$2;
  _M0L5startS1267 = _M0L3strS312.$1;
  _M0L8str__lenS311 = _M0L3endS1266 - _M0L5startS1267;
  if (_M0L8str__lenS311 == 0) {
    return 0;
  }
  _M0L3lenS1265 = _M0L4selfS314->$1;
  _M0L8requiredS313 = _M0L3lenS1265 + _M0L8str__lenS311;
  _M0L4dataS1258 = _M0L4selfS314->$0;
  _M0L6_2atmpS1257 = Moonbit_array_length(_M0L4dataS1258);
  if (_M0L8requiredS313 > _M0L6_2atmpS1257) {
    _if__result_1999 = 1;
  } else {
    int32_t _M0L3lenS1256 = _M0L4selfS314->$1;
    _if__result_1999 = _M0L8requiredS313 < _M0L3lenS1256;
  }
  if (_if__result_1999) {
    #line 168 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0MPB13StringBuilder4grow(_M0L4selfS314, _M0L8requiredS313);
  }
  _M0L4dataS1259 = _M0L4selfS314->$0;
  _M0L3lenS1260 = _M0L4selfS314->$1;
  moonbit_incref(_M0L4dataS1259);
  #line 172 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L6_2atmpS1261 = _M0MPC16string10StringView4data(_M0L3strS312);
  #line 173 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L6_2atmpS1262 = _M0MPC16string10StringView13start__offset(_M0L3strS312);
  #line 170 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0MPC15array10FixedArray26unsafe__blit__from__string(_M0L4dataS1259, _M0L3lenS1260, _M0L6_2atmpS1261, _M0L6_2atmpS1262, _M0L8str__lenS311);
  moonbit_decref(_M0L4dataS1259);
  moonbit_decref(_M0L6_2atmpS1261);
  _M0L3lenS1264 = _M0L4selfS314->$1;
  _M0L6_2atmpS1263 = _M0L3lenS1264 + _M0L8str__lenS311;
  _M0L4selfS314->$1 = _M0L6_2atmpS1263;
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
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_9.data);
  }
  if (_M0L4selfS303 == 0ull) {
    return (moonbit_string_t)moonbit_string_literal_1.data;
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
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_9.data);
  }
  if (_M0L4selfS286 == 0ll) {
    return (moonbit_string_t)moonbit_string_literal_1.data;
  }
  _M0L12is__negativeS287 = _M0L4selfS286 < 0ll;
  if (_M0L12is__negativeS287) {
    int64_t _M0L6_2atmpS1255 = -_M0L4selfS286;
    _M0L3numS288 = *(uint64_t*)&_M0L6_2atmpS1255;
  } else {
    _M0L3numS288 = *(uint64_t*)&_M0L4selfS286;
  }
  switch (_M0L5radixS285) {
    case 10: {
      int32_t _M0L10digit__lenS290;
      int32_t _M0L6_2atmpS1252;
      int32_t _M0L10total__lenS291;
      uint16_t* _M0L6bufferS292;
      int32_t _M0L12digit__startS293;
      #line 573 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS290 = _M0FPB12dec__count64(_M0L3numS288);
      if (_M0L12is__negativeS287) {
        _M0L6_2atmpS1252 = 1;
      } else {
        _M0L6_2atmpS1252 = 0;
      }
      _M0L10total__lenS291 = _M0L10digit__lenS290 + _M0L6_2atmpS1252;
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
      int32_t _M0L6_2atmpS1253;
      int32_t _M0L10total__lenS295;
      uint16_t* _M0L6bufferS296;
      int32_t _M0L12digit__startS297;
      #line 581 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS294 = _M0FPB12hex__count64(_M0L3numS288);
      if (_M0L12is__negativeS287) {
        _M0L6_2atmpS1253 = 1;
      } else {
        _M0L6_2atmpS1253 = 0;
      }
      _M0L10total__lenS295 = _M0L10digit__lenS294 + _M0L6_2atmpS1253;
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
      int32_t _M0L6_2atmpS1254;
      int32_t _M0L10total__lenS299;
      uint16_t* _M0L6bufferS300;
      int32_t _M0L12digit__startS301;
      #line 589 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS298
      = _M0FPB14radix__count64(_M0L3numS288, _M0L5radixS285);
      if (_M0L12is__negativeS287) {
        _M0L6_2atmpS1254 = 1;
      } else {
        _M0L6_2atmpS1254 = 0;
      }
      _M0L10total__lenS299 = _M0L10digit__lenS298 + _M0L6_2atmpS1254;
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
  int32_t _M0L6_2atmpS1251;
  uint64_t _M0L3numS261;
  int32_t _M0L6offsetS262;
  #line 493 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L6_2atmpS1251 = _M0L10total__lenS284 - _M0L12digit__startS272;
  _M0L3numS261 = _M0L3numS283;
  _M0L6offsetS262 = _M0L6_2atmpS1251;
  while (1) {
    if (_M0L3numS261 >= 10000ull) {
      uint64_t _M0L1tS263 = _M0L3numS261 / 10000ull;
      uint64_t _M0L6_2atmpS1228 = _M0L3numS261 % 10000ull;
      int32_t _M0L1rS264 = (int32_t)_M0L6_2atmpS1228;
      int32_t _M0L2d1S265 = _M0L1rS264 / 100;
      int32_t _M0L2d2S266 = _M0L1rS264 % 100;
      int32_t _M0L6_2atmpS1227 = _M0L2d1S265 / 10;
      int32_t _M0L6_2atmpS1226 = 48 + _M0L6_2atmpS1227;
      int32_t _M0L6d1__hiS267 = (uint16_t)_M0L6_2atmpS1226;
      int32_t _M0L6_2atmpS1225 = _M0L2d1S265 % 10;
      int32_t _M0L6_2atmpS1224 = 48 + _M0L6_2atmpS1225;
      int32_t _M0L6d1__loS268 = (uint16_t)_M0L6_2atmpS1224;
      int32_t _M0L6_2atmpS1223 = _M0L2d2S266 / 10;
      int32_t _M0L6_2atmpS1222 = 48 + _M0L6_2atmpS1223;
      int32_t _M0L6d2__hiS269 = (uint16_t)_M0L6_2atmpS1222;
      int32_t _M0L6_2atmpS1221 = _M0L2d2S266 % 10;
      int32_t _M0L6_2atmpS1220 = 48 + _M0L6_2atmpS1221;
      int32_t _M0L6d2__loS270 = (uint16_t)_M0L6_2atmpS1220;
      int32_t _M0L6_2atmpS1212 = _M0L12digit__startS272 + _M0L6offsetS262;
      int32_t _M0L6_2atmpS1211 = _M0L6_2atmpS1212 - 4;
      int32_t _M0L6_2atmpS1214;
      int32_t _M0L6_2atmpS1213;
      int32_t _M0L6_2atmpS1216;
      int32_t _M0L6_2atmpS1215;
      int32_t _M0L6_2atmpS1218;
      int32_t _M0L6_2atmpS1217;
      int32_t _M0L6_2atmpS1219;
      _M0L6bufferS271[_M0L6_2atmpS1211] = _M0L6d1__hiS267;
      _M0L6_2atmpS1214 = _M0L12digit__startS272 + _M0L6offsetS262;
      _M0L6_2atmpS1213 = _M0L6_2atmpS1214 - 3;
      _M0L6bufferS271[_M0L6_2atmpS1213] = _M0L6d1__loS268;
      _M0L6_2atmpS1216 = _M0L12digit__startS272 + _M0L6offsetS262;
      _M0L6_2atmpS1215 = _M0L6_2atmpS1216 - 2;
      _M0L6bufferS271[_M0L6_2atmpS1215] = _M0L6d2__hiS269;
      _M0L6_2atmpS1218 = _M0L12digit__startS272 + _M0L6offsetS262;
      _M0L6_2atmpS1217 = _M0L6_2atmpS1218 - 1;
      _M0L6bufferS271[_M0L6_2atmpS1217] = _M0L6d2__loS270;
      _M0L6_2atmpS1219 = _M0L6offsetS262 - 4;
      _M0L3numS261 = _M0L1tS263;
      _M0L6offsetS262 = _M0L6_2atmpS1219;
      continue;
    } else {
      int32_t _M0L6_2atmpS1250 = (int32_t)_M0L3numS261;
      int32_t _M0L9remainingS274 = _M0L6_2atmpS1250;
      int32_t _M0L6offsetS275 = _M0L6offsetS262;
      while (1) {
        if (_M0L9remainingS274 >= 100) {
          int32_t _M0L1tS276 = _M0L9remainingS274 / 100;
          int32_t _M0L1dS277 = _M0L9remainingS274 % 100;
          int32_t _M0L6_2atmpS1237 = _M0L1dS277 / 10;
          int32_t _M0L6_2atmpS1236 = 48 + _M0L6_2atmpS1237;
          int32_t _M0L5d__hiS278 = (uint16_t)_M0L6_2atmpS1236;
          int32_t _M0L6_2atmpS1235 = _M0L1dS277 % 10;
          int32_t _M0L6_2atmpS1234 = 48 + _M0L6_2atmpS1235;
          int32_t _M0L5d__loS279 = (uint16_t)_M0L6_2atmpS1234;
          int32_t _M0L6_2atmpS1230 = _M0L12digit__startS272 + _M0L6offsetS275;
          int32_t _M0L6_2atmpS1229 = _M0L6_2atmpS1230 - 2;
          int32_t _M0L6_2atmpS1232;
          int32_t _M0L6_2atmpS1231;
          int32_t _M0L6_2atmpS1233;
          _M0L6bufferS271[_M0L6_2atmpS1229] = _M0L5d__hiS278;
          _M0L6_2atmpS1232 = _M0L12digit__startS272 + _M0L6offsetS275;
          _M0L6_2atmpS1231 = _M0L6_2atmpS1232 - 1;
          _M0L6bufferS271[_M0L6_2atmpS1231] = _M0L5d__loS279;
          _M0L6_2atmpS1233 = _M0L6offsetS275 - 2;
          _M0L9remainingS274 = _M0L1tS276;
          _M0L6offsetS275 = _M0L6_2atmpS1233;
          continue;
        } else if (_M0L9remainingS274 >= 10) {
          int32_t _M0L6_2atmpS1245 = _M0L9remainingS274 / 10;
          int32_t _M0L6_2atmpS1244 = 48 + _M0L6_2atmpS1245;
          int32_t _M0L5d__hiS281 = (uint16_t)_M0L6_2atmpS1244;
          int32_t _M0L6_2atmpS1243 = _M0L9remainingS274 % 10;
          int32_t _M0L6_2atmpS1242 = 48 + _M0L6_2atmpS1243;
          int32_t _M0L5d__loS282 = (uint16_t)_M0L6_2atmpS1242;
          int32_t _M0L6_2atmpS1239 = _M0L12digit__startS272 + _M0L6offsetS275;
          int32_t _M0L6_2atmpS1238 = _M0L6_2atmpS1239 - 2;
          int32_t _M0L6_2atmpS1241;
          int32_t _M0L6_2atmpS1240;
          _M0L6bufferS271[_M0L6_2atmpS1238] = _M0L5d__hiS281;
          _M0L6_2atmpS1241 = _M0L12digit__startS272 + _M0L6offsetS275;
          _M0L6_2atmpS1240 = _M0L6_2atmpS1241 - 1;
          _M0L6bufferS271[_M0L6_2atmpS1240] = _M0L5d__loS282;
        } else {
          int32_t _M0L6_2atmpS1249 = _M0L12digit__startS272 + _M0L6offsetS275;
          int32_t _M0L6_2atmpS1246 = _M0L6_2atmpS1249 - 1;
          int32_t _M0L6_2atmpS1248 = 48 + _M0L9remainingS274;
          int32_t _M0L6_2atmpS1247 = (uint16_t)_M0L6_2atmpS1248;
          _M0L6bufferS271[_M0L6_2atmpS1246] = _M0L6_2atmpS1247;
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
  int32_t _M0L6_2atmpS1196;
  int32_t _M0L6_2atmpS1195;
  #line 462 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  #line 470 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L4baseS244 = _M0MPC13int3Int10to__uint64(_M0L5radixS245);
  _M0L6_2atmpS1196 = _M0L5radixS245 - 1;
  _M0L6_2atmpS1195 = _M0L5radixS245 & _M0L6_2atmpS1196;
  if (_M0L6_2atmpS1195 == 0) {
    int32_t _M0L5shiftS246;
    uint64_t _M0L4maskS247;
    int32_t _M0L6_2atmpS1203;
    int32_t _M0L6offsetS248;
    uint64_t _M0L1nS249;
    #line 473 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0L5shiftS246 = moonbit_ctz32(_M0L5radixS245);
    _M0L4maskS247 = _M0L4baseS244 - 1ull;
    _M0L6_2atmpS1203 = _M0L10total__lenS254 - _M0L12digit__startS252;
    _M0L6offsetS248 = _M0L6_2atmpS1203;
    _M0L1nS249 = _M0L3numS255;
    while (1) {
      if (_M0L1nS249 > 0ull) {
        uint64_t _M0L6_2atmpS1202 = _M0L1nS249 & _M0L4maskS247;
        int32_t _M0L5digitS250 = (int32_t)_M0L6_2atmpS1202;
        int32_t _M0L6_2atmpS1199 = _M0L12digit__startS252 + _M0L6offsetS248;
        int32_t _M0L6_2atmpS1197 = _M0L6_2atmpS1199 - 1;
        int32_t _M0L6_2atmpS1198 =
          ((moonbit_string_t)moonbit_string_literal_10.data)[_M0L5digitS250];
        int32_t _M0L6_2atmpS1200;
        uint64_t _M0L6_2atmpS1201;
        _M0L6bufferS251[_M0L6_2atmpS1197] = _M0L6_2atmpS1198;
        _M0L6_2atmpS1200 = _M0L6offsetS248 - 1;
        _M0L6_2atmpS1201 = _M0L1nS249 >> (_M0L5shiftS246 & 63);
        _M0L6offsetS248 = _M0L6_2atmpS1200;
        _M0L1nS249 = _M0L6_2atmpS1201;
        continue;
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1210 = _M0L10total__lenS254 - _M0L12digit__startS252;
    int32_t _M0L6offsetS256 = _M0L6_2atmpS1210;
    uint64_t _M0L1nS257 = _M0L3numS255;
    while (1) {
      if (_M0L1nS257 > 0ull) {
        uint64_t _M0L1qS258 = _M0L1nS257 / _M0L4baseS244;
        uint64_t _M0L6_2atmpS1209 = _M0L1qS258 * _M0L4baseS244;
        uint64_t _M0L6_2atmpS1208 = _M0L1nS257 - _M0L6_2atmpS1209;
        int32_t _M0L5digitS259 = (int32_t)_M0L6_2atmpS1208;
        int32_t _M0L6_2atmpS1206 = _M0L12digit__startS252 + _M0L6offsetS256;
        int32_t _M0L6_2atmpS1204 = _M0L6_2atmpS1206 - 1;
        int32_t _M0L6_2atmpS1205 =
          ((moonbit_string_t)moonbit_string_literal_10.data)[_M0L5digitS259];
        int32_t _M0L6_2atmpS1207;
        _M0L6bufferS251[_M0L6_2atmpS1204] = _M0L6_2atmpS1205;
        _M0L6_2atmpS1207 = _M0L6offsetS256 - 1;
        _M0L6offsetS256 = _M0L6_2atmpS1207;
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
  int32_t _M0L6_2atmpS1194;
  int32_t _M0L6offsetS233;
  uint64_t _M0L1nS234;
  #line 434 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L6_2atmpS1194 = _M0L10total__lenS242 - _M0L12digit__startS239;
  _M0L6offsetS233 = _M0L6_2atmpS1194;
  _M0L1nS234 = _M0L3numS243;
  while (1) {
    if (_M0L6offsetS233 >= 2) {
      uint64_t _M0L6_2atmpS1191 = _M0L1nS234 & 255ull;
      int32_t _M0L9byte__valS235 = (int32_t)_M0L6_2atmpS1191;
      int32_t _M0L2hiS236 = _M0L9byte__valS235 / 16;
      int32_t _M0L2loS237 = _M0L9byte__valS235 % 16;
      int32_t _M0L6_2atmpS1185 = _M0L12digit__startS239 + _M0L6offsetS233;
      int32_t _M0L6_2atmpS1183 = _M0L6_2atmpS1185 - 2;
      int32_t _M0L6_2atmpS1184 =
        ((moonbit_string_t)moonbit_string_literal_10.data)[_M0L2hiS236];
      int32_t _M0L6_2atmpS1188;
      int32_t _M0L6_2atmpS1186;
      int32_t _M0L6_2atmpS1187;
      int32_t _M0L6_2atmpS1189;
      uint64_t _M0L6_2atmpS1190;
      _M0L6bufferS238[_M0L6_2atmpS1183] = _M0L6_2atmpS1184;
      _M0L6_2atmpS1188 = _M0L12digit__startS239 + _M0L6offsetS233;
      _M0L6_2atmpS1186 = _M0L6_2atmpS1188 - 1;
      _M0L6_2atmpS1187
      = ((moonbit_string_t)moonbit_string_literal_10.data)[
        _M0L2loS237
      ];
      _M0L6bufferS238[_M0L6_2atmpS1186] = _M0L6_2atmpS1187;
      _M0L6_2atmpS1189 = _M0L6offsetS233 - 2;
      _M0L6_2atmpS1190 = _M0L1nS234 >> 8;
      _M0L6offsetS233 = _M0L6_2atmpS1189;
      _M0L1nS234 = _M0L6_2atmpS1190;
      continue;
    } else if (_M0L6offsetS233 == 1) {
      uint64_t _M0L6_2atmpS1193 = _M0L1nS234 & 15ull;
      int32_t _M0L6nibbleS241 = (int32_t)_M0L6_2atmpS1193;
      int32_t _M0L6_2atmpS1192 =
        ((moonbit_string_t)moonbit_string_literal_10.data)[_M0L6nibbleS241];
      _M0L6bufferS238[_M0L12digit__startS239] = _M0L6_2atmpS1192;
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
      uint64_t _M0L6_2atmpS1181 = _M0L3numS230 / _M0L4baseS228;
      int32_t _M0L6_2atmpS1182 = _M0L5countS231 + 1;
      _M0L3numS230 = _M0L6_2atmpS1181;
      _M0L5countS231 = _M0L6_2atmpS1182;
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
    int32_t _M0L6_2atmpS1180;
    int32_t _M0L6_2atmpS1179;
    #line 412 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0L14leading__zerosS226 = moonbit_clz64(_M0L5valueS225);
    _M0L6_2atmpS1180 = 63 - _M0L14leading__zerosS226;
    _M0L6_2atmpS1179 = _M0L6_2atmpS1180 / 4;
    return _M0L6_2atmpS1179 + 1;
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
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_9.data);
  }
  if (_M0L4selfS208 == 0) {
    return (moonbit_string_t)moonbit_string_literal_1.data;
  }
  _M0L12is__negativeS209 = _M0L4selfS208 < 0;
  if (_M0L12is__negativeS209) {
    int32_t _M0L6_2atmpS1178 = -_M0L4selfS208;
    _M0L3numS210 = *(uint32_t*)&_M0L6_2atmpS1178;
  } else {
    _M0L3numS210 = *(uint32_t*)&_M0L4selfS208;
  }
  switch (_M0L5radixS207) {
    case 10: {
      int32_t _M0L10digit__lenS212;
      int32_t _M0L6_2atmpS1175;
      int32_t _M0L10total__lenS213;
      uint16_t* _M0L6bufferS214;
      int32_t _M0L12digit__startS215;
      #line 235 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS212 = _M0FPB12dec__count32(_M0L3numS210);
      if (_M0L12is__negativeS209) {
        _M0L6_2atmpS1175 = 1;
      } else {
        _M0L6_2atmpS1175 = 0;
      }
      _M0L10total__lenS213 = _M0L10digit__lenS212 + _M0L6_2atmpS1175;
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
      int32_t _M0L6_2atmpS1176;
      int32_t _M0L10total__lenS217;
      uint16_t* _M0L6bufferS218;
      int32_t _M0L12digit__startS219;
      #line 243 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS216 = _M0FPB12hex__count32(_M0L3numS210);
      if (_M0L12is__negativeS209) {
        _M0L6_2atmpS1176 = 1;
      } else {
        _M0L6_2atmpS1176 = 0;
      }
      _M0L10total__lenS217 = _M0L10digit__lenS216 + _M0L6_2atmpS1176;
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
      int32_t _M0L6_2atmpS1177;
      int32_t _M0L10total__lenS221;
      uint16_t* _M0L6bufferS222;
      int32_t _M0L12digit__startS223;
      #line 251 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS220
      = _M0FPB14radix__count32(_M0L3numS210, _M0L5radixS207);
      if (_M0L12is__negativeS209) {
        _M0L6_2atmpS1177 = 1;
      } else {
        _M0L6_2atmpS1177 = 0;
      }
      _M0L10total__lenS221 = _M0L10digit__lenS220 + _M0L6_2atmpS1177;
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
      uint32_t _M0L6_2atmpS1173 = _M0L3numS204 / _M0L4baseS202;
      int32_t _M0L6_2atmpS1174 = _M0L5countS205 + 1;
      _M0L3numS204 = _M0L6_2atmpS1173;
      _M0L5countS205 = _M0L6_2atmpS1174;
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
    int32_t _M0L6_2atmpS1172;
    int32_t _M0L6_2atmpS1171;
    #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0L14leading__zerosS200 = moonbit_clz32(_M0L5valueS199);
    _M0L6_2atmpS1172 = 31 - _M0L14leading__zerosS200;
    _M0L6_2atmpS1171 = _M0L6_2atmpS1172 / 4;
    return _M0L6_2atmpS1171 + 1;
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
  int32_t _M0L6_2atmpS1170;
  uint32_t _M0L3numS174;
  int32_t _M0L6offsetS175;
  #line 88 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L6_2atmpS1170 = _M0L10total__lenS197 - _M0L12digit__startS185;
  _M0L3numS174 = _M0L3numS196;
  _M0L6offsetS175 = _M0L6_2atmpS1170;
  while (1) {
    if (_M0L3numS174 >= 10000u) {
      uint32_t _M0L1tS176 = _M0L3numS174 / 10000u;
      uint32_t _M0L6_2atmpS1147 = _M0L3numS174 % 10000u;
      int32_t _M0L1rS177 = *(int32_t*)&_M0L6_2atmpS1147;
      int32_t _M0L2d1S178 = _M0L1rS177 / 100;
      int32_t _M0L2d2S179 = _M0L1rS177 % 100;
      int32_t _M0L6_2atmpS1146 = _M0L2d1S178 / 10;
      int32_t _M0L6_2atmpS1145 = 48 + _M0L6_2atmpS1146;
      int32_t _M0L6d1__hiS180 = (uint16_t)_M0L6_2atmpS1145;
      int32_t _M0L6_2atmpS1144 = _M0L2d1S178 % 10;
      int32_t _M0L6_2atmpS1143 = 48 + _M0L6_2atmpS1144;
      int32_t _M0L6d1__loS181 = (uint16_t)_M0L6_2atmpS1143;
      int32_t _M0L6_2atmpS1142 = _M0L2d2S179 / 10;
      int32_t _M0L6_2atmpS1141 = 48 + _M0L6_2atmpS1142;
      int32_t _M0L6d2__hiS182 = (uint16_t)_M0L6_2atmpS1141;
      int32_t _M0L6_2atmpS1140 = _M0L2d2S179 % 10;
      int32_t _M0L6_2atmpS1139 = 48 + _M0L6_2atmpS1140;
      int32_t _M0L6d2__loS183 = (uint16_t)_M0L6_2atmpS1139;
      int32_t _M0L6_2atmpS1131 = _M0L12digit__startS185 + _M0L6offsetS175;
      int32_t _M0L6_2atmpS1130 = _M0L6_2atmpS1131 - 4;
      int32_t _M0L6_2atmpS1133;
      int32_t _M0L6_2atmpS1132;
      int32_t _M0L6_2atmpS1135;
      int32_t _M0L6_2atmpS1134;
      int32_t _M0L6_2atmpS1137;
      int32_t _M0L6_2atmpS1136;
      int32_t _M0L6_2atmpS1138;
      _M0L6bufferS184[_M0L6_2atmpS1130] = _M0L6d1__hiS180;
      _M0L6_2atmpS1133 = _M0L12digit__startS185 + _M0L6offsetS175;
      _M0L6_2atmpS1132 = _M0L6_2atmpS1133 - 3;
      _M0L6bufferS184[_M0L6_2atmpS1132] = _M0L6d1__loS181;
      _M0L6_2atmpS1135 = _M0L12digit__startS185 + _M0L6offsetS175;
      _M0L6_2atmpS1134 = _M0L6_2atmpS1135 - 2;
      _M0L6bufferS184[_M0L6_2atmpS1134] = _M0L6d2__hiS182;
      _M0L6_2atmpS1137 = _M0L12digit__startS185 + _M0L6offsetS175;
      _M0L6_2atmpS1136 = _M0L6_2atmpS1137 - 1;
      _M0L6bufferS184[_M0L6_2atmpS1136] = _M0L6d2__loS183;
      _M0L6_2atmpS1138 = _M0L6offsetS175 - 4;
      _M0L3numS174 = _M0L1tS176;
      _M0L6offsetS175 = _M0L6_2atmpS1138;
      continue;
    } else {
      int32_t _M0L6_2atmpS1169 = *(int32_t*)&_M0L3numS174;
      int32_t _M0L9remainingS187 = _M0L6_2atmpS1169;
      int32_t _M0L6offsetS188 = _M0L6offsetS175;
      while (1) {
        if (_M0L9remainingS187 >= 100) {
          int32_t _M0L1tS189 = _M0L9remainingS187 / 100;
          int32_t _M0L1dS190 = _M0L9remainingS187 % 100;
          int32_t _M0L6_2atmpS1156 = _M0L1dS190 / 10;
          int32_t _M0L6_2atmpS1155 = 48 + _M0L6_2atmpS1156;
          int32_t _M0L5d__hiS191 = (uint16_t)_M0L6_2atmpS1155;
          int32_t _M0L6_2atmpS1154 = _M0L1dS190 % 10;
          int32_t _M0L6_2atmpS1153 = 48 + _M0L6_2atmpS1154;
          int32_t _M0L5d__loS192 = (uint16_t)_M0L6_2atmpS1153;
          int32_t _M0L6_2atmpS1149 = _M0L12digit__startS185 + _M0L6offsetS188;
          int32_t _M0L6_2atmpS1148 = _M0L6_2atmpS1149 - 2;
          int32_t _M0L6_2atmpS1151;
          int32_t _M0L6_2atmpS1150;
          int32_t _M0L6_2atmpS1152;
          _M0L6bufferS184[_M0L6_2atmpS1148] = _M0L5d__hiS191;
          _M0L6_2atmpS1151 = _M0L12digit__startS185 + _M0L6offsetS188;
          _M0L6_2atmpS1150 = _M0L6_2atmpS1151 - 1;
          _M0L6bufferS184[_M0L6_2atmpS1150] = _M0L5d__loS192;
          _M0L6_2atmpS1152 = _M0L6offsetS188 - 2;
          _M0L9remainingS187 = _M0L1tS189;
          _M0L6offsetS188 = _M0L6_2atmpS1152;
          continue;
        } else if (_M0L9remainingS187 >= 10) {
          int32_t _M0L6_2atmpS1164 = _M0L9remainingS187 / 10;
          int32_t _M0L6_2atmpS1163 = 48 + _M0L6_2atmpS1164;
          int32_t _M0L5d__hiS194 = (uint16_t)_M0L6_2atmpS1163;
          int32_t _M0L6_2atmpS1162 = _M0L9remainingS187 % 10;
          int32_t _M0L6_2atmpS1161 = 48 + _M0L6_2atmpS1162;
          int32_t _M0L5d__loS195 = (uint16_t)_M0L6_2atmpS1161;
          int32_t _M0L6_2atmpS1158 = _M0L12digit__startS185 + _M0L6offsetS188;
          int32_t _M0L6_2atmpS1157 = _M0L6_2atmpS1158 - 2;
          int32_t _M0L6_2atmpS1160;
          int32_t _M0L6_2atmpS1159;
          _M0L6bufferS184[_M0L6_2atmpS1157] = _M0L5d__hiS194;
          _M0L6_2atmpS1160 = _M0L12digit__startS185 + _M0L6offsetS188;
          _M0L6_2atmpS1159 = _M0L6_2atmpS1160 - 1;
          _M0L6bufferS184[_M0L6_2atmpS1159] = _M0L5d__loS195;
        } else {
          int32_t _M0L6_2atmpS1168 = _M0L12digit__startS185 + _M0L6offsetS188;
          int32_t _M0L6_2atmpS1165 = _M0L6_2atmpS1168 - 1;
          int32_t _M0L6_2atmpS1167 = 48 + _M0L9remainingS187;
          int32_t _M0L6_2atmpS1166 = (uint16_t)_M0L6_2atmpS1167;
          _M0L6bufferS184[_M0L6_2atmpS1165] = _M0L6_2atmpS1166;
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
  int32_t _M0L6_2atmpS1115;
  int32_t _M0L6_2atmpS1114;
  #line 57 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L4baseS157 = *(uint32_t*)&_M0L5radixS158;
  _M0L6_2atmpS1115 = _M0L5radixS158 - 1;
  _M0L6_2atmpS1114 = _M0L5radixS158 & _M0L6_2atmpS1115;
  if (_M0L6_2atmpS1114 == 0) {
    int32_t _M0L5shiftS159;
    uint32_t _M0L4maskS160;
    int32_t _M0L6_2atmpS1122;
    int32_t _M0L6offsetS161;
    uint32_t _M0L1nS162;
    #line 68 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0L5shiftS159 = moonbit_ctz32(_M0L5radixS158);
    _M0L4maskS160 = _M0L4baseS157 - 1u;
    _M0L6_2atmpS1122 = _M0L10total__lenS167 - _M0L12digit__startS165;
    _M0L6offsetS161 = _M0L6_2atmpS1122;
    _M0L1nS162 = _M0L3numS168;
    while (1) {
      if (_M0L1nS162 > 0u) {
        uint32_t _M0L6_2atmpS1121 = _M0L1nS162 & _M0L4maskS160;
        int32_t _M0L5digitS163 = *(int32_t*)&_M0L6_2atmpS1121;
        int32_t _M0L6_2atmpS1118 = _M0L12digit__startS165 + _M0L6offsetS161;
        int32_t _M0L6_2atmpS1116 = _M0L6_2atmpS1118 - 1;
        int32_t _M0L6_2atmpS1117 =
          ((moonbit_string_t)moonbit_string_literal_10.data)[_M0L5digitS163];
        int32_t _M0L6_2atmpS1119;
        uint32_t _M0L6_2atmpS1120;
        _M0L6bufferS164[_M0L6_2atmpS1116] = _M0L6_2atmpS1117;
        _M0L6_2atmpS1119 = _M0L6offsetS161 - 1;
        _M0L6_2atmpS1120 = _M0L1nS162 >> (_M0L5shiftS159 & 31);
        _M0L6offsetS161 = _M0L6_2atmpS1119;
        _M0L1nS162 = _M0L6_2atmpS1120;
        continue;
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1129 = _M0L10total__lenS167 - _M0L12digit__startS165;
    int32_t _M0L6offsetS169 = _M0L6_2atmpS1129;
    uint32_t _M0L1nS170 = _M0L3numS168;
    while (1) {
      if (_M0L1nS170 > 0u) {
        uint32_t _M0L1qS171 = _M0L1nS170 / _M0L4baseS157;
        uint32_t _M0L6_2atmpS1128 = _M0L1qS171 * _M0L4baseS157;
        uint32_t _M0L6_2atmpS1127 = _M0L1nS170 - _M0L6_2atmpS1128;
        int32_t _M0L5digitS172 = *(int32_t*)&_M0L6_2atmpS1127;
        int32_t _M0L6_2atmpS1125 = _M0L12digit__startS165 + _M0L6offsetS169;
        int32_t _M0L6_2atmpS1123 = _M0L6_2atmpS1125 - 1;
        int32_t _M0L6_2atmpS1124 =
          ((moonbit_string_t)moonbit_string_literal_10.data)[_M0L5digitS172];
        int32_t _M0L6_2atmpS1126;
        _M0L6bufferS164[_M0L6_2atmpS1123] = _M0L6_2atmpS1124;
        _M0L6_2atmpS1126 = _M0L6offsetS169 - 1;
        _M0L6offsetS169 = _M0L6_2atmpS1126;
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
  int32_t _M0L6_2atmpS1113;
  int32_t _M0L6offsetS146;
  uint32_t _M0L1nS147;
  #line 29 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L6_2atmpS1113 = _M0L10total__lenS155 - _M0L12digit__startS152;
  _M0L6offsetS146 = _M0L6_2atmpS1113;
  _M0L1nS147 = _M0L3numS156;
  while (1) {
    if (_M0L6offsetS146 >= 2) {
      uint32_t _M0L6_2atmpS1110 = _M0L1nS147 & 255u;
      int32_t _M0L9byte__valS148 = *(int32_t*)&_M0L6_2atmpS1110;
      int32_t _M0L2hiS149 = _M0L9byte__valS148 / 16;
      int32_t _M0L2loS150 = _M0L9byte__valS148 % 16;
      int32_t _M0L6_2atmpS1104 = _M0L12digit__startS152 + _M0L6offsetS146;
      int32_t _M0L6_2atmpS1102 = _M0L6_2atmpS1104 - 2;
      int32_t _M0L6_2atmpS1103 =
        ((moonbit_string_t)moonbit_string_literal_10.data)[_M0L2hiS149];
      int32_t _M0L6_2atmpS1107;
      int32_t _M0L6_2atmpS1105;
      int32_t _M0L6_2atmpS1106;
      int32_t _M0L6_2atmpS1108;
      uint32_t _M0L6_2atmpS1109;
      _M0L6bufferS151[_M0L6_2atmpS1102] = _M0L6_2atmpS1103;
      _M0L6_2atmpS1107 = _M0L12digit__startS152 + _M0L6offsetS146;
      _M0L6_2atmpS1105 = _M0L6_2atmpS1107 - 1;
      _M0L6_2atmpS1106
      = ((moonbit_string_t)moonbit_string_literal_10.data)[
        _M0L2loS150
      ];
      _M0L6bufferS151[_M0L6_2atmpS1105] = _M0L6_2atmpS1106;
      _M0L6_2atmpS1108 = _M0L6offsetS146 - 2;
      _M0L6_2atmpS1109 = _M0L1nS147 >> 8;
      _M0L6offsetS146 = _M0L6_2atmpS1108;
      _M0L1nS147 = _M0L6_2atmpS1109;
      continue;
    } else if (_M0L6offsetS146 == 1) {
      uint32_t _M0L6_2atmpS1112 = _M0L1nS147 & 15u;
      int32_t _M0L6nibbleS154 = *(int32_t*)&_M0L6_2atmpS1112;
      int32_t _M0L6_2atmpS1111 =
        ((moonbit_string_t)moonbit_string_literal_10.data)[_M0L6nibbleS154];
      _M0L6bufferS151[_M0L12digit__startS152] = _M0L6_2atmpS1111;
    }
    break;
  }
  return 0;
}

int32_t _M0IP016_24default__implPB4Show6outputGmE(
  uint64_t _M0L4selfS143,
  struct _M0TPB6Logger _M0L6loggerS142
) {
  moonbit_string_t _M0L6_2atmpS1100;
  #line 159 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  #line 160 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS1100 = _M0IPC16uint646UInt64PB4Show10to__string(_M0L4selfS143);
  #line 160 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6loggerS142.$0->$method_0(_M0L6loggerS142.$1, _M0L6_2atmpS1100);
  moonbit_decref(_M0L6_2atmpS1100);
  return 0;
}

int32_t _M0IP016_24default__implPB4Show6outputGiE(
  int32_t _M0L4selfS145,
  struct _M0TPB6Logger _M0L6loggerS144
) {
  moonbit_string_t _M0L6_2atmpS1101;
  #line 159 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  #line 160 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS1101 = _M0IPC13int3IntPB4Show10to__string(_M0L4selfS145);
  #line 160 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6loggerS144.$0->$method_0(_M0L6loggerS144.$1, _M0L6_2atmpS1101);
  moonbit_decref(_M0L6_2atmpS1101);
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
  moonbit_string_t _M0L8_2afieldS1906;
  #line 92 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
  _M0L8_2afieldS1906 = _M0L4selfS140.$0;
  moonbit_incref(_M0L8_2afieldS1906);
  return _M0L8_2afieldS1906;
}

int32_t _M0IP016_24default__implPB6Logger16write__substringGRPB13StringBuilderE(
  struct _M0TPB13StringBuilder* _M0L4selfS136,
  moonbit_string_t _M0L5valueS137,
  int32_t _M0L5startS138,
  int32_t _M0L3lenS139
) {
  int32_t _M0L6_2atmpS1099;
  int64_t _M0L6_2atmpS1098;
  struct _M0TPC16string10StringView _M0L6_2atmpS1097;
  #line 122 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS1099 = _M0L5startS138 + _M0L3lenS139;
  _M0L6_2atmpS1098 = (int64_t)_M0L6_2atmpS1099;
  #line 123 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS1097
  = _M0MPC16string6String11sub_2einner(_M0L5valueS137, _M0L5startS138, _M0L6_2atmpS1098);
  #line 123 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0IPB13StringBuilderPB6Logger11write__view(_M0L4selfS136, _M0L6_2atmpS1097);
  moonbit_decref(_M0L6_2atmpS1097.$0);
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
  goto joinlet_2012;
  join_130:;
  _M0L3endS129 = _M0L3endS131;
  joinlet_2012:;
  if (
    _M0L5startS135 >= 0
    && _M0L5startS135 <= _M0L3endS129
    && _M0L3endS129 <= _M0L3lenS127
  ) {
    if (_M0L5startS135 < _M0L3lenS127) {
      int32_t _M0L6_2atmpS1094 = _M0L4selfS128[_M0L5startS135];
      int32_t _M0L6_2atmpS1093;
      #line 931 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
      _M0L6_2atmpS1093
      = _M0MPC16uint166UInt1623is__trailing__surrogate(_M0L6_2atmpS1094);
      if (!_M0L6_2atmpS1093) {
        
      } else {
        #line 931 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
        moonbit_panic();
      }
    }
    if (_M0L3endS129 < _M0L3lenS127) {
      int32_t _M0L6_2atmpS1096 = _M0L4selfS128[_M0L3endS129];
      int32_t _M0L6_2atmpS1095;
      #line 934 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
      _M0L6_2atmpS1095
      = _M0MPC16uint166UInt1623is__trailing__surrogate(_M0L6_2atmpS1096);
      if (!_M0L6_2atmpS1095) {
        
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
  struct _M0TPB6Logger _M0L6_2atmpS1092;
  #line 116 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  moonbit_incref(_M0L4selfS126);
  _M0L6_2atmpS1092
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS126
  };
  #line 117 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L4showS125.$0->$method_0(_M0L4showS125.$1, _M0L6_2atmpS1092);
  if (_M0L6_2atmpS1092.$1) {
    moonbit_decref(_M0L6_2atmpS1092.$1);
  }
  return 0;
}

int32_t _M0IP016_24default__implPB6Logger28write__string__interpolationGRPB13StringBuilderE(
  struct _M0TPB13StringBuilder* _M0L4selfS124,
  struct _M0TPB4Show _M0L4showS123
) {
  struct _M0TPB6Logger _M0L6_2atmpS1091;
  #line 111 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  moonbit_incref(_M0L4selfS124);
  _M0L6_2atmpS1091
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS124
  };
  #line 112 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L4showS123.$0->$method_0(_M0L4showS123.$1, _M0L6_2atmpS1091);
  if (_M0L6_2atmpS1091.$1) {
    moonbit_decref(_M0L6_2atmpS1091.$1);
  }
  return 0;
}

uint64_t _M0MPC13int3Int10to__uint64(int32_t _M0L4selfS122) {
  int64_t _M0L6_2atmpS1090;
  #line 989 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS1090 = (int64_t)_M0L4selfS122;
  return *(uint64_t*)&_M0L6_2atmpS1090;
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
  int32_t _M0L3lenS1089;
  int32_t _M0L8requiredS119;
  uint16_t* _M0L4dataS1084;
  int32_t _M0L6_2atmpS1083;
  int32_t _if__result_2013;
  uint16_t* _M0L4dataS1085;
  int32_t _M0L3lenS1086;
  int32_t _M0L3lenS1088;
  int32_t _M0L6_2atmpS1087;
  #line 105 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L8str__lenS117 = Moonbit_array_length(_M0L3strS118);
  if (_M0L8str__lenS117 == 0) {
    return 0;
  }
  _M0L3lenS1089 = _M0L4selfS120->$1;
  _M0L8requiredS119 = _M0L3lenS1089 + _M0L8str__lenS117;
  _M0L4dataS1084 = _M0L4selfS120->$0;
  _M0L6_2atmpS1083 = Moonbit_array_length(_M0L4dataS1084);
  if (_M0L8requiredS119 > _M0L6_2atmpS1083) {
    _if__result_2013 = 1;
  } else {
    int32_t _M0L3lenS1082 = _M0L4selfS120->$1;
    _if__result_2013 = _M0L8requiredS119 < _M0L3lenS1082;
  }
  if (_if__result_2013) {
    #line 112 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0MPB13StringBuilder4grow(_M0L4selfS120, _M0L8requiredS119);
  }
  _M0L4dataS1085 = _M0L4selfS120->$0;
  _M0L3lenS1086 = _M0L4selfS120->$1;
  moonbit_incref(_M0L4dataS1085);
  #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0MPC15array10FixedArray26unsafe__blit__from__string(_M0L4dataS1085, _M0L3lenS1086, _M0L3strS118, 0, _M0L8str__lenS117);
  moonbit_decref(_M0L4dataS1085);
  _M0L3lenS1088 = _M0L4selfS120->$1;
  _M0L6_2atmpS1087 = _M0L3lenS1088 + _M0L8str__lenS117;
  _M0L4selfS120->$1 = _M0L6_2atmpS1087;
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
      int32_t _M0L6_2atmpS1079 = _M0L3strS114[_M0L1iS111];
      int32_t _M0L6_2atmpS1080;
      int32_t _M0L6_2atmpS1081;
      _M0L4selfS113[_M0L1jS112] = _M0L6_2atmpS1079;
      _M0L6_2atmpS1080 = _M0L1iS111 + 1;
      _M0L6_2atmpS1081 = _M0L1jS112 + 1;
      _M0L1iS111 = _M0L6_2atmpS1080;
      _M0L1jS112 = _M0L6_2atmpS1081;
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
    int32_t _M0L3lenS1050 = _M0L4selfS106->$1;
    uint16_t* _M0L4dataS1052 = _M0L4selfS106->$0;
    int32_t _M0L6_2atmpS1051 = Moonbit_array_length(_M0L4dataS1052);
    uint16_t* _M0L4dataS1055;
    int32_t _M0L3lenS1056;
    int32_t _M0L6_2atmpS1057;
    int32_t _M0L3lenS1059;
    int32_t _M0L6_2atmpS1058;
    if (_M0L3lenS1050 >= _M0L6_2atmpS1051) {
      int32_t _M0L3lenS1054 = _M0L4selfS106->$1;
      int32_t _M0L6_2atmpS1053 = _M0L3lenS1054 + 1;
      #line 124 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0MPB13StringBuilder4grow(_M0L4selfS106, _M0L6_2atmpS1053);
    }
    _M0L4dataS1055 = _M0L4selfS106->$0;
    _M0L3lenS1056 = _M0L4selfS106->$1;
    moonbit_incref(_M0L4dataS1055);
    #line 126 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0L6_2atmpS1057 = _M0MPC14uint4UInt10to__uint16(_M0L4codeS104);
    if (
      _M0L3lenS1056 < 0
      || _M0L3lenS1056 >= Moonbit_array_length(_M0L4dataS1055)
    ) {
      #line 126 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      moonbit_panic();
    }
    _M0L4dataS1055[_M0L3lenS1056] = _M0L6_2atmpS1057;
    moonbit_decref(_M0L4dataS1055);
    _M0L3lenS1059 = _M0L4selfS106->$1;
    _M0L6_2atmpS1058 = _M0L3lenS1059 + 1;
    _M0L4selfS106->$1 = _M0L6_2atmpS1058;
  } else if (_M0L4codeS104 <= 1114111u) {
    uint16_t* _M0L4dataS1063 = _M0L4selfS106->$0;
    int32_t _M0L6_2atmpS1061 = Moonbit_array_length(_M0L4dataS1063);
    int32_t _M0L3lenS1062 = _M0L4selfS106->$1;
    int32_t _M0L6_2atmpS1060 = _M0L6_2atmpS1061 - _M0L3lenS1062;
    uint32_t _M0L4codeS107;
    uint16_t* _M0L4dataS1066;
    int32_t _M0L3lenS1067;
    uint32_t _M0L6_2atmpS1070;
    uint32_t _M0L6_2atmpS1069;
    int32_t _M0L6_2atmpS1068;
    uint16_t* _M0L4dataS1071;
    int32_t _M0L3lenS1076;
    int32_t _M0L6_2atmpS1072;
    uint32_t _M0L6_2atmpS1075;
    uint32_t _M0L6_2atmpS1074;
    int32_t _M0L6_2atmpS1073;
    int32_t _M0L3lenS1078;
    int32_t _M0L6_2atmpS1077;
    if (_M0L6_2atmpS1060 < 2) {
      int32_t _M0L3lenS1065 = _M0L4selfS106->$1;
      int32_t _M0L6_2atmpS1064 = _M0L3lenS1065 + 2;
      #line 130 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0MPB13StringBuilder4grow(_M0L4selfS106, _M0L6_2atmpS1064);
    }
    _M0L4codeS107 = _M0L4codeS104 - 65536u;
    _M0L4dataS1066 = _M0L4selfS106->$0;
    _M0L3lenS1067 = _M0L4selfS106->$1;
    _M0L6_2atmpS1070 = _M0L4codeS107 >> 10;
    _M0L6_2atmpS1069 = 55296u + _M0L6_2atmpS1070;
    moonbit_incref(_M0L4dataS1066);
    #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0L6_2atmpS1068 = _M0MPC14uint4UInt10to__uint16(_M0L6_2atmpS1069);
    if (
      _M0L3lenS1067 < 0
      || _M0L3lenS1067 >= Moonbit_array_length(_M0L4dataS1066)
    ) {
      #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      moonbit_panic();
    }
    _M0L4dataS1066[_M0L3lenS1067] = _M0L6_2atmpS1068;
    moonbit_decref(_M0L4dataS1066);
    _M0L4dataS1071 = _M0L4selfS106->$0;
    _M0L3lenS1076 = _M0L4selfS106->$1;
    _M0L6_2atmpS1072 = _M0L3lenS1076 + 1;
    _M0L6_2atmpS1075 = _M0L4codeS107 & 1023u;
    _M0L6_2atmpS1074 = 56320u + _M0L6_2atmpS1075;
    moonbit_incref(_M0L4dataS1071);
    #line 134 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0L6_2atmpS1073 = _M0MPC14uint4UInt10to__uint16(_M0L6_2atmpS1074);
    if (
      _M0L6_2atmpS1072 < 0
      || _M0L6_2atmpS1072 >= Moonbit_array_length(_M0L4dataS1071)
    ) {
      #line 134 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      moonbit_panic();
    }
    _M0L4dataS1071[_M0L6_2atmpS1072] = _M0L6_2atmpS1073;
    moonbit_decref(_M0L4dataS1071);
    _M0L3lenS1078 = _M0L4selfS106->$1;
    _M0L6_2atmpS1077 = _M0L3lenS1078 + 2;
    _M0L4selfS106->$1 = _M0L6_2atmpS1077;
  } else {
    #line 137 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_11.data);
  }
  return 0;
}

int32_t _M0MPB13StringBuilder4grow(
  struct _M0TPB13StringBuilder* _M0L4selfS101,
  int32_t _M0L8requiredS102
) {
  uint16_t* _M0L4dataS1049;
  int32_t _M0L6_2atmpS1047;
  int32_t _M0L3lenS1048;
  int32_t _M0L13new__capacityS100;
  uint16_t* _M0L4dataS1044;
  int32_t _M0L6_2atmpS1045;
  int32_t _M0L3lenS1046;
  uint16_t* _M0L9new__dataS103;
  uint16_t* _M0L6_2aoldS1907;
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L4dataS1049 = _M0L4selfS101->$0;
  _M0L6_2atmpS1047 = Moonbit_array_length(_M0L4dataS1049);
  _M0L3lenS1048 = _M0L4selfS101->$1;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L13new__capacityS100
  = _M0FPB31stringbuilder__growth__capacity(_M0L6_2atmpS1047, _M0L3lenS1048, _M0L8requiredS102);
  _M0L4dataS1044 = _M0L4selfS101->$0;
  moonbit_incref(_M0L4dataS1044);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L6_2atmpS1045 = _M0IPC16uint166UInt16PB7Default7default();
  _M0L3lenS1046 = _M0L4selfS101->$1;
  #line 80 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L9new__dataS103
  = _M0MPC15array10FixedArray23make__and__blit_2einnerGkE(_M0L4dataS1044, _M0L13new__capacityS100, _M0L6_2atmpS1045, _M0L3lenS1046, 0, 0);
  _M0L6_2aoldS1907 = _M0L4selfS101->$0;
  moonbit_decref(_M0L6_2aoldS1907);
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
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_12.data);
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
  int32_t _M0L6_2atmpS1043;
  #line 2785 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS1043 = *(int32_t*)&_M0L4selfS93;
  return (uint16_t)_M0L6_2atmpS1043;
}

uint32_t _M0MPC14char4Char8to__uint(int32_t _M0L4selfS92) {
  int32_t _M0L6_2atmpS1042;
  #line 1335 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS1042 = _M0L4selfS92;
  return *(uint32_t*)&_M0L6_2atmpS1042;
}

moonbit_string_t _M0MPB13StringBuilder10to__string(
  struct _M0TPB13StringBuilder* _M0L4selfS90
) {
  int32_t _M0L3lenS1033;
  #line 181 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L3lenS1033 = _M0L4selfS90->$1;
  if (_M0L3lenS1033 == 0) {
    return (moonbit_string_t)moonbit_string_literal_5.data;
  } else {
    int32_t _M0L3lenS1034 = _M0L4selfS90->$1;
    uint16_t* _M0L4dataS1036 = _M0L4selfS90->$0;
    int32_t _M0L6_2atmpS1035 = Moonbit_array_length(_M0L4dataS1036);
    if (_M0L3lenS1034 == _M0L6_2atmpS1035) {
      uint16_t* _M0L4dataS1037 = _M0L4selfS90->$0;
      moonbit_incref(_M0L4dataS1037);
      return _M0L4dataS1037;
    } else {
      uint16_t* _M0L4dataS1038 = _M0L4selfS90->$0;
      int32_t _M0L3lenS1039 = _M0L4selfS90->$1;
      int32_t _M0L6_2atmpS1040;
      int32_t _M0L3lenS1041;
      uint16_t* _M0L4dataS91;
      moonbit_incref(_M0L4dataS1038);
      #line 190 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0L6_2atmpS1040 = _M0IPC16uint166UInt16PB7Default7default();
      _M0L3lenS1041 = _M0L4selfS90->$1;
      #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0L4dataS91
      = _M0MPC15array10FixedArray23make__and__blit_2einnerGkE(_M0L4dataS1038, _M0L3lenS1039, _M0L6_2atmpS1040, _M0L3lenS1041, 0, 0);
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
  int32_t _if__result_2016;
  #line 97 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
  if (_M0L13allocate__lenS83 >= 0) {
    if (_M0L3lenS84 >= 0) {
      if (_M0L11src__offsetS85 >= 0) {
        if (_M0L11dst__offsetS86 >= 0) {
          int32_t _M0L6_2atmpS1029 = _M0L11src__offsetS85 + _M0L3lenS84;
          int32_t _M0L6_2atmpS1030 = Moonbit_array_length(_M0L3srcS87);
          if (_M0L6_2atmpS1029 <= _M0L6_2atmpS1030) {
            int32_t _M0L6_2atmpS1028 = _M0L11dst__offsetS86 + _M0L3lenS84;
            _if__result_2016 = _M0L6_2atmpS1028 <= _M0L13allocate__lenS83;
          } else {
            _if__result_2016 = 0;
          }
        } else {
          _if__result_2016 = 0;
        }
      } else {
        _if__result_2016 = 0;
      }
    } else {
      _if__result_2016 = 0;
    }
  } else {
    _if__result_2016 = 0;
  }
  if (_if__result_2016) {
    #line 116 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    return _M0MPC15array10FixedArray23unsafe__make__and__blitGkE(_M0L3srcS87, _M0L13allocate__lenS83, _M0L4initS88, _M0L11src__offsetS85, _M0L11dst__offsetS86, _M0L3lenS84);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS89;
    int32_t _M0L6_2atmpS1032;
    moonbit_string_t _M0L6_2atmpS1031;
    uint16_t* _result_2017;
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0L18_2astring__builderS89
    = _M0MPB13StringBuilder21StringBuilder_2einner(89);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS89, (moonbit_string_t)moonbit_string_literal_13.data);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS89, _M0L13allocate__lenS83);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS89, (moonbit_string_t)moonbit_string_literal_14.data);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS89, _M0L11src__offsetS85);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS89, (moonbit_string_t)moonbit_string_literal_15.data);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS89, _M0L11dst__offsetS86);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS89, (moonbit_string_t)moonbit_string_literal_16.data);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS89, _M0L3lenS84);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS89, (moonbit_string_t)moonbit_string_literal_17.data);
    _M0L6_2atmpS1032 = Moonbit_array_length(_M0L3srcS87);
    moonbit_decref(_M0L3srcS87);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS89, _M0L6_2atmpS1032);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0L6_2atmpS1031
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS89);
    moonbit_decref(_M0L18_2astring__builderS89);
    #line 112 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _result_2017 = _M0FPC15abort5abortGAkE(_M0L6_2atmpS1031);
    moonbit_decref(_M0L6_2atmpS1031);
    return _result_2017;
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
  struct _M0TPB13StringBuilder* _block_2018;
  #line 32 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  if (_M0L10size__hintS74 < 1) {
    _M0L7initialS73 = 1;
  } else {
    int32_t _M0L6_2atmpS1027 = _M0L10size__hintS74 + 1;
    _M0L7initialS73 = _M0L6_2atmpS1027 / 2;
  }
  _M0L4dataS75 = (uint16_t*)moonbit_make_string(_M0L7initialS73, 0);
  _block_2018
  = (struct _M0TPB13StringBuilder*)moonbit_malloc(sizeof(struct _M0TPB13StringBuilder));
  Moonbit_object_header(_block_2018)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 32, 0);
  _block_2018->$0 = _M0L4dataS75;
  _block_2018->$1 = 0;
  return _block_2018;
}

int32_t* _M0MPB18UninitializedArray23make__and__blit_2einnerGiE(
  int32_t* _M0L3srcS65,
  int32_t _M0L13allocate__lenS61,
  int32_t _M0L3lenS62,
  int32_t _M0L11src__offsetS63,
  int32_t _M0L11dst__offsetS64
) {
  int32_t _if__result_2019;
  #line 167 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  if (_M0L13allocate__lenS61 >= 0) {
    if (_M0L3lenS62 >= 0) {
      if (_M0L11src__offsetS63 >= 0) {
        if (_M0L11dst__offsetS64 >= 0) {
          int32_t _M0L6_2atmpS1018 = _M0L11src__offsetS63 + _M0L3lenS62;
          int32_t _M0L6_2atmpS1019;
          #line 179 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
          _M0L6_2atmpS1019
          = _M0MPB18UninitializedArray6lengthGiE(_M0L3srcS65);
          if (_M0L6_2atmpS1018 <= _M0L6_2atmpS1019) {
            int32_t _M0L6_2atmpS1017 = _M0L11dst__offsetS64 + _M0L3lenS62;
            _if__result_2019 = _M0L6_2atmpS1017 <= _M0L13allocate__lenS61;
          } else {
            _if__result_2019 = 0;
          }
        } else {
          _if__result_2019 = 0;
        }
      } else {
        _if__result_2019 = 0;
      }
    } else {
      _if__result_2019 = 0;
    }
  } else {
    _if__result_2019 = 0;
  }
  if (_if__result_2019) {
    #line 185 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    return _M0MPB18UninitializedArray23unsafe__make__and__blitGiE(_M0L3srcS65, _M0L13allocate__lenS61, _M0L11src__offsetS63, _M0L11dst__offsetS64, _M0L3lenS62);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS66;
    int32_t _M0L6_2atmpS1021;
    moonbit_string_t _M0L6_2atmpS1020;
    int32_t* _result_2020;
    #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L18_2astring__builderS66
    = _M0MPB13StringBuilder21StringBuilder_2einner(89);
    #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS66, (moonbit_string_t)moonbit_string_literal_13.data);
    #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS66, _M0L13allocate__lenS61);
    #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS66, (moonbit_string_t)moonbit_string_literal_14.data);
    #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS66, _M0L11src__offsetS63);
    #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS66, (moonbit_string_t)moonbit_string_literal_15.data);
    #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS66, _M0L11dst__offsetS64);
    #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS66, (moonbit_string_t)moonbit_string_literal_16.data);
    #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS66, _M0L3lenS62);
    #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS66, (moonbit_string_t)moonbit_string_literal_17.data);
    #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS1021 = _M0MPB18UninitializedArray6lengthGiE(_M0L3srcS65);
    moonbit_decref(_M0L3srcS65);
    #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS66, _M0L6_2atmpS1021);
    #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS1020
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS66);
    moonbit_decref(_M0L18_2astring__builderS66);
    #line 181 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _result_2020
    = _M0FPC15abort5abortGRPB18UninitializedArrayGiEE(_M0L6_2atmpS1020);
    moonbit_decref(_M0L6_2atmpS1020);
    return _result_2020;
  }
}

float* _M0MPB18UninitializedArray23make__and__blit_2einnerGfE(
  float* _M0L3srcS71,
  int32_t _M0L13allocate__lenS67,
  int32_t _M0L3lenS68,
  int32_t _M0L11src__offsetS69,
  int32_t _M0L11dst__offsetS70
) {
  int32_t _if__result_2021;
  #line 167 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  if (_M0L13allocate__lenS67 >= 0) {
    if (_M0L3lenS68 >= 0) {
      if (_M0L11src__offsetS69 >= 0) {
        if (_M0L11dst__offsetS70 >= 0) {
          int32_t _M0L6_2atmpS1023 = _M0L11src__offsetS69 + _M0L3lenS68;
          int32_t _M0L6_2atmpS1024;
          #line 179 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
          _M0L6_2atmpS1024
          = _M0MPB18UninitializedArray6lengthGfE(_M0L3srcS71);
          if (_M0L6_2atmpS1023 <= _M0L6_2atmpS1024) {
            int32_t _M0L6_2atmpS1022 = _M0L11dst__offsetS70 + _M0L3lenS68;
            _if__result_2021 = _M0L6_2atmpS1022 <= _M0L13allocate__lenS67;
          } else {
            _if__result_2021 = 0;
          }
        } else {
          _if__result_2021 = 0;
        }
      } else {
        _if__result_2021 = 0;
      }
    } else {
      _if__result_2021 = 0;
    }
  } else {
    _if__result_2021 = 0;
  }
  if (_if__result_2021) {
    #line 185 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    return _M0MPB18UninitializedArray23unsafe__make__and__blitGfE(_M0L3srcS71, _M0L13allocate__lenS67, _M0L11src__offsetS69, _M0L11dst__offsetS70, _M0L3lenS68);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS72;
    int32_t _M0L6_2atmpS1026;
    moonbit_string_t _M0L6_2atmpS1025;
    float* _result_2022;
    #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L18_2astring__builderS72
    = _M0MPB13StringBuilder21StringBuilder_2einner(89);
    #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS72, (moonbit_string_t)moonbit_string_literal_13.data);
    #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS72, _M0L13allocate__lenS67);
    #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS72, (moonbit_string_t)moonbit_string_literal_14.data);
    #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS72, _M0L11src__offsetS69);
    #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS72, (moonbit_string_t)moonbit_string_literal_15.data);
    #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS72, _M0L11dst__offsetS70);
    #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS72, (moonbit_string_t)moonbit_string_literal_16.data);
    #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS72, _M0L3lenS68);
    #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS72, (moonbit_string_t)moonbit_string_literal_17.data);
    #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS1026 = _M0MPB18UninitializedArray6lengthGfE(_M0L3srcS71);
    moonbit_decref(_M0L3srcS71);
    #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS72, _M0L6_2atmpS1026);
    #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS1025
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS72);
    moonbit_decref(_M0L18_2astring__builderS72);
    #line 181 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _result_2022
    = _M0FPC15abort5abortGRPB18UninitializedArrayGfEE(_M0L6_2atmpS1025);
    moonbit_decref(_M0L6_2atmpS1025);
    return _result_2022;
  }
}

int32_t _M0MPB13StringBuilder13write__objectGmE(
  struct _M0TPB13StringBuilder* _M0L4selfS58,
  uint64_t _M0L3objS57
) {
  struct _M0TPB6Logger _M0L6_2atmpS1015;
  #line 17 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  moonbit_incref(_M0L4selfS58);
  _M0L6_2atmpS1015
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS58
  };
  #line 23 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  _M0IP016_24default__implPB4Show6outputGmE(_M0L3objS57, _M0L6_2atmpS1015);
  if (_M0L6_2atmpS1015.$1) {
    moonbit_decref(_M0L6_2atmpS1015.$1);
  }
  return 0;
}

int32_t _M0MPB13StringBuilder13write__objectGiE(
  struct _M0TPB13StringBuilder* _M0L4selfS60,
  int32_t _M0L3objS59
) {
  struct _M0TPB6Logger _M0L6_2atmpS1016;
  #line 17 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  moonbit_incref(_M0L4selfS60);
  _M0L6_2atmpS1016
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS60
  };
  #line 23 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  _M0IP016_24default__implPB4Show6outputGiE(_M0L3objS59, _M0L6_2atmpS1016);
  if (_M0L6_2atmpS1016.$1) {
    moonbit_decref(_M0L6_2atmpS1016.$1);
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
        int32_t _M0L6_2atmpS988 = _M0L11dst__offsetS10 + _M0L1iS12;
        int32_t _M0L6_2atmpS990 = _M0L11src__offsetS11 + _M0L1iS12;
        int32_t _M0L6_2atmpS989;
        int32_t _M0L6_2atmpS991;
        if (
          _M0L6_2atmpS990 < 0
          || _M0L6_2atmpS990 >= Moonbit_array_length(_M0L3srcS9)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS989 = (int32_t)_M0L3srcS9[_M0L6_2atmpS990];
        if (
          _M0L6_2atmpS988 < 0
          || _M0L6_2atmpS988 >= Moonbit_array_length(_M0L3dstS8)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS8[_M0L6_2atmpS988] = _M0L6_2atmpS989;
        _M0L6_2atmpS991 = _M0L1iS12 + 1;
        _M0L1iS12 = _M0L6_2atmpS991;
        continue;
      } else {
        moonbit_decref(_M0L3srcS9);
        moonbit_decref(_M0L3dstS8);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS996 = _M0L3lenS13 - 1;
    int32_t _M0L1iS15 = _M0L6_2atmpS996;
    while (1) {
      if (_M0L1iS15 >= 0) {
        int32_t _M0L6_2atmpS992 = _M0L11dst__offsetS10 + _M0L1iS15;
        int32_t _M0L6_2atmpS994 = _M0L11src__offsetS11 + _M0L1iS15;
        int32_t _M0L6_2atmpS993;
        int32_t _M0L6_2atmpS995;
        if (
          _M0L6_2atmpS994 < 0
          || _M0L6_2atmpS994 >= Moonbit_array_length(_M0L3srcS9)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS993 = (int32_t)_M0L3srcS9[_M0L6_2atmpS994];
        if (
          _M0L6_2atmpS992 < 0
          || _M0L6_2atmpS992 >= Moonbit_array_length(_M0L3dstS8)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS8[_M0L6_2atmpS992] = _M0L6_2atmpS993;
        _M0L6_2atmpS995 = _M0L1iS15 - 1;
        _M0L1iS15 = _M0L6_2atmpS995;
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
        int32_t _M0L6_2atmpS997 = _M0L11dst__offsetS19 + _M0L1iS21;
        int32_t _M0L6_2atmpS999 = _M0L11src__offsetS20 + _M0L1iS21;
        float _M0L6_2atmpS998;
        int32_t _M0L6_2atmpS1000;
        if (
          _M0L6_2atmpS999 < 0
          || _M0L6_2atmpS999 >= Moonbit_array_length(_M0L3srcS18)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS998 = (float)_M0L3srcS18[_M0L6_2atmpS999];
        if (
          _M0L6_2atmpS997 < 0
          || _M0L6_2atmpS997 >= Moonbit_array_length(_M0L3dstS17)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS17[_M0L6_2atmpS997] = _M0L6_2atmpS998;
        _M0L6_2atmpS1000 = _M0L1iS21 + 1;
        _M0L1iS21 = _M0L6_2atmpS1000;
        continue;
      } else {
        moonbit_decref(_M0L3srcS18);
        moonbit_decref(_M0L3dstS17);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1005 = _M0L3lenS22 - 1;
    int32_t _M0L1iS24 = _M0L6_2atmpS1005;
    while (1) {
      if (_M0L1iS24 >= 0) {
        int32_t _M0L6_2atmpS1001 = _M0L11dst__offsetS19 + _M0L1iS24;
        int32_t _M0L6_2atmpS1003 = _M0L11src__offsetS20 + _M0L1iS24;
        float _M0L6_2atmpS1002;
        int32_t _M0L6_2atmpS1004;
        if (
          _M0L6_2atmpS1003 < 0
          || _M0L6_2atmpS1003 >= Moonbit_array_length(_M0L3srcS18)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1002 = (float)_M0L3srcS18[_M0L6_2atmpS1003];
        if (
          _M0L6_2atmpS1001 < 0
          || _M0L6_2atmpS1001 >= Moonbit_array_length(_M0L3dstS17)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS17[_M0L6_2atmpS1001] = _M0L6_2atmpS1002;
        _M0L6_2atmpS1004 = _M0L1iS24 - 1;
        _M0L1iS24 = _M0L6_2atmpS1004;
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
        int32_t _M0L6_2atmpS1006 = _M0L11dst__offsetS28 + _M0L1iS30;
        int32_t _M0L6_2atmpS1008 = _M0L11src__offsetS29 + _M0L1iS30;
        int32_t _M0L6_2atmpS1007;
        int32_t _M0L6_2atmpS1009;
        if (
          _M0L6_2atmpS1008 < 0
          || _M0L6_2atmpS1008 >= Moonbit_array_length(_M0L3srcS27)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1007 = (int32_t)_M0L3srcS27[_M0L6_2atmpS1008];
        if (
          _M0L6_2atmpS1006 < 0
          || _M0L6_2atmpS1006 >= Moonbit_array_length(_M0L3dstS26)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS26[_M0L6_2atmpS1006] = _M0L6_2atmpS1007;
        _M0L6_2atmpS1009 = _M0L1iS30 + 1;
        _M0L1iS30 = _M0L6_2atmpS1009;
        continue;
      } else {
        moonbit_decref(_M0L3srcS27);
        moonbit_decref(_M0L3dstS26);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1014 = _M0L3lenS31 - 1;
    int32_t _M0L1iS33 = _M0L6_2atmpS1014;
    while (1) {
      if (_M0L1iS33 >= 0) {
        int32_t _M0L6_2atmpS1010 = _M0L11dst__offsetS28 + _M0L1iS33;
        int32_t _M0L6_2atmpS1012 = _M0L11src__offsetS29 + _M0L1iS33;
        int32_t _M0L6_2atmpS1011;
        int32_t _M0L6_2atmpS1013;
        if (
          _M0L6_2atmpS1012 < 0
          || _M0L6_2atmpS1012 >= Moonbit_array_length(_M0L3srcS27)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1011 = (int32_t)_M0L3srcS27[_M0L6_2atmpS1012];
        if (
          _M0L6_2atmpS1010 < 0
          || _M0L6_2atmpS1010 >= Moonbit_array_length(_M0L3dstS26)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS26[_M0L6_2atmpS1010] = _M0L6_2atmpS1011;
        _M0L6_2atmpS1013 = _M0L1iS33 - 1;
        _M0L1iS33 = _M0L6_2atmpS1013;
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
  void* _M0L11_2aobj__ptrS935,
  struct _M0TPB4Show _M0L8_2aparamS934
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS933 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS935;
  _M0IP016_24default__implPB6Logger5writeGRPB13StringBuilderE(_M0L7_2aselfS933, _M0L8_2aparamS934);
  return 0;
}

int32_t _M0IP016_24default__implPB6Logger84write__string__interpolation_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLoggerGRPB13StringBuilderE(
  void* _M0L11_2aobj__ptrS932,
  struct _M0TPB4Show _M0L8_2aparamS931
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS930 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS932;
  _M0IP016_24default__implPB6Logger28write__string__interpolationGRPB13StringBuilderE(_M0L7_2aselfS930, _M0L8_2aparamS931);
  return 0;
}

int32_t _M0IPB13StringBuilderPB6Logger67write__char_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLogger(
  void* _M0L11_2aobj__ptrS929,
  int32_t _M0L8_2aparamS928
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS927 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS929;
  _M0IPB13StringBuilderPB6Logger11write__char(_M0L7_2aselfS927, _M0L8_2aparamS928);
  return 0;
}

int32_t _M0IPB13StringBuilderPB6Logger67write__view_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLogger(
  void* _M0L11_2aobj__ptrS926,
  struct _M0TPC16string10StringView _M0L8_2aparamS925
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS924 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS926;
  _M0IPB13StringBuilderPB6Logger11write__view(_M0L7_2aselfS924, _M0L8_2aparamS925);
  return 0;
}

int32_t _M0IP016_24default__implPB6Logger72write__substring_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLoggerGRPB13StringBuilderE(
  void* _M0L11_2aobj__ptrS923,
  moonbit_string_t _M0L8_2aparamS920,
  int32_t _M0L8_2aparamS921,
  int32_t _M0L8_2aparamS922
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS919 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS923;
  _M0IP016_24default__implPB6Logger16write__substringGRPB13StringBuilderE(_M0L7_2aselfS919, _M0L8_2aparamS920, _M0L8_2aparamS921, _M0L8_2aparamS922);
  return 0;
}

int32_t _M0IPB13StringBuilderPB6Logger69write__string_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLogger(
  void* _M0L11_2aobj__ptrS918,
  moonbit_string_t _M0L8_2aparamS917
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS916 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS918;
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L7_2aselfS916, _M0L8_2aparamS917);
  return 0;
}

void moonbit_init() {
  moonbit_layout_table = moonbit_layout_table_data;
  int64_t _tmp_2029 = 9218868437227405311ll;
  int64_t _tmp_2030;
  int64_t _tmp_2031;
  int64_t _tmp_2032;
  int64_t _tmp_2033;
  _M0FPB18double__max__value = *(double*)&_tmp_2029;
  _tmp_2030 = -4503599627370497ll;
  _M0FPB18double__min__value = *(double*)&_tmp_2030;
  _tmp_2031 = 9221120237041090561ll;
  _M0FPC16double14not__a__number = *(double*)&_tmp_2031;
  _tmp_2032 = -4503599627370496ll;
  _M0FPC16double13neg__infinity = *(double*)&_tmp_2032;
  _tmp_2033 = 4503599627370496ll;
  _M0FPC16double13min__positive = *(double*)&_tmp_2033;
}

int main(int argc, char** argv) {
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS866;
  int32_t _M0L2neS867;
  int32_t _M0L2niS868;
  struct _M0TP26RiantR8snn__mbt11IZParameter* _M0L8e__paramS869;
  struct _M0TP26RiantR8snn__mbt2IZ* _M0L6e__popS870;
  struct _M0TP26RiantR8snn__mbt11IZParameter* _M0L8i__paramS871;
  struct _M0TP26RiantR8snn__mbt2IZ* _M0L6i__popS872;
  struct _M0TP26RiantR8snn__mbt16SpikingSynapseIZ* _M0L2eeS873;
  struct _M0TP26RiantR8snn__mbt16SpikingSynapseIZ* _M0L2eiS874;
  struct _M0TP26RiantR8snn__mbt16SpikingSynapseIZ* _M0L2ieS875;
  struct _M0TP26RiantR8snn__mbt16SpikingSynapseIZ* _M0L2iiS876;
  moonbit_string_t _M0L6_2atmpS941;
  moonbit_string_t _M0L6_2atmpS940;
  moonbit_string_t _M0L6_2atmpS938;
  moonbit_string_t _M0L6_2atmpS939;
  moonbit_string_t _M0L6_2atmpS937;
  moonbit_string_t _M0L6_2atmpS936;
  struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS945;
  int32_t _M0L6_2atmpS944;
  moonbit_string_t _M0L6_2atmpS943;
  moonbit_string_t _M0L6_2atmpS942;
  struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS949;
  int32_t _M0L6_2atmpS948;
  moonbit_string_t _M0L6_2atmpS947;
  moonbit_string_t _M0L6_2atmpS946;
  struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS953;
  int32_t _M0L6_2atmpS952;
  moonbit_string_t _M0L6_2atmpS951;
  moonbit_string_t _M0L6_2atmpS950;
  struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS957;
  int32_t _M0L6_2atmpS956;
  moonbit_string_t _M0L6_2atmpS955;
  moonbit_string_t _M0L6_2atmpS954;
  float _M0L2dtS877;
  struct _M0TPB8MutLocalGiE* _M0L16total__e__spikesS878;
  struct _M0TPB8MutLocalGiE* _M0L16total__i__spikesS879;
  int32_t _M0L7_2abindS880;
  int32_t _M0L7_2abindS881;
  int32_t _M0L2__S882;
  struct _M0TPB5ArrayGfE* _M0L1vS977;
  int32_t _M0L6_2acntS1908;
  float _M0L6_2atmpS976;
  moonbit_string_t _M0L6_2atmpS975;
  moonbit_string_t _M0L6_2atmpS974;
  struct _M0TPB5ArrayGfE* _M0L1vS981;
  int32_t _M0L6_2acntS1916;
  float _M0L6_2atmpS980;
  moonbit_string_t _M0L6_2atmpS979;
  moonbit_string_t _M0L6_2atmpS978;
  int32_t _M0L3valS984;
  moonbit_string_t _M0L6_2atmpS983;
  moonbit_string_t _M0L6_2atmpS982;
  int32_t _M0L3valS987;
  moonbit_string_t _M0L6_2atmpS986;
  moonbit_string_t _M0L6_2atmpS985;
  moonbit_runtime_init(argc, argv);
  moonbit_init();
  #line 21 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\iz_net\\main.mbt"
  _M0L3rngS866 = _M0MP26RiantR8snn__mbt7Xoshiro7default();
  _M0L2neS867 = 16;
  _M0L2niS868 = 4;
  #line 25 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\iz_net\\main.mbt"
  _M0L8e__paramS869 = _M0MP26RiantR8snn__mbt11IZParameter2rs();
  #line 26 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\iz_net\\main.mbt"
  _M0L6e__popS870
  = _M0MP26RiantR8snn__mbt2IZ3new(_M0L2neS867, _M0L8e__paramS869, _M0L3rngS866);
  moonbit_decref(_M0L8e__paramS869);
  #line 27 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\iz_net\\main.mbt"
  _M0L8i__paramS871 = _M0MP26RiantR8snn__mbt11IZParameter2fs();
  #line 28 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\iz_net\\main.mbt"
  _M0L6i__popS872
  = _M0MP26RiantR8snn__mbt2IZ3new(_M0L2niS868, _M0L8i__paramS871, _M0L3rngS866);
  moonbit_decref(_M0L8i__paramS871);
  #line 31 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\iz_net\\main.mbt"
  _M0L2eeS873
  = _M0MP26RiantR8snn__mbt16SpikingSynapseIZ6random(_M0L6e__popS870, _M0L6e__popS870, (moonbit_string_t)moonbit_string_literal_0.data, 0x1.999999999999ap-5f, 0x0p+0f, 0x1.999999999999ap-1f, _M0L3rngS866);
  #line 32 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\iz_net\\main.mbt"
  _M0L2eiS874
  = _M0MP26RiantR8snn__mbt16SpikingSynapseIZ6random(_M0L6e__popS870, _M0L6i__popS872, (moonbit_string_t)moonbit_string_literal_0.data, 0x1p-1f, 0x0p+0f, 0x1.999999999999ap-1f, _M0L3rngS866);
  #line 33 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\iz_net\\main.mbt"
  _M0L2ieS875
  = _M0MP26RiantR8snn__mbt16SpikingSynapseIZ6random(_M0L6i__popS872, _M0L6e__popS870, (moonbit_string_t)moonbit_string_literal_18.data, 0x1p+0f, 0x0p+0f, 0x1.999999999999ap-1f, _M0L3rngS866);
  #line 34 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\iz_net\\main.mbt"
  _M0L2iiS876
  = _M0MP26RiantR8snn__mbt16SpikingSynapseIZ6random(_M0L6i__popS872, _M0L6i__popS872, (moonbit_string_t)moonbit_string_literal_18.data, 0x1p+0f, 0x0p+0f, 0x1.999999999999ap-1f, _M0L3rngS866);
  #line 36 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\iz_net\\main.mbt"
  _M0FPB7printlnGsE((moonbit_string_t)moonbit_string_literal_19.data);
  #line 37 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\iz_net\\main.mbt"
  _M0L6_2atmpS941 = _M0MPC13int3Int18to__string_2einner(_M0L2neS867, 10);
  #line 37 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\iz_net\\main.mbt"
  _M0L6_2atmpS940
  = moonbit_add_string((moonbit_string_t)moonbit_string_literal_20.data, _M0L6_2atmpS941);
  moonbit_decref(_M0L6_2atmpS941);
  #line 37 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\iz_net\\main.mbt"
  _M0L6_2atmpS938
  = moonbit_add_string(_M0L6_2atmpS940, (moonbit_string_t)moonbit_string_literal_21.data);
  moonbit_decref(_M0L6_2atmpS940);
  #line 37 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\iz_net\\main.mbt"
  _M0L6_2atmpS939 = _M0MPC13int3Int18to__string_2einner(_M0L2niS868, 10);
  #line 37 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\iz_net\\main.mbt"
  _M0L6_2atmpS937 = moonbit_add_string(_M0L6_2atmpS938, _M0L6_2atmpS939);
  moonbit_decref(_M0L6_2atmpS939);
  moonbit_decref(_M0L6_2atmpS938);
  #line 37 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\iz_net\\main.mbt"
  _M0L6_2atmpS936
  = moonbit_add_string(_M0L6_2atmpS937, (moonbit_string_t)moonbit_string_literal_22.data);
  moonbit_decref(_M0L6_2atmpS937);
  #line 37 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\iz_net\\main.mbt"
  _M0FPB7printlnGsE(_M0L6_2atmpS936);
  moonbit_decref(_M0L6_2atmpS936);
  _M0L6matrixS945 = _M0L2eeS873->$3;
  moonbit_incref(_M0L6matrixS945);
  #line 38 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\iz_net\\main.mbt"
  _M0L6_2atmpS944
  = _M0MP26RiantR8snn__mbt15SparseMatrixCSR3nnz(_M0L6matrixS945);
  moonbit_decref(_M0L6matrixS945);
  #line 38 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\iz_net\\main.mbt"
  _M0L6_2atmpS943 = _M0MPC13int3Int18to__string_2einner(_M0L6_2atmpS944, 10);
  #line 38 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\iz_net\\main.mbt"
  _M0L6_2atmpS942
  = moonbit_add_string((moonbit_string_t)moonbit_string_literal_23.data, _M0L6_2atmpS943);
  moonbit_decref(_M0L6_2atmpS943);
  #line 38 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\iz_net\\main.mbt"
  _M0FPB7printlnGsE(_M0L6_2atmpS942);
  moonbit_decref(_M0L6_2atmpS942);
  _M0L6matrixS949 = _M0L2eiS874->$3;
  moonbit_incref(_M0L6matrixS949);
  #line 39 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\iz_net\\main.mbt"
  _M0L6_2atmpS948
  = _M0MP26RiantR8snn__mbt15SparseMatrixCSR3nnz(_M0L6matrixS949);
  moonbit_decref(_M0L6matrixS949);
  #line 39 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\iz_net\\main.mbt"
  _M0L6_2atmpS947 = _M0MPC13int3Int18to__string_2einner(_M0L6_2atmpS948, 10);
  #line 39 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\iz_net\\main.mbt"
  _M0L6_2atmpS946
  = moonbit_add_string((moonbit_string_t)moonbit_string_literal_24.data, _M0L6_2atmpS947);
  moonbit_decref(_M0L6_2atmpS947);
  #line 39 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\iz_net\\main.mbt"
  _M0FPB7printlnGsE(_M0L6_2atmpS946);
  moonbit_decref(_M0L6_2atmpS946);
  _M0L6matrixS953 = _M0L2ieS875->$3;
  moonbit_incref(_M0L6matrixS953);
  #line 40 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\iz_net\\main.mbt"
  _M0L6_2atmpS952
  = _M0MP26RiantR8snn__mbt15SparseMatrixCSR3nnz(_M0L6matrixS953);
  moonbit_decref(_M0L6matrixS953);
  #line 40 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\iz_net\\main.mbt"
  _M0L6_2atmpS951 = _M0MPC13int3Int18to__string_2einner(_M0L6_2atmpS952, 10);
  #line 40 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\iz_net\\main.mbt"
  _M0L6_2atmpS950
  = moonbit_add_string((moonbit_string_t)moonbit_string_literal_25.data, _M0L6_2atmpS951);
  moonbit_decref(_M0L6_2atmpS951);
  #line 40 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\iz_net\\main.mbt"
  _M0FPB7printlnGsE(_M0L6_2atmpS950);
  moonbit_decref(_M0L6_2atmpS950);
  _M0L6matrixS957 = _M0L2iiS876->$3;
  moonbit_incref(_M0L6matrixS957);
  #line 41 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\iz_net\\main.mbt"
  _M0L6_2atmpS956
  = _M0MP26RiantR8snn__mbt15SparseMatrixCSR3nnz(_M0L6matrixS957);
  moonbit_decref(_M0L6matrixS957);
  #line 41 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\iz_net\\main.mbt"
  _M0L6_2atmpS955 = _M0MPC13int3Int18to__string_2einner(_M0L6_2atmpS956, 10);
  #line 41 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\iz_net\\main.mbt"
  _M0L6_2atmpS954
  = moonbit_add_string((moonbit_string_t)moonbit_string_literal_26.data, _M0L6_2atmpS955);
  moonbit_decref(_M0L6_2atmpS955);
  #line 41 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\iz_net\\main.mbt"
  _M0FPB7printlnGsE(_M0L6_2atmpS954);
  moonbit_decref(_M0L6_2atmpS954);
  _M0L2dtS877 = 0x1p-3f;
  _M0L16total__e__spikesS878
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L16total__e__spikesS878)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L16total__e__spikesS878->$0 = 0;
  _M0L16total__i__spikesS879
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L16total__i__spikesS879)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L16total__i__spikesS879->$0 = 0;
  _M0L7_2abindS880 = 0;
  _M0L7_2abindS881 = 1000;
  _M0L2__S882 = _M0L7_2abindS880;
  while (1) {
    if (_M0L2__S882 < _M0L7_2abindS881) {
      int32_t _M0L7_2abindS883 = 0;
      int32_t _M0L1kS884 = _M0L7_2abindS883;
      int32_t _M0L7_2abindS890;
      int32_t _M0L1kS891;
      int32_t _M0L7_2abindS897;
      int32_t _M0L7_2abindS898;
      int32_t _M0L2__S899;
      struct _M0TPB5ArrayGbE* _M0L7_2abindS901;
      int32_t _M0L7_2abindS902;
      uint8_t* _M0L7_2abindS903;
      int32_t _M0L2__S904;
      struct _M0TPB5ArrayGbE* _M0L7_2abindS907;
      int32_t _M0L7_2abindS908;
      uint8_t* _M0L7_2abindS909;
      int32_t _M0L2__S910;
      int32_t _M0L6_2atmpS973;
      while (1) {
        if (_M0L1kS884 < _M0L2neS867) {
          double _M0L2z1S886;
          struct _M0TUddE* _M0L7_2abindS887;
          double _M0L5_2az1S888;
          struct _M0TPB5ArrayGfE* _M0L1iS958;
          float _M0L6_2atmpS960;
          float _M0L6_2atmpS959;
          int32_t _M0L6_2atmpS961;
          #line 50 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\iz_net\\main.mbt"
          _M0L7_2abindS887
          = _M0FP26RiantR8snn__mbt11box__muller(_M0L3rngS866);
          _M0L5_2az1S888 = _M0L7_2abindS887->$0;
          moonbit_decref(_M0L7_2abindS887);
          _M0L2z1S886 = _M0L5_2az1S888;
          goto join_885;
          goto joinlet_2036;
          join_885:;
          _M0L1iS958 = _M0L6e__popS870->$5;
          _M0L6_2atmpS960 = (float)_M0L2z1S886;
          _M0L6_2atmpS959 = 0x1.4p+2f * _M0L6_2atmpS960;
          moonbit_incref(_M0L1iS958);
          #line 51 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\iz_net\\main.mbt"
          _M0MPC15array5Array3setGfE(_M0L1iS958, _M0L1kS884, _M0L6_2atmpS959);
          moonbit_decref(_M0L1iS958);
          joinlet_2036:;
          _M0L6_2atmpS961 = _M0L1kS884 + 1;
          _M0L1kS884 = _M0L6_2atmpS961;
          continue;
        }
        break;
      }
      _M0L7_2abindS890 = 0;
      _M0L1kS891 = _M0L7_2abindS890;
      while (1) {
        if (_M0L1kS891 < _M0L2niS868) {
          double _M0L2z1S893;
          struct _M0TUddE* _M0L7_2abindS894;
          double _M0L5_2az1S895;
          struct _M0TPB5ArrayGfE* _M0L1iS962;
          float _M0L6_2atmpS964;
          float _M0L6_2atmpS963;
          int32_t _M0L6_2atmpS965;
          #line 54 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\iz_net\\main.mbt"
          _M0L7_2abindS894
          = _M0FP26RiantR8snn__mbt11box__muller(_M0L3rngS866);
          _M0L5_2az1S895 = _M0L7_2abindS894->$0;
          moonbit_decref(_M0L7_2abindS894);
          _M0L2z1S893 = _M0L5_2az1S895;
          goto join_892;
          goto joinlet_2038;
          join_892:;
          _M0L1iS962 = _M0L6i__popS872->$5;
          _M0L6_2atmpS964 = (float)_M0L2z1S893;
          _M0L6_2atmpS963 = 0x1p+1f * _M0L6_2atmpS964;
          moonbit_incref(_M0L1iS962);
          #line 55 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\iz_net\\main.mbt"
          _M0MPC15array5Array3setGfE(_M0L1iS962, _M0L1kS891, _M0L6_2atmpS963);
          moonbit_decref(_M0L1iS962);
          joinlet_2038:;
          _M0L6_2atmpS965 = _M0L1kS891 + 1;
          _M0L1kS891 = _M0L6_2atmpS965;
          continue;
        }
        break;
      }
      _M0L7_2abindS897 = 0;
      _M0L7_2abindS898 = 8;
      _M0L2__S899 = _M0L7_2abindS897;
      while (1) {
        if (_M0L2__S899 < _M0L7_2abindS898) {
          int32_t _M0L6_2atmpS966;
          #line 59 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\iz_net\\main.mbt"
          _M0FP26RiantR8snn__mbt20forward__iz__synapse(_M0L2eeS873);
          #line 60 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\iz_net\\main.mbt"
          _M0FP26RiantR8snn__mbt20forward__iz__synapse(_M0L2eiS874);
          #line 61 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\iz_net\\main.mbt"
          _M0FP26RiantR8snn__mbt20forward__iz__synapse(_M0L2ieS875);
          #line 62 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\iz_net\\main.mbt"
          _M0FP26RiantR8snn__mbt20forward__iz__synapse(_M0L2iiS876);
          #line 63 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\iz_net\\main.mbt"
          _M0FP26RiantR8snn__mbt8step__iz(_M0L6e__popS870, _M0L2dtS877);
          #line 64 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\iz_net\\main.mbt"
          _M0FP26RiantR8snn__mbt8step__iz(_M0L6i__popS872, _M0L2dtS877);
          _M0L6_2atmpS966 = _M0L2__S899 + 1;
          _M0L2__S899 = _M0L6_2atmpS966;
          continue;
        }
        break;
      }
      _M0L7_2abindS901 = _M0L6e__popS870->$4;
      _M0L7_2abindS902 = _M0L7_2abindS901->$1;
      _M0L7_2abindS903 = _M0L7_2abindS901->$0;
      moonbit_incref(_M0L7_2abindS903);
      _M0L2__S904 = 0;
      while (1) {
        if (_M0L2__S904 < _M0L7_2abindS902) {
          int32_t _M0L1fS905 = (int32_t)_M0L7_2abindS903[_M0L2__S904];
          int32_t _M0L6_2atmpS969;
          if (_M0L1fS905) {
            int32_t _M0L3valS968 = _M0L16total__e__spikesS878->$0;
            int32_t _M0L6_2atmpS967 = _M0L3valS968 + 1;
            _M0L16total__e__spikesS878->$0 = _M0L6_2atmpS967;
          }
          _M0L6_2atmpS969 = _M0L2__S904 + 1;
          _M0L2__S904 = _M0L6_2atmpS969;
          continue;
        } else {
          moonbit_decref(_M0L7_2abindS903);
        }
        break;
      }
      _M0L7_2abindS907 = _M0L6i__popS872->$4;
      _M0L7_2abindS908 = _M0L7_2abindS907->$1;
      _M0L7_2abindS909 = _M0L7_2abindS907->$0;
      moonbit_incref(_M0L7_2abindS909);
      _M0L2__S910 = 0;
      while (1) {
        if (_M0L2__S910 < _M0L7_2abindS908) {
          int32_t _M0L1fS911 = (int32_t)_M0L7_2abindS909[_M0L2__S910];
          int32_t _M0L6_2atmpS972;
          if (_M0L1fS911) {
            int32_t _M0L3valS971 = _M0L16total__i__spikesS879->$0;
            int32_t _M0L6_2atmpS970 = _M0L3valS971 + 1;
            _M0L16total__i__spikesS879->$0 = _M0L6_2atmpS970;
          }
          _M0L6_2atmpS972 = _M0L2__S910 + 1;
          _M0L2__S910 = _M0L6_2atmpS972;
          continue;
        } else {
          moonbit_decref(_M0L7_2abindS909);
        }
        break;
      }
      _M0L6_2atmpS973 = _M0L2__S882 + 1;
      _M0L2__S882 = _M0L6_2atmpS973;
      continue;
    } else {
      moonbit_decref(_M0L2iiS876);
      moonbit_decref(_M0L2ieS875);
      moonbit_decref(_M0L2eiS874);
      moonbit_decref(_M0L2eeS873);
      moonbit_decref(_M0L3rngS866);
    }
    break;
  }
  #line 74 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\iz_net\\main.mbt"
  _M0FPB7printlnGsE((moonbit_string_t)moonbit_string_literal_27.data);
  _M0L1vS977 = _M0L6e__popS870->$2;
  _M0L6_2acntS1908 = Moonbit_rc_count(Moonbit_object_header(_M0L6e__popS870));
  if (_M0L6_2acntS1908 > 1) {
    int32_t _M0L11_2anew__cntS1915 = _M0L6_2acntS1908 - 1;
    Moonbit_set_rc_count(Moonbit_object_header(_M0L6e__popS870), _M0L11_2anew__cntS1915);
    moonbit_incref(_M0L1vS977);
  } else if (_M0L6_2acntS1908 == 1) {
    struct _M0TPB5ArrayGfE* _M0L8_2afieldS1914 = _M0L6e__popS870->$7;
    struct _M0TPB5ArrayGfE* _M0L8_2afieldS1913;
    struct _M0TPB5ArrayGfE* _M0L8_2afieldS1912;
    struct _M0TPB5ArrayGbE* _M0L8_2afieldS1911;
    struct _M0TPB5ArrayGfE* _M0L8_2afieldS1910;
    struct _M0TP26RiantR8snn__mbt11IZParameter* _M0L8_2afieldS1909;
    moonbit_decref(_M0L8_2afieldS1914);
    _M0L8_2afieldS1913 = _M0L6e__popS870->$6;
    moonbit_decref(_M0L8_2afieldS1913);
    _M0L8_2afieldS1912 = _M0L6e__popS870->$5;
    moonbit_decref(_M0L8_2afieldS1912);
    _M0L8_2afieldS1911 = _M0L6e__popS870->$4;
    moonbit_decref(_M0L8_2afieldS1911);
    _M0L8_2afieldS1910 = _M0L6e__popS870->$3;
    moonbit_decref(_M0L8_2afieldS1910);
    _M0L8_2afieldS1909 = _M0L6e__popS870->$0;
    moonbit_decref(_M0L8_2afieldS1909);
    #line 46 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\iz_net\\main.mbt"
    moonbit_free(_M0L6e__popS870);
  }
  #line 75 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\iz_net\\main.mbt"
  _M0L6_2atmpS976 = _M0MPC15array5Array2atGfE(_M0L1vS977, 0);
  moonbit_decref(_M0L1vS977);
  #line 75 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\iz_net\\main.mbt"
  _M0L6_2atmpS975 = _M0IPC15float5FloatPB4Show10to__string(_M0L6_2atmpS976);
  #line 75 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\iz_net\\main.mbt"
  _M0L6_2atmpS974
  = moonbit_add_string((moonbit_string_t)moonbit_string_literal_28.data, _M0L6_2atmpS975);
  moonbit_decref(_M0L6_2atmpS975);
  #line 75 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\iz_net\\main.mbt"
  _M0FPB7printlnGsE(_M0L6_2atmpS974);
  moonbit_decref(_M0L6_2atmpS974);
  _M0L1vS981 = _M0L6i__popS872->$2;
  _M0L6_2acntS1916 = Moonbit_rc_count(Moonbit_object_header(_M0L6i__popS872));
  if (_M0L6_2acntS1916 > 1) {
    int32_t _M0L11_2anew__cntS1923 = _M0L6_2acntS1916 - 1;
    Moonbit_set_rc_count(Moonbit_object_header(_M0L6i__popS872), _M0L11_2anew__cntS1923);
    moonbit_incref(_M0L1vS981);
  } else if (_M0L6_2acntS1916 == 1) {
    struct _M0TPB5ArrayGfE* _M0L8_2afieldS1922 = _M0L6i__popS872->$7;
    struct _M0TPB5ArrayGfE* _M0L8_2afieldS1921;
    struct _M0TPB5ArrayGfE* _M0L8_2afieldS1920;
    struct _M0TPB5ArrayGbE* _M0L8_2afieldS1919;
    struct _M0TPB5ArrayGfE* _M0L8_2afieldS1918;
    struct _M0TP26RiantR8snn__mbt11IZParameter* _M0L8_2afieldS1917;
    moonbit_decref(_M0L8_2afieldS1922);
    _M0L8_2afieldS1921 = _M0L6i__popS872->$6;
    moonbit_decref(_M0L8_2afieldS1921);
    _M0L8_2afieldS1920 = _M0L6i__popS872->$5;
    moonbit_decref(_M0L8_2afieldS1920);
    _M0L8_2afieldS1919 = _M0L6i__popS872->$4;
    moonbit_decref(_M0L8_2afieldS1919);
    _M0L8_2afieldS1918 = _M0L6i__popS872->$3;
    moonbit_decref(_M0L8_2afieldS1918);
    _M0L8_2afieldS1917 = _M0L6i__popS872->$0;
    moonbit_decref(_M0L8_2afieldS1917);
    #line 46 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\iz_net\\main.mbt"
    moonbit_free(_M0L6i__popS872);
  }
  #line 76 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\iz_net\\main.mbt"
  _M0L6_2atmpS980 = _M0MPC15array5Array2atGfE(_M0L1vS981, 0);
  moonbit_decref(_M0L1vS981);
  #line 76 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\iz_net\\main.mbt"
  _M0L6_2atmpS979 = _M0IPC15float5FloatPB4Show10to__string(_M0L6_2atmpS980);
  #line 76 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\iz_net\\main.mbt"
  _M0L6_2atmpS978
  = moonbit_add_string((moonbit_string_t)moonbit_string_literal_29.data, _M0L6_2atmpS979);
  moonbit_decref(_M0L6_2atmpS979);
  #line 76 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\iz_net\\main.mbt"
  _M0FPB7printlnGsE(_M0L6_2atmpS978);
  moonbit_decref(_M0L6_2atmpS978);
  _M0L3valS984 = _M0L16total__e__spikesS878->$0;
  moonbit_decref(_M0L16total__e__spikesS878);
  #line 77 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\iz_net\\main.mbt"
  _M0L6_2atmpS983 = _M0MPC13int3Int18to__string_2einner(_M0L3valS984, 10);
  #line 77 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\iz_net\\main.mbt"
  _M0L6_2atmpS982
  = moonbit_add_string((moonbit_string_t)moonbit_string_literal_30.data, _M0L6_2atmpS983);
  moonbit_decref(_M0L6_2atmpS983);
  #line 77 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\iz_net\\main.mbt"
  _M0FPB7printlnGsE(_M0L6_2atmpS982);
  moonbit_decref(_M0L6_2atmpS982);
  _M0L3valS987 = _M0L16total__i__spikesS879->$0;
  moonbit_decref(_M0L16total__i__spikesS879);
  #line 78 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\iz_net\\main.mbt"
  _M0L6_2atmpS986 = _M0MPC13int3Int18to__string_2einner(_M0L3valS987, 10);
  #line 78 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\iz_net\\main.mbt"
  _M0L6_2atmpS985
  = moonbit_add_string((moonbit_string_t)moonbit_string_literal_31.data, _M0L6_2atmpS986);
  moonbit_decref(_M0L6_2atmpS986);
  #line 78 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\iz_net\\main.mbt"
  _M0FPB7printlnGsE(_M0L6_2atmpS985);
  moonbit_decref(_M0L6_2atmpS985);
  return 0;
}