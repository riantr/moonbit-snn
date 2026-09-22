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

struct _M0TPB8MutLocalGiE;

struct _M0TPC16string10StringView;

struct _M0KTPB6LoggerTPB13StringBuilder;

struct _M0TPB6Logger;

struct _M0TPB4Show;

struct _M0TP26RiantR8snn__mbt7Xoshiro;

struct _M0TPB13StringBuilder;

struct _M0TPB5ArrayGfE;

struct _M0TPB17FloatingDecimal64;

struct _M0TUmmmmE;

struct _M0TP26RiantR8snn__mbt2IZ;

struct _M0TPB19MulShiftAll64Result;

struct _M0TP26RiantR8snn__mbt11IZParameter;

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

struct _M0TPB4Show {
  struct _M0BTPB4Show* $0;
  void* $1;
  
};

struct _M0TP26RiantR8snn__mbt7Xoshiro {
  uint64_t $0;
  uint64_t $1;
  uint64_t $2;
  uint64_t $3;
  
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

struct _M0TPB19MulShiftAll64Result {
  uint64_t $0;
  uint64_t $1;
  uint64_t $2;
  
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

int32_t _M0FP46RiantR8snn__mbt8examples10izhikevich15run__izhikevich(
  struct _M0TP26RiantR8snn__mbt2IZ*,
  int32_t,
  float
);

struct _M0TP26RiantR8snn__mbt2IZ* _M0MP26RiantR8snn__mbt2IZ3new(
  int32_t,
  struct _M0TP26RiantR8snn__mbt11IZParameter*,
  struct _M0TP26RiantR8snn__mbt7Xoshiro*
);

struct _M0TP26RiantR8snn__mbt11IZParameter* _M0MP26RiantR8snn__mbt11IZParameter2rs(
  
);

int32_t _M0FP26RiantR8snn__mbt8step__iz(
  struct _M0TP26RiantR8snn__mbt2IZ*,
  float
);

struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0MP26RiantR8snn__mbt7Xoshiro7default(
  
);

struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0MP26RiantR8snn__mbt7Xoshiro3new(
  uint64_t
);

struct _M0TUmmmmE* _M0FP26RiantR8snn__mbt10splitmix64(uint64_t);

uint64_t _M0FP26RiantR8snn__mbt5mix64(uint64_t);

moonbit_string_t _M0IPC15float5FloatPB4Show10to__string(float);

int32_t _M0MPC15float5Float7to__int(float);

struct _M0TPB5ArrayGfE* _M0MPC15array5Array4makeGfE(int32_t, float);

struct _M0TPB5ArrayGbE* _M0MPC15array5Array4makeGbE(int32_t, int32_t);

int32_t _M0MPC15array5Array3setGfE(struct _M0TPB5ArrayGfE*, int32_t, float);

int32_t _M0MPC15array5Array3setGbE(struct _M0TPB5ArrayGbE*, int32_t, int32_t);

float _M0MPC15array5Array2atGfE(struct _M0TPB5ArrayGfE*, int32_t);

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

int32_t _M0MPB13StringBuilder13write__objectGmE(
  struct _M0TPB13StringBuilder*,
  uint64_t
);

int32_t _M0MPB13StringBuilder13write__objectGiE(
  struct _M0TPB13StringBuilder*,
  int32_t
);

int32_t _M0MPC15array10FixedArray12unsafe__blitGkE(
  uint16_t*,
  int32_t,
  uint16_t*,
  int32_t,
  int32_t
);

int32_t _M0FPC15abort5abortGuE(moonbit_string_t);

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

struct { int32_t rc; uint32_t meta; uint16_t const data[37]; 
} const moonbit_string_literal_18 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 36, 105, 122, 
    104, 105, 107, 101, 118, 105, 99, 104, 46, 109, 98, 116, 58, 32, 
    115, 105, 109, 117, 108, 97, 116, 105, 111, 110, 32, 100, 111, 110, 
    101, 32, 40, 50, 115, 41, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[1]; 
} const moonbit_string_literal_4 =
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
} const moonbit_string_literal_3 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 1, 45, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[10]; 
} const moonbit_string_literal_20 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 9, 32, 32, 
    117, 91, 48, 93, 32, 61, 32, 0
  };

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

struct { int32_t rc; uint32_t meta; uint16_t const data[2]; 
} const moonbit_string_literal_0 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 1, 48, 0};

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

struct { int32_t rc; uint32_t meta; uint16_t const data[11]; 
} const moonbit_string_literal_22 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 10, 32, 32, 
    103, 105, 91, 48, 93, 32, 61, 32, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[13]; 
} const moonbit_string_literal_23 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 12, 32, 32, 
    102, 105, 114, 101, 91, 48, 93, 32, 61, 32, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[5]; 
} const moonbit_string_literal_7 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 4, 116, 114, 
    117, 101, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[23]; 
} const moonbit_string_literal_24 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 22, 32, 32, 
    116, 111, 116, 97, 108, 32, 115, 112, 105, 107, 101, 115, 32, 105, 
    110, 32, 50, 115, 58, 32, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[37]; 
} const moonbit_string_literal_10 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 36, 48, 49, 
    50, 51, 52, 53, 54, 55, 56, 57, 97, 98, 99, 100, 101, 102, 103, 104, 
    105, 106, 107, 108, 109, 110, 111, 112, 113, 114, 115, 116, 117, 
    118, 119, 120, 121, 122, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[16]; 
} const moonbit_string_literal_17 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 15, 44, 32, 
    115, 114, 99, 46, 108, 101, 110, 103, 116, 104, 32, 61, 32, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[16]; 
} const moonbit_string_literal_14 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 15, 44, 32, 
    115, 114, 99, 95, 111, 102, 102, 115, 101, 116, 32, 61, 32, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[11]; 
} const moonbit_string_literal_21 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 10, 32, 32, 
    103, 101, 91, 48, 93, 32, 61, 32, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[26]; 
} const moonbit_string_literal_1 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 25, 73, 108, 
    108, 101, 103, 97, 108, 65, 114, 103, 117, 109, 101, 110, 116, 69, 
    120, 99, 101, 112, 116, 105, 111, 110, 32, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[10]; 
} const moonbit_string_literal_19 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 9, 32, 32, 
    118, 91, 48, 93, 32, 61, 32, 0
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

uint32_t const moonbit_layout_table_data[18] =
  {
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

int32_t _M0FP46RiantR8snn__mbt8examples10izhikevich15run__izhikevich(
  struct _M0TP26RiantR8snn__mbt2IZ* _M0L3popS587,
  int32_t _M0L12total__stepsS585,
  float _M0L2dtS588
) {
  struct _M0TPB8MutLocalGiE* _M0L6spikesS582;
  int32_t _M0L7_2abindS583;
  int32_t _M0L2__S584;
  int32_t _result_1384;
  #line 30 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\izhikevich\\main.mbt"
  _M0L6spikesS582
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L6spikesS582)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L6spikesS582->$0 = 0;
  _M0L7_2abindS583 = 0;
  _M0L2__S584 = _M0L7_2abindS583;
  while (1) {
    if (_M0L2__S584 < _M0L12total__stepsS585) {
      struct _M0TPB5ArrayGbE* _M0L4fireS1367 = _M0L3popS587->$4;
      int32_t _M0L10prev__fireS586;
      struct _M0TPB5ArrayGbE* _M0L4fireS1364;
      int32_t _if__result_1383;
      int32_t _M0L6_2atmpS1368;
      #line 33 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\izhikevich\\main.mbt"
      _M0L10prev__fireS586 = _M0MPC15array5Array2atGbE(_M0L4fireS1367, 0);
      #line 34 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\izhikevich\\main.mbt"
      _M0FP26RiantR8snn__mbt8step__iz(_M0L3popS587, _M0L2dtS588);
      _M0L4fireS1364 = _M0L3popS587->$4;
      #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\izhikevich\\main.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS1364, 0)) {
        _if__result_1383 = !_M0L10prev__fireS586;
      } else {
        _if__result_1383 = 0;
      }
      if (_if__result_1383) {
        int32_t _M0L3valS1366 = _M0L6spikesS582->$0;
        int32_t _M0L6_2atmpS1365 = _M0L3valS1366 + 1;
        _M0L6spikesS582->$0 = _M0L6_2atmpS1365;
      }
      _M0L6_2atmpS1368 = _M0L2__S584 + 1;
      _M0L2__S584 = _M0L6_2atmpS1368;
      continue;
    }
    break;
  }
  _result_1384 = _M0L6spikesS582->$0;
  moonbit_decref(_M0L6spikesS582);
  return _result_1384;
}

struct _M0TP26RiantR8snn__mbt2IZ* _M0MP26RiantR8snn__mbt2IZ3new(
  int32_t _M0L1nS571,
  struct _M0TP26RiantR8snn__mbt11IZParameter* _M0L5paramS575,
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS581
) {
  struct _M0TPB5ArrayGfE* _M0L1vS570;
  struct _M0TPB5ArrayGfE* _M0L1uS572;
  int32_t _M0L7_2abindS573;
  int32_t _M0L1kS574;
  struct _M0TPB5ArrayGbE* _M0L4fireS577;
  struct _M0TPB5ArrayGfE* _M0L1iS578;
  struct _M0TPB5ArrayGfE* _M0L2geS579;
  struct _M0TPB5ArrayGfE* _M0L2giS580;
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L6_2atmpS1369;
  struct _M0TP26RiantR8snn__mbt2IZ* _block_1386;
  #line 83 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  #line 85 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  _M0L1vS570 = _M0MPC15array5Array4makeGfE(_M0L1nS571, -0x1.04p+6f);
  #line 87 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  _M0L1uS572 = _M0MPC15array5Array4makeGfE(_M0L1nS571, 0x0p+0f);
  _M0L7_2abindS573 = 0;
  _M0L1kS574 = _M0L7_2abindS573;
  while (1) {
    if (_M0L1kS574 < _M0L1nS571) {
      float _M0L1bS1361 = _M0L5paramS575->$1;
      float _M0L6_2atmpS1362;
      float _M0L6_2atmpS1360;
      int32_t _M0L6_2atmpS1363;
      #line 89 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L6_2atmpS1362 = _M0MPC15array5Array2atGfE(_M0L1vS570, _M0L1kS574);
      _M0L6_2atmpS1360 = _M0L1bS1361 * _M0L6_2atmpS1362;
      #line 89 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0MPC15array5Array3setGfE(_M0L1uS572, _M0L1kS574, _M0L6_2atmpS1360);
      _M0L6_2atmpS1363 = _M0L1kS574 + 1;
      _M0L1kS574 = _M0L6_2atmpS1363;
      continue;
    }
    break;
  }
  #line 91 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  _M0L4fireS577 = _M0MPC15array5Array4makeGbE(_M0L1nS571, 0);
  #line 92 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  _M0L1iS578 = _M0MPC15array5Array4makeGfE(_M0L1nS571, 0x0p+0f);
  #line 106 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  _M0L2geS579 = _M0MPC15array5Array4makeGfE(_M0L1nS571, 0x0p+0f);
  #line 107 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  _M0L2giS580 = _M0MPC15array5Array4makeGfE(_M0L1nS571, 0x0p+0f);
  _M0L6_2atmpS1369 = _M0L3rngS581;
  moonbit_incref(_M0L5paramS575);
  _block_1386
  = (struct _M0TP26RiantR8snn__mbt2IZ*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt2IZ));
  Moonbit_object_header(_block_1386)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 0, 0);
  _block_1386->$0 = _M0L5paramS575;
  _block_1386->$1 = _M0L1nS571;
  _block_1386->$2 = _M0L1vS570;
  _block_1386->$3 = _M0L1uS572;
  _block_1386->$4 = _M0L4fireS577;
  _block_1386->$5 = _M0L1iS578;
  _block_1386->$6 = _M0L2geS579;
  _block_1386->$7 = _M0L2giS580;
  return _block_1386;
}

struct _M0TP26RiantR8snn__mbt11IZParameter* _M0MP26RiantR8snn__mbt11IZParameter2rs(
  
) {
  struct _M0TP26RiantR8snn__mbt11IZParameter* _block_1387;
  #line 55 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  _block_1387
  = (struct _M0TP26RiantR8snn__mbt11IZParameter*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt11IZParameter));
  Moonbit_object_header(_block_1387)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_1387->$0 = 0x1.47ae147ae147bp-6f;
  _block_1387->$1 = 0x1.999999999999ap-3f;
  _block_1387->$2 = -0x1.04p+6f;
  _block_1387->$3 = 0x1p+3f;
  _block_1387->$4 = 0x1.4p+2f;
  _block_1387->$5 = 0x1.4p+3f;
  _block_1387->$6 = 0x0p+0f;
  _block_1387->$7 = -0x1.4p+6f;
  return _block_1387;
}

int32_t _M0FP26RiantR8snn__mbt8step__iz(
  struct _M0TP26RiantR8snn__mbt2IZ* _M0L1pS539,
  float _M0L2dtS551
) {
  int32_t _M0L1nS538;
  struct _M0TP26RiantR8snn__mbt11IZParameter* _M0L3p__S540;
  float _M0L1aS541;
  float _M0L1bS542;
  float _M0L1cS543;
  float _M0L1dS544;
  float _M0L6tau__eS545;
  float _M0L6tau__iS546;
  float _M0L4e__eS547;
  float _M0L4e__iS548;
  int32_t _M0L7_2abindS549;
  int32_t _M0L1iS550;
  int32_t _M0L7_2abindS553;
  int32_t _M0L1iS554;
  int32_t _M0L7_2abindS560;
  int32_t _M0L1iS561;
  int32_t _M0L7_2abindS564;
  int32_t _M0L1iS565;
  int32_t _M0L7_2abindS567;
  int32_t _M0L1iS568;
  #line 139 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  _M0L1nS538 = _M0L1pS539->$1;
  _M0L3p__S540 = _M0L1pS539->$0;
  _M0L1aS541 = _M0L3p__S540->$0;
  _M0L1bS542 = _M0L3p__S540->$1;
  _M0L1cS543 = _M0L3p__S540->$2;
  _M0L1dS544 = _M0L3p__S540->$3;
  _M0L6tau__eS545 = _M0L3p__S540->$4;
  _M0L6tau__iS546 = _M0L3p__S540->$5;
  _M0L4e__eS547 = _M0L3p__S540->$6;
  _M0L4e__iS548 = _M0L3p__S540->$7;
  _M0L7_2abindS549 = 0;
  _M0L1iS550 = _M0L7_2abindS549;
  while (1) {
    if (_M0L1iS550 < _M0L1nS538) {
      struct _M0TPB5ArrayGfE* _M0L2geS1268 = _M0L1pS539->$6;
      struct _M0TPB5ArrayGfE* _M0L2geS1276 = _M0L1pS539->$6;
      float _M0L6_2atmpS1270;
      struct _M0TPB5ArrayGfE* _M0L2geS1275;
      float _M0L6_2atmpS1274;
      float _M0L6_2atmpS1273;
      float _M0L6_2atmpS1272;
      float _M0L6_2atmpS1271;
      float _M0L6_2atmpS1269;
      struct _M0TPB5ArrayGfE* _M0L2giS1277;
      struct _M0TPB5ArrayGfE* _M0L2giS1285;
      float _M0L6_2atmpS1279;
      struct _M0TPB5ArrayGfE* _M0L2giS1284;
      float _M0L6_2atmpS1283;
      float _M0L6_2atmpS1282;
      float _M0L6_2atmpS1281;
      float _M0L6_2atmpS1280;
      float _M0L6_2atmpS1278;
      int32_t _M0L6_2atmpS1286;
      #line 152 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L6_2atmpS1270 = _M0MPC15array5Array2atGfE(_M0L2geS1276, _M0L1iS550);
      _M0L2geS1275 = _M0L1pS539->$6;
      #line 152 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L6_2atmpS1274 = _M0MPC15array5Array2atGfE(_M0L2geS1275, _M0L1iS550);
      _M0L6_2atmpS1273 = -_M0L6_2atmpS1274;
      _M0L6_2atmpS1272 = _M0L2dtS551 * _M0L6_2atmpS1273;
      _M0L6_2atmpS1271 = _M0L6_2atmpS1272 / _M0L6tau__eS545;
      _M0L6_2atmpS1269 = _M0L6_2atmpS1270 + _M0L6_2atmpS1271;
      #line 152 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0MPC15array5Array3setGfE(_M0L2geS1268, _M0L1iS550, _M0L6_2atmpS1269);
      _M0L2giS1277 = _M0L1pS539->$7;
      _M0L2giS1285 = _M0L1pS539->$7;
      #line 153 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L6_2atmpS1279 = _M0MPC15array5Array2atGfE(_M0L2giS1285, _M0L1iS550);
      _M0L2giS1284 = _M0L1pS539->$7;
      #line 153 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L6_2atmpS1283 = _M0MPC15array5Array2atGfE(_M0L2giS1284, _M0L1iS550);
      _M0L6_2atmpS1282 = -_M0L6_2atmpS1283;
      _M0L6_2atmpS1281 = _M0L2dtS551 * _M0L6_2atmpS1282;
      _M0L6_2atmpS1280 = _M0L6_2atmpS1281 / _M0L6tau__iS546;
      _M0L6_2atmpS1278 = _M0L6_2atmpS1279 + _M0L6_2atmpS1280;
      #line 153 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0MPC15array5Array3setGfE(_M0L2giS1277, _M0L1iS550, _M0L6_2atmpS1278);
      _M0L6_2atmpS1286 = _M0L1iS550 + 1;
      _M0L1iS550 = _M0L6_2atmpS1286;
      continue;
    }
    break;
  }
  _M0L7_2abindS553 = 0;
  _M0L1iS554 = _M0L7_2abindS553;
  while (1) {
    if (_M0L1iS554 < _M0L1nS538) {
      struct _M0TPB5ArrayGfE* _M0L1vS1312 = _M0L1pS539->$2;
      float _M0L1vS555;
      struct _M0TPB5ArrayGfE* _M0L1uS1311;
      float _M0L1uS556;
      struct _M0TPB5ArrayGfE* _M0L1iS1310;
      float _M0L2iiS557;
      struct _M0TPB5ArrayGfE* _M0L1vS1287;
      float _M0L6_2atmpS1290;
      float _M0L6_2atmpS1297;
      float _M0L6_2atmpS1295;
      float _M0L6_2atmpS1296;
      float _M0L6_2atmpS1294;
      float _M0L6_2atmpS1293;
      float _M0L6_2atmpS1292;
      float _M0L6_2atmpS1291;
      float _M0L6_2atmpS1289;
      float _M0L6_2atmpS1288;
      struct _M0TPB5ArrayGfE* _M0L1vS1309;
      float _M0L2v2S558;
      struct _M0TPB5ArrayGfE* _M0L1vS1298;
      float _M0L6_2atmpS1301;
      float _M0L6_2atmpS1308;
      float _M0L6_2atmpS1306;
      float _M0L6_2atmpS1307;
      float _M0L6_2atmpS1305;
      float _M0L6_2atmpS1304;
      float _M0L6_2atmpS1303;
      float _M0L6_2atmpS1302;
      float _M0L6_2atmpS1300;
      float _M0L6_2atmpS1299;
      int32_t _M0L6_2atmpS1313;
      #line 157 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L1vS555 = _M0MPC15array5Array2atGfE(_M0L1vS1312, _M0L1iS554);
      _M0L1uS1311 = _M0L1pS539->$3;
      #line 158 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L1uS556 = _M0MPC15array5Array2atGfE(_M0L1uS1311, _M0L1iS554);
      _M0L1iS1310 = _M0L1pS539->$5;
      #line 159 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L2iiS557 = _M0MPC15array5Array2atGfE(_M0L1iS1310, _M0L1iS554);
      _M0L1vS1287 = _M0L1pS539->$2;
      _M0L6_2atmpS1290 = 0x1p-1f * _M0L2dtS551;
      _M0L6_2atmpS1297 = 0x1.47ae147ae147bp-5f * _M0L1vS555;
      _M0L6_2atmpS1295 = _M0L6_2atmpS1297 * _M0L1vS555;
      _M0L6_2atmpS1296 = 0x1.4p+2f * _M0L1vS555;
      _M0L6_2atmpS1294 = _M0L6_2atmpS1295 + _M0L6_2atmpS1296;
      _M0L6_2atmpS1293 = _M0L6_2atmpS1294 + 0x1.18p+7f;
      _M0L6_2atmpS1292 = _M0L6_2atmpS1293 - _M0L1uS556;
      _M0L6_2atmpS1291 = _M0L6_2atmpS1292 + _M0L2iiS557;
      _M0L6_2atmpS1289 = _M0L6_2atmpS1290 * _M0L6_2atmpS1291;
      _M0L6_2atmpS1288 = _M0L1vS555 + _M0L6_2atmpS1289;
      #line 160 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS1287, _M0L1iS554, _M0L6_2atmpS1288);
      _M0L1vS1309 = _M0L1pS539->$2;
      #line 161 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L2v2S558 = _M0MPC15array5Array2atGfE(_M0L1vS1309, _M0L1iS554);
      _M0L1vS1298 = _M0L1pS539->$2;
      _M0L6_2atmpS1301 = 0x1p-1f * _M0L2dtS551;
      _M0L6_2atmpS1308 = 0x1.47ae147ae147bp-5f * _M0L2v2S558;
      _M0L6_2atmpS1306 = _M0L6_2atmpS1308 * _M0L2v2S558;
      _M0L6_2atmpS1307 = 0x1.4p+2f * _M0L2v2S558;
      _M0L6_2atmpS1305 = _M0L6_2atmpS1306 + _M0L6_2atmpS1307;
      _M0L6_2atmpS1304 = _M0L6_2atmpS1305 + 0x1.18p+7f;
      _M0L6_2atmpS1303 = _M0L6_2atmpS1304 - _M0L1uS556;
      _M0L6_2atmpS1302 = _M0L6_2atmpS1303 + _M0L2iiS557;
      _M0L6_2atmpS1300 = _M0L6_2atmpS1301 * _M0L6_2atmpS1302;
      _M0L6_2atmpS1299 = _M0L2v2S558 + _M0L6_2atmpS1300;
      #line 162 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS1298, _M0L1iS554, _M0L6_2atmpS1299);
      _M0L6_2atmpS1313 = _M0L1iS554 + 1;
      _M0L1iS554 = _M0L6_2atmpS1313;
      continue;
    }
    break;
  }
  _M0L7_2abindS560 = 0;
  _M0L1iS561 = _M0L7_2abindS560;
  while (1) {
    if (_M0L1iS561 < _M0L1nS538) {
      struct _M0TPB5ArrayGfE* _M0L1vS1324 = _M0L1pS539->$2;
      float _M0L1vS562;
      struct _M0TPB5ArrayGfE* _M0L1uS1314;
      struct _M0TPB5ArrayGfE* _M0L1uS1323;
      float _M0L6_2atmpS1316;
      float _M0L6_2atmpS1318;
      float _M0L6_2atmpS1320;
      struct _M0TPB5ArrayGfE* _M0L1uS1322;
      float _M0L6_2atmpS1321;
      float _M0L6_2atmpS1319;
      float _M0L6_2atmpS1317;
      float _M0L6_2atmpS1315;
      int32_t _M0L6_2atmpS1325;
      #line 165 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L1vS562 = _M0MPC15array5Array2atGfE(_M0L1vS1324, _M0L1iS561);
      _M0L1uS1314 = _M0L1pS539->$3;
      _M0L1uS1323 = _M0L1pS539->$3;
      #line 166 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L6_2atmpS1316 = _M0MPC15array5Array2atGfE(_M0L1uS1323, _M0L1iS561);
      _M0L6_2atmpS1318 = _M0L2dtS551 * _M0L1aS541;
      _M0L6_2atmpS1320 = _M0L1bS542 * _M0L1vS562;
      _M0L1uS1322 = _M0L1pS539->$3;
      #line 166 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L6_2atmpS1321 = _M0MPC15array5Array2atGfE(_M0L1uS1322, _M0L1iS561);
      _M0L6_2atmpS1319 = _M0L6_2atmpS1320 - _M0L6_2atmpS1321;
      _M0L6_2atmpS1317 = _M0L6_2atmpS1318 * _M0L6_2atmpS1319;
      _M0L6_2atmpS1315 = _M0L6_2atmpS1316 + _M0L6_2atmpS1317;
      #line 166 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0MPC15array5Array3setGfE(_M0L1uS1314, _M0L1iS561, _M0L6_2atmpS1315);
      _M0L6_2atmpS1325 = _M0L1iS561 + 1;
      _M0L1iS561 = _M0L6_2atmpS1325;
      continue;
    }
    break;
  }
  _M0L7_2abindS564 = 0;
  _M0L1iS565 = _M0L7_2abindS564;
  while (1) {
    if (_M0L1iS565 < _M0L1nS538) {
      struct _M0TPB5ArrayGfE* _M0L1vS1326 = _M0L1pS539->$2;
      struct _M0TPB5ArrayGfE* _M0L1vS1343 = _M0L1pS539->$2;
      float _M0L6_2atmpS1328;
      struct _M0TPB5ArrayGfE* _M0L2geS1342;
      float _M0L6_2atmpS1338;
      struct _M0TPB5ArrayGfE* _M0L1vS1341;
      float _M0L6_2atmpS1340;
      float _M0L6_2atmpS1339;
      float _M0L6_2atmpS1331;
      struct _M0TPB5ArrayGfE* _M0L2giS1337;
      float _M0L6_2atmpS1333;
      struct _M0TPB5ArrayGfE* _M0L1vS1336;
      float _M0L6_2atmpS1335;
      float _M0L6_2atmpS1334;
      float _M0L6_2atmpS1332;
      float _M0L6_2atmpS1330;
      float _M0L6_2atmpS1329;
      float _M0L6_2atmpS1327;
      int32_t _M0L6_2atmpS1344;
      #line 169 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L6_2atmpS1328 = _M0MPC15array5Array2atGfE(_M0L1vS1343, _M0L1iS565);
      _M0L2geS1342 = _M0L1pS539->$6;
      #line 169 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L6_2atmpS1338 = _M0MPC15array5Array2atGfE(_M0L2geS1342, _M0L1iS565);
      _M0L1vS1341 = _M0L1pS539->$2;
      #line 169 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L6_2atmpS1340 = _M0MPC15array5Array2atGfE(_M0L1vS1341, _M0L1iS565);
      _M0L6_2atmpS1339 = _M0L4e__eS547 - _M0L6_2atmpS1340;
      _M0L6_2atmpS1331 = _M0L6_2atmpS1338 * _M0L6_2atmpS1339;
      _M0L2giS1337 = _M0L1pS539->$7;
      #line 169 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L6_2atmpS1333 = _M0MPC15array5Array2atGfE(_M0L2giS1337, _M0L1iS565);
      _M0L1vS1336 = _M0L1pS539->$2;
      #line 169 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L6_2atmpS1335 = _M0MPC15array5Array2atGfE(_M0L1vS1336, _M0L1iS565);
      _M0L6_2atmpS1334 = _M0L4e__iS548 - _M0L6_2atmpS1335;
      _M0L6_2atmpS1332 = _M0L6_2atmpS1333 * _M0L6_2atmpS1334;
      _M0L6_2atmpS1330 = _M0L6_2atmpS1331 + _M0L6_2atmpS1332;
      _M0L6_2atmpS1329 = _M0L2dtS551 * _M0L6_2atmpS1330;
      _M0L6_2atmpS1327 = _M0L6_2atmpS1328 + _M0L6_2atmpS1329;
      #line 169 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS1326, _M0L1iS565, _M0L6_2atmpS1327);
      _M0L6_2atmpS1344 = _M0L1iS565 + 1;
      _M0L1iS565 = _M0L6_2atmpS1344;
      continue;
    }
    break;
  }
  _M0L7_2abindS567 = 0;
  _M0L1iS568 = _M0L7_2abindS567;
  while (1) {
    if (_M0L1iS568 < _M0L1nS538) {
      struct _M0TPB5ArrayGbE* _M0L4fireS1345 = _M0L1pS539->$4;
      struct _M0TPB5ArrayGfE* _M0L1vS1348 = _M0L1pS539->$2;
      float _M0L6_2atmpS1347;
      int32_t _M0L6_2atmpS1346;
      struct _M0TPB5ArrayGfE* _M0L1vS1349;
      struct _M0TPB5ArrayGbE* _M0L4fireS1351;
      float _M0L6_2atmpS1350;
      struct _M0TPB5ArrayGfE* _M0L1uS1353;
      struct _M0TPB5ArrayGfE* _M0L1uS1358;
      float _M0L6_2atmpS1355;
      struct _M0TPB5ArrayGbE* _M0L4fireS1357;
      float _M0L6_2atmpS1356;
      float _M0L6_2atmpS1354;
      int32_t _M0L6_2atmpS1359;
      #line 173 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L6_2atmpS1347 = _M0MPC15array5Array2atGfE(_M0L1vS1348, _M0L1iS568);
      _M0L6_2atmpS1346 = _M0L6_2atmpS1347 > 0x1.ep+4f;
      #line 173 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0MPC15array5Array3setGbE(_M0L4fireS1345, _M0L1iS568, _M0L6_2atmpS1346);
      _M0L1vS1349 = _M0L1pS539->$2;
      _M0L4fireS1351 = _M0L1pS539->$4;
      #line 174 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS1351, _M0L1iS568)) {
        _M0L6_2atmpS1350 = _M0L1cS543;
      } else {
        struct _M0TPB5ArrayGfE* _M0L1vS1352 = _M0L1pS539->$2;
        #line 174 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
        _M0L6_2atmpS1350 = _M0MPC15array5Array2atGfE(_M0L1vS1352, _M0L1iS568);
      }
      #line 174 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS1349, _M0L1iS568, _M0L6_2atmpS1350);
      _M0L1uS1353 = _M0L1pS539->$3;
      _M0L1uS1358 = _M0L1pS539->$3;
      #line 175 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L6_2atmpS1355 = _M0MPC15array5Array2atGfE(_M0L1uS1358, _M0L1iS568);
      _M0L4fireS1357 = _M0L1pS539->$4;
      #line 175 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS1357, _M0L1iS568)) {
        _M0L6_2atmpS1356 = _M0L1dS544;
      } else {
        _M0L6_2atmpS1356 = 0x0p+0f;
      }
      _M0L6_2atmpS1354 = _M0L6_2atmpS1355 + _M0L6_2atmpS1356;
      #line 175 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0MPC15array5Array3setGfE(_M0L1uS1353, _M0L1iS568, _M0L6_2atmpS1354);
      _M0L6_2atmpS1359 = _M0L1iS568 + 1;
      _M0L1iS568 = _M0L6_2atmpS1359;
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

struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0MP26RiantR8snn__mbt7Xoshiro3new(
  uint64_t _M0L4seedS536
) {
  struct _M0TUmmmmE* _M0L1sS535;
  uint64_t _M0L6_2atmpS1267;
  struct _M0TUmmmmE* _M0L1tS537;
  uint64_t _M0L6_2atmpS1263;
  uint64_t _M0L6_2atmpS1264;
  uint64_t _M0L6_2atmpS1265;
  uint64_t _M0L6_2atmpS1266;
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _block_1393;
  #line 48 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  #line 49 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L1sS535 = _M0FP26RiantR8snn__mbt10splitmix64(_M0L4seedS536);
  _M0L6_2atmpS1267 = _M0L1sS535->$3;
  #line 50 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L1tS537 = _M0FP26RiantR8snn__mbt10splitmix64(_M0L6_2atmpS1267);
  _M0L6_2atmpS1263 = _M0L1sS535->$0;
  _M0L6_2atmpS1264 = _M0L1sS535->$1;
  _M0L6_2atmpS1265 = _M0L1sS535->$2;
  moonbit_decref(_M0L1sS535);
  _M0L6_2atmpS1266 = _M0L1tS537->$0;
  moonbit_decref(_M0L1tS537);
  _block_1393
  = (struct _M0TP26RiantR8snn__mbt7Xoshiro*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt7Xoshiro));
  Moonbit_object_header(_block_1393)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_1393->$0 = _M0L6_2atmpS1263;
  _block_1393->$1 = _M0L6_2atmpS1264;
  _block_1393->$2 = _M0L6_2atmpS1265;
  _block_1393->$3 = _M0L6_2atmpS1266;
  return _block_1393;
}

struct _M0TUmmmmE* _M0FP26RiantR8snn__mbt10splitmix64(uint64_t _M0L4seedS527) {
  uint64_t _M0L2s1S526;
  uint64_t _M0L2z1S528;
  uint64_t _M0L2s2S529;
  uint64_t _M0L2z2S530;
  uint64_t _M0L2s3S531;
  uint64_t _M0L2z3S532;
  uint64_t _M0L2s4S533;
  uint64_t _M0L2z4S534;
  struct _M0TUmmmmE* _block_1394;
  #line 62 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2s1S526 = _M0L4seedS527 + 11400714819323198485ull;
  #line 64 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2z1S528 = _M0FP26RiantR8snn__mbt5mix64(_M0L2s1S526);
  _M0L2s2S529 = _M0L2s1S526 + 11400714819323198485ull;
  #line 66 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2z2S530 = _M0FP26RiantR8snn__mbt5mix64(_M0L2s2S529);
  _M0L2s3S531 = _M0L2s2S529 + 11400714819323198485ull;
  #line 68 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2z3S532 = _M0FP26RiantR8snn__mbt5mix64(_M0L2s3S531);
  _M0L2s4S533 = _M0L2s3S531 + 11400714819323198485ull;
  #line 70 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2z4S534 = _M0FP26RiantR8snn__mbt5mix64(_M0L2s4S533);
  _block_1394 = (struct _M0TUmmmmE*)moonbit_malloc(sizeof(struct _M0TUmmmmE));
  Moonbit_object_header(_block_1394)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_1394->$0 = _M0L2z1S528;
  _block_1394->$1 = _M0L2z2S530;
  _block_1394->$2 = _M0L2z3S532;
  _block_1394->$3 = _M0L2z4S534;
  return _block_1394;
}

uint64_t _M0FP26RiantR8snn__mbt5mix64(uint64_t _M0L1zS524) {
  uint64_t _M0L6_2atmpS1262;
  uint64_t _M0L6_2atmpS1261;
  uint64_t _M0L1zS523;
  uint64_t _M0L6_2atmpS1260;
  uint64_t _M0L6_2atmpS1259;
  uint64_t _M0L1zS525;
  uint64_t _M0L6_2atmpS1258;
  #line 75 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L6_2atmpS1262 = _M0L1zS524 >> 30;
  _M0L6_2atmpS1261 = _M0L1zS524 ^ _M0L6_2atmpS1262;
  _M0L1zS523 = _M0L6_2atmpS1261 * 13787848793156543929ull;
  _M0L6_2atmpS1260 = _M0L1zS523 >> 27;
  _M0L6_2atmpS1259 = _M0L1zS523 ^ _M0L6_2atmpS1260;
  _M0L1zS525 = _M0L6_2atmpS1259 * 10723151780598845931ull;
  _M0L6_2atmpS1258 = _M0L1zS525 >> 31;
  return _M0L1zS525 ^ _M0L6_2atmpS1258;
}

moonbit_string_t _M0IPC15float5FloatPB4Show10to__string(float _M0L4selfS522) {
  double _M0L6_2atmpS1257;
  #line 16 "C:\\Users\\31379\\.moon\\lib\\core\\float\\methods.mbt"
  _M0L6_2atmpS1257 = (double)_M0L4selfS522;
  #line 17 "C:\\Users\\31379\\.moon\\lib\\core\\float\\methods.mbt"
  return _M0MPC16double6Double10to__string(_M0L6_2atmpS1257);
}

int32_t _M0MPC15float5Float7to__int(float _M0L4selfS521) {
  #line 43 "C:\\Users\\31379\\.moon\\lib\\core\\float\\to_int.mbt"
  if (_M0L4selfS521 != _M0L4selfS521) {
    return 0;
  } else if (_M0L4selfS521 >= 0x1.fffffffcp+30f) {
    return 2147483647;
  } else if (_M0L4selfS521 <= -0x1p+31f) {
    return (int32_t)0x80000000;
  } else {
    return (int32_t)_M0L4selfS521;
  }
}

struct _M0TPB5ArrayGfE* _M0MPC15array5Array4makeGfE(
  int32_t _M0L3lenS512,
  float _M0L4elemS514
) {
  struct _M0TPB5ArrayGfE* _M0L3arrS511;
  int32_t _M0L1iS513;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3arrS511 = _M0MPC15array5Array20unsafe__make__uninitGfE(_M0L3lenS512);
  _M0L1iS513 = 0;
  while (1) {
    if (_M0L1iS513 < _M0L3lenS512) {
      float* _M0L3bufS1253 = _M0L3arrS511->$0;
      int32_t _M0L6_2atmpS1254;
      _M0L3bufS1253[_M0L1iS513] = _M0L4elemS514;
      _M0L6_2atmpS1254 = _M0L1iS513 + 1;
      _M0L1iS513 = _M0L6_2atmpS1254;
      continue;
    }
    break;
  }
  return _M0L3arrS511;
}

struct _M0TPB5ArrayGbE* _M0MPC15array5Array4makeGbE(
  int32_t _M0L3lenS517,
  int32_t _M0L4elemS519
) {
  struct _M0TPB5ArrayGbE* _M0L3arrS516;
  int32_t _M0L1iS518;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3arrS516 = _M0MPC15array5Array20unsafe__make__uninitGbE(_M0L3lenS517);
  _M0L1iS518 = 0;
  while (1) {
    if (_M0L1iS518 < _M0L3lenS517) {
      uint8_t* _M0L3bufS1255 = _M0L3arrS516->$0;
      int32_t _M0L6_2atmpS1256;
      _M0L3bufS1255[_M0L1iS518] = _M0L4elemS519;
      _M0L6_2atmpS1256 = _M0L1iS518 + 1;
      _M0L1iS518 = _M0L6_2atmpS1256;
      continue;
    }
    break;
  }
  return _M0L3arrS516;
}

int32_t _M0MPC15array5Array3setGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS504,
  int32_t _M0L5indexS505,
  float _M0L5valueS506
) {
  int32_t _M0L3lenS503;
  #line 262 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS503 = _M0L4selfS504->$1;
  if (_M0L5indexS505 >= 0 && _M0L5indexS505 < _M0L3lenS503) {
    float* _M0L6_2atmpS1251;
    #line 268 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS1251 = _M0MPC15array5Array6bufferGfE(_M0L4selfS504);
    _M0L6_2atmpS1251[_M0L5indexS505] = _M0L5valueS506;
    moonbit_decref(_M0L6_2atmpS1251);
  } else {
    #line 267 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
  return 0;
}

int32_t _M0MPC15array5Array3setGbE(
  struct _M0TPB5ArrayGbE* _M0L4selfS508,
  int32_t _M0L5indexS509,
  int32_t _M0L5valueS510
) {
  int32_t _M0L3lenS507;
  #line 262 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS507 = _M0L4selfS508->$1;
  if (_M0L5indexS509 >= 0 && _M0L5indexS509 < _M0L3lenS507) {
    uint8_t* _M0L6_2atmpS1252;
    #line 268 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS1252 = _M0MPC15array5Array6bufferGbE(_M0L4selfS508);
    _M0L6_2atmpS1252[_M0L5indexS509] = _M0L5valueS510;
    moonbit_decref(_M0L6_2atmpS1252);
  } else {
    #line 267 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
  return 0;
}

float _M0MPC15array5Array2atGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS498,
  int32_t _M0L5indexS499
) {
  int32_t _M0L3lenS497;
  #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS497 = _M0L4selfS498->$1;
  if (_M0L5indexS499 >= 0 && _M0L5indexS499 < _M0L3lenS497) {
    float* _M0L6_2atmpS1249;
    float _result_1397;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS1249 = _M0MPC15array5Array6bufferGfE(_M0L4selfS498);
    _result_1397 = (float)_M0L6_2atmpS1249[_M0L5indexS499];
    moonbit_decref(_M0L6_2atmpS1249);
    return _result_1397;
  } else {
    #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
}

int32_t _M0MPC15array5Array2atGbE(
  struct _M0TPB5ArrayGbE* _M0L4selfS501,
  int32_t _M0L5indexS502
) {
  int32_t _M0L3lenS500;
  #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS500 = _M0L4selfS501->$1;
  if (_M0L5indexS502 >= 0 && _M0L5indexS502 < _M0L3lenS500) {
    uint8_t* _M0L6_2atmpS1250;
    int32_t _result_1398;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS1250 = _M0MPC15array5Array6bufferGbE(_M0L4selfS501);
    _result_1398 = (int32_t)_M0L6_2atmpS1250[_M0L5indexS502];
    moonbit_decref(_M0L6_2atmpS1250);
    return _result_1398;
  } else {
    #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
}

int32_t _M0FPB7printlnGsE(moonbit_string_t _M0L5inputS496) {
  moonbit_string_t _M0L6_2atmpS1248;
  #line 36 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\console.mbt"
  #line 37 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\console.mbt"
  _M0L6_2atmpS1248 = _M0IPC16string6StringPB4Show10to__string(_M0L5inputS496);
  #line 37 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\console.mbt"
  moonbit_println(_M0L6_2atmpS1248);
  moonbit_decref(_M0L6_2atmpS1248);
  return 0;
}

moonbit_string_t _M0MPC16double6Double10to__string(double _M0L4selfS495) {
  #line 296 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double.mbt"
  #line 298 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double.mbt"
  return _M0FPB15ryu__to__string(_M0L4selfS495);
}

moonbit_string_t _M0FPB15ryu__to__string(double _M0L3valS480) {
  uint64_t _M0L4bitsS483;
  uint64_t _M0L6_2atmpS1247;
  uint64_t _M0L6_2atmpS1246;
  int32_t _M0L8ieeeSignS484;
  uint64_t _M0L12ieeeMantissaS485;
  uint64_t _M0L6_2atmpS1245;
  uint64_t _M0L6_2atmpS1244;
  int32_t _M0L12ieeeExponentS486;
  struct _M0TPB17FloatingDecimal64* _M0L7_2abindS487;
  struct _M0TPB17FloatingDecimal64* _M0L1vS488;
  moonbit_string_t _result_1400;
  #line 668 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  if (_M0L3valS480 == 0x0p+0) {
    return (moonbit_string_t)moonbit_string_literal_0.data;
  }
  if (_M0L3valS480 >= -0x1p+53 && _M0L3valS480 <= 0x1p+53) {
    if (_M0L3valS480 >= -0x1p+31 && _M0L3valS480 <= 0x1.fffffffcp+30) {
      int32_t _M0L1iS481;
      double _M0L6_2atmpS1233;
      #line 683 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0L1iS481 = _M0MPC16double6Double7to__int(_M0L3valS480);
      _M0L6_2atmpS1233 = (double)_M0L1iS481;
      if (_M0L6_2atmpS1233 == _M0L3valS480) {
        #line 685 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        return _M0MPC13int3Int18to__string_2einner(_M0L1iS481, 10);
      }
    } else {
      int64_t _M0L1iS482;
      double _M0L6_2atmpS1234;
      #line 688 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0L1iS482 = _M0MPC16double6Double9to__int64(_M0L3valS480);
      _M0L6_2atmpS1234 = (double)_M0L1iS482;
      if (_M0L6_2atmpS1234 == _M0L3valS480) {
        #line 690 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        return _M0MPC15int645Int6418to__string_2einner(_M0L1iS482, 10);
      }
    }
  }
  _M0L4bitsS483 = *(int64_t*)&_M0L3valS480;
  _M0L6_2atmpS1247 = _M0L4bitsS483 >> 63;
  _M0L6_2atmpS1246 = _M0L6_2atmpS1247 & 1ull;
  _M0L8ieeeSignS484 = _M0L6_2atmpS1246 != 0ull;
  _M0L12ieeeMantissaS485 = _M0L4bitsS483 & 4503599627370495ull;
  _M0L6_2atmpS1245 = _M0L4bitsS483 >> 52;
  _M0L6_2atmpS1244 = _M0L6_2atmpS1245 & 2047ull;
  _M0L12ieeeExponentS486 = (int32_t)_M0L6_2atmpS1244;
  if (
    _M0L12ieeeExponentS486 == 2047
    || _M0L12ieeeExponentS486 == 0 && _M0L12ieeeMantissaS485 == 0ull
  ) {
    int32_t _M0L6_2atmpS1235 = _M0L12ieeeExponentS486 != 0;
    int32_t _M0L6_2atmpS1236 = _M0L12ieeeMantissaS485 != 0ull;
    #line 707 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    return _M0FPB18copy__special__str(_M0L8ieeeSignS484, _M0L6_2atmpS1235, _M0L6_2atmpS1236);
  }
  #line 709 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS487
  = _M0FPB15d2d__small__int(_M0L12ieeeMantissaS485, _M0L12ieeeExponentS486);
  if (_M0L7_2abindS487 == 0) {
    uint32_t _M0L6_2atmpS1237;
    if (_M0L7_2abindS487) {
      moonbit_decref(_M0L7_2abindS487);
    }
    _M0L6_2atmpS1237 = *(uint32_t*)&_M0L12ieeeExponentS486;
    #line 719 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L1vS488 = _M0FPB3d2d(_M0L12ieeeMantissaS485, _M0L6_2atmpS1237);
  } else {
    struct _M0TPB17FloatingDecimal64* _M0L7_2aSomeS489 = _M0L7_2abindS487;
    struct _M0TPB17FloatingDecimal64* _M0L4_2afS490 = _M0L7_2aSomeS489;
    struct _M0TPB17FloatingDecimal64* _M0L1xS491 = _M0L4_2afS490;
    while (1) {
      uint64_t _M0L8mantissaS1243 = _M0L1xS491->$0;
      uint64_t _M0L1qS492 = _M0L8mantissaS1243 / 10ull;
      uint64_t _M0L8mantissaS1241 = _M0L1xS491->$0;
      uint64_t _M0L6_2atmpS1242 = 10ull * _M0L1qS492;
      uint64_t _M0L1rS493 = _M0L8mantissaS1241 - _M0L6_2atmpS1242;
      int32_t _M0L8exponentS1240;
      int32_t _M0L6_2atmpS1239;
      struct _M0TPB17FloatingDecimal64* _M0L6_2atmpS1238;
      if (_M0L1rS493 != 0ull) {
        _M0L1vS488 = _M0L1xS491;
        break;
      }
      _M0L8exponentS1240 = _M0L1xS491->$1;
      moonbit_decref(_M0L1xS491);
      _M0L6_2atmpS1239 = _M0L8exponentS1240 + 1;
      _M0L6_2atmpS1238
      = (struct _M0TPB17FloatingDecimal64*)moonbit_malloc(sizeof(struct _M0TPB17FloatingDecimal64));
      Moonbit_object_header(_M0L6_2atmpS1238)->meta
      = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
      _M0L6_2atmpS1238->$0 = _M0L1qS492;
      _M0L6_2atmpS1238->$1 = _M0L6_2atmpS1239;
      _M0L1xS491 = _M0L6_2atmpS1238;
      continue;
      break;
    }
  }
  #line 721 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _result_1400 = _M0FPB9to__chars(_M0L1vS488, _M0L8ieeeSignS484);
  moonbit_decref(_M0L1vS488);
  return _result_1400;
}

struct _M0TPB17FloatingDecimal64* _M0FPB15d2d__small__int(
  uint64_t _M0L12ieeeMantissaS475,
  int32_t _M0L12ieeeExponentS477
) {
  uint64_t _M0L2m2S474;
  int32_t _M0L6_2atmpS1232;
  int32_t _M0L2e2S476;
  int32_t _M0L6_2atmpS1231;
  uint64_t _M0L6_2atmpS1230;
  uint64_t _M0L4maskS478;
  uint64_t _M0L8fractionS479;
  int32_t _M0L6_2atmpS1229;
  uint64_t _M0L6_2atmpS1228;
  struct _M0TPB17FloatingDecimal64* _M0L6_2atmpS1227;
  #line 637 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L2m2S474 = 4503599627370496ull | _M0L12ieeeMantissaS475;
  _M0L6_2atmpS1232 = _M0L12ieeeExponentS477 - 1023;
  _M0L2e2S476 = _M0L6_2atmpS1232 - 52;
  if (_M0L2e2S476 > 0) {
    return 0;
  }
  if (_M0L2e2S476 < -52) {
    return 0;
  }
  _M0L6_2atmpS1231 = -_M0L2e2S476;
  _M0L6_2atmpS1230 = 1ull << (_M0L6_2atmpS1231 & 63);
  _M0L4maskS478 = _M0L6_2atmpS1230 - 1ull;
  _M0L8fractionS479 = _M0L2m2S474 & _M0L4maskS478;
  if (_M0L8fractionS479 != 0ull) {
    return 0;
  }
  _M0L6_2atmpS1229 = -_M0L2e2S476;
  _M0L6_2atmpS1228 = _M0L2m2S474 >> (_M0L6_2atmpS1229 & 63);
  _M0L6_2atmpS1227
  = (struct _M0TPB17FloatingDecimal64*)moonbit_malloc(sizeof(struct _M0TPB17FloatingDecimal64));
  Moonbit_object_header(_M0L6_2atmpS1227)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L6_2atmpS1227->$0 = _M0L6_2atmpS1228;
  _M0L6_2atmpS1227->$1 = 0;
  return _M0L6_2atmpS1227;
}

moonbit_string_t _M0FPB9to__chars(
  struct _M0TPB17FloatingDecimal64* _M0L1vS442,
  int32_t _M0L4signS440
) {
  moonbit_bytes_t _M0L6resultS438;
  int32_t _M0Lm5indexS439;
  uint64_t _M0L6outputS441;
  int32_t _M0L7olengthS443;
  int32_t _M0L8exponentS1226;
  int32_t _M0L6_2atmpS1225;
  int32_t _M0Lm3expS444;
  int32_t _M0L6_2atmpS1224;
  int32_t _M0L6_2atmpS1222;
  int32_t _M0L18scientificNotationS445;
  #line 530 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6resultS438 = (moonbit_bytes_t)moonbit_make_bytes(25, 0);
  _M0Lm5indexS439 = 0;
  if (_M0L4signS440) {
    int32_t _M0L6_2atmpS1096 = _M0Lm5indexS439;
    int32_t _M0L6_2atmpS1097;
    if (
      _M0L6_2atmpS1096 < 0
      || _M0L6_2atmpS1096 >= Moonbit_array_length(_M0L6resultS438)
    ) {
      #line 535 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      moonbit_panic();
    }
    _M0L6resultS438[_M0L6_2atmpS1096] = 45;
    _M0L6_2atmpS1097 = _M0Lm5indexS439;
    _M0Lm5indexS439 = _M0L6_2atmpS1097 + 1;
  }
  _M0L6outputS441 = _M0L1vS442->$0;
  #line 539 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7olengthS443 = _M0FPB17decimal__length17(_M0L6outputS441);
  _M0L8exponentS1226 = _M0L1vS442->$1;
  _M0L6_2atmpS1225 = _M0L8exponentS1226 + _M0L7olengthS443;
  _M0Lm3expS444 = _M0L6_2atmpS1225 - 1;
  _M0L6_2atmpS1224 = _M0Lm3expS444;
  if (_M0L6_2atmpS1224 >= -6) {
    int32_t _M0L6_2atmpS1223 = _M0Lm3expS444;
    _M0L6_2atmpS1222 = _M0L6_2atmpS1223 < 21;
  } else {
    _M0L6_2atmpS1222 = 0;
  }
  _M0L18scientificNotationS445 = !_M0L6_2atmpS1222;
  if (_M0L18scientificNotationS445) {
    int32_t _M0L7_2abindS446 = _M0L7olengthS443 - 1;
    uint64_t _M0L6outputS447;
    int32_t _M0L1iS448 = 0;
    uint64_t _M0L6outputS449 = _M0L6outputS441;
    int32_t _M0L6_2atmpS1098;
    int32_t _M0L6_2atmpS1102;
    int32_t _M0L6_2atmpS1101;
    int32_t _M0L6_2atmpS1100;
    int32_t _M0L6_2atmpS1099;
    int32_t _M0L6_2atmpS1106;
    int32_t _M0L6_2atmpS1107;
    int32_t _M0L6_2atmpS1108;
    int32_t _M0L6_2atmpS1109;
    int32_t _M0L6_2atmpS1110;
    int32_t _M0L6_2atmpS1116;
    int32_t _M0L6_2atmpS1149;
    moonbit_string_t _result_1402;
    while (1) {
      if (_M0L1iS448 < _M0L7_2abindS446) {
        uint64_t _M0L1cS450 = _M0L6outputS449 % 10ull;
        int32_t _M0L6_2atmpS1155 = _M0Lm5indexS439;
        int32_t _M0L6_2atmpS1154 = _M0L6_2atmpS1155 + _M0L7olengthS443;
        int32_t _M0L6_2atmpS1150 = _M0L6_2atmpS1154 - _M0L1iS448;
        int32_t _M0L6_2atmpS1153 = (int32_t)_M0L1cS450;
        int32_t _M0L6_2atmpS1152 = 48 + _M0L6_2atmpS1153;
        int32_t _M0L6_2atmpS1151 = _M0L6_2atmpS1152 & 0xff;
        int32_t _M0L6_2atmpS1156;
        uint64_t _M0L6_2atmpS1157;
        if (
          _M0L6_2atmpS1150 < 0
          || _M0L6_2atmpS1150 >= Moonbit_array_length(_M0L6resultS438)
        ) {
          #line 547 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
          moonbit_panic();
        }
        _M0L6resultS438[_M0L6_2atmpS1150] = _M0L6_2atmpS1151;
        _M0L6_2atmpS1156 = _M0L1iS448 + 1;
        _M0L6_2atmpS1157 = _M0L6outputS449 / 10ull;
        _M0L1iS448 = _M0L6_2atmpS1156;
        _M0L6outputS449 = _M0L6_2atmpS1157;
        continue;
      } else {
        _M0L6outputS447 = _M0L6outputS449;
      }
      break;
    }
    _M0L6_2atmpS1098 = _M0Lm5indexS439;
    _M0L6_2atmpS1102 = (int32_t)_M0L6outputS447;
    _M0L6_2atmpS1101 = _M0L6_2atmpS1102 % 10;
    _M0L6_2atmpS1100 = 48 + _M0L6_2atmpS1101;
    _M0L6_2atmpS1099 = _M0L6_2atmpS1100 & 0xff;
    if (
      _M0L6_2atmpS1098 < 0
      || _M0L6_2atmpS1098 >= Moonbit_array_length(_M0L6resultS438)
    ) {
      #line 552 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      moonbit_panic();
    }
    _M0L6resultS438[_M0L6_2atmpS1098] = _M0L6_2atmpS1099;
    if (_M0L7olengthS443 > 1) {
      int32_t _M0L6_2atmpS1104 = _M0Lm5indexS439;
      int32_t _M0L6_2atmpS1103 = _M0L6_2atmpS1104 + 1;
      if (
        _M0L6_2atmpS1103 < 0
        || _M0L6_2atmpS1103 >= Moonbit_array_length(_M0L6resultS438)
      ) {
        #line 554 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS438[_M0L6_2atmpS1103] = 46;
    } else {
      int32_t _M0L6_2atmpS1105 = _M0Lm5indexS439;
      _M0Lm5indexS439 = _M0L6_2atmpS1105 - 1;
    }
    _M0L6_2atmpS1106 = _M0Lm5indexS439;
    _M0L6_2atmpS1107 = _M0L7olengthS443 + 1;
    _M0Lm5indexS439 = _M0L6_2atmpS1106 + _M0L6_2atmpS1107;
    _M0L6_2atmpS1108 = _M0Lm5indexS439;
    if (
      _M0L6_2atmpS1108 < 0
      || _M0L6_2atmpS1108 >= Moonbit_array_length(_M0L6resultS438)
    ) {
      #line 562 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      moonbit_panic();
    }
    _M0L6resultS438[_M0L6_2atmpS1108] = 101;
    _M0L6_2atmpS1109 = _M0Lm5indexS439;
    _M0Lm5indexS439 = _M0L6_2atmpS1109 + 1;
    _M0L6_2atmpS1110 = _M0Lm3expS444;
    if (_M0L6_2atmpS1110 < 0) {
      int32_t _M0L6_2atmpS1111 = _M0Lm5indexS439;
      int32_t _M0L6_2atmpS1112;
      int32_t _M0L6_2atmpS1113;
      if (
        _M0L6_2atmpS1111 < 0
        || _M0L6_2atmpS1111 >= Moonbit_array_length(_M0L6resultS438)
      ) {
        #line 565 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS438[_M0L6_2atmpS1111] = 45;
      _M0L6_2atmpS1112 = _M0Lm5indexS439;
      _M0Lm5indexS439 = _M0L6_2atmpS1112 + 1;
      _M0L6_2atmpS1113 = _M0Lm3expS444;
      _M0Lm3expS444 = -_M0L6_2atmpS1113;
    } else {
      int32_t _M0L6_2atmpS1114 = _M0Lm5indexS439;
      int32_t _M0L6_2atmpS1115;
      if (
        _M0L6_2atmpS1114 < 0
        || _M0L6_2atmpS1114 >= Moonbit_array_length(_M0L6resultS438)
      ) {
        #line 569 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS438[_M0L6_2atmpS1114] = 43;
      _M0L6_2atmpS1115 = _M0Lm5indexS439;
      _M0Lm5indexS439 = _M0L6_2atmpS1115 + 1;
    }
    _M0L6_2atmpS1116 = _M0Lm3expS444;
    if (_M0L6_2atmpS1116 >= 100) {
      int32_t _M0L6_2atmpS1132 = _M0Lm3expS444;
      int32_t _M0L1aS452 = _M0L6_2atmpS1132 / 100;
      int32_t _M0L6_2atmpS1131 = _M0Lm3expS444;
      int32_t _M0L6_2atmpS1130 = _M0L6_2atmpS1131 / 10;
      int32_t _M0L1bS453 = _M0L6_2atmpS1130 % 10;
      int32_t _M0L6_2atmpS1129 = _M0Lm3expS444;
      int32_t _M0L1cS454 = _M0L6_2atmpS1129 % 10;
      int32_t _M0L6_2atmpS1117 = _M0Lm5indexS439;
      int32_t _M0L6_2atmpS1119 = 48 + _M0L1aS452;
      int32_t _M0L6_2atmpS1118 = _M0L6_2atmpS1119 & 0xff;
      int32_t _M0L6_2atmpS1123;
      int32_t _M0L6_2atmpS1120;
      int32_t _M0L6_2atmpS1122;
      int32_t _M0L6_2atmpS1121;
      int32_t _M0L6_2atmpS1127;
      int32_t _M0L6_2atmpS1124;
      int32_t _M0L6_2atmpS1126;
      int32_t _M0L6_2atmpS1125;
      int32_t _M0L6_2atmpS1128;
      if (
        _M0L6_2atmpS1117 < 0
        || _M0L6_2atmpS1117 >= Moonbit_array_length(_M0L6resultS438)
      ) {
        #line 576 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS438[_M0L6_2atmpS1117] = _M0L6_2atmpS1118;
      _M0L6_2atmpS1123 = _M0Lm5indexS439;
      _M0L6_2atmpS1120 = _M0L6_2atmpS1123 + 1;
      _M0L6_2atmpS1122 = 48 + _M0L1bS453;
      _M0L6_2atmpS1121 = _M0L6_2atmpS1122 & 0xff;
      if (
        _M0L6_2atmpS1120 < 0
        || _M0L6_2atmpS1120 >= Moonbit_array_length(_M0L6resultS438)
      ) {
        #line 577 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS438[_M0L6_2atmpS1120] = _M0L6_2atmpS1121;
      _M0L6_2atmpS1127 = _M0Lm5indexS439;
      _M0L6_2atmpS1124 = _M0L6_2atmpS1127 + 2;
      _M0L6_2atmpS1126 = 48 + _M0L1cS454;
      _M0L6_2atmpS1125 = _M0L6_2atmpS1126 & 0xff;
      if (
        _M0L6_2atmpS1124 < 0
        || _M0L6_2atmpS1124 >= Moonbit_array_length(_M0L6resultS438)
      ) {
        #line 578 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS438[_M0L6_2atmpS1124] = _M0L6_2atmpS1125;
      _M0L6_2atmpS1128 = _M0Lm5indexS439;
      _M0Lm5indexS439 = _M0L6_2atmpS1128 + 3;
    } else {
      int32_t _M0L6_2atmpS1133 = _M0Lm3expS444;
      if (_M0L6_2atmpS1133 >= 10) {
        int32_t _M0L6_2atmpS1143 = _M0Lm3expS444;
        int32_t _M0L1aS455 = _M0L6_2atmpS1143 / 10;
        int32_t _M0L6_2atmpS1142 = _M0Lm3expS444;
        int32_t _M0L1bS456 = _M0L6_2atmpS1142 % 10;
        int32_t _M0L6_2atmpS1134 = _M0Lm5indexS439;
        int32_t _M0L6_2atmpS1136 = 48 + _M0L1aS455;
        int32_t _M0L6_2atmpS1135 = _M0L6_2atmpS1136 & 0xff;
        int32_t _M0L6_2atmpS1140;
        int32_t _M0L6_2atmpS1137;
        int32_t _M0L6_2atmpS1139;
        int32_t _M0L6_2atmpS1138;
        int32_t _M0L6_2atmpS1141;
        if (
          _M0L6_2atmpS1134 < 0
          || _M0L6_2atmpS1134 >= Moonbit_array_length(_M0L6resultS438)
        ) {
          #line 583 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
          moonbit_panic();
        }
        _M0L6resultS438[_M0L6_2atmpS1134] = _M0L6_2atmpS1135;
        _M0L6_2atmpS1140 = _M0Lm5indexS439;
        _M0L6_2atmpS1137 = _M0L6_2atmpS1140 + 1;
        _M0L6_2atmpS1139 = 48 + _M0L1bS456;
        _M0L6_2atmpS1138 = _M0L6_2atmpS1139 & 0xff;
        if (
          _M0L6_2atmpS1137 < 0
          || _M0L6_2atmpS1137 >= Moonbit_array_length(_M0L6resultS438)
        ) {
          #line 584 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
          moonbit_panic();
        }
        _M0L6resultS438[_M0L6_2atmpS1137] = _M0L6_2atmpS1138;
        _M0L6_2atmpS1141 = _M0Lm5indexS439;
        _M0Lm5indexS439 = _M0L6_2atmpS1141 + 2;
      } else {
        int32_t _M0L6_2atmpS1144 = _M0Lm5indexS439;
        int32_t _M0L6_2atmpS1147 = _M0Lm3expS444;
        int32_t _M0L6_2atmpS1146 = 48 + _M0L6_2atmpS1147;
        int32_t _M0L6_2atmpS1145 = _M0L6_2atmpS1146 & 0xff;
        int32_t _M0L6_2atmpS1148;
        if (
          _M0L6_2atmpS1144 < 0
          || _M0L6_2atmpS1144 >= Moonbit_array_length(_M0L6resultS438)
        ) {
          #line 587 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
          moonbit_panic();
        }
        _M0L6resultS438[_M0L6_2atmpS1144] = _M0L6_2atmpS1145;
        _M0L6_2atmpS1148 = _M0Lm5indexS439;
        _M0Lm5indexS439 = _M0L6_2atmpS1148 + 1;
      }
    }
    _M0L6_2atmpS1149 = _M0Lm5indexS439;
    #line 590 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _result_1402
    = _M0FPB19string__from__bytes(_M0L6resultS438, 0, _M0L6_2atmpS1149);
    moonbit_decref(_M0L6resultS438);
    return _result_1402;
  } else {
    int32_t _M0L6_2atmpS1158 = _M0Lm3expS444;
    int32_t _M0L6_2atmpS1221;
    moonbit_string_t _result_1408;
    if (_M0L6_2atmpS1158 < 0) {
      int32_t _M0L6_2atmpS1159 = _M0Lm5indexS439;
      int32_t _M0L6_2atmpS1161;
      int32_t _M0L6_2atmpS1160;
      int32_t _M0L6_2atmpS1162;
      int32_t _M0L1iS457;
      int32_t _M0L6_2atmpS1177;
      int32_t _M0L6_2atmpS1179;
      int32_t _M0L6_2atmpS1178;
      int32_t _M0L7currentS459;
      int32_t _M0L1iS460;
      uint64_t _M0L6outputS461;
      if (
        _M0L6_2atmpS1159 < 0
        || _M0L6_2atmpS1159 >= Moonbit_array_length(_M0L6resultS438)
      ) {
        #line 595 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS438[_M0L6_2atmpS1159] = 48;
      _M0L6_2atmpS1161 = _M0Lm5indexS439;
      _M0L6_2atmpS1160 = _M0L6_2atmpS1161 + 1;
      if (
        _M0L6_2atmpS1160 < 0
        || _M0L6_2atmpS1160 >= Moonbit_array_length(_M0L6resultS438)
      ) {
        #line 596 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS438[_M0L6_2atmpS1160] = 46;
      _M0L6_2atmpS1162 = _M0Lm5indexS439;
      _M0Lm5indexS439 = _M0L6_2atmpS1162 + 2;
      _M0L1iS457 = -1;
      while (1) {
        int32_t _M0L6_2atmpS1163 = _M0Lm3expS444;
        if (_M0L1iS457 > _M0L6_2atmpS1163) {
          int32_t _M0L6_2atmpS1166 = _M0Lm5indexS439;
          int32_t _M0L6_2atmpS1165 = _M0L6_2atmpS1166 - _M0L1iS457;
          int32_t _M0L6_2atmpS1164 = _M0L6_2atmpS1165 - 1;
          int32_t _M0L6_2atmpS1167;
          if (
            _M0L6_2atmpS1164 < 0
            || _M0L6_2atmpS1164 >= Moonbit_array_length(_M0L6resultS438)
          ) {
            #line 599 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
            moonbit_panic();
          }
          _M0L6resultS438[_M0L6_2atmpS1164] = 48;
          _M0L6_2atmpS1167 = _M0L1iS457 - 1;
          _M0L1iS457 = _M0L6_2atmpS1167;
          continue;
        }
        break;
      }
      _M0L6_2atmpS1177 = _M0Lm5indexS439;
      _M0L6_2atmpS1179 = _M0Lm3expS444;
      _M0L6_2atmpS1178 = -1 - _M0L6_2atmpS1179;
      _M0L7currentS459 = _M0L6_2atmpS1177 + _M0L6_2atmpS1178;
      _M0L1iS460 = 0;
      _M0L6outputS461 = _M0L6outputS441;
      while (1) {
        if (_M0L1iS460 < _M0L7olengthS443) {
          int32_t _M0L6_2atmpS1174 = _M0L7currentS459 + _M0L7olengthS443;
          int32_t _M0L6_2atmpS1173 = _M0L6_2atmpS1174 - _M0L1iS460;
          int32_t _M0L6_2atmpS1168 = _M0L6_2atmpS1173 - 1;
          uint64_t _M0L6_2atmpS1172 = _M0L6outputS461 % 10ull;
          int32_t _M0L6_2atmpS1171 = (int32_t)_M0L6_2atmpS1172;
          int32_t _M0L6_2atmpS1170 = 48 + _M0L6_2atmpS1171;
          int32_t _M0L6_2atmpS1169 = _M0L6_2atmpS1170 & 0xff;
          int32_t _M0L6_2atmpS1175;
          uint64_t _M0L6_2atmpS1176;
          if (
            _M0L6_2atmpS1168 < 0
            || _M0L6_2atmpS1168 >= Moonbit_array_length(_M0L6resultS438)
          ) {
            #line 603 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
            moonbit_panic();
          }
          _M0L6resultS438[_M0L6_2atmpS1168] = _M0L6_2atmpS1169;
          _M0L6_2atmpS1175 = _M0L1iS460 + 1;
          _M0L6_2atmpS1176 = _M0L6outputS461 / 10ull;
          _M0L1iS460 = _M0L6_2atmpS1175;
          _M0L6outputS461 = _M0L6_2atmpS1176;
          continue;
        }
        break;
      }
      _M0Lm5indexS439 = _M0L7currentS459 + _M0L7olengthS443;
    } else {
      int32_t _M0L6_2atmpS1181 = _M0Lm3expS444;
      int32_t _M0L6_2atmpS1180 = _M0L6_2atmpS1181 + 1;
      if (_M0L6_2atmpS1180 >= _M0L7olengthS443) {
        int32_t _M0L1iS463 = 0;
        uint64_t _M0L6outputS464 = _M0L6outputS441;
        int32_t _M0L6_2atmpS1192;
        int32_t _M0L6_2atmpS1197;
        int32_t _M0L7_2abindS466;
        int32_t _M0L1iS467;
        int32_t _M0L6_2atmpS1198;
        int32_t _M0L6_2atmpS1201;
        int32_t _M0L6_2atmpS1200;
        int32_t _M0L6_2atmpS1199;
        while (1) {
          if (_M0L1iS463 < _M0L7olengthS443) {
            int32_t _M0L6_2atmpS1189 = _M0Lm5indexS439;
            int32_t _M0L6_2atmpS1188 = _M0L6_2atmpS1189 + _M0L7olengthS443;
            int32_t _M0L6_2atmpS1187 = _M0L6_2atmpS1188 - _M0L1iS463;
            int32_t _M0L6_2atmpS1182 = _M0L6_2atmpS1187 - 1;
            uint64_t _M0L6_2atmpS1186 = _M0L6outputS464 % 10ull;
            int32_t _M0L6_2atmpS1185 = (int32_t)_M0L6_2atmpS1186;
            int32_t _M0L6_2atmpS1184 = 48 + _M0L6_2atmpS1185;
            int32_t _M0L6_2atmpS1183 = _M0L6_2atmpS1184 & 0xff;
            int32_t _M0L6_2atmpS1190;
            uint64_t _M0L6_2atmpS1191;
            if (
              _M0L6_2atmpS1182 < 0
              || _M0L6_2atmpS1182 >= Moonbit_array_length(_M0L6resultS438)
            ) {
              #line 610 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
              moonbit_panic();
            }
            _M0L6resultS438[_M0L6_2atmpS1182] = _M0L6_2atmpS1183;
            _M0L6_2atmpS1190 = _M0L1iS463 + 1;
            _M0L6_2atmpS1191 = _M0L6outputS464 / 10ull;
            _M0L1iS463 = _M0L6_2atmpS1190;
            _M0L6outputS464 = _M0L6_2atmpS1191;
            continue;
          }
          break;
        }
        _M0L6_2atmpS1192 = _M0Lm5indexS439;
        _M0Lm5indexS439 = _M0L6_2atmpS1192 + _M0L7olengthS443;
        _M0L6_2atmpS1197 = _M0Lm3expS444;
        _M0L7_2abindS466 = _M0L6_2atmpS1197 + 1;
        _M0L1iS467 = _M0L7olengthS443;
        while (1) {
          if (_M0L1iS467 < _M0L7_2abindS466) {
            int32_t _M0L6_2atmpS1195 = _M0Lm5indexS439;
            int32_t _M0L6_2atmpS1194 = _M0L6_2atmpS1195 + _M0L1iS467;
            int32_t _M0L6_2atmpS1193 = _M0L6_2atmpS1194 - _M0L7olengthS443;
            int32_t _M0L6_2atmpS1196;
            if (
              _M0L6_2atmpS1193 < 0
              || _M0L6_2atmpS1193 >= Moonbit_array_length(_M0L6resultS438)
            ) {
              #line 615 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
              moonbit_panic();
            }
            _M0L6resultS438[_M0L6_2atmpS1193] = 48;
            _M0L6_2atmpS1196 = _M0L1iS467 + 1;
            _M0L1iS467 = _M0L6_2atmpS1196;
            continue;
          }
          break;
        }
        _M0L6_2atmpS1198 = _M0Lm5indexS439;
        _M0L6_2atmpS1201 = _M0Lm3expS444;
        _M0L6_2atmpS1200 = _M0L6_2atmpS1201 + 1;
        _M0L6_2atmpS1199 = _M0L6_2atmpS1200 - _M0L7olengthS443;
        _M0Lm5indexS439 = _M0L6_2atmpS1198 + _M0L6_2atmpS1199;
      } else {
        int32_t _M0L6_2atmpS1218 = _M0Lm5indexS439;
        int32_t _M0L6_2atmpS1217 = _M0L6_2atmpS1218 + 1;
        int32_t _M0L1iS469 = 0;
        int32_t _M0L7currentS470 = _M0L6_2atmpS1217;
        uint64_t _M0L6outputS471 = _M0L6outputS441;
        int32_t _M0L6_2atmpS1219;
        int32_t _M0L6_2atmpS1220;
        while (1) {
          if (_M0L1iS469 < _M0L7olengthS443) {
            int32_t _M0L6_2atmpS1213 = _M0L7olengthS443 - _M0L1iS469;
            int32_t _M0L6_2atmpS1211 = _M0L6_2atmpS1213 - 1;
            int32_t _M0L6_2atmpS1212 = _M0Lm3expS444;
            int32_t _M0L7currentS472;
            int32_t _M0L6_2atmpS1208;
            int32_t _M0L6_2atmpS1207;
            int32_t _M0L6_2atmpS1202;
            uint64_t _M0L6_2atmpS1206;
            int32_t _M0L6_2atmpS1205;
            int32_t _M0L6_2atmpS1204;
            int32_t _M0L6_2atmpS1203;
            int32_t _M0L6_2atmpS1209;
            uint64_t _M0L6_2atmpS1210;
            if (_M0L6_2atmpS1211 == _M0L6_2atmpS1212) {
              int32_t _M0L6_2atmpS1216 = _M0L7currentS470 + _M0L7olengthS443;
              int32_t _M0L6_2atmpS1215 = _M0L6_2atmpS1216 - _M0L1iS469;
              int32_t _M0L6_2atmpS1214 = _M0L6_2atmpS1215 - 1;
              if (
                _M0L6_2atmpS1214 < 0
                || _M0L6_2atmpS1214 >= Moonbit_array_length(_M0L6resultS438)
              ) {
                #line 622 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
                moonbit_panic();
              }
              _M0L6resultS438[_M0L6_2atmpS1214] = 46;
              _M0L7currentS472 = _M0L7currentS470 - 1;
            } else {
              _M0L7currentS472 = _M0L7currentS470;
            }
            _M0L6_2atmpS1208 = _M0L7currentS472 + _M0L7olengthS443;
            _M0L6_2atmpS1207 = _M0L6_2atmpS1208 - _M0L1iS469;
            _M0L6_2atmpS1202 = _M0L6_2atmpS1207 - 1;
            _M0L6_2atmpS1206 = _M0L6outputS471 % 10ull;
            _M0L6_2atmpS1205 = (int32_t)_M0L6_2atmpS1206;
            _M0L6_2atmpS1204 = 48 + _M0L6_2atmpS1205;
            _M0L6_2atmpS1203 = _M0L6_2atmpS1204 & 0xff;
            if (
              _M0L6_2atmpS1202 < 0
              || _M0L6_2atmpS1202 >= Moonbit_array_length(_M0L6resultS438)
            ) {
              #line 627 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
              moonbit_panic();
            }
            _M0L6resultS438[_M0L6_2atmpS1202] = _M0L6_2atmpS1203;
            _M0L6_2atmpS1209 = _M0L1iS469 + 1;
            _M0L6_2atmpS1210 = _M0L6outputS471 / 10ull;
            _M0L1iS469 = _M0L6_2atmpS1209;
            _M0L7currentS470 = _M0L7currentS472;
            _M0L6outputS471 = _M0L6_2atmpS1210;
            continue;
          }
          break;
        }
        _M0L6_2atmpS1219 = _M0Lm5indexS439;
        _M0L6_2atmpS1220 = _M0L7olengthS443 + 1;
        _M0Lm5indexS439 = _M0L6_2atmpS1219 + _M0L6_2atmpS1220;
      }
    }
    _M0L6_2atmpS1221 = _M0Lm5indexS439;
    #line 632 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _result_1408
    = _M0FPB19string__from__bytes(_M0L6resultS438, 0, _M0L6_2atmpS1221);
    moonbit_decref(_M0L6resultS438);
    return _result_1408;
  }
}

struct _M0TPB17FloatingDecimal64* _M0FPB3d2d(
  uint64_t _M0L12ieeeMantissaS384,
  uint32_t _M0L12ieeeExponentS383
) {
  int32_t _M0Lm2e2S381;
  uint64_t _M0Lm2m2S382;
  uint64_t _M0L6_2atmpS1095;
  uint64_t _M0L6_2atmpS1094;
  int32_t _M0L4evenS385;
  uint64_t _M0L6_2atmpS1093;
  uint64_t _M0L2mvS386;
  int32_t _M0L7mmShiftS387;
  uint64_t _M0Lm2vrS388;
  uint64_t _M0Lm2vpS389;
  uint64_t _M0Lm2vmS390;
  int32_t _M0Lm3e10S391;
  int32_t _M0Lm17vmIsTrailingZerosS392;
  int32_t _M0Lm17vrIsTrailingZerosS393;
  int32_t _M0L6_2atmpS995;
  int32_t _M0Lm7removedS412;
  int32_t _M0Lm16lastRemovedDigitS413;
  uint64_t _M0Lm6outputS414;
  int32_t _M0L6_2atmpS1091;
  int32_t _M0L6_2atmpS1092;
  int32_t _M0L3expS437;
  uint64_t _M0L6_2atmpS1090;
  struct _M0TPB17FloatingDecimal64* _block_1414;
  #line 347 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0Lm2e2S381 = 0;
  _M0Lm2m2S382 = 0ull;
  if (_M0L12ieeeExponentS383 == 0u) {
    _M0Lm2e2S381 = -1076;
    _M0Lm2m2S382 = _M0L12ieeeMantissaS384;
  } else {
    int32_t _M0L6_2atmpS994 = *(int32_t*)&_M0L12ieeeExponentS383;
    int32_t _M0L6_2atmpS993 = _M0L6_2atmpS994 - 1023;
    int32_t _M0L6_2atmpS992 = _M0L6_2atmpS993 - 52;
    _M0Lm2e2S381 = _M0L6_2atmpS992 - 2;
    _M0Lm2m2S382 = 4503599627370496ull | _M0L12ieeeMantissaS384;
  }
  _M0L6_2atmpS1095 = _M0Lm2m2S382;
  _M0L6_2atmpS1094 = _M0L6_2atmpS1095 & 1ull;
  _M0L4evenS385 = _M0L6_2atmpS1094 == 0ull;
  _M0L6_2atmpS1093 = _M0Lm2m2S382;
  _M0L2mvS386 = 4ull * _M0L6_2atmpS1093;
  _M0L7mmShiftS387
  = _M0L12ieeeMantissaS384 != 0ull || _M0L12ieeeExponentS383 <= 1u;
  _M0Lm2vrS388 = 0ull;
  _M0Lm2vpS389 = 0ull;
  _M0Lm2vmS390 = 0ull;
  _M0Lm3e10S391 = 0;
  _M0Lm17vmIsTrailingZerosS392 = 0;
  _M0Lm17vrIsTrailingZerosS393 = 0;
  _M0L6_2atmpS995 = _M0Lm2e2S381;
  if (_M0L6_2atmpS995 >= 0) {
    int32_t _M0L6_2atmpS1017 = _M0Lm2e2S381;
    int32_t _M0L6_2atmpS1013;
    int32_t _M0L6_2atmpS1016;
    int32_t _M0L6_2atmpS1015;
    int32_t _M0L6_2atmpS1014;
    int32_t _M0L1qS394;
    int32_t _M0L6_2atmpS1012;
    int32_t _M0L6_2atmpS1011;
    int32_t _M0L1kS395;
    int32_t _M0L6_2atmpS1010;
    int32_t _M0L6_2atmpS1009;
    int32_t _M0L6_2atmpS1008;
    int32_t _M0L1iS396;
    struct _M0TPB8Pow5Pair _M0L4pow5S397;
    uint64_t _M0L6_2atmpS1007;
    struct _M0TPB19MulShiftAll64Result _M0L7_2abindS398;
    uint64_t _M0L8_2avrOutS399;
    uint64_t _M0L8_2avpOutS400;
    uint64_t _M0L8_2avmOutS401;
    #line 383 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1013 = _M0FPB9log10Pow2(_M0L6_2atmpS1017);
    _M0L6_2atmpS1016 = _M0Lm2e2S381;
    _M0L6_2atmpS1015 = _M0L6_2atmpS1016 > 3;
    #line 383 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1014 = _M0MPC14bool4Bool7to__int(_M0L6_2atmpS1015);
    _M0L1qS394 = _M0L6_2atmpS1013 - _M0L6_2atmpS1014;
    _M0Lm3e10S391 = _M0L1qS394;
    #line 385 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1012 = _M0FPB8pow5bits(_M0L1qS394);
    _M0L6_2atmpS1011 = 125 + _M0L6_2atmpS1012;
    _M0L1kS395 = _M0L6_2atmpS1011 - 1;
    _M0L6_2atmpS1010 = _M0Lm2e2S381;
    _M0L6_2atmpS1009 = -_M0L6_2atmpS1010;
    _M0L6_2atmpS1008 = _M0L6_2atmpS1009 + _M0L1qS394;
    _M0L1iS396 = _M0L6_2atmpS1008 + _M0L1kS395;
    #line 387 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L4pow5S397 = _M0FPB22double__computeInvPow5(_M0L1qS394);
    _M0L6_2atmpS1007 = _M0Lm2m2S382;
    #line 388 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L7_2abindS398
    = _M0FPB13mulShiftAll64(_M0L6_2atmpS1007, _M0L4pow5S397, _M0L1iS396, _M0L7mmShiftS387);
    _M0L8_2avrOutS399 = _M0L7_2abindS398.$0;
    _M0L8_2avpOutS400 = _M0L7_2abindS398.$1;
    _M0L8_2avmOutS401 = _M0L7_2abindS398.$2;
    _M0Lm2vrS388 = _M0L8_2avrOutS399;
    _M0Lm2vpS389 = _M0L8_2avpOutS400;
    _M0Lm2vmS390 = _M0L8_2avmOutS401;
    if (_M0L1qS394 <= 21) {
      int32_t _M0L6_2atmpS1003 = (int32_t)_M0L2mvS386;
      uint64_t _M0L6_2atmpS1006 = _M0L2mvS386 / 5ull;
      int32_t _M0L6_2atmpS1005 = (int32_t)_M0L6_2atmpS1006;
      int32_t _M0L6_2atmpS1004 = 5 * _M0L6_2atmpS1005;
      int32_t _M0L6mvMod5S402 = _M0L6_2atmpS1003 - _M0L6_2atmpS1004;
      if (_M0L6mvMod5S402 == 0) {
        #line 400 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0Lm17vrIsTrailingZerosS393
        = _M0FPB18multipleOfPowerOf5(_M0L2mvS386, _M0L1qS394);
      } else if (_M0L4evenS385) {
        uint64_t _M0L6_2atmpS997 = _M0L2mvS386 - 1ull;
        uint64_t _M0L6_2atmpS998;
        uint64_t _M0L6_2atmpS996;
        #line 406 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0L6_2atmpS998 = _M0MPC14bool4Bool10to__uint64(_M0L7mmShiftS387);
        _M0L6_2atmpS996 = _M0L6_2atmpS997 - _M0L6_2atmpS998;
        #line 405 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0Lm17vmIsTrailingZerosS392
        = _M0FPB18multipleOfPowerOf5(_M0L6_2atmpS996, _M0L1qS394);
      } else {
        uint64_t _M0L6_2atmpS999 = _M0Lm2vpS389;
        uint64_t _M0L6_2atmpS1002 = _M0L2mvS386 + 2ull;
        int32_t _M0L6_2atmpS1001;
        uint64_t _M0L6_2atmpS1000;
        #line 410 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0L6_2atmpS1001
        = _M0FPB18multipleOfPowerOf5(_M0L6_2atmpS1002, _M0L1qS394);
        #line 410 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0L6_2atmpS1000 = _M0MPC14bool4Bool10to__uint64(_M0L6_2atmpS1001);
        _M0Lm2vpS389 = _M0L6_2atmpS999 - _M0L6_2atmpS1000;
      }
    }
  } else {
    int32_t _M0L6_2atmpS1031 = _M0Lm2e2S381;
    int32_t _M0L6_2atmpS1030 = -_M0L6_2atmpS1031;
    int32_t _M0L6_2atmpS1025;
    int32_t _M0L6_2atmpS1029;
    int32_t _M0L6_2atmpS1028;
    int32_t _M0L6_2atmpS1027;
    int32_t _M0L6_2atmpS1026;
    int32_t _M0L1qS403;
    int32_t _M0L6_2atmpS1018;
    int32_t _M0L6_2atmpS1024;
    int32_t _M0L6_2atmpS1023;
    int32_t _M0L1iS404;
    int32_t _M0L6_2atmpS1022;
    int32_t _M0L1kS405;
    int32_t _M0L1jS406;
    struct _M0TPB8Pow5Pair _M0L4pow5S407;
    uint64_t _M0L6_2atmpS1021;
    struct _M0TPB19MulShiftAll64Result _M0L7_2abindS408;
    uint64_t _M0L8_2avrOutS409;
    uint64_t _M0L8_2avpOutS410;
    uint64_t _M0L8_2avmOutS411;
    #line 415 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1025 = _M0FPB9log10Pow5(_M0L6_2atmpS1030);
    _M0L6_2atmpS1029 = _M0Lm2e2S381;
    _M0L6_2atmpS1028 = -_M0L6_2atmpS1029;
    _M0L6_2atmpS1027 = _M0L6_2atmpS1028 > 1;
    #line 415 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1026 = _M0MPC14bool4Bool7to__int(_M0L6_2atmpS1027);
    _M0L1qS403 = _M0L6_2atmpS1025 - _M0L6_2atmpS1026;
    _M0L6_2atmpS1018 = _M0Lm2e2S381;
    _M0Lm3e10S391 = _M0L1qS403 + _M0L6_2atmpS1018;
    _M0L6_2atmpS1024 = _M0Lm2e2S381;
    _M0L6_2atmpS1023 = -_M0L6_2atmpS1024;
    _M0L1iS404 = _M0L6_2atmpS1023 - _M0L1qS403;
    #line 418 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1022 = _M0FPB8pow5bits(_M0L1iS404);
    _M0L1kS405 = _M0L6_2atmpS1022 - 125;
    _M0L1jS406 = _M0L1qS403 - _M0L1kS405;
    #line 420 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L4pow5S407 = _M0FPB19double__computePow5(_M0L1iS404);
    _M0L6_2atmpS1021 = _M0Lm2m2S382;
    #line 421 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L7_2abindS408
    = _M0FPB13mulShiftAll64(_M0L6_2atmpS1021, _M0L4pow5S407, _M0L1jS406, _M0L7mmShiftS387);
    _M0L8_2avrOutS409 = _M0L7_2abindS408.$0;
    _M0L8_2avpOutS410 = _M0L7_2abindS408.$1;
    _M0L8_2avmOutS411 = _M0L7_2abindS408.$2;
    _M0Lm2vrS388 = _M0L8_2avrOutS409;
    _M0Lm2vpS389 = _M0L8_2avpOutS410;
    _M0Lm2vmS390 = _M0L8_2avmOutS411;
    if (_M0L1qS403 <= 1) {
      _M0Lm17vrIsTrailingZerosS393 = 1;
      if (_M0L4evenS385) {
        int32_t _M0L6_2atmpS1019;
        #line 432 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0L6_2atmpS1019 = _M0MPC14bool4Bool7to__int(_M0L7mmShiftS387);
        _M0Lm17vmIsTrailingZerosS392 = _M0L6_2atmpS1019 == 1;
      } else {
        uint64_t _M0L6_2atmpS1020 = _M0Lm2vpS389;
        _M0Lm2vpS389 = _M0L6_2atmpS1020 - 1ull;
      }
    } else if (_M0L1qS403 < 63) {
      #line 437 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0Lm17vrIsTrailingZerosS393
      = _M0FPB18multipleOfPowerOf2(_M0L2mvS386, _M0L1qS403);
    }
  }
  _M0Lm7removedS412 = 0;
  _M0Lm16lastRemovedDigitS413 = 0;
  _M0Lm6outputS414 = 0ull;
  if (_M0Lm17vmIsTrailingZerosS392 || _M0Lm17vrIsTrailingZerosS393) {
    int32_t _if__result_1411;
    uint64_t _M0L6_2atmpS1061;
    uint64_t _M0L6_2atmpS1067;
    uint64_t _M0L6_2atmpS1068;
    int32_t _if__result_1412;
    int32_t _M0L6_2atmpS1064;
    int64_t _M0L6_2atmpS1063;
    uint64_t _M0L6_2atmpS1062;
    while (1) {
      uint64_t _M0L6_2atmpS1044 = _M0Lm2vpS389;
      uint64_t _M0L7vpDiv10S415 = _M0L6_2atmpS1044 / 10ull;
      uint64_t _M0L6_2atmpS1043 = _M0Lm2vmS390;
      uint64_t _M0L7vmDiv10S416 = _M0L6_2atmpS1043 / 10ull;
      uint64_t _M0L6_2atmpS1042;
      int32_t _M0L6_2atmpS1039;
      int32_t _M0L6_2atmpS1041;
      int32_t _M0L6_2atmpS1040;
      int32_t _M0L7vmMod10S418;
      uint64_t _M0L6_2atmpS1038;
      uint64_t _M0L7vrDiv10S419;
      uint64_t _M0L6_2atmpS1037;
      int32_t _M0L6_2atmpS1034;
      int32_t _M0L6_2atmpS1036;
      int32_t _M0L6_2atmpS1035;
      int32_t _M0L7vrMod10S420;
      int32_t _M0L6_2atmpS1033;
      if (_M0L7vpDiv10S415 <= _M0L7vmDiv10S416) {
        break;
      }
      _M0L6_2atmpS1042 = _M0Lm2vmS390;
      _M0L6_2atmpS1039 = (int32_t)_M0L6_2atmpS1042;
      _M0L6_2atmpS1041 = (int32_t)_M0L7vmDiv10S416;
      _M0L6_2atmpS1040 = 10 * _M0L6_2atmpS1041;
      _M0L7vmMod10S418 = _M0L6_2atmpS1039 - _M0L6_2atmpS1040;
      _M0L6_2atmpS1038 = _M0Lm2vrS388;
      _M0L7vrDiv10S419 = _M0L6_2atmpS1038 / 10ull;
      _M0L6_2atmpS1037 = _M0Lm2vrS388;
      _M0L6_2atmpS1034 = (int32_t)_M0L6_2atmpS1037;
      _M0L6_2atmpS1036 = (int32_t)_M0L7vrDiv10S419;
      _M0L6_2atmpS1035 = 10 * _M0L6_2atmpS1036;
      _M0L7vrMod10S420 = _M0L6_2atmpS1034 - _M0L6_2atmpS1035;
      _M0Lm17vmIsTrailingZerosS392
      = _M0Lm17vmIsTrailingZerosS392 && _M0L7vmMod10S418 == 0;
      if (_M0Lm17vrIsTrailingZerosS393) {
        int32_t _M0L6_2atmpS1032 = _M0Lm16lastRemovedDigitS413;
        _M0Lm17vrIsTrailingZerosS393 = _M0L6_2atmpS1032 == 0;
      } else {
        _M0Lm17vrIsTrailingZerosS393 = 0;
      }
      _M0Lm16lastRemovedDigitS413 = _M0L7vrMod10S420;
      _M0Lm2vrS388 = _M0L7vrDiv10S419;
      _M0Lm2vpS389 = _M0L7vpDiv10S415;
      _M0Lm2vmS390 = _M0L7vmDiv10S416;
      _M0L6_2atmpS1033 = _M0Lm7removedS412;
      _M0Lm7removedS412 = _M0L6_2atmpS1033 + 1;
      continue;
      break;
    }
    if (_M0Lm17vmIsTrailingZerosS392) {
      while (1) {
        uint64_t _M0L6_2atmpS1057 = _M0Lm2vmS390;
        uint64_t _M0L7vmDiv10S421 = _M0L6_2atmpS1057 / 10ull;
        uint64_t _M0L6_2atmpS1056 = _M0Lm2vmS390;
        int32_t _M0L6_2atmpS1053 = (int32_t)_M0L6_2atmpS1056;
        int32_t _M0L6_2atmpS1055 = (int32_t)_M0L7vmDiv10S421;
        int32_t _M0L6_2atmpS1054 = 10 * _M0L6_2atmpS1055;
        int32_t _M0L7vmMod10S422 = _M0L6_2atmpS1053 - _M0L6_2atmpS1054;
        uint64_t _M0L6_2atmpS1052;
        uint64_t _M0L7vpDiv10S424;
        uint64_t _M0L6_2atmpS1051;
        uint64_t _M0L7vrDiv10S425;
        uint64_t _M0L6_2atmpS1050;
        int32_t _M0L6_2atmpS1047;
        int32_t _M0L6_2atmpS1049;
        int32_t _M0L6_2atmpS1048;
        int32_t _M0L7vrMod10S426;
        int32_t _M0L6_2atmpS1046;
        if (_M0L7vmMod10S422 != 0) {
          break;
        }
        _M0L6_2atmpS1052 = _M0Lm2vpS389;
        _M0L7vpDiv10S424 = _M0L6_2atmpS1052 / 10ull;
        _M0L6_2atmpS1051 = _M0Lm2vrS388;
        _M0L7vrDiv10S425 = _M0L6_2atmpS1051 / 10ull;
        _M0L6_2atmpS1050 = _M0Lm2vrS388;
        _M0L6_2atmpS1047 = (int32_t)_M0L6_2atmpS1050;
        _M0L6_2atmpS1049 = (int32_t)_M0L7vrDiv10S425;
        _M0L6_2atmpS1048 = 10 * _M0L6_2atmpS1049;
        _M0L7vrMod10S426 = _M0L6_2atmpS1047 - _M0L6_2atmpS1048;
        if (_M0Lm17vrIsTrailingZerosS393) {
          int32_t _M0L6_2atmpS1045 = _M0Lm16lastRemovedDigitS413;
          _M0Lm17vrIsTrailingZerosS393 = _M0L6_2atmpS1045 == 0;
        } else {
          _M0Lm17vrIsTrailingZerosS393 = 0;
        }
        _M0Lm16lastRemovedDigitS413 = _M0L7vrMod10S426;
        _M0Lm2vrS388 = _M0L7vrDiv10S425;
        _M0Lm2vpS389 = _M0L7vpDiv10S424;
        _M0Lm2vmS390 = _M0L7vmDiv10S421;
        _M0L6_2atmpS1046 = _M0Lm7removedS412;
        _M0Lm7removedS412 = _M0L6_2atmpS1046 + 1;
        continue;
        break;
      }
    }
    if (_M0Lm17vrIsTrailingZerosS393) {
      int32_t _M0L6_2atmpS1060 = _M0Lm16lastRemovedDigitS413;
      if (_M0L6_2atmpS1060 == 5) {
        uint64_t _M0L6_2atmpS1059 = _M0Lm2vrS388;
        uint64_t _M0L6_2atmpS1058 = _M0L6_2atmpS1059 % 2ull;
        _if__result_1411 = _M0L6_2atmpS1058 == 0ull;
      } else {
        _if__result_1411 = 0;
      }
    } else {
      _if__result_1411 = 0;
    }
    if (_if__result_1411) {
      _M0Lm16lastRemovedDigitS413 = 4;
    }
    _M0L6_2atmpS1061 = _M0Lm2vrS388;
    _M0L6_2atmpS1067 = _M0Lm2vrS388;
    _M0L6_2atmpS1068 = _M0Lm2vmS390;
    if (_M0L6_2atmpS1067 == _M0L6_2atmpS1068) {
      if (!_M0L4evenS385) {
        _if__result_1412 = 1;
      } else {
        int32_t _M0L6_2atmpS1066 = _M0Lm17vmIsTrailingZerosS392;
        _if__result_1412 = !_M0L6_2atmpS1066;
      }
    } else {
      _if__result_1412 = 0;
    }
    if (_if__result_1412) {
      _M0L6_2atmpS1064 = 1;
    } else {
      int32_t _M0L6_2atmpS1065 = _M0Lm16lastRemovedDigitS413;
      _M0L6_2atmpS1064 = _M0L6_2atmpS1065 >= 5;
    }
    #line 487 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1063 = _M0MPC14bool4Bool9to__int64(_M0L6_2atmpS1064);
    _M0L6_2atmpS1062 = *(uint64_t*)&_M0L6_2atmpS1063;
    _M0Lm6outputS414 = _M0L6_2atmpS1061 + _M0L6_2atmpS1062;
  } else {
    int32_t _M0Lm7roundUpS427 = 0;
    uint64_t _M0L6_2atmpS1089 = _M0Lm2vpS389;
    uint64_t _M0L8vpDiv100S428 = _M0L6_2atmpS1089 / 100ull;
    uint64_t _M0L6_2atmpS1088 = _M0Lm2vmS390;
    uint64_t _M0L8vmDiv100S429 = _M0L6_2atmpS1088 / 100ull;
    uint64_t _M0L6_2atmpS1083;
    uint64_t _M0L6_2atmpS1086;
    uint64_t _M0L6_2atmpS1087;
    int32_t _M0L6_2atmpS1085;
    uint64_t _M0L6_2atmpS1084;
    if (_M0L8vpDiv100S428 > _M0L8vmDiv100S429) {
      uint64_t _M0L6_2atmpS1074 = _M0Lm2vrS388;
      uint64_t _M0L8vrDiv100S430 = _M0L6_2atmpS1074 / 100ull;
      uint64_t _M0L6_2atmpS1073 = _M0Lm2vrS388;
      int32_t _M0L6_2atmpS1070 = (int32_t)_M0L6_2atmpS1073;
      int32_t _M0L6_2atmpS1072 = (int32_t)_M0L8vrDiv100S430;
      int32_t _M0L6_2atmpS1071 = 100 * _M0L6_2atmpS1072;
      int32_t _M0L8vrMod100S431 = _M0L6_2atmpS1070 - _M0L6_2atmpS1071;
      int32_t _M0L6_2atmpS1069;
      _M0Lm7roundUpS427 = _M0L8vrMod100S431 >= 50;
      _M0Lm2vrS388 = _M0L8vrDiv100S430;
      _M0Lm2vpS389 = _M0L8vpDiv100S428;
      _M0Lm2vmS390 = _M0L8vmDiv100S429;
      _M0L6_2atmpS1069 = _M0Lm7removedS412;
      _M0Lm7removedS412 = _M0L6_2atmpS1069 + 2;
    }
    while (1) {
      uint64_t _M0L6_2atmpS1082 = _M0Lm2vpS389;
      uint64_t _M0L7vpDiv10S432 = _M0L6_2atmpS1082 / 10ull;
      uint64_t _M0L6_2atmpS1081 = _M0Lm2vmS390;
      uint64_t _M0L7vmDiv10S433 = _M0L6_2atmpS1081 / 10ull;
      uint64_t _M0L6_2atmpS1080;
      uint64_t _M0L7vrDiv10S435;
      uint64_t _M0L6_2atmpS1079;
      int32_t _M0L6_2atmpS1076;
      int32_t _M0L6_2atmpS1078;
      int32_t _M0L6_2atmpS1077;
      int32_t _M0L7vrMod10S436;
      int32_t _M0L6_2atmpS1075;
      if (_M0L7vpDiv10S432 <= _M0L7vmDiv10S433) {
        break;
      }
      _M0L6_2atmpS1080 = _M0Lm2vrS388;
      _M0L7vrDiv10S435 = _M0L6_2atmpS1080 / 10ull;
      _M0L6_2atmpS1079 = _M0Lm2vrS388;
      _M0L6_2atmpS1076 = (int32_t)_M0L6_2atmpS1079;
      _M0L6_2atmpS1078 = (int32_t)_M0L7vrDiv10S435;
      _M0L6_2atmpS1077 = 10 * _M0L6_2atmpS1078;
      _M0L7vrMod10S436 = _M0L6_2atmpS1076 - _M0L6_2atmpS1077;
      _M0Lm7roundUpS427 = _M0L7vrMod10S436 >= 5;
      _M0Lm2vrS388 = _M0L7vrDiv10S435;
      _M0Lm2vpS389 = _M0L7vpDiv10S432;
      _M0Lm2vmS390 = _M0L7vmDiv10S433;
      _M0L6_2atmpS1075 = _M0Lm7removedS412;
      _M0Lm7removedS412 = _M0L6_2atmpS1075 + 1;
      continue;
      break;
    }
    _M0L6_2atmpS1083 = _M0Lm2vrS388;
    _M0L6_2atmpS1086 = _M0Lm2vrS388;
    _M0L6_2atmpS1087 = _M0Lm2vmS390;
    _M0L6_2atmpS1085
    = _M0L6_2atmpS1086 == _M0L6_2atmpS1087 || _M0Lm7roundUpS427;
    #line 522 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1084 = _M0MPC14bool4Bool10to__uint64(_M0L6_2atmpS1085);
    _M0Lm6outputS414 = _M0L6_2atmpS1083 + _M0L6_2atmpS1084;
  }
  _M0L6_2atmpS1091 = _M0Lm3e10S391;
  _M0L6_2atmpS1092 = _M0Lm7removedS412;
  _M0L3expS437 = _M0L6_2atmpS1091 + _M0L6_2atmpS1092;
  _M0L6_2atmpS1090 = _M0Lm6outputS414;
  _block_1414
  = (struct _M0TPB17FloatingDecimal64*)moonbit_malloc(sizeof(struct _M0TPB17FloatingDecimal64));
  Moonbit_object_header(_block_1414)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_1414->$0 = _M0L6_2atmpS1090;
  _block_1414->$1 = _M0L3expS437;
  return _block_1414;
}

uint64_t _M0MPC14bool4Bool10to__uint64(int32_t _M0L4selfS380) {
  #line 110 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bool.mbt"
  if (_M0L4selfS380) {
    return 1ull;
  } else {
    return 0ull;
  }
}

int64_t _M0MPC14bool4Bool9to__int64(int32_t _M0L4selfS379) {
  #line 58 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bool.mbt"
  if (_M0L4selfS379) {
    return 1ll;
  } else {
    return 0ll;
  }
}

int32_t _M0MPC14bool4Bool7to__int(int32_t _M0L4selfS378) {
  #line 32 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bool.mbt"
  if (_M0L4selfS378) {
    return 1;
  } else {
    return 0;
  }
}

int32_t _M0FPB17decimal__length17(uint64_t _M0L1vS377) {
  #line 280 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  if (_M0L1vS377 >= 10000000000000000ull) {
    return 17;
  }
  if (_M0L1vS377 >= 1000000000000000ull) {
    return 16;
  }
  if (_M0L1vS377 >= 100000000000000ull) {
    return 15;
  }
  if (_M0L1vS377 >= 10000000000000ull) {
    return 14;
  }
  if (_M0L1vS377 >= 1000000000000ull) {
    return 13;
  }
  if (_M0L1vS377 >= 100000000000ull) {
    return 12;
  }
  if (_M0L1vS377 >= 10000000000ull) {
    return 11;
  }
  if (_M0L1vS377 >= 1000000000ull) {
    return 10;
  }
  if (_M0L1vS377 >= 100000000ull) {
    return 9;
  }
  if (_M0L1vS377 >= 10000000ull) {
    return 8;
  }
  if (_M0L1vS377 >= 1000000ull) {
    return 7;
  }
  if (_M0L1vS377 >= 100000ull) {
    return 6;
  }
  if (_M0L1vS377 >= 10000ull) {
    return 5;
  }
  if (_M0L1vS377 >= 1000ull) {
    return 4;
  }
  if (_M0L1vS377 >= 100ull) {
    return 3;
  }
  if (_M0L1vS377 >= 10ull) {
    return 2;
  }
  return 1;
}

struct _M0TPB8Pow5Pair _M0FPB22double__computeInvPow5(int32_t _M0L1iS360) {
  int32_t _M0L6_2atmpS991;
  int32_t _M0L6_2atmpS990;
  int32_t _M0L4baseS359;
  int32_t _M0L5base2S361;
  int32_t _M0L6offsetS362;
  int32_t _M0L6_2atmpS989;
  uint64_t _M0L4mul0S363;
  int32_t _M0L6_2atmpS988;
  int32_t _M0L6_2atmpS987;
  uint64_t _M0L4mul1S364;
  uint64_t _M0L1mS365;
  struct _M0TPB7Umul128 _M0L7_2abindS366;
  uint64_t _M0L7_2alow1S367;
  uint64_t _M0L8_2ahigh1S368;
  struct _M0TPB7Umul128 _M0L7_2abindS369;
  uint64_t _M0L7_2alow0S370;
  uint64_t _M0L8_2ahigh0S371;
  uint64_t _M0L3sumS372;
  uint64_t _M0Lm5high1S373;
  int32_t _M0L6_2atmpS985;
  int32_t _M0L6_2atmpS986;
  int32_t _M0L5deltaS374;
  uint64_t _M0L6_2atmpS984;
  uint64_t _M0L6_2atmpS976;
  int32_t _M0L6_2atmpS983;
  uint32_t _M0L6_2atmpS980;
  int32_t _M0L6_2atmpS982;
  int32_t _M0L6_2atmpS981;
  uint32_t _M0L6_2atmpS979;
  uint32_t _M0L6_2atmpS978;
  uint64_t _M0L6_2atmpS977;
  uint64_t _M0L1aS375;
  uint64_t _M0L6_2atmpS975;
  uint64_t _M0L1bS376;
  #line 239 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS991 = _M0L1iS360 + 26;
  _M0L6_2atmpS990 = _M0L6_2atmpS991 - 1;
  _M0L4baseS359 = _M0L6_2atmpS990 / 26;
  _M0L5base2S361 = _M0L4baseS359 * 26;
  _M0L6offsetS362 = _M0L5base2S361 - _M0L1iS360;
  _M0L6_2atmpS989 = _M0L4baseS359 * 2;
  #line 243 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4mul0S363
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB26gDOUBLE__POW5__INV__SPLIT2, _M0L6_2atmpS989);
  _M0L6_2atmpS988 = _M0L4baseS359 * 2;
  _M0L6_2atmpS987 = _M0L6_2atmpS988 + 1;
  #line 244 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4mul1S364
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB26gDOUBLE__POW5__INV__SPLIT2, _M0L6_2atmpS987);
  if (_M0L6offsetS362 == 0) {
    return (struct _M0TPB8Pow5Pair){.$0 = _M0L4mul0S363, .$1 = _M0L4mul1S364};
  }
  #line 248 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L1mS365
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB20gDOUBLE__POW5__TABLE, _M0L6offsetS362);
  #line 249 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS366 = _M0FPB7umul128(_M0L1mS365, _M0L4mul1S364);
  _M0L7_2alow1S367 = _M0L7_2abindS366.$0;
  _M0L8_2ahigh1S368 = _M0L7_2abindS366.$1;
  #line 250 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS369 = _M0FPB7umul128(_M0L1mS365, _M0L4mul0S363);
  _M0L7_2alow0S370 = _M0L7_2abindS369.$0;
  _M0L8_2ahigh0S371 = _M0L7_2abindS369.$1;
  _M0L3sumS372 = _M0L8_2ahigh0S371 + _M0L7_2alow1S367;
  _M0Lm5high1S373 = _M0L8_2ahigh1S368;
  if (_M0L3sumS372 < _M0L8_2ahigh0S371) {
    uint64_t _M0L6_2atmpS974 = _M0Lm5high1S373;
    _M0Lm5high1S373 = _M0L6_2atmpS974 + 1ull;
  }
  #line 256 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS985 = _M0FPB8pow5bits(_M0L5base2S361);
  #line 256 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS986 = _M0FPB8pow5bits(_M0L1iS360);
  _M0L5deltaS374 = _M0L6_2atmpS985 - _M0L6_2atmpS986;
  #line 257 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS984
  = _M0FPB13shiftright128(_M0L7_2alow0S370, _M0L3sumS372, _M0L5deltaS374);
  _M0L6_2atmpS976 = _M0L6_2atmpS984 + 1ull;
  _M0L6_2atmpS983 = _M0L1iS360 / 16;
  #line 259 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS980
  = _M0MPC15array13ReadOnlyArray2atGjE(_M0FPB19gPOW5__INV__OFFSETS, _M0L6_2atmpS983);
  _M0L6_2atmpS982 = _M0L1iS360 % 16;
  _M0L6_2atmpS981 = _M0L6_2atmpS982 << 1;
  _M0L6_2atmpS979 = _M0L6_2atmpS980 >> (_M0L6_2atmpS981 & 31);
  _M0L6_2atmpS978 = _M0L6_2atmpS979 & 3u;
  #line 259 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS977 = _M0MPC14uint4UInt10to__uint64(_M0L6_2atmpS978);
  _M0L1aS375 = _M0L6_2atmpS976 + _M0L6_2atmpS977;
  _M0L6_2atmpS975 = _M0Lm5high1S373;
  #line 260 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L1bS376
  = _M0FPB13shiftright128(_M0L3sumS372, _M0L6_2atmpS975, _M0L5deltaS374);
  return (struct _M0TPB8Pow5Pair){.$0 = _M0L1aS375, .$1 = _M0L1bS376};
}

struct _M0TPB8Pow5Pair _M0FPB19double__computePow5(int32_t _M0L1iS342) {
  int32_t _M0L4baseS341;
  int32_t _M0L5base2S343;
  int32_t _M0L6offsetS344;
  int32_t _M0L6_2atmpS973;
  uint64_t _M0L4mul0S345;
  int32_t _M0L6_2atmpS972;
  int32_t _M0L6_2atmpS971;
  uint64_t _M0L4mul1S346;
  uint64_t _M0L1mS347;
  struct _M0TPB7Umul128 _M0L7_2abindS348;
  uint64_t _M0L7_2alow1S349;
  uint64_t _M0L8_2ahigh1S350;
  struct _M0TPB7Umul128 _M0L7_2abindS351;
  uint64_t _M0L7_2alow0S352;
  uint64_t _M0L8_2ahigh0S353;
  uint64_t _M0L3sumS354;
  uint64_t _M0Lm5high1S355;
  int32_t _M0L6_2atmpS969;
  int32_t _M0L6_2atmpS970;
  int32_t _M0L5deltaS356;
  uint64_t _M0L6_2atmpS961;
  int32_t _M0L6_2atmpS968;
  uint32_t _M0L6_2atmpS965;
  int32_t _M0L6_2atmpS967;
  int32_t _M0L6_2atmpS966;
  uint32_t _M0L6_2atmpS964;
  uint32_t _M0L6_2atmpS963;
  uint64_t _M0L6_2atmpS962;
  uint64_t _M0L1aS357;
  uint64_t _M0L6_2atmpS960;
  uint64_t _M0L1bS358;
  #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4baseS341 = _M0L1iS342 / 26;
  _M0L5base2S343 = _M0L4baseS341 * 26;
  _M0L6offsetS344 = _M0L1iS342 - _M0L5base2S343;
  _M0L6_2atmpS973 = _M0L4baseS341 * 2;
  #line 217 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4mul0S345
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB21gDOUBLE__POW5__SPLIT2, _M0L6_2atmpS973);
  _M0L6_2atmpS972 = _M0L4baseS341 * 2;
  _M0L6_2atmpS971 = _M0L6_2atmpS972 + 1;
  #line 218 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4mul1S346
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB21gDOUBLE__POW5__SPLIT2, _M0L6_2atmpS971);
  if (_M0L6offsetS344 == 0) {
    return (struct _M0TPB8Pow5Pair){.$0 = _M0L4mul0S345, .$1 = _M0L4mul1S346};
  }
  #line 222 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L1mS347
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB20gDOUBLE__POW5__TABLE, _M0L6offsetS344);
  #line 223 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS348 = _M0FPB7umul128(_M0L1mS347, _M0L4mul1S346);
  _M0L7_2alow1S349 = _M0L7_2abindS348.$0;
  _M0L8_2ahigh1S350 = _M0L7_2abindS348.$1;
  #line 224 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS351 = _M0FPB7umul128(_M0L1mS347, _M0L4mul0S345);
  _M0L7_2alow0S352 = _M0L7_2abindS351.$0;
  _M0L8_2ahigh0S353 = _M0L7_2abindS351.$1;
  _M0L3sumS354 = _M0L8_2ahigh0S353 + _M0L7_2alow1S349;
  _M0Lm5high1S355 = _M0L8_2ahigh1S350;
  if (_M0L3sumS354 < _M0L8_2ahigh0S353) {
    uint64_t _M0L6_2atmpS959 = _M0Lm5high1S355;
    _M0Lm5high1S355 = _M0L6_2atmpS959 + 1ull;
  }
  #line 230 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS969 = _M0FPB8pow5bits(_M0L1iS342);
  #line 230 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS970 = _M0FPB8pow5bits(_M0L5base2S343);
  _M0L5deltaS356 = _M0L6_2atmpS969 - _M0L6_2atmpS970;
  #line 231 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS961
  = _M0FPB13shiftright128(_M0L7_2alow0S352, _M0L3sumS354, _M0L5deltaS356);
  _M0L6_2atmpS968 = _M0L1iS342 / 16;
  #line 232 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS965
  = _M0MPC15array13ReadOnlyArray2atGjE(_M0FPB14gPOW5__OFFSETS, _M0L6_2atmpS968);
  _M0L6_2atmpS967 = _M0L1iS342 % 16;
  _M0L6_2atmpS966 = _M0L6_2atmpS967 << 1;
  _M0L6_2atmpS964 = _M0L6_2atmpS965 >> (_M0L6_2atmpS966 & 31);
  _M0L6_2atmpS963 = _M0L6_2atmpS964 & 3u;
  #line 232 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS962 = _M0MPC14uint4UInt10to__uint64(_M0L6_2atmpS963);
  _M0L1aS357 = _M0L6_2atmpS961 + _M0L6_2atmpS962;
  _M0L6_2atmpS960 = _M0Lm5high1S355;
  #line 233 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L1bS358
  = _M0FPB13shiftright128(_M0L3sumS354, _M0L6_2atmpS960, _M0L5deltaS356);
  return (struct _M0TPB8Pow5Pair){.$0 = _M0L1aS357, .$1 = _M0L1bS358};
}

struct _M0TPB19MulShiftAll64Result _M0FPB13mulShiftAll64(
  uint64_t _M0L1mS315,
  struct _M0TPB8Pow5Pair _M0L3mulS312,
  int32_t _M0L1jS328,
  int32_t _M0L7mmShiftS330
) {
  uint64_t _M0L7_2amul0S311;
  uint64_t _M0L7_2amul1S313;
  uint64_t _M0L1mS314;
  struct _M0TPB7Umul128 _M0L7_2abindS316;
  uint64_t _M0L5_2aloS317;
  uint64_t _M0L6_2atmpS318;
  struct _M0TPB7Umul128 _M0L7_2abindS319;
  uint64_t _M0L6_2alo2S320;
  uint64_t _M0L6_2ahi2S321;
  uint64_t _M0L3midS322;
  uint64_t _M0L6_2atmpS958;
  uint64_t _M0L2hiS323;
  uint64_t _M0L3lo2S324;
  uint64_t _M0L6_2atmpS956;
  uint64_t _M0L6_2atmpS957;
  uint64_t _M0L4mid2S325;
  uint64_t _M0L6_2atmpS955;
  uint64_t _M0L3hi2S326;
  int32_t _M0L6_2atmpS954;
  int32_t _M0L6_2atmpS953;
  uint64_t _M0L2vpS327;
  uint64_t _M0Lm2vmS329;
  int32_t _M0L6_2atmpS952;
  int32_t _M0L6_2atmpS951;
  uint64_t _M0L2vrS340;
  uint64_t _M0L6_2atmpS950;
  #line 129 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2amul0S311 = _M0L3mulS312.$0;
  _M0L7_2amul1S313 = _M0L3mulS312.$1;
  _M0L1mS314 = _M0L1mS315 << 1;
  #line 137 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS316 = _M0FPB7umul128(_M0L1mS314, _M0L7_2amul0S311);
  _M0L5_2aloS317 = _M0L7_2abindS316.$0;
  _M0L6_2atmpS318 = _M0L7_2abindS316.$1;
  #line 138 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS319 = _M0FPB7umul128(_M0L1mS314, _M0L7_2amul1S313);
  _M0L6_2alo2S320 = _M0L7_2abindS319.$0;
  _M0L6_2ahi2S321 = _M0L7_2abindS319.$1;
  _M0L3midS322 = _M0L6_2atmpS318 + _M0L6_2alo2S320;
  if (_M0L3midS322 < _M0L6_2atmpS318) {
    _M0L6_2atmpS958 = 1ull;
  } else {
    _M0L6_2atmpS958 = 0ull;
  }
  _M0L2hiS323 = _M0L6_2ahi2S321 + _M0L6_2atmpS958;
  _M0L3lo2S324 = _M0L5_2aloS317 + _M0L7_2amul0S311;
  _M0L6_2atmpS956 = _M0L3midS322 + _M0L7_2amul1S313;
  if (_M0L3lo2S324 < _M0L5_2aloS317) {
    _M0L6_2atmpS957 = 1ull;
  } else {
    _M0L6_2atmpS957 = 0ull;
  }
  _M0L4mid2S325 = _M0L6_2atmpS956 + _M0L6_2atmpS957;
  if (_M0L4mid2S325 < _M0L3midS322) {
    _M0L6_2atmpS955 = 1ull;
  } else {
    _M0L6_2atmpS955 = 0ull;
  }
  _M0L3hi2S326 = _M0L2hiS323 + _M0L6_2atmpS955;
  _M0L6_2atmpS954 = _M0L1jS328 - 64;
  _M0L6_2atmpS953 = _M0L6_2atmpS954 - 1;
  #line 144 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L2vpS327
  = _M0FPB13shiftright128(_M0L4mid2S325, _M0L3hi2S326, _M0L6_2atmpS953);
  _M0Lm2vmS329 = 0ull;
  if (_M0L7mmShiftS330) {
    uint64_t _M0L3lo3S331 = _M0L5_2aloS317 - _M0L7_2amul0S311;
    uint64_t _M0L6_2atmpS940 = _M0L3midS322 - _M0L7_2amul1S313;
    uint64_t _M0L6_2atmpS941;
    uint64_t _M0L4mid3S332;
    uint64_t _M0L6_2atmpS939;
    uint64_t _M0L3hi3S333;
    int32_t _M0L6_2atmpS938;
    int32_t _M0L6_2atmpS937;
    if (_M0L5_2aloS317 < _M0L3lo3S331) {
      _M0L6_2atmpS941 = 1ull;
    } else {
      _M0L6_2atmpS941 = 0ull;
    }
    _M0L4mid3S332 = _M0L6_2atmpS940 - _M0L6_2atmpS941;
    if (_M0L3midS322 < _M0L4mid3S332) {
      _M0L6_2atmpS939 = 1ull;
    } else {
      _M0L6_2atmpS939 = 0ull;
    }
    _M0L3hi3S333 = _M0L2hiS323 - _M0L6_2atmpS939;
    _M0L6_2atmpS938 = _M0L1jS328 - 64;
    _M0L6_2atmpS937 = _M0L6_2atmpS938 - 1;
    #line 150 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0Lm2vmS329
    = _M0FPB13shiftright128(_M0L4mid3S332, _M0L3hi3S333, _M0L6_2atmpS937);
  } else {
    uint64_t _M0L3lo3S334 = _M0L5_2aloS317 + _M0L5_2aloS317;
    uint64_t _M0L6_2atmpS948 = _M0L3midS322 + _M0L3midS322;
    uint64_t _M0L6_2atmpS949;
    uint64_t _M0L4mid3S335;
    uint64_t _M0L6_2atmpS946;
    uint64_t _M0L6_2atmpS947;
    uint64_t _M0L3hi3S336;
    uint64_t _M0L3lo4S337;
    uint64_t _M0L6_2atmpS944;
    uint64_t _M0L6_2atmpS945;
    uint64_t _M0L4mid4S338;
    uint64_t _M0L6_2atmpS943;
    uint64_t _M0L3hi4S339;
    int32_t _M0L6_2atmpS942;
    if (_M0L3lo3S334 < _M0L5_2aloS317) {
      _M0L6_2atmpS949 = 1ull;
    } else {
      _M0L6_2atmpS949 = 0ull;
    }
    _M0L4mid3S335 = _M0L6_2atmpS948 + _M0L6_2atmpS949;
    _M0L6_2atmpS946 = _M0L2hiS323 + _M0L2hiS323;
    if (_M0L4mid3S335 < _M0L3midS322) {
      _M0L6_2atmpS947 = 1ull;
    } else {
      _M0L6_2atmpS947 = 0ull;
    }
    _M0L3hi3S336 = _M0L6_2atmpS946 + _M0L6_2atmpS947;
    _M0L3lo4S337 = _M0L3lo3S334 - _M0L7_2amul0S311;
    _M0L6_2atmpS944 = _M0L4mid3S335 - _M0L7_2amul1S313;
    if (_M0L3lo3S334 < _M0L3lo4S337) {
      _M0L6_2atmpS945 = 1ull;
    } else {
      _M0L6_2atmpS945 = 0ull;
    }
    _M0L4mid4S338 = _M0L6_2atmpS944 - _M0L6_2atmpS945;
    if (_M0L4mid3S335 < _M0L4mid4S338) {
      _M0L6_2atmpS943 = 1ull;
    } else {
      _M0L6_2atmpS943 = 0ull;
    }
    _M0L3hi4S339 = _M0L3hi3S336 - _M0L6_2atmpS943;
    _M0L6_2atmpS942 = _M0L1jS328 - 64;
    #line 158 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0Lm2vmS329
    = _M0FPB13shiftright128(_M0L4mid4S338, _M0L3hi4S339, _M0L6_2atmpS942);
  }
  _M0L6_2atmpS952 = _M0L1jS328 - 64;
  _M0L6_2atmpS951 = _M0L6_2atmpS952 - 1;
  #line 160 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L2vrS340
  = _M0FPB13shiftright128(_M0L3midS322, _M0L2hiS323, _M0L6_2atmpS951);
  _M0L6_2atmpS950 = _M0Lm2vmS329;
  return (struct _M0TPB19MulShiftAll64Result){.$0 = _M0L2vrS340,
                                                .$1 = _M0L2vpS327,
                                                .$2 = _M0L6_2atmpS950};
}

int32_t _M0FPB18multipleOfPowerOf2(
  uint64_t _M0L5valueS309,
  int32_t _M0L1pS310
) {
  uint64_t _M0L6_2atmpS936;
  uint64_t _M0L6_2atmpS935;
  uint64_t _M0L6_2atmpS934;
  #line 124 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS936 = 1ull << (_M0L1pS310 & 63);
  _M0L6_2atmpS935 = _M0L6_2atmpS936 - 1ull;
  _M0L6_2atmpS934 = _M0L5valueS309 & _M0L6_2atmpS935;
  return _M0L6_2atmpS934 == 0ull;
}

int32_t _M0FPB18multipleOfPowerOf5(
  uint64_t _M0L5valueS307,
  int32_t _M0L1pS308
) {
  int32_t _M0L6_2atmpS933;
  #line 119 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  #line 120 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS933 = _M0FPB10pow5Factor(_M0L5valueS307);
  return _M0L6_2atmpS933 >= _M0L1pS308;
}

int32_t _M0FPB10pow5Factor(uint64_t _M0L5valueS302) {
  uint64_t _M0L6_2atmpS924;
  uint64_t _M0L6_2atmpS925;
  uint64_t _M0L6_2atmpS926;
  uint64_t _M0L6_2atmpS927;
  uint64_t _M0L6_2atmpS932;
  int32_t _M0L5countS303;
  uint64_t _M0L1vS304;
  #line 94 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS924 = _M0L5valueS302 % 5ull;
  if (_M0L6_2atmpS924 != 0ull) {
    return 0;
  }
  _M0L6_2atmpS925 = _M0L5valueS302 % 25ull;
  if (_M0L6_2atmpS925 != 0ull) {
    return 1;
  }
  _M0L6_2atmpS926 = _M0L5valueS302 % 125ull;
  if (_M0L6_2atmpS926 != 0ull) {
    return 2;
  }
  _M0L6_2atmpS927 = _M0L5valueS302 % 625ull;
  if (_M0L6_2atmpS927 != 0ull) {
    return 3;
  }
  _M0L6_2atmpS932 = _M0L5valueS302 / 625ull;
  _M0L5countS303 = 4;
  _M0L1vS304 = _M0L6_2atmpS932;
  while (1) {
    if (_M0L1vS304 > 0ull) {
      uint64_t _M0L6_2atmpS928 = _M0L1vS304 % 5ull;
      int32_t _M0L6_2atmpS929;
      uint64_t _M0L6_2atmpS930;
      if (_M0L6_2atmpS928 != 0ull) {
        return _M0L5countS303;
      }
      _M0L6_2atmpS929 = _M0L5countS303 + 1;
      _M0L6_2atmpS930 = _M0L1vS304 / 5ull;
      _M0L5countS303 = _M0L6_2atmpS929;
      _M0L1vS304 = _M0L6_2atmpS930;
      continue;
    } else {
      struct _M0TPB13StringBuilder* _M0L18_2astring__builderS306;
      moonbit_string_t _M0L6_2atmpS931;
      int32_t _result_1416;
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0L18_2astring__builderS306
      = _M0MPB13StringBuilder21StringBuilder_2einner(25);
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS306, (moonbit_string_t)moonbit_string_literal_1.data);
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0MPB13StringBuilder13write__objectGmE(_M0L18_2astring__builderS306, _M0L5valueS302);
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0L6_2atmpS931
      = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS306);
      moonbit_decref(_M0L18_2astring__builderS306);
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _result_1416 = _M0FPC15abort5abortGiE(_M0L6_2atmpS931);
      moonbit_decref(_M0L6_2atmpS931);
      return _result_1416;
    }
    break;
  }
}

uint64_t _M0FPB13shiftright128(
  uint64_t _M0L2loS301,
  uint64_t _M0L2hiS299,
  int32_t _M0L4distS300
) {
  int32_t _M0L6_2atmpS923;
  uint64_t _M0L6_2atmpS921;
  uint64_t _M0L6_2atmpS922;
  #line 89 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS923 = 64 - _M0L4distS300;
  _M0L6_2atmpS921 = _M0L2hiS299 << (_M0L6_2atmpS923 & 63);
  _M0L6_2atmpS922 = _M0L2loS301 >> (_M0L4distS300 & 63);
  return _M0L6_2atmpS921 | _M0L6_2atmpS922;
}

struct _M0TPB7Umul128 _M0FPB7umul128(
  uint64_t _M0L1aS289,
  uint64_t _M0L1bS292
) {
  uint64_t _M0L3aLoS288;
  uint64_t _M0L3aHiS290;
  uint64_t _M0L3bLoS291;
  uint64_t _M0L3bHiS293;
  uint64_t _M0L1xS294;
  uint64_t _M0L6_2atmpS919;
  uint64_t _M0L6_2atmpS920;
  uint64_t _M0L1yS295;
  uint64_t _M0L6_2atmpS917;
  uint64_t _M0L6_2atmpS918;
  uint64_t _M0L1zS296;
  uint64_t _M0L6_2atmpS915;
  uint64_t _M0L6_2atmpS916;
  uint64_t _M0L6_2atmpS913;
  uint64_t _M0L6_2atmpS914;
  uint64_t _M0L1wS297;
  uint64_t _M0L2loS298;
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L3aLoS288 = _M0L1aS289 & 4294967295ull;
  _M0L3aHiS290 = _M0L1aS289 >> 32;
  _M0L3bLoS291 = _M0L1bS292 & 4294967295ull;
  _M0L3bHiS293 = _M0L1bS292 >> 32;
  _M0L1xS294 = _M0L3aLoS288 * _M0L3bLoS291;
  _M0L6_2atmpS919 = _M0L3aHiS290 * _M0L3bLoS291;
  _M0L6_2atmpS920 = _M0L1xS294 >> 32;
  _M0L1yS295 = _M0L6_2atmpS919 + _M0L6_2atmpS920;
  _M0L6_2atmpS917 = _M0L3aLoS288 * _M0L3bHiS293;
  _M0L6_2atmpS918 = _M0L1yS295 & 4294967295ull;
  _M0L1zS296 = _M0L6_2atmpS917 + _M0L6_2atmpS918;
  _M0L6_2atmpS915 = _M0L3aHiS290 * _M0L3bHiS293;
  _M0L6_2atmpS916 = _M0L1yS295 >> 32;
  _M0L6_2atmpS913 = _M0L6_2atmpS915 + _M0L6_2atmpS916;
  _M0L6_2atmpS914 = _M0L1zS296 >> 32;
  _M0L1wS297 = _M0L6_2atmpS913 + _M0L6_2atmpS914;
  _M0L2loS298 = _M0L1aS289 * _M0L1bS292;
  return (struct _M0TPB7Umul128){.$0 = _M0L2loS298, .$1 = _M0L1wS297};
}

moonbit_string_t _M0FPB19string__from__bytes(
  moonbit_bytes_t _M0L5bytesS286,
  int32_t _M0L4fromS283,
  int32_t _M0L2toS282
) {
  int32_t _M0L3lenS281;
  int32_t _M0L6_2atmpS912;
  uint16_t* _M0L6bufferS284;
  int32_t _M0L1iS285;
  #line 52 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L3lenS281 = _M0L2toS282 - _M0L4fromS283;
  #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS912 = _M0IPC16uint166UInt16PB7Default7default();
  _M0L6bufferS284
  = (uint16_t*)moonbit_make_string(_M0L3lenS281, _M0L6_2atmpS912);
  _M0L1iS285 = 0;
  while (1) {
    if (_M0L1iS285 < _M0L3lenS281) {
      int32_t _M0L6_2atmpS910 = _M0L4fromS283 + _M0L1iS285;
      int32_t _M0L6_2atmpS909;
      int32_t _M0L6_2atmpS908;
      int32_t _M0L6_2atmpS911;
      if (
        _M0L6_2atmpS910 < 0
        || _M0L6_2atmpS910 >= Moonbit_array_length(_M0L5bytesS286)
      ) {
        #line 56 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6_2atmpS909 = (int32_t)_M0L5bytesS286[_M0L6_2atmpS910];
      _M0L6_2atmpS908 = (uint16_t)_M0L6_2atmpS909;
      if (
        _M0L1iS285 < 0 || _M0L1iS285 >= Moonbit_array_length(_M0L6bufferS284)
      ) {
        #line 56 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6bufferS284[_M0L1iS285] = _M0L6_2atmpS908;
      _M0L6_2atmpS911 = _M0L1iS285 + 1;
      _M0L1iS285 = _M0L6_2atmpS911;
      continue;
    }
    break;
  }
  return _M0L6bufferS284;
}

int32_t _M0FPB9log10Pow2(int32_t _M0L1eS280) {
  int32_t _M0L6_2atmpS907;
  uint32_t _M0L6_2atmpS906;
  uint32_t _M0L6_2atmpS905;
  #line 44 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS907 = _M0L1eS280 * 78913;
  _M0L6_2atmpS906 = *(uint32_t*)&_M0L6_2atmpS907;
  _M0L6_2atmpS905 = _M0L6_2atmpS906 >> 18;
  return *(int32_t*)&_M0L6_2atmpS905;
}

int32_t _M0FPB9log10Pow5(int32_t _M0L1eS279) {
  int32_t _M0L6_2atmpS904;
  uint32_t _M0L6_2atmpS903;
  uint32_t _M0L6_2atmpS902;
  #line 37 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS904 = _M0L1eS279 * 732923;
  _M0L6_2atmpS903 = *(uint32_t*)&_M0L6_2atmpS904;
  _M0L6_2atmpS902 = _M0L6_2atmpS903 >> 20;
  return *(int32_t*)&_M0L6_2atmpS902;
}

moonbit_string_t _M0FPB18copy__special__str(
  int32_t _M0L4signS277,
  int32_t _M0L8exponentS278,
  int32_t _M0L8mantissaS275
) {
  moonbit_string_t _M0L1sS276;
  moonbit_string_t _result_1419;
  #line 23 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  if (_M0L8mantissaS275) {
    return (moonbit_string_t)moonbit_string_literal_2.data;
  }
  if (_M0L4signS277) {
    _M0L1sS276 = (moonbit_string_t)moonbit_string_literal_3.data;
  } else {
    _M0L1sS276 = (moonbit_string_t)moonbit_string_literal_4.data;
  }
  if (_M0L8exponentS278) {
    moonbit_string_t _result_1418;
    #line 29 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _result_1418
    = moonbit_add_string(_M0L1sS276, (moonbit_string_t)moonbit_string_literal_5.data);
    moonbit_decref(_M0L1sS276);
    return _result_1418;
  }
  #line 31 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _result_1419
  = moonbit_add_string(_M0L1sS276, (moonbit_string_t)moonbit_string_literal_6.data);
  moonbit_decref(_M0L1sS276);
  return _result_1419;
}

int32_t _M0FPB8pow5bits(int32_t _M0L1eS274) {
  int32_t _M0L6_2atmpS901;
  uint32_t _M0L6_2atmpS900;
  uint32_t _M0L6_2atmpS899;
  int32_t _M0L6_2atmpS898;
  #line 18 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS901 = _M0L1eS274 * 1217359;
  _M0L6_2atmpS900 = *(uint32_t*)&_M0L6_2atmpS901;
  _M0L6_2atmpS899 = _M0L6_2atmpS900 >> 19;
  _M0L6_2atmpS898 = *(int32_t*)&_M0L6_2atmpS899;
  return _M0L6_2atmpS898 + 1;
}

int32_t _M0MPC16double6Double7to__int(double _M0L4selfS273) {
  #line 43 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_to_int.mbt"
  if (_M0L4selfS273 != _M0L4selfS273) {
    return 0;
  } else if (_M0L4selfS273 >= 0x1.fffffffcp+30) {
    return 2147483647;
  } else if (_M0L4selfS273 <= -0x1p+31) {
    return (int32_t)0x80000000;
  } else {
    return (int32_t)_M0L4selfS273;
  }
}

int64_t _M0MPC16double6Double9to__int64(double _M0L4selfS272) {
  #line 46 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_to_int64_native.mbt"
  if (_M0L4selfS272 != _M0L4selfS272) {
    return 0ll;
  } else if (_M0L4selfS272 >= 0x1p+63) {
    return 9223372036854775807ll;
  } else if (_M0L4selfS272 <= -0x1p+63) {
    return (int64_t)0x8000000000000000ll;
  } else {
    return (int64_t)_M0L4selfS272;
  }
}

struct _M0TPB5ArrayGfE* _M0MPC15array5Array20unsafe__make__uninitGfE(
  int32_t _M0L3lenS270
) {
  float* _M0L6_2atmpS896;
  struct _M0TPB5ArrayGfE* _block_1420;
  #line 30 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS896 = (float*)moonbit_make_float_array_raw(_M0L3lenS270);
  _block_1420
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_block_1420)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 9, 0);
  _block_1420->$0 = _M0L6_2atmpS896;
  _block_1420->$1 = _M0L3lenS270;
  return _block_1420;
}

struct _M0TPB5ArrayGbE* _M0MPC15array5Array20unsafe__make__uninitGbE(
  int32_t _M0L3lenS271
) {
  uint8_t* _M0L6_2atmpS897;
  struct _M0TPB5ArrayGbE* _block_1421;
  #line 30 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS897 = (uint8_t*)moonbit_make_bytes_raw(_M0L3lenS271);
  _block_1421
  = (struct _M0TPB5ArrayGbE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGbE));
  Moonbit_object_header(_block_1421)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 12, 0);
  _block_1421->$0 = _M0L6_2atmpS897;
  _block_1421->$1 = _M0L3lenS271;
  return _block_1421;
}

uint64_t _M0MPC15array13ReadOnlyArray2atGmE(
  uint64_t* _M0L4selfS266,
  int32_t _M0L5indexS267
) {
  uint64_t* _M0L6_2atmpS894;
  #line 38 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\readonlyarray.mbt"
  _M0L6_2atmpS894 = _M0L4selfS266;
  if (
    _M0L5indexS267 < 0
    || _M0L5indexS267 >= Moonbit_array_length(_M0L6_2atmpS894)
  ) {
    #line 40 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\readonlyarray.mbt"
    moonbit_panic();
  }
  return (uint64_t)_M0L6_2atmpS894[_M0L5indexS267];
}

uint32_t _M0MPC15array13ReadOnlyArray2atGjE(
  uint32_t* _M0L4selfS268,
  int32_t _M0L5indexS269
) {
  uint32_t* _M0L6_2atmpS895;
  #line 38 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\readonlyarray.mbt"
  _M0L6_2atmpS895 = _M0L4selfS268;
  if (
    _M0L5indexS269 < 0
    || _M0L5indexS269 >= Moonbit_array_length(_M0L6_2atmpS895)
  ) {
    #line 40 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\readonlyarray.mbt"
    moonbit_panic();
  }
  return (uint32_t)_M0L6_2atmpS895[_M0L5indexS269];
}

moonbit_string_t _M0IPC16uint646UInt64PB4Show10to__string(
  uint64_t _M0L4selfS265
) {
  #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  #line 51 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  return _M0MPC16uint646UInt6418to__string_2einner(_M0L4selfS265, 10);
}

moonbit_string_t _M0IPC13int3IntPB4Show10to__string(int32_t _M0L4selfS264) {
  #line 35 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  #line 36 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  return _M0MPC13int3Int18to__string_2einner(_M0L4selfS264, 10);
}

moonbit_string_t _M0IPC14bool4BoolPB4Show10to__string(int32_t _M0L4selfS263) {
  #line 26 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  if (_M0L4selfS263) {
    return (moonbit_string_t)moonbit_string_literal_7.data;
  } else {
    return (moonbit_string_t)moonbit_string_literal_8.data;
  }
}

uint64_t _M0MPC14uint4UInt10to__uint64(uint32_t _M0L4selfS262) {
  #line 2576 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  return (uint64_t)_M0L4selfS262;
}

float* _M0MPC15array5Array6bufferGfE(struct _M0TPB5ArrayGfE* _M0L4selfS260) {
  float* _M0L8_2afieldS1370;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS1370 = _M0L4selfS260->$0;
  moonbit_incref(_M0L8_2afieldS1370);
  return _M0L8_2afieldS1370;
}

uint8_t* _M0MPC15array5Array6bufferGbE(struct _M0TPB5ArrayGbE* _M0L4selfS261) {
  uint8_t* _M0L8_2afieldS1371;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS1371 = _M0L4selfS261->$0;
  moonbit_incref(_M0L8_2afieldS1371);
  return _M0L8_2afieldS1371;
}

moonbit_string_t _M0IPC16string6StringPB4Show10to__string(
  moonbit_string_t _M0L4selfS259
) {
  #line 222 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  moonbit_incref(_M0L4selfS259);
  return _M0L4selfS259;
}

int32_t _M0IPB13StringBuilderPB6Logger11write__view(
  struct _M0TPB13StringBuilder* _M0L4selfS258,
  struct _M0TPC16string10StringView _M0L3strS256
) {
  int32_t _M0L3endS892;
  int32_t _M0L5startS893;
  int32_t _M0L8str__lenS255;
  int32_t _M0L3lenS891;
  int32_t _M0L8requiredS257;
  uint16_t* _M0L4dataS884;
  int32_t _M0L6_2atmpS883;
  int32_t _if__result_1422;
  uint16_t* _M0L4dataS885;
  int32_t _M0L3lenS886;
  moonbit_string_t _M0L6_2atmpS887;
  int32_t _M0L6_2atmpS888;
  int32_t _M0L3lenS890;
  int32_t _M0L6_2atmpS889;
  #line 158 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L3endS892 = _M0L3strS256.$2;
  _M0L5startS893 = _M0L3strS256.$1;
  _M0L8str__lenS255 = _M0L3endS892 - _M0L5startS893;
  if (_M0L8str__lenS255 == 0) {
    return 0;
  }
  _M0L3lenS891 = _M0L4selfS258->$1;
  _M0L8requiredS257 = _M0L3lenS891 + _M0L8str__lenS255;
  _M0L4dataS884 = _M0L4selfS258->$0;
  _M0L6_2atmpS883 = Moonbit_array_length(_M0L4dataS884);
  if (_M0L8requiredS257 > _M0L6_2atmpS883) {
    _if__result_1422 = 1;
  } else {
    int32_t _M0L3lenS882 = _M0L4selfS258->$1;
    _if__result_1422 = _M0L8requiredS257 < _M0L3lenS882;
  }
  if (_if__result_1422) {
    #line 168 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0MPB13StringBuilder4grow(_M0L4selfS258, _M0L8requiredS257);
  }
  _M0L4dataS885 = _M0L4selfS258->$0;
  _M0L3lenS886 = _M0L4selfS258->$1;
  moonbit_incref(_M0L4dataS885);
  #line 172 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L6_2atmpS887 = _M0MPC16string10StringView4data(_M0L3strS256);
  #line 173 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L6_2atmpS888 = _M0MPC16string10StringView13start__offset(_M0L3strS256);
  #line 170 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0MPC15array10FixedArray26unsafe__blit__from__string(_M0L4dataS885, _M0L3lenS886, _M0L6_2atmpS887, _M0L6_2atmpS888, _M0L8str__lenS255);
  moonbit_decref(_M0L4dataS885);
  moonbit_decref(_M0L6_2atmpS887);
  _M0L3lenS890 = _M0L4selfS258->$1;
  _M0L6_2atmpS889 = _M0L3lenS890 + _M0L8str__lenS255;
  _M0L4selfS258->$1 = _M0L6_2atmpS889;
  return 0;
}

moonbit_string_t _M0MPC16uint646UInt6418to__string_2einner(
  uint64_t _M0L4selfS247,
  int32_t _M0L5radixS246
) {
  uint16_t* _M0L6bufferS248;
  #line 607 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  if (_M0L5radixS246 < 2 || _M0L5radixS246 > 36) {
    #line 611 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_9.data);
  }
  if (_M0L4selfS247 == 0ull) {
    return (moonbit_string_t)moonbit_string_literal_0.data;
  }
  switch (_M0L5radixS246) {
    case 10: {
      int32_t _M0L3lenS249;
      uint16_t* _M0L6bufferS250;
      #line 622 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L3lenS249 = _M0FPB12dec__count64(_M0L4selfS247);
      _M0L6bufferS250 = (uint16_t*)moonbit_make_string(_M0L3lenS249, 0);
      #line 624 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0FPB22int64__to__string__dec(_M0L6bufferS250, _M0L4selfS247, 0, _M0L3lenS249);
      _M0L6bufferS248 = _M0L6bufferS250;
      break;
    }
    
    case 16: {
      int32_t _M0L3lenS251;
      uint16_t* _M0L6bufferS252;
      #line 628 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L3lenS251 = _M0FPB12hex__count64(_M0L4selfS247);
      _M0L6bufferS252 = (uint16_t*)moonbit_make_string(_M0L3lenS251, 0);
      #line 630 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0FPB22int64__to__string__hex(_M0L6bufferS252, _M0L4selfS247, 0, _M0L3lenS251);
      _M0L6bufferS248 = _M0L6bufferS252;
      break;
    }
    default: {
      int32_t _M0L3lenS253;
      uint16_t* _M0L6bufferS254;
      #line 634 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L3lenS253 = _M0FPB14radix__count64(_M0L4selfS247, _M0L5radixS246);
      _M0L6bufferS254 = (uint16_t*)moonbit_make_string(_M0L3lenS253, 0);
      #line 636 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0FPB26int64__to__string__generic(_M0L6bufferS254, _M0L4selfS247, 0, _M0L3lenS253, _M0L5radixS246);
      _M0L6bufferS248 = _M0L6bufferS254;
      break;
    }
  }
  return _M0L6bufferS248;
}

moonbit_string_t _M0MPC15int645Int6418to__string_2einner(
  int64_t _M0L4selfS230,
  int32_t _M0L5radixS229
) {
  int32_t _M0L12is__negativeS231;
  uint64_t _M0L3numS232;
  uint16_t* _M0L6bufferS233;
  #line 548 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  if (_M0L5radixS229 < 2 || _M0L5radixS229 > 36) {
    #line 552 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_9.data);
  }
  if (_M0L4selfS230 == 0ll) {
    return (moonbit_string_t)moonbit_string_literal_0.data;
  }
  _M0L12is__negativeS231 = _M0L4selfS230 < 0ll;
  if (_M0L12is__negativeS231) {
    int64_t _M0L6_2atmpS881 = -_M0L4selfS230;
    _M0L3numS232 = *(uint64_t*)&_M0L6_2atmpS881;
  } else {
    _M0L3numS232 = *(uint64_t*)&_M0L4selfS230;
  }
  switch (_M0L5radixS229) {
    case 10: {
      int32_t _M0L10digit__lenS234;
      int32_t _M0L6_2atmpS878;
      int32_t _M0L10total__lenS235;
      uint16_t* _M0L6bufferS236;
      int32_t _M0L12digit__startS237;
      #line 573 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS234 = _M0FPB12dec__count64(_M0L3numS232);
      if (_M0L12is__negativeS231) {
        _M0L6_2atmpS878 = 1;
      } else {
        _M0L6_2atmpS878 = 0;
      }
      _M0L10total__lenS235 = _M0L10digit__lenS234 + _M0L6_2atmpS878;
      _M0L6bufferS236
      = (uint16_t*)moonbit_make_string(_M0L10total__lenS235, 0);
      if (_M0L12is__negativeS231) {
        _M0L12digit__startS237 = 1;
      } else {
        _M0L12digit__startS237 = 0;
      }
      #line 577 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0FPB22int64__to__string__dec(_M0L6bufferS236, _M0L3numS232, _M0L12digit__startS237, _M0L10total__lenS235);
      _M0L6bufferS233 = _M0L6bufferS236;
      break;
    }
    
    case 16: {
      int32_t _M0L10digit__lenS238;
      int32_t _M0L6_2atmpS879;
      int32_t _M0L10total__lenS239;
      uint16_t* _M0L6bufferS240;
      int32_t _M0L12digit__startS241;
      #line 581 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS238 = _M0FPB12hex__count64(_M0L3numS232);
      if (_M0L12is__negativeS231) {
        _M0L6_2atmpS879 = 1;
      } else {
        _M0L6_2atmpS879 = 0;
      }
      _M0L10total__lenS239 = _M0L10digit__lenS238 + _M0L6_2atmpS879;
      _M0L6bufferS240
      = (uint16_t*)moonbit_make_string(_M0L10total__lenS239, 0);
      if (_M0L12is__negativeS231) {
        _M0L12digit__startS241 = 1;
      } else {
        _M0L12digit__startS241 = 0;
      }
      #line 585 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0FPB22int64__to__string__hex(_M0L6bufferS240, _M0L3numS232, _M0L12digit__startS241, _M0L10total__lenS239);
      _M0L6bufferS233 = _M0L6bufferS240;
      break;
    }
    default: {
      int32_t _M0L10digit__lenS242;
      int32_t _M0L6_2atmpS880;
      int32_t _M0L10total__lenS243;
      uint16_t* _M0L6bufferS244;
      int32_t _M0L12digit__startS245;
      #line 589 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS242
      = _M0FPB14radix__count64(_M0L3numS232, _M0L5radixS229);
      if (_M0L12is__negativeS231) {
        _M0L6_2atmpS880 = 1;
      } else {
        _M0L6_2atmpS880 = 0;
      }
      _M0L10total__lenS243 = _M0L10digit__lenS242 + _M0L6_2atmpS880;
      _M0L6bufferS244
      = (uint16_t*)moonbit_make_string(_M0L10total__lenS243, 0);
      if (_M0L12is__negativeS231) {
        _M0L12digit__startS245 = 1;
      } else {
        _M0L12digit__startS245 = 0;
      }
      #line 593 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0FPB26int64__to__string__generic(_M0L6bufferS244, _M0L3numS232, _M0L12digit__startS245, _M0L10total__lenS243, _M0L5radixS229);
      _M0L6bufferS233 = _M0L6bufferS244;
      break;
    }
  }
  if (_M0L12is__negativeS231) {
    _M0L6bufferS233[0] = 45;
  }
  return _M0L6bufferS233;
}

int32_t _M0FPB22int64__to__string__dec(
  uint16_t* _M0L6bufferS215,
  uint64_t _M0L3numS227,
  int32_t _M0L12digit__startS216,
  int32_t _M0L10total__lenS228
) {
  int32_t _M0L6_2atmpS877;
  uint64_t _M0L3numS205;
  int32_t _M0L6offsetS206;
  #line 493 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L6_2atmpS877 = _M0L10total__lenS228 - _M0L12digit__startS216;
  _M0L3numS205 = _M0L3numS227;
  _M0L6offsetS206 = _M0L6_2atmpS877;
  while (1) {
    if (_M0L3numS205 >= 10000ull) {
      uint64_t _M0L1tS207 = _M0L3numS205 / 10000ull;
      uint64_t _M0L6_2atmpS854 = _M0L3numS205 % 10000ull;
      int32_t _M0L1rS208 = (int32_t)_M0L6_2atmpS854;
      int32_t _M0L2d1S209 = _M0L1rS208 / 100;
      int32_t _M0L2d2S210 = _M0L1rS208 % 100;
      int32_t _M0L6_2atmpS853 = _M0L2d1S209 / 10;
      int32_t _M0L6_2atmpS852 = 48 + _M0L6_2atmpS853;
      int32_t _M0L6d1__hiS211 = (uint16_t)_M0L6_2atmpS852;
      int32_t _M0L6_2atmpS851 = _M0L2d1S209 % 10;
      int32_t _M0L6_2atmpS850 = 48 + _M0L6_2atmpS851;
      int32_t _M0L6d1__loS212 = (uint16_t)_M0L6_2atmpS850;
      int32_t _M0L6_2atmpS849 = _M0L2d2S210 / 10;
      int32_t _M0L6_2atmpS848 = 48 + _M0L6_2atmpS849;
      int32_t _M0L6d2__hiS213 = (uint16_t)_M0L6_2atmpS848;
      int32_t _M0L6_2atmpS847 = _M0L2d2S210 % 10;
      int32_t _M0L6_2atmpS846 = 48 + _M0L6_2atmpS847;
      int32_t _M0L6d2__loS214 = (uint16_t)_M0L6_2atmpS846;
      int32_t _M0L6_2atmpS838 = _M0L12digit__startS216 + _M0L6offsetS206;
      int32_t _M0L6_2atmpS837 = _M0L6_2atmpS838 - 4;
      int32_t _M0L6_2atmpS840;
      int32_t _M0L6_2atmpS839;
      int32_t _M0L6_2atmpS842;
      int32_t _M0L6_2atmpS841;
      int32_t _M0L6_2atmpS844;
      int32_t _M0L6_2atmpS843;
      int32_t _M0L6_2atmpS845;
      _M0L6bufferS215[_M0L6_2atmpS837] = _M0L6d1__hiS211;
      _M0L6_2atmpS840 = _M0L12digit__startS216 + _M0L6offsetS206;
      _M0L6_2atmpS839 = _M0L6_2atmpS840 - 3;
      _M0L6bufferS215[_M0L6_2atmpS839] = _M0L6d1__loS212;
      _M0L6_2atmpS842 = _M0L12digit__startS216 + _M0L6offsetS206;
      _M0L6_2atmpS841 = _M0L6_2atmpS842 - 2;
      _M0L6bufferS215[_M0L6_2atmpS841] = _M0L6d2__hiS213;
      _M0L6_2atmpS844 = _M0L12digit__startS216 + _M0L6offsetS206;
      _M0L6_2atmpS843 = _M0L6_2atmpS844 - 1;
      _M0L6bufferS215[_M0L6_2atmpS843] = _M0L6d2__loS214;
      _M0L6_2atmpS845 = _M0L6offsetS206 - 4;
      _M0L3numS205 = _M0L1tS207;
      _M0L6offsetS206 = _M0L6_2atmpS845;
      continue;
    } else {
      int32_t _M0L6_2atmpS876 = (int32_t)_M0L3numS205;
      int32_t _M0L9remainingS218 = _M0L6_2atmpS876;
      int32_t _M0L6offsetS219 = _M0L6offsetS206;
      while (1) {
        if (_M0L9remainingS218 >= 100) {
          int32_t _M0L1tS220 = _M0L9remainingS218 / 100;
          int32_t _M0L1dS221 = _M0L9remainingS218 % 100;
          int32_t _M0L6_2atmpS863 = _M0L1dS221 / 10;
          int32_t _M0L6_2atmpS862 = 48 + _M0L6_2atmpS863;
          int32_t _M0L5d__hiS222 = (uint16_t)_M0L6_2atmpS862;
          int32_t _M0L6_2atmpS861 = _M0L1dS221 % 10;
          int32_t _M0L6_2atmpS860 = 48 + _M0L6_2atmpS861;
          int32_t _M0L5d__loS223 = (uint16_t)_M0L6_2atmpS860;
          int32_t _M0L6_2atmpS856 = _M0L12digit__startS216 + _M0L6offsetS219;
          int32_t _M0L6_2atmpS855 = _M0L6_2atmpS856 - 2;
          int32_t _M0L6_2atmpS858;
          int32_t _M0L6_2atmpS857;
          int32_t _M0L6_2atmpS859;
          _M0L6bufferS215[_M0L6_2atmpS855] = _M0L5d__hiS222;
          _M0L6_2atmpS858 = _M0L12digit__startS216 + _M0L6offsetS219;
          _M0L6_2atmpS857 = _M0L6_2atmpS858 - 1;
          _M0L6bufferS215[_M0L6_2atmpS857] = _M0L5d__loS223;
          _M0L6_2atmpS859 = _M0L6offsetS219 - 2;
          _M0L9remainingS218 = _M0L1tS220;
          _M0L6offsetS219 = _M0L6_2atmpS859;
          continue;
        } else if (_M0L9remainingS218 >= 10) {
          int32_t _M0L6_2atmpS871 = _M0L9remainingS218 / 10;
          int32_t _M0L6_2atmpS870 = 48 + _M0L6_2atmpS871;
          int32_t _M0L5d__hiS225 = (uint16_t)_M0L6_2atmpS870;
          int32_t _M0L6_2atmpS869 = _M0L9remainingS218 % 10;
          int32_t _M0L6_2atmpS868 = 48 + _M0L6_2atmpS869;
          int32_t _M0L5d__loS226 = (uint16_t)_M0L6_2atmpS868;
          int32_t _M0L6_2atmpS865 = _M0L12digit__startS216 + _M0L6offsetS219;
          int32_t _M0L6_2atmpS864 = _M0L6_2atmpS865 - 2;
          int32_t _M0L6_2atmpS867;
          int32_t _M0L6_2atmpS866;
          _M0L6bufferS215[_M0L6_2atmpS864] = _M0L5d__hiS225;
          _M0L6_2atmpS867 = _M0L12digit__startS216 + _M0L6offsetS219;
          _M0L6_2atmpS866 = _M0L6_2atmpS867 - 1;
          _M0L6bufferS215[_M0L6_2atmpS866] = _M0L5d__loS226;
        } else {
          int32_t _M0L6_2atmpS875 = _M0L12digit__startS216 + _M0L6offsetS219;
          int32_t _M0L6_2atmpS872 = _M0L6_2atmpS875 - 1;
          int32_t _M0L6_2atmpS874 = 48 + _M0L9remainingS218;
          int32_t _M0L6_2atmpS873 = (uint16_t)_M0L6_2atmpS874;
          _M0L6bufferS215[_M0L6_2atmpS872] = _M0L6_2atmpS873;
        }
        break;
      }
    }
    break;
  }
  return 0;
}

int32_t _M0FPB26int64__to__string__generic(
  uint16_t* _M0L6bufferS195,
  uint64_t _M0L3numS199,
  int32_t _M0L12digit__startS196,
  int32_t _M0L10total__lenS198,
  int32_t _M0L5radixS189
) {
  uint64_t _M0L4baseS188;
  int32_t _M0L6_2atmpS822;
  int32_t _M0L6_2atmpS821;
  #line 462 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  #line 470 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L4baseS188 = _M0MPC13int3Int10to__uint64(_M0L5radixS189);
  _M0L6_2atmpS822 = _M0L5radixS189 - 1;
  _M0L6_2atmpS821 = _M0L5radixS189 & _M0L6_2atmpS822;
  if (_M0L6_2atmpS821 == 0) {
    int32_t _M0L5shiftS190;
    uint64_t _M0L4maskS191;
    int32_t _M0L6_2atmpS829;
    int32_t _M0L6offsetS192;
    uint64_t _M0L1nS193;
    #line 473 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0L5shiftS190 = moonbit_ctz32(_M0L5radixS189);
    _M0L4maskS191 = _M0L4baseS188 - 1ull;
    _M0L6_2atmpS829 = _M0L10total__lenS198 - _M0L12digit__startS196;
    _M0L6offsetS192 = _M0L6_2atmpS829;
    _M0L1nS193 = _M0L3numS199;
    while (1) {
      if (_M0L1nS193 > 0ull) {
        uint64_t _M0L6_2atmpS828 = _M0L1nS193 & _M0L4maskS191;
        int32_t _M0L5digitS194 = (int32_t)_M0L6_2atmpS828;
        int32_t _M0L6_2atmpS825 = _M0L12digit__startS196 + _M0L6offsetS192;
        int32_t _M0L6_2atmpS823 = _M0L6_2atmpS825 - 1;
        int32_t _M0L6_2atmpS824 =
          ((moonbit_string_t)moonbit_string_literal_10.data)[_M0L5digitS194];
        int32_t _M0L6_2atmpS826;
        uint64_t _M0L6_2atmpS827;
        _M0L6bufferS195[_M0L6_2atmpS823] = _M0L6_2atmpS824;
        _M0L6_2atmpS826 = _M0L6offsetS192 - 1;
        _M0L6_2atmpS827 = _M0L1nS193 >> (_M0L5shiftS190 & 63);
        _M0L6offsetS192 = _M0L6_2atmpS826;
        _M0L1nS193 = _M0L6_2atmpS827;
        continue;
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS836 = _M0L10total__lenS198 - _M0L12digit__startS196;
    int32_t _M0L6offsetS200 = _M0L6_2atmpS836;
    uint64_t _M0L1nS201 = _M0L3numS199;
    while (1) {
      if (_M0L1nS201 > 0ull) {
        uint64_t _M0L1qS202 = _M0L1nS201 / _M0L4baseS188;
        uint64_t _M0L6_2atmpS835 = _M0L1qS202 * _M0L4baseS188;
        uint64_t _M0L6_2atmpS834 = _M0L1nS201 - _M0L6_2atmpS835;
        int32_t _M0L5digitS203 = (int32_t)_M0L6_2atmpS834;
        int32_t _M0L6_2atmpS832 = _M0L12digit__startS196 + _M0L6offsetS200;
        int32_t _M0L6_2atmpS830 = _M0L6_2atmpS832 - 1;
        int32_t _M0L6_2atmpS831 =
          ((moonbit_string_t)moonbit_string_literal_10.data)[_M0L5digitS203];
        int32_t _M0L6_2atmpS833;
        _M0L6bufferS195[_M0L6_2atmpS830] = _M0L6_2atmpS831;
        _M0L6_2atmpS833 = _M0L6offsetS200 - 1;
        _M0L6offsetS200 = _M0L6_2atmpS833;
        _M0L1nS201 = _M0L1qS202;
        continue;
      }
      break;
    }
  }
  return 0;
}

int32_t _M0FPB22int64__to__string__hex(
  uint16_t* _M0L6bufferS182,
  uint64_t _M0L3numS187,
  int32_t _M0L12digit__startS183,
  int32_t _M0L10total__lenS186
) {
  int32_t _M0L6_2atmpS820;
  int32_t _M0L6offsetS177;
  uint64_t _M0L1nS178;
  #line 434 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L6_2atmpS820 = _M0L10total__lenS186 - _M0L12digit__startS183;
  _M0L6offsetS177 = _M0L6_2atmpS820;
  _M0L1nS178 = _M0L3numS187;
  while (1) {
    if (_M0L6offsetS177 >= 2) {
      uint64_t _M0L6_2atmpS817 = _M0L1nS178 & 255ull;
      int32_t _M0L9byte__valS179 = (int32_t)_M0L6_2atmpS817;
      int32_t _M0L2hiS180 = _M0L9byte__valS179 / 16;
      int32_t _M0L2loS181 = _M0L9byte__valS179 % 16;
      int32_t _M0L6_2atmpS811 = _M0L12digit__startS183 + _M0L6offsetS177;
      int32_t _M0L6_2atmpS809 = _M0L6_2atmpS811 - 2;
      int32_t _M0L6_2atmpS810 =
        ((moonbit_string_t)moonbit_string_literal_10.data)[_M0L2hiS180];
      int32_t _M0L6_2atmpS814;
      int32_t _M0L6_2atmpS812;
      int32_t _M0L6_2atmpS813;
      int32_t _M0L6_2atmpS815;
      uint64_t _M0L6_2atmpS816;
      _M0L6bufferS182[_M0L6_2atmpS809] = _M0L6_2atmpS810;
      _M0L6_2atmpS814 = _M0L12digit__startS183 + _M0L6offsetS177;
      _M0L6_2atmpS812 = _M0L6_2atmpS814 - 1;
      _M0L6_2atmpS813
      = ((moonbit_string_t)moonbit_string_literal_10.data)[
        _M0L2loS181
      ];
      _M0L6bufferS182[_M0L6_2atmpS812] = _M0L6_2atmpS813;
      _M0L6_2atmpS815 = _M0L6offsetS177 - 2;
      _M0L6_2atmpS816 = _M0L1nS178 >> 8;
      _M0L6offsetS177 = _M0L6_2atmpS815;
      _M0L1nS178 = _M0L6_2atmpS816;
      continue;
    } else if (_M0L6offsetS177 == 1) {
      uint64_t _M0L6_2atmpS819 = _M0L1nS178 & 15ull;
      int32_t _M0L6nibbleS185 = (int32_t)_M0L6_2atmpS819;
      int32_t _M0L6_2atmpS818 =
        ((moonbit_string_t)moonbit_string_literal_10.data)[_M0L6nibbleS185];
      _M0L6bufferS182[_M0L12digit__startS183] = _M0L6_2atmpS818;
    }
    break;
  }
  return 0;
}

int32_t _M0FPB14radix__count64(
  uint64_t _M0L5valueS171,
  int32_t _M0L5radixS173
) {
  uint64_t _M0L4baseS172;
  uint64_t _M0L3numS174;
  int32_t _M0L5countS175;
  #line 419 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  if (_M0L5valueS171 == 0ull) {
    return 1;
  }
  #line 424 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L4baseS172 = _M0MPC13int3Int10to__uint64(_M0L5radixS173);
  _M0L3numS174 = _M0L5valueS171;
  _M0L5countS175 = 0;
  while (1) {
    if (_M0L3numS174 > 0ull) {
      uint64_t _M0L6_2atmpS807 = _M0L3numS174 / _M0L4baseS172;
      int32_t _M0L6_2atmpS808 = _M0L5countS175 + 1;
      _M0L3numS174 = _M0L6_2atmpS807;
      _M0L5countS175 = _M0L6_2atmpS808;
      continue;
    } else {
      return _M0L5countS175;
    }
    break;
  }
}

int32_t _M0FPB12hex__count64(uint64_t _M0L5valueS169) {
  #line 407 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  if (_M0L5valueS169 == 0ull) {
    return 1;
  } else {
    int32_t _M0L14leading__zerosS170;
    int32_t _M0L6_2atmpS806;
    int32_t _M0L6_2atmpS805;
    #line 412 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0L14leading__zerosS170 = moonbit_clz64(_M0L5valueS169);
    _M0L6_2atmpS806 = 63 - _M0L14leading__zerosS170;
    _M0L6_2atmpS805 = _M0L6_2atmpS806 / 4;
    return _M0L6_2atmpS805 + 1;
  }
}

int32_t _M0FPB12dec__count64(uint64_t _M0L5valueS168) {
  #line 343 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  if (_M0L5valueS168 >= 10000000000ull) {
    if (_M0L5valueS168 >= 100000000000000ull) {
      if (_M0L5valueS168 >= 10000000000000000ull) {
        if (_M0L5valueS168 >= 1000000000000000000ull) {
          if (_M0L5valueS168 >= 10000000000000000000ull) {
            return 20;
          } else {
            return 19;
          }
        } else if (_M0L5valueS168 >= 100000000000000000ull) {
          return 18;
        } else {
          return 17;
        }
      } else if (_M0L5valueS168 >= 1000000000000000ull) {
        return 16;
      } else {
        return 15;
      }
    } else if (_M0L5valueS168 >= 1000000000000ull) {
      if (_M0L5valueS168 >= 10000000000000ull) {
        return 14;
      } else {
        return 13;
      }
    } else if (_M0L5valueS168 >= 100000000000ull) {
      return 12;
    } else {
      return 11;
    }
  } else if (_M0L5valueS168 >= 100000ull) {
    if (_M0L5valueS168 >= 10000000ull) {
      if (_M0L5valueS168 >= 1000000000ull) {
        return 10;
      } else if (_M0L5valueS168 >= 100000000ull) {
        return 9;
      } else {
        return 8;
      }
    } else if (_M0L5valueS168 >= 1000000ull) {
      return 7;
    } else {
      return 6;
    }
  } else if (_M0L5valueS168 >= 1000ull) {
    if (_M0L5valueS168 >= 10000ull) {
      return 5;
    } else {
      return 4;
    }
  } else if (_M0L5valueS168 >= 100ull) {
    return 3;
  } else if (_M0L5valueS168 >= 10ull) {
    return 2;
  } else {
    return 1;
  }
}

moonbit_string_t _M0MPC13int3Int18to__string_2einner(
  int32_t _M0L4selfS152,
  int32_t _M0L5radixS151
) {
  int32_t _M0L12is__negativeS153;
  uint32_t _M0L3numS154;
  uint16_t* _M0L6bufferS155;
  #line 209 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  if (_M0L5radixS151 < 2 || _M0L5radixS151 > 36) {
    #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_9.data);
  }
  if (_M0L4selfS152 == 0) {
    return (moonbit_string_t)moonbit_string_literal_0.data;
  }
  _M0L12is__negativeS153 = _M0L4selfS152 < 0;
  if (_M0L12is__negativeS153) {
    int32_t _M0L6_2atmpS804 = -_M0L4selfS152;
    _M0L3numS154 = *(uint32_t*)&_M0L6_2atmpS804;
  } else {
    _M0L3numS154 = *(uint32_t*)&_M0L4selfS152;
  }
  switch (_M0L5radixS151) {
    case 10: {
      int32_t _M0L10digit__lenS156;
      int32_t _M0L6_2atmpS801;
      int32_t _M0L10total__lenS157;
      uint16_t* _M0L6bufferS158;
      int32_t _M0L12digit__startS159;
      #line 235 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS156 = _M0FPB12dec__count32(_M0L3numS154);
      if (_M0L12is__negativeS153) {
        _M0L6_2atmpS801 = 1;
      } else {
        _M0L6_2atmpS801 = 0;
      }
      _M0L10total__lenS157 = _M0L10digit__lenS156 + _M0L6_2atmpS801;
      _M0L6bufferS158
      = (uint16_t*)moonbit_make_string(_M0L10total__lenS157, 0);
      if (_M0L12is__negativeS153) {
        _M0L12digit__startS159 = 1;
      } else {
        _M0L12digit__startS159 = 0;
      }
      #line 239 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0FPB20int__to__string__dec(_M0L6bufferS158, _M0L3numS154, _M0L12digit__startS159, _M0L10total__lenS157);
      _M0L6bufferS155 = _M0L6bufferS158;
      break;
    }
    
    case 16: {
      int32_t _M0L10digit__lenS160;
      int32_t _M0L6_2atmpS802;
      int32_t _M0L10total__lenS161;
      uint16_t* _M0L6bufferS162;
      int32_t _M0L12digit__startS163;
      #line 243 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS160 = _M0FPB12hex__count32(_M0L3numS154);
      if (_M0L12is__negativeS153) {
        _M0L6_2atmpS802 = 1;
      } else {
        _M0L6_2atmpS802 = 0;
      }
      _M0L10total__lenS161 = _M0L10digit__lenS160 + _M0L6_2atmpS802;
      _M0L6bufferS162
      = (uint16_t*)moonbit_make_string(_M0L10total__lenS161, 0);
      if (_M0L12is__negativeS153) {
        _M0L12digit__startS163 = 1;
      } else {
        _M0L12digit__startS163 = 0;
      }
      #line 247 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0FPB20int__to__string__hex(_M0L6bufferS162, _M0L3numS154, _M0L12digit__startS163, _M0L10total__lenS161);
      _M0L6bufferS155 = _M0L6bufferS162;
      break;
    }
    default: {
      int32_t _M0L10digit__lenS164;
      int32_t _M0L6_2atmpS803;
      int32_t _M0L10total__lenS165;
      uint16_t* _M0L6bufferS166;
      int32_t _M0L12digit__startS167;
      #line 251 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS164
      = _M0FPB14radix__count32(_M0L3numS154, _M0L5radixS151);
      if (_M0L12is__negativeS153) {
        _M0L6_2atmpS803 = 1;
      } else {
        _M0L6_2atmpS803 = 0;
      }
      _M0L10total__lenS165 = _M0L10digit__lenS164 + _M0L6_2atmpS803;
      _M0L6bufferS166
      = (uint16_t*)moonbit_make_string(_M0L10total__lenS165, 0);
      if (_M0L12is__negativeS153) {
        _M0L12digit__startS167 = 1;
      } else {
        _M0L12digit__startS167 = 0;
      }
      #line 255 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0FPB24int__to__string__generic(_M0L6bufferS166, _M0L3numS154, _M0L12digit__startS167, _M0L10total__lenS165, _M0L5radixS151);
      _M0L6bufferS155 = _M0L6bufferS166;
      break;
    }
  }
  if (_M0L12is__negativeS153) {
    _M0L6bufferS155[0] = 45;
  }
  return _M0L6bufferS155;
}

int32_t _M0FPB14radix__count32(
  uint32_t _M0L5valueS145,
  int32_t _M0L5radixS147
) {
  uint32_t _M0L4baseS146;
  uint32_t _M0L3numS148;
  int32_t _M0L5countS149;
  #line 189 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  if (_M0L5valueS145 == 0u) {
    return 1;
  }
  _M0L4baseS146 = *(uint32_t*)&_M0L5radixS147;
  _M0L3numS148 = _M0L5valueS145;
  _M0L5countS149 = 0;
  while (1) {
    if (_M0L3numS148 > 0u) {
      uint32_t _M0L6_2atmpS799 = _M0L3numS148 / _M0L4baseS146;
      int32_t _M0L6_2atmpS800 = _M0L5countS149 + 1;
      _M0L3numS148 = _M0L6_2atmpS799;
      _M0L5countS149 = _M0L6_2atmpS800;
      continue;
    } else {
      return _M0L5countS149;
    }
    break;
  }
}

int32_t _M0FPB12hex__count32(uint32_t _M0L5valueS143) {
  #line 177 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  if (_M0L5valueS143 == 0u) {
    return 1;
  } else {
    int32_t _M0L14leading__zerosS144;
    int32_t _M0L6_2atmpS798;
    int32_t _M0L6_2atmpS797;
    #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0L14leading__zerosS144 = moonbit_clz32(_M0L5valueS143);
    _M0L6_2atmpS798 = 31 - _M0L14leading__zerosS144;
    _M0L6_2atmpS797 = _M0L6_2atmpS798 / 4;
    return _M0L6_2atmpS797 + 1;
  }
}

int32_t _M0FPB12dec__count32(uint32_t _M0L5valueS142) {
  #line 143 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  if (_M0L5valueS142 >= 100000u) {
    if (_M0L5valueS142 >= 10000000u) {
      if (_M0L5valueS142 >= 1000000000u) {
        return 10;
      } else if (_M0L5valueS142 >= 100000000u) {
        return 9;
      } else {
        return 8;
      }
    } else if (_M0L5valueS142 >= 1000000u) {
      return 7;
    } else {
      return 6;
    }
  } else if (_M0L5valueS142 >= 1000u) {
    if (_M0L5valueS142 >= 10000u) {
      return 5;
    } else {
      return 4;
    }
  } else if (_M0L5valueS142 >= 100u) {
    return 3;
  } else if (_M0L5valueS142 >= 10u) {
    return 2;
  } else {
    return 1;
  }
}

int32_t _M0FPB20int__to__string__dec(
  uint16_t* _M0L6bufferS128,
  uint32_t _M0L3numS140,
  int32_t _M0L12digit__startS129,
  int32_t _M0L10total__lenS141
) {
  int32_t _M0L6_2atmpS796;
  uint32_t _M0L3numS118;
  int32_t _M0L6offsetS119;
  #line 88 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L6_2atmpS796 = _M0L10total__lenS141 - _M0L12digit__startS129;
  _M0L3numS118 = _M0L3numS140;
  _M0L6offsetS119 = _M0L6_2atmpS796;
  while (1) {
    if (_M0L3numS118 >= 10000u) {
      uint32_t _M0L1tS120 = _M0L3numS118 / 10000u;
      uint32_t _M0L6_2atmpS773 = _M0L3numS118 % 10000u;
      int32_t _M0L1rS121 = *(int32_t*)&_M0L6_2atmpS773;
      int32_t _M0L2d1S122 = _M0L1rS121 / 100;
      int32_t _M0L2d2S123 = _M0L1rS121 % 100;
      int32_t _M0L6_2atmpS772 = _M0L2d1S122 / 10;
      int32_t _M0L6_2atmpS771 = 48 + _M0L6_2atmpS772;
      int32_t _M0L6d1__hiS124 = (uint16_t)_M0L6_2atmpS771;
      int32_t _M0L6_2atmpS770 = _M0L2d1S122 % 10;
      int32_t _M0L6_2atmpS769 = 48 + _M0L6_2atmpS770;
      int32_t _M0L6d1__loS125 = (uint16_t)_M0L6_2atmpS769;
      int32_t _M0L6_2atmpS768 = _M0L2d2S123 / 10;
      int32_t _M0L6_2atmpS767 = 48 + _M0L6_2atmpS768;
      int32_t _M0L6d2__hiS126 = (uint16_t)_M0L6_2atmpS767;
      int32_t _M0L6_2atmpS766 = _M0L2d2S123 % 10;
      int32_t _M0L6_2atmpS765 = 48 + _M0L6_2atmpS766;
      int32_t _M0L6d2__loS127 = (uint16_t)_M0L6_2atmpS765;
      int32_t _M0L6_2atmpS757 = _M0L12digit__startS129 + _M0L6offsetS119;
      int32_t _M0L6_2atmpS756 = _M0L6_2atmpS757 - 4;
      int32_t _M0L6_2atmpS759;
      int32_t _M0L6_2atmpS758;
      int32_t _M0L6_2atmpS761;
      int32_t _M0L6_2atmpS760;
      int32_t _M0L6_2atmpS763;
      int32_t _M0L6_2atmpS762;
      int32_t _M0L6_2atmpS764;
      _M0L6bufferS128[_M0L6_2atmpS756] = _M0L6d1__hiS124;
      _M0L6_2atmpS759 = _M0L12digit__startS129 + _M0L6offsetS119;
      _M0L6_2atmpS758 = _M0L6_2atmpS759 - 3;
      _M0L6bufferS128[_M0L6_2atmpS758] = _M0L6d1__loS125;
      _M0L6_2atmpS761 = _M0L12digit__startS129 + _M0L6offsetS119;
      _M0L6_2atmpS760 = _M0L6_2atmpS761 - 2;
      _M0L6bufferS128[_M0L6_2atmpS760] = _M0L6d2__hiS126;
      _M0L6_2atmpS763 = _M0L12digit__startS129 + _M0L6offsetS119;
      _M0L6_2atmpS762 = _M0L6_2atmpS763 - 1;
      _M0L6bufferS128[_M0L6_2atmpS762] = _M0L6d2__loS127;
      _M0L6_2atmpS764 = _M0L6offsetS119 - 4;
      _M0L3numS118 = _M0L1tS120;
      _M0L6offsetS119 = _M0L6_2atmpS764;
      continue;
    } else {
      int32_t _M0L6_2atmpS795 = *(int32_t*)&_M0L3numS118;
      int32_t _M0L9remainingS131 = _M0L6_2atmpS795;
      int32_t _M0L6offsetS132 = _M0L6offsetS119;
      while (1) {
        if (_M0L9remainingS131 >= 100) {
          int32_t _M0L1tS133 = _M0L9remainingS131 / 100;
          int32_t _M0L1dS134 = _M0L9remainingS131 % 100;
          int32_t _M0L6_2atmpS782 = _M0L1dS134 / 10;
          int32_t _M0L6_2atmpS781 = 48 + _M0L6_2atmpS782;
          int32_t _M0L5d__hiS135 = (uint16_t)_M0L6_2atmpS781;
          int32_t _M0L6_2atmpS780 = _M0L1dS134 % 10;
          int32_t _M0L6_2atmpS779 = 48 + _M0L6_2atmpS780;
          int32_t _M0L5d__loS136 = (uint16_t)_M0L6_2atmpS779;
          int32_t _M0L6_2atmpS775 = _M0L12digit__startS129 + _M0L6offsetS132;
          int32_t _M0L6_2atmpS774 = _M0L6_2atmpS775 - 2;
          int32_t _M0L6_2atmpS777;
          int32_t _M0L6_2atmpS776;
          int32_t _M0L6_2atmpS778;
          _M0L6bufferS128[_M0L6_2atmpS774] = _M0L5d__hiS135;
          _M0L6_2atmpS777 = _M0L12digit__startS129 + _M0L6offsetS132;
          _M0L6_2atmpS776 = _M0L6_2atmpS777 - 1;
          _M0L6bufferS128[_M0L6_2atmpS776] = _M0L5d__loS136;
          _M0L6_2atmpS778 = _M0L6offsetS132 - 2;
          _M0L9remainingS131 = _M0L1tS133;
          _M0L6offsetS132 = _M0L6_2atmpS778;
          continue;
        } else if (_M0L9remainingS131 >= 10) {
          int32_t _M0L6_2atmpS790 = _M0L9remainingS131 / 10;
          int32_t _M0L6_2atmpS789 = 48 + _M0L6_2atmpS790;
          int32_t _M0L5d__hiS138 = (uint16_t)_M0L6_2atmpS789;
          int32_t _M0L6_2atmpS788 = _M0L9remainingS131 % 10;
          int32_t _M0L6_2atmpS787 = 48 + _M0L6_2atmpS788;
          int32_t _M0L5d__loS139 = (uint16_t)_M0L6_2atmpS787;
          int32_t _M0L6_2atmpS784 = _M0L12digit__startS129 + _M0L6offsetS132;
          int32_t _M0L6_2atmpS783 = _M0L6_2atmpS784 - 2;
          int32_t _M0L6_2atmpS786;
          int32_t _M0L6_2atmpS785;
          _M0L6bufferS128[_M0L6_2atmpS783] = _M0L5d__hiS138;
          _M0L6_2atmpS786 = _M0L12digit__startS129 + _M0L6offsetS132;
          _M0L6_2atmpS785 = _M0L6_2atmpS786 - 1;
          _M0L6bufferS128[_M0L6_2atmpS785] = _M0L5d__loS139;
        } else {
          int32_t _M0L6_2atmpS794 = _M0L12digit__startS129 + _M0L6offsetS132;
          int32_t _M0L6_2atmpS791 = _M0L6_2atmpS794 - 1;
          int32_t _M0L6_2atmpS793 = 48 + _M0L9remainingS131;
          int32_t _M0L6_2atmpS792 = (uint16_t)_M0L6_2atmpS793;
          _M0L6bufferS128[_M0L6_2atmpS791] = _M0L6_2atmpS792;
        }
        break;
      }
    }
    break;
  }
  return 0;
}

int32_t _M0FPB24int__to__string__generic(
  uint16_t* _M0L6bufferS108,
  uint32_t _M0L3numS112,
  int32_t _M0L12digit__startS109,
  int32_t _M0L10total__lenS111,
  int32_t _M0L5radixS102
) {
  uint32_t _M0L4baseS101;
  int32_t _M0L6_2atmpS741;
  int32_t _M0L6_2atmpS740;
  #line 57 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L4baseS101 = *(uint32_t*)&_M0L5radixS102;
  _M0L6_2atmpS741 = _M0L5radixS102 - 1;
  _M0L6_2atmpS740 = _M0L5radixS102 & _M0L6_2atmpS741;
  if (_M0L6_2atmpS740 == 0) {
    int32_t _M0L5shiftS103;
    uint32_t _M0L4maskS104;
    int32_t _M0L6_2atmpS748;
    int32_t _M0L6offsetS105;
    uint32_t _M0L1nS106;
    #line 68 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0L5shiftS103 = moonbit_ctz32(_M0L5radixS102);
    _M0L4maskS104 = _M0L4baseS101 - 1u;
    _M0L6_2atmpS748 = _M0L10total__lenS111 - _M0L12digit__startS109;
    _M0L6offsetS105 = _M0L6_2atmpS748;
    _M0L1nS106 = _M0L3numS112;
    while (1) {
      if (_M0L1nS106 > 0u) {
        uint32_t _M0L6_2atmpS747 = _M0L1nS106 & _M0L4maskS104;
        int32_t _M0L5digitS107 = *(int32_t*)&_M0L6_2atmpS747;
        int32_t _M0L6_2atmpS744 = _M0L12digit__startS109 + _M0L6offsetS105;
        int32_t _M0L6_2atmpS742 = _M0L6_2atmpS744 - 1;
        int32_t _M0L6_2atmpS743 =
          ((moonbit_string_t)moonbit_string_literal_10.data)[_M0L5digitS107];
        int32_t _M0L6_2atmpS745;
        uint32_t _M0L6_2atmpS746;
        _M0L6bufferS108[_M0L6_2atmpS742] = _M0L6_2atmpS743;
        _M0L6_2atmpS745 = _M0L6offsetS105 - 1;
        _M0L6_2atmpS746 = _M0L1nS106 >> (_M0L5shiftS103 & 31);
        _M0L6offsetS105 = _M0L6_2atmpS745;
        _M0L1nS106 = _M0L6_2atmpS746;
        continue;
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS755 = _M0L10total__lenS111 - _M0L12digit__startS109;
    int32_t _M0L6offsetS113 = _M0L6_2atmpS755;
    uint32_t _M0L1nS114 = _M0L3numS112;
    while (1) {
      if (_M0L1nS114 > 0u) {
        uint32_t _M0L1qS115 = _M0L1nS114 / _M0L4baseS101;
        uint32_t _M0L6_2atmpS754 = _M0L1qS115 * _M0L4baseS101;
        uint32_t _M0L6_2atmpS753 = _M0L1nS114 - _M0L6_2atmpS754;
        int32_t _M0L5digitS116 = *(int32_t*)&_M0L6_2atmpS753;
        int32_t _M0L6_2atmpS751 = _M0L12digit__startS109 + _M0L6offsetS113;
        int32_t _M0L6_2atmpS749 = _M0L6_2atmpS751 - 1;
        int32_t _M0L6_2atmpS750 =
          ((moonbit_string_t)moonbit_string_literal_10.data)[_M0L5digitS116];
        int32_t _M0L6_2atmpS752;
        _M0L6bufferS108[_M0L6_2atmpS749] = _M0L6_2atmpS750;
        _M0L6_2atmpS752 = _M0L6offsetS113 - 1;
        _M0L6offsetS113 = _M0L6_2atmpS752;
        _M0L1nS114 = _M0L1qS115;
        continue;
      }
      break;
    }
  }
  return 0;
}

int32_t _M0FPB20int__to__string__hex(
  uint16_t* _M0L6bufferS95,
  uint32_t _M0L3numS100,
  int32_t _M0L12digit__startS96,
  int32_t _M0L10total__lenS99
) {
  int32_t _M0L6_2atmpS739;
  int32_t _M0L6offsetS90;
  uint32_t _M0L1nS91;
  #line 29 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L6_2atmpS739 = _M0L10total__lenS99 - _M0L12digit__startS96;
  _M0L6offsetS90 = _M0L6_2atmpS739;
  _M0L1nS91 = _M0L3numS100;
  while (1) {
    if (_M0L6offsetS90 >= 2) {
      uint32_t _M0L6_2atmpS736 = _M0L1nS91 & 255u;
      int32_t _M0L9byte__valS92 = *(int32_t*)&_M0L6_2atmpS736;
      int32_t _M0L2hiS93 = _M0L9byte__valS92 / 16;
      int32_t _M0L2loS94 = _M0L9byte__valS92 % 16;
      int32_t _M0L6_2atmpS730 = _M0L12digit__startS96 + _M0L6offsetS90;
      int32_t _M0L6_2atmpS728 = _M0L6_2atmpS730 - 2;
      int32_t _M0L6_2atmpS729 =
        ((moonbit_string_t)moonbit_string_literal_10.data)[_M0L2hiS93];
      int32_t _M0L6_2atmpS733;
      int32_t _M0L6_2atmpS731;
      int32_t _M0L6_2atmpS732;
      int32_t _M0L6_2atmpS734;
      uint32_t _M0L6_2atmpS735;
      _M0L6bufferS95[_M0L6_2atmpS728] = _M0L6_2atmpS729;
      _M0L6_2atmpS733 = _M0L12digit__startS96 + _M0L6offsetS90;
      _M0L6_2atmpS731 = _M0L6_2atmpS733 - 1;
      _M0L6_2atmpS732
      = ((moonbit_string_t)moonbit_string_literal_10.data)[
        _M0L2loS94
      ];
      _M0L6bufferS95[_M0L6_2atmpS731] = _M0L6_2atmpS732;
      _M0L6_2atmpS734 = _M0L6offsetS90 - 2;
      _M0L6_2atmpS735 = _M0L1nS91 >> 8;
      _M0L6offsetS90 = _M0L6_2atmpS734;
      _M0L1nS91 = _M0L6_2atmpS735;
      continue;
    } else if (_M0L6offsetS90 == 1) {
      uint32_t _M0L6_2atmpS738 = _M0L1nS91 & 15u;
      int32_t _M0L6nibbleS98 = *(int32_t*)&_M0L6_2atmpS738;
      int32_t _M0L6_2atmpS737 =
        ((moonbit_string_t)moonbit_string_literal_10.data)[_M0L6nibbleS98];
      _M0L6bufferS95[_M0L12digit__startS96] = _M0L6_2atmpS737;
    }
    break;
  }
  return 0;
}

int32_t _M0IP016_24default__implPB4Show6outputGmE(
  uint64_t _M0L4selfS87,
  struct _M0TPB6Logger _M0L6loggerS86
) {
  moonbit_string_t _M0L6_2atmpS726;
  #line 159 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  #line 160 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS726 = _M0IPC16uint646UInt64PB4Show10to__string(_M0L4selfS87);
  #line 160 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6loggerS86.$0->$method_0(_M0L6loggerS86.$1, _M0L6_2atmpS726);
  moonbit_decref(_M0L6_2atmpS726);
  return 0;
}

int32_t _M0IP016_24default__implPB4Show6outputGiE(
  int32_t _M0L4selfS89,
  struct _M0TPB6Logger _M0L6loggerS88
) {
  moonbit_string_t _M0L6_2atmpS727;
  #line 159 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  #line 160 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS727 = _M0IPC13int3IntPB4Show10to__string(_M0L4selfS89);
  #line 160 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6loggerS88.$0->$method_0(_M0L6loggerS88.$1, _M0L6_2atmpS727);
  moonbit_decref(_M0L6_2atmpS727);
  return 0;
}

int32_t _M0MPC16string10StringView13start__offset(
  struct _M0TPC16string10StringView _M0L4selfS85
) {
  #line 99 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
  return _M0L4selfS85.$1;
}

moonbit_string_t _M0MPC16string10StringView4data(
  struct _M0TPC16string10StringView _M0L4selfS84
) {
  moonbit_string_t _M0L8_2afieldS1372;
  #line 92 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
  _M0L8_2afieldS1372 = _M0L4selfS84.$0;
  moonbit_incref(_M0L8_2afieldS1372);
  return _M0L8_2afieldS1372;
}

int32_t _M0IP016_24default__implPB6Logger16write__substringGRPB13StringBuilderE(
  struct _M0TPB13StringBuilder* _M0L4selfS80,
  moonbit_string_t _M0L5valueS81,
  int32_t _M0L5startS82,
  int32_t _M0L3lenS83
) {
  int32_t _M0L6_2atmpS725;
  int64_t _M0L6_2atmpS724;
  struct _M0TPC16string10StringView _M0L6_2atmpS723;
  #line 122 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS725 = _M0L5startS82 + _M0L3lenS83;
  _M0L6_2atmpS724 = (int64_t)_M0L6_2atmpS725;
  #line 123 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS723
  = _M0MPC16string6String11sub_2einner(_M0L5valueS81, _M0L5startS82, _M0L6_2atmpS724);
  #line 123 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0IPB13StringBuilderPB6Logger11write__view(_M0L4selfS80, _M0L6_2atmpS723);
  moonbit_decref(_M0L6_2atmpS723.$0);
  return 0;
}

struct _M0TPC16string10StringView _M0MPC16string6String11sub_2einner(
  moonbit_string_t _M0L4selfS72,
  int32_t _M0L5startS79,
  int64_t _M0L3endS76
) {
  int32_t _M0L3lenS71;
  int32_t _M0L3endS75;
  int32_t _M0L3endS73;
  #line 923 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
  _M0L3lenS71 = Moonbit_array_length(_M0L4selfS72);
  if (_M0L3endS76 == 4294967296ll) {
    _M0L3endS75 = _M0L3lenS71;
    goto join_74;
  } else {
    int64_t _M0L7_2aSomeS77 = _M0L3endS76;
    int32_t _M0L6_2aendS78 = (int32_t)_M0L7_2aSomeS77;
    _M0L3endS75 = _M0L6_2aendS78;
    goto join_74;
  }
  goto joinlet_1435;
  join_74:;
  _M0L3endS73 = _M0L3endS75;
  joinlet_1435:;
  if (
    _M0L5startS79 >= 0
    && _M0L5startS79 <= _M0L3endS73
    && _M0L3endS73 <= _M0L3lenS71
  ) {
    if (_M0L5startS79 < _M0L3lenS71) {
      int32_t _M0L6_2atmpS720 = _M0L4selfS72[_M0L5startS79];
      int32_t _M0L6_2atmpS719;
      #line 931 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
      _M0L6_2atmpS719
      = _M0MPC16uint166UInt1623is__trailing__surrogate(_M0L6_2atmpS720);
      if (!_M0L6_2atmpS719) {
        
      } else {
        #line 931 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
        moonbit_panic();
      }
    }
    if (_M0L3endS73 < _M0L3lenS71) {
      int32_t _M0L6_2atmpS722 = _M0L4selfS72[_M0L3endS73];
      int32_t _M0L6_2atmpS721;
      #line 934 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
      _M0L6_2atmpS721
      = _M0MPC16uint166UInt1623is__trailing__surrogate(_M0L6_2atmpS722);
      if (!_M0L6_2atmpS721) {
        
      } else {
        #line 934 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
        moonbit_panic();
      }
    }
    moonbit_incref(_M0L4selfS72);
    return (struct _M0TPC16string10StringView){.$0 = _M0L4selfS72,
                                                 .$1 = _M0L5startS79,
                                                 .$2 = _M0L3endS73};
  } else {
    #line 929 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
    moonbit_panic();
  }
}

int32_t _M0IP016_24default__implPB6Logger5writeGRPB13StringBuilderE(
  struct _M0TPB13StringBuilder* _M0L4selfS70,
  struct _M0TPB4Show _M0L4showS69
) {
  struct _M0TPB6Logger _M0L6_2atmpS718;
  #line 116 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  moonbit_incref(_M0L4selfS70);
  _M0L6_2atmpS718
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS70
  };
  #line 117 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L4showS69.$0->$method_0(_M0L4showS69.$1, _M0L6_2atmpS718);
  if (_M0L6_2atmpS718.$1) {
    moonbit_decref(_M0L6_2atmpS718.$1);
  }
  return 0;
}

int32_t _M0IP016_24default__implPB6Logger28write__string__interpolationGRPB13StringBuilderE(
  struct _M0TPB13StringBuilder* _M0L4selfS68,
  struct _M0TPB4Show _M0L4showS67
) {
  struct _M0TPB6Logger _M0L6_2atmpS717;
  #line 111 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  moonbit_incref(_M0L4selfS68);
  _M0L6_2atmpS717
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS68
  };
  #line 112 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L4showS67.$0->$method_0(_M0L4showS67.$1, _M0L6_2atmpS717);
  if (_M0L6_2atmpS717.$1) {
    moonbit_decref(_M0L6_2atmpS717.$1);
  }
  return 0;
}

uint64_t _M0MPC13int3Int10to__uint64(int32_t _M0L4selfS66) {
  int64_t _M0L6_2atmpS716;
  #line 989 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS716 = (int64_t)_M0L4selfS66;
  return *(uint64_t*)&_M0L6_2atmpS716;
}

int32_t _M0IPC16uint166UInt16PB7Default7default() {
  #line 201 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uint16_char.mbt"
  return 0;
}

int32_t _M0MPC16uint166UInt1623is__trailing__surrogate(int32_t _M0L4selfS65) {
  #line 58 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uint16_char.mbt"
  return _M0L4selfS65 >= 56320 && _M0L4selfS65 <= 57343;
}

int32_t _M0IPB13StringBuilderPB6Logger13write__string(
  struct _M0TPB13StringBuilder* _M0L4selfS64,
  moonbit_string_t _M0L3strS62
) {
  int32_t _M0L8str__lenS61;
  int32_t _M0L3lenS715;
  int32_t _M0L8requiredS63;
  uint16_t* _M0L4dataS710;
  int32_t _M0L6_2atmpS709;
  int32_t _if__result_1436;
  uint16_t* _M0L4dataS711;
  int32_t _M0L3lenS712;
  int32_t _M0L3lenS714;
  int32_t _M0L6_2atmpS713;
  #line 105 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L8str__lenS61 = Moonbit_array_length(_M0L3strS62);
  if (_M0L8str__lenS61 == 0) {
    return 0;
  }
  _M0L3lenS715 = _M0L4selfS64->$1;
  _M0L8requiredS63 = _M0L3lenS715 + _M0L8str__lenS61;
  _M0L4dataS710 = _M0L4selfS64->$0;
  _M0L6_2atmpS709 = Moonbit_array_length(_M0L4dataS710);
  if (_M0L8requiredS63 > _M0L6_2atmpS709) {
    _if__result_1436 = 1;
  } else {
    int32_t _M0L3lenS708 = _M0L4selfS64->$1;
    _if__result_1436 = _M0L8requiredS63 < _M0L3lenS708;
  }
  if (_if__result_1436) {
    #line 112 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0MPB13StringBuilder4grow(_M0L4selfS64, _M0L8requiredS63);
  }
  _M0L4dataS711 = _M0L4selfS64->$0;
  _M0L3lenS712 = _M0L4selfS64->$1;
  moonbit_incref(_M0L4dataS711);
  #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0MPC15array10FixedArray26unsafe__blit__from__string(_M0L4dataS711, _M0L3lenS712, _M0L3strS62, 0, _M0L8str__lenS61);
  moonbit_decref(_M0L4dataS711);
  _M0L3lenS714 = _M0L4selfS64->$1;
  _M0L6_2atmpS713 = _M0L3lenS714 + _M0L8str__lenS61;
  _M0L4selfS64->$1 = _M0L6_2atmpS713;
  return 0;
}

int32_t _M0MPC15array10FixedArray26unsafe__blit__from__string(
  uint16_t* _M0L4selfS57,
  int32_t _M0L11dst__offsetS60,
  moonbit_string_t _M0L3strS58,
  int32_t _M0L11str__offsetS53,
  int32_t _M0L3lenS54
) {
  int32_t _M0L16end__str__offsetS52;
  int32_t _M0L1iS55;
  int32_t _M0L1jS56;
  #line 90 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L16end__str__offsetS52 = _M0L11str__offsetS53 + _M0L3lenS54;
  _M0L1iS55 = _M0L11str__offsetS53;
  _M0L1jS56 = _M0L11dst__offsetS60;
  while (1) {
    if (_M0L1iS55 < _M0L16end__str__offsetS52) {
      int32_t _M0L6_2atmpS705 = _M0L3strS58[_M0L1iS55];
      int32_t _M0L6_2atmpS706;
      int32_t _M0L6_2atmpS707;
      _M0L4selfS57[_M0L1jS56] = _M0L6_2atmpS705;
      _M0L6_2atmpS706 = _M0L1iS55 + 1;
      _M0L6_2atmpS707 = _M0L1jS56 + 1;
      _M0L1iS55 = _M0L6_2atmpS706;
      _M0L1jS56 = _M0L6_2atmpS707;
      continue;
    }
    break;
  }
  return 0;
}

int32_t _M0IPB13StringBuilderPB6Logger11write__char(
  struct _M0TPB13StringBuilder* _M0L4selfS50,
  int32_t _M0L2chS49
) {
  uint32_t _M0L4codeS48;
  #line 120 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  #line 121 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L4codeS48 = _M0MPC14char4Char8to__uint(_M0L2chS49);
  if (_M0L4codeS48 <= 65535u) {
    int32_t _M0L3lenS676 = _M0L4selfS50->$1;
    uint16_t* _M0L4dataS678 = _M0L4selfS50->$0;
    int32_t _M0L6_2atmpS677 = Moonbit_array_length(_M0L4dataS678);
    uint16_t* _M0L4dataS681;
    int32_t _M0L3lenS682;
    int32_t _M0L6_2atmpS683;
    int32_t _M0L3lenS685;
    int32_t _M0L6_2atmpS684;
    if (_M0L3lenS676 >= _M0L6_2atmpS677) {
      int32_t _M0L3lenS680 = _M0L4selfS50->$1;
      int32_t _M0L6_2atmpS679 = _M0L3lenS680 + 1;
      #line 124 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0MPB13StringBuilder4grow(_M0L4selfS50, _M0L6_2atmpS679);
    }
    _M0L4dataS681 = _M0L4selfS50->$0;
    _M0L3lenS682 = _M0L4selfS50->$1;
    moonbit_incref(_M0L4dataS681);
    #line 126 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0L6_2atmpS683 = _M0MPC14uint4UInt10to__uint16(_M0L4codeS48);
    if (
      _M0L3lenS682 < 0 || _M0L3lenS682 >= Moonbit_array_length(_M0L4dataS681)
    ) {
      #line 126 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      moonbit_panic();
    }
    _M0L4dataS681[_M0L3lenS682] = _M0L6_2atmpS683;
    moonbit_decref(_M0L4dataS681);
    _M0L3lenS685 = _M0L4selfS50->$1;
    _M0L6_2atmpS684 = _M0L3lenS685 + 1;
    _M0L4selfS50->$1 = _M0L6_2atmpS684;
  } else if (_M0L4codeS48 <= 1114111u) {
    uint16_t* _M0L4dataS689 = _M0L4selfS50->$0;
    int32_t _M0L6_2atmpS687 = Moonbit_array_length(_M0L4dataS689);
    int32_t _M0L3lenS688 = _M0L4selfS50->$1;
    int32_t _M0L6_2atmpS686 = _M0L6_2atmpS687 - _M0L3lenS688;
    uint32_t _M0L4codeS51;
    uint16_t* _M0L4dataS692;
    int32_t _M0L3lenS693;
    uint32_t _M0L6_2atmpS696;
    uint32_t _M0L6_2atmpS695;
    int32_t _M0L6_2atmpS694;
    uint16_t* _M0L4dataS697;
    int32_t _M0L3lenS702;
    int32_t _M0L6_2atmpS698;
    uint32_t _M0L6_2atmpS701;
    uint32_t _M0L6_2atmpS700;
    int32_t _M0L6_2atmpS699;
    int32_t _M0L3lenS704;
    int32_t _M0L6_2atmpS703;
    if (_M0L6_2atmpS686 < 2) {
      int32_t _M0L3lenS691 = _M0L4selfS50->$1;
      int32_t _M0L6_2atmpS690 = _M0L3lenS691 + 2;
      #line 130 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0MPB13StringBuilder4grow(_M0L4selfS50, _M0L6_2atmpS690);
    }
    _M0L4codeS51 = _M0L4codeS48 - 65536u;
    _M0L4dataS692 = _M0L4selfS50->$0;
    _M0L3lenS693 = _M0L4selfS50->$1;
    _M0L6_2atmpS696 = _M0L4codeS51 >> 10;
    _M0L6_2atmpS695 = 55296u + _M0L6_2atmpS696;
    moonbit_incref(_M0L4dataS692);
    #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0L6_2atmpS694 = _M0MPC14uint4UInt10to__uint16(_M0L6_2atmpS695);
    if (
      _M0L3lenS693 < 0 || _M0L3lenS693 >= Moonbit_array_length(_M0L4dataS692)
    ) {
      #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      moonbit_panic();
    }
    _M0L4dataS692[_M0L3lenS693] = _M0L6_2atmpS694;
    moonbit_decref(_M0L4dataS692);
    _M0L4dataS697 = _M0L4selfS50->$0;
    _M0L3lenS702 = _M0L4selfS50->$1;
    _M0L6_2atmpS698 = _M0L3lenS702 + 1;
    _M0L6_2atmpS701 = _M0L4codeS51 & 1023u;
    _M0L6_2atmpS700 = 56320u + _M0L6_2atmpS701;
    moonbit_incref(_M0L4dataS697);
    #line 134 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0L6_2atmpS699 = _M0MPC14uint4UInt10to__uint16(_M0L6_2atmpS700);
    if (
      _M0L6_2atmpS698 < 0
      || _M0L6_2atmpS698 >= Moonbit_array_length(_M0L4dataS697)
    ) {
      #line 134 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      moonbit_panic();
    }
    _M0L4dataS697[_M0L6_2atmpS698] = _M0L6_2atmpS699;
    moonbit_decref(_M0L4dataS697);
    _M0L3lenS704 = _M0L4selfS50->$1;
    _M0L6_2atmpS703 = _M0L3lenS704 + 2;
    _M0L4selfS50->$1 = _M0L6_2atmpS703;
  } else {
    #line 137 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_11.data);
  }
  return 0;
}

int32_t _M0MPB13StringBuilder4grow(
  struct _M0TPB13StringBuilder* _M0L4selfS45,
  int32_t _M0L8requiredS46
) {
  uint16_t* _M0L4dataS675;
  int32_t _M0L6_2atmpS673;
  int32_t _M0L3lenS674;
  int32_t _M0L13new__capacityS44;
  uint16_t* _M0L4dataS670;
  int32_t _M0L6_2atmpS671;
  int32_t _M0L3lenS672;
  uint16_t* _M0L9new__dataS47;
  uint16_t* _M0L6_2aoldS1373;
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L4dataS675 = _M0L4selfS45->$0;
  _M0L6_2atmpS673 = Moonbit_array_length(_M0L4dataS675);
  _M0L3lenS674 = _M0L4selfS45->$1;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L13new__capacityS44
  = _M0FPB31stringbuilder__growth__capacity(_M0L6_2atmpS673, _M0L3lenS674, _M0L8requiredS46);
  _M0L4dataS670 = _M0L4selfS45->$0;
  moonbit_incref(_M0L4dataS670);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L6_2atmpS671 = _M0IPC16uint166UInt16PB7Default7default();
  _M0L3lenS672 = _M0L4selfS45->$1;
  #line 80 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L9new__dataS47
  = _M0MPC15array10FixedArray23make__and__blit_2einnerGkE(_M0L4dataS670, _M0L13new__capacityS44, _M0L6_2atmpS671, _M0L3lenS672, 0, 0);
  _M0L6_2aoldS1373 = _M0L4selfS45->$0;
  moonbit_decref(_M0L6_2aoldS1373);
  _M0L4selfS45->$0 = _M0L9new__dataS47;
  return 0;
}

int32_t _M0FPB31stringbuilder__growth__capacity(
  int32_t _M0L7currentS43,
  int32_t _M0L3lenS39,
  int32_t _M0L8requiredS38
) {
  int32_t _M0L5spaceS40;
  #line 48 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  if (_M0L8requiredS38 < _M0L3lenS39) {
    #line 55 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_12.data);
  }
  _M0L5spaceS40 = _M0L7currentS43;
  while (1) {
    if (_M0L5spaceS40 < _M0L8requiredS38) {
      int32_t _M0L4nextS41 = _M0L5spaceS40 * 2;
      if (_M0L4nextS41 <= _M0L5spaceS40) {
        return _M0L8requiredS38;
      }
      _M0L5spaceS40 = _M0L4nextS41;
      continue;
    } else {
      return _M0L5spaceS40;
    }
    break;
  }
}

int32_t _M0MPC14uint4UInt10to__uint16(uint32_t _M0L4selfS37) {
  int32_t _M0L6_2atmpS669;
  #line 2785 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS669 = *(int32_t*)&_M0L4selfS37;
  return (uint16_t)_M0L6_2atmpS669;
}

uint32_t _M0MPC14char4Char8to__uint(int32_t _M0L4selfS36) {
  int32_t _M0L6_2atmpS668;
  #line 1335 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS668 = _M0L4selfS36;
  return *(uint32_t*)&_M0L6_2atmpS668;
}

moonbit_string_t _M0MPB13StringBuilder10to__string(
  struct _M0TPB13StringBuilder* _M0L4selfS34
) {
  int32_t _M0L3lenS659;
  #line 181 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L3lenS659 = _M0L4selfS34->$1;
  if (_M0L3lenS659 == 0) {
    return (moonbit_string_t)moonbit_string_literal_4.data;
  } else {
    int32_t _M0L3lenS660 = _M0L4selfS34->$1;
    uint16_t* _M0L4dataS662 = _M0L4selfS34->$0;
    int32_t _M0L6_2atmpS661 = Moonbit_array_length(_M0L4dataS662);
    if (_M0L3lenS660 == _M0L6_2atmpS661) {
      uint16_t* _M0L4dataS663 = _M0L4selfS34->$0;
      moonbit_incref(_M0L4dataS663);
      return _M0L4dataS663;
    } else {
      uint16_t* _M0L4dataS664 = _M0L4selfS34->$0;
      int32_t _M0L3lenS665 = _M0L4selfS34->$1;
      int32_t _M0L6_2atmpS666;
      int32_t _M0L3lenS667;
      uint16_t* _M0L4dataS35;
      moonbit_incref(_M0L4dataS664);
      #line 190 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0L6_2atmpS666 = _M0IPC16uint166UInt16PB7Default7default();
      _M0L3lenS667 = _M0L4selfS34->$1;
      #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0L4dataS35
      = _M0MPC15array10FixedArray23make__and__blit_2einnerGkE(_M0L4dataS664, _M0L3lenS665, _M0L6_2atmpS666, _M0L3lenS667, 0, 0);
      return _M0L4dataS35;
    }
  }
}

uint16_t* _M0MPC15array10FixedArray23make__and__blit_2einnerGkE(
  uint16_t* _M0L3srcS31,
  int32_t _M0L13allocate__lenS27,
  int32_t _M0L4initS32,
  int32_t _M0L3lenS28,
  int32_t _M0L11src__offsetS29,
  int32_t _M0L11dst__offsetS30
) {
  int32_t _if__result_1439;
  #line 97 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
  if (_M0L13allocate__lenS27 >= 0) {
    if (_M0L3lenS28 >= 0) {
      if (_M0L11src__offsetS29 >= 0) {
        if (_M0L11dst__offsetS30 >= 0) {
          int32_t _M0L6_2atmpS655 = _M0L11src__offsetS29 + _M0L3lenS28;
          int32_t _M0L6_2atmpS656 = Moonbit_array_length(_M0L3srcS31);
          if (_M0L6_2atmpS655 <= _M0L6_2atmpS656) {
            int32_t _M0L6_2atmpS654 = _M0L11dst__offsetS30 + _M0L3lenS28;
            _if__result_1439 = _M0L6_2atmpS654 <= _M0L13allocate__lenS27;
          } else {
            _if__result_1439 = 0;
          }
        } else {
          _if__result_1439 = 0;
        }
      } else {
        _if__result_1439 = 0;
      }
    } else {
      _if__result_1439 = 0;
    }
  } else {
    _if__result_1439 = 0;
  }
  if (_if__result_1439) {
    #line 116 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    return _M0MPC15array10FixedArray23unsafe__make__and__blitGkE(_M0L3srcS31, _M0L13allocate__lenS27, _M0L4initS32, _M0L11src__offsetS29, _M0L11dst__offsetS30, _M0L3lenS28);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS33;
    int32_t _M0L6_2atmpS658;
    moonbit_string_t _M0L6_2atmpS657;
    uint16_t* _result_1440;
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0L18_2astring__builderS33
    = _M0MPB13StringBuilder21StringBuilder_2einner(89);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS33, (moonbit_string_t)moonbit_string_literal_13.data);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS33, _M0L13allocate__lenS27);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS33, (moonbit_string_t)moonbit_string_literal_14.data);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS33, _M0L11src__offsetS29);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS33, (moonbit_string_t)moonbit_string_literal_15.data);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS33, _M0L11dst__offsetS30);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS33, (moonbit_string_t)moonbit_string_literal_16.data);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS33, _M0L3lenS28);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS33, (moonbit_string_t)moonbit_string_literal_17.data);
    _M0L6_2atmpS658 = Moonbit_array_length(_M0L3srcS31);
    moonbit_decref(_M0L3srcS31);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS33, _M0L6_2atmpS658);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0L6_2atmpS657
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS33);
    moonbit_decref(_M0L18_2astring__builderS33);
    #line 112 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _result_1440 = _M0FPC15abort5abortGAkE(_M0L6_2atmpS657);
    moonbit_decref(_M0L6_2atmpS657);
    return _result_1440;
  }
}

uint16_t* _M0MPC15array10FixedArray23unsafe__make__and__blitGkE(
  uint16_t* _M0L3srcS24,
  int32_t _M0L13allocate__lenS21,
  int32_t _M0L4initS22,
  int32_t _M0L11src__offsetS25,
  int32_t _M0L11dst__offsetS23,
  int32_t _M0L9blit__lenS26
) {
  uint16_t* _M0L3dstS20;
  #line 79 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
  _M0L3dstS20
  = (uint16_t*)moonbit_make_string(_M0L13allocate__lenS21, _M0L4initS22);
  moonbit_incref(_M0L3dstS20);
  #line 90 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
  moonbit_unsafe_val_array_blit(_M0L3dstS20, _M0L11dst__offsetS23, _M0L3srcS24, _M0L11src__offsetS25, _M0L9blit__lenS26, sizeof(uint16_t));
  return _M0L3dstS20;
}

struct _M0TPB13StringBuilder* _M0MPB13StringBuilder21StringBuilder_2einner(
  int32_t _M0L10size__hintS18
) {
  int32_t _M0L7initialS17;
  uint16_t* _M0L4dataS19;
  struct _M0TPB13StringBuilder* _block_1441;
  #line 32 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  if (_M0L10size__hintS18 < 1) {
    _M0L7initialS17 = 1;
  } else {
    int32_t _M0L6_2atmpS653 = _M0L10size__hintS18 + 1;
    _M0L7initialS17 = _M0L6_2atmpS653 / 2;
  }
  _M0L4dataS19 = (uint16_t*)moonbit_make_string(_M0L7initialS17, 0);
  _block_1441
  = (struct _M0TPB13StringBuilder*)moonbit_malloc(sizeof(struct _M0TPB13StringBuilder));
  Moonbit_object_header(_block_1441)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 15, 0);
  _block_1441->$0 = _M0L4dataS19;
  _block_1441->$1 = 0;
  return _block_1441;
}

int32_t _M0MPB13StringBuilder13write__objectGmE(
  struct _M0TPB13StringBuilder* _M0L4selfS14,
  uint64_t _M0L3objS13
) {
  struct _M0TPB6Logger _M0L6_2atmpS651;
  #line 17 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  moonbit_incref(_M0L4selfS14);
  _M0L6_2atmpS651
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS14
  };
  #line 23 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  _M0IP016_24default__implPB4Show6outputGmE(_M0L3objS13, _M0L6_2atmpS651);
  if (_M0L6_2atmpS651.$1) {
    moonbit_decref(_M0L6_2atmpS651.$1);
  }
  return 0;
}

int32_t _M0MPB13StringBuilder13write__objectGiE(
  struct _M0TPB13StringBuilder* _M0L4selfS16,
  int32_t _M0L3objS15
) {
  struct _M0TPB6Logger _M0L6_2atmpS652;
  #line 17 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  moonbit_incref(_M0L4selfS16);
  _M0L6_2atmpS652
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS16
  };
  #line 23 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  _M0IP016_24default__implPB4Show6outputGiE(_M0L3objS15, _M0L6_2atmpS652);
  if (_M0L6_2atmpS652.$1) {
    moonbit_decref(_M0L6_2atmpS652.$1);
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
        int32_t _M0L6_2atmpS642 = _M0L11dst__offsetS6 + _M0L1iS8;
        int32_t _M0L6_2atmpS644 = _M0L11src__offsetS7 + _M0L1iS8;
        int32_t _M0L6_2atmpS643;
        int32_t _M0L6_2atmpS645;
        if (
          _M0L6_2atmpS644 < 0
          || _M0L6_2atmpS644 >= Moonbit_array_length(_M0L3srcS5)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS643 = (int32_t)_M0L3srcS5[_M0L6_2atmpS644];
        if (
          _M0L6_2atmpS642 < 0
          || _M0L6_2atmpS642 >= Moonbit_array_length(_M0L3dstS4)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS4[_M0L6_2atmpS642] = _M0L6_2atmpS643;
        _M0L6_2atmpS645 = _M0L1iS8 + 1;
        _M0L1iS8 = _M0L6_2atmpS645;
        continue;
      } else {
        moonbit_decref(_M0L3srcS5);
        moonbit_decref(_M0L3dstS4);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS650 = _M0L3lenS9 - 1;
    int32_t _M0L1iS11 = _M0L6_2atmpS650;
    while (1) {
      if (_M0L1iS11 >= 0) {
        int32_t _M0L6_2atmpS646 = _M0L11dst__offsetS6 + _M0L1iS11;
        int32_t _M0L6_2atmpS648 = _M0L11src__offsetS7 + _M0L1iS11;
        int32_t _M0L6_2atmpS647;
        int32_t _M0L6_2atmpS649;
        if (
          _M0L6_2atmpS648 < 0
          || _M0L6_2atmpS648 >= Moonbit_array_length(_M0L3srcS5)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS647 = (int32_t)_M0L3srcS5[_M0L6_2atmpS648];
        if (
          _M0L6_2atmpS646 < 0
          || _M0L6_2atmpS646 >= Moonbit_array_length(_M0L3dstS4)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS4[_M0L6_2atmpS646] = _M0L6_2atmpS647;
        _M0L6_2atmpS649 = _M0L1iS11 - 1;
        _M0L1iS11 = _M0L6_2atmpS649;
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

int32_t _M0FPC15abort5abortGiE(moonbit_string_t _M0L3msgS2) {
  #line 47 "C:\\Users\\31379\\.moon\\lib\\core\\abort\\abort.mbt"
  #line 49 "C:\\Users\\31379\\.moon\\lib\\core\\abort\\abort.mbt"
  moonbit_println(_M0L3msgS2);
  #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\abort\\abort.mbt"
  moonbit_panic();
}

uint16_t* _M0FPC15abort5abortGAkE(moonbit_string_t _M0L3msgS3) {
  #line 47 "C:\\Users\\31379\\.moon\\lib\\core\\abort\\abort.mbt"
  #line 49 "C:\\Users\\31379\\.moon\\lib\\core\\abort\\abort.mbt"
  moonbit_println(_M0L3msgS3);
  #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\abort\\abort.mbt"
  moonbit_panic();
}

int32_t _M0IP016_24default__implPB6Logger61write_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLoggerGRPB13StringBuilderE(
  void* _M0L11_2aobj__ptrS616,
  struct _M0TPB4Show _M0L8_2aparamS615
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS614 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS616;
  _M0IP016_24default__implPB6Logger5writeGRPB13StringBuilderE(_M0L7_2aselfS614, _M0L8_2aparamS615);
  return 0;
}

int32_t _M0IP016_24default__implPB6Logger84write__string__interpolation_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLoggerGRPB13StringBuilderE(
  void* _M0L11_2aobj__ptrS613,
  struct _M0TPB4Show _M0L8_2aparamS612
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS611 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS613;
  _M0IP016_24default__implPB6Logger28write__string__interpolationGRPB13StringBuilderE(_M0L7_2aselfS611, _M0L8_2aparamS612);
  return 0;
}

int32_t _M0IPB13StringBuilderPB6Logger67write__char_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLogger(
  void* _M0L11_2aobj__ptrS610,
  int32_t _M0L8_2aparamS609
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS608 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS610;
  _M0IPB13StringBuilderPB6Logger11write__char(_M0L7_2aselfS608, _M0L8_2aparamS609);
  return 0;
}

int32_t _M0IPB13StringBuilderPB6Logger67write__view_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLogger(
  void* _M0L11_2aobj__ptrS607,
  struct _M0TPC16string10StringView _M0L8_2aparamS606
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS605 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS607;
  _M0IPB13StringBuilderPB6Logger11write__view(_M0L7_2aselfS605, _M0L8_2aparamS606);
  return 0;
}

int32_t _M0IP016_24default__implPB6Logger72write__substring_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLoggerGRPB13StringBuilderE(
  void* _M0L11_2aobj__ptrS604,
  moonbit_string_t _M0L8_2aparamS601,
  int32_t _M0L8_2aparamS602,
  int32_t _M0L8_2aparamS603
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS600 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS604;
  _M0IP016_24default__implPB6Logger16write__substringGRPB13StringBuilderE(_M0L7_2aselfS600, _M0L8_2aparamS601, _M0L8_2aparamS602, _M0L8_2aparamS603);
  return 0;
}

int32_t _M0IPB13StringBuilderPB6Logger69write__string_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLogger(
  void* _M0L11_2aobj__ptrS599,
  moonbit_string_t _M0L8_2aparamS598
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS597 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS599;
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L7_2aselfS597, _M0L8_2aparamS598);
  return 0;
}

void moonbit_init() {
  moonbit_layout_table = moonbit_layout_table_data;
}

int main(int argc, char** argv) {
  int32_t _M0L1nS590;
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS591;
  struct _M0TP26RiantR8snn__mbt11IZParameter* _M0L5paramS592;
  struct _M0TP26RiantR8snn__mbt2IZ* _M0L3popS593;
  struct _M0TPB5ArrayGfE* _M0L1iS617;
  float _M0L6_2atmpS641;
  float _M0L6_2atmpS640;
  int32_t _M0L12total__stepsS594;
  float _M0L2dtS595;
  int32_t _M0L6spikesS596;
  struct _M0TPB5ArrayGfE* _M0L1vS621;
  float _M0L6_2atmpS620;
  moonbit_string_t _M0L6_2atmpS619;
  moonbit_string_t _M0L6_2atmpS618;
  struct _M0TPB5ArrayGfE* _M0L1uS625;
  float _M0L6_2atmpS624;
  moonbit_string_t _M0L6_2atmpS623;
  moonbit_string_t _M0L6_2atmpS622;
  struct _M0TPB5ArrayGfE* _M0L2geS629;
  float _M0L6_2atmpS628;
  moonbit_string_t _M0L6_2atmpS627;
  moonbit_string_t _M0L6_2atmpS626;
  struct _M0TPB5ArrayGfE* _M0L2giS633;
  float _M0L6_2atmpS632;
  moonbit_string_t _M0L6_2atmpS631;
  moonbit_string_t _M0L6_2atmpS630;
  struct _M0TPB5ArrayGbE* _M0L4fireS637;
  int32_t _M0L6_2acntS1374;
  int32_t _M0L6_2atmpS636;
  moonbit_string_t _M0L6_2atmpS635;
  moonbit_string_t _M0L6_2atmpS634;
  moonbit_string_t _M0L6_2atmpS639;
  moonbit_string_t _M0L6_2atmpS638;
  moonbit_runtime_init(argc, argv);
  moonbit_init();
  _M0L1nS590 = 1;
  #line 9 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\izhikevich\\main.mbt"
  _M0L3rngS591 = _M0MP26RiantR8snn__mbt7Xoshiro7default();
  #line 10 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\izhikevich\\main.mbt"
  _M0L5paramS592 = _M0MP26RiantR8snn__mbt11IZParameter2rs();
  #line 11 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\izhikevich\\main.mbt"
  _M0L3popS593
  = _M0MP26RiantR8snn__mbt2IZ3new(_M0L1nS590, _M0L5paramS592, _M0L3rngS591);
  moonbit_decref(_M0L5paramS592);
  moonbit_decref(_M0L3rngS591);
  _M0L1iS617 = _M0L3popS593->$5;
  moonbit_incref(_M0L1iS617);
  #line 14 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\izhikevich\\main.mbt"
  _M0MPC15array5Array3setGfE(_M0L1iS617, 0, 0x1.4p+3f);
  moonbit_decref(_M0L1iS617);
  _M0L6_2atmpS641 = 0x1p+1f * 0x1.f4p+9f;
  _M0L6_2atmpS640 = _M0L6_2atmpS641 / 0x1p-3f;
  #line 17 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\izhikevich\\main.mbt"
  _M0L12total__stepsS594 = _M0MPC15float5Float7to__int(_M0L6_2atmpS640);
  _M0L2dtS595 = 0x1p-3f;
  #line 19 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\izhikevich\\main.mbt"
  _M0L6spikesS596
  = _M0FP46RiantR8snn__mbt8examples10izhikevich15run__izhikevich(_M0L3popS593, _M0L12total__stepsS594, _M0L2dtS595);
  #line 20 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\izhikevich\\main.mbt"
  _M0FPB7printlnGsE((moonbit_string_t)moonbit_string_literal_18.data);
  _M0L1vS621 = _M0L3popS593->$2;
  moonbit_incref(_M0L1vS621);
  #line 21 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\izhikevich\\main.mbt"
  _M0L6_2atmpS620 = _M0MPC15array5Array2atGfE(_M0L1vS621, 0);
  moonbit_decref(_M0L1vS621);
  #line 21 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\izhikevich\\main.mbt"
  _M0L6_2atmpS619 = _M0IPC15float5FloatPB4Show10to__string(_M0L6_2atmpS620);
  #line 21 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\izhikevich\\main.mbt"
  _M0L6_2atmpS618
  = moonbit_add_string((moonbit_string_t)moonbit_string_literal_19.data, _M0L6_2atmpS619);
  moonbit_decref(_M0L6_2atmpS619);
  #line 21 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\izhikevich\\main.mbt"
  _M0FPB7printlnGsE(_M0L6_2atmpS618);
  moonbit_decref(_M0L6_2atmpS618);
  _M0L1uS625 = _M0L3popS593->$3;
  moonbit_incref(_M0L1uS625);
  #line 22 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\izhikevich\\main.mbt"
  _M0L6_2atmpS624 = _M0MPC15array5Array2atGfE(_M0L1uS625, 0);
  moonbit_decref(_M0L1uS625);
  #line 22 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\izhikevich\\main.mbt"
  _M0L6_2atmpS623 = _M0IPC15float5FloatPB4Show10to__string(_M0L6_2atmpS624);
  #line 22 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\izhikevich\\main.mbt"
  _M0L6_2atmpS622
  = moonbit_add_string((moonbit_string_t)moonbit_string_literal_20.data, _M0L6_2atmpS623);
  moonbit_decref(_M0L6_2atmpS623);
  #line 22 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\izhikevich\\main.mbt"
  _M0FPB7printlnGsE(_M0L6_2atmpS622);
  moonbit_decref(_M0L6_2atmpS622);
  _M0L2geS629 = _M0L3popS593->$6;
  moonbit_incref(_M0L2geS629);
  #line 23 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\izhikevich\\main.mbt"
  _M0L6_2atmpS628 = _M0MPC15array5Array2atGfE(_M0L2geS629, 0);
  moonbit_decref(_M0L2geS629);
  #line 23 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\izhikevich\\main.mbt"
  _M0L6_2atmpS627 = _M0IPC15float5FloatPB4Show10to__string(_M0L6_2atmpS628);
  #line 23 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\izhikevich\\main.mbt"
  _M0L6_2atmpS626
  = moonbit_add_string((moonbit_string_t)moonbit_string_literal_21.data, _M0L6_2atmpS627);
  moonbit_decref(_M0L6_2atmpS627);
  #line 23 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\izhikevich\\main.mbt"
  _M0FPB7printlnGsE(_M0L6_2atmpS626);
  moonbit_decref(_M0L6_2atmpS626);
  _M0L2giS633 = _M0L3popS593->$7;
  moonbit_incref(_M0L2giS633);
  #line 24 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\izhikevich\\main.mbt"
  _M0L6_2atmpS632 = _M0MPC15array5Array2atGfE(_M0L2giS633, 0);
  moonbit_decref(_M0L2giS633);
  #line 24 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\izhikevich\\main.mbt"
  _M0L6_2atmpS631 = _M0IPC15float5FloatPB4Show10to__string(_M0L6_2atmpS632);
  #line 24 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\izhikevich\\main.mbt"
  _M0L6_2atmpS630
  = moonbit_add_string((moonbit_string_t)moonbit_string_literal_22.data, _M0L6_2atmpS631);
  moonbit_decref(_M0L6_2atmpS631);
  #line 24 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\izhikevich\\main.mbt"
  _M0FPB7printlnGsE(_M0L6_2atmpS630);
  moonbit_decref(_M0L6_2atmpS630);
  _M0L4fireS637 = _M0L3popS593->$4;
  _M0L6_2acntS1374 = Moonbit_rc_count(Moonbit_object_header(_M0L3popS593));
  if (_M0L6_2acntS1374 > 1) {
    int32_t _M0L11_2anew__cntS1381 = _M0L6_2acntS1374 - 1;
    Moonbit_set_rc_count(Moonbit_object_header(_M0L3popS593), _M0L11_2anew__cntS1381);
    moonbit_incref(_M0L4fireS637);
  } else if (_M0L6_2acntS1374 == 1) {
    struct _M0TPB5ArrayGfE* _M0L8_2afieldS1380 = _M0L3popS593->$7;
    struct _M0TPB5ArrayGfE* _M0L8_2afieldS1379;
    struct _M0TPB5ArrayGfE* _M0L8_2afieldS1378;
    struct _M0TPB5ArrayGfE* _M0L8_2afieldS1377;
    struct _M0TPB5ArrayGfE* _M0L8_2afieldS1376;
    struct _M0TP26RiantR8snn__mbt11IZParameter* _M0L8_2afieldS1375;
    moonbit_decref(_M0L8_2afieldS1380);
    _M0L8_2afieldS1379 = _M0L3popS593->$6;
    moonbit_decref(_M0L8_2afieldS1379);
    _M0L8_2afieldS1378 = _M0L3popS593->$5;
    moonbit_decref(_M0L8_2afieldS1378);
    _M0L8_2afieldS1377 = _M0L3popS593->$3;
    moonbit_decref(_M0L8_2afieldS1377);
    _M0L8_2afieldS1376 = _M0L3popS593->$2;
    moonbit_decref(_M0L8_2afieldS1376);
    _M0L8_2afieldS1375 = _M0L3popS593->$0;
    moonbit_decref(_M0L8_2afieldS1375);
    #line 19 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\izhikevich\\main.mbt"
    moonbit_free(_M0L3popS593);
  }
  #line 25 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\izhikevich\\main.mbt"
  _M0L6_2atmpS636 = _M0MPC15array5Array2atGbE(_M0L4fireS637, 0);
  moonbit_decref(_M0L4fireS637);
  #line 25 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\izhikevich\\main.mbt"
  _M0L6_2atmpS635 = _M0IPC14bool4BoolPB4Show10to__string(_M0L6_2atmpS636);
  #line 25 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\izhikevich\\main.mbt"
  _M0L6_2atmpS634
  = moonbit_add_string((moonbit_string_t)moonbit_string_literal_23.data, _M0L6_2atmpS635);
  moonbit_decref(_M0L6_2atmpS635);
  #line 25 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\izhikevich\\main.mbt"
  _M0FPB7printlnGsE(_M0L6_2atmpS634);
  moonbit_decref(_M0L6_2atmpS634);
  #line 26 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\izhikevich\\main.mbt"
  _M0L6_2atmpS639 = _M0MPC13int3Int18to__string_2einner(_M0L6spikesS596, 10);
  #line 26 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\izhikevich\\main.mbt"
  _M0L6_2atmpS638
  = moonbit_add_string((moonbit_string_t)moonbit_string_literal_24.data, _M0L6_2atmpS639);
  moonbit_decref(_M0L6_2atmpS639);
  #line 26 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\izhikevich\\main.mbt"
  _M0FPB7printlnGsE(_M0L6_2atmpS638);
  moonbit_decref(_M0L6_2atmpS638);
  return 0;
}