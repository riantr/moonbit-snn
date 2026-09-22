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
struct _M0TP26RiantR8snn__mbt4AdEx;

struct _M0TP26RiantR8snn__mbt13AdExPostSpike;

struct _M0TP26RiantR8snn__mbt11WCParameter;

struct _M0TP26RiantR8snn__mbt12PoissonLayer;

struct _M0TP26RiantR8snn__mbt15MarkramSTPEntry;

struct _M0DTP26RiantR8snn__mbt6AnyPop8HetRec__;

struct _M0DTP26RiantR8snn__mbt6AnyPop4HH__;

struct _M0TPB17FloatingDecimal64;

struct _M0TP26RiantR8snn__mbt15HetRecParameter;

struct _M0TP26RiantR8snn__mbt16BalancedStimulus;

struct _M0DTP26RiantR8snn__mbt13STDPEntryKind11Symmetric__;

struct _M0TP26RiantR8snn__mbt18STDPConfavreux2025;

struct _M0DTP26RiantR8snn__mbt13STDPEntryKind16Confavreux2025__;

struct _M0DTP26RiantR8snn__mbt6AnyPop4WC__;

struct _M0TP26RiantR8snn__mbt2IF;

struct _M0TPB6Logger;

struct _M0TP26RiantR8snn__mbt26STDPAntiSymmetricVariables;

struct _M0TUmmmmE;

struct _M0TP26RiantR8snn__mbt19MarkramSTPParameter;

struct _M0TPB5ArrayGiE;

struct _M0TPB19MulShiftAll64Result;

struct _M0TP26RiantR8snn__mbt4Time;

struct _M0DTP26RiantR8snn__mbt6AnyPop4ML__;

struct _M0TP26RiantR8snn__mbt11HHParameter;

struct _M0DTP26RiantR8snn__mbt13STDPEntryKind11IstdpRate__;

struct _M0TP26RiantR8snn__mbt18STDPEntrySymmetric;

struct _M0TP26RiantR8snn__mbt14SpikingSynapse;

struct _M0DTP26RiantR8snn__mbt7AnyStim11TimedStim__;

struct _M0TP26RiantR8snn__mbt17CurrentStimulusIF;

struct _M0TPB5ArrayGRP26RiantR8snn__mbt7MonitorE;

struct _M0TPB4Show;

struct _M0TPB8MutLocalGfE;

struct _M0DTP26RiantR8snn__mbt13STDPEntryKind12MexicanHat__;

struct _M0TP26RiantR8snn__mbt9PostSpike;

struct _M0DTP26RiantR8snn__mbt7AnyStim12CurrentArr__;

struct _M0DTP26RiantR8snn__mbt6AnyPop6AdEx__;

struct _M0TP26RiantR8snn__mbt18SpikeTimeParameter;

struct _M0TP26RiantR8snn__mbt10AdExSinExp;

struct _M0TP26RiantR8snn__mbt13STDPSymmetric;

struct _M0TPB5ArrayGbE;

struct _M0TP26RiantR8snn__mbt11WilsonCowan;

struct _M0DTP26RiantR8snn__mbt6AnyPop9Poisson__;

struct _M0TP26RiantR8snn__mbt20PoissonHomoParameter;

struct _M0TP26RiantR8snn__mbt23IstdpPotentialVariables;

struct _M0TPB5ArrayGRP26RiantR8snn__mbt7AnyStimE;

struct _M0KTPB6LoggerTPB13StringBuilder;

struct _M0DTP26RiantR8snn__mbt7AnyStim12BalancedIF__;

struct _M0DTP26RiantR8snn__mbt6AnyPop4IZ__;

struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR;

struct _M0TP26RiantR8snn__mbt2HH;

struct _M0TP26RiantR8snn__mbt2IZ;

struct _M0TP26RiantR8snn__mbt11IFParameter;

struct _M0TP26RiantR8snn__mbt18HeterogeneousModel;

struct _M0TP26RiantR8snn__mbt17BalancedParameter;

struct _M0TP26RiantR8snn__mbt23STDPEntryConfavreux2025;

struct _M0TP26RiantR8snn__mbt20PoissonLayerStimulus;

struct _M0TP26RiantR8snn__mbt19STDPEntryMexicanHat;

struct _M0TUddE;

struct _M0DTP26RiantR8snn__mbt12STPEntryKind15MarkramSTPHet__;

struct _M0TP26RiantR8snn__mbt13STDPVariables;

struct _M0TP26RiantR8snn__mbt18MarkramSTPEntryHet;

struct _M0TP26RiantR8snn__mbt17STDPAntiSymmetric;

struct _M0TP26RiantR8snn__mbt19MarkramSTPVariables;

struct _M0TPB5ArrayGRP26RiantR8snn__mbt6AnyPopE;

struct _M0TP26RiantR8snn__mbt22MarkramSTPParameterHet;

struct _M0TPB5ArrayGRP26RiantR8snn__mbt14SpikingSynapseE;

struct _M0TPB13StringBuilder;

struct _M0TPB5ArrayGfE;

struct _M0TP26RiantR8snn__mbt17PoissonStimulusIF;

struct _M0TP26RiantR8snn__mbt13AdExParameter;

struct _M0TP26RiantR8snn__mbt11IZParameter;

struct _M0DTP26RiantR8snn__mbt7AnyStim11PoissonIF__;

struct _M0TUdiE;

struct _M0DTP26RiantR8snn__mbt13STDPEntryKind16IstdpPotential__;

struct _M0TP26RiantR8snn__mbt9STDPEntry;

struct _M0TP26RiantR8snn__mbt9IstdpRate;

struct _M0TP26RiantR8snn__mbt18IstdpRateVariables;

struct _M0BTPB6Logger;

struct _M0TP26RiantR8snn__mbt23MarkramSTPEntryTimestep;

struct _M0DTP26RiantR8snn__mbt12STPEntryKind12MarkramSTP__;

struct _M0DTP26RiantR8snn__mbt13STDPEntryKind15AntiSymmetric__;

struct _M0TP26RiantR8snn__mbt7Monitor;

struct _M0TP26RiantR8snn__mbt27MarkramSTPParameterTimestep;

struct _M0TP26RiantR8snn__mbt6HetRec;

struct _M0DTP26RiantR8snn__mbt6AnyPop4IF__;

struct _M0DTP26RiantR8snn__mbt6AnyPop12AdExSinExp__;

struct _M0TP26RiantR8snn__mbt7Xoshiro;

struct _M0DTP26RiantR8snn__mbt12STPEntryKind20MarkramSTPTimestep__;

struct _M0DTPC16option6OptionGfE4Some;

struct _M0TP26RiantR8snn__mbt20MorrisLecarParameter;

struct _M0TP26RiantR8snn__mbt19IstdpPotentialEntry;

struct _M0TPB5ArrayGRP26RiantR8snn__mbt13STDPEntryKindE;

struct _M0TP26RiantR8snn__mbt17SpikeTimeStimulus;

struct _M0TP26RiantR8snn__mbt22STDPEntryAntiSymmetric;

struct _M0TPB8MutLocalGiE;

struct _M0TP26RiantR8snn__mbt12STDPGerstner;

struct _M0TP26RiantR8snn__mbt11MorrisLecar;

struct _M0TP26RiantR8snn__mbt7Poisson;

struct _M0TP26RiantR8snn__mbt19AdExSinExpParameter;

struct _M0TPB5ArrayGRPB5ArrayGfEE;

struct _M0DTP26RiantR8snn__mbt7AnyStim14PoissonLayer__;

struct _M0TP26RiantR8snn__mbt20CurrentStimulusArray;

struct _M0DTP26RiantR8snn__mbt13STDPEntryKind10Gerstner__;

struct _M0TPB8MutLocalGdE;

struct _M0BTPB4Show;

struct _M0DTP26RiantR8snn__mbt7AnyStim11CurrentIF__;

struct _M0TPC16string10StringView;

struct _M0TP26RiantR8snn__mbt22STDPSymmetricVariables;

struct _M0TP26RiantR8snn__mbt12PoissonFixed;

struct _M0TP26RiantR8snn__mbt14STDPMexicanHat;

struct _M0TPB5ArrayGRP26RiantR8snn__mbt12STPEntryKindE;

struct _M0TP26RiantR8snn__mbt14IstdpPotential;

struct _M0TP26RiantR8snn__mbt14IstdpRateEntry;

struct _M0TPB7Umul128;

struct _M0TPB8Pow5Pair;

struct _M0TP26RiantR8snn__mbt4AdEx {
  struct _M0TP26RiantR8snn__mbt13AdExParameter* $0;
  struct _M0TP26RiantR8snn__mbt13AdExPostSpike* $1;
  int32_t $2;
  struct _M0TPB5ArrayGfE* $3;
  struct _M0TPB5ArrayGfE* $4;
  struct _M0TPB5ArrayGbE* $5;
  struct _M0TPB5ArrayGfE* $6;
  struct _M0TPB5ArrayGiE* $7;
  struct _M0TPB5ArrayGfE* $8;
  struct _M0TPB5ArrayGfE* $9;
  struct _M0TPB5ArrayGfE* $10;
  struct _M0TPB5ArrayGfE* $11;
  struct _M0TPB5ArrayGfE* $12;
  struct _M0TPB5ArrayGfE* $13;
  struct _M0TPB5ArrayGfE* $14;
  struct _M0TPB5ArrayGfE* $15;
  struct _M0TPB5ArrayGfE* $16;
  struct _M0TPB5ArrayGfE* $17;
  float $18;
  float $19;
  float $20;
  float $21;
  float $22;
  float $23;
  
};

struct _M0TP26RiantR8snn__mbt13AdExPostSpike {
  float $0;
  float $1;
  float $2;
  float $3;
  float $4;
  
};

struct _M0TP26RiantR8snn__mbt11WCParameter {
  float $0;
  
};

struct _M0TP26RiantR8snn__mbt12PoissonLayer {
  float $0;
  int32_t $1;
  struct _M0TPB5ArrayGbE* $2;
  float $3;
  float $4;
  float $5;
  moonbit_string_t $6;
  moonbit_string_t $7;
  
};

struct _M0TP26RiantR8snn__mbt15MarkramSTPEntry {
  int32_t $0;
  struct _M0TP26RiantR8snn__mbt19MarkramSTPVariables* $1;
  struct _M0TP26RiantR8snn__mbt19MarkramSTPParameter* $2;
  
};

struct _M0DTP26RiantR8snn__mbt6AnyPop8HetRec__ {
  struct _M0TP26RiantR8snn__mbt6HetRec* $0;
  
};

struct _M0DTP26RiantR8snn__mbt6AnyPop4HH__ {
  struct _M0TP26RiantR8snn__mbt2HH* $0;
  
};

struct _M0TPB17FloatingDecimal64 {
  uint64_t $0;
  int32_t $1;
  
};

struct _M0TP26RiantR8snn__mbt15HetRecParameter {
  int32_t $0;
  float $1;
  float $2;
  float $3;
  float $4;
  float $5;
  float $6;
  float $7;
  float $8;
  float $9;
  
};

struct _M0TP26RiantR8snn__mbt16BalancedStimulus {
  struct _M0TP26RiantR8snn__mbt17BalancedParameter* $0;
  int32_t $1;
  struct _M0TPB5ArrayGfE* $2;
  struct _M0TPB5ArrayGfE* $3;
  struct _M0TPB5ArrayGbE* $4;
  struct _M0TPB5ArrayGfE* $5;
  struct _M0TPB5ArrayGfE* $6;
  struct _M0TP26RiantR8snn__mbt7Xoshiro* $7;
  
};

struct _M0DTP26RiantR8snn__mbt13STDPEntryKind11Symmetric__ {
  struct _M0TP26RiantR8snn__mbt18STDPEntrySymmetric* $0;
  
};

struct _M0TP26RiantR8snn__mbt18STDPConfavreux2025 {
  float $0;
  float $1;
  float $2;
  float $3;
  float $4;
  float $5;
  float $6;
  float $7;
  float $8;
  
};

struct _M0DTP26RiantR8snn__mbt13STDPEntryKind16Confavreux2025__ {
  struct _M0TP26RiantR8snn__mbt23STDPEntryConfavreux2025* $0;
  
};

struct _M0DTP26RiantR8snn__mbt6AnyPop4WC__ {
  struct _M0TP26RiantR8snn__mbt11WilsonCowan* $0;
  
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

struct _M0TPB6Logger {
  struct _M0BTPB6Logger* $0;
  void* $1;
  
};

struct _M0TP26RiantR8snn__mbt26STDPAntiSymmetricVariables {
  struct _M0TPB5ArrayGfE* $0;
  struct _M0TPB5ArrayGfE* $1;
  
};

struct _M0TUmmmmE {
  uint64_t $0;
  uint64_t $1;
  uint64_t $2;
  uint64_t $3;
  
};

struct _M0TP26RiantR8snn__mbt19MarkramSTPParameter {
  float $0;
  float $1;
  float $2;
  float $3;
  float $4;
  
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

struct _M0DTP26RiantR8snn__mbt6AnyPop4ML__ {
  struct _M0TP26RiantR8snn__mbt11MorrisLecar* $0;
  
};

struct _M0TP26RiantR8snn__mbt11HHParameter {
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
  float $11;
  
};

struct _M0DTP26RiantR8snn__mbt13STDPEntryKind11IstdpRate__ {
  struct _M0TP26RiantR8snn__mbt14IstdpRateEntry* $0;
  
};

struct _M0TP26RiantR8snn__mbt18STDPEntrySymmetric {
  int32_t $0;
  int32_t $1;
  int32_t $2;
  struct _M0TP26RiantR8snn__mbt13STDPSymmetric* $3;
  struct _M0TP26RiantR8snn__mbt22STDPSymmetricVariables* $4;
  struct _M0TPB5ArrayGfE* $5;
  
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

struct _M0DTP26RiantR8snn__mbt7AnyStim11TimedStim__ {
  struct _M0TP26RiantR8snn__mbt17SpikeTimeStimulus* $0;
  float $1;
  
};

struct _M0TP26RiantR8snn__mbt17CurrentStimulusIF {
  float $0;
  struct _M0TPB5ArrayGbE* $1;
  struct _M0TP26RiantR8snn__mbt2IF* $2;
  float $3;
  struct _M0TP26RiantR8snn__mbt7Xoshiro* $4;
  
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

struct _M0DTP26RiantR8snn__mbt13STDPEntryKind12MexicanHat__ {
  struct _M0TP26RiantR8snn__mbt19STDPEntryMexicanHat* $0;
  
};

struct _M0TP26RiantR8snn__mbt9PostSpike {
  float $0;
  
};

struct _M0DTP26RiantR8snn__mbt7AnyStim12CurrentArr__ {
  struct _M0TP26RiantR8snn__mbt20CurrentStimulusArray* $0;
  
};

struct _M0DTP26RiantR8snn__mbt6AnyPop6AdEx__ {
  struct _M0TP26RiantR8snn__mbt4AdEx* $0;
  
};

struct _M0TP26RiantR8snn__mbt18SpikeTimeParameter {
  struct _M0TPB5ArrayGfE* $0;
  struct _M0TPB5ArrayGiE* $1;
  
};

struct _M0TP26RiantR8snn__mbt10AdExSinExp {
  struct _M0TP26RiantR8snn__mbt19AdExSinExpParameter* $0;
  struct _M0TP26RiantR8snn__mbt13AdExPostSpike* $1;
  int32_t $2;
  struct _M0TPB5ArrayGfE* $3;
  struct _M0TPB5ArrayGfE* $4;
  struct _M0TPB5ArrayGbE* $5;
  struct _M0TPB5ArrayGfE* $6;
  struct _M0TPB5ArrayGiE* $7;
  struct _M0TPB5ArrayGfE* $8;
  struct _M0TPB5ArrayGfE* $9;
  struct _M0TPB5ArrayGfE* $10;
  struct _M0TPB5ArrayGfE* $11;
  struct _M0TPB5ArrayGfE* $12;
  struct _M0TPB5ArrayGfE* $13;
  struct _M0TPB5ArrayGfE* $14;
  struct _M0TPB5ArrayGfE* $15;
  float $16;
  float $17;
  float $18;
  float $19;
  
};

struct _M0TP26RiantR8snn__mbt13STDPSymmetric {
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

struct _M0TP26RiantR8snn__mbt11WilsonCowan {
  struct _M0TP26RiantR8snn__mbt11WCParameter* $0;
  int32_t $1;
  struct _M0TPB5ArrayGfE* $2;
  struct _M0TPB5ArrayGfE* $3;
  struct _M0TPB5ArrayGfE* $4;
  struct _M0TPB5ArrayGfE* $5;
  
};

struct _M0DTP26RiantR8snn__mbt6AnyPop9Poisson__ {
  struct _M0TP26RiantR8snn__mbt7Poisson* $0;
  
};

struct _M0TP26RiantR8snn__mbt20PoissonHomoParameter {
  float $0;
  
};

struct _M0TP26RiantR8snn__mbt23IstdpPotentialVariables {
  struct _M0TPB5ArrayGfE* $0;
  struct _M0TPB5ArrayGfE* $1;
  
};

struct _M0TPB5ArrayGRP26RiantR8snn__mbt7AnyStimE {
  void** $0;
  int32_t $1;
  
};

struct _M0KTPB6LoggerTPB13StringBuilder {
  struct _M0BTPB6Logger* $0;
  void* $1;
  
};

struct _M0DTP26RiantR8snn__mbt7AnyStim12BalancedIF__ {
  struct _M0TP26RiantR8snn__mbt16BalancedStimulus* $0;
  
};

struct _M0DTP26RiantR8snn__mbt6AnyPop4IZ__ {
  struct _M0TP26RiantR8snn__mbt2IZ* $0;
  
};

struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR {
  int32_t $0;
  int32_t $1;
  struct _M0TPB5ArrayGiE* $2;
  struct _M0TPB5ArrayGiE* $3;
  struct _M0TPB5ArrayGfE* $4;
  
};

struct _M0TP26RiantR8snn__mbt2HH {
  struct _M0TP26RiantR8snn__mbt11HHParameter* $0;
  int32_t $1;
  struct _M0TPB5ArrayGfE* $2;
  struct _M0TPB5ArrayGfE* $3;
  struct _M0TPB5ArrayGfE* $4;
  struct _M0TPB5ArrayGfE* $5;
  struct _M0TPB5ArrayGbE* $6;
  struct _M0TPB5ArrayGfE* $7;
  struct _M0TPB5ArrayGfE* $8;
  struct _M0TPB5ArrayGfE* $9;
  
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

struct _M0TP26RiantR8snn__mbt18HeterogeneousModel {
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt6AnyPopE* $0;
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt14SpikingSynapseE* $1;
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt7AnyStimE* $2;
  struct _M0TP26RiantR8snn__mbt4Time* $3;
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt7MonitorE* $4;
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt13STDPEntryKindE* $5;
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt12STPEntryKindE* $6;
  
};

struct _M0TP26RiantR8snn__mbt17BalancedParameter {
  float $0;
  float $1;
  float $2;
  float $3;
  float $4;
  float $5;
  int32_t $6;
  
};

struct _M0TP26RiantR8snn__mbt23STDPEntryConfavreux2025 {
  int32_t $0;
  int32_t $1;
  int32_t $2;
  struct _M0TP26RiantR8snn__mbt18STDPConfavreux2025* $3;
  struct _M0TP26RiantR8snn__mbt13STDPVariables* $4;
  struct _M0TPB5ArrayGfE* $5;
  
};

struct _M0TP26RiantR8snn__mbt20PoissonLayerStimulus {
  struct _M0TP26RiantR8snn__mbt12PoissonLayer* $0;
  struct _M0TP26RiantR8snn__mbt2IF* $1;
  moonbit_string_t $2;
  struct _M0TPB5ArrayGbE* $3;
  struct _M0TPB5ArrayGfE* $4;
  struct _M0TPB5ArrayGbE* $5;
  struct _M0TP26RiantR8snn__mbt7Xoshiro* $6;
  
};

struct _M0TP26RiantR8snn__mbt19STDPEntryMexicanHat {
  int32_t $0;
  int32_t $1;
  int32_t $2;
  struct _M0TP26RiantR8snn__mbt14STDPMexicanHat* $3;
  struct _M0TPB5ArrayGfE* $4;
  struct _M0TPB5ArrayGfE* $5;
  struct _M0TPB5ArrayGfE* $6;
  
};

struct _M0TUddE {
  double $0;
  double $1;
  
};

struct _M0DTP26RiantR8snn__mbt12STPEntryKind15MarkramSTPHet__ {
  struct _M0TP26RiantR8snn__mbt18MarkramSTPEntryHet* $0;
  
};

struct _M0TP26RiantR8snn__mbt13STDPVariables {
  struct _M0TPB5ArrayGfE* $0;
  struct _M0TPB5ArrayGfE* $1;
  struct _M0TPB5ArrayGfE* $2;
  struct _M0TPB5ArrayGfE* $3;
  struct _M0TPB5ArrayGbE* $4;
  
};

struct _M0TP26RiantR8snn__mbt18MarkramSTPEntryHet {
  int32_t $0;
  struct _M0TP26RiantR8snn__mbt19MarkramSTPVariables* $1;
  struct _M0TP26RiantR8snn__mbt22MarkramSTPParameterHet* $2;
  
};

struct _M0TP26RiantR8snn__mbt17STDPAntiSymmetric {
  float $0;
  float $1;
  float $2;
  float $3;
  float $4;
  float $5;
  float $6;
  float $7;
  
};

struct _M0TP26RiantR8snn__mbt19MarkramSTPVariables {
  int32_t $0;
  int32_t $1;
  struct _M0TPB5ArrayGfE* $2;
  struct _M0TPB5ArrayGfE* $3;
  struct _M0TPB5ArrayGfE* $4;
  struct _M0TPB5ArrayGfE* $5;
  struct _M0TPB5ArrayGbE* $6;
  
};

struct _M0TPB5ArrayGRP26RiantR8snn__mbt6AnyPopE {
  void** $0;
  int32_t $1;
  
};

struct _M0TP26RiantR8snn__mbt22MarkramSTPParameterHet {
  struct _M0TPB5ArrayGfE* $0;
  struct _M0TPB5ArrayGfE* $1;
  struct _M0TPB5ArrayGfE* $2;
  float $3;
  float $4;
  
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

struct _M0TP26RiantR8snn__mbt17PoissonStimulusIF {
  struct _M0TP26RiantR8snn__mbt12PoissonFixed* $0;
  struct _M0TPB5ArrayGiE* $1;
  struct _M0TPB5ArrayGfE* $2;
  struct _M0TP26RiantR8snn__mbt7Xoshiro* $3;
  
};

struct _M0TP26RiantR8snn__mbt13AdExParameter {
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

struct _M0DTP26RiantR8snn__mbt7AnyStim11PoissonIF__ {
  struct _M0TP26RiantR8snn__mbt17PoissonStimulusIF* $0;
  
};

struct _M0TUdiE {
  double $0;
  int32_t $1;
  
};

struct _M0DTP26RiantR8snn__mbt13STDPEntryKind16IstdpPotential__ {
  struct _M0TP26RiantR8snn__mbt19IstdpPotentialEntry* $0;
  
};

struct _M0TP26RiantR8snn__mbt9STDPEntry {
  int32_t $0;
  int32_t $1;
  int32_t $2;
  struct _M0TP26RiantR8snn__mbt13STDPVariables* $3;
  struct _M0TP26RiantR8snn__mbt12STDPGerstner* $4;
  struct _M0TPB5ArrayGfE* $5;
  
};

struct _M0TP26RiantR8snn__mbt9IstdpRate {
  float $0;
  float $1;
  float $2;
  float $3;
  float $4;
  
};

struct _M0TP26RiantR8snn__mbt18IstdpRateVariables {
  struct _M0TPB5ArrayGfE* $0;
  struct _M0TPB5ArrayGfE* $1;
  
};

struct _M0BTPB6Logger {
  int32_t(* $method_0)(void*, moonbit_string_t);
  int32_t(* $method_1)(void*, moonbit_string_t, int32_t, int32_t);
  int32_t(* $method_2)(void*, struct _M0TPC16string10StringView);
  int32_t(* $method_3)(void*, int32_t);
  int32_t(* $method_4)(void*, struct _M0TPB4Show);
  int32_t(* $method_5)(void*, struct _M0TPB4Show);
  
};

struct _M0TP26RiantR8snn__mbt23MarkramSTPEntryTimestep {
  int32_t $0;
  struct _M0TP26RiantR8snn__mbt19MarkramSTPVariables* $1;
  struct _M0TP26RiantR8snn__mbt27MarkramSTPParameterTimestep* $2;
  
};

struct _M0DTP26RiantR8snn__mbt12STPEntryKind12MarkramSTP__ {
  struct _M0TP26RiantR8snn__mbt15MarkramSTPEntry* $0;
  
};

struct _M0DTP26RiantR8snn__mbt13STDPEntryKind15AntiSymmetric__ {
  struct _M0TP26RiantR8snn__mbt22STDPEntryAntiSymmetric* $0;
  
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

struct _M0TP26RiantR8snn__mbt27MarkramSTPParameterTimestep {
  float $0;
  float $1;
  float $2;
  float $3;
  float $4;
  
};

struct _M0TP26RiantR8snn__mbt6HetRec {
  struct _M0TP26RiantR8snn__mbt15HetRecParameter* $0;
  int32_t $1;
  struct _M0TPB5ArrayGfE* $2;
  struct _M0TPB5ArrayGfE* $3;
  struct _M0TPB5ArrayGfE* $4;
  struct _M0TPB5ArrayGfE* $5;
  struct _M0TPB5ArrayGfE* $6;
  struct _M0TPB5ArrayGbE* $7;
  struct _M0TPB5ArrayGiE* $8;
  struct _M0TPB5ArrayGfE* $9;
  struct _M0TPB5ArrayGfE* $10;
  struct _M0TPB5ArrayGiE* $11;
  struct _M0TPB5ArrayGiE* $12;
  struct _M0TPB5ArrayGfE* $13;
  
};

struct _M0DTP26RiantR8snn__mbt6AnyPop4IF__ {
  struct _M0TP26RiantR8snn__mbt2IF* $0;
  
};

struct _M0DTP26RiantR8snn__mbt6AnyPop12AdExSinExp__ {
  struct _M0TP26RiantR8snn__mbt10AdExSinExp* $0;
  
};

struct _M0TP26RiantR8snn__mbt7Xoshiro {
  uint64_t $0;
  uint64_t $1;
  uint64_t $2;
  uint64_t $3;
  
};

struct _M0DTP26RiantR8snn__mbt12STPEntryKind20MarkramSTPTimestep__ {
  struct _M0TP26RiantR8snn__mbt23MarkramSTPEntryTimestep* $0;
  
};

struct _M0DTPC16option6OptionGfE4Some {
  float $0;
  
};

struct _M0TP26RiantR8snn__mbt20MorrisLecarParameter {
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
  float $11;
  float $12;
  float $13;
  float $14;
  float $15;
  
};

struct _M0TP26RiantR8snn__mbt19IstdpPotentialEntry {
  int32_t $0;
  int32_t $1;
  int32_t $2;
  struct _M0TP26RiantR8snn__mbt14IstdpPotential* $3;
  struct _M0TP26RiantR8snn__mbt23IstdpPotentialVariables* $4;
  struct _M0TPB5ArrayGfE* $5;
  
};

struct _M0TPB5ArrayGRP26RiantR8snn__mbt13STDPEntryKindE {
  void** $0;
  int32_t $1;
  
};

struct _M0TP26RiantR8snn__mbt17SpikeTimeStimulus {
  int32_t $0;
  struct _M0TP26RiantR8snn__mbt18SpikeTimeParameter* $1;
  struct _M0TPB5ArrayGfE* $2;
  struct _M0TPB5ArrayGiE* $3;
  struct _M0TPB5ArrayGbE* $4;
  struct _M0TPB5ArrayGfE* $5;
  
};

struct _M0TP26RiantR8snn__mbt22STDPEntryAntiSymmetric {
  int32_t $0;
  int32_t $1;
  int32_t $2;
  struct _M0TP26RiantR8snn__mbt17STDPAntiSymmetric* $3;
  struct _M0TP26RiantR8snn__mbt26STDPAntiSymmetricVariables* $4;
  struct _M0TPB5ArrayGfE* $5;
  
};

struct _M0TPB8MutLocalGiE {
  int32_t $0;
  
};

struct _M0TP26RiantR8snn__mbt12STDPGerstner {
  float $0;
  float $1;
  float $2;
  float $3;
  float $4;
  float $5;
  
};

struct _M0TP26RiantR8snn__mbt11MorrisLecar {
  struct _M0TP26RiantR8snn__mbt20MorrisLecarParameter* $0;
  int32_t $1;
  struct _M0TPB5ArrayGfE* $2;
  struct _M0TPB5ArrayGfE* $3;
  struct _M0TPB5ArrayGbE* $4;
  struct _M0TPB5ArrayGfE* $5;
  struct _M0TPB5ArrayGfE* $6;
  struct _M0TPB5ArrayGfE* $7;
  
};

struct _M0TP26RiantR8snn__mbt7Poisson {
  struct _M0TP26RiantR8snn__mbt20PoissonHomoParameter* $0;
  int32_t $1;
  struct _M0TPB5ArrayGbE* $2;
  struct _M0TPB5ArrayGfE* $3;
  struct _M0TP26RiantR8snn__mbt7Xoshiro* $4;
  
};

struct _M0TP26RiantR8snn__mbt19AdExSinExpParameter {
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

struct _M0TPB5ArrayGRPB5ArrayGfEE {
  struct _M0TPB5ArrayGfE** $0;
  int32_t $1;
  
};

struct _M0DTP26RiantR8snn__mbt7AnyStim14PoissonLayer__ {
  struct _M0TP26RiantR8snn__mbt20PoissonLayerStimulus* $0;
  
};

struct _M0TP26RiantR8snn__mbt20CurrentStimulusArray {
  float $0;
  struct _M0TPB5ArrayGbE* $1;
  struct _M0TPB5ArrayGfE* $2;
  int32_t $3;
  float $4;
  struct _M0TP26RiantR8snn__mbt7Xoshiro* $5;
  
};

struct _M0DTP26RiantR8snn__mbt13STDPEntryKind10Gerstner__ {
  struct _M0TP26RiantR8snn__mbt9STDPEntry* $0;
  
};

struct _M0TPB8MutLocalGdE {
  double $0;
  
};

struct _M0BTPB4Show {
  int32_t(* $method_0)(void*, struct _M0TPB6Logger);
  moonbit_string_t(* $method_1)(void*);
  
};

struct _M0DTP26RiantR8snn__mbt7AnyStim11CurrentIF__ {
  struct _M0TP26RiantR8snn__mbt17CurrentStimulusIF* $0;
  
};

struct _M0TPC16string10StringView {
  moonbit_string_t $0;
  int32_t $1;
  int32_t $2;
  
};

struct _M0TP26RiantR8snn__mbt22STDPSymmetricVariables {
  struct _M0TPB5ArrayGfE* $0;
  struct _M0TPB5ArrayGfE* $1;
  struct _M0TPB5ArrayGfE* $2;
  struct _M0TPB5ArrayGfE* $3;
  
};

struct _M0TP26RiantR8snn__mbt12PoissonFixed {
  float $0;
  float $1;
  struct _M0TPB5ArrayGbE* $2;
  
};

struct _M0TP26RiantR8snn__mbt14STDPMexicanHat {
  float $0;
  float $1;
  float $2;
  float $3;
  
};

struct _M0TPB5ArrayGRP26RiantR8snn__mbt12STPEntryKindE {
  void** $0;
  int32_t $1;
  
};

struct _M0TP26RiantR8snn__mbt14IstdpPotential {
  float $0;
  float $1;
  float $2;
  float $3;
  float $4;
  
};

struct _M0TP26RiantR8snn__mbt14IstdpRateEntry {
  int32_t $0;
  int32_t $1;
  int32_t $2;
  struct _M0TP26RiantR8snn__mbt9IstdpRate* $3;
  struct _M0TP26RiantR8snn__mbt18IstdpRateVariables* $4;
  struct _M0TPB5ArrayGfE* $5;
  
};

struct _M0TPB7Umul128 {
  uint64_t $0;
  uint64_t $1;
  
};

struct _M0TPB8Pow5Pair {
  uint64_t $0;
  uint64_t $1;
  
};

int32_t _M0FP26RiantR8snn__mbt19step__heterogeneous(
  struct _M0TP26RiantR8snn__mbt18HeterogeneousModel*,
  float
);

struct _M0TP26RiantR8snn__mbt18HeterogeneousModel* _M0FP26RiantR8snn__mbt7compose(
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt6AnyPopE*,
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt14SpikingSynapseE*,
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt7AnyStimE*,
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt7MonitorE*,
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt13STDPEntryKindE*,
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt12STPEntryKindE*
);

struct _M0TP26RiantR8snn__mbt18HeterogeneousModel* _M0FP26RiantR8snn__mbt15compose_2einner(
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt6AnyPopE*,
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt14SpikingSynapseE*,
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt7AnyStimE*,
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt7MonitorE*,
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt13STDPEntryKindE*,
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt12STPEntryKindE*
);

int32_t _M0FP26RiantR8snn__mbt14stimulate__any(
  void*,
  struct _M0TP26RiantR8snn__mbt4Time*,
  float
);

struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0MP26RiantR8snn__mbt14SpikingSynapse6random(
  struct _M0TP26RiantR8snn__mbt2IF*,
  struct _M0TP26RiantR8snn__mbt2IF*,
  moonbit_string_t,
  float,
  float,
  float,
  struct _M0TP26RiantR8snn__mbt7Xoshiro*
);

int32_t _M0FP26RiantR8snn__mbt22istdp__potential__step(
  struct _M0TPB5ArrayGfE*,
  struct _M0TPB5ArrayGbE*,
  struct _M0TPB5ArrayGbE*,
  struct _M0TPB5ArrayGiE*,
  struct _M0TPB5ArrayGiE*,
  struct _M0TPB5ArrayGfE*,
  struct _M0TP26RiantR8snn__mbt23IstdpPotentialVariables*,
  struct _M0TP26RiantR8snn__mbt14IstdpPotential*,
  float,
  float
);

int32_t _M0FP26RiantR8snn__mbt17istdp__rate__step(
  struct _M0TPB5ArrayGfE*,
  struct _M0TPB5ArrayGbE*,
  struct _M0TPB5ArrayGbE*,
  struct _M0TPB5ArrayGiE*,
  struct _M0TPB5ArrayGiE*,
  struct _M0TP26RiantR8snn__mbt18IstdpRateVariables*,
  struct _M0TP26RiantR8snn__mbt9IstdpRate*,
  float,
  float
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

int32_t _M0FP26RiantR8snn__mbt14integrate__any(void*, float);

int32_t _M0FP26RiantR8snn__mbt8step__wc(
  struct _M0TP26RiantR8snn__mbt11WilsonCowan*,
  float
);

int32_t _M0FP26RiantR8snn__mbt13step__poisson(
  struct _M0TP26RiantR8snn__mbt7Poisson*,
  float
);

int32_t _M0FP26RiantR8snn__mbt8step__ml(
  struct _M0TP26RiantR8snn__mbt11MorrisLecar*,
  float
);

#define _M0FP26RiantR8snn__mbt5tanhf tanhf

int32_t _M0FP26RiantR8snn__mbt8step__iz(
  struct _M0TP26RiantR8snn__mbt2IZ*,
  float
);

int32_t _M0FP26RiantR8snn__mbt8step__hh(
  struct _M0TP26RiantR8snn__mbt2HH*,
  float
);

int32_t _M0FP26RiantR8snn__mbt12step__hetrec(
  struct _M0TP26RiantR8snn__mbt6HetRec*,
  float
);

int32_t _M0FP26RiantR8snn__mbt18step__adex__sinexp(
  struct _M0TP26RiantR8snn__mbt10AdExSinExp*,
  float
);

int32_t _M0FP26RiantR8snn__mbt31adex__sinexp__synaptic__current(
  struct _M0TP26RiantR8snn__mbt10AdExSinExp*
);

int32_t _M0FP26RiantR8snn__mbt28adex__sinexp__step__synapses(
  struct _M0TP26RiantR8snn__mbt10AdExSinExp*,
  float
);

struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0MP26RiantR8snn__mbt7Xoshiro7default(
  
);

int32_t _M0FP26RiantR8snn__mbt10step__adex(
  struct _M0TP26RiantR8snn__mbt4AdEx*,
  float
);

int32_t _M0FP26RiantR8snn__mbt23adex__synaptic__current(
  struct _M0TP26RiantR8snn__mbt4AdEx*
);

int32_t _M0FP26RiantR8snn__mbt20adex__step__synapses(
  struct _M0TP26RiantR8snn__mbt4AdEx*,
  float
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

int32_t _M0FP26RiantR8snn__mbt21stdp__symmetric__step(
  struct _M0TPB5ArrayGfE*,
  struct _M0TPB5ArrayGbE*,
  struct _M0TPB5ArrayGbE*,
  struct _M0TPB5ArrayGiE*,
  struct _M0TPB5ArrayGiE*,
  struct _M0TP26RiantR8snn__mbt22STDPSymmetricVariables*,
  struct _M0TP26RiantR8snn__mbt13STDPSymmetric*,
  float,
  float
);

int32_t _M0FP26RiantR8snn__mbt22stdp__confavreux__step(
  struct _M0TPB5ArrayGfE*,
  struct _M0TPB5ArrayGbE*,
  struct _M0TPB5ArrayGbE*,
  struct _M0TPB5ArrayGiE*,
  struct _M0TPB5ArrayGiE*,
  struct _M0TP26RiantR8snn__mbt13STDPVariables*,
  struct _M0TP26RiantR8snn__mbt18STDPConfavreux2025*,
  float,
  float
);

int32_t _M0FP26RiantR8snn__mbt10stdp__step(
  struct _M0TPB5ArrayGfE*,
  struct _M0TPB5ArrayGbE*,
  struct _M0TPB5ArrayGbE*,
  struct _M0TPB5ArrayGiE*,
  struct _M0TPB5ArrayGiE*,
  struct _M0TP26RiantR8snn__mbt13STDPVariables*,
  struct _M0TP26RiantR8snn__mbt12STDPGerstner*,
  float,
  float
);

int32_t _M0FP26RiantR8snn__mbt25stdp__antisymmetric__step(
  struct _M0TPB5ArrayGfE*,
  struct _M0TPB5ArrayGbE*,
  struct _M0TPB5ArrayGbE*,
  struct _M0TPB5ArrayGiE*,
  struct _M0TPB5ArrayGiE*,
  struct _M0TP26RiantR8snn__mbt26STDPAntiSymmetricVariables*,
  struct _M0TP26RiantR8snn__mbt17STDPAntiSymmetric*,
  float
);

int32_t _M0FP26RiantR8snn__mbt24stdp__mexican__hat__step(
  struct _M0TPB5ArrayGfE*,
  struct _M0TPB5ArrayGbE*,
  struct _M0TPB5ArrayGbE*,
  struct _M0TPB5ArrayGiE*,
  struct _M0TPB5ArrayGiE*,
  struct _M0TPB5ArrayGfE*,
  struct _M0TPB5ArrayGfE*,
  struct _M0TP26RiantR8snn__mbt14STDPMexicanHat*,
  float
);

#define _M0FP26RiantR8snn__mbt4logf logf

int32_t _M0FP26RiantR8snn__mbt20find__pre__for__conn(
  struct _M0TPB5ArrayGiE*,
  int32_t
);

float _M0FP26RiantR8snn__mbt20mexican__hat__kernel(float);

struct _M0TP26RiantR8snn__mbt9STDPEntry* _M0MP26RiantR8snn__mbt9STDPEntry3new(
  int32_t,
  int32_t,
  int32_t
);

struct _M0TP26RiantR8snn__mbt13STDPVariables* _M0MP26RiantR8snn__mbt13STDPVariables3new(
  int32_t,
  int32_t
);

struct _M0TP26RiantR8snn__mbt12STDPGerstner* _M0MP26RiantR8snn__mbt12STDPGerstner3new(
  
);

int32_t _M0FP26RiantR8snn__mbt19stimulate__balanced(
  struct _M0TP26RiantR8snn__mbt16BalancedStimulus*,
  float,
  float
);

struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0MP26RiantR8snn__mbt7Xoshiro3new(
  uint64_t
);

struct _M0TUmmmmE* _M0FP26RiantR8snn__mbt10splitmix64(uint64_t);

uint64_t _M0FP26RiantR8snn__mbt5mix64(uint64_t);

int32_t _M0FP26RiantR8snn__mbt25stimulate__current__array(
  struct _M0TP26RiantR8snn__mbt20CurrentStimulusArray*
);

int32_t _M0FP26RiantR8snn__mbt22stimulate__current__if(
  struct _M0TP26RiantR8snn__mbt17CurrentStimulusIF*
);

int32_t _M0FP26RiantR8snn__mbt13stimulate__if(
  struct _M0TP26RiantR8snn__mbt17PoissonStimulusIF*,
  float,
  float
);

struct _M0TP26RiantR8snn__mbt17PoissonStimulusIF* _M0MP26RiantR8snn__mbt17PoissonStimulusIF3new(
  struct _M0TP26RiantR8snn__mbt2IF*,
  moonbit_string_t,
  float,
  struct _M0TP26RiantR8snn__mbt7Xoshiro*
);

struct _M0TP26RiantR8snn__mbt12PoissonFixed* _M0MP26RiantR8snn__mbt12PoissonFixed3new(
  float
);

int32_t _M0FP26RiantR8snn__mbt16stimulate__layer(
  struct _M0TP26RiantR8snn__mbt20PoissonLayerStimulus*,
  float,
  float
);

struct _M0TUddE* _M0FP26RiantR8snn__mbt11box__muller(
  struct _M0TP26RiantR8snn__mbt7Xoshiro*
);

int32_t _M0FP26RiantR8snn__mbt15sample__poisson(
  struct _M0TP26RiantR8snn__mbt7Xoshiro*,
  float
);

double _M0FP26RiantR8snn__mbt9next__f64(
  struct _M0TP26RiantR8snn__mbt7Xoshiro*
);

int32_t _M0FP26RiantR8snn__mbt20stimulate__spiketime(
  struct _M0TP26RiantR8snn__mbt17SpikeTimeStimulus*,
  float,
  float
);

int32_t _M0FP26RiantR8snn__mbt28markram__stp__step__timestep(
  struct _M0TP26RiantR8snn__mbt14SpikingSynapse*,
  struct _M0TP26RiantR8snn__mbt19MarkramSTPVariables*,
  struct _M0TP26RiantR8snn__mbt27MarkramSTPParameterTimestep*,
  float,
  float
);

int32_t _M0FP26RiantR8snn__mbt23markram__stp__step__het(
  struct _M0TP26RiantR8snn__mbt14SpikingSynapse*,
  struct _M0TP26RiantR8snn__mbt19MarkramSTPVariables*,
  struct _M0TP26RiantR8snn__mbt22MarkramSTPParameterHet*,
  float
);

int32_t _M0FP26RiantR8snn__mbt18markram__stp__step(
  struct _M0TP26RiantR8snn__mbt14SpikingSynapse*,
  struct _M0TP26RiantR8snn__mbt19MarkramSTPVariables*,
  struct _M0TP26RiantR8snn__mbt19MarkramSTPParameter*,
  float
);

int32_t _M0FP26RiantR8snn__mbt12update__time(
  struct _M0TP26RiantR8snn__mbt4Time*,
  float
);

float _M0FP26RiantR8snn__mbt9get__time(struct _M0TP26RiantR8snn__mbt4Time*);

struct _M0TP26RiantR8snn__mbt4Time* _M0MP26RiantR8snn__mbt4Time3new();

#define _M0FP26RiantR8snn__mbt4expf expf

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

float _M0MP26RiantR8snn__mbt7Monitor12firing__rate(
  struct _M0TP26RiantR8snn__mbt7Monitor*
);

int32_t _M0MP26RiantR8snn__mbt7Monitor13count__spikes(
  struct _M0TP26RiantR8snn__mbt7Monitor*
);

double _M0FPC14math2ln(double);

#define _M0FPC14math3cos cos

#define _M0FPC14math3sin sin

struct _M0TUdiE* _M0FPC14math5frexp(double);

struct _M0TUdiE* _M0FPC14math9normalize(double);

int32_t _M0MPC15float5Float7is__nan(float);

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

int32_t _M0MPC15array5Array3setGiE(struct _M0TPB5ArrayGiE*, int32_t, int32_t);

int32_t _M0MPC15array5Array3setGRPB5ArrayGfEE(
  struct _M0TPB5ArrayGRPB5ArrayGfEE*,
  int32_t,
  struct _M0TPB5ArrayGfE*
);

int32_t _M0MPC15array5Array3setGbE(struct _M0TPB5ArrayGbE*, int32_t, int32_t);

void* _M0MPC15array5Array3popGfE(struct _M0TPB5ArrayGfE*);

int64_t _M0MPC15array5Array3popGiE(struct _M0TPB5ArrayGiE*);

float _M0MPC15array5Array2atGfE(struct _M0TPB5ArrayGfE*, int32_t);

struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0MPC15array5Array2atGRP26RiantR8snn__mbt14SpikingSynapseE(
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt14SpikingSynapseE*,
  int32_t
);

int32_t _M0MPC15array5Array2atGiE(struct _M0TPB5ArrayGiE*, int32_t);

int32_t _M0MPC15array5Array2atGbE(struct _M0TPB5ArrayGbE*, int32_t);

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

int32_t _M0MPC15array5Array6lengthGiE(struct _M0TPB5ArrayGiE*);

float* _M0MPC15array5Array6bufferGfE(struct _M0TPB5ArrayGfE*);

int32_t* _M0MPC15array5Array6bufferGiE(struct _M0TPB5ArrayGiE*);

struct _M0TP26RiantR8snn__mbt14SpikingSynapse** _M0MPC15array5Array6bufferGRP26RiantR8snn__mbt14SpikingSynapseE(
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt14SpikingSynapseE*
);

uint8_t* _M0MPC15array5Array6bufferGbE(struct _M0TPB5ArrayGbE*);

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

int32_t _M0MPC15array10FixedArray12unsafe__blitGkE(
  uint16_t*,
  int32_t,
  uint16_t*,
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

int32_t _M0MPB18UninitializedArray6lengthGiE(int32_t*);

int32_t _M0MPB18UninitializedArray6lengthGfE(float*);

int32_t _M0FPC15abort5abortGuE(moonbit_string_t);

int32_t* _M0FPC15abort5abortGRPB18UninitializedArrayGiEE(moonbit_string_t);

float* _M0FPC15abort5abortGRPB18UninitializedArrayGfEE(moonbit_string_t);

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

float logf(float);

double sin(double);

float tanhf(float);

float expf(float);

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

struct { int32_t rc; uint32_t meta; uint16_t const data[20]; 
} const moonbit_string_literal_25 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 19, 32, 32, 
    87, 50, 50, 32, 99, 111, 110, 110, 101, 99, 116, 105, 111, 110, 115, 
    58, 32, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[19]; 
} const moonbit_string_literal_31 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 18, 32, 32, 
    87, 50, 50, 32, 119, 101, 105, 103, 104, 116, 32, 115, 117, 109, 
    58, 32, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[19]; 
} const moonbit_string_literal_30 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 18, 32, 32, 
    87, 49, 49, 32, 119, 101, 105, 103, 104, 116, 32, 115, 117, 109, 
    58, 32, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[7]; 
} const moonbit_string_literal_23 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 6, 32, 32, 
    69, 50, 58, 32, 0
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

struct { int32_t rc; uint32_t meta; uint16_t const data[2]; 
} const moonbit_string_literal_4 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 1, 48, 0};

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

struct { int32_t rc; uint32_t meta; uint16_t const data[3]; 
} const moonbit_string_literal_1 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 2, 103, 101, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[13]; 
} const moonbit_string_literal_28 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 12, 32, 32, 
    69, 49, 91, 48, 93, 32, 72, 122, 58, 32, 0
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

struct { int32_t rc; uint32_t meta; uint16_t const data[55]; 
} const moonbit_string_literal_20 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 54, 108, 97, 
    103, 122, 105, 50, 48, 50, 50, 46, 109, 98, 116, 58, 32, 115, 105, 
    109, 112, 108, 105, 102, 105, 101, 100, 32, 76, 97, 103, 122, 105, 
    50, 48, 50, 50, 32, 65, 115, 115, 101, 109, 98, 108, 121, 32, 70, 
    111, 114, 109, 97, 116, 105, 111, 110, 0
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

struct { int32_t rc; uint32_t meta; uint16_t const data[51]; 
} const moonbit_string_literal_22 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 50, 32, 73, 
    70, 32, 110, 101, 117, 114, 111, 110, 115, 32, 40, 99, 111, 110, 
    115, 116, 97, 110, 116, 32, 51, 53, 48, 112, 65, 32, 100, 114, 105, 
    118, 101, 32, 43, 32, 53, 48, 48, 72, 122, 32, 80, 111, 105, 115, 
    115, 111, 110, 41, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[24]; 
} const moonbit_string_literal_10 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 23, 65, 114, 
    114, 97, 121, 32, 99, 97, 112, 97, 99, 105, 116, 121, 32, 111, 118, 
    101, 114, 102, 108, 111, 119, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[36]; 
} const moonbit_string_literal_27 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 35, 108, 97, 
    103, 122, 105, 50, 48, 50, 50, 46, 109, 98, 116, 58, 32, 115, 105, 
    109, 117, 108, 97, 116, 105, 111, 110, 32, 100, 111, 110, 101, 32, 
    40, 49, 115, 41, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[20]; 
} const moonbit_string_literal_24 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 19, 32, 32, 
    87, 49, 49, 32, 99, 111, 110, 110, 101, 99, 116, 105, 111, 110, 115, 
    58, 32, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[26]; 
} const moonbit_string_literal_5 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 25, 73, 108, 
    108, 101, 103, 97, 108, 65, 114, 103, 117, 109, 101, 110, 116, 69, 
    120, 99, 101, 112, 116, 105, 111, 110, 32, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[63]; 
} const moonbit_string_literal_26 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 62, 32, 32, 
    71, 101, 114, 115, 116, 110, 101, 114, 32, 83, 84, 68, 80, 32, 97, 
    117, 116, 111, 45, 97, 112, 112, 108, 105, 101, 100, 32, 116, 111, 
    32, 87, 49, 49, 32, 43, 32, 87, 50, 50, 32, 40, 97, 115, 121, 109, 
    109, 101, 116, 114, 105, 99, 32, 76, 84, 80, 47, 76, 84, 68, 41, 
    0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[7]; 
} const moonbit_string_literal_21 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 6, 32, 32, 
    69, 49, 58, 32, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[13]; 
} const moonbit_string_literal_29 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 12, 32, 32, 
    69, 50, 91, 48, 93, 32, 72, 122, 58, 32, 0
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

uint32_t const moonbit_layout_table_data[117] =
  {
    sizeof(struct _M0TPB5ArrayGRP26RiantR8snn__mbt7AnyStimE) / 4, 1,
    offsetof(struct _M0TPB5ArrayGRP26RiantR8snn__mbt7AnyStimE, $0) / 4 * 2,
    sizeof(struct _M0TPB5ArrayGRP26RiantR8snn__mbt7MonitorE) / 4, 1,
    offsetof(struct _M0TPB5ArrayGRP26RiantR8snn__mbt7MonitorE, $0) / 4 * 2,
    sizeof(struct _M0TPB5ArrayGRP26RiantR8snn__mbt13STDPEntryKindE) / 4, 
    1,
    offsetof(struct _M0TPB5ArrayGRP26RiantR8snn__mbt13STDPEntryKindE, $0)
    / 4
    * 2, sizeof(struct _M0TPB5ArrayGRP26RiantR8snn__mbt12STPEntryKindE) / 4,
    1,
    offsetof(struct _M0TPB5ArrayGRP26RiantR8snn__mbt12STPEntryKindE, $0)
    / 4
    * 2, sizeof(struct _M0TP26RiantR8snn__mbt18HeterogeneousModel) / 4, 
    7,
    offsetof(struct _M0TP26RiantR8snn__mbt18HeterogeneousModel, $0) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt18HeterogeneousModel, $1) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt18HeterogeneousModel, $2) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt18HeterogeneousModel, $3) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt18HeterogeneousModel, $4) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt18HeterogeneousModel, $5) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt18HeterogeneousModel, $6) / 4 * 2,
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
    sizeof(struct _M0TP26RiantR8snn__mbt9STDPEntry) / 4, 3,
    offsetof(struct _M0TP26RiantR8snn__mbt9STDPEntry, $3) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt9STDPEntry, $4) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt9STDPEntry, $5) / 4 * 2,
    sizeof(struct _M0TPB5ArrayGbE) / 4, 1,
    offsetof(struct _M0TPB5ArrayGbE, $0) / 4 * 2,
    sizeof(struct _M0TP26RiantR8snn__mbt13STDPVariables) / 4, 5,
    offsetof(struct _M0TP26RiantR8snn__mbt13STDPVariables, $0) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt13STDPVariables, $1) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt13STDPVariables, $2) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt13STDPVariables, $3) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt13STDPVariables, $4) / 4 * 2,
    sizeof(struct _M0TP26RiantR8snn__mbt17PoissonStimulusIF) / 4, 4,
    offsetof(struct _M0TP26RiantR8snn__mbt17PoissonStimulusIF, $0) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt17PoissonStimulusIF, $1) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt17PoissonStimulusIF, $2) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt17PoissonStimulusIF, $3) / 4 * 2,
    sizeof(struct _M0TP26RiantR8snn__mbt12PoissonFixed) / 4, 1,
    offsetof(struct _M0TP26RiantR8snn__mbt12PoissonFixed, $2) / 4 * 2,
    sizeof(struct _M0TP26RiantR8snn__mbt4Time) / 4, 2,
    offsetof(struct _M0TP26RiantR8snn__mbt4Time, $0) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt4Time, $1) / 4 * 2,
    sizeof(struct _M0TPB5ArrayGRPB5ArrayGfEE) / 4, 1,
    offsetof(struct _M0TPB5ArrayGRPB5ArrayGfEE, $0) / 4 * 2,
    sizeof(struct _M0TPB13StringBuilder) / 4, 1,
    offsetof(struct _M0TPB13StringBuilder, $0) / 4 * 2,
    sizeof(struct _M0DTP26RiantR8snn__mbt13STDPEntryKind10Gerstner__) / 4, 
    1,
    offsetof(struct _M0DTP26RiantR8snn__mbt13STDPEntryKind10Gerstner__, $0)
    / 4
    * 2, sizeof(struct _M0DTP26RiantR8snn__mbt6AnyPop4IF__) / 4, 1,
    offsetof(struct _M0DTP26RiantR8snn__mbt6AnyPop4IF__, $0) / 4 * 2,
    sizeof(struct _M0TPB5ArrayGRP26RiantR8snn__mbt6AnyPopE) / 4, 1,
    offsetof(struct _M0TPB5ArrayGRP26RiantR8snn__mbt6AnyPopE, $0) / 4 * 2,
    sizeof(struct _M0TPB5ArrayGRP26RiantR8snn__mbt14SpikingSynapseE) / 4, 
    1,
    offsetof(struct _M0TPB5ArrayGRP26RiantR8snn__mbt14SpikingSynapseE, $0)
    / 4
    * 2, sizeof(struct _M0DTP26RiantR8snn__mbt7AnyStim11PoissonIF__) / 4, 
    1,
    offsetof(struct _M0DTP26RiantR8snn__mbt7AnyStim11PoissonIF__, $0) / 4 * 2
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

int32_t _M0FP26RiantR8snn__mbt19step__heterogeneous(
  struct _M0TP26RiantR8snn__mbt18HeterogeneousModel* _M0L1mS1669,
  float _M0L2dtS1674
) {
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt7AnyStimE* _M0L7_2abindS1668;
  int32_t _M0L7_2abindS1670;
  void** _M0L7_2abindS1671;
  int32_t _M0L2__S1672;
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt14SpikingSynapseE* _M0L7_2abindS1676;
  int32_t _M0L7_2abindS1677;
  struct _M0TP26RiantR8snn__mbt14SpikingSynapse** _M0L7_2abindS1678;
  int32_t _M0L2__S1679;
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt12STPEntryKindE* _M0L7_2abindS1682;
  int32_t _M0L7_2abindS1683;
  void** _M0L7_2abindS1684;
  int32_t _M0L2__S1685;
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt14SpikingSynapseE* _M0L7_2abindS1703;
  int32_t _M0L7_2abindS1704;
  struct _M0TP26RiantR8snn__mbt14SpikingSynapse** _M0L7_2abindS1705;
  int32_t _M0L2__S1706;
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt13STDPEntryKindE* _M0L7_2abindS1709;
  int32_t _M0L7_2abindS1710;
  void** _M0L7_2abindS1711;
  int32_t _M0L2__S1712;
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt6AnyPopE* _M0L7_2abindS1750;
  int32_t _M0L7_2abindS1751;
  void** _M0L7_2abindS1752;
  int32_t _M0L2__S1753;
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt7MonitorE* _M0L7_2abindS1756;
  int32_t _M0L7_2abindS1757;
  struct _M0TP26RiantR8snn__mbt7Monitor** _M0L7_2abindS1758;
  int32_t _M0L2__S1759;
  struct _M0TP26RiantR8snn__mbt4Time* _M0L4timeS5108;
  #line 100 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
  _M0L7_2abindS1668 = _M0L1mS1669->$2;
  _M0L7_2abindS1670 = _M0L7_2abindS1668->$1;
  _M0L7_2abindS1671 = _M0L7_2abindS1668->$0;
  moonbit_incref(_M0L7_2abindS1671);
  _M0L2__S1672 = 0;
  while (1) {
    if (_M0L2__S1672 < _M0L7_2abindS1670) {
      void* _M0L1sS1673 = (void*)_M0L7_2abindS1671[_M0L2__S1672];
      struct _M0TP26RiantR8snn__mbt4Time* _M0L4timeS4937 = _M0L1mS1669->$3;
      int32_t _M0L6_2atmpS4938;
      moonbit_incref(_M0L1sS1673);
      #line 103 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0FP26RiantR8snn__mbt14stimulate__any(_M0L1sS1673, _M0L4timeS4937, _M0L2dtS1674);
      moonbit_decref(_M0L1sS1673);
      _M0L6_2atmpS4938 = _M0L2__S1672 + 1;
      _M0L2__S1672 = _M0L6_2atmpS4938;
      continue;
    } else {
      moonbit_decref(_M0L7_2abindS1671);
    }
    break;
  }
  _M0L7_2abindS1676 = _M0L1mS1669->$1;
  _M0L7_2abindS1677 = _M0L7_2abindS1676->$1;
  _M0L7_2abindS1678 = _M0L7_2abindS1676->$0;
  moonbit_incref(_M0L7_2abindS1678);
  _M0L2__S1679 = 0;
  while (1) {
    if (_M0L2__S1679 < _M0L7_2abindS1677) {
      struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0L1cS1680 =
        (struct _M0TP26RiantR8snn__mbt14SpikingSynapse*)_M0L7_2abindS1678[
          _M0L2__S1679
        ];
      struct _M0TP26RiantR8snn__mbt4Time* _M0L4timeS4940 = _M0L1mS1669->$3;
      float _M0L6_2atmpS4939;
      int32_t _M0L6_2atmpS4941;
      moonbit_incref(_M0L1cS1680);
      #line 107 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L6_2atmpS4939 = _M0FP26RiantR8snn__mbt9get__time(_M0L4timeS4940);
      #line 107 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0FP26RiantR8snn__mbt25deliver__pending__synapse(_M0L1cS1680, _M0L6_2atmpS4939);
      moonbit_decref(_M0L1cS1680);
      _M0L6_2atmpS4941 = _M0L2__S1679 + 1;
      _M0L2__S1679 = _M0L6_2atmpS4941;
      continue;
    } else {
      moonbit_decref(_M0L7_2abindS1678);
    }
    break;
  }
  _M0L7_2abindS1682 = _M0L1mS1669->$6;
  _M0L7_2abindS1683 = _M0L7_2abindS1682->$1;
  _M0L7_2abindS1684 = _M0L7_2abindS1682->$0;
  moonbit_incref(_M0L7_2abindS1684);
  _M0L2__S1685 = 0;
  while (1) {
    if (_M0L2__S1685 < _M0L7_2abindS1683) {
      void* _M0L5entryS1686 = (void*)_M0L7_2abindS1684[_M0L2__S1685];
      struct _M0TP26RiantR8snn__mbt23MarkramSTPEntryTimestep* _M0L1eS1688;
      struct _M0TP26RiantR8snn__mbt18MarkramSTPEntryHet* _M0L1eS1691;
      struct _M0TP26RiantR8snn__mbt15MarkramSTPEntry* _M0L1eS1694;
      struct _M0TPB5ArrayGRP26RiantR8snn__mbt14SpikingSynapseE* _M0L5connsS4958;
      int32_t _M0L11conn__indexS4959;
      struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0L3synS1695;
      struct _M0TP26RiantR8snn__mbt19MarkramSTPVariables* _M0L4varsS4954;
      struct _M0TP26RiantR8snn__mbt19MarkramSTPParameter* _M0L5paramS4955;
      int32_t _M0L6_2acntS5134;
      struct _M0TP26RiantR8snn__mbt4Time* _M0L4timeS4957;
      float _M0L6_2atmpS4956;
      struct _M0TPB5ArrayGRP26RiantR8snn__mbt14SpikingSynapseE* _M0L5connsS4952;
      int32_t _M0L11conn__indexS4953;
      struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0L3synS1692;
      struct _M0TP26RiantR8snn__mbt19MarkramSTPVariables* _M0L4varsS4948;
      struct _M0TP26RiantR8snn__mbt22MarkramSTPParameterHet* _M0L5paramS4949;
      int32_t _M0L6_2acntS5132;
      struct _M0TP26RiantR8snn__mbt4Time* _M0L4timeS4951;
      float _M0L6_2atmpS4950;
      struct _M0TPB5ArrayGRP26RiantR8snn__mbt14SpikingSynapseE* _M0L5connsS4946;
      int32_t _M0L11conn__indexS4947;
      struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0L3synS1689;
      struct _M0TP26RiantR8snn__mbt19MarkramSTPVariables* _M0L4varsS4942;
      struct _M0TP26RiantR8snn__mbt27MarkramSTPParameterTimestep* _M0L5paramS4943;
      int32_t _M0L6_2acntS5130;
      struct _M0TP26RiantR8snn__mbt4Time* _M0L4timeS4945;
      float _M0L6_2atmpS4944;
      int32_t _M0L6_2atmpS4960;
      switch (Moonbit_object_tag(_M0L5entryS1686)) {
        case 0: {
          struct _M0DTP26RiantR8snn__mbt12STPEntryKind12MarkramSTP__* _M0L15_2aMarkramSTP__S1696 =
            (struct _M0DTP26RiantR8snn__mbt12STPEntryKind12MarkramSTP__*)_M0L5entryS1686;
          struct _M0TP26RiantR8snn__mbt15MarkramSTPEntry* _M0L4_2aeS1697 =
            _M0L15_2aMarkramSTP__S1696->$0;
          moonbit_incref(_M0L4_2aeS1697);
          _M0L1eS1694 = _M0L4_2aeS1697;
          goto join_1693;
          break;
        }
        
        case 1: {
          struct _M0DTP26RiantR8snn__mbt12STPEntryKind15MarkramSTPHet__* _M0L18_2aMarkramSTPHet__S1698 =
            (struct _M0DTP26RiantR8snn__mbt12STPEntryKind15MarkramSTPHet__*)_M0L5entryS1686;
          struct _M0TP26RiantR8snn__mbt18MarkramSTPEntryHet* _M0L4_2aeS1699 =
            _M0L18_2aMarkramSTPHet__S1698->$0;
          moonbit_incref(_M0L4_2aeS1699);
          _M0L1eS1691 = _M0L4_2aeS1699;
          goto join_1690;
          break;
        }
        default: {
          struct _M0DTP26RiantR8snn__mbt12STPEntryKind20MarkramSTPTimestep__* _M0L23_2aMarkramSTPTimestep__S1700 =
            (struct _M0DTP26RiantR8snn__mbt12STPEntryKind20MarkramSTPTimestep__*)_M0L5entryS1686;
          struct _M0TP26RiantR8snn__mbt23MarkramSTPEntryTimestep* _M0L4_2aeS1701 =
            _M0L23_2aMarkramSTPTimestep__S1700->$0;
          moonbit_incref(_M0L4_2aeS1701);
          _M0L1eS1688 = _M0L4_2aeS1701;
          goto join_1687;
          break;
        }
      }
      goto joinlet_5310;
      join_1693:;
      _M0L5connsS4958 = _M0L1mS1669->$1;
      _M0L11conn__indexS4959 = _M0L1eS1694->$0;
      #line 114 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L3synS1695
      = _M0MPC15array5Array2atGRP26RiantR8snn__mbt14SpikingSynapseE(_M0L5connsS4958, _M0L11conn__indexS4959);
      _M0L4varsS4954 = _M0L1eS1694->$1;
      _M0L5paramS4955 = _M0L1eS1694->$2;
      _M0L6_2acntS5134 = Moonbit_rc_count(Moonbit_object_header(_M0L1eS1694));
      if (_M0L6_2acntS5134 > 1) {
        int32_t _M0L11_2anew__cntS5135 = _M0L6_2acntS5134 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L1eS1694), _M0L11_2anew__cntS5135);
        moonbit_incref(_M0L5paramS4955);
        moonbit_incref(_M0L4varsS4954);
      } else if (_M0L6_2acntS5134 == 1) {
        #line 114 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
        moonbit_free(_M0L1eS1694);
      }
      _M0L4timeS4957 = _M0L1mS1669->$3;
      #line 115 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L6_2atmpS4956 = _M0FP26RiantR8snn__mbt9get__time(_M0L4timeS4957);
      #line 115 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0FP26RiantR8snn__mbt18markram__stp__step(_M0L3synS1695, _M0L4varsS4954, _M0L5paramS4955, _M0L6_2atmpS4956);
      moonbit_decref(_M0L3synS1695);
      moonbit_decref(_M0L4varsS4954);
      moonbit_decref(_M0L5paramS4955);
      joinlet_5310:;
      goto joinlet_5309;
      join_1690:;
      _M0L5connsS4952 = _M0L1mS1669->$1;
      _M0L11conn__indexS4953 = _M0L1eS1691->$0;
      #line 118 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L3synS1692
      = _M0MPC15array5Array2atGRP26RiantR8snn__mbt14SpikingSynapseE(_M0L5connsS4952, _M0L11conn__indexS4953);
      _M0L4varsS4948 = _M0L1eS1691->$1;
      _M0L5paramS4949 = _M0L1eS1691->$2;
      _M0L6_2acntS5132 = Moonbit_rc_count(Moonbit_object_header(_M0L1eS1691));
      if (_M0L6_2acntS5132 > 1) {
        int32_t _M0L11_2anew__cntS5133 = _M0L6_2acntS5132 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L1eS1691), _M0L11_2anew__cntS5133);
        moonbit_incref(_M0L5paramS4949);
        moonbit_incref(_M0L4varsS4948);
      } else if (_M0L6_2acntS5132 == 1) {
        #line 118 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
        moonbit_free(_M0L1eS1691);
      }
      _M0L4timeS4951 = _M0L1mS1669->$3;
      #line 119 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L6_2atmpS4950 = _M0FP26RiantR8snn__mbt9get__time(_M0L4timeS4951);
      #line 119 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0FP26RiantR8snn__mbt23markram__stp__step__het(_M0L3synS1692, _M0L4varsS4948, _M0L5paramS4949, _M0L6_2atmpS4950);
      moonbit_decref(_M0L3synS1692);
      moonbit_decref(_M0L4varsS4948);
      moonbit_decref(_M0L5paramS4949);
      joinlet_5309:;
      goto joinlet_5308;
      join_1687:;
      _M0L5connsS4946 = _M0L1mS1669->$1;
      _M0L11conn__indexS4947 = _M0L1eS1688->$0;
      #line 122 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L3synS1689
      = _M0MPC15array5Array2atGRP26RiantR8snn__mbt14SpikingSynapseE(_M0L5connsS4946, _M0L11conn__indexS4947);
      _M0L4varsS4942 = _M0L1eS1688->$1;
      _M0L5paramS4943 = _M0L1eS1688->$2;
      _M0L6_2acntS5130 = Moonbit_rc_count(Moonbit_object_header(_M0L1eS1688));
      if (_M0L6_2acntS5130 > 1) {
        int32_t _M0L11_2anew__cntS5131 = _M0L6_2acntS5130 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L1eS1688), _M0L11_2anew__cntS5131);
        moonbit_incref(_M0L5paramS4943);
        moonbit_incref(_M0L4varsS4942);
      } else if (_M0L6_2acntS5130 == 1) {
        #line 122 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
        moonbit_free(_M0L1eS1688);
      }
      _M0L4timeS4945 = _M0L1mS1669->$3;
      #line 123 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L6_2atmpS4944 = _M0FP26RiantR8snn__mbt9get__time(_M0L4timeS4945);
      #line 123 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0FP26RiantR8snn__mbt28markram__stp__step__timestep(_M0L3synS1689, _M0L4varsS4942, _M0L5paramS4943, _M0L6_2atmpS4944, _M0L2dtS1674);
      moonbit_decref(_M0L3synS1689);
      moonbit_decref(_M0L4varsS4942);
      moonbit_decref(_M0L5paramS4943);
      joinlet_5308:;
      _M0L6_2atmpS4960 = _M0L2__S1685 + 1;
      _M0L2__S1685 = _M0L6_2atmpS4960;
      continue;
    } else {
      moonbit_decref(_M0L7_2abindS1684);
    }
    break;
  }
  _M0L7_2abindS1703 = _M0L1mS1669->$1;
  _M0L7_2abindS1704 = _M0L7_2abindS1703->$1;
  _M0L7_2abindS1705 = _M0L7_2abindS1703->$0;
  moonbit_incref(_M0L7_2abindS1705);
  _M0L2__S1706 = 0;
  while (1) {
    if (_M0L2__S1706 < _M0L7_2abindS1704) {
      struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0L1cS1707 =
        (struct _M0TP26RiantR8snn__mbt14SpikingSynapse*)_M0L7_2abindS1705[
          _M0L2__S1706
        ];
      struct _M0TP26RiantR8snn__mbt4Time* _M0L4timeS4962 = _M0L1mS1669->$3;
      float _M0L6_2atmpS4961;
      int32_t _M0L6_2atmpS4963;
      moonbit_incref(_M0L1cS1707);
      #line 130 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L6_2atmpS4961 = _M0FP26RiantR8snn__mbt9get__time(_M0L4timeS4962);
      #line 130 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0FP26RiantR8snn__mbt16forward__synapse(_M0L1cS1707, _M0L6_2atmpS4961);
      moonbit_decref(_M0L1cS1707);
      _M0L6_2atmpS4963 = _M0L2__S1706 + 1;
      _M0L2__S1706 = _M0L6_2atmpS4963;
      continue;
    } else {
      moonbit_decref(_M0L7_2abindS1705);
    }
    break;
  }
  _M0L7_2abindS1709 = _M0L1mS1669->$5;
  _M0L7_2abindS1710 = _M0L7_2abindS1709->$1;
  _M0L7_2abindS1711 = _M0L7_2abindS1709->$0;
  moonbit_incref(_M0L7_2abindS1711);
  _M0L2__S1712 = 0;
  while (1) {
    if (_M0L2__S1712 < _M0L7_2abindS1710) {
      void* _M0L5entryS1713 = (void*)_M0L7_2abindS1711[_M0L2__S1712];
      struct _M0TP26RiantR8snn__mbt18STDPEntrySymmetric* _M0L1eS1715;
      struct _M0TP26RiantR8snn__mbt19IstdpPotentialEntry* _M0L1eS1718;
      struct _M0TP26RiantR8snn__mbt14IstdpRateEntry* _M0L1eS1721;
      struct _M0TP26RiantR8snn__mbt23STDPEntryConfavreux2025* _M0L1eS1724;
      struct _M0TP26RiantR8snn__mbt22STDPEntryAntiSymmetric* _M0L1eS1727;
      struct _M0TP26RiantR8snn__mbt19STDPEntryMexicanHat* _M0L1eS1730;
      struct _M0TP26RiantR8snn__mbt9STDPEntry* _M0L1eS1733;
      struct _M0TPB5ArrayGRP26RiantR8snn__mbt14SpikingSynapseE* _M0L5connsS5101;
      int32_t _M0L11conn__indexS5102;
      struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0L3synS1734;
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS5096;
      struct _M0TPB5ArrayGfE* _M0L4valsS5083;
      struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS5095;
      struct _M0TPB5ArrayGbE* _M0L4fireS5084;
      struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS5094;
      struct _M0TPB5ArrayGbE* _M0L4fireS5085;
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS5093;
      struct _M0TPB5ArrayGiE* _M0L6colptrS5086;
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS5092;
      int32_t _M0L6_2acntS5264;
      struct _M0TPB5ArrayGiE* _M0L6rowptrS5087;
      int32_t _M0L6_2acntS5275;
      struct _M0TP26RiantR8snn__mbt13STDPVariables* _M0L4varsS5088;
      struct _M0TP26RiantR8snn__mbt12STDPGerstner* _M0L5paramS5089;
      struct _M0TPB5ArrayGfE* _M0L6t__nowS5091;
      float _M0L6_2atmpS5090;
      struct _M0TPB5ArrayGfE* _M0L6t__nowS5097;
      struct _M0TPB5ArrayGfE* _M0L6t__nowS5100;
      int32_t _M0L6_2acntS5279;
      float _M0L6_2atmpS5099;
      float _M0L6_2atmpS5098;
      struct _M0TPB5ArrayGRP26RiantR8snn__mbt14SpikingSynapseE* _M0L5connsS5081;
      int32_t _M0L11conn__indexS5082;
      struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0L3synS1731;
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS5076;
      struct _M0TPB5ArrayGfE* _M0L4valsS5064;
      struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS5075;
      struct _M0TPB5ArrayGbE* _M0L4fireS5065;
      struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS5074;
      struct _M0TPB5ArrayGbE* _M0L4fireS5066;
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS5073;
      struct _M0TPB5ArrayGiE* _M0L6colptrS5067;
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS5072;
      int32_t _M0L6_2acntS5244;
      struct _M0TPB5ArrayGiE* _M0L6rowptrS5068;
      int32_t _M0L6_2acntS5255;
      struct _M0TPB5ArrayGfE* _M0L4tpreS5069;
      struct _M0TPB5ArrayGfE* _M0L5tpostS5070;
      struct _M0TP26RiantR8snn__mbt14STDPMexicanHat* _M0L5paramS5071;
      struct _M0TPB5ArrayGfE* _M0L6t__nowS5077;
      struct _M0TPB5ArrayGfE* _M0L6t__nowS5080;
      int32_t _M0L6_2acntS5259;
      float _M0L6_2atmpS5079;
      float _M0L6_2atmpS5078;
      struct _M0TPB5ArrayGRP26RiantR8snn__mbt14SpikingSynapseE* _M0L5connsS5062;
      int32_t _M0L11conn__indexS5063;
      struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0L3synS1728;
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS5057;
      struct _M0TPB5ArrayGfE* _M0L4valsS5046;
      struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS5056;
      struct _M0TPB5ArrayGbE* _M0L4fireS5047;
      struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS5055;
      struct _M0TPB5ArrayGbE* _M0L4fireS5048;
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS5054;
      struct _M0TPB5ArrayGiE* _M0L6colptrS5049;
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS5053;
      int32_t _M0L6_2acntS5225;
      struct _M0TPB5ArrayGiE* _M0L6rowptrS5050;
      int32_t _M0L6_2acntS5236;
      struct _M0TP26RiantR8snn__mbt26STDPAntiSymmetricVariables* _M0L4varsS5051;
      struct _M0TP26RiantR8snn__mbt17STDPAntiSymmetric* _M0L5paramS5052;
      struct _M0TPB5ArrayGfE* _M0L6t__nowS5058;
      struct _M0TPB5ArrayGfE* _M0L6t__nowS5061;
      int32_t _M0L6_2acntS5240;
      float _M0L6_2atmpS5060;
      float _M0L6_2atmpS5059;
      struct _M0TPB5ArrayGRP26RiantR8snn__mbt14SpikingSynapseE* _M0L5connsS5044;
      int32_t _M0L11conn__indexS5045;
      struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0L3synS1725;
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS5039;
      struct _M0TPB5ArrayGfE* _M0L4valsS5026;
      struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS5038;
      struct _M0TPB5ArrayGbE* _M0L4fireS5027;
      struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS5037;
      struct _M0TPB5ArrayGbE* _M0L4fireS5028;
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS5036;
      struct _M0TPB5ArrayGiE* _M0L6colptrS5029;
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS5035;
      int32_t _M0L6_2acntS5206;
      struct _M0TPB5ArrayGiE* _M0L6rowptrS5030;
      int32_t _M0L6_2acntS5217;
      struct _M0TP26RiantR8snn__mbt13STDPVariables* _M0L4varsS5031;
      struct _M0TP26RiantR8snn__mbt18STDPConfavreux2025* _M0L5paramS5032;
      struct _M0TPB5ArrayGfE* _M0L6t__nowS5034;
      float _M0L6_2atmpS5033;
      struct _M0TPB5ArrayGfE* _M0L6t__nowS5040;
      struct _M0TPB5ArrayGfE* _M0L6t__nowS5043;
      int32_t _M0L6_2acntS5221;
      float _M0L6_2atmpS5042;
      float _M0L6_2atmpS5041;
      struct _M0TPB5ArrayGRP26RiantR8snn__mbt14SpikingSynapseE* _M0L5connsS5024;
      int32_t _M0L11conn__indexS5025;
      struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0L3synS1722;
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS5019;
      struct _M0TPB5ArrayGfE* _M0L4valsS5006;
      struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS5018;
      struct _M0TPB5ArrayGbE* _M0L4fireS5007;
      struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS5017;
      struct _M0TPB5ArrayGbE* _M0L4fireS5008;
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS5016;
      struct _M0TPB5ArrayGiE* _M0L6colptrS5009;
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS5015;
      int32_t _M0L6_2acntS5187;
      struct _M0TPB5ArrayGiE* _M0L6rowptrS5010;
      int32_t _M0L6_2acntS5198;
      struct _M0TP26RiantR8snn__mbt18IstdpRateVariables* _M0L4varsS5011;
      struct _M0TP26RiantR8snn__mbt9IstdpRate* _M0L5paramS5012;
      struct _M0TPB5ArrayGfE* _M0L6t__nowS5014;
      float _M0L6_2atmpS5013;
      struct _M0TPB5ArrayGfE* _M0L6t__nowS5020;
      struct _M0TPB5ArrayGfE* _M0L6t__nowS5023;
      int32_t _M0L6_2acntS5202;
      float _M0L6_2atmpS5022;
      float _M0L6_2atmpS5021;
      struct _M0TPB5ArrayGRP26RiantR8snn__mbt14SpikingSynapseE* _M0L5connsS5004;
      int32_t _M0L11conn__indexS5005;
      struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0L3synS1719;
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS4999;
      struct _M0TPB5ArrayGfE* _M0L4valsS4984;
      struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS4998;
      struct _M0TPB5ArrayGbE* _M0L4fireS4985;
      struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS4997;
      struct _M0TPB5ArrayGbE* _M0L4fireS4986;
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS4996;
      struct _M0TPB5ArrayGiE* _M0L6colptrS4987;
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS4995;
      struct _M0TPB5ArrayGiE* _M0L6rowptrS4988;
      struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS4994;
      int32_t _M0L6_2acntS5155;
      struct _M0TPB5ArrayGfE* _M0L1vS4989;
      int32_t _M0L6_2acntS5166;
      struct _M0TP26RiantR8snn__mbt23IstdpPotentialVariables* _M0L4varsS4990;
      struct _M0TP26RiantR8snn__mbt14IstdpPotential* _M0L5paramS4991;
      struct _M0TPB5ArrayGfE* _M0L6t__nowS4993;
      float _M0L6_2atmpS4992;
      struct _M0TPB5ArrayGfE* _M0L6t__nowS5000;
      struct _M0TPB5ArrayGfE* _M0L6t__nowS5003;
      int32_t _M0L6_2acntS5183;
      float _M0L6_2atmpS5002;
      float _M0L6_2atmpS5001;
      struct _M0TPB5ArrayGRP26RiantR8snn__mbt14SpikingSynapseE* _M0L5connsS4982;
      int32_t _M0L11conn__indexS4983;
      struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0L3synS1716;
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS4977;
      struct _M0TPB5ArrayGfE* _M0L4valsS4964;
      struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS4976;
      struct _M0TPB5ArrayGbE* _M0L4fireS4965;
      struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS4975;
      struct _M0TPB5ArrayGbE* _M0L4fireS4966;
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS4974;
      struct _M0TPB5ArrayGiE* _M0L6colptrS4967;
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS4973;
      int32_t _M0L6_2acntS5136;
      struct _M0TPB5ArrayGiE* _M0L6rowptrS4968;
      int32_t _M0L6_2acntS5147;
      struct _M0TP26RiantR8snn__mbt22STDPSymmetricVariables* _M0L4varsS4969;
      struct _M0TP26RiantR8snn__mbt13STDPSymmetric* _M0L5paramS4970;
      struct _M0TPB5ArrayGfE* _M0L6t__nowS4972;
      float _M0L6_2atmpS4971;
      struct _M0TPB5ArrayGfE* _M0L6t__nowS4978;
      struct _M0TPB5ArrayGfE* _M0L6t__nowS4981;
      int32_t _M0L6_2acntS5151;
      float _M0L6_2atmpS4980;
      float _M0L6_2atmpS4979;
      int32_t _M0L6_2atmpS5103;
      switch (Moonbit_object_tag(_M0L5entryS1713)) {
        case 0: {
          struct _M0DTP26RiantR8snn__mbt13STDPEntryKind10Gerstner__* _M0L13_2aGerstner__S1735 =
            (struct _M0DTP26RiantR8snn__mbt13STDPEntryKind10Gerstner__*)_M0L5entryS1713;
          struct _M0TP26RiantR8snn__mbt9STDPEntry* _M0L4_2aeS1736 =
            _M0L13_2aGerstner__S1735->$0;
          moonbit_incref(_M0L4_2aeS1736);
          _M0L1eS1733 = _M0L4_2aeS1736;
          goto join_1732;
          break;
        }
        
        case 1: {
          struct _M0DTP26RiantR8snn__mbt13STDPEntryKind12MexicanHat__* _M0L15_2aMexicanHat__S1737 =
            (struct _M0DTP26RiantR8snn__mbt13STDPEntryKind12MexicanHat__*)_M0L5entryS1713;
          struct _M0TP26RiantR8snn__mbt19STDPEntryMexicanHat* _M0L4_2aeS1738 =
            _M0L15_2aMexicanHat__S1737->$0;
          moonbit_incref(_M0L4_2aeS1738);
          _M0L1eS1730 = _M0L4_2aeS1738;
          goto join_1729;
          break;
        }
        
        case 2: {
          struct _M0DTP26RiantR8snn__mbt13STDPEntryKind15AntiSymmetric__* _M0L18_2aAntiSymmetric__S1739 =
            (struct _M0DTP26RiantR8snn__mbt13STDPEntryKind15AntiSymmetric__*)_M0L5entryS1713;
          struct _M0TP26RiantR8snn__mbt22STDPEntryAntiSymmetric* _M0L4_2aeS1740 =
            _M0L18_2aAntiSymmetric__S1739->$0;
          moonbit_incref(_M0L4_2aeS1740);
          _M0L1eS1727 = _M0L4_2aeS1740;
          goto join_1726;
          break;
        }
        
        case 3: {
          struct _M0DTP26RiantR8snn__mbt13STDPEntryKind16Confavreux2025__* _M0L19_2aConfavreux2025__S1741 =
            (struct _M0DTP26RiantR8snn__mbt13STDPEntryKind16Confavreux2025__*)_M0L5entryS1713;
          struct _M0TP26RiantR8snn__mbt23STDPEntryConfavreux2025* _M0L4_2aeS1742 =
            _M0L19_2aConfavreux2025__S1741->$0;
          moonbit_incref(_M0L4_2aeS1742);
          _M0L1eS1724 = _M0L4_2aeS1742;
          goto join_1723;
          break;
        }
        
        case 4: {
          struct _M0DTP26RiantR8snn__mbt13STDPEntryKind11IstdpRate__* _M0L14_2aIstdpRate__S1743 =
            (struct _M0DTP26RiantR8snn__mbt13STDPEntryKind11IstdpRate__*)_M0L5entryS1713;
          struct _M0TP26RiantR8snn__mbt14IstdpRateEntry* _M0L4_2aeS1744 =
            _M0L14_2aIstdpRate__S1743->$0;
          moonbit_incref(_M0L4_2aeS1744);
          _M0L1eS1721 = _M0L4_2aeS1744;
          goto join_1720;
          break;
        }
        
        case 5: {
          struct _M0DTP26RiantR8snn__mbt13STDPEntryKind16IstdpPotential__* _M0L19_2aIstdpPotential__S1745 =
            (struct _M0DTP26RiantR8snn__mbt13STDPEntryKind16IstdpPotential__*)_M0L5entryS1713;
          struct _M0TP26RiantR8snn__mbt19IstdpPotentialEntry* _M0L4_2aeS1746 =
            _M0L19_2aIstdpPotential__S1745->$0;
          moonbit_incref(_M0L4_2aeS1746);
          _M0L1eS1718 = _M0L4_2aeS1746;
          goto join_1717;
          break;
        }
        default: {
          struct _M0DTP26RiantR8snn__mbt13STDPEntryKind11Symmetric__* _M0L14_2aSymmetric__S1747 =
            (struct _M0DTP26RiantR8snn__mbt13STDPEntryKind11Symmetric__*)_M0L5entryS1713;
          struct _M0TP26RiantR8snn__mbt18STDPEntrySymmetric* _M0L4_2aeS1748 =
            _M0L14_2aSymmetric__S1747->$0;
          moonbit_incref(_M0L4_2aeS1748);
          _M0L1eS1715 = _M0L4_2aeS1748;
          goto join_1714;
          break;
        }
      }
      goto joinlet_5319;
      join_1732:;
      _M0L5connsS5101 = _M0L1mS1669->$1;
      _M0L11conn__indexS5102 = _M0L1eS1733->$0;
      #line 136 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L3synS1734
      = _M0MPC15array5Array2atGRP26RiantR8snn__mbt14SpikingSynapseE(_M0L5connsS5101, _M0L11conn__indexS5102);
      _M0L6matrixS5096 = _M0L3synS1734->$4;
      _M0L4valsS5083 = _M0L6matrixS5096->$4;
      _M0L3preS5095 = _M0L3synS1734->$0;
      _M0L4fireS5084 = _M0L3preS5095->$5;
      _M0L4postS5094 = _M0L3synS1734->$1;
      _M0L4fireS5085 = _M0L4postS5094->$5;
      _M0L6matrixS5093 = _M0L3synS1734->$4;
      _M0L6colptrS5086 = _M0L6matrixS5093->$3;
      _M0L6matrixS5092 = _M0L3synS1734->$4;
      moonbit_incref(_M0L6colptrS5086);
      moonbit_incref(_M0L4fireS5085);
      moonbit_incref(_M0L4fireS5084);
      moonbit_incref(_M0L4valsS5083);
      _M0L6_2acntS5264
      = Moonbit_rc_count(Moonbit_object_header(_M0L3synS1734));
      if (_M0L6_2acntS5264 > 1) {
        int32_t _M0L11_2anew__cntS5274 = _M0L6_2acntS5264 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L3synS1734), _M0L11_2anew__cntS5274);
        moonbit_incref(_M0L6matrixS5092);
      } else if (_M0L6_2acntS5264 == 1) {
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5273 = _M0L3synS1734->$9;
        struct _M0TPB5ArrayGiE* _M0L8_2afieldS5272;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5271;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5270;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5269;
        moonbit_string_t _M0L8_2afieldS5268;
        moonbit_string_t _M0L8_2afieldS5267;
        struct _M0TP26RiantR8snn__mbt2IF* _M0L8_2afieldS5266;
        struct _M0TP26RiantR8snn__mbt2IF* _M0L8_2afieldS5265;
        moonbit_decref(_M0L8_2afieldS5273);
        _M0L8_2afieldS5272 = _M0L3synS1734->$8;
        moonbit_decref(_M0L8_2afieldS5272);
        _M0L8_2afieldS5271 = _M0L3synS1734->$7;
        moonbit_decref(_M0L8_2afieldS5271);
        _M0L8_2afieldS5270 = _M0L3synS1734->$6;
        moonbit_decref(_M0L8_2afieldS5270);
        _M0L8_2afieldS5269 = _M0L3synS1734->$5;
        moonbit_decref(_M0L8_2afieldS5269);
        _M0L8_2afieldS5268 = _M0L3synS1734->$3;
        moonbit_decref(_M0L8_2afieldS5268);
        _M0L8_2afieldS5267 = _M0L3synS1734->$2;
        moonbit_decref(_M0L8_2afieldS5267);
        _M0L8_2afieldS5266 = _M0L3synS1734->$1;
        moonbit_decref(_M0L8_2afieldS5266);
        _M0L8_2afieldS5265 = _M0L3synS1734->$0;
        moonbit_decref(_M0L8_2afieldS5265);
        #line 136 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
        moonbit_free(_M0L3synS1734);
      }
      _M0L6rowptrS5087 = _M0L6matrixS5092->$2;
      _M0L6_2acntS5275
      = Moonbit_rc_count(Moonbit_object_header(_M0L6matrixS5092));
      if (_M0L6_2acntS5275 > 1) {
        int32_t _M0L11_2anew__cntS5278 = _M0L6_2acntS5275 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L6matrixS5092), _M0L11_2anew__cntS5278);
        moonbit_incref(_M0L6rowptrS5087);
      } else if (_M0L6_2acntS5275 == 1) {
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5277 = _M0L6matrixS5092->$4;
        struct _M0TPB5ArrayGiE* _M0L8_2afieldS5276;
        moonbit_decref(_M0L8_2afieldS5277);
        _M0L8_2afieldS5276 = _M0L6matrixS5092->$3;
        moonbit_decref(_M0L8_2afieldS5276);
        #line 136 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
        moonbit_free(_M0L6matrixS5092);
      }
      _M0L4varsS5088 = _M0L1eS1733->$3;
      _M0L5paramS5089 = _M0L1eS1733->$4;
      _M0L6t__nowS5091 = _M0L1eS1733->$5;
      moonbit_incref(_M0L6t__nowS5091);
      moonbit_incref(_M0L5paramS5089);
      moonbit_incref(_M0L4varsS5088);
      #line 145 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L6_2atmpS5090 = _M0MPC15array5Array2atGfE(_M0L6t__nowS5091, 0);
      moonbit_decref(_M0L6t__nowS5091);
      #line 137 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0FP26RiantR8snn__mbt10stdp__step(_M0L4valsS5083, _M0L4fireS5084, _M0L4fireS5085, _M0L6colptrS5086, _M0L6rowptrS5087, _M0L4varsS5088, _M0L5paramS5089, _M0L6_2atmpS5090, _M0L2dtS1674);
      moonbit_decref(_M0L4valsS5083);
      moonbit_decref(_M0L4fireS5084);
      moonbit_decref(_M0L4fireS5085);
      moonbit_decref(_M0L6colptrS5086);
      moonbit_decref(_M0L6rowptrS5087);
      moonbit_decref(_M0L4varsS5088);
      moonbit_decref(_M0L5paramS5089);
      _M0L6t__nowS5097 = _M0L1eS1733->$5;
      _M0L6t__nowS5100 = _M0L1eS1733->$5;
      moonbit_incref(_M0L6t__nowS5097);
      _M0L6_2acntS5279 = Moonbit_rc_count(Moonbit_object_header(_M0L1eS1733));
      if (_M0L6_2acntS5279 > 1) {
        int32_t _M0L11_2anew__cntS5282 = _M0L6_2acntS5279 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L1eS1733), _M0L11_2anew__cntS5282);
        moonbit_incref(_M0L6t__nowS5100);
      } else if (_M0L6_2acntS5279 == 1) {
        struct _M0TP26RiantR8snn__mbt12STDPGerstner* _M0L8_2afieldS5281 =
          _M0L1eS1733->$4;
        struct _M0TP26RiantR8snn__mbt13STDPVariables* _M0L8_2afieldS5280;
        moonbit_decref(_M0L8_2afieldS5281);
        _M0L8_2afieldS5280 = _M0L1eS1733->$3;
        moonbit_decref(_M0L8_2afieldS5280);
        #line 136 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
        moonbit_free(_M0L1eS1733);
      }
      #line 148 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L6_2atmpS5099 = _M0MPC15array5Array2atGfE(_M0L6t__nowS5100, 0);
      moonbit_decref(_M0L6t__nowS5100);
      _M0L6_2atmpS5098 = _M0L6_2atmpS5099 + _M0L2dtS1674;
      #line 148 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0MPC15array5Array3setGfE(_M0L6t__nowS5097, 0, _M0L6_2atmpS5098);
      moonbit_decref(_M0L6t__nowS5097);
      joinlet_5319:;
      goto joinlet_5318;
      join_1729:;
      _M0L5connsS5081 = _M0L1mS1669->$1;
      _M0L11conn__indexS5082 = _M0L1eS1730->$0;
      #line 151 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L3synS1731
      = _M0MPC15array5Array2atGRP26RiantR8snn__mbt14SpikingSynapseE(_M0L5connsS5081, _M0L11conn__indexS5082);
      _M0L6matrixS5076 = _M0L3synS1731->$4;
      _M0L4valsS5064 = _M0L6matrixS5076->$4;
      _M0L3preS5075 = _M0L3synS1731->$0;
      _M0L4fireS5065 = _M0L3preS5075->$5;
      _M0L4postS5074 = _M0L3synS1731->$1;
      _M0L4fireS5066 = _M0L4postS5074->$5;
      _M0L6matrixS5073 = _M0L3synS1731->$4;
      _M0L6colptrS5067 = _M0L6matrixS5073->$3;
      _M0L6matrixS5072 = _M0L3synS1731->$4;
      moonbit_incref(_M0L6colptrS5067);
      moonbit_incref(_M0L4fireS5066);
      moonbit_incref(_M0L4fireS5065);
      moonbit_incref(_M0L4valsS5064);
      _M0L6_2acntS5244
      = Moonbit_rc_count(Moonbit_object_header(_M0L3synS1731));
      if (_M0L6_2acntS5244 > 1) {
        int32_t _M0L11_2anew__cntS5254 = _M0L6_2acntS5244 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L3synS1731), _M0L11_2anew__cntS5254);
        moonbit_incref(_M0L6matrixS5072);
      } else if (_M0L6_2acntS5244 == 1) {
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5253 = _M0L3synS1731->$9;
        struct _M0TPB5ArrayGiE* _M0L8_2afieldS5252;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5251;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5250;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5249;
        moonbit_string_t _M0L8_2afieldS5248;
        moonbit_string_t _M0L8_2afieldS5247;
        struct _M0TP26RiantR8snn__mbt2IF* _M0L8_2afieldS5246;
        struct _M0TP26RiantR8snn__mbt2IF* _M0L8_2afieldS5245;
        moonbit_decref(_M0L8_2afieldS5253);
        _M0L8_2afieldS5252 = _M0L3synS1731->$8;
        moonbit_decref(_M0L8_2afieldS5252);
        _M0L8_2afieldS5251 = _M0L3synS1731->$7;
        moonbit_decref(_M0L8_2afieldS5251);
        _M0L8_2afieldS5250 = _M0L3synS1731->$6;
        moonbit_decref(_M0L8_2afieldS5250);
        _M0L8_2afieldS5249 = _M0L3synS1731->$5;
        moonbit_decref(_M0L8_2afieldS5249);
        _M0L8_2afieldS5248 = _M0L3synS1731->$3;
        moonbit_decref(_M0L8_2afieldS5248);
        _M0L8_2afieldS5247 = _M0L3synS1731->$2;
        moonbit_decref(_M0L8_2afieldS5247);
        _M0L8_2afieldS5246 = _M0L3synS1731->$1;
        moonbit_decref(_M0L8_2afieldS5246);
        _M0L8_2afieldS5245 = _M0L3synS1731->$0;
        moonbit_decref(_M0L8_2afieldS5245);
        #line 151 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
        moonbit_free(_M0L3synS1731);
      }
      _M0L6rowptrS5068 = _M0L6matrixS5072->$2;
      _M0L6_2acntS5255
      = Moonbit_rc_count(Moonbit_object_header(_M0L6matrixS5072));
      if (_M0L6_2acntS5255 > 1) {
        int32_t _M0L11_2anew__cntS5258 = _M0L6_2acntS5255 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L6matrixS5072), _M0L11_2anew__cntS5258);
        moonbit_incref(_M0L6rowptrS5068);
      } else if (_M0L6_2acntS5255 == 1) {
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5257 = _M0L6matrixS5072->$4;
        struct _M0TPB5ArrayGiE* _M0L8_2afieldS5256;
        moonbit_decref(_M0L8_2afieldS5257);
        _M0L8_2afieldS5256 = _M0L6matrixS5072->$3;
        moonbit_decref(_M0L8_2afieldS5256);
        #line 151 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
        moonbit_free(_M0L6matrixS5072);
      }
      _M0L4tpreS5069 = _M0L1eS1730->$4;
      _M0L5tpostS5070 = _M0L1eS1730->$5;
      _M0L5paramS5071 = _M0L1eS1730->$3;
      moonbit_incref(_M0L5paramS5071);
      moonbit_incref(_M0L5tpostS5070);
      moonbit_incref(_M0L4tpreS5069);
      #line 152 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0FP26RiantR8snn__mbt24stdp__mexican__hat__step(_M0L4valsS5064, _M0L4fireS5065, _M0L4fireS5066, _M0L6colptrS5067, _M0L6rowptrS5068, _M0L4tpreS5069, _M0L5tpostS5070, _M0L5paramS5071, _M0L2dtS1674);
      moonbit_decref(_M0L4valsS5064);
      moonbit_decref(_M0L4fireS5065);
      moonbit_decref(_M0L4fireS5066);
      moonbit_decref(_M0L6colptrS5067);
      moonbit_decref(_M0L6rowptrS5068);
      moonbit_decref(_M0L4tpreS5069);
      moonbit_decref(_M0L5tpostS5070);
      moonbit_decref(_M0L5paramS5071);
      _M0L6t__nowS5077 = _M0L1eS1730->$6;
      _M0L6t__nowS5080 = _M0L1eS1730->$6;
      moonbit_incref(_M0L6t__nowS5077);
      _M0L6_2acntS5259 = Moonbit_rc_count(Moonbit_object_header(_M0L1eS1730));
      if (_M0L6_2acntS5259 > 1) {
        int32_t _M0L11_2anew__cntS5263 = _M0L6_2acntS5259 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L1eS1730), _M0L11_2anew__cntS5263);
        moonbit_incref(_M0L6t__nowS5080);
      } else if (_M0L6_2acntS5259 == 1) {
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5262 = _M0L1eS1730->$5;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5261;
        struct _M0TP26RiantR8snn__mbt14STDPMexicanHat* _M0L8_2afieldS5260;
        moonbit_decref(_M0L8_2afieldS5262);
        _M0L8_2afieldS5261 = _M0L1eS1730->$4;
        moonbit_decref(_M0L8_2afieldS5261);
        _M0L8_2afieldS5260 = _M0L1eS1730->$3;
        moonbit_decref(_M0L8_2afieldS5260);
        #line 151 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
        moonbit_free(_M0L1eS1730);
      }
      #line 163 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L6_2atmpS5079 = _M0MPC15array5Array2atGfE(_M0L6t__nowS5080, 0);
      moonbit_decref(_M0L6t__nowS5080);
      _M0L6_2atmpS5078 = _M0L6_2atmpS5079 + _M0L2dtS1674;
      #line 163 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0MPC15array5Array3setGfE(_M0L6t__nowS5077, 0, _M0L6_2atmpS5078);
      moonbit_decref(_M0L6t__nowS5077);
      joinlet_5318:;
      goto joinlet_5317;
      join_1726:;
      _M0L5connsS5062 = _M0L1mS1669->$1;
      _M0L11conn__indexS5063 = _M0L1eS1727->$0;
      #line 166 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L3synS1728
      = _M0MPC15array5Array2atGRP26RiantR8snn__mbt14SpikingSynapseE(_M0L5connsS5062, _M0L11conn__indexS5063);
      _M0L6matrixS5057 = _M0L3synS1728->$4;
      _M0L4valsS5046 = _M0L6matrixS5057->$4;
      _M0L3preS5056 = _M0L3synS1728->$0;
      _M0L4fireS5047 = _M0L3preS5056->$5;
      _M0L4postS5055 = _M0L3synS1728->$1;
      _M0L4fireS5048 = _M0L4postS5055->$5;
      _M0L6matrixS5054 = _M0L3synS1728->$4;
      _M0L6colptrS5049 = _M0L6matrixS5054->$3;
      _M0L6matrixS5053 = _M0L3synS1728->$4;
      moonbit_incref(_M0L6colptrS5049);
      moonbit_incref(_M0L4fireS5048);
      moonbit_incref(_M0L4fireS5047);
      moonbit_incref(_M0L4valsS5046);
      _M0L6_2acntS5225
      = Moonbit_rc_count(Moonbit_object_header(_M0L3synS1728));
      if (_M0L6_2acntS5225 > 1) {
        int32_t _M0L11_2anew__cntS5235 = _M0L6_2acntS5225 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L3synS1728), _M0L11_2anew__cntS5235);
        moonbit_incref(_M0L6matrixS5053);
      } else if (_M0L6_2acntS5225 == 1) {
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5234 = _M0L3synS1728->$9;
        struct _M0TPB5ArrayGiE* _M0L8_2afieldS5233;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5232;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5231;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5230;
        moonbit_string_t _M0L8_2afieldS5229;
        moonbit_string_t _M0L8_2afieldS5228;
        struct _M0TP26RiantR8snn__mbt2IF* _M0L8_2afieldS5227;
        struct _M0TP26RiantR8snn__mbt2IF* _M0L8_2afieldS5226;
        moonbit_decref(_M0L8_2afieldS5234);
        _M0L8_2afieldS5233 = _M0L3synS1728->$8;
        moonbit_decref(_M0L8_2afieldS5233);
        _M0L8_2afieldS5232 = _M0L3synS1728->$7;
        moonbit_decref(_M0L8_2afieldS5232);
        _M0L8_2afieldS5231 = _M0L3synS1728->$6;
        moonbit_decref(_M0L8_2afieldS5231);
        _M0L8_2afieldS5230 = _M0L3synS1728->$5;
        moonbit_decref(_M0L8_2afieldS5230);
        _M0L8_2afieldS5229 = _M0L3synS1728->$3;
        moonbit_decref(_M0L8_2afieldS5229);
        _M0L8_2afieldS5228 = _M0L3synS1728->$2;
        moonbit_decref(_M0L8_2afieldS5228);
        _M0L8_2afieldS5227 = _M0L3synS1728->$1;
        moonbit_decref(_M0L8_2afieldS5227);
        _M0L8_2afieldS5226 = _M0L3synS1728->$0;
        moonbit_decref(_M0L8_2afieldS5226);
        #line 166 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
        moonbit_free(_M0L3synS1728);
      }
      _M0L6rowptrS5050 = _M0L6matrixS5053->$2;
      _M0L6_2acntS5236
      = Moonbit_rc_count(Moonbit_object_header(_M0L6matrixS5053));
      if (_M0L6_2acntS5236 > 1) {
        int32_t _M0L11_2anew__cntS5239 = _M0L6_2acntS5236 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L6matrixS5053), _M0L11_2anew__cntS5239);
        moonbit_incref(_M0L6rowptrS5050);
      } else if (_M0L6_2acntS5236 == 1) {
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5238 = _M0L6matrixS5053->$4;
        struct _M0TPB5ArrayGiE* _M0L8_2afieldS5237;
        moonbit_decref(_M0L8_2afieldS5238);
        _M0L8_2afieldS5237 = _M0L6matrixS5053->$3;
        moonbit_decref(_M0L8_2afieldS5237);
        #line 166 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
        moonbit_free(_M0L6matrixS5053);
      }
      _M0L4varsS5051 = _M0L1eS1727->$4;
      _M0L5paramS5052 = _M0L1eS1727->$3;
      moonbit_incref(_M0L5paramS5052);
      moonbit_incref(_M0L4varsS5051);
      #line 167 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0FP26RiantR8snn__mbt25stdp__antisymmetric__step(_M0L4valsS5046, _M0L4fireS5047, _M0L4fireS5048, _M0L6colptrS5049, _M0L6rowptrS5050, _M0L4varsS5051, _M0L5paramS5052, _M0L2dtS1674);
      moonbit_decref(_M0L4valsS5046);
      moonbit_decref(_M0L4fireS5047);
      moonbit_decref(_M0L4fireS5048);
      moonbit_decref(_M0L6colptrS5049);
      moonbit_decref(_M0L6rowptrS5050);
      moonbit_decref(_M0L4varsS5051);
      moonbit_decref(_M0L5paramS5052);
      _M0L6t__nowS5058 = _M0L1eS1727->$5;
      _M0L6t__nowS5061 = _M0L1eS1727->$5;
      moonbit_incref(_M0L6t__nowS5058);
      _M0L6_2acntS5240 = Moonbit_rc_count(Moonbit_object_header(_M0L1eS1727));
      if (_M0L6_2acntS5240 > 1) {
        int32_t _M0L11_2anew__cntS5243 = _M0L6_2acntS5240 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L1eS1727), _M0L11_2anew__cntS5243);
        moonbit_incref(_M0L6t__nowS5061);
      } else if (_M0L6_2acntS5240 == 1) {
        struct _M0TP26RiantR8snn__mbt26STDPAntiSymmetricVariables* _M0L8_2afieldS5242 =
          _M0L1eS1727->$4;
        struct _M0TP26RiantR8snn__mbt17STDPAntiSymmetric* _M0L8_2afieldS5241;
        moonbit_decref(_M0L8_2afieldS5242);
        _M0L8_2afieldS5241 = _M0L1eS1727->$3;
        moonbit_decref(_M0L8_2afieldS5241);
        #line 166 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
        moonbit_free(_M0L1eS1727);
      }
      #line 177 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L6_2atmpS5060 = _M0MPC15array5Array2atGfE(_M0L6t__nowS5061, 0);
      moonbit_decref(_M0L6t__nowS5061);
      _M0L6_2atmpS5059 = _M0L6_2atmpS5060 + _M0L2dtS1674;
      #line 177 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0MPC15array5Array3setGfE(_M0L6t__nowS5058, 0, _M0L6_2atmpS5059);
      moonbit_decref(_M0L6t__nowS5058);
      joinlet_5317:;
      goto joinlet_5316;
      join_1723:;
      _M0L5connsS5044 = _M0L1mS1669->$1;
      _M0L11conn__indexS5045 = _M0L1eS1724->$0;
      #line 180 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L3synS1725
      = _M0MPC15array5Array2atGRP26RiantR8snn__mbt14SpikingSynapseE(_M0L5connsS5044, _M0L11conn__indexS5045);
      _M0L6matrixS5039 = _M0L3synS1725->$4;
      _M0L4valsS5026 = _M0L6matrixS5039->$4;
      _M0L3preS5038 = _M0L3synS1725->$0;
      _M0L4fireS5027 = _M0L3preS5038->$5;
      _M0L4postS5037 = _M0L3synS1725->$1;
      _M0L4fireS5028 = _M0L4postS5037->$5;
      _M0L6matrixS5036 = _M0L3synS1725->$4;
      _M0L6colptrS5029 = _M0L6matrixS5036->$3;
      _M0L6matrixS5035 = _M0L3synS1725->$4;
      moonbit_incref(_M0L6colptrS5029);
      moonbit_incref(_M0L4fireS5028);
      moonbit_incref(_M0L4fireS5027);
      moonbit_incref(_M0L4valsS5026);
      _M0L6_2acntS5206
      = Moonbit_rc_count(Moonbit_object_header(_M0L3synS1725));
      if (_M0L6_2acntS5206 > 1) {
        int32_t _M0L11_2anew__cntS5216 = _M0L6_2acntS5206 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L3synS1725), _M0L11_2anew__cntS5216);
        moonbit_incref(_M0L6matrixS5035);
      } else if (_M0L6_2acntS5206 == 1) {
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5215 = _M0L3synS1725->$9;
        struct _M0TPB5ArrayGiE* _M0L8_2afieldS5214;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5213;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5212;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5211;
        moonbit_string_t _M0L8_2afieldS5210;
        moonbit_string_t _M0L8_2afieldS5209;
        struct _M0TP26RiantR8snn__mbt2IF* _M0L8_2afieldS5208;
        struct _M0TP26RiantR8snn__mbt2IF* _M0L8_2afieldS5207;
        moonbit_decref(_M0L8_2afieldS5215);
        _M0L8_2afieldS5214 = _M0L3synS1725->$8;
        moonbit_decref(_M0L8_2afieldS5214);
        _M0L8_2afieldS5213 = _M0L3synS1725->$7;
        moonbit_decref(_M0L8_2afieldS5213);
        _M0L8_2afieldS5212 = _M0L3synS1725->$6;
        moonbit_decref(_M0L8_2afieldS5212);
        _M0L8_2afieldS5211 = _M0L3synS1725->$5;
        moonbit_decref(_M0L8_2afieldS5211);
        _M0L8_2afieldS5210 = _M0L3synS1725->$3;
        moonbit_decref(_M0L8_2afieldS5210);
        _M0L8_2afieldS5209 = _M0L3synS1725->$2;
        moonbit_decref(_M0L8_2afieldS5209);
        _M0L8_2afieldS5208 = _M0L3synS1725->$1;
        moonbit_decref(_M0L8_2afieldS5208);
        _M0L8_2afieldS5207 = _M0L3synS1725->$0;
        moonbit_decref(_M0L8_2afieldS5207);
        #line 180 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
        moonbit_free(_M0L3synS1725);
      }
      _M0L6rowptrS5030 = _M0L6matrixS5035->$2;
      _M0L6_2acntS5217
      = Moonbit_rc_count(Moonbit_object_header(_M0L6matrixS5035));
      if (_M0L6_2acntS5217 > 1) {
        int32_t _M0L11_2anew__cntS5220 = _M0L6_2acntS5217 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L6matrixS5035), _M0L11_2anew__cntS5220);
        moonbit_incref(_M0L6rowptrS5030);
      } else if (_M0L6_2acntS5217 == 1) {
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5219 = _M0L6matrixS5035->$4;
        struct _M0TPB5ArrayGiE* _M0L8_2afieldS5218;
        moonbit_decref(_M0L8_2afieldS5219);
        _M0L8_2afieldS5218 = _M0L6matrixS5035->$3;
        moonbit_decref(_M0L8_2afieldS5218);
        #line 180 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
        moonbit_free(_M0L6matrixS5035);
      }
      _M0L4varsS5031 = _M0L1eS1724->$4;
      _M0L5paramS5032 = _M0L1eS1724->$3;
      _M0L6t__nowS5034 = _M0L1eS1724->$5;
      moonbit_incref(_M0L6t__nowS5034);
      moonbit_incref(_M0L5paramS5032);
      moonbit_incref(_M0L4varsS5031);
      #line 189 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L6_2atmpS5033 = _M0MPC15array5Array2atGfE(_M0L6t__nowS5034, 0);
      moonbit_decref(_M0L6t__nowS5034);
      #line 181 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0FP26RiantR8snn__mbt22stdp__confavreux__step(_M0L4valsS5026, _M0L4fireS5027, _M0L4fireS5028, _M0L6colptrS5029, _M0L6rowptrS5030, _M0L4varsS5031, _M0L5paramS5032, _M0L6_2atmpS5033, _M0L2dtS1674);
      moonbit_decref(_M0L4valsS5026);
      moonbit_decref(_M0L4fireS5027);
      moonbit_decref(_M0L4fireS5028);
      moonbit_decref(_M0L6colptrS5029);
      moonbit_decref(_M0L6rowptrS5030);
      moonbit_decref(_M0L4varsS5031);
      moonbit_decref(_M0L5paramS5032);
      _M0L6t__nowS5040 = _M0L1eS1724->$5;
      _M0L6t__nowS5043 = _M0L1eS1724->$5;
      moonbit_incref(_M0L6t__nowS5040);
      _M0L6_2acntS5221 = Moonbit_rc_count(Moonbit_object_header(_M0L1eS1724));
      if (_M0L6_2acntS5221 > 1) {
        int32_t _M0L11_2anew__cntS5224 = _M0L6_2acntS5221 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L1eS1724), _M0L11_2anew__cntS5224);
        moonbit_incref(_M0L6t__nowS5043);
      } else if (_M0L6_2acntS5221 == 1) {
        struct _M0TP26RiantR8snn__mbt13STDPVariables* _M0L8_2afieldS5223 =
          _M0L1eS1724->$4;
        struct _M0TP26RiantR8snn__mbt18STDPConfavreux2025* _M0L8_2afieldS5222;
        moonbit_decref(_M0L8_2afieldS5223);
        _M0L8_2afieldS5222 = _M0L1eS1724->$3;
        moonbit_decref(_M0L8_2afieldS5222);
        #line 180 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
        moonbit_free(_M0L1eS1724);
      }
      #line 192 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L6_2atmpS5042 = _M0MPC15array5Array2atGfE(_M0L6t__nowS5043, 0);
      moonbit_decref(_M0L6t__nowS5043);
      _M0L6_2atmpS5041 = _M0L6_2atmpS5042 + _M0L2dtS1674;
      #line 192 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0MPC15array5Array3setGfE(_M0L6t__nowS5040, 0, _M0L6_2atmpS5041);
      moonbit_decref(_M0L6t__nowS5040);
      joinlet_5316:;
      goto joinlet_5315;
      join_1720:;
      _M0L5connsS5024 = _M0L1mS1669->$1;
      _M0L11conn__indexS5025 = _M0L1eS1721->$0;
      #line 195 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L3synS1722
      = _M0MPC15array5Array2atGRP26RiantR8snn__mbt14SpikingSynapseE(_M0L5connsS5024, _M0L11conn__indexS5025);
      _M0L6matrixS5019 = _M0L3synS1722->$4;
      _M0L4valsS5006 = _M0L6matrixS5019->$4;
      _M0L3preS5018 = _M0L3synS1722->$0;
      _M0L4fireS5007 = _M0L3preS5018->$5;
      _M0L4postS5017 = _M0L3synS1722->$1;
      _M0L4fireS5008 = _M0L4postS5017->$5;
      _M0L6matrixS5016 = _M0L3synS1722->$4;
      _M0L6colptrS5009 = _M0L6matrixS5016->$3;
      _M0L6matrixS5015 = _M0L3synS1722->$4;
      moonbit_incref(_M0L6colptrS5009);
      moonbit_incref(_M0L4fireS5008);
      moonbit_incref(_M0L4fireS5007);
      moonbit_incref(_M0L4valsS5006);
      _M0L6_2acntS5187
      = Moonbit_rc_count(Moonbit_object_header(_M0L3synS1722));
      if (_M0L6_2acntS5187 > 1) {
        int32_t _M0L11_2anew__cntS5197 = _M0L6_2acntS5187 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L3synS1722), _M0L11_2anew__cntS5197);
        moonbit_incref(_M0L6matrixS5015);
      } else if (_M0L6_2acntS5187 == 1) {
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5196 = _M0L3synS1722->$9;
        struct _M0TPB5ArrayGiE* _M0L8_2afieldS5195;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5194;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5193;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5192;
        moonbit_string_t _M0L8_2afieldS5191;
        moonbit_string_t _M0L8_2afieldS5190;
        struct _M0TP26RiantR8snn__mbt2IF* _M0L8_2afieldS5189;
        struct _M0TP26RiantR8snn__mbt2IF* _M0L8_2afieldS5188;
        moonbit_decref(_M0L8_2afieldS5196);
        _M0L8_2afieldS5195 = _M0L3synS1722->$8;
        moonbit_decref(_M0L8_2afieldS5195);
        _M0L8_2afieldS5194 = _M0L3synS1722->$7;
        moonbit_decref(_M0L8_2afieldS5194);
        _M0L8_2afieldS5193 = _M0L3synS1722->$6;
        moonbit_decref(_M0L8_2afieldS5193);
        _M0L8_2afieldS5192 = _M0L3synS1722->$5;
        moonbit_decref(_M0L8_2afieldS5192);
        _M0L8_2afieldS5191 = _M0L3synS1722->$3;
        moonbit_decref(_M0L8_2afieldS5191);
        _M0L8_2afieldS5190 = _M0L3synS1722->$2;
        moonbit_decref(_M0L8_2afieldS5190);
        _M0L8_2afieldS5189 = _M0L3synS1722->$1;
        moonbit_decref(_M0L8_2afieldS5189);
        _M0L8_2afieldS5188 = _M0L3synS1722->$0;
        moonbit_decref(_M0L8_2afieldS5188);
        #line 195 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
        moonbit_free(_M0L3synS1722);
      }
      _M0L6rowptrS5010 = _M0L6matrixS5015->$2;
      _M0L6_2acntS5198
      = Moonbit_rc_count(Moonbit_object_header(_M0L6matrixS5015));
      if (_M0L6_2acntS5198 > 1) {
        int32_t _M0L11_2anew__cntS5201 = _M0L6_2acntS5198 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L6matrixS5015), _M0L11_2anew__cntS5201);
        moonbit_incref(_M0L6rowptrS5010);
      } else if (_M0L6_2acntS5198 == 1) {
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5200 = _M0L6matrixS5015->$4;
        struct _M0TPB5ArrayGiE* _M0L8_2afieldS5199;
        moonbit_decref(_M0L8_2afieldS5200);
        _M0L8_2afieldS5199 = _M0L6matrixS5015->$3;
        moonbit_decref(_M0L8_2afieldS5199);
        #line 195 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
        moonbit_free(_M0L6matrixS5015);
      }
      _M0L4varsS5011 = _M0L1eS1721->$4;
      _M0L5paramS5012 = _M0L1eS1721->$3;
      _M0L6t__nowS5014 = _M0L1eS1721->$5;
      moonbit_incref(_M0L6t__nowS5014);
      moonbit_incref(_M0L5paramS5012);
      moonbit_incref(_M0L4varsS5011);
      #line 204 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L6_2atmpS5013 = _M0MPC15array5Array2atGfE(_M0L6t__nowS5014, 0);
      moonbit_decref(_M0L6t__nowS5014);
      #line 196 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0FP26RiantR8snn__mbt17istdp__rate__step(_M0L4valsS5006, _M0L4fireS5007, _M0L4fireS5008, _M0L6colptrS5009, _M0L6rowptrS5010, _M0L4varsS5011, _M0L5paramS5012, _M0L6_2atmpS5013, _M0L2dtS1674);
      moonbit_decref(_M0L4valsS5006);
      moonbit_decref(_M0L4fireS5007);
      moonbit_decref(_M0L4fireS5008);
      moonbit_decref(_M0L6colptrS5009);
      moonbit_decref(_M0L6rowptrS5010);
      moonbit_decref(_M0L4varsS5011);
      moonbit_decref(_M0L5paramS5012);
      _M0L6t__nowS5020 = _M0L1eS1721->$5;
      _M0L6t__nowS5023 = _M0L1eS1721->$5;
      moonbit_incref(_M0L6t__nowS5020);
      _M0L6_2acntS5202 = Moonbit_rc_count(Moonbit_object_header(_M0L1eS1721));
      if (_M0L6_2acntS5202 > 1) {
        int32_t _M0L11_2anew__cntS5205 = _M0L6_2acntS5202 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L1eS1721), _M0L11_2anew__cntS5205);
        moonbit_incref(_M0L6t__nowS5023);
      } else if (_M0L6_2acntS5202 == 1) {
        struct _M0TP26RiantR8snn__mbt18IstdpRateVariables* _M0L8_2afieldS5204 =
          _M0L1eS1721->$4;
        struct _M0TP26RiantR8snn__mbt9IstdpRate* _M0L8_2afieldS5203;
        moonbit_decref(_M0L8_2afieldS5204);
        _M0L8_2afieldS5203 = _M0L1eS1721->$3;
        moonbit_decref(_M0L8_2afieldS5203);
        #line 195 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
        moonbit_free(_M0L1eS1721);
      }
      #line 207 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L6_2atmpS5022 = _M0MPC15array5Array2atGfE(_M0L6t__nowS5023, 0);
      moonbit_decref(_M0L6t__nowS5023);
      _M0L6_2atmpS5021 = _M0L6_2atmpS5022 + _M0L2dtS1674;
      #line 207 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0MPC15array5Array3setGfE(_M0L6t__nowS5020, 0, _M0L6_2atmpS5021);
      moonbit_decref(_M0L6t__nowS5020);
      joinlet_5315:;
      goto joinlet_5314;
      join_1717:;
      _M0L5connsS5004 = _M0L1mS1669->$1;
      _M0L11conn__indexS5005 = _M0L1eS1718->$0;
      #line 210 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L3synS1719
      = _M0MPC15array5Array2atGRP26RiantR8snn__mbt14SpikingSynapseE(_M0L5connsS5004, _M0L11conn__indexS5005);
      _M0L6matrixS4999 = _M0L3synS1719->$4;
      _M0L4valsS4984 = _M0L6matrixS4999->$4;
      _M0L3preS4998 = _M0L3synS1719->$0;
      _M0L4fireS4985 = _M0L3preS4998->$5;
      _M0L4postS4997 = _M0L3synS1719->$1;
      _M0L4fireS4986 = _M0L4postS4997->$5;
      _M0L6matrixS4996 = _M0L3synS1719->$4;
      _M0L6colptrS4987 = _M0L6matrixS4996->$3;
      _M0L6matrixS4995 = _M0L3synS1719->$4;
      _M0L6rowptrS4988 = _M0L6matrixS4995->$2;
      _M0L4postS4994 = _M0L3synS1719->$1;
      moonbit_incref(_M0L6rowptrS4988);
      moonbit_incref(_M0L6colptrS4987);
      moonbit_incref(_M0L4fireS4986);
      moonbit_incref(_M0L4fireS4985);
      moonbit_incref(_M0L4valsS4984);
      _M0L6_2acntS5155
      = Moonbit_rc_count(Moonbit_object_header(_M0L3synS1719));
      if (_M0L6_2acntS5155 > 1) {
        int32_t _M0L11_2anew__cntS5165 = _M0L6_2acntS5155 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L3synS1719), _M0L11_2anew__cntS5165);
        moonbit_incref(_M0L4postS4994);
      } else if (_M0L6_2acntS5155 == 1) {
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5164 = _M0L3synS1719->$9;
        struct _M0TPB5ArrayGiE* _M0L8_2afieldS5163;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5162;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5161;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5160;
        struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L8_2afieldS5159;
        moonbit_string_t _M0L8_2afieldS5158;
        moonbit_string_t _M0L8_2afieldS5157;
        struct _M0TP26RiantR8snn__mbt2IF* _M0L8_2afieldS5156;
        moonbit_decref(_M0L8_2afieldS5164);
        _M0L8_2afieldS5163 = _M0L3synS1719->$8;
        moonbit_decref(_M0L8_2afieldS5163);
        _M0L8_2afieldS5162 = _M0L3synS1719->$7;
        moonbit_decref(_M0L8_2afieldS5162);
        _M0L8_2afieldS5161 = _M0L3synS1719->$6;
        moonbit_decref(_M0L8_2afieldS5161);
        _M0L8_2afieldS5160 = _M0L3synS1719->$5;
        moonbit_decref(_M0L8_2afieldS5160);
        _M0L8_2afieldS5159 = _M0L3synS1719->$4;
        moonbit_decref(_M0L8_2afieldS5159);
        _M0L8_2afieldS5158 = _M0L3synS1719->$3;
        moonbit_decref(_M0L8_2afieldS5158);
        _M0L8_2afieldS5157 = _M0L3synS1719->$2;
        moonbit_decref(_M0L8_2afieldS5157);
        _M0L8_2afieldS5156 = _M0L3synS1719->$0;
        moonbit_decref(_M0L8_2afieldS5156);
        #line 210 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
        moonbit_free(_M0L3synS1719);
      }
      _M0L1vS4989 = _M0L4postS4994->$3;
      _M0L6_2acntS5166
      = Moonbit_rc_count(Moonbit_object_header(_M0L4postS4994));
      if (_M0L6_2acntS5166 > 1) {
        int32_t _M0L11_2anew__cntS5182 = _M0L6_2acntS5166 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L4postS4994), _M0L11_2anew__cntS5182);
        moonbit_incref(_M0L1vS4989);
      } else if (_M0L6_2acntS5166 == 1) {
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5181 = _M0L4postS4994->$16;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5180;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5179;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5178;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5177;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5176;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5175;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5174;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5173;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5172;
        struct _M0TPB5ArrayGiE* _M0L8_2afieldS5171;
        struct _M0TPB5ArrayGbE* _M0L8_2afieldS5170;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5169;
        struct _M0TP26RiantR8snn__mbt9PostSpike* _M0L8_2afieldS5168;
        struct _M0TP26RiantR8snn__mbt11IFParameter* _M0L8_2afieldS5167;
        moonbit_decref(_M0L8_2afieldS5181);
        _M0L8_2afieldS5180 = _M0L4postS4994->$15;
        moonbit_decref(_M0L8_2afieldS5180);
        _M0L8_2afieldS5179 = _M0L4postS4994->$14;
        moonbit_decref(_M0L8_2afieldS5179);
        _M0L8_2afieldS5178 = _M0L4postS4994->$13;
        moonbit_decref(_M0L8_2afieldS5178);
        _M0L8_2afieldS5177 = _M0L4postS4994->$12;
        moonbit_decref(_M0L8_2afieldS5177);
        _M0L8_2afieldS5176 = _M0L4postS4994->$11;
        moonbit_decref(_M0L8_2afieldS5176);
        _M0L8_2afieldS5175 = _M0L4postS4994->$10;
        moonbit_decref(_M0L8_2afieldS5175);
        _M0L8_2afieldS5174 = _M0L4postS4994->$9;
        moonbit_decref(_M0L8_2afieldS5174);
        _M0L8_2afieldS5173 = _M0L4postS4994->$8;
        moonbit_decref(_M0L8_2afieldS5173);
        _M0L8_2afieldS5172 = _M0L4postS4994->$7;
        moonbit_decref(_M0L8_2afieldS5172);
        _M0L8_2afieldS5171 = _M0L4postS4994->$6;
        moonbit_decref(_M0L8_2afieldS5171);
        _M0L8_2afieldS5170 = _M0L4postS4994->$5;
        moonbit_decref(_M0L8_2afieldS5170);
        _M0L8_2afieldS5169 = _M0L4postS4994->$4;
        moonbit_decref(_M0L8_2afieldS5169);
        _M0L8_2afieldS5168 = _M0L4postS4994->$1;
        moonbit_decref(_M0L8_2afieldS5168);
        _M0L8_2afieldS5167 = _M0L4postS4994->$0;
        moonbit_decref(_M0L8_2afieldS5167);
        #line 210 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
        moonbit_free(_M0L4postS4994);
      }
      _M0L4varsS4990 = _M0L1eS1718->$4;
      _M0L5paramS4991 = _M0L1eS1718->$3;
      _M0L6t__nowS4993 = _M0L1eS1718->$5;
      moonbit_incref(_M0L6t__nowS4993);
      moonbit_incref(_M0L5paramS4991);
      moonbit_incref(_M0L4varsS4990);
      #line 220 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L6_2atmpS4992 = _M0MPC15array5Array2atGfE(_M0L6t__nowS4993, 0);
      moonbit_decref(_M0L6t__nowS4993);
      #line 211 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0FP26RiantR8snn__mbt22istdp__potential__step(_M0L4valsS4984, _M0L4fireS4985, _M0L4fireS4986, _M0L6colptrS4987, _M0L6rowptrS4988, _M0L1vS4989, _M0L4varsS4990, _M0L5paramS4991, _M0L6_2atmpS4992, _M0L2dtS1674);
      moonbit_decref(_M0L4valsS4984);
      moonbit_decref(_M0L4fireS4985);
      moonbit_decref(_M0L4fireS4986);
      moonbit_decref(_M0L6colptrS4987);
      moonbit_decref(_M0L6rowptrS4988);
      moonbit_decref(_M0L1vS4989);
      moonbit_decref(_M0L4varsS4990);
      moonbit_decref(_M0L5paramS4991);
      _M0L6t__nowS5000 = _M0L1eS1718->$5;
      _M0L6t__nowS5003 = _M0L1eS1718->$5;
      moonbit_incref(_M0L6t__nowS5000);
      _M0L6_2acntS5183 = Moonbit_rc_count(Moonbit_object_header(_M0L1eS1718));
      if (_M0L6_2acntS5183 > 1) {
        int32_t _M0L11_2anew__cntS5186 = _M0L6_2acntS5183 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L1eS1718), _M0L11_2anew__cntS5186);
        moonbit_incref(_M0L6t__nowS5003);
      } else if (_M0L6_2acntS5183 == 1) {
        struct _M0TP26RiantR8snn__mbt23IstdpPotentialVariables* _M0L8_2afieldS5185 =
          _M0L1eS1718->$4;
        struct _M0TP26RiantR8snn__mbt14IstdpPotential* _M0L8_2afieldS5184;
        moonbit_decref(_M0L8_2afieldS5185);
        _M0L8_2afieldS5184 = _M0L1eS1718->$3;
        moonbit_decref(_M0L8_2afieldS5184);
        #line 210 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
        moonbit_free(_M0L1eS1718);
      }
      #line 223 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L6_2atmpS5002 = _M0MPC15array5Array2atGfE(_M0L6t__nowS5003, 0);
      moonbit_decref(_M0L6t__nowS5003);
      _M0L6_2atmpS5001 = _M0L6_2atmpS5002 + _M0L2dtS1674;
      #line 223 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0MPC15array5Array3setGfE(_M0L6t__nowS5000, 0, _M0L6_2atmpS5001);
      moonbit_decref(_M0L6t__nowS5000);
      joinlet_5314:;
      goto joinlet_5313;
      join_1714:;
      _M0L5connsS4982 = _M0L1mS1669->$1;
      _M0L11conn__indexS4983 = _M0L1eS1715->$0;
      #line 226 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L3synS1716
      = _M0MPC15array5Array2atGRP26RiantR8snn__mbt14SpikingSynapseE(_M0L5connsS4982, _M0L11conn__indexS4983);
      _M0L6matrixS4977 = _M0L3synS1716->$4;
      _M0L4valsS4964 = _M0L6matrixS4977->$4;
      _M0L3preS4976 = _M0L3synS1716->$0;
      _M0L4fireS4965 = _M0L3preS4976->$5;
      _M0L4postS4975 = _M0L3synS1716->$1;
      _M0L4fireS4966 = _M0L4postS4975->$5;
      _M0L6matrixS4974 = _M0L3synS1716->$4;
      _M0L6colptrS4967 = _M0L6matrixS4974->$3;
      _M0L6matrixS4973 = _M0L3synS1716->$4;
      moonbit_incref(_M0L6colptrS4967);
      moonbit_incref(_M0L4fireS4966);
      moonbit_incref(_M0L4fireS4965);
      moonbit_incref(_M0L4valsS4964);
      _M0L6_2acntS5136
      = Moonbit_rc_count(Moonbit_object_header(_M0L3synS1716));
      if (_M0L6_2acntS5136 > 1) {
        int32_t _M0L11_2anew__cntS5146 = _M0L6_2acntS5136 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L3synS1716), _M0L11_2anew__cntS5146);
        moonbit_incref(_M0L6matrixS4973);
      } else if (_M0L6_2acntS5136 == 1) {
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5145 = _M0L3synS1716->$9;
        struct _M0TPB5ArrayGiE* _M0L8_2afieldS5144;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5143;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5142;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5141;
        moonbit_string_t _M0L8_2afieldS5140;
        moonbit_string_t _M0L8_2afieldS5139;
        struct _M0TP26RiantR8snn__mbt2IF* _M0L8_2afieldS5138;
        struct _M0TP26RiantR8snn__mbt2IF* _M0L8_2afieldS5137;
        moonbit_decref(_M0L8_2afieldS5145);
        _M0L8_2afieldS5144 = _M0L3synS1716->$8;
        moonbit_decref(_M0L8_2afieldS5144);
        _M0L8_2afieldS5143 = _M0L3synS1716->$7;
        moonbit_decref(_M0L8_2afieldS5143);
        _M0L8_2afieldS5142 = _M0L3synS1716->$6;
        moonbit_decref(_M0L8_2afieldS5142);
        _M0L8_2afieldS5141 = _M0L3synS1716->$5;
        moonbit_decref(_M0L8_2afieldS5141);
        _M0L8_2afieldS5140 = _M0L3synS1716->$3;
        moonbit_decref(_M0L8_2afieldS5140);
        _M0L8_2afieldS5139 = _M0L3synS1716->$2;
        moonbit_decref(_M0L8_2afieldS5139);
        _M0L8_2afieldS5138 = _M0L3synS1716->$1;
        moonbit_decref(_M0L8_2afieldS5138);
        _M0L8_2afieldS5137 = _M0L3synS1716->$0;
        moonbit_decref(_M0L8_2afieldS5137);
        #line 226 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
        moonbit_free(_M0L3synS1716);
      }
      _M0L6rowptrS4968 = _M0L6matrixS4973->$2;
      _M0L6_2acntS5147
      = Moonbit_rc_count(Moonbit_object_header(_M0L6matrixS4973));
      if (_M0L6_2acntS5147 > 1) {
        int32_t _M0L11_2anew__cntS5150 = _M0L6_2acntS5147 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L6matrixS4973), _M0L11_2anew__cntS5150);
        moonbit_incref(_M0L6rowptrS4968);
      } else if (_M0L6_2acntS5147 == 1) {
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5149 = _M0L6matrixS4973->$4;
        struct _M0TPB5ArrayGiE* _M0L8_2afieldS5148;
        moonbit_decref(_M0L8_2afieldS5149);
        _M0L8_2afieldS5148 = _M0L6matrixS4973->$3;
        moonbit_decref(_M0L8_2afieldS5148);
        #line 226 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
        moonbit_free(_M0L6matrixS4973);
      }
      _M0L4varsS4969 = _M0L1eS1715->$4;
      _M0L5paramS4970 = _M0L1eS1715->$3;
      _M0L6t__nowS4972 = _M0L1eS1715->$5;
      moonbit_incref(_M0L6t__nowS4972);
      moonbit_incref(_M0L5paramS4970);
      moonbit_incref(_M0L4varsS4969);
      #line 235 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L6_2atmpS4971 = _M0MPC15array5Array2atGfE(_M0L6t__nowS4972, 0);
      moonbit_decref(_M0L6t__nowS4972);
      #line 227 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0FP26RiantR8snn__mbt21stdp__symmetric__step(_M0L4valsS4964, _M0L4fireS4965, _M0L4fireS4966, _M0L6colptrS4967, _M0L6rowptrS4968, _M0L4varsS4969, _M0L5paramS4970, _M0L6_2atmpS4971, _M0L2dtS1674);
      moonbit_decref(_M0L4valsS4964);
      moonbit_decref(_M0L4fireS4965);
      moonbit_decref(_M0L4fireS4966);
      moonbit_decref(_M0L6colptrS4967);
      moonbit_decref(_M0L6rowptrS4968);
      moonbit_decref(_M0L4varsS4969);
      moonbit_decref(_M0L5paramS4970);
      _M0L6t__nowS4978 = _M0L1eS1715->$5;
      _M0L6t__nowS4981 = _M0L1eS1715->$5;
      moonbit_incref(_M0L6t__nowS4978);
      _M0L6_2acntS5151 = Moonbit_rc_count(Moonbit_object_header(_M0L1eS1715));
      if (_M0L6_2acntS5151 > 1) {
        int32_t _M0L11_2anew__cntS5154 = _M0L6_2acntS5151 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L1eS1715), _M0L11_2anew__cntS5154);
        moonbit_incref(_M0L6t__nowS4981);
      } else if (_M0L6_2acntS5151 == 1) {
        struct _M0TP26RiantR8snn__mbt22STDPSymmetricVariables* _M0L8_2afieldS5153 =
          _M0L1eS1715->$4;
        struct _M0TP26RiantR8snn__mbt13STDPSymmetric* _M0L8_2afieldS5152;
        moonbit_decref(_M0L8_2afieldS5153);
        _M0L8_2afieldS5152 = _M0L1eS1715->$3;
        moonbit_decref(_M0L8_2afieldS5152);
        #line 226 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
        moonbit_free(_M0L1eS1715);
      }
      #line 238 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L6_2atmpS4980 = _M0MPC15array5Array2atGfE(_M0L6t__nowS4981, 0);
      moonbit_decref(_M0L6t__nowS4981);
      _M0L6_2atmpS4979 = _M0L6_2atmpS4980 + _M0L2dtS1674;
      #line 238 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0MPC15array5Array3setGfE(_M0L6t__nowS4978, 0, _M0L6_2atmpS4979);
      moonbit_decref(_M0L6t__nowS4978);
      joinlet_5313:;
      _M0L6_2atmpS5103 = _M0L2__S1712 + 1;
      _M0L2__S1712 = _M0L6_2atmpS5103;
      continue;
    } else {
      moonbit_decref(_M0L7_2abindS1711);
    }
    break;
  }
  _M0L7_2abindS1750 = _M0L1mS1669->$0;
  _M0L7_2abindS1751 = _M0L7_2abindS1750->$1;
  _M0L7_2abindS1752 = _M0L7_2abindS1750->$0;
  moonbit_incref(_M0L7_2abindS1752);
  _M0L2__S1753 = 0;
  while (1) {
    if (_M0L2__S1753 < _M0L7_2abindS1751) {
      void* _M0L1pS1754 = (void*)_M0L7_2abindS1752[_M0L2__S1753];
      int32_t _M0L6_2atmpS5104;
      moonbit_incref(_M0L1pS1754);
      #line 244 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0FP26RiantR8snn__mbt14integrate__any(_M0L1pS1754, _M0L2dtS1674);
      moonbit_decref(_M0L1pS1754);
      _M0L6_2atmpS5104 = _M0L2__S1753 + 1;
      _M0L2__S1753 = _M0L6_2atmpS5104;
      continue;
    } else {
      moonbit_decref(_M0L7_2abindS1752);
    }
    break;
  }
  _M0L7_2abindS1756 = _M0L1mS1669->$4;
  _M0L7_2abindS1757 = _M0L7_2abindS1756->$1;
  _M0L7_2abindS1758 = _M0L7_2abindS1756->$0;
  moonbit_incref(_M0L7_2abindS1758);
  _M0L2__S1759 = 0;
  while (1) {
    if (_M0L2__S1759 < _M0L7_2abindS1757) {
      struct _M0TP26RiantR8snn__mbt7Monitor* _M0L3monS1760 =
        (struct _M0TP26RiantR8snn__mbt7Monitor*)_M0L7_2abindS1758[
          _M0L2__S1759
        ];
      struct _M0TP26RiantR8snn__mbt4Time* _M0L4timeS5106 = _M0L1mS1669->$3;
      float _M0L6_2atmpS5105;
      int32_t _M0L6_2atmpS5107;
      moonbit_incref(_M0L3monS1760);
      #line 248 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L6_2atmpS5105 = _M0FP26RiantR8snn__mbt9get__time(_M0L4timeS5106);
      #line 248 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0FP26RiantR8snn__mbt11record__one(_M0L3monS1760, _M0L6_2atmpS5105);
      moonbit_decref(_M0L3monS1760);
      _M0L6_2atmpS5107 = _M0L2__S1759 + 1;
      _M0L2__S1759 = _M0L6_2atmpS5107;
      continue;
    } else {
      moonbit_decref(_M0L7_2abindS1758);
    }
    break;
  }
  _M0L4timeS5108 = _M0L1mS1669->$3;
  #line 251 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
  _M0FP26RiantR8snn__mbt12update__time(_M0L4timeS5108, _M0L2dtS1674);
  return 0;
}

struct _M0TP26RiantR8snn__mbt18HeterogeneousModel* _M0FP26RiantR8snn__mbt7compose(
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt6AnyPopE* _M0L4popsS1666,
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt14SpikingSynapseE* _M0L5connsS1667,
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt7AnyStimE* _M0L11stims_2eoptS1655,
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt7MonitorE* _M0L14monitors_2eoptS1658,
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt13STDPEntryKindE* _M0L10stdp_2eoptS1661,
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt12STPEntryKindE* _M0L9stp_2eoptS1664
) {
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt7AnyStimE* _M0L5stimsS1654;
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt7MonitorE* _M0L8monitorsS1657;
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt13STDPEntryKindE* _M0L4stdpS1660;
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt12STPEntryKindE* _M0L3stpS1663;
  struct _M0TP26RiantR8snn__mbt18HeterogeneousModel* _result_5322;
  if (_M0L11stims_2eoptS1655 == 0) {
    void** _M0L6_2atmpS4936 = (void**)moonbit_empty_ref_array;
    _M0L5stimsS1654
    = (struct _M0TPB5ArrayGRP26RiantR8snn__mbt7AnyStimE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGRP26RiantR8snn__mbt7AnyStimE));
    Moonbit_object_header(_M0L5stimsS1654)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 0, 0);
    _M0L5stimsS1654->$0 = _M0L6_2atmpS4936;
    _M0L5stimsS1654->$1 = 0;
  } else {
    struct _M0TPB5ArrayGRP26RiantR8snn__mbt7AnyStimE* _M0L7_2aSomeS1656 =
      _M0L11stims_2eoptS1655;
    if (_M0L7_2aSomeS1656) {
      moonbit_incref(_M0L7_2aSomeS1656);
    }
    _M0L5stimsS1654 = _M0L7_2aSomeS1656;
  }
  if (_M0L14monitors_2eoptS1658 == 0) {
    struct _M0TP26RiantR8snn__mbt7Monitor** _M0L6_2atmpS4935 =
      (struct _M0TP26RiantR8snn__mbt7Monitor**)moonbit_empty_ref_array;
    _M0L8monitorsS1657
    = (struct _M0TPB5ArrayGRP26RiantR8snn__mbt7MonitorE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGRP26RiantR8snn__mbt7MonitorE));
    Moonbit_object_header(_M0L8monitorsS1657)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 3, 0);
    _M0L8monitorsS1657->$0 = _M0L6_2atmpS4935;
    _M0L8monitorsS1657->$1 = 0;
  } else {
    struct _M0TPB5ArrayGRP26RiantR8snn__mbt7MonitorE* _M0L7_2aSomeS1659 =
      _M0L14monitors_2eoptS1658;
    if (_M0L7_2aSomeS1659) {
      moonbit_incref(_M0L7_2aSomeS1659);
    }
    _M0L8monitorsS1657 = _M0L7_2aSomeS1659;
  }
  if (_M0L10stdp_2eoptS1661 == 0) {
    void** _M0L6_2atmpS4934 = (void**)moonbit_empty_ref_array;
    _M0L4stdpS1660
    = (struct _M0TPB5ArrayGRP26RiantR8snn__mbt13STDPEntryKindE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGRP26RiantR8snn__mbt13STDPEntryKindE));
    Moonbit_object_header(_M0L4stdpS1660)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 6, 0);
    _M0L4stdpS1660->$0 = _M0L6_2atmpS4934;
    _M0L4stdpS1660->$1 = 0;
  } else {
    struct _M0TPB5ArrayGRP26RiantR8snn__mbt13STDPEntryKindE* _M0L7_2aSomeS1662 =
      _M0L10stdp_2eoptS1661;
    if (_M0L7_2aSomeS1662) {
      moonbit_incref(_M0L7_2aSomeS1662);
    }
    _M0L4stdpS1660 = _M0L7_2aSomeS1662;
  }
  if (_M0L9stp_2eoptS1664 == 0) {
    void** _M0L6_2atmpS4933 = (void**)moonbit_empty_ref_array;
    _M0L3stpS1663
    = (struct _M0TPB5ArrayGRP26RiantR8snn__mbt12STPEntryKindE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGRP26RiantR8snn__mbt12STPEntryKindE));
    Moonbit_object_header(_M0L3stpS1663)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 9, 0);
    _M0L3stpS1663->$0 = _M0L6_2atmpS4933;
    _M0L3stpS1663->$1 = 0;
  } else {
    struct _M0TPB5ArrayGRP26RiantR8snn__mbt12STPEntryKindE* _M0L7_2aSomeS1665 =
      _M0L9stp_2eoptS1664;
    if (_M0L7_2aSomeS1665) {
      moonbit_incref(_M0L7_2aSomeS1665);
    }
    _M0L3stpS1663 = _M0L7_2aSomeS1665;
  }
  _result_5322
  = _M0FP26RiantR8snn__mbt15compose_2einner(_M0L4popsS1666, _M0L5connsS1667, _M0L5stimsS1654, _M0L8monitorsS1657, _M0L4stdpS1660, _M0L3stpS1663);
  moonbit_decref(_M0L5stimsS1654);
  moonbit_decref(_M0L8monitorsS1657);
  moonbit_decref(_M0L4stdpS1660);
  moonbit_decref(_M0L3stpS1663);
  return _result_5322;
}

struct _M0TP26RiantR8snn__mbt18HeterogeneousModel* _M0FP26RiantR8snn__mbt15compose_2einner(
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt6AnyPopE* _M0L4popsS1648,
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt14SpikingSynapseE* _M0L5connsS1649,
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt7AnyStimE* _M0L5stimsS1650,
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt7MonitorE* _M0L8monitorsS1651,
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt13STDPEntryKindE* _M0L4stdpS1652,
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt12STPEntryKindE* _M0L3stpS1653
) {
  struct _M0TP26RiantR8snn__mbt4Time* _M0L6_2atmpS4932;
  struct _M0TP26RiantR8snn__mbt18HeterogeneousModel* _block_5323;
  #line 79 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
  #line 87 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
  _M0L6_2atmpS4932 = _M0MP26RiantR8snn__mbt4Time3new();
  moonbit_incref(_M0L4popsS1648);
  moonbit_incref(_M0L5connsS1649);
  moonbit_incref(_M0L5stimsS1650);
  moonbit_incref(_M0L8monitorsS1651);
  moonbit_incref(_M0L4stdpS1652);
  moonbit_incref(_M0L3stpS1653);
  _block_5323
  = (struct _M0TP26RiantR8snn__mbt18HeterogeneousModel*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt18HeterogeneousModel));
  Moonbit_object_header(_block_5323)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 12, 0);
  _block_5323->$0 = _M0L4popsS1648;
  _block_5323->$1 = _M0L5connsS1649;
  _block_5323->$2 = _M0L5stimsS1650;
  _block_5323->$3 = _M0L6_2atmpS4932;
  _block_5323->$4 = _M0L8monitorsS1651;
  _block_5323->$5 = _M0L4stdpS1652;
  _block_5323->$6 = _M0L3stpS1653;
  return _block_5323;
}

int32_t _M0FP26RiantR8snn__mbt14stimulate__any(
  void* _M0L1sS1634,
  struct _M0TP26RiantR8snn__mbt4Time* _M0L1tS1622,
  float _M0L2dtS1629
) {
  struct _M0TP26RiantR8snn__mbt17SpikeTimeStimulus* _M0L1xS1620;
  float _M0L1wS1621;
  struct _M0TP26RiantR8snn__mbt20CurrentStimulusArray* _M0L1xS1624;
  struct _M0TP26RiantR8snn__mbt17CurrentStimulusIF* _M0L1xS1626;
  struct _M0TP26RiantR8snn__mbt16BalancedStimulus* _M0L1xS1628;
  struct _M0TP26RiantR8snn__mbt20PoissonLayerStimulus* _M0L1xS1631;
  struct _M0TP26RiantR8snn__mbt17PoissonStimulusIF* _M0L1xS1633;
  float _M0L6_2atmpS4931;
  float _M0L6_2atmpS4930;
  float _M0L6_2atmpS4929;
  float _M0L6_2atmpS4928;
  #line 64 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
  switch (Moonbit_object_tag(_M0L1sS1634)) {
    case 0: {
      struct _M0DTP26RiantR8snn__mbt7AnyStim11PoissonIF__* _M0L14_2aPoissonIF__S1635 =
        (struct _M0DTP26RiantR8snn__mbt7AnyStim11PoissonIF__*)_M0L1sS1634;
      struct _M0TP26RiantR8snn__mbt17PoissonStimulusIF* _M0L4_2axS1636 =
        _M0L14_2aPoissonIF__S1635->$0;
      moonbit_incref(_M0L4_2axS1636);
      _M0L1xS1633 = _M0L4_2axS1636;
      goto join_1632;
      break;
    }
    
    case 1: {
      struct _M0DTP26RiantR8snn__mbt7AnyStim14PoissonLayer__* _M0L17_2aPoissonLayer__S1637 =
        (struct _M0DTP26RiantR8snn__mbt7AnyStim14PoissonLayer__*)_M0L1sS1634;
      struct _M0TP26RiantR8snn__mbt20PoissonLayerStimulus* _M0L4_2axS1638 =
        _M0L17_2aPoissonLayer__S1637->$0;
      moonbit_incref(_M0L4_2axS1638);
      _M0L1xS1631 = _M0L4_2axS1638;
      goto join_1630;
      break;
    }
    
    case 2: {
      struct _M0DTP26RiantR8snn__mbt7AnyStim12BalancedIF__* _M0L15_2aBalancedIF__S1639 =
        (struct _M0DTP26RiantR8snn__mbt7AnyStim12BalancedIF__*)_M0L1sS1634;
      struct _M0TP26RiantR8snn__mbt16BalancedStimulus* _M0L4_2axS1640 =
        _M0L15_2aBalancedIF__S1639->$0;
      moonbit_incref(_M0L4_2axS1640);
      _M0L1xS1628 = _M0L4_2axS1640;
      goto join_1627;
      break;
    }
    
    case 3: {
      struct _M0DTP26RiantR8snn__mbt7AnyStim11CurrentIF__* _M0L14_2aCurrentIF__S1641 =
        (struct _M0DTP26RiantR8snn__mbt7AnyStim11CurrentIF__*)_M0L1sS1634;
      struct _M0TP26RiantR8snn__mbt17CurrentStimulusIF* _M0L4_2axS1642 =
        _M0L14_2aCurrentIF__S1641->$0;
      moonbit_incref(_M0L4_2axS1642);
      _M0L1xS1626 = _M0L4_2axS1642;
      goto join_1625;
      break;
    }
    
    case 4: {
      struct _M0DTP26RiantR8snn__mbt7AnyStim12CurrentArr__* _M0L15_2aCurrentArr__S1643 =
        (struct _M0DTP26RiantR8snn__mbt7AnyStim12CurrentArr__*)_M0L1sS1634;
      struct _M0TP26RiantR8snn__mbt20CurrentStimulusArray* _M0L4_2axS1644 =
        _M0L15_2aCurrentArr__S1643->$0;
      moonbit_incref(_M0L4_2axS1644);
      _M0L1xS1624 = _M0L4_2axS1644;
      goto join_1623;
      break;
    }
    default: {
      struct _M0DTP26RiantR8snn__mbt7AnyStim11TimedStim__* _M0L14_2aTimedStim__S1645 =
        (struct _M0DTP26RiantR8snn__mbt7AnyStim11TimedStim__*)_M0L1sS1634;
      struct _M0TP26RiantR8snn__mbt17SpikeTimeStimulus* _M0L4_2axS1646 =
        _M0L14_2aTimedStim__S1645->$0;
      float _M0L4_2awS1647 = _M0L14_2aTimedStim__S1645->$1;
      moonbit_incref(_M0L4_2axS1646);
      _M0L1xS1620 = _M0L4_2axS1646;
      _M0L1wS1621 = _M0L4_2awS1647;
      goto join_1619;
      break;
    }
  }
  goto joinlet_5329;
  join_1632:;
  #line 66 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
  _M0L6_2atmpS4931 = _M0FP26RiantR8snn__mbt9get__time(_M0L1tS1622);
  #line 66 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
  _M0FP26RiantR8snn__mbt13stimulate__if(_M0L1xS1633, _M0L6_2atmpS4931, _M0L2dtS1629);
  moonbit_decref(_M0L1xS1633);
  joinlet_5329:;
  goto joinlet_5328;
  join_1630:;
  #line 67 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
  _M0L6_2atmpS4930 = _M0FP26RiantR8snn__mbt9get__time(_M0L1tS1622);
  #line 67 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
  _M0FP26RiantR8snn__mbt16stimulate__layer(_M0L1xS1631, _M0L6_2atmpS4930, _M0L2dtS1629);
  moonbit_decref(_M0L1xS1631);
  joinlet_5328:;
  goto joinlet_5327;
  join_1627:;
  #line 68 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
  _M0L6_2atmpS4929 = _M0FP26RiantR8snn__mbt9get__time(_M0L1tS1622);
  #line 68 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
  _M0FP26RiantR8snn__mbt19stimulate__balanced(_M0L1xS1628, _M0L6_2atmpS4929, _M0L2dtS1629);
  moonbit_decref(_M0L1xS1628);
  joinlet_5327:;
  goto joinlet_5326;
  join_1625:;
  #line 69 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
  _M0FP26RiantR8snn__mbt22stimulate__current__if(_M0L1xS1626);
  moonbit_decref(_M0L1xS1626);
  joinlet_5326:;
  goto joinlet_5325;
  join_1623:;
  #line 70 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
  _M0FP26RiantR8snn__mbt25stimulate__current__array(_M0L1xS1624);
  moonbit_decref(_M0L1xS1624);
  joinlet_5325:;
  goto joinlet_5324;
  join_1619:;
  #line 71 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
  _M0L6_2atmpS4928 = _M0FP26RiantR8snn__mbt9get__time(_M0L1tS1622);
  #line 71 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
  _M0FP26RiantR8snn__mbt20stimulate__spiketime(_M0L1xS1620, _M0L6_2atmpS4928, _M0L1wS1621);
  moonbit_decref(_M0L1xS1620);
  joinlet_5324:;
  return 0;
}

struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0MP26RiantR8snn__mbt14SpikingSynapse6random(
  struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS1612,
  struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS1613,
  moonbit_string_t _M0L3symS1618,
  float _M0L2muS1614,
  float _M0L5sigmaS1615,
  float _M0L1pS1616,
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS1617
) {
  int32_t _M0L1nS4926;
  int32_t _M0L1nS4927;
  struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS1611;
  float* _M0L6_2atmpS4925;
  struct _M0TPB5ArrayGfE* _M0L6_2atmpS4916;
  float* _M0L6_2atmpS4924;
  struct _M0TPB5ArrayGfE* _M0L6_2atmpS4917;
  float* _M0L6_2atmpS4923;
  struct _M0TPB5ArrayGfE* _M0L6_2atmpS4918;
  int32_t* _M0L6_2atmpS4922;
  struct _M0TPB5ArrayGiE* _M0L6_2atmpS4919;
  float* _M0L6_2atmpS4921;
  struct _M0TPB5ArrayGfE* _M0L6_2atmpS4920;
  struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _block_5330;
  #line 131 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
  _M0L1nS4926 = _M0L3preS1612->$2;
  _M0L1nS4927 = _M0L4postS1613->$2;
  #line 140 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
  _M0L6matrixS1611
  = _M0MP26RiantR8snn__mbt15SparseMatrixCSR6random(_M0L1nS4926, _M0L1nS4927, _M0L2muS1614, _M0L5sigmaS1615, _M0L1pS1616, _M0L3rngS1617);
  _M0L6_2atmpS4925 = moonbit_empty_float_array;
  _M0L6_2atmpS4916
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_M0L6_2atmpS4916)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 21, 0);
  _M0L6_2atmpS4916->$0 = _M0L6_2atmpS4925;
  _M0L6_2atmpS4916->$1 = 0;
  _M0L6_2atmpS4924 = moonbit_empty_float_array;
  _M0L6_2atmpS4917
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_M0L6_2atmpS4917)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 21, 0);
  _M0L6_2atmpS4917->$0 = _M0L6_2atmpS4924;
  _M0L6_2atmpS4917->$1 = 0;
  _M0L6_2atmpS4923 = moonbit_empty_float_array;
  _M0L6_2atmpS4918
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_M0L6_2atmpS4918)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 21, 0);
  _M0L6_2atmpS4918->$0 = _M0L6_2atmpS4923;
  _M0L6_2atmpS4918->$1 = 0;
  _M0L6_2atmpS4922 = (int32_t*)moonbit_empty_int32_array;
  _M0L6_2atmpS4919
  = (struct _M0TPB5ArrayGiE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGiE));
  Moonbit_object_header(_M0L6_2atmpS4919)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 24, 0);
  _M0L6_2atmpS4919->$0 = _M0L6_2atmpS4922;
  _M0L6_2atmpS4919->$1 = 0;
  _M0L6_2atmpS4921 = moonbit_empty_float_array;
  _M0L6_2atmpS4920
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_M0L6_2atmpS4920)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 21, 0);
  _M0L6_2atmpS4920->$0 = _M0L6_2atmpS4921;
  _M0L6_2atmpS4920->$1 = 0;
  moonbit_incref(_M0L3preS1612);
  moonbit_incref(_M0L4postS1613);
  moonbit_incref(_M0L3symS1618);
  _block_5330
  = (struct _M0TP26RiantR8snn__mbt14SpikingSynapse*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt14SpikingSynapse));
  Moonbit_object_header(_block_5330)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 27, 0);
  _block_5330->$0 = _M0L3preS1612;
  _block_5330->$1 = _M0L4postS1613;
  _block_5330->$2 = _M0L3symS1618;
  _block_5330->$3 = (moonbit_string_t)moonbit_string_literal_0.data;
  _block_5330->$4 = _M0L6matrixS1611;
  _block_5330->$5 = _M0L6_2atmpS4916;
  _block_5330->$6 = _M0L6_2atmpS4917;
  _block_5330->$7 = _M0L6_2atmpS4918;
  _block_5330->$8 = _M0L6_2atmpS4919;
  _block_5330->$9 = _M0L6_2atmpS4920;
  return _block_5330;
}

int32_t _M0FP26RiantR8snn__mbt22istdp__potential__step(
  struct _M0TPB5ArrayGfE* _M0L1wS1607,
  struct _M0TPB5ArrayGbE* _M0L9pre__fireS1584,
  struct _M0TPB5ArrayGbE* _M0L10post__fireS1586,
  struct _M0TPB5ArrayGiE* _M0L6colptrS1603,
  struct _M0TPB5ArrayGiE* _M0L6rowptrS1597,
  struct _M0TPB5ArrayGfE* _M0L7v__postS1594,
  struct _M0TP26RiantR8snn__mbt23IstdpPotentialVariables* _M0L4varsS1590,
  struct _M0TP26RiantR8snn__mbt14IstdpPotential* _M0L5paramS1588,
  float _M0L6t__nowS1582,
  float _M0L2dtS1591
) {
  int32_t _M0L6n__preS1583;
  int32_t _M0L7n__postS1585;
  float _M0L6tau__yS4915;
  float _M0L11inv__tau__yS1587;
  struct _M0TPB8MutLocalGiE* _M0L1jS1589;
  struct _M0TPB8MutLocalGiE* _M0L1iS1593;
  #line 316 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
  #line 329 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
  _M0L6n__preS1583 = _M0MPC15array5Array6lengthGbE(_M0L9pre__fireS1584);
  #line 330 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
  _M0L7n__postS1585 = _M0MPC15array5Array6lengthGbE(_M0L10post__fireS1586);
  _M0L6tau__yS4915 = _M0L5paramS1588->$2;
  _M0L11inv__tau__yS1587 = 0x1p+0f / _M0L6tau__yS4915;
  _M0L1jS1589
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1jS1589)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1jS1589->$0 = 0;
  while (1) {
    int32_t _M0L3valS4832 = _M0L1jS1589->$0;
    if (_M0L3valS4832 < _M0L6n__preS1583) {
      struct _M0TPB5ArrayGfE* _M0L4tpreS4833 = _M0L4varsS1590->$0;
      int32_t _M0L3valS4834 = _M0L1jS1589->$0;
      struct _M0TPB5ArrayGfE* _M0L4tpreS4843 = _M0L4varsS1590->$0;
      int32_t _M0L3valS4844 = _M0L1jS1589->$0;
      float _M0L6_2atmpS4836;
      struct _M0TPB5ArrayGfE* _M0L4tpreS4841;
      int32_t _M0L3valS4842;
      float _M0L6_2atmpS4840;
      float _M0L6_2atmpS4839;
      float _M0L6_2atmpS4838;
      float _M0L6_2atmpS4837;
      float _M0L6_2atmpS4835;
      int32_t _M0L3valS4845;
      int32_t _M0L3valS4853;
      int32_t _M0L6_2atmpS4852;
      #line 337 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      _M0L6_2atmpS4836
      = _M0MPC15array5Array2atGfE(_M0L4tpreS4843, _M0L3valS4844);
      _M0L4tpreS4841 = _M0L4varsS1590->$0;
      _M0L3valS4842 = _M0L1jS1589->$0;
      #line 337 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      _M0L6_2atmpS4840
      = _M0MPC15array5Array2atGfE(_M0L4tpreS4841, _M0L3valS4842);
      _M0L6_2atmpS4839 = -_M0L6_2atmpS4840;
      _M0L6_2atmpS4838 = _M0L2dtS1591 * _M0L6_2atmpS4839;
      _M0L6_2atmpS4837 = _M0L6_2atmpS4838 * _M0L11inv__tau__yS1587;
      _M0L6_2atmpS4835 = _M0L6_2atmpS4836 + _M0L6_2atmpS4837;
      #line 337 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      _M0MPC15array5Array3setGfE(_M0L4tpreS4833, _M0L3valS4834, _M0L6_2atmpS4835);
      _M0L3valS4845 = _M0L1jS1589->$0;
      #line 338 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L9pre__fireS1584, _M0L3valS4845)) {
        struct _M0TPB5ArrayGfE* _M0L4tpreS4846 = _M0L4varsS1590->$0;
        int32_t _M0L3valS4847 = _M0L1jS1589->$0;
        struct _M0TPB5ArrayGfE* _M0L4tpreS4850 = _M0L4varsS1590->$0;
        int32_t _M0L3valS4851 = _M0L1jS1589->$0;
        float _M0L6_2atmpS4849;
        float _M0L6_2atmpS4848;
        #line 339 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
        _M0L6_2atmpS4849
        = _M0MPC15array5Array2atGfE(_M0L4tpreS4850, _M0L3valS4851);
        _M0L6_2atmpS4848 = _M0L6_2atmpS4849 + 0x1p+0f;
        #line 339 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L4tpreS4846, _M0L3valS4847, _M0L6_2atmpS4848);
      }
      _M0L3valS4853 = _M0L1jS1589->$0;
      _M0L6_2atmpS4852 = _M0L3valS4853 + 1;
      _M0L1jS1589->$0 = _M0L6_2atmpS4852;
      continue;
    }
    break;
  }
  _M0L1iS1593
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1iS1593)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1iS1593->$0 = 0;
  while (1) {
    int32_t _M0L3valS4854 = _M0L1iS1593->$0;
    if (_M0L3valS4854 < _M0L7n__postS1585) {
      struct _M0TPB5ArrayGfE* _M0L5tpostS4855 = _M0L4varsS1590->$1;
      int32_t _M0L3valS4856 = _M0L1iS1593->$0;
      struct _M0TPB5ArrayGfE* _M0L5tpostS4868 = _M0L4varsS1590->$1;
      int32_t _M0L3valS4869 = _M0L1iS1593->$0;
      float _M0L6_2atmpS4858;
      struct _M0TPB5ArrayGfE* _M0L5tpostS4866;
      int32_t _M0L3valS4867;
      float _M0L6_2atmpS4863;
      int32_t _M0L3valS4865;
      float _M0L6_2atmpS4864;
      float _M0L6_2atmpS4862;
      float _M0L6_2atmpS4861;
      float _M0L6_2atmpS4860;
      float _M0L6_2atmpS4859;
      float _M0L6_2atmpS4857;
      int32_t _M0L3valS4870;
      int32_t _M0L3valS4878;
      int32_t _M0L6_2atmpS4877;
      #line 346 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      _M0L6_2atmpS4858
      = _M0MPC15array5Array2atGfE(_M0L5tpostS4868, _M0L3valS4869);
      _M0L5tpostS4866 = _M0L4varsS1590->$1;
      _M0L3valS4867 = _M0L1iS1593->$0;
      #line 346 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      _M0L6_2atmpS4863
      = _M0MPC15array5Array2atGfE(_M0L5tpostS4866, _M0L3valS4867);
      _M0L3valS4865 = _M0L1iS1593->$0;
      #line 346 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      _M0L6_2atmpS4864
      = _M0MPC15array5Array2atGfE(_M0L7v__postS1594, _M0L3valS4865);
      _M0L6_2atmpS4862 = _M0L6_2atmpS4863 - _M0L6_2atmpS4864;
      _M0L6_2atmpS4861 = -_M0L6_2atmpS4862;
      _M0L6_2atmpS4860 = _M0L2dtS1591 * _M0L6_2atmpS4861;
      _M0L6_2atmpS4859 = _M0L6_2atmpS4860 * _M0L11inv__tau__yS1587;
      _M0L6_2atmpS4857 = _M0L6_2atmpS4858 + _M0L6_2atmpS4859;
      #line 346 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      _M0MPC15array5Array3setGfE(_M0L5tpostS4855, _M0L3valS4856, _M0L6_2atmpS4857);
      _M0L3valS4870 = _M0L1iS1593->$0;
      #line 347 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L10post__fireS1586, _M0L3valS4870)) {
        struct _M0TPB5ArrayGfE* _M0L5tpostS4871 = _M0L4varsS1590->$1;
        int32_t _M0L3valS4872 = _M0L1iS1593->$0;
        struct _M0TPB5ArrayGfE* _M0L5tpostS4875 = _M0L4varsS1590->$1;
        int32_t _M0L3valS4876 = _M0L1iS1593->$0;
        float _M0L6_2atmpS4874;
        float _M0L6_2atmpS4873;
        #line 348 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
        _M0L6_2atmpS4874
        = _M0MPC15array5Array2atGfE(_M0L5tpostS4875, _M0L3valS4876);
        _M0L6_2atmpS4873 = _M0L6_2atmpS4874 + 0x1p+0f;
        #line 348 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L5tpostS4871, _M0L3valS4872, _M0L6_2atmpS4873);
      }
      _M0L3valS4878 = _M0L1iS1593->$0;
      _M0L6_2atmpS4877 = _M0L3valS4878 + 1;
      _M0L1iS1593->$0 = _M0L6_2atmpS4877;
      continue;
    } else {
      moonbit_decref(_M0L1iS1593);
    }
    break;
  }
  _M0L1jS1589->$0 = 0;
  while (1) {
    int32_t _M0L3valS4879 = _M0L1jS1589->$0;
    if (_M0L3valS4879 < _M0L6n__preS1583) {
      int32_t _M0L3valS4914 = _M0L1jS1589->$0;
      int32_t _M0L5startS1596;
      int32_t _M0L3valS4913;
      int32_t _M0L6_2atmpS4912;
      int32_t _M0L3endS1598;
      int32_t _M0L3valS4911;
      int32_t _M0L10pre__firedS1599;
      struct _M0TPB5ArrayGfE* _M0L4tpreS4909;
      int32_t _M0L3valS4910;
      float _M0L7tpre__jS1600;
      struct _M0TPB8MutLocalGiE* _M0L1sS1601;
      int32_t _M0L3valS4908;
      int32_t _M0L6_2atmpS4907;
      #line 358 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      _M0L5startS1596
      = _M0MPC15array5Array2atGiE(_M0L6rowptrS1597, _M0L3valS4914);
      _M0L3valS4913 = _M0L1jS1589->$0;
      _M0L6_2atmpS4912 = _M0L3valS4913 + 1;
      #line 359 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      _M0L3endS1598
      = _M0MPC15array5Array2atGiE(_M0L6rowptrS1597, _M0L6_2atmpS4912);
      _M0L3valS4911 = _M0L1jS1589->$0;
      #line 360 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      _M0L10pre__firedS1599
      = _M0MPC15array5Array2atGbE(_M0L9pre__fireS1584, _M0L3valS4911);
      _M0L4tpreS4909 = _M0L4varsS1590->$0;
      _M0L3valS4910 = _M0L1jS1589->$0;
      #line 361 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      _M0L7tpre__jS1600
      = _M0MPC15array5Array2atGfE(_M0L4tpreS4909, _M0L3valS4910);
      _M0L1sS1601
      = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
      Moonbit_object_header(_M0L1sS1601)->meta
      = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
      _M0L1sS1601->$0 = _M0L5startS1596;
      while (1) {
        int32_t _M0L3valS4880 = _M0L1sS1601->$0;
        if (_M0L3valS4880 < _M0L3endS1598) {
          int32_t _M0L3valS4906 = _M0L1sS1601->$0;
          int32_t _M0L9post__idxS1602;
          int32_t _M0L11post__firedS1604;
          struct _M0TPB5ArrayGfE* _M0L5tpostS4905;
          float _M0L8tpost__iS1605;
          int32_t _M0L3valS4895;
          float _M0L6_2atmpS4893;
          float _M0L6w__minS4894;
          int32_t _M0L3valS4900;
          float _M0L6_2atmpS4898;
          float _M0L6w__maxS4899;
          int32_t _M0L3valS4904;
          int32_t _M0L6_2atmpS4903;
          #line 364 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
          _M0L9post__idxS1602
          = _M0MPC15array5Array2atGiE(_M0L6colptrS1603, _M0L3valS4906);
          #line 365 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
          _M0L11post__firedS1604
          = _M0MPC15array5Array2atGbE(_M0L10post__fireS1586, _M0L9post__idxS1602);
          _M0L5tpostS4905 = _M0L4varsS1590->$1;
          #line 366 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
          _M0L8tpost__iS1605
          = _M0MPC15array5Array2atGfE(_M0L5tpostS4905, _M0L9post__idxS1602);
          if (_M0L10pre__firedS1599) {
            float _M0L3etaS4885 = _M0L5paramS1588->$0;
            float _M0L2v0S4887 = _M0L5paramS1588->$1;
            float _M0L6_2atmpS4886 = _M0L8tpost__iS1605 - _M0L2v0S4887;
            float _M0L2dwS1606 = _M0L3etaS4885 * _M0L6_2atmpS4886;
            int32_t _M0L3valS4881 = _M0L1sS1601->$0;
            int32_t _M0L3valS4884 = _M0L1sS1601->$0;
            float _M0L6_2atmpS4883;
            float _M0L6_2atmpS4882;
            #line 369 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
            _M0L6_2atmpS4883
            = _M0MPC15array5Array2atGfE(_M0L1wS1607, _M0L3valS4884);
            _M0L6_2atmpS4882 = _M0L6_2atmpS4883 + _M0L2dwS1606;
            #line 369 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
            _M0MPC15array5Array3setGfE(_M0L1wS1607, _M0L3valS4881, _M0L6_2atmpS4882);
          }
          if (_M0L11post__firedS1604) {
            float _M0L3etaS4892 = _M0L5paramS1588->$0;
            float _M0L2dwS1608 = _M0L3etaS4892 * _M0L7tpre__jS1600;
            int32_t _M0L3valS4888 = _M0L1sS1601->$0;
            int32_t _M0L3valS4891 = _M0L1sS1601->$0;
            float _M0L6_2atmpS4890;
            float _M0L6_2atmpS4889;
            #line 373 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
            _M0L6_2atmpS4890
            = _M0MPC15array5Array2atGfE(_M0L1wS1607, _M0L3valS4891);
            _M0L6_2atmpS4889 = _M0L6_2atmpS4890 + _M0L2dwS1608;
            #line 373 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
            _M0MPC15array5Array3setGfE(_M0L1wS1607, _M0L3valS4888, _M0L6_2atmpS4889);
          }
          _M0L3valS4895 = _M0L1sS1601->$0;
          #line 376 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
          _M0L6_2atmpS4893
          = _M0MPC15array5Array2atGfE(_M0L1wS1607, _M0L3valS4895);
          _M0L6w__minS4894 = _M0L5paramS1588->$4;
          if (_M0L6_2atmpS4893 < _M0L6w__minS4894) {
            int32_t _M0L3valS4896 = _M0L1sS1601->$0;
            float _M0L6w__minS4897 = _M0L5paramS1588->$4;
            #line 376 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
            _M0MPC15array5Array3setGfE(_M0L1wS1607, _M0L3valS4896, _M0L6w__minS4897);
          }
          _M0L3valS4900 = _M0L1sS1601->$0;
          #line 377 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
          _M0L6_2atmpS4898
          = _M0MPC15array5Array2atGfE(_M0L1wS1607, _M0L3valS4900);
          _M0L6w__maxS4899 = _M0L5paramS1588->$3;
          if (_M0L6_2atmpS4898 > _M0L6w__maxS4899) {
            int32_t _M0L3valS4901 = _M0L1sS1601->$0;
            float _M0L6w__maxS4902 = _M0L5paramS1588->$3;
            #line 377 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
            _M0MPC15array5Array3setGfE(_M0L1wS1607, _M0L3valS4901, _M0L6w__maxS4902);
          }
          _M0L3valS4904 = _M0L1sS1601->$0;
          _M0L6_2atmpS4903 = _M0L3valS4904 + 1;
          _M0L1sS1601->$0 = _M0L6_2atmpS4903;
          continue;
        } else {
          moonbit_decref(_M0L1sS1601);
        }
        break;
      }
      _M0L3valS4908 = _M0L1jS1589->$0;
      _M0L6_2atmpS4907 = _M0L3valS4908 + 1;
      _M0L1jS1589->$0 = _M0L6_2atmpS4907;
      continue;
    } else {
      moonbit_decref(_M0L1jS1589);
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt17istdp__rate__step(
  struct _M0TPB5ArrayGfE* _M0L1wS1578,
  struct _M0TPB5ArrayGbE* _M0L9pre__fireS1556,
  struct _M0TPB5ArrayGbE* _M0L10post__fireS1558,
  struct _M0TPB5ArrayGiE* _M0L6colptrS1574,
  struct _M0TPB5ArrayGiE* _M0L6rowptrS1568,
  struct _M0TP26RiantR8snn__mbt18IstdpRateVariables* _M0L4varsS1562,
  struct _M0TP26RiantR8snn__mbt9IstdpRate* _M0L5paramS1560,
  float _M0L6t__nowS1554,
  float _M0L2dtS1563
) {
  int32_t _M0L6n__preS1555;
  int32_t _M0L7n__postS1557;
  float _M0L6tau__yS4831;
  float _M0L11inv__tau__yS1559;
  struct _M0TPB8MutLocalGiE* _M0L1jS1561;
  struct _M0TPB8MutLocalGiE* _M0L1iS1565;
  #line 141 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
  #line 153 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
  _M0L6n__preS1555 = _M0MPC15array5Array6lengthGbE(_M0L9pre__fireS1556);
  #line 154 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
  _M0L7n__postS1557 = _M0MPC15array5Array6lengthGbE(_M0L10post__fireS1558);
  _M0L6tau__yS4831 = _M0L5paramS1560->$2;
  _M0L11inv__tau__yS1559 = 0x1p+0f / _M0L6tau__yS4831;
  _M0L1jS1561
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1jS1561)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1jS1561->$0 = 0;
  while (1) {
    int32_t _M0L3valS4748 = _M0L1jS1561->$0;
    if (_M0L3valS4748 < _M0L6n__preS1555) {
      struct _M0TPB5ArrayGfE* _M0L4tpreS4749 = _M0L4varsS1562->$0;
      int32_t _M0L3valS4750 = _M0L1jS1561->$0;
      struct _M0TPB5ArrayGfE* _M0L4tpreS4759 = _M0L4varsS1562->$0;
      int32_t _M0L3valS4760 = _M0L1jS1561->$0;
      float _M0L6_2atmpS4752;
      struct _M0TPB5ArrayGfE* _M0L4tpreS4757;
      int32_t _M0L3valS4758;
      float _M0L6_2atmpS4756;
      float _M0L6_2atmpS4755;
      float _M0L6_2atmpS4754;
      float _M0L6_2atmpS4753;
      float _M0L6_2atmpS4751;
      int32_t _M0L3valS4761;
      int32_t _M0L3valS4769;
      int32_t _M0L6_2atmpS4768;
      #line 159 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      _M0L6_2atmpS4752
      = _M0MPC15array5Array2atGfE(_M0L4tpreS4759, _M0L3valS4760);
      _M0L4tpreS4757 = _M0L4varsS1562->$0;
      _M0L3valS4758 = _M0L1jS1561->$0;
      #line 159 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      _M0L6_2atmpS4756
      = _M0MPC15array5Array2atGfE(_M0L4tpreS4757, _M0L3valS4758);
      _M0L6_2atmpS4755 = -_M0L6_2atmpS4756;
      _M0L6_2atmpS4754 = _M0L2dtS1563 * _M0L6_2atmpS4755;
      _M0L6_2atmpS4753 = _M0L6_2atmpS4754 * _M0L11inv__tau__yS1559;
      _M0L6_2atmpS4751 = _M0L6_2atmpS4752 + _M0L6_2atmpS4753;
      #line 159 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      _M0MPC15array5Array3setGfE(_M0L4tpreS4749, _M0L3valS4750, _M0L6_2atmpS4751);
      _M0L3valS4761 = _M0L1jS1561->$0;
      #line 160 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L9pre__fireS1556, _M0L3valS4761)) {
        struct _M0TPB5ArrayGfE* _M0L4tpreS4762 = _M0L4varsS1562->$0;
        int32_t _M0L3valS4763 = _M0L1jS1561->$0;
        struct _M0TPB5ArrayGfE* _M0L4tpreS4766 = _M0L4varsS1562->$0;
        int32_t _M0L3valS4767 = _M0L1jS1561->$0;
        float _M0L6_2atmpS4765;
        float _M0L6_2atmpS4764;
        #line 161 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
        _M0L6_2atmpS4765
        = _M0MPC15array5Array2atGfE(_M0L4tpreS4766, _M0L3valS4767);
        _M0L6_2atmpS4764 = _M0L6_2atmpS4765 + 0x1p+0f;
        #line 161 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L4tpreS4762, _M0L3valS4763, _M0L6_2atmpS4764);
      }
      _M0L3valS4769 = _M0L1jS1561->$0;
      _M0L6_2atmpS4768 = _M0L3valS4769 + 1;
      _M0L1jS1561->$0 = _M0L6_2atmpS4768;
      continue;
    }
    break;
  }
  _M0L1iS1565
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1iS1565)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1iS1565->$0 = 0;
  while (1) {
    int32_t _M0L3valS4770 = _M0L1iS1565->$0;
    if (_M0L3valS4770 < _M0L7n__postS1557) {
      struct _M0TPB5ArrayGfE* _M0L5tpostS4771 = _M0L4varsS1562->$1;
      int32_t _M0L3valS4772 = _M0L1iS1565->$0;
      struct _M0TPB5ArrayGfE* _M0L5tpostS4781 = _M0L4varsS1562->$1;
      int32_t _M0L3valS4782 = _M0L1iS1565->$0;
      float _M0L6_2atmpS4774;
      struct _M0TPB5ArrayGfE* _M0L5tpostS4779;
      int32_t _M0L3valS4780;
      float _M0L6_2atmpS4778;
      float _M0L6_2atmpS4777;
      float _M0L6_2atmpS4776;
      float _M0L6_2atmpS4775;
      float _M0L6_2atmpS4773;
      int32_t _M0L3valS4783;
      int32_t _M0L3valS4791;
      int32_t _M0L6_2atmpS4790;
      #line 167 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      _M0L6_2atmpS4774
      = _M0MPC15array5Array2atGfE(_M0L5tpostS4781, _M0L3valS4782);
      _M0L5tpostS4779 = _M0L4varsS1562->$1;
      _M0L3valS4780 = _M0L1iS1565->$0;
      #line 167 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      _M0L6_2atmpS4778
      = _M0MPC15array5Array2atGfE(_M0L5tpostS4779, _M0L3valS4780);
      _M0L6_2atmpS4777 = -_M0L6_2atmpS4778;
      _M0L6_2atmpS4776 = _M0L2dtS1563 * _M0L6_2atmpS4777;
      _M0L6_2atmpS4775 = _M0L6_2atmpS4776 * _M0L11inv__tau__yS1559;
      _M0L6_2atmpS4773 = _M0L6_2atmpS4774 + _M0L6_2atmpS4775;
      #line 167 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      _M0MPC15array5Array3setGfE(_M0L5tpostS4771, _M0L3valS4772, _M0L6_2atmpS4773);
      _M0L3valS4783 = _M0L1iS1565->$0;
      #line 168 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L10post__fireS1558, _M0L3valS4783)) {
        struct _M0TPB5ArrayGfE* _M0L5tpostS4784 = _M0L4varsS1562->$1;
        int32_t _M0L3valS4785 = _M0L1iS1565->$0;
        struct _M0TPB5ArrayGfE* _M0L5tpostS4788 = _M0L4varsS1562->$1;
        int32_t _M0L3valS4789 = _M0L1iS1565->$0;
        float _M0L6_2atmpS4787;
        float _M0L6_2atmpS4786;
        #line 169 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
        _M0L6_2atmpS4787
        = _M0MPC15array5Array2atGfE(_M0L5tpostS4788, _M0L3valS4789);
        _M0L6_2atmpS4786 = _M0L6_2atmpS4787 + 0x1p+0f;
        #line 169 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L5tpostS4784, _M0L3valS4785, _M0L6_2atmpS4786);
      }
      _M0L3valS4791 = _M0L1iS1565->$0;
      _M0L6_2atmpS4790 = _M0L3valS4791 + 1;
      _M0L1iS1565->$0 = _M0L6_2atmpS4790;
      continue;
    } else {
      moonbit_decref(_M0L1iS1565);
    }
    break;
  }
  _M0L1jS1561->$0 = 0;
  while (1) {
    int32_t _M0L3valS4792 = _M0L1jS1561->$0;
    if (_M0L3valS4792 < _M0L6n__preS1555) {
      int32_t _M0L3valS4830 = _M0L1jS1561->$0;
      int32_t _M0L5startS1567;
      int32_t _M0L3valS4829;
      int32_t _M0L6_2atmpS4828;
      int32_t _M0L3endS1569;
      int32_t _M0L3valS4827;
      int32_t _M0L10pre__firedS1570;
      struct _M0TPB5ArrayGfE* _M0L4tpreS4825;
      int32_t _M0L3valS4826;
      float _M0L7tpre__jS1571;
      struct _M0TPB8MutLocalGiE* _M0L1sS1572;
      int32_t _M0L3valS4824;
      int32_t _M0L6_2atmpS4823;
      #line 179 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      _M0L5startS1567
      = _M0MPC15array5Array2atGiE(_M0L6rowptrS1568, _M0L3valS4830);
      _M0L3valS4829 = _M0L1jS1561->$0;
      _M0L6_2atmpS4828 = _M0L3valS4829 + 1;
      #line 180 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      _M0L3endS1569
      = _M0MPC15array5Array2atGiE(_M0L6rowptrS1568, _M0L6_2atmpS4828);
      _M0L3valS4827 = _M0L1jS1561->$0;
      #line 181 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      _M0L10pre__firedS1570
      = _M0MPC15array5Array2atGbE(_M0L9pre__fireS1556, _M0L3valS4827);
      _M0L4tpreS4825 = _M0L4varsS1562->$0;
      _M0L3valS4826 = _M0L1jS1561->$0;
      #line 182 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      _M0L7tpre__jS1571
      = _M0MPC15array5Array2atGfE(_M0L4tpreS4825, _M0L3valS4826);
      _M0L1sS1572
      = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
      Moonbit_object_header(_M0L1sS1572)->meta
      = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
      _M0L1sS1572->$0 = _M0L5startS1567;
      while (1) {
        int32_t _M0L3valS4793 = _M0L1sS1572->$0;
        if (_M0L3valS4793 < _M0L3endS1569) {
          int32_t _M0L3valS4822 = _M0L1sS1572->$0;
          int32_t _M0L9post__idxS1573;
          int32_t _M0L11post__firedS1575;
          struct _M0TPB5ArrayGfE* _M0L5tpostS4821;
          float _M0L8tpost__iS1576;
          int32_t _M0L3valS4811;
          float _M0L6_2atmpS4809;
          float _M0L6w__minS4810;
          int32_t _M0L3valS4816;
          float _M0L6_2atmpS4814;
          float _M0L6w__maxS4815;
          int32_t _M0L3valS4820;
          int32_t _M0L6_2atmpS4819;
          #line 185 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
          _M0L9post__idxS1573
          = _M0MPC15array5Array2atGiE(_M0L6colptrS1574, _M0L3valS4822);
          #line 186 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
          _M0L11post__firedS1575
          = _M0MPC15array5Array2atGbE(_M0L10post__fireS1558, _M0L9post__idxS1573);
          _M0L5tpostS4821 = _M0L4varsS1562->$1;
          #line 187 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
          _M0L8tpost__iS1576
          = _M0MPC15array5Array2atGfE(_M0L5tpostS4821, _M0L9post__idxS1573);
          if (_M0L10pre__firedS1570) {
            float _M0L3etaS4798 = _M0L5paramS1560->$0;
            float _M0L1rS4803 = _M0L5paramS1560->$1;
            float _M0L6_2atmpS4801 = 0x1p+1f * _M0L1rS4803;
            float _M0L6tau__yS4802 = _M0L5paramS1560->$2;
            float _M0L6_2atmpS4800 = _M0L6_2atmpS4801 * _M0L6tau__yS4802;
            float _M0L6_2atmpS4799 = _M0L8tpost__iS1576 - _M0L6_2atmpS4800;
            float _M0L2dwS1577 = _M0L3etaS4798 * _M0L6_2atmpS4799;
            int32_t _M0L3valS4794 = _M0L1sS1572->$0;
            int32_t _M0L3valS4797 = _M0L1sS1572->$0;
            float _M0L6_2atmpS4796;
            float _M0L6_2atmpS4795;
            #line 190 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
            _M0L6_2atmpS4796
            = _M0MPC15array5Array2atGfE(_M0L1wS1578, _M0L3valS4797);
            _M0L6_2atmpS4795 = _M0L6_2atmpS4796 + _M0L2dwS1577;
            #line 190 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
            _M0MPC15array5Array3setGfE(_M0L1wS1578, _M0L3valS4794, _M0L6_2atmpS4795);
          }
          if (_M0L11post__firedS1575) {
            float _M0L3etaS4808 = _M0L5paramS1560->$0;
            float _M0L2dwS1579 = _M0L3etaS4808 * _M0L7tpre__jS1571;
            int32_t _M0L3valS4804 = _M0L1sS1572->$0;
            int32_t _M0L3valS4807 = _M0L1sS1572->$0;
            float _M0L6_2atmpS4806;
            float _M0L6_2atmpS4805;
            #line 194 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
            _M0L6_2atmpS4806
            = _M0MPC15array5Array2atGfE(_M0L1wS1578, _M0L3valS4807);
            _M0L6_2atmpS4805 = _M0L6_2atmpS4806 + _M0L2dwS1579;
            #line 194 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
            _M0MPC15array5Array3setGfE(_M0L1wS1578, _M0L3valS4804, _M0L6_2atmpS4805);
          }
          _M0L3valS4811 = _M0L1sS1572->$0;
          #line 197 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
          _M0L6_2atmpS4809
          = _M0MPC15array5Array2atGfE(_M0L1wS1578, _M0L3valS4811);
          _M0L6w__minS4810 = _M0L5paramS1560->$4;
          if (_M0L6_2atmpS4809 < _M0L6w__minS4810) {
            int32_t _M0L3valS4812 = _M0L1sS1572->$0;
            float _M0L6w__minS4813 = _M0L5paramS1560->$4;
            #line 197 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
            _M0MPC15array5Array3setGfE(_M0L1wS1578, _M0L3valS4812, _M0L6w__minS4813);
          }
          _M0L3valS4816 = _M0L1sS1572->$0;
          #line 198 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
          _M0L6_2atmpS4814
          = _M0MPC15array5Array2atGfE(_M0L1wS1578, _M0L3valS4816);
          _M0L6w__maxS4815 = _M0L5paramS1560->$3;
          if (_M0L6_2atmpS4814 > _M0L6w__maxS4815) {
            int32_t _M0L3valS4817 = _M0L1sS1572->$0;
            float _M0L6w__maxS4818 = _M0L5paramS1560->$3;
            #line 198 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
            _M0MPC15array5Array3setGfE(_M0L1wS1578, _M0L3valS4817, _M0L6w__maxS4818);
          }
          _M0L3valS4820 = _M0L1sS1572->$0;
          _M0L6_2atmpS4819 = _M0L3valS4820 + 1;
          _M0L1sS1572->$0 = _M0L6_2atmpS4819;
          continue;
        } else {
          moonbit_decref(_M0L1sS1572);
        }
        break;
      }
      _M0L3valS4824 = _M0L1jS1561->$0;
      _M0L6_2atmpS4823 = _M0L3valS4824 + 1;
      _M0L1jS1561->$0 = _M0L6_2atmpS4823;
      continue;
    } else {
      moonbit_decref(_M0L1jS1561);
    }
    break;
  }
  return 0;
}

struct _M0TP26RiantR8snn__mbt11IFParameter* _M0MP26RiantR8snn__mbt11IFParameter3new(
  
) {
  float _M0L1cS1552;
  float _M0L2glS1553;
  struct _M0TP26RiantR8snn__mbt11IFParameter* _block_5339;
  #line 39 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L1cS1552 = -0x1p+0f;
  _M0L2glS1553 = -0x1p+0f;
  _block_5339
  = (struct _M0TP26RiantR8snn__mbt11IFParameter*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt11IFParameter));
  Moonbit_object_header(_block_5339)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_5339->$0 = _M0L1cS1552;
  _block_5339->$1 = _M0L2glS1553;
  _block_5339->$2 = 0x1.ep+3f;
  _block_5339->$3 = -0x1.9p+5f;
  _block_5339->$4 = -0x1.ep+5f;
  _block_5339->$5 = -0x1.18p+6f;
  _block_5339->$6 = 0x1.eb851eb851eb8p-5f;
  _block_5339->$7 = 0x1p+1f;
  _block_5339->$8 = 0x0p+0f;
  _block_5339->$9 = 0x0p+0f;
  _block_5339->$10 = 0x0p+0f;
  return _block_5339;
}

struct _M0TP26RiantR8snn__mbt2IF* _M0MP26RiantR8snn__mbt2IF3new(
  int32_t _M0L1nS1526,
  struct _M0TP26RiantR8snn__mbt11IFParameter* _M0L5paramS1528,
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS1531
) {
  struct _M0TPB5ArrayGfE* _M0L1vS1525;
  float _M0L2vtS4746;
  float _M0L2vrS4747;
  float _M0L6spreadS1527;
  int32_t _M0L7_2abindS1529;
  int32_t _M0L1kS1530;
  struct _M0TPB5ArrayGfE* _M0L1wS1533;
  struct _M0TPB5ArrayGbE* _M0L4fireS1534;
  struct _M0TPB5ArrayGiE* _M0L4tabsS1535;
  struct _M0TPB5ArrayGfE* _M0L1iS1536;
  struct _M0TPB5ArrayGfE* _M0L9syn__currS1537;
  struct _M0TPB5ArrayGfE* _M0L2geS1538;
  struct _M0TPB5ArrayGfE* _M0L2giS1539;
  struct _M0TPB5ArrayGfE* _M0L2heS1540;
  struct _M0TPB5ArrayGfE* _M0L2hiS1541;
  struct _M0TPB5ArrayGfE* _M0L3gluS1542;
  struct _M0TPB5ArrayGfE* _M0L4gabaS1543;
  struct _M0TPB5ArrayGfE* _M0L7gsyn__eS1544;
  struct _M0TPB5ArrayGfE* _M0L7gsyn__iS1545;
  float _M0L4e__eS1546;
  float _M0L4e__iS1547;
  float _M0L3treS1548;
  float _M0L3tdeS1549;
  float _M0L3triS1550;
  float _M0L3tdiS1551;
  struct _M0TP26RiantR8snn__mbt9PostSpike* _M0L6_2atmpS4745;
  struct _M0TP26RiantR8snn__mbt2IF* _block_5341;
  #line 113 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  #line 114 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L1vS1525 = _M0MPC15array5Array4makeGfE(_M0L1nS1526, 0x0p+0f);
  _M0L2vtS4746 = _M0L5paramS1528->$3;
  _M0L2vrS4747 = _M0L5paramS1528->$4;
  _M0L6spreadS1527 = _M0L2vtS4746 - _M0L2vrS4747;
  _M0L7_2abindS1529 = 0;
  _M0L1kS1530 = _M0L7_2abindS1529;
  while (1) {
    if (_M0L1kS1530 < _M0L1nS1526) {
      float _M0L2vrS4741 = _M0L5paramS1528->$4;
      float _M0L6_2atmpS4743;
      float _M0L6_2atmpS4742;
      float _M0L6_2atmpS4740;
      int32_t _M0L6_2atmpS4744;
      #line 117 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS4743 = _M0FP26RiantR8snn__mbt9next__f32(_M0L3rngS1531);
      _M0L6_2atmpS4742 = _M0L6_2atmpS4743 * _M0L6spreadS1527;
      _M0L6_2atmpS4740 = _M0L2vrS4741 + _M0L6_2atmpS4742;
      #line 117 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS1525, _M0L1kS1530, _M0L6_2atmpS4740);
      _M0L6_2atmpS4744 = _M0L1kS1530 + 1;
      _M0L1kS1530 = _M0L6_2atmpS4744;
      continue;
    }
    break;
  }
  #line 119 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L1wS1533 = _M0MPC15array5Array4makeGfE(_M0L1nS1526, 0x0p+0f);
  #line 120 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L4fireS1534 = _M0MPC15array5Array4makeGbE(_M0L1nS1526, 0);
  #line 121 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L4tabsS1535 = _M0MPC15array5Array4makeGiE(_M0L1nS1526, 0);
  #line 122 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L1iS1536 = _M0MPC15array5Array4makeGfE(_M0L1nS1526, 0x0p+0f);
  #line 123 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L9syn__currS1537 = _M0MPC15array5Array4makeGfE(_M0L1nS1526, 0x0p+0f);
  #line 124 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L2geS1538 = _M0MPC15array5Array4makeGfE(_M0L1nS1526, 0x0p+0f);
  #line 125 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L2giS1539 = _M0MPC15array5Array4makeGfE(_M0L1nS1526, 0x0p+0f);
  #line 126 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L2heS1540 = _M0MPC15array5Array4makeGfE(_M0L1nS1526, 0x0p+0f);
  #line 127 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L2hiS1541 = _M0MPC15array5Array4makeGfE(_M0L1nS1526, 0x0p+0f);
  #line 128 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L3gluS1542 = _M0MPC15array5Array4makeGfE(_M0L1nS1526, 0x0p+0f);
  #line 129 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L4gabaS1543 = _M0MPC15array5Array4makeGfE(_M0L1nS1526, 0x0p+0f);
  #line 130 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L7gsyn__eS1544 = _M0MPC15array5Array4makeGfE(_M0L1nS1526, 0x1p+0f);
  #line 131 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L7gsyn__iS1545 = _M0MPC15array5Array4makeGfE(_M0L1nS1526, 0x1p+0f);
  _M0L4e__eS1546 = 0x0p+0f;
  _M0L4e__iS1547 = -0x1.2cp+6f;
  _M0L3treS1548 = 0x1p+0f;
  _M0L3tdeS1549 = 0x1.8p+2f;
  _M0L3triS1550 = 0x1p-1f;
  _M0L3tdiS1551 = 0x1p+1f;
  #line 138 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L6_2atmpS4745 = _M0MP26RiantR8snn__mbt9PostSpike3new();
  moonbit_incref(_M0L5paramS1528);
  _block_5341
  = (struct _M0TP26RiantR8snn__mbt2IF*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt2IF));
  Moonbit_object_header(_block_5341)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 39, 0);
  _block_5341->$0 = _M0L5paramS1528;
  _block_5341->$1 = _M0L6_2atmpS4745;
  _block_5341->$2 = _M0L1nS1526;
  _block_5341->$3 = _M0L1vS1525;
  _block_5341->$4 = _M0L1wS1533;
  _block_5341->$5 = _M0L4fireS1534;
  _block_5341->$6 = _M0L4tabsS1535;
  _block_5341->$7 = _M0L1iS1536;
  _block_5341->$8 = _M0L9syn__currS1537;
  _block_5341->$9 = _M0L2geS1538;
  _block_5341->$10 = _M0L2giS1539;
  _block_5341->$11 = _M0L2heS1540;
  _block_5341->$12 = _M0L2hiS1541;
  _block_5341->$13 = _M0L3gluS1542;
  _block_5341->$14 = _M0L4gabaS1543;
  _block_5341->$15 = _M0L7gsyn__eS1544;
  _block_5341->$16 = _M0L7gsyn__iS1545;
  _block_5341->$17 = _M0L4e__eS1546;
  _block_5341->$18 = _M0L4e__iS1547;
  _block_5341->$19 = _M0L3treS1548;
  _block_5341->$20 = _M0L3tdeS1549;
  _block_5341->$21 = _M0L3triS1550;
  _block_5341->$22 = _M0L3tdiS1551;
  return _block_5341;
}

struct _M0TP26RiantR8snn__mbt9PostSpike* _M0MP26RiantR8snn__mbt9PostSpike3new(
  
) {
  struct _M0TP26RiantR8snn__mbt9PostSpike* _block_5342;
  #line 75 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _block_5342
  = (struct _M0TP26RiantR8snn__mbt9PostSpike*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt9PostSpike));
  Moonbit_object_header(_block_5342)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_5342->$0 = 0x1p+1f;
  return _block_5342;
}

int32_t _M0FP26RiantR8snn__mbt14integrate__any(
  void* _M0L1pS1506,
  float _M0L2dtS1489
) {
  struct _M0TP26RiantR8snn__mbt6HetRec* _M0L1xS1488;
  struct _M0TP26RiantR8snn__mbt11WilsonCowan* _M0L1xS1491;
  struct _M0TP26RiantR8snn__mbt7Poisson* _M0L1xS1493;
  struct _M0TP26RiantR8snn__mbt11MorrisLecar* _M0L1xS1495;
  struct _M0TP26RiantR8snn__mbt2HH* _M0L1xS1497;
  struct _M0TP26RiantR8snn__mbt2IZ* _M0L1xS1499;
  struct _M0TP26RiantR8snn__mbt10AdExSinExp* _M0L1xS1501;
  struct _M0TP26RiantR8snn__mbt4AdEx* _M0L1xS1503;
  struct _M0TP26RiantR8snn__mbt2IF* _M0L1xS1505;
  #line 30 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\population.mbt"
  switch (Moonbit_object_tag(_M0L1pS1506)) {
    case 0: {
      struct _M0DTP26RiantR8snn__mbt6AnyPop4IF__* _M0L7_2aIF__S1507 =
        (struct _M0DTP26RiantR8snn__mbt6AnyPop4IF__*)_M0L1pS1506;
      struct _M0TP26RiantR8snn__mbt2IF* _M0L4_2axS1508 =
        _M0L7_2aIF__S1507->$0;
      moonbit_incref(_M0L4_2axS1508);
      _M0L1xS1505 = _M0L4_2axS1508;
      goto join_1504;
      break;
    }
    
    case 1: {
      struct _M0DTP26RiantR8snn__mbt6AnyPop6AdEx__* _M0L9_2aAdEx__S1509 =
        (struct _M0DTP26RiantR8snn__mbt6AnyPop6AdEx__*)_M0L1pS1506;
      struct _M0TP26RiantR8snn__mbt4AdEx* _M0L4_2axS1510 =
        _M0L9_2aAdEx__S1509->$0;
      moonbit_incref(_M0L4_2axS1510);
      _M0L1xS1503 = _M0L4_2axS1510;
      goto join_1502;
      break;
    }
    
    case 2: {
      struct _M0DTP26RiantR8snn__mbt6AnyPop12AdExSinExp__* _M0L15_2aAdExSinExp__S1511 =
        (struct _M0DTP26RiantR8snn__mbt6AnyPop12AdExSinExp__*)_M0L1pS1506;
      struct _M0TP26RiantR8snn__mbt10AdExSinExp* _M0L4_2axS1512 =
        _M0L15_2aAdExSinExp__S1511->$0;
      moonbit_incref(_M0L4_2axS1512);
      _M0L1xS1501 = _M0L4_2axS1512;
      goto join_1500;
      break;
    }
    
    case 3: {
      struct _M0DTP26RiantR8snn__mbt6AnyPop4IZ__* _M0L7_2aIZ__S1513 =
        (struct _M0DTP26RiantR8snn__mbt6AnyPop4IZ__*)_M0L1pS1506;
      struct _M0TP26RiantR8snn__mbt2IZ* _M0L4_2axS1514 =
        _M0L7_2aIZ__S1513->$0;
      moonbit_incref(_M0L4_2axS1514);
      _M0L1xS1499 = _M0L4_2axS1514;
      goto join_1498;
      break;
    }
    
    case 4: {
      struct _M0DTP26RiantR8snn__mbt6AnyPop4HH__* _M0L7_2aHH__S1515 =
        (struct _M0DTP26RiantR8snn__mbt6AnyPop4HH__*)_M0L1pS1506;
      struct _M0TP26RiantR8snn__mbt2HH* _M0L4_2axS1516 =
        _M0L7_2aHH__S1515->$0;
      moonbit_incref(_M0L4_2axS1516);
      _M0L1xS1497 = _M0L4_2axS1516;
      goto join_1496;
      break;
    }
    
    case 5: {
      struct _M0DTP26RiantR8snn__mbt6AnyPop4ML__* _M0L7_2aML__S1517 =
        (struct _M0DTP26RiantR8snn__mbt6AnyPop4ML__*)_M0L1pS1506;
      struct _M0TP26RiantR8snn__mbt11MorrisLecar* _M0L4_2axS1518 =
        _M0L7_2aML__S1517->$0;
      moonbit_incref(_M0L4_2axS1518);
      _M0L1xS1495 = _M0L4_2axS1518;
      goto join_1494;
      break;
    }
    
    case 6: {
      struct _M0DTP26RiantR8snn__mbt6AnyPop9Poisson__* _M0L12_2aPoisson__S1519 =
        (struct _M0DTP26RiantR8snn__mbt6AnyPop9Poisson__*)_M0L1pS1506;
      struct _M0TP26RiantR8snn__mbt7Poisson* _M0L4_2axS1520 =
        _M0L12_2aPoisson__S1519->$0;
      moonbit_incref(_M0L4_2axS1520);
      _M0L1xS1493 = _M0L4_2axS1520;
      goto join_1492;
      break;
    }
    
    case 7: {
      struct _M0DTP26RiantR8snn__mbt6AnyPop4WC__* _M0L7_2aWC__S1521 =
        (struct _M0DTP26RiantR8snn__mbt6AnyPop4WC__*)_M0L1pS1506;
      struct _M0TP26RiantR8snn__mbt11WilsonCowan* _M0L4_2axS1522 =
        _M0L7_2aWC__S1521->$0;
      moonbit_incref(_M0L4_2axS1522);
      _M0L1xS1491 = _M0L4_2axS1522;
      goto join_1490;
      break;
    }
    default: {
      struct _M0DTP26RiantR8snn__mbt6AnyPop8HetRec__* _M0L11_2aHetRec__S1523 =
        (struct _M0DTP26RiantR8snn__mbt6AnyPop8HetRec__*)_M0L1pS1506;
      struct _M0TP26RiantR8snn__mbt6HetRec* _M0L4_2axS1524 =
        _M0L11_2aHetRec__S1523->$0;
      moonbit_incref(_M0L4_2axS1524);
      _M0L1xS1488 = _M0L4_2axS1524;
      goto join_1487;
      break;
    }
  }
  goto joinlet_5351;
  join_1504:;
  #line 33 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\population.mbt"
  _M0FP26RiantR8snn__mbt14step__synapses(_M0L1xS1505, _M0L2dtS1489);
  #line 34 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\population.mbt"
  _M0FP26RiantR8snn__mbt17synaptic__current(_M0L1xS1505);
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\population.mbt"
  _M0FP26RiantR8snn__mbt12step__neuron(_M0L1xS1505, _M0L2dtS1489);
  moonbit_decref(_M0L1xS1505);
  joinlet_5351:;
  goto joinlet_5350;
  join_1502:;
  #line 38 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\population.mbt"
  _M0FP26RiantR8snn__mbt20adex__step__synapses(_M0L1xS1503, _M0L2dtS1489);
  #line 39 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\population.mbt"
  _M0FP26RiantR8snn__mbt23adex__synaptic__current(_M0L1xS1503);
  #line 40 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\population.mbt"
  _M0FP26RiantR8snn__mbt10step__adex(_M0L1xS1503, _M0L2dtS1489);
  moonbit_decref(_M0L1xS1503);
  joinlet_5350:;
  goto joinlet_5349;
  join_1500:;
  #line 43 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\population.mbt"
  _M0FP26RiantR8snn__mbt28adex__sinexp__step__synapses(_M0L1xS1501, _M0L2dtS1489);
  #line 44 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\population.mbt"
  _M0FP26RiantR8snn__mbt31adex__sinexp__synaptic__current(_M0L1xS1501);
  #line 45 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\population.mbt"
  _M0FP26RiantR8snn__mbt18step__adex__sinexp(_M0L1xS1501, _M0L2dtS1489);
  moonbit_decref(_M0L1xS1501);
  joinlet_5349:;
  goto joinlet_5348;
  join_1498:;
  #line 47 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\population.mbt"
  _M0FP26RiantR8snn__mbt8step__iz(_M0L1xS1499, _M0L2dtS1489);
  moonbit_decref(_M0L1xS1499);
  joinlet_5348:;
  goto joinlet_5347;
  join_1496:;
  #line 48 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\population.mbt"
  _M0FP26RiantR8snn__mbt8step__hh(_M0L1xS1497, _M0L2dtS1489);
  moonbit_decref(_M0L1xS1497);
  joinlet_5347:;
  goto joinlet_5346;
  join_1494:;
  #line 49 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\population.mbt"
  _M0FP26RiantR8snn__mbt8step__ml(_M0L1xS1495, _M0L2dtS1489);
  moonbit_decref(_M0L1xS1495);
  joinlet_5346:;
  goto joinlet_5345;
  join_1492:;
  #line 50 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\population.mbt"
  _M0FP26RiantR8snn__mbt13step__poisson(_M0L1xS1493, _M0L2dtS1489);
  moonbit_decref(_M0L1xS1493);
  joinlet_5345:;
  goto joinlet_5344;
  join_1490:;
  #line 51 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\population.mbt"
  _M0FP26RiantR8snn__mbt8step__wc(_M0L1xS1491, _M0L2dtS1489);
  moonbit_decref(_M0L1xS1491);
  joinlet_5344:;
  goto joinlet_5343;
  join_1487:;
  #line 52 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\population.mbt"
  _M0FP26RiantR8snn__mbt12step__hetrec(_M0L1xS1488, _M0L2dtS1489);
  moonbit_decref(_M0L1xS1488);
  joinlet_5343:;
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt8step__wc(
  struct _M0TP26RiantR8snn__mbt11WilsonCowan* _M0L1pS1482,
  float _M0L2dtS1485
) {
  int32_t _M0L1nS1481;
  int32_t _M0L7_2abindS1483;
  int32_t _M0L1kS1484;
  #line 62 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_wc.mbt"
  _M0L1nS1481 = _M0L1pS1482->$1;
  _M0L7_2abindS1483 = 0;
  _M0L1kS1484 = _M0L7_2abindS1483;
  while (1) {
    if (_M0L1kS1484 < _M0L1nS1481) {
      struct _M0TPB5ArrayGfE* _M0L1xS4720 = _M0L1pS1482->$2;
      struct _M0TPB5ArrayGfE* _M0L1xS4733 = _M0L1pS1482->$2;
      float _M0L6_2atmpS4722;
      struct _M0TPB5ArrayGfE* _M0L1xS4732;
      float _M0L6_2atmpS4731;
      float _M0L6_2atmpS4728;
      struct _M0TPB5ArrayGfE* _M0L1gS4730;
      float _M0L6_2atmpS4729;
      float _M0L6_2atmpS4725;
      struct _M0TPB5ArrayGfE* _M0L1iS4727;
      float _M0L6_2atmpS4726;
      float _M0L6_2atmpS4724;
      float _M0L6_2atmpS4723;
      float _M0L6_2atmpS4721;
      struct _M0TPB5ArrayGfE* _M0L1rS4734;
      struct _M0TPB5ArrayGfE* _M0L1xS4737;
      float _M0L6_2atmpS4736;
      float _M0L6_2atmpS4735;
      struct _M0TPB5ArrayGfE* _M0L1gS4738;
      int32_t _M0L6_2atmpS4739;
      #line 65 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_wc.mbt"
      _M0L6_2atmpS4722 = _M0MPC15array5Array2atGfE(_M0L1xS4733, _M0L1kS1484);
      _M0L1xS4732 = _M0L1pS1482->$2;
      #line 65 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_wc.mbt"
      _M0L6_2atmpS4731 = _M0MPC15array5Array2atGfE(_M0L1xS4732, _M0L1kS1484);
      _M0L6_2atmpS4728 = -_M0L6_2atmpS4731;
      _M0L1gS4730 = _M0L1pS1482->$4;
      #line 65 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_wc.mbt"
      _M0L6_2atmpS4729 = _M0MPC15array5Array2atGfE(_M0L1gS4730, _M0L1kS1484);
      _M0L6_2atmpS4725 = _M0L6_2atmpS4728 + _M0L6_2atmpS4729;
      _M0L1iS4727 = _M0L1pS1482->$5;
      #line 65 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_wc.mbt"
      _M0L6_2atmpS4726 = _M0MPC15array5Array2atGfE(_M0L1iS4727, _M0L1kS1484);
      _M0L6_2atmpS4724 = _M0L6_2atmpS4725 + _M0L6_2atmpS4726;
      _M0L6_2atmpS4723 = _M0L2dtS1485 * _M0L6_2atmpS4724;
      _M0L6_2atmpS4721 = _M0L6_2atmpS4722 + _M0L6_2atmpS4723;
      #line 65 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_wc.mbt"
      _M0MPC15array5Array3setGfE(_M0L1xS4720, _M0L1kS1484, _M0L6_2atmpS4721);
      _M0L1rS4734 = _M0L1pS1482->$3;
      _M0L1xS4737 = _M0L1pS1482->$2;
      #line 66 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_wc.mbt"
      _M0L6_2atmpS4736 = _M0MPC15array5Array2atGfE(_M0L1xS4737, _M0L1kS1484);
      #line 66 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_wc.mbt"
      _M0L6_2atmpS4735 = _M0FP26RiantR8snn__mbt5tanhf(_M0L6_2atmpS4736);
      #line 66 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_wc.mbt"
      _M0MPC15array5Array3setGfE(_M0L1rS4734, _M0L1kS1484, _M0L6_2atmpS4735);
      _M0L1gS4738 = _M0L1pS1482->$4;
      #line 67 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_wc.mbt"
      _M0MPC15array5Array3setGfE(_M0L1gS4738, _M0L1kS1484, 0x0p+0f);
      _M0L6_2atmpS4739 = _M0L1kS1484 + 1;
      _M0L1kS1484 = _M0L6_2atmpS4739;
      continue;
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt13step__poisson(
  struct _M0TP26RiantR8snn__mbt7Poisson* _M0L1pS1474,
  float _M0L2dtS1476
) {
  int32_t _M0L1nS1473;
  struct _M0TP26RiantR8snn__mbt20PoissonHomoParameter* _M0L5paramS4719;
  float _M0L4rateS4718;
  float _M0L8rate__dtS1475;
  int32_t _M0L7_2abindS1477;
  int32_t _M0L1iS1478;
  #line 48 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_poisson.mbt"
  _M0L1nS1473 = _M0L1pS1474->$1;
  _M0L5paramS4719 = _M0L1pS1474->$0;
  _M0L4rateS4718 = _M0L5paramS4719->$0;
  _M0L8rate__dtS1475 = _M0L4rateS4718 * _M0L2dtS1476;
  _M0L7_2abindS1477 = 0;
  _M0L1iS1478 = _M0L7_2abindS1477;
  while (1) {
    if (_M0L1iS1478 < _M0L1nS1473) {
      struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS4716 = _M0L1pS1474->$4;
      float _M0L1uS1479;
      struct _M0TPB5ArrayGfE* _M0L9randcacheS4713;
      struct _M0TPB5ArrayGbE* _M0L4fireS4714;
      int32_t _M0L6_2atmpS4715;
      int32_t _M0L6_2atmpS4717;
      #line 52 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_poisson.mbt"
      _M0L1uS1479 = _M0FP26RiantR8snn__mbt9next__f32(_M0L3rngS4716);
      _M0L9randcacheS4713 = _M0L1pS1474->$3;
      #line 53 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_poisson.mbt"
      _M0MPC15array5Array3setGfE(_M0L9randcacheS4713, _M0L1iS1478, _M0L1uS1479);
      _M0L4fireS4714 = _M0L1pS1474->$2;
      _M0L6_2atmpS4715 = _M0L1uS1479 < _M0L8rate__dtS1475;
      #line 54 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_poisson.mbt"
      _M0MPC15array5Array3setGbE(_M0L4fireS4714, _M0L1iS1478, _M0L6_2atmpS4715);
      _M0L6_2atmpS4717 = _M0L1iS1478 + 1;
      _M0L1iS1478 = _M0L6_2atmpS4717;
      continue;
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt8step__ml(
  struct _M0TP26RiantR8snn__mbt11MorrisLecar* _M0L1pS1433,
  float _M0L2dtS1462
) {
  int32_t _M0L1nS1432;
  struct _M0TP26RiantR8snn__mbt20MorrisLecarParameter* _M0L3p__S1434;
  float _M0L2cmS1435;
  float _M0L2elS1436;
  float _M0L2ekS1437;
  float _M0L3ecaS1438;
  float _M0L2glS1439;
  float _M0L2gkS1440;
  float _M0L3gcaS1441;
  float _M0L6tau__eS1442;
  float _M0L6tau__iS1443;
  float _M0L2v1S1444;
  float _M0L2v2S1445;
  float _M0L2v3S1446;
  float _M0L2v4S1447;
  float _M0L3phiS1448;
  float _M0L4e__eS1449;
  float _M0L4e__iS1450;
  int32_t _M0L7_2abindS1451;
  int32_t _M0L1iS1452;
  int32_t _M0L7_2abindS1464;
  int32_t _M0L1iS1465;
  int32_t _M0L7_2abindS1467;
  int32_t _M0L1iS1468;
  int32_t _M0L7_2abindS1470;
  int32_t _M0L1iS1471;
  #line 86 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ml.mbt"
  _M0L1nS1432 = _M0L1pS1433->$1;
  _M0L3p__S1434 = _M0L1pS1433->$0;
  _M0L2cmS1435 = _M0L3p__S1434->$0;
  _M0L2elS1436 = _M0L3p__S1434->$1;
  _M0L2ekS1437 = _M0L3p__S1434->$2;
  _M0L3ecaS1438 = _M0L3p__S1434->$3;
  _M0L2glS1439 = _M0L3p__S1434->$4;
  _M0L2gkS1440 = _M0L3p__S1434->$5;
  _M0L3gcaS1441 = _M0L3p__S1434->$6;
  _M0L6tau__eS1442 = _M0L3p__S1434->$7;
  _M0L6tau__iS1443 = _M0L3p__S1434->$8;
  _M0L2v1S1444 = _M0L3p__S1434->$9;
  _M0L2v2S1445 = _M0L3p__S1434->$10;
  _M0L2v3S1446 = _M0L3p__S1434->$11;
  _M0L2v4S1447 = _M0L3p__S1434->$12;
  _M0L3phiS1448 = _M0L3p__S1434->$13;
  _M0L4e__eS1449 = _M0L3p__S1434->$14;
  _M0L4e__iS1450 = _M0L3p__S1434->$15;
  _M0L7_2abindS1451 = 0;
  _M0L1iS1452 = _M0L7_2abindS1451;
  while (1) {
    if (_M0L1iS1452 < _M0L1nS1432) {
      struct _M0TPB5ArrayGfE* _M0L1vS4667 = _M0L1pS1433->$2;
      float _M0L1vS1453;
      struct _M0TPB5ArrayGfE* _M0L1wS4666;
      float _M0L1wS1454;
      float _M0L6_2atmpS4665;
      float _M0L6_2atmpS4664;
      float _M0L6_2atmpS4663;
      float _M0L6_2atmpS4662;
      float _M0L5m__ssS1455;
      struct _M0TPB5ArrayGfE* _M0L1iS4661;
      float _M0L6_2atmpS4658;
      float _M0L6_2atmpS4660;
      float _M0L6_2atmpS4659;
      float _M0L6_2atmpS4654;
      float _M0L6_2atmpS4657;
      float _M0L6_2atmpS4656;
      float _M0L6_2atmpS4655;
      float _M0L6_2atmpS4650;
      float _M0L6_2atmpS4653;
      float _M0L6_2atmpS4652;
      float _M0L6_2atmpS4651;
      float _M0L2dvS1456;
      float _M0L6_2atmpS4649;
      float _M0L6_2atmpS4648;
      float _M0L6_2atmpS4647;
      float _M0L6_2atmpS4646;
      float _M0L5n__ssS1457;
      float _M0L6_2atmpS4644;
      float _M0L6_2atmpS4645;
      float _M0L9cosh__argS1458;
      float _M0L6_2atmpS4641;
      float _M0L6_2atmpS4643;
      float _M0L6_2atmpS4642;
      float _M0L6_2atmpS4640;
      float _M0L9cosh__valS1459;
      float _M0L6_2atmpS4638;
      float _M0L3tauS1460;
      float _M0L6_2atmpS4637;
      float _M0L2dwS1461;
      struct _M0TPB5ArrayGfE* _M0L1vS4630;
      float _M0L6_2atmpS4633;
      float _M0L6_2atmpS4632;
      float _M0L6_2atmpS4631;
      struct _M0TPB5ArrayGfE* _M0L1wS4634;
      float _M0L6_2atmpS4636;
      float _M0L6_2atmpS4635;
      int32_t _M0L6_2atmpS4668;
      #line 106 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ml.mbt"
      _M0L1vS1453 = _M0MPC15array5Array2atGfE(_M0L1vS4667, _M0L1iS1452);
      _M0L1wS4666 = _M0L1pS1433->$3;
      #line 107 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ml.mbt"
      _M0L1wS1454 = _M0MPC15array5Array2atGfE(_M0L1wS4666, _M0L1iS1452);
      _M0L6_2atmpS4665 = _M0L1vS1453 - _M0L2v1S1444;
      _M0L6_2atmpS4664 = _M0L6_2atmpS4665 / _M0L2v2S1445;
      #line 109 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ml.mbt"
      _M0L6_2atmpS4663 = _M0FP26RiantR8snn__mbt5tanhf(_M0L6_2atmpS4664);
      _M0L6_2atmpS4662 = 0x1p+0f + _M0L6_2atmpS4663;
      _M0L5m__ssS1455 = 0x1p-1f * _M0L6_2atmpS4662;
      _M0L1iS4661 = _M0L1pS1433->$5;
      #line 110 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ml.mbt"
      _M0L6_2atmpS4658 = _M0MPC15array5Array2atGfE(_M0L1iS4661, _M0L1iS1452);
      _M0L6_2atmpS4660 = _M0L2elS1436 - _M0L1vS1453;
      _M0L6_2atmpS4659 = _M0L2glS1439 * _M0L6_2atmpS4660;
      _M0L6_2atmpS4654 = _M0L6_2atmpS4658 + _M0L6_2atmpS4659;
      _M0L6_2atmpS4657 = _M0L3ecaS1438 - _M0L1vS1453;
      _M0L6_2atmpS4656 = _M0L3gcaS1441 * _M0L6_2atmpS4657;
      _M0L6_2atmpS4655 = _M0L6_2atmpS4656 * _M0L5m__ssS1455;
      _M0L6_2atmpS4650 = _M0L6_2atmpS4654 + _M0L6_2atmpS4655;
      _M0L6_2atmpS4653 = _M0L2ekS1437 - _M0L1vS1453;
      _M0L6_2atmpS4652 = _M0L2gkS1440 * _M0L6_2atmpS4653;
      _M0L6_2atmpS4651 = _M0L6_2atmpS4652 * _M0L1wS1454;
      _M0L2dvS1456 = _M0L6_2atmpS4650 + _M0L6_2atmpS4651;
      _M0L6_2atmpS4649 = _M0L1vS1453 - _M0L2v3S1446;
      _M0L6_2atmpS4648 = _M0L6_2atmpS4649 / _M0L2v4S1447;
      #line 112 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ml.mbt"
      _M0L6_2atmpS4647 = _M0FP26RiantR8snn__mbt5tanhf(_M0L6_2atmpS4648);
      _M0L6_2atmpS4646 = 0x1p+0f + _M0L6_2atmpS4647;
      _M0L5n__ssS1457 = 0x1p-1f * _M0L6_2atmpS4646;
      _M0L6_2atmpS4644 = _M0L1vS1453 - _M0L2v3S1446;
      _M0L6_2atmpS4645 = 0x1p+1f * _M0L2v4S1447;
      _M0L9cosh__argS1458 = _M0L6_2atmpS4644 / _M0L6_2atmpS4645;
      #line 116 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ml.mbt"
      _M0L6_2atmpS4641 = _M0FP26RiantR8snn__mbt4expf(_M0L9cosh__argS1458);
      _M0L6_2atmpS4643 = -_M0L9cosh__argS1458;
      #line 116 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ml.mbt"
      _M0L6_2atmpS4642 = _M0FP26RiantR8snn__mbt4expf(_M0L6_2atmpS4643);
      _M0L6_2atmpS4640 = _M0L6_2atmpS4641 + _M0L6_2atmpS4642;
      _M0L9cosh__valS1459 = 0x1p-1f * _M0L6_2atmpS4640;
      _M0L6_2atmpS4638 = _M0L3phiS1448 * _M0L9cosh__valS1459;
      if (_M0L6_2atmpS4638 != 0x0p+0f) {
        float _M0L6_2atmpS4639 = _M0L3phiS1448 * _M0L9cosh__valS1459;
        _M0L3tauS1460 = 0x1p+0f / _M0L6_2atmpS4639;
      } else {
        _M0L3tauS1460 = 0x0p+0f;
      }
      _M0L6_2atmpS4637 = _M0L5n__ssS1457 - _M0L1wS1454;
      _M0L2dwS1461 = _M0L6_2atmpS4637 / _M0L3tauS1460;
      _M0L1vS4630 = _M0L1pS1433->$2;
      _M0L6_2atmpS4633 = _M0L2dtS1462 / _M0L2cmS1435;
      _M0L6_2atmpS4632 = _M0L6_2atmpS4633 * _M0L2dvS1456;
      _M0L6_2atmpS4631 = _M0L1vS1453 + _M0L6_2atmpS4632;
      #line 119 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ml.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS4630, _M0L1iS1452, _M0L6_2atmpS4631);
      _M0L1wS4634 = _M0L1pS1433->$3;
      _M0L6_2atmpS4636 = _M0L2dtS1462 * _M0L2dwS1461;
      _M0L6_2atmpS4635 = _M0L1wS1454 + _M0L6_2atmpS4636;
      #line 120 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ml.mbt"
      _M0MPC15array5Array3setGfE(_M0L1wS4634, _M0L1iS1452, _M0L6_2atmpS4635);
      _M0L6_2atmpS4668 = _M0L1iS1452 + 1;
      _M0L1iS1452 = _M0L6_2atmpS4668;
      continue;
    }
    break;
  }
  _M0L7_2abindS1464 = 0;
  _M0L1iS1465 = _M0L7_2abindS1464;
  while (1) {
    if (_M0L1iS1465 < _M0L1nS1432) {
      struct _M0TPB5ArrayGfE* _M0L1vS4669 = _M0L1pS1433->$2;
      struct _M0TPB5ArrayGfE* _M0L1vS4687 = _M0L1pS1433->$2;
      float _M0L6_2atmpS4671;
      float _M0L6_2atmpS4673;
      struct _M0TPB5ArrayGfE* _M0L2geS4686;
      float _M0L6_2atmpS4682;
      struct _M0TPB5ArrayGfE* _M0L1vS4685;
      float _M0L6_2atmpS4684;
      float _M0L6_2atmpS4683;
      float _M0L6_2atmpS4675;
      struct _M0TPB5ArrayGfE* _M0L2giS4681;
      float _M0L6_2atmpS4677;
      struct _M0TPB5ArrayGfE* _M0L1vS4680;
      float _M0L6_2atmpS4679;
      float _M0L6_2atmpS4678;
      float _M0L6_2atmpS4676;
      float _M0L6_2atmpS4674;
      float _M0L6_2atmpS4672;
      float _M0L6_2atmpS4670;
      int32_t _M0L6_2atmpS4688;
      #line 123 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ml.mbt"
      _M0L6_2atmpS4671 = _M0MPC15array5Array2atGfE(_M0L1vS4687, _M0L1iS1465);
      _M0L6_2atmpS4673 = _M0L2dtS1462 / _M0L2cmS1435;
      _M0L2geS4686 = _M0L1pS1433->$6;
      #line 123 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ml.mbt"
      _M0L6_2atmpS4682 = _M0MPC15array5Array2atGfE(_M0L2geS4686, _M0L1iS1465);
      _M0L1vS4685 = _M0L1pS1433->$2;
      #line 123 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ml.mbt"
      _M0L6_2atmpS4684 = _M0MPC15array5Array2atGfE(_M0L1vS4685, _M0L1iS1465);
      _M0L6_2atmpS4683 = _M0L4e__eS1449 - _M0L6_2atmpS4684;
      _M0L6_2atmpS4675 = _M0L6_2atmpS4682 * _M0L6_2atmpS4683;
      _M0L2giS4681 = _M0L1pS1433->$7;
      #line 123 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ml.mbt"
      _M0L6_2atmpS4677 = _M0MPC15array5Array2atGfE(_M0L2giS4681, _M0L1iS1465);
      _M0L1vS4680 = _M0L1pS1433->$2;
      #line 123 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ml.mbt"
      _M0L6_2atmpS4679 = _M0MPC15array5Array2atGfE(_M0L1vS4680, _M0L1iS1465);
      _M0L6_2atmpS4678 = _M0L4e__iS1450 - _M0L6_2atmpS4679;
      _M0L6_2atmpS4676 = _M0L6_2atmpS4677 * _M0L6_2atmpS4678;
      _M0L6_2atmpS4674 = _M0L6_2atmpS4675 + _M0L6_2atmpS4676;
      _M0L6_2atmpS4672 = _M0L6_2atmpS4673 * _M0L6_2atmpS4674;
      _M0L6_2atmpS4670 = _M0L6_2atmpS4671 + _M0L6_2atmpS4672;
      #line 123 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ml.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS4669, _M0L1iS1465, _M0L6_2atmpS4670);
      _M0L6_2atmpS4688 = _M0L1iS1465 + 1;
      _M0L1iS1465 = _M0L6_2atmpS4688;
      continue;
    }
    break;
  }
  _M0L7_2abindS1467 = 0;
  _M0L1iS1468 = _M0L7_2abindS1467;
  while (1) {
    if (_M0L1iS1468 < _M0L1nS1432) {
      struct _M0TPB5ArrayGfE* _M0L2geS4689 = _M0L1pS1433->$6;
      struct _M0TPB5ArrayGfE* _M0L2geS4697 = _M0L1pS1433->$6;
      float _M0L6_2atmpS4691;
      struct _M0TPB5ArrayGfE* _M0L2geS4696;
      float _M0L6_2atmpS4695;
      float _M0L6_2atmpS4694;
      float _M0L6_2atmpS4693;
      float _M0L6_2atmpS4692;
      float _M0L6_2atmpS4690;
      struct _M0TPB5ArrayGfE* _M0L2giS4698;
      struct _M0TPB5ArrayGfE* _M0L2giS4706;
      float _M0L6_2atmpS4700;
      struct _M0TPB5ArrayGfE* _M0L2giS4705;
      float _M0L6_2atmpS4704;
      float _M0L6_2atmpS4703;
      float _M0L6_2atmpS4702;
      float _M0L6_2atmpS4701;
      float _M0L6_2atmpS4699;
      int32_t _M0L6_2atmpS4707;
      #line 126 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ml.mbt"
      _M0L6_2atmpS4691 = _M0MPC15array5Array2atGfE(_M0L2geS4697, _M0L1iS1468);
      _M0L2geS4696 = _M0L1pS1433->$6;
      #line 126 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ml.mbt"
      _M0L6_2atmpS4695 = _M0MPC15array5Array2atGfE(_M0L2geS4696, _M0L1iS1468);
      _M0L6_2atmpS4694 = -_M0L6_2atmpS4695;
      _M0L6_2atmpS4693 = _M0L6_2atmpS4694 / _M0L6tau__eS1442;
      _M0L6_2atmpS4692 = _M0L2dtS1462 * _M0L6_2atmpS4693;
      _M0L6_2atmpS4690 = _M0L6_2atmpS4691 + _M0L6_2atmpS4692;
      #line 126 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ml.mbt"
      _M0MPC15array5Array3setGfE(_M0L2geS4689, _M0L1iS1468, _M0L6_2atmpS4690);
      _M0L2giS4698 = _M0L1pS1433->$7;
      _M0L2giS4706 = _M0L1pS1433->$7;
      #line 127 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ml.mbt"
      _M0L6_2atmpS4700 = _M0MPC15array5Array2atGfE(_M0L2giS4706, _M0L1iS1468);
      _M0L2giS4705 = _M0L1pS1433->$7;
      #line 127 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ml.mbt"
      _M0L6_2atmpS4704 = _M0MPC15array5Array2atGfE(_M0L2giS4705, _M0L1iS1468);
      _M0L6_2atmpS4703 = -_M0L6_2atmpS4704;
      _M0L6_2atmpS4702 = _M0L6_2atmpS4703 / _M0L6tau__iS1443;
      _M0L6_2atmpS4701 = _M0L2dtS1462 * _M0L6_2atmpS4702;
      _M0L6_2atmpS4699 = _M0L6_2atmpS4700 + _M0L6_2atmpS4701;
      #line 127 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ml.mbt"
      _M0MPC15array5Array3setGfE(_M0L2giS4698, _M0L1iS1468, _M0L6_2atmpS4699);
      _M0L6_2atmpS4707 = _M0L1iS1468 + 1;
      _M0L1iS1468 = _M0L6_2atmpS4707;
      continue;
    }
    break;
  }
  _M0L7_2abindS1470 = 0;
  _M0L1iS1471 = _M0L7_2abindS1470;
  while (1) {
    if (_M0L1iS1471 < _M0L1nS1432) {
      struct _M0TPB5ArrayGbE* _M0L4fireS4708 = _M0L1pS1433->$4;
      struct _M0TPB5ArrayGfE* _M0L1vS4711 = _M0L1pS1433->$2;
      float _M0L6_2atmpS4710;
      int32_t _M0L6_2atmpS4709;
      int32_t _M0L6_2atmpS4712;
      #line 130 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ml.mbt"
      _M0L6_2atmpS4710 = _M0MPC15array5Array2atGfE(_M0L1vS4711, _M0L1iS1471);
      _M0L6_2atmpS4709 = _M0L6_2atmpS4710 > 0x1.4p+4f;
      #line 130 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ml.mbt"
      _M0MPC15array5Array3setGbE(_M0L4fireS4708, _M0L1iS1471, _M0L6_2atmpS4709);
      _M0L6_2atmpS4712 = _M0L1iS1471 + 1;
      _M0L1iS1471 = _M0L6_2atmpS4712;
      continue;
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt8step__iz(
  struct _M0TP26RiantR8snn__mbt2IZ* _M0L1pS1401,
  float _M0L2dtS1413
) {
  int32_t _M0L1nS1400;
  struct _M0TP26RiantR8snn__mbt11IZParameter* _M0L3p__S1402;
  float _M0L1aS1403;
  float _M0L1bS1404;
  float _M0L1cS1405;
  float _M0L1dS1406;
  float _M0L6tau__eS1407;
  float _M0L6tau__iS1408;
  float _M0L4e__eS1409;
  float _M0L4e__iS1410;
  int32_t _M0L7_2abindS1411;
  int32_t _M0L1iS1412;
  int32_t _M0L7_2abindS1415;
  int32_t _M0L1iS1416;
  int32_t _M0L7_2abindS1422;
  int32_t _M0L1iS1423;
  int32_t _M0L7_2abindS1426;
  int32_t _M0L1iS1427;
  int32_t _M0L7_2abindS1429;
  int32_t _M0L1iS1430;
  #line 139 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  _M0L1nS1400 = _M0L1pS1401->$1;
  _M0L3p__S1402 = _M0L1pS1401->$0;
  _M0L1aS1403 = _M0L3p__S1402->$0;
  _M0L1bS1404 = _M0L3p__S1402->$1;
  _M0L1cS1405 = _M0L3p__S1402->$2;
  _M0L1dS1406 = _M0L3p__S1402->$3;
  _M0L6tau__eS1407 = _M0L3p__S1402->$4;
  _M0L6tau__iS1408 = _M0L3p__S1402->$5;
  _M0L4e__eS1409 = _M0L3p__S1402->$6;
  _M0L4e__iS1410 = _M0L3p__S1402->$7;
  _M0L7_2abindS1411 = 0;
  _M0L1iS1412 = _M0L7_2abindS1411;
  while (1) {
    if (_M0L1iS1412 < _M0L1nS1400) {
      struct _M0TPB5ArrayGfE* _M0L2geS4538 = _M0L1pS1401->$6;
      struct _M0TPB5ArrayGfE* _M0L2geS4546 = _M0L1pS1401->$6;
      float _M0L6_2atmpS4540;
      struct _M0TPB5ArrayGfE* _M0L2geS4545;
      float _M0L6_2atmpS4544;
      float _M0L6_2atmpS4543;
      float _M0L6_2atmpS4542;
      float _M0L6_2atmpS4541;
      float _M0L6_2atmpS4539;
      struct _M0TPB5ArrayGfE* _M0L2giS4547;
      struct _M0TPB5ArrayGfE* _M0L2giS4555;
      float _M0L6_2atmpS4549;
      struct _M0TPB5ArrayGfE* _M0L2giS4554;
      float _M0L6_2atmpS4553;
      float _M0L6_2atmpS4552;
      float _M0L6_2atmpS4551;
      float _M0L6_2atmpS4550;
      float _M0L6_2atmpS4548;
      int32_t _M0L6_2atmpS4556;
      #line 152 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L6_2atmpS4540 = _M0MPC15array5Array2atGfE(_M0L2geS4546, _M0L1iS1412);
      _M0L2geS4545 = _M0L1pS1401->$6;
      #line 152 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L6_2atmpS4544 = _M0MPC15array5Array2atGfE(_M0L2geS4545, _M0L1iS1412);
      _M0L6_2atmpS4543 = -_M0L6_2atmpS4544;
      _M0L6_2atmpS4542 = _M0L2dtS1413 * _M0L6_2atmpS4543;
      _M0L6_2atmpS4541 = _M0L6_2atmpS4542 / _M0L6tau__eS1407;
      _M0L6_2atmpS4539 = _M0L6_2atmpS4540 + _M0L6_2atmpS4541;
      #line 152 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0MPC15array5Array3setGfE(_M0L2geS4538, _M0L1iS1412, _M0L6_2atmpS4539);
      _M0L2giS4547 = _M0L1pS1401->$7;
      _M0L2giS4555 = _M0L1pS1401->$7;
      #line 153 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L6_2atmpS4549 = _M0MPC15array5Array2atGfE(_M0L2giS4555, _M0L1iS1412);
      _M0L2giS4554 = _M0L1pS1401->$7;
      #line 153 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L6_2atmpS4553 = _M0MPC15array5Array2atGfE(_M0L2giS4554, _M0L1iS1412);
      _M0L6_2atmpS4552 = -_M0L6_2atmpS4553;
      _M0L6_2atmpS4551 = _M0L2dtS1413 * _M0L6_2atmpS4552;
      _M0L6_2atmpS4550 = _M0L6_2atmpS4551 / _M0L6tau__iS1408;
      _M0L6_2atmpS4548 = _M0L6_2atmpS4549 + _M0L6_2atmpS4550;
      #line 153 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0MPC15array5Array3setGfE(_M0L2giS4547, _M0L1iS1412, _M0L6_2atmpS4548);
      _M0L6_2atmpS4556 = _M0L1iS1412 + 1;
      _M0L1iS1412 = _M0L6_2atmpS4556;
      continue;
    }
    break;
  }
  _M0L7_2abindS1415 = 0;
  _M0L1iS1416 = _M0L7_2abindS1415;
  while (1) {
    if (_M0L1iS1416 < _M0L1nS1400) {
      struct _M0TPB5ArrayGfE* _M0L1vS4582 = _M0L1pS1401->$2;
      float _M0L1vS1417;
      struct _M0TPB5ArrayGfE* _M0L1uS4581;
      float _M0L1uS1418;
      struct _M0TPB5ArrayGfE* _M0L1iS4580;
      float _M0L2iiS1419;
      struct _M0TPB5ArrayGfE* _M0L1vS4557;
      float _M0L6_2atmpS4560;
      float _M0L6_2atmpS4567;
      float _M0L6_2atmpS4565;
      float _M0L6_2atmpS4566;
      float _M0L6_2atmpS4564;
      float _M0L6_2atmpS4563;
      float _M0L6_2atmpS4562;
      float _M0L6_2atmpS4561;
      float _M0L6_2atmpS4559;
      float _M0L6_2atmpS4558;
      struct _M0TPB5ArrayGfE* _M0L1vS4579;
      float _M0L2v2S1420;
      struct _M0TPB5ArrayGfE* _M0L1vS4568;
      float _M0L6_2atmpS4571;
      float _M0L6_2atmpS4578;
      float _M0L6_2atmpS4576;
      float _M0L6_2atmpS4577;
      float _M0L6_2atmpS4575;
      float _M0L6_2atmpS4574;
      float _M0L6_2atmpS4573;
      float _M0L6_2atmpS4572;
      float _M0L6_2atmpS4570;
      float _M0L6_2atmpS4569;
      int32_t _M0L6_2atmpS4583;
      #line 157 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L1vS1417 = _M0MPC15array5Array2atGfE(_M0L1vS4582, _M0L1iS1416);
      _M0L1uS4581 = _M0L1pS1401->$3;
      #line 158 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L1uS1418 = _M0MPC15array5Array2atGfE(_M0L1uS4581, _M0L1iS1416);
      _M0L1iS4580 = _M0L1pS1401->$5;
      #line 159 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L2iiS1419 = _M0MPC15array5Array2atGfE(_M0L1iS4580, _M0L1iS1416);
      _M0L1vS4557 = _M0L1pS1401->$2;
      _M0L6_2atmpS4560 = 0x1p-1f * _M0L2dtS1413;
      _M0L6_2atmpS4567 = 0x1.47ae147ae147bp-5f * _M0L1vS1417;
      _M0L6_2atmpS4565 = _M0L6_2atmpS4567 * _M0L1vS1417;
      _M0L6_2atmpS4566 = 0x1.4p+2f * _M0L1vS1417;
      _M0L6_2atmpS4564 = _M0L6_2atmpS4565 + _M0L6_2atmpS4566;
      _M0L6_2atmpS4563 = _M0L6_2atmpS4564 + 0x1.18p+7f;
      _M0L6_2atmpS4562 = _M0L6_2atmpS4563 - _M0L1uS1418;
      _M0L6_2atmpS4561 = _M0L6_2atmpS4562 + _M0L2iiS1419;
      _M0L6_2atmpS4559 = _M0L6_2atmpS4560 * _M0L6_2atmpS4561;
      _M0L6_2atmpS4558 = _M0L1vS1417 + _M0L6_2atmpS4559;
      #line 160 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS4557, _M0L1iS1416, _M0L6_2atmpS4558);
      _M0L1vS4579 = _M0L1pS1401->$2;
      #line 161 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L2v2S1420 = _M0MPC15array5Array2atGfE(_M0L1vS4579, _M0L1iS1416);
      _M0L1vS4568 = _M0L1pS1401->$2;
      _M0L6_2atmpS4571 = 0x1p-1f * _M0L2dtS1413;
      _M0L6_2atmpS4578 = 0x1.47ae147ae147bp-5f * _M0L2v2S1420;
      _M0L6_2atmpS4576 = _M0L6_2atmpS4578 * _M0L2v2S1420;
      _M0L6_2atmpS4577 = 0x1.4p+2f * _M0L2v2S1420;
      _M0L6_2atmpS4575 = _M0L6_2atmpS4576 + _M0L6_2atmpS4577;
      _M0L6_2atmpS4574 = _M0L6_2atmpS4575 + 0x1.18p+7f;
      _M0L6_2atmpS4573 = _M0L6_2atmpS4574 - _M0L1uS1418;
      _M0L6_2atmpS4572 = _M0L6_2atmpS4573 + _M0L2iiS1419;
      _M0L6_2atmpS4570 = _M0L6_2atmpS4571 * _M0L6_2atmpS4572;
      _M0L6_2atmpS4569 = _M0L2v2S1420 + _M0L6_2atmpS4570;
      #line 162 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS4568, _M0L1iS1416, _M0L6_2atmpS4569);
      _M0L6_2atmpS4583 = _M0L1iS1416 + 1;
      _M0L1iS1416 = _M0L6_2atmpS4583;
      continue;
    }
    break;
  }
  _M0L7_2abindS1422 = 0;
  _M0L1iS1423 = _M0L7_2abindS1422;
  while (1) {
    if (_M0L1iS1423 < _M0L1nS1400) {
      struct _M0TPB5ArrayGfE* _M0L1vS4594 = _M0L1pS1401->$2;
      float _M0L1vS1424;
      struct _M0TPB5ArrayGfE* _M0L1uS4584;
      struct _M0TPB5ArrayGfE* _M0L1uS4593;
      float _M0L6_2atmpS4586;
      float _M0L6_2atmpS4588;
      float _M0L6_2atmpS4590;
      struct _M0TPB5ArrayGfE* _M0L1uS4592;
      float _M0L6_2atmpS4591;
      float _M0L6_2atmpS4589;
      float _M0L6_2atmpS4587;
      float _M0L6_2atmpS4585;
      int32_t _M0L6_2atmpS4595;
      #line 165 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L1vS1424 = _M0MPC15array5Array2atGfE(_M0L1vS4594, _M0L1iS1423);
      _M0L1uS4584 = _M0L1pS1401->$3;
      _M0L1uS4593 = _M0L1pS1401->$3;
      #line 166 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L6_2atmpS4586 = _M0MPC15array5Array2atGfE(_M0L1uS4593, _M0L1iS1423);
      _M0L6_2atmpS4588 = _M0L2dtS1413 * _M0L1aS1403;
      _M0L6_2atmpS4590 = _M0L1bS1404 * _M0L1vS1424;
      _M0L1uS4592 = _M0L1pS1401->$3;
      #line 166 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L6_2atmpS4591 = _M0MPC15array5Array2atGfE(_M0L1uS4592, _M0L1iS1423);
      _M0L6_2atmpS4589 = _M0L6_2atmpS4590 - _M0L6_2atmpS4591;
      _M0L6_2atmpS4587 = _M0L6_2atmpS4588 * _M0L6_2atmpS4589;
      _M0L6_2atmpS4585 = _M0L6_2atmpS4586 + _M0L6_2atmpS4587;
      #line 166 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0MPC15array5Array3setGfE(_M0L1uS4584, _M0L1iS1423, _M0L6_2atmpS4585);
      _M0L6_2atmpS4595 = _M0L1iS1423 + 1;
      _M0L1iS1423 = _M0L6_2atmpS4595;
      continue;
    }
    break;
  }
  _M0L7_2abindS1426 = 0;
  _M0L1iS1427 = _M0L7_2abindS1426;
  while (1) {
    if (_M0L1iS1427 < _M0L1nS1400) {
      struct _M0TPB5ArrayGfE* _M0L1vS4596 = _M0L1pS1401->$2;
      struct _M0TPB5ArrayGfE* _M0L1vS4613 = _M0L1pS1401->$2;
      float _M0L6_2atmpS4598;
      struct _M0TPB5ArrayGfE* _M0L2geS4612;
      float _M0L6_2atmpS4608;
      struct _M0TPB5ArrayGfE* _M0L1vS4611;
      float _M0L6_2atmpS4610;
      float _M0L6_2atmpS4609;
      float _M0L6_2atmpS4601;
      struct _M0TPB5ArrayGfE* _M0L2giS4607;
      float _M0L6_2atmpS4603;
      struct _M0TPB5ArrayGfE* _M0L1vS4606;
      float _M0L6_2atmpS4605;
      float _M0L6_2atmpS4604;
      float _M0L6_2atmpS4602;
      float _M0L6_2atmpS4600;
      float _M0L6_2atmpS4599;
      float _M0L6_2atmpS4597;
      int32_t _M0L6_2atmpS4614;
      #line 169 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L6_2atmpS4598 = _M0MPC15array5Array2atGfE(_M0L1vS4613, _M0L1iS1427);
      _M0L2geS4612 = _M0L1pS1401->$6;
      #line 169 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L6_2atmpS4608 = _M0MPC15array5Array2atGfE(_M0L2geS4612, _M0L1iS1427);
      _M0L1vS4611 = _M0L1pS1401->$2;
      #line 169 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L6_2atmpS4610 = _M0MPC15array5Array2atGfE(_M0L1vS4611, _M0L1iS1427);
      _M0L6_2atmpS4609 = _M0L4e__eS1409 - _M0L6_2atmpS4610;
      _M0L6_2atmpS4601 = _M0L6_2atmpS4608 * _M0L6_2atmpS4609;
      _M0L2giS4607 = _M0L1pS1401->$7;
      #line 169 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L6_2atmpS4603 = _M0MPC15array5Array2atGfE(_M0L2giS4607, _M0L1iS1427);
      _M0L1vS4606 = _M0L1pS1401->$2;
      #line 169 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L6_2atmpS4605 = _M0MPC15array5Array2atGfE(_M0L1vS4606, _M0L1iS1427);
      _M0L6_2atmpS4604 = _M0L4e__iS1410 - _M0L6_2atmpS4605;
      _M0L6_2atmpS4602 = _M0L6_2atmpS4603 * _M0L6_2atmpS4604;
      _M0L6_2atmpS4600 = _M0L6_2atmpS4601 + _M0L6_2atmpS4602;
      _M0L6_2atmpS4599 = _M0L2dtS1413 * _M0L6_2atmpS4600;
      _M0L6_2atmpS4597 = _M0L6_2atmpS4598 + _M0L6_2atmpS4599;
      #line 169 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS4596, _M0L1iS1427, _M0L6_2atmpS4597);
      _M0L6_2atmpS4614 = _M0L1iS1427 + 1;
      _M0L1iS1427 = _M0L6_2atmpS4614;
      continue;
    }
    break;
  }
  _M0L7_2abindS1429 = 0;
  _M0L1iS1430 = _M0L7_2abindS1429;
  while (1) {
    if (_M0L1iS1430 < _M0L1nS1400) {
      struct _M0TPB5ArrayGbE* _M0L4fireS4615 = _M0L1pS1401->$4;
      struct _M0TPB5ArrayGfE* _M0L1vS4618 = _M0L1pS1401->$2;
      float _M0L6_2atmpS4617;
      int32_t _M0L6_2atmpS4616;
      struct _M0TPB5ArrayGfE* _M0L1vS4619;
      struct _M0TPB5ArrayGbE* _M0L4fireS4621;
      float _M0L6_2atmpS4620;
      struct _M0TPB5ArrayGfE* _M0L1uS4623;
      struct _M0TPB5ArrayGfE* _M0L1uS4628;
      float _M0L6_2atmpS4625;
      struct _M0TPB5ArrayGbE* _M0L4fireS4627;
      float _M0L6_2atmpS4626;
      float _M0L6_2atmpS4624;
      int32_t _M0L6_2atmpS4629;
      #line 173 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L6_2atmpS4617 = _M0MPC15array5Array2atGfE(_M0L1vS4618, _M0L1iS1430);
      _M0L6_2atmpS4616 = _M0L6_2atmpS4617 > 0x1.ep+4f;
      #line 173 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0MPC15array5Array3setGbE(_M0L4fireS4615, _M0L1iS1430, _M0L6_2atmpS4616);
      _M0L1vS4619 = _M0L1pS1401->$2;
      _M0L4fireS4621 = _M0L1pS1401->$4;
      #line 174 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS4621, _M0L1iS1430)) {
        _M0L6_2atmpS4620 = _M0L1cS1405;
      } else {
        struct _M0TPB5ArrayGfE* _M0L1vS4622 = _M0L1pS1401->$2;
        #line 174 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
        _M0L6_2atmpS4620
        = _M0MPC15array5Array2atGfE(_M0L1vS4622, _M0L1iS1430);
      }
      #line 174 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS4619, _M0L1iS1430, _M0L6_2atmpS4620);
      _M0L1uS4623 = _M0L1pS1401->$3;
      _M0L1uS4628 = _M0L1pS1401->$3;
      #line 175 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L6_2atmpS4625 = _M0MPC15array5Array2atGfE(_M0L1uS4628, _M0L1iS1430);
      _M0L4fireS4627 = _M0L1pS1401->$4;
      #line 175 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS4627, _M0L1iS1430)) {
        _M0L6_2atmpS4626 = _M0L1dS1406;
      } else {
        _M0L6_2atmpS4626 = 0x0p+0f;
      }
      _M0L6_2atmpS4624 = _M0L6_2atmpS4625 + _M0L6_2atmpS4626;
      #line 175 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0MPC15array5Array3setGfE(_M0L1uS4623, _M0L1iS1430, _M0L6_2atmpS4624);
      _M0L6_2atmpS4629 = _M0L1iS1430 + 1;
      _M0L1iS1430 = _M0L6_2atmpS4629;
      continue;
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt8step__hh(
  struct _M0TP26RiantR8snn__mbt2HH* _M0L1pS1357,
  float _M0L2dtS1383
) {
  int32_t _M0L1nS1356;
  struct _M0TP26RiantR8snn__mbt11HHParameter* _M0L3p__S1358;
  float _M0L2cmS1359;
  float _M0L2glS1360;
  float _M0L2elS1361;
  float _M0L2ekS1362;
  float _M0L2enS1363;
  float _M0L2gnS1364;
  float _M0L2gkS1365;
  float _M0L2vtS1366;
  float _M0L6tau__eS1367;
  float _M0L6tau__iS1368;
  float _M0L4e__eS1369;
  float _M0L4e__iS1370;
  int32_t _M0L7_2abindS1371;
  int32_t _M0L1iS1372;
  int32_t _M0L7_2abindS1397;
  int32_t _M0L1iS1398;
  #line 108 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
  _M0L1nS1356 = _M0L1pS1357->$1;
  _M0L3p__S1358 = _M0L1pS1357->$0;
  _M0L2cmS1359 = _M0L3p__S1358->$0;
  _M0L2glS1360 = _M0L3p__S1358->$1;
  _M0L2elS1361 = _M0L3p__S1358->$2;
  _M0L2ekS1362 = _M0L3p__S1358->$3;
  _M0L2enS1363 = _M0L3p__S1358->$4;
  _M0L2gnS1364 = _M0L3p__S1358->$5;
  _M0L2gkS1365 = _M0L3p__S1358->$6;
  _M0L2vtS1366 = _M0L3p__S1358->$7;
  _M0L6tau__eS1367 = _M0L3p__S1358->$8;
  _M0L6tau__iS1368 = _M0L3p__S1358->$9;
  _M0L4e__eS1369 = _M0L3p__S1358->$10;
  _M0L4e__iS1370 = _M0L3p__S1358->$11;
  _M0L7_2abindS1371 = 0;
  _M0L1iS1372 = _M0L7_2abindS1371;
  while (1) {
    if (_M0L1iS1372 < _M0L1nS1356) {
      struct _M0TPB5ArrayGfE* _M0L1vS4531 = _M0L1pS1357->$2;
      float _M0L1vS1373;
      struct _M0TPB5ArrayGfE* _M0L1mS4530;
      float _M0L1mS1374;
      struct _M0TPB5ArrayGfE* _M0L7n__gateS4529;
      float _M0L2nnS1375;
      struct _M0TPB5ArrayGfE* _M0L1hS4528;
      float _M0L1hS1376;
      struct _M0TPB5ArrayGfE* _M0L2geS4527;
      float _M0L2geS1377;
      struct _M0TPB5ArrayGfE* _M0L2giS4526;
      float _M0L2giS1378;
      struct _M0TPB5ArrayGbE* _M0L4fireS4429;
      float _M0L6_2atmpS4525;
      float _M0L7am__numS1379;
      float _M0L6_2atmpS4524;
      float _M0L7bm__numS1380;
      float _M0L6_2atmpS4519;
      float _M0L6_2atmpS4518;
      float _M0L6_2atmpS4517;
      float _M0L2amS1381;
      float _M0L6_2atmpS4512;
      float _M0L6_2atmpS4511;
      float _M0L6_2atmpS4510;
      float _M0L2bmS1382;
      struct _M0TPB5ArrayGfE* _M0L1mS4430;
      float _M0L6_2atmpS4436;
      float _M0L6_2atmpS4434;
      float _M0L6_2atmpS4435;
      float _M0L6_2atmpS4433;
      float _M0L6_2atmpS4432;
      float _M0L6_2atmpS4431;
      float _M0L6_2atmpS4509;
      float _M0L7an__numS1384;
      float _M0L6_2atmpS4504;
      float _M0L6_2atmpS4503;
      float _M0L6_2atmpS4502;
      float _M0L2anS1385;
      float _M0L6_2atmpS4501;
      float _M0L6_2atmpS4500;
      float _M0L6_2atmpS4499;
      float _M0L6_2atmpS4498;
      float _M0L2bnS1386;
      struct _M0TPB5ArrayGfE* _M0L7n__gateS4437;
      float _M0L6_2atmpS4443;
      float _M0L6_2atmpS4441;
      float _M0L6_2atmpS4442;
      float _M0L6_2atmpS4440;
      float _M0L6_2atmpS4439;
      float _M0L6_2atmpS4438;
      float _M0L6_2atmpS4497;
      float _M0L6_2atmpS4496;
      float _M0L6_2atmpS4495;
      float _M0L6_2atmpS4494;
      float _M0L2ahS1387;
      float _M0L6_2atmpS4493;
      float _M0L6_2atmpS4492;
      float _M0L6_2atmpS4491;
      float _M0L6_2atmpS4490;
      float _M0L9bh__denomS1388;
      float _M0L2bhS1389;
      struct _M0TPB5ArrayGfE* _M0L1hS4444;
      float _M0L6_2atmpS4450;
      float _M0L6_2atmpS4448;
      float _M0L6_2atmpS4449;
      float _M0L6_2atmpS4447;
      float _M0L6_2atmpS4446;
      float _M0L6_2atmpS4445;
      struct _M0TPB5ArrayGfE* _M0L1mS4489;
      float _M0L6m__newS1390;
      struct _M0TPB5ArrayGfE* _M0L7n__gateS4488;
      float _M0L6n__newS1391;
      struct _M0TPB5ArrayGfE* _M0L1hS4487;
      float _M0L6h__newS1392;
      float _M0L6_2atmpS4486;
      float _M0L6_2atmpS4485;
      float _M0L3m3hS1393;
      float _M0L6_2atmpS4484;
      float _M0L6_2atmpS4483;
      float _M0L2n4S1394;
      struct _M0TPB5ArrayGfE* _M0L1iS4482;
      float _M0L6_2atmpS4479;
      float _M0L6_2atmpS4481;
      float _M0L6_2atmpS4480;
      float _M0L6_2atmpS4476;
      float _M0L6_2atmpS4478;
      float _M0L6_2atmpS4477;
      float _M0L6_2atmpS4473;
      float _M0L6_2atmpS4475;
      float _M0L6_2atmpS4474;
      float _M0L6_2atmpS4469;
      float _M0L6_2atmpS4471;
      float _M0L6_2atmpS4472;
      float _M0L6_2atmpS4470;
      float _M0L6_2atmpS4465;
      float _M0L6_2atmpS4467;
      float _M0L6_2atmpS4468;
      float _M0L6_2atmpS4466;
      float _M0L7currentS1395;
      struct _M0TPB5ArrayGfE* _M0L1vS4451;
      float _M0L6_2atmpS4454;
      float _M0L6_2atmpS4453;
      float _M0L6_2atmpS4452;
      struct _M0TPB5ArrayGfE* _M0L2geS4455;
      float _M0L6_2atmpS4459;
      float _M0L6_2atmpS4458;
      float _M0L6_2atmpS4457;
      float _M0L6_2atmpS4456;
      struct _M0TPB5ArrayGfE* _M0L2giS4460;
      float _M0L6_2atmpS4464;
      float _M0L6_2atmpS4463;
      float _M0L6_2atmpS4462;
      float _M0L6_2atmpS4461;
      int32_t _M0L6_2atmpS4532;
      #line 124 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0L1vS1373 = _M0MPC15array5Array2atGfE(_M0L1vS4531, _M0L1iS1372);
      _M0L1mS4530 = _M0L1pS1357->$3;
      #line 125 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0L1mS1374 = _M0MPC15array5Array2atGfE(_M0L1mS4530, _M0L1iS1372);
      _M0L7n__gateS4529 = _M0L1pS1357->$4;
      #line 126 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0L2nnS1375
      = _M0MPC15array5Array2atGfE(_M0L7n__gateS4529, _M0L1iS1372);
      _M0L1hS4528 = _M0L1pS1357->$5;
      #line 127 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0L1hS1376 = _M0MPC15array5Array2atGfE(_M0L1hS4528, _M0L1iS1372);
      _M0L2geS4527 = _M0L1pS1357->$8;
      #line 128 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0L2geS1377 = _M0MPC15array5Array2atGfE(_M0L2geS4527, _M0L1iS1372);
      _M0L2giS4526 = _M0L1pS1357->$9;
      #line 129 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0L2giS1378 = _M0MPC15array5Array2atGfE(_M0L2giS4526, _M0L1iS1372);
      _M0L4fireS4429 = _M0L1pS1357->$6;
      #line 130 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0MPC15array5Array3setGbE(_M0L4fireS4429, _M0L1iS1372, 0);
      _M0L6_2atmpS4525 = 0x1.ap+3f - _M0L1vS1373;
      _M0L7am__numS1379 = _M0L6_2atmpS4525 + _M0L2vtS1366;
      _M0L6_2atmpS4524 = _M0L1vS1373 - _M0L2vtS1366;
      _M0L7bm__numS1380 = _M0L6_2atmpS4524 - 0x1.4p+5f;
      _M0L6_2atmpS4519 = _M0L7am__numS1379 / 0x1p+2f;
      #line 134 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0L6_2atmpS4518 = _M0FP26RiantR8snn__mbt4expf(_M0L6_2atmpS4519);
      _M0L6_2atmpS4517 = _M0L6_2atmpS4518 - 0x1p+0f;
      if (_M0L6_2atmpS4517 != 0x0p+0f) {
        float _M0L6_2atmpS4520 = 0x1.47ae147ae147bp-2f * _M0L7am__numS1379;
        float _M0L6_2atmpS4523 = _M0L7am__numS1379 / 0x1p+2f;
        float _M0L6_2atmpS4522;
        float _M0L6_2atmpS4521;
        #line 135 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
        _M0L6_2atmpS4522 = _M0FP26RiantR8snn__mbt4expf(_M0L6_2atmpS4523);
        _M0L6_2atmpS4521 = _M0L6_2atmpS4522 - 0x1p+0f;
        _M0L2amS1381 = _M0L6_2atmpS4520 / _M0L6_2atmpS4521;
      } else {
        _M0L2amS1381 = 0x0p+0f;
      }
      _M0L6_2atmpS4512 = _M0L7bm__numS1380 / 0x1.4p+2f;
      #line 139 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0L6_2atmpS4511 = _M0FP26RiantR8snn__mbt4expf(_M0L6_2atmpS4512);
      _M0L6_2atmpS4510 = _M0L6_2atmpS4511 - 0x1p+0f;
      if (_M0L6_2atmpS4510 != 0x0p+0f) {
        float _M0L6_2atmpS4513 = 0x1.1eb851eb851ecp-2f * _M0L7bm__numS1380;
        float _M0L6_2atmpS4516 = _M0L7bm__numS1380 / 0x1.4p+2f;
        float _M0L6_2atmpS4515;
        float _M0L6_2atmpS4514;
        #line 140 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
        _M0L6_2atmpS4515 = _M0FP26RiantR8snn__mbt4expf(_M0L6_2atmpS4516);
        _M0L6_2atmpS4514 = _M0L6_2atmpS4515 - 0x1p+0f;
        _M0L2bmS1382 = _M0L6_2atmpS4513 / _M0L6_2atmpS4514;
      } else {
        _M0L2bmS1382 = 0x0p+0f;
      }
      _M0L1mS4430 = _M0L1pS1357->$3;
      _M0L6_2atmpS4436 = 0x1p+0f - _M0L1mS1374;
      _M0L6_2atmpS4434 = _M0L2amS1381 * _M0L6_2atmpS4436;
      _M0L6_2atmpS4435 = _M0L2bmS1382 * _M0L1mS1374;
      _M0L6_2atmpS4433 = _M0L6_2atmpS4434 - _M0L6_2atmpS4435;
      _M0L6_2atmpS4432 = _M0L2dtS1383 * _M0L6_2atmpS4433;
      _M0L6_2atmpS4431 = _M0L1mS1374 + _M0L6_2atmpS4432;
      #line 144 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0MPC15array5Array3setGfE(_M0L1mS4430, _M0L1iS1372, _M0L6_2atmpS4431);
      _M0L6_2atmpS4509 = 0x1.ep+3f - _M0L1vS1373;
      _M0L7an__numS1384 = _M0L6_2atmpS4509 + _M0L2vtS1366;
      _M0L6_2atmpS4504 = _M0L7an__numS1384 / 0x1.4p+2f;
      #line 147 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0L6_2atmpS4503 = _M0FP26RiantR8snn__mbt4expf(_M0L6_2atmpS4504);
      _M0L6_2atmpS4502 = _M0L6_2atmpS4503 - 0x1p+0f;
      if (_M0L6_2atmpS4502 != 0x0p+0f) {
        float _M0L6_2atmpS4505 = 0x1.0624dd2f1a9fcp-5f * _M0L7an__numS1384;
        float _M0L6_2atmpS4508 = _M0L7an__numS1384 / 0x1.4p+2f;
        float _M0L6_2atmpS4507;
        float _M0L6_2atmpS4506;
        #line 148 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
        _M0L6_2atmpS4507 = _M0FP26RiantR8snn__mbt4expf(_M0L6_2atmpS4508);
        _M0L6_2atmpS4506 = _M0L6_2atmpS4507 - 0x1p+0f;
        _M0L2anS1385 = _M0L6_2atmpS4505 / _M0L6_2atmpS4506;
      } else {
        _M0L2anS1385 = 0x0p+0f;
      }
      _M0L6_2atmpS4501 = 0x1.4p+3f - _M0L1vS1373;
      _M0L6_2atmpS4500 = _M0L6_2atmpS4501 + _M0L2vtS1366;
      _M0L6_2atmpS4499 = _M0L6_2atmpS4500 / 0x1.4p+5f;
      #line 152 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0L6_2atmpS4498 = _M0FP26RiantR8snn__mbt4expf(_M0L6_2atmpS4499);
      _M0L2bnS1386 = 0x1p-1f * _M0L6_2atmpS4498;
      _M0L7n__gateS4437 = _M0L1pS1357->$4;
      _M0L6_2atmpS4443 = 0x1p+0f - _M0L2nnS1375;
      _M0L6_2atmpS4441 = _M0L2anS1385 * _M0L6_2atmpS4443;
      _M0L6_2atmpS4442 = _M0L2bnS1386 * _M0L2nnS1375;
      _M0L6_2atmpS4440 = _M0L6_2atmpS4441 - _M0L6_2atmpS4442;
      _M0L6_2atmpS4439 = _M0L2dtS1383 * _M0L6_2atmpS4440;
      _M0L6_2atmpS4438 = _M0L2nnS1375 + _M0L6_2atmpS4439;
      #line 153 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0MPC15array5Array3setGfE(_M0L7n__gateS4437, _M0L1iS1372, _M0L6_2atmpS4438);
      _M0L6_2atmpS4497 = 0x1.1p+4f - _M0L1vS1373;
      _M0L6_2atmpS4496 = _M0L6_2atmpS4497 + _M0L2vtS1366;
      _M0L6_2atmpS4495 = _M0L6_2atmpS4496 / 0x1.2p+4f;
      #line 155 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0L6_2atmpS4494 = _M0FP26RiantR8snn__mbt4expf(_M0L6_2atmpS4495);
      _M0L2ahS1387 = 0x1.0624dd2f1a9fcp-3f * _M0L6_2atmpS4494;
      _M0L6_2atmpS4493 = 0x1.4p+5f - _M0L1vS1373;
      _M0L6_2atmpS4492 = _M0L6_2atmpS4493 + _M0L2vtS1366;
      _M0L6_2atmpS4491 = _M0L6_2atmpS4492 / 0x1.4p+2f;
      #line 156 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0L6_2atmpS4490 = _M0FP26RiantR8snn__mbt4expf(_M0L6_2atmpS4491);
      _M0L9bh__denomS1388 = 0x1p+0f + _M0L6_2atmpS4490;
      if (_M0L9bh__denomS1388 != 0x0p+0f) {
        _M0L2bhS1389 = 0x1p+2f / _M0L9bh__denomS1388;
      } else {
        _M0L2bhS1389 = 0x0p+0f;
      }
      _M0L1hS4444 = _M0L1pS1357->$5;
      _M0L6_2atmpS4450 = 0x1p+0f - _M0L1hS1376;
      _M0L6_2atmpS4448 = _M0L2ahS1387 * _M0L6_2atmpS4450;
      _M0L6_2atmpS4449 = _M0L2bhS1389 * _M0L1hS1376;
      _M0L6_2atmpS4447 = _M0L6_2atmpS4448 - _M0L6_2atmpS4449;
      _M0L6_2atmpS4446 = _M0L2dtS1383 * _M0L6_2atmpS4447;
      _M0L6_2atmpS4445 = _M0L1hS1376 + _M0L6_2atmpS4446;
      #line 158 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0MPC15array5Array3setGfE(_M0L1hS4444, _M0L1iS1372, _M0L6_2atmpS4445);
      _M0L1mS4489 = _M0L1pS1357->$3;
      #line 160 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0L6m__newS1390 = _M0MPC15array5Array2atGfE(_M0L1mS4489, _M0L1iS1372);
      _M0L7n__gateS4488 = _M0L1pS1357->$4;
      #line 161 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0L6n__newS1391
      = _M0MPC15array5Array2atGfE(_M0L7n__gateS4488, _M0L1iS1372);
      _M0L1hS4487 = _M0L1pS1357->$5;
      #line 162 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0L6h__newS1392 = _M0MPC15array5Array2atGfE(_M0L1hS4487, _M0L1iS1372);
      _M0L6_2atmpS4486 = _M0L6m__newS1390 * _M0L6m__newS1390;
      _M0L6_2atmpS4485 = _M0L6_2atmpS4486 * _M0L6m__newS1390;
      _M0L3m3hS1393 = _M0L6_2atmpS4485 * _M0L6h__newS1392;
      _M0L6_2atmpS4484 = _M0L6n__newS1391 * _M0L6n__newS1391;
      _M0L6_2atmpS4483 = _M0L6_2atmpS4484 * _M0L6n__newS1391;
      _M0L2n4S1394 = _M0L6_2atmpS4483 * _M0L6n__newS1391;
      _M0L1iS4482 = _M0L1pS1357->$7;
      #line 165 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0L6_2atmpS4479 = _M0MPC15array5Array2atGfE(_M0L1iS4482, _M0L1iS1372);
      _M0L6_2atmpS4481 = _M0L2elS1361 - _M0L1vS1373;
      _M0L6_2atmpS4480 = _M0L2glS1360 * _M0L6_2atmpS4481;
      _M0L6_2atmpS4476 = _M0L6_2atmpS4479 + _M0L6_2atmpS4480;
      _M0L6_2atmpS4478 = _M0L4e__eS1369 - _M0L1vS1373;
      _M0L6_2atmpS4477 = _M0L2geS1377 * _M0L6_2atmpS4478;
      _M0L6_2atmpS4473 = _M0L6_2atmpS4476 + _M0L6_2atmpS4477;
      _M0L6_2atmpS4475 = _M0L4e__iS1370 - _M0L1vS1373;
      _M0L6_2atmpS4474 = _M0L2giS1378 * _M0L6_2atmpS4475;
      _M0L6_2atmpS4469 = _M0L6_2atmpS4473 + _M0L6_2atmpS4474;
      _M0L6_2atmpS4471 = _M0L2gnS1364 * _M0L3m3hS1393;
      _M0L6_2atmpS4472 = _M0L2enS1363 - _M0L1vS1373;
      _M0L6_2atmpS4470 = _M0L6_2atmpS4471 * _M0L6_2atmpS4472;
      _M0L6_2atmpS4465 = _M0L6_2atmpS4469 + _M0L6_2atmpS4470;
      _M0L6_2atmpS4467 = _M0L2gkS1365 * _M0L2n4S1394;
      _M0L6_2atmpS4468 = _M0L2ekS1362 - _M0L1vS1373;
      _M0L6_2atmpS4466 = _M0L6_2atmpS4467 * _M0L6_2atmpS4468;
      _M0L7currentS1395 = _M0L6_2atmpS4465 + _M0L6_2atmpS4466;
      _M0L1vS4451 = _M0L1pS1357->$2;
      _M0L6_2atmpS4454 = _M0L2dtS1383 / _M0L2cmS1359;
      _M0L6_2atmpS4453 = _M0L6_2atmpS4454 * _M0L7currentS1395;
      _M0L6_2atmpS4452 = _M0L1vS1373 + _M0L6_2atmpS4453;
      #line 171 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS4451, _M0L1iS1372, _M0L6_2atmpS4452);
      _M0L2geS4455 = _M0L1pS1357->$8;
      _M0L6_2atmpS4459 = -_M0L2geS1377;
      _M0L6_2atmpS4458 = _M0L6_2atmpS4459 / _M0L6tau__eS1367;
      _M0L6_2atmpS4457 = _M0L2dtS1383 * _M0L6_2atmpS4458;
      _M0L6_2atmpS4456 = _M0L2geS1377 + _M0L6_2atmpS4457;
      #line 173 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0MPC15array5Array3setGfE(_M0L2geS4455, _M0L1iS1372, _M0L6_2atmpS4456);
      _M0L2giS4460 = _M0L1pS1357->$9;
      _M0L6_2atmpS4464 = -_M0L2giS1378;
      _M0L6_2atmpS4463 = _M0L6_2atmpS4464 / _M0L6tau__iS1368;
      _M0L6_2atmpS4462 = _M0L2dtS1383 * _M0L6_2atmpS4463;
      _M0L6_2atmpS4461 = _M0L2giS1378 + _M0L6_2atmpS4462;
      #line 174 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0MPC15array5Array3setGfE(_M0L2giS4460, _M0L1iS1372, _M0L6_2atmpS4461);
      _M0L6_2atmpS4532 = _M0L1iS1372 + 1;
      _M0L1iS1372 = _M0L6_2atmpS4532;
      continue;
    }
    break;
  }
  _M0L7_2abindS1397 = 0;
  _M0L1iS1398 = _M0L7_2abindS1397;
  while (1) {
    if (_M0L1iS1398 < _M0L1nS1356) {
      struct _M0TPB5ArrayGbE* _M0L4fireS4533 = _M0L1pS1357->$6;
      struct _M0TPB5ArrayGfE* _M0L1vS4536 = _M0L1pS1357->$2;
      float _M0L6_2atmpS4535;
      int32_t _M0L6_2atmpS4534;
      int32_t _M0L6_2atmpS4537;
      #line 177 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0L6_2atmpS4535 = _M0MPC15array5Array2atGfE(_M0L1vS4536, _M0L1iS1398);
      _M0L6_2atmpS4534 = _M0L6_2atmpS4535 > -0x1.4p+4f;
      #line 177 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0MPC15array5Array3setGbE(_M0L4fireS4533, _M0L1iS1398, _M0L6_2atmpS4534);
      _M0L6_2atmpS4537 = _M0L1iS1398 + 1;
      _M0L1iS1398 = _M0L6_2atmpS4537;
      continue;
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt12step__hetrec(
  struct _M0TP26RiantR8snn__mbt6HetRec* _M0L1pS1327,
  float _M0L2dtS1335
) {
  int32_t _M0L1nS1326;
  struct _M0TP26RiantR8snn__mbt15HetRecParameter* _M0L5paramS4428;
  int32_t _M0L2ndS1328;
  int32_t _M0L8total__dS1329;
  struct _M0TP26RiantR8snn__mbt15HetRecParameter* _M0L5paramS4427;
  float _M0L9steepnessS1330;
  struct _M0TP26RiantR8snn__mbt15HetRecParameter* _M0L5paramS4426;
  float _M0L6tau__mS1331;
  struct _M0TP26RiantR8snn__mbt15HetRecParameter* _M0L5paramS4425;
  float _M0L9tau__rateS1332;
  struct _M0TP26RiantR8snn__mbt15HetRecParameter* _M0L5paramS4424;
  float _M0L8tau__absS1333;
  float _M0L6_2atmpS4423;
  int32_t _M0L11tabs__stepsS1334;
  int32_t _M0L7_2abindS1336;
  int32_t _M0L1iS1337;
  int32_t _M0L7_2abindS1340;
  int32_t _M0L1iS1341;
  int32_t _M0L7_2abindS1350;
  int32_t _M0L1iS1351;
  #line 214 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
  _M0L1nS1326 = _M0L1pS1327->$1;
  _M0L5paramS4428 = _M0L1pS1327->$0;
  _M0L2ndS1328 = _M0L5paramS4428->$0;
  _M0L8total__dS1329 = _M0L1nS1326 * _M0L2ndS1328;
  _M0L5paramS4427 = _M0L1pS1327->$0;
  _M0L9steepnessS1330 = _M0L5paramS4427->$7;
  _M0L5paramS4426 = _M0L1pS1327->$0;
  _M0L6tau__mS1331 = _M0L5paramS4426->$8;
  _M0L5paramS4425 = _M0L1pS1327->$0;
  _M0L9tau__rateS1332 = _M0L5paramS4425->$9;
  _M0L5paramS4424 = _M0L1pS1327->$0;
  _M0L8tau__absS1333 = _M0L5paramS4424->$6;
  _M0L6_2atmpS4423 = _M0L8tau__absS1333 / _M0L2dtS1335;
  #line 222 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
  _M0L11tabs__stepsS1334 = _M0MPC15float5Float7to__int(_M0L6_2atmpS4423);
  _M0L7_2abindS1336 = 0;
  _M0L1iS1337 = _M0L7_2abindS1336;
  while (1) {
    if (_M0L1iS1337 < _M0L8total__dS1329) {
      struct _M0TPB5ArrayGfE* _M0L6tau__dS4351 = _M0L1pS1327->$6;
      float _M0L7tau__diS1338;
      struct _M0TPB5ArrayGfE* _M0L4v__dS4339;
      struct _M0TPB5ArrayGfE* _M0L4v__dS4350;
      float _M0L6_2atmpS4341;
      struct _M0TPB5ArrayGfE* _M0L4v__dS4349;
      float _M0L6_2atmpS4348;
      float _M0L6_2atmpS4345;
      struct _M0TPB5ArrayGfE* _M0L4is__S4347;
      float _M0L6_2atmpS4346;
      float _M0L6_2atmpS4344;
      float _M0L6_2atmpS4343;
      float _M0L6_2atmpS4342;
      float _M0L6_2atmpS4340;
      int32_t _M0L6_2atmpS4352;
      #line 226 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
      _M0L7tau__diS1338
      = _M0MPC15array5Array2atGfE(_M0L6tau__dS4351, _M0L1iS1337);
      _M0L4v__dS4339 = _M0L1pS1327->$2;
      _M0L4v__dS4350 = _M0L1pS1327->$2;
      #line 227 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
      _M0L6_2atmpS4341
      = _M0MPC15array5Array2atGfE(_M0L4v__dS4350, _M0L1iS1337);
      _M0L4v__dS4349 = _M0L1pS1327->$2;
      #line 227 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
      _M0L6_2atmpS4348
      = _M0MPC15array5Array2atGfE(_M0L4v__dS4349, _M0L1iS1337);
      _M0L6_2atmpS4345 = -_M0L6_2atmpS4348;
      _M0L4is__S4347 = _M0L1pS1327->$4;
      #line 227 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
      _M0L6_2atmpS4346
      = _M0MPC15array5Array2atGfE(_M0L4is__S4347, _M0L1iS1337);
      _M0L6_2atmpS4344 = _M0L6_2atmpS4345 - _M0L6_2atmpS4346;
      _M0L6_2atmpS4343 = _M0L2dtS1335 * _M0L6_2atmpS4344;
      _M0L6_2atmpS4342 = _M0L6_2atmpS4343 / _M0L7tau__diS1338;
      _M0L6_2atmpS4340 = _M0L6_2atmpS4341 + _M0L6_2atmpS4342;
      #line 227 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
      _M0MPC15array5Array3setGfE(_M0L4v__dS4339, _M0L1iS1337, _M0L6_2atmpS4340);
      _M0L6_2atmpS4352 = _M0L1iS1337 + 1;
      _M0L1iS1337 = _M0L6_2atmpS4352;
      continue;
    }
    break;
  }
  _M0L7_2abindS1340 = 0;
  _M0L1iS1341 = _M0L7_2abindS1340;
  while (1) {
    if (_M0L1iS1341 < _M0L1nS1326) {
      struct _M0TPB5ArrayGiE* _M0L6colptrS4373 = _M0L1pS1327->$11;
      int32_t _M0L5startS1342;
      struct _M0TPB5ArrayGiE* _M0L6colptrS4371;
      int32_t _M0L6_2atmpS4372;
      int32_t _M0L3endS1343;
      float _M0L16dt__over__tau__mS1344;
      struct _M0TPB8MutLocalGiE* _M0L1sS1345;
      int32_t _M0L6_2atmpS4374;
      #line 232 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
      _M0L5startS1342
      = _M0MPC15array5Array2atGiE(_M0L6colptrS4373, _M0L1iS1341);
      _M0L6colptrS4371 = _M0L1pS1327->$11;
      _M0L6_2atmpS4372 = _M0L1iS1341 + 1;
      #line 233 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
      _M0L3endS1343
      = _M0MPC15array5Array2atGiE(_M0L6colptrS4371, _M0L6_2atmpS4372);
      _M0L16dt__over__tau__mS1344 = _M0L2dtS1335 / _M0L6tau__mS1331;
      _M0L1sS1345
      = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
      Moonbit_object_header(_M0L1sS1345)->meta
      = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
      _M0L1sS1345->$0 = _M0L5startS1342;
      while (1) {
        int32_t _M0L3valS4353 = _M0L1sS1345->$0;
        if (_M0L3valS4353 < _M0L3endS1343) {
          struct _M0TPB5ArrayGiE* _M0L6i__synS4369 = _M0L1pS1327->$12;
          int32_t _M0L3valS4370 = _M0L1sS1345->$0;
          int32_t _M0L9dend__idxS1346;
          struct _M0TPB5ArrayGfE* _M0L6w__synS4367;
          int32_t _M0L3valS4368;
          float _M0L1wS1347;
          struct _M0TPB5ArrayGfE* _M0L4v__sS4354;
          struct _M0TPB5ArrayGfE* _M0L4v__sS4364;
          float _M0L6_2atmpS4356;
          struct _M0TPB5ArrayGfE* _M0L4v__dS4363;
          float _M0L6_2atmpS4362;
          float _M0L6_2atmpS4359;
          struct _M0TPB5ArrayGfE* _M0L4v__sS4361;
          float _M0L6_2atmpS4360;
          float _M0L6_2atmpS4358;
          float _M0L6_2atmpS4357;
          float _M0L6_2atmpS4355;
          int32_t _M0L3valS4366;
          int32_t _M0L6_2atmpS4365;
          #line 237 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
          _M0L9dend__idxS1346
          = _M0MPC15array5Array2atGiE(_M0L6i__synS4369, _M0L3valS4370);
          _M0L6w__synS4367 = _M0L1pS1327->$13;
          _M0L3valS4368 = _M0L1sS1345->$0;
          #line 238 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
          _M0L1wS1347
          = _M0MPC15array5Array2atGfE(_M0L6w__synS4367, _M0L3valS4368);
          _M0L4v__sS4354 = _M0L1pS1327->$3;
          _M0L4v__sS4364 = _M0L1pS1327->$3;
          #line 239 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
          _M0L6_2atmpS4356
          = _M0MPC15array5Array2atGfE(_M0L4v__sS4364, _M0L1iS1341);
          _M0L4v__dS4363 = _M0L1pS1327->$2;
          #line 239 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
          _M0L6_2atmpS4362
          = _M0MPC15array5Array2atGfE(_M0L4v__dS4363, _M0L9dend__idxS1346);
          _M0L6_2atmpS4359 = _M0L1wS1347 * _M0L6_2atmpS4362;
          _M0L4v__sS4361 = _M0L1pS1327->$3;
          #line 239 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
          _M0L6_2atmpS4360
          = _M0MPC15array5Array2atGfE(_M0L4v__sS4361, _M0L1iS1341);
          _M0L6_2atmpS4358 = _M0L6_2atmpS4359 - _M0L6_2atmpS4360;
          _M0L6_2atmpS4357 = _M0L6_2atmpS4358 * _M0L16dt__over__tau__mS1344;
          _M0L6_2atmpS4355 = _M0L6_2atmpS4356 + _M0L6_2atmpS4357;
          #line 239 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
          _M0MPC15array5Array3setGfE(_M0L4v__sS4354, _M0L1iS1341, _M0L6_2atmpS4355);
          _M0L3valS4366 = _M0L1sS1345->$0;
          _M0L6_2atmpS4365 = _M0L3valS4366 + 1;
          _M0L1sS1345->$0 = _M0L6_2atmpS4365;
          continue;
        } else {
          moonbit_decref(_M0L1sS1345);
        }
        break;
      }
      _M0L6_2atmpS4374 = _M0L1iS1341 + 1;
      _M0L1iS1341 = _M0L6_2atmpS4374;
      continue;
    }
    break;
  }
  _M0L7_2abindS1350 = 0;
  _M0L1iS1351 = _M0L7_2abindS1350;
  while (1) {
    if (_M0L1iS1351 < _M0L1nS1326) {
      struct _M0TPB5ArrayGiE* _M0L4tabsS4376 = _M0L1pS1327->$8;
      struct _M0TPB5ArrayGiE* _M0L4tabsS4379 = _M0L1pS1327->$8;
      int32_t _M0L6_2atmpS4378;
      int32_t _M0L6_2atmpS4377;
      struct _M0TPB5ArrayGbE* _M0L4fireS4380;
      struct _M0TPB5ArrayGfE* _M0L5traceS4381;
      struct _M0TPB5ArrayGfE* _M0L5traceS4389;
      float _M0L6_2atmpS4383;
      struct _M0TPB5ArrayGfE* _M0L5traceS4388;
      float _M0L6_2atmpS4387;
      float _M0L6_2atmpS4386;
      float _M0L6_2atmpS4385;
      float _M0L6_2atmpS4384;
      float _M0L6_2atmpS4382;
      struct _M0TPB5ArrayGiE* _M0L4tabsS4391;
      int32_t _M0L6_2atmpS4390;
      struct _M0TPB5ArrayGfE* _M0L5traceS4392;
      struct _M0TPB5ArrayGfE* _M0L5traceS4401;
      float _M0L6_2atmpS4394;
      struct _M0TPB5ArrayGfE* _M0L4v__sS4400;
      float _M0L6_2atmpS4397;
      struct _M0TPB5ArrayGfE* _M0L5traceS4399;
      float _M0L6_2atmpS4398;
      float _M0L6_2atmpS4396;
      float _M0L6_2atmpS4395;
      float _M0L6_2atmpS4393;
      float _M0L6_2atmpS4417;
      struct _M0TPB5ArrayGfE* _M0L4v__sS4422;
      float _M0L6_2atmpS4419;
      struct _M0TPB5ArrayGfE* _M0L5traceS4421;
      float _M0L6_2atmpS4420;
      float _M0L6_2atmpS4418;
      float _M0L12sigmoid__argS1354;
      float _M0L4rateS1355;
      struct _M0TPB5ArrayGfE* _M0L9randcacheS4403;
      float _M0L6_2atmpS4402;
      int32_t _M0L6_2atmpS4375;
      #line 246 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
      _M0L6_2atmpS4378
      = _M0MPC15array5Array2atGiE(_M0L4tabsS4379, _M0L1iS1351);
      _M0L6_2atmpS4377 = _M0L6_2atmpS4378 - 1;
      #line 246 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
      _M0MPC15array5Array3setGiE(_M0L4tabsS4376, _M0L1iS1351, _M0L6_2atmpS4377);
      _M0L4fireS4380 = _M0L1pS1327->$7;
      #line 247 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
      _M0MPC15array5Array3setGbE(_M0L4fireS4380, _M0L1iS1351, 0);
      _M0L5traceS4381 = _M0L1pS1327->$9;
      _M0L5traceS4389 = _M0L1pS1327->$9;
      #line 249 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
      _M0L6_2atmpS4383
      = _M0MPC15array5Array2atGfE(_M0L5traceS4389, _M0L1iS1351);
      _M0L5traceS4388 = _M0L1pS1327->$9;
      #line 249 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
      _M0L6_2atmpS4387
      = _M0MPC15array5Array2atGfE(_M0L5traceS4388, _M0L1iS1351);
      _M0L6_2atmpS4386 = -_M0L6_2atmpS4387;
      _M0L6_2atmpS4385 = _M0L6_2atmpS4386 / _M0L9tau__rateS1332;
      _M0L6_2atmpS4384 = _M0L2dtS1335 * _M0L6_2atmpS4385;
      _M0L6_2atmpS4382 = _M0L6_2atmpS4383 + _M0L6_2atmpS4384;
      #line 249 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
      _M0MPC15array5Array3setGfE(_M0L5traceS4381, _M0L1iS1351, _M0L6_2atmpS4382);
      _M0L4tabsS4391 = _M0L1pS1327->$8;
      #line 250 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
      _M0L6_2atmpS4390
      = _M0MPC15array5Array2atGiE(_M0L4tabsS4391, _M0L1iS1351);
      if (_M0L6_2atmpS4390 > 0) {
        goto join_1352;
      }
      _M0L5traceS4392 = _M0L1pS1327->$9;
      _M0L5traceS4401 = _M0L1pS1327->$9;
      #line 254 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
      _M0L6_2atmpS4394
      = _M0MPC15array5Array2atGfE(_M0L5traceS4401, _M0L1iS1351);
      _M0L4v__sS4400 = _M0L1pS1327->$3;
      #line 254 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
      _M0L6_2atmpS4397
      = _M0MPC15array5Array2atGfE(_M0L4v__sS4400, _M0L1iS1351);
      _M0L5traceS4399 = _M0L1pS1327->$9;
      #line 254 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
      _M0L6_2atmpS4398
      = _M0MPC15array5Array2atGfE(_M0L5traceS4399, _M0L1iS1351);
      _M0L6_2atmpS4396 = _M0L6_2atmpS4397 - _M0L6_2atmpS4398;
      _M0L6_2atmpS4395 = _M0L6_2atmpS4396 / _M0L9tau__rateS1332;
      _M0L6_2atmpS4393 = _M0L6_2atmpS4394 + _M0L6_2atmpS4395;
      #line 254 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
      _M0MPC15array5Array3setGfE(_M0L5traceS4392, _M0L1iS1351, _M0L6_2atmpS4393);
      _M0L6_2atmpS4417 = -_M0L9steepnessS1330;
      _M0L4v__sS4422 = _M0L1pS1327->$3;
      #line 256 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
      _M0L6_2atmpS4419
      = _M0MPC15array5Array2atGfE(_M0L4v__sS4422, _M0L1iS1351);
      _M0L5traceS4421 = _M0L1pS1327->$9;
      #line 256 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
      _M0L6_2atmpS4420
      = _M0MPC15array5Array2atGfE(_M0L5traceS4421, _M0L1iS1351);
      _M0L6_2atmpS4418 = _M0L6_2atmpS4419 - _M0L6_2atmpS4420;
      _M0L12sigmoid__argS1354 = _M0L6_2atmpS4417 * _M0L6_2atmpS4418;
      if (_M0L12sigmoid__argS1354 > 0x1.6p+6f) {
        struct _M0TPB5ArrayGfE* _M0L1rS4411 = _M0L1pS1327->$5;
        float _M0L6_2atmpS4410;
        #line 259 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
        _M0L6_2atmpS4410
        = _M0MPC15array5Array2atGfE(_M0L1rS4411, _M0L1iS1351);
        _M0L4rateS1355 = _M0L6_2atmpS4410 * _M0L2dtS1335;
      } else if (_M0L12sigmoid__argS1354 < -0x1.6p+6f) {
        _M0L4rateS1355 = 0x0p+0f;
      } else {
        struct _M0TPB5ArrayGfE* _M0L1rS4416 = _M0L1pS1327->$5;
        float _M0L6_2atmpS4415;
        float _M0L6_2atmpS4412;
        float _M0L6_2atmpS4414;
        float _M0L6_2atmpS4413;
        #line 264 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
        _M0L6_2atmpS4415
        = _M0MPC15array5Array2atGfE(_M0L1rS4416, _M0L1iS1351);
        _M0L6_2atmpS4412 = _M0L6_2atmpS4415 * _M0L2dtS1335;
        #line 264 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
        _M0L6_2atmpS4414
        = _M0FP26RiantR8snn__mbt4expf(_M0L12sigmoid__argS1354);
        _M0L6_2atmpS4413 = 0x1p+0f + _M0L6_2atmpS4414;
        _M0L4rateS1355 = _M0L6_2atmpS4412 / _M0L6_2atmpS4413;
      }
      _M0L9randcacheS4403 = _M0L1pS1327->$10;
      #line 266 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
      _M0L6_2atmpS4402
      = _M0MPC15array5Array2atGfE(_M0L9randcacheS4403, _M0L1iS1351);
      if (_M0L6_2atmpS4402 < _M0L4rateS1355) {
        struct _M0TPB5ArrayGbE* _M0L4fireS4404 = _M0L1pS1327->$7;
        struct _M0TPB5ArrayGiE* _M0L4tabsS4405;
        struct _M0TPB5ArrayGfE* _M0L5traceS4406;
        struct _M0TPB5ArrayGfE* _M0L5traceS4409;
        float _M0L6_2atmpS4408;
        float _M0L6_2atmpS4407;
        #line 267 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
        _M0MPC15array5Array3setGbE(_M0L4fireS4404, _M0L1iS1351, 1);
        _M0L4tabsS4405 = _M0L1pS1327->$8;
        #line 268 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
        _M0MPC15array5Array3setGiE(_M0L4tabsS4405, _M0L1iS1351, _M0L11tabs__stepsS1334);
        _M0L5traceS4406 = _M0L1pS1327->$9;
        _M0L5traceS4409 = _M0L1pS1327->$9;
        #line 269 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
        _M0L6_2atmpS4408
        = _M0MPC15array5Array2atGfE(_M0L5traceS4409, _M0L1iS1351);
        _M0L6_2atmpS4407 = _M0L6_2atmpS4408 + 0x1p+0f;
        #line 269 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
        _M0MPC15array5Array3setGfE(_M0L5traceS4406, _M0L1iS1351, _M0L6_2atmpS4407);
      }
      goto join_1352;
      goto joinlet_5369;
      join_1352:;
      _M0L6_2atmpS4375 = _M0L1iS1351 + 1;
      _M0L1iS1351 = _M0L6_2atmpS4375;
      continue;
      joinlet_5369:;
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt18step__adex__sinexp(
  struct _M0TP26RiantR8snn__mbt10AdExSinExp* _M0L1pS1305,
  float _M0L2dtS1320
) {
  int32_t _M0L1nS1304;
  struct _M0TP26RiantR8snn__mbt19AdExSinExpParameter* _M0L3p__S1306;
  float _M0L2tmS1307;
  float _M0L2vtS1308;
  float _M0L2vrS1309;
  float _M0L2elS1310;
  float _M0L1rS1311;
  float _M0L9dt__slopeS1312;
  float _M0L2twS1313;
  float _M0L1aS1314;
  float _M0L1bS1315;
  struct _M0TP26RiantR8snn__mbt13AdExPostSpike* _M0L5spikeS4338;
  float _M0L2atS1316;
  struct _M0TP26RiantR8snn__mbt13AdExPostSpike* _M0L5spikeS4337;
  float _M0L6tau__aS1317;
  struct _M0TP26RiantR8snn__mbt13AdExPostSpike* _M0L5spikeS4336;
  float _M0L11tabs__constS1318;
  float _M0L6_2atmpS4335;
  int32_t _M0L11tabs__stepsS1319;
  int32_t _M0L7_2abindS1321;
  int32_t _M0L1iS1322;
  #line 190 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
  _M0L1nS1304 = _M0L1pS1305->$2;
  _M0L3p__S1306 = _M0L1pS1305->$0;
  _M0L2tmS1307 = _M0L3p__S1306->$5;
  _M0L2vtS1308 = _M0L3p__S1306->$2;
  _M0L2vrS1309 = _M0L3p__S1306->$3;
  _M0L2elS1310 = _M0L3p__S1306->$4;
  _M0L1rS1311 = _M0L3p__S1306->$6;
  _M0L9dt__slopeS1312 = _M0L3p__S1306->$7;
  _M0L2twS1313 = _M0L3p__S1306->$8;
  _M0L1aS1314 = _M0L3p__S1306->$9;
  _M0L1bS1315 = _M0L3p__S1306->$10;
  _M0L5spikeS4338 = _M0L1pS1305->$1;
  _M0L2atS1316 = _M0L5spikeS4338->$0;
  _M0L5spikeS4337 = _M0L1pS1305->$1;
  _M0L6tau__aS1317 = _M0L5spikeS4337->$1;
  _M0L5spikeS4336 = _M0L1pS1305->$1;
  _M0L11tabs__constS1318 = _M0L5spikeS4336->$3;
  _M0L6_2atmpS4335 = _M0L11tabs__constS1318 / _M0L2dtS1320;
  #line 205 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
  _M0L11tabs__stepsS1319 = _M0MPC15float5Float7to__int(_M0L6_2atmpS4335);
  _M0L7_2abindS1321 = 0;
  _M0L1iS1322 = _M0L7_2abindS1321;
  while (1) {
    if (_M0L1iS1322 < _M0L1nS1304) {
      struct _M0TPB5ArrayGfE* _M0L1vS4248 = _M0L1pS1305->$3;
      struct _M0TPB5ArrayGbE* _M0L4fireS4250 = _M0L1pS1305->$5;
      float _M0L6_2atmpS4249;
      struct _M0TPB5ArrayGbE* _M0L4fireS4252;
      struct _M0TPB5ArrayGiE* _M0L4tabsS4253;
      struct _M0TPB5ArrayGiE* _M0L4tabsS4256;
      int32_t _M0L6_2atmpS4255;
      int32_t _M0L6_2atmpS4254;
      struct _M0TPB5ArrayGiE* _M0L4tabsS4258;
      int32_t _M0L6_2atmpS4257;
      struct _M0TPB5ArrayGfE* _M0L1wS4259;
      struct _M0TPB5ArrayGfE* _M0L1wS4271;
      float _M0L6_2atmpS4261;
      struct _M0TPB5ArrayGfE* _M0L1vS4270;
      float _M0L6_2atmpS4269;
      float _M0L6_2atmpS4268;
      float _M0L6_2atmpS4265;
      struct _M0TPB5ArrayGfE* _M0L1wS4267;
      float _M0L6_2atmpS4266;
      float _M0L6_2atmpS4264;
      float _M0L6_2atmpS4263;
      float _M0L6_2atmpS4262;
      float _M0L6_2atmpS4260;
      float _M0L9exp__termS1325;
      struct _M0TPB5ArrayGfE* _M0L1vS4272;
      struct _M0TPB5ArrayGfE* _M0L1vS4294;
      float _M0L6_2atmpS4274;
      struct _M0TPB5ArrayGfE* _M0L1vS4293;
      float _M0L6_2atmpS4292;
      float _M0L6_2atmpS4291;
      float _M0L6_2atmpS4290;
      float _M0L6_2atmpS4286;
      struct _M0TPB5ArrayGfE* _M0L9syn__currS4289;
      float _M0L6_2atmpS4288;
      float _M0L6_2atmpS4287;
      float _M0L6_2atmpS4282;
      struct _M0TPB5ArrayGfE* _M0L1wS4285;
      float _M0L6_2atmpS4284;
      float _M0L6_2atmpS4283;
      float _M0L6_2atmpS4278;
      struct _M0TPB5ArrayGfE* _M0L1iS4281;
      float _M0L6_2atmpS4280;
      float _M0L6_2atmpS4279;
      float _M0L6_2atmpS4277;
      float _M0L6_2atmpS4276;
      float _M0L6_2atmpS4275;
      float _M0L6_2atmpS4273;
      struct _M0TPB5ArrayGfE* _M0L9thresholdS4295;
      struct _M0TPB5ArrayGfE* _M0L9thresholdS4303;
      float _M0L6_2atmpS4297;
      struct _M0TPB5ArrayGfE* _M0L9thresholdS4302;
      float _M0L6_2atmpS4301;
      float _M0L6_2atmpS4300;
      float _M0L6_2atmpS4299;
      float _M0L6_2atmpS4298;
      float _M0L6_2atmpS4296;
      struct _M0TPB5ArrayGbE* _M0L4fireS4304;
      struct _M0TPB5ArrayGfE* _M0L1vS4307;
      float _M0L6_2atmpS4306;
      int32_t _M0L6_2atmpS4305;
      struct _M0TPB5ArrayGfE* _M0L1vS4308;
      struct _M0TPB5ArrayGbE* _M0L4fireS4310;
      float _M0L6_2atmpS4309;
      struct _M0TPB5ArrayGfE* _M0L1wS4312;
      struct _M0TPB5ArrayGbE* _M0L4fireS4314;
      float _M0L6_2atmpS4313;
      struct _M0TPB5ArrayGfE* _M0L9thresholdS4318;
      struct _M0TPB5ArrayGbE* _M0L4fireS4320;
      float _M0L6_2atmpS4319;
      struct _M0TPB5ArrayGiE* _M0L4tabsS4324;
      struct _M0TPB5ArrayGbE* _M0L4fireS4326;
      int32_t _M0L6_2atmpS4325;
      int32_t _M0L6_2atmpS4247;
      #line 209 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS4250, _M0L1iS1322)) {
        _M0L6_2atmpS4249 = _M0L2vrS1309;
      } else {
        struct _M0TPB5ArrayGfE* _M0L1vS4251 = _M0L1pS1305->$3;
        #line 209 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
        _M0L6_2atmpS4249
        = _M0MPC15array5Array2atGfE(_M0L1vS4251, _M0L1iS1322);
      }
      #line 209 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS4248, _M0L1iS1322, _M0L6_2atmpS4249);
      _M0L4fireS4252 = _M0L1pS1305->$5;
      #line 212 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0MPC15array5Array3setGbE(_M0L4fireS4252, _M0L1iS1322, 0);
      _M0L4tabsS4253 = _M0L1pS1305->$7;
      _M0L4tabsS4256 = _M0L1pS1305->$7;
      #line 213 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS4255
      = _M0MPC15array5Array2atGiE(_M0L4tabsS4256, _M0L1iS1322);
      _M0L6_2atmpS4254 = _M0L6_2atmpS4255 - 1;
      #line 213 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0MPC15array5Array3setGiE(_M0L4tabsS4253, _M0L1iS1322, _M0L6_2atmpS4254);
      _M0L4tabsS4258 = _M0L1pS1305->$7;
      #line 214 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS4257
      = _M0MPC15array5Array2atGiE(_M0L4tabsS4258, _M0L1iS1322);
      if (_M0L6_2atmpS4257 > 0) {
        goto join_1323;
      }
      _M0L1wS4259 = _M0L1pS1305->$4;
      _M0L1wS4271 = _M0L1pS1305->$4;
      #line 219 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS4261 = _M0MPC15array5Array2atGfE(_M0L1wS4271, _M0L1iS1322);
      _M0L1vS4270 = _M0L1pS1305->$3;
      #line 219 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS4269 = _M0MPC15array5Array2atGfE(_M0L1vS4270, _M0L1iS1322);
      _M0L6_2atmpS4268 = _M0L6_2atmpS4269 - _M0L2elS1310;
      _M0L6_2atmpS4265 = _M0L1aS1314 * _M0L6_2atmpS4268;
      _M0L1wS4267 = _M0L1pS1305->$4;
      #line 219 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS4266 = _M0MPC15array5Array2atGfE(_M0L1wS4267, _M0L1iS1322);
      _M0L6_2atmpS4264 = _M0L6_2atmpS4265 - _M0L6_2atmpS4266;
      _M0L6_2atmpS4263 = _M0L2dtS1320 * _M0L6_2atmpS4264;
      _M0L6_2atmpS4262 = _M0L6_2atmpS4263 / _M0L2twS1313;
      _M0L6_2atmpS4260 = _M0L6_2atmpS4261 + _M0L6_2atmpS4262;
      #line 219 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0MPC15array5Array3setGfE(_M0L1wS4259, _M0L1iS1322, _M0L6_2atmpS4260);
      if (_M0L9dt__slopeS1312 < 0x0p+0f) {
        _M0L9exp__termS1325 = 0x0p+0f;
      } else {
        struct _M0TPB5ArrayGfE* _M0L1vS4334 = _M0L1pS1305->$3;
        float _M0L6_2atmpS4331;
        struct _M0TPB5ArrayGfE* _M0L9thresholdS4333;
        float _M0L6_2atmpS4332;
        float _M0L6_2atmpS4330;
        float _M0L6_2atmpS4329;
        float _M0L6_2atmpS4328;
        #line 225 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
        _M0L6_2atmpS4331
        = _M0MPC15array5Array2atGfE(_M0L1vS4334, _M0L1iS1322);
        _M0L9thresholdS4333 = _M0L1pS1305->$6;
        #line 225 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
        _M0L6_2atmpS4332
        = _M0MPC15array5Array2atGfE(_M0L9thresholdS4333, _M0L1iS1322);
        _M0L6_2atmpS4330 = _M0L6_2atmpS4331 - _M0L6_2atmpS4332;
        _M0L6_2atmpS4329 = _M0L6_2atmpS4330 / _M0L9dt__slopeS1312;
        #line 225 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
        _M0L6_2atmpS4328 = _M0FP26RiantR8snn__mbt4expf(_M0L6_2atmpS4329);
        _M0L9exp__termS1325 = _M0L9dt__slopeS1312 * _M0L6_2atmpS4328;
      }
      _M0L1vS4272 = _M0L1pS1305->$3;
      _M0L1vS4294 = _M0L1pS1305->$3;
      #line 227 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS4274 = _M0MPC15array5Array2atGfE(_M0L1vS4294, _M0L1iS1322);
      _M0L1vS4293 = _M0L1pS1305->$3;
      #line 230 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS4292 = _M0MPC15array5Array2atGfE(_M0L1vS4293, _M0L1iS1322);
      _M0L6_2atmpS4291 = _M0L6_2atmpS4292 - _M0L2elS1310;
      _M0L6_2atmpS4290 = -_M0L6_2atmpS4291;
      _M0L6_2atmpS4286 = _M0L6_2atmpS4290 + _M0L9exp__termS1325;
      _M0L9syn__currS4289 = _M0L1pS1305->$9;
      #line 230 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS4288
      = _M0MPC15array5Array2atGfE(_M0L9syn__currS4289, _M0L1iS1322);
      _M0L6_2atmpS4287 = _M0L1rS1311 * _M0L6_2atmpS4288;
      _M0L6_2atmpS4282 = _M0L6_2atmpS4286 - _M0L6_2atmpS4287;
      _M0L1wS4285 = _M0L1pS1305->$4;
      #line 230 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS4284 = _M0MPC15array5Array2atGfE(_M0L1wS4285, _M0L1iS1322);
      _M0L6_2atmpS4283 = _M0L1rS1311 * _M0L6_2atmpS4284;
      _M0L6_2atmpS4278 = _M0L6_2atmpS4282 - _M0L6_2atmpS4283;
      _M0L1iS4281 = _M0L1pS1305->$8;
      #line 231 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS4280 = _M0MPC15array5Array2atGfE(_M0L1iS4281, _M0L1iS1322);
      _M0L6_2atmpS4279 = _M0L1rS1311 * _M0L6_2atmpS4280;
      _M0L6_2atmpS4277 = _M0L6_2atmpS4278 + _M0L6_2atmpS4279;
      _M0L6_2atmpS4276 = _M0L2dtS1320 * _M0L6_2atmpS4277;
      _M0L6_2atmpS4275 = _M0L6_2atmpS4276 / _M0L2tmS1307;
      _M0L6_2atmpS4273 = _M0L6_2atmpS4274 + _M0L6_2atmpS4275;
      #line 227 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS4272, _M0L1iS1322, _M0L6_2atmpS4273);
      _M0L9thresholdS4295 = _M0L1pS1305->$6;
      _M0L9thresholdS4303 = _M0L1pS1305->$6;
      #line 235 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS4297
      = _M0MPC15array5Array2atGfE(_M0L9thresholdS4303, _M0L1iS1322);
      _M0L9thresholdS4302 = _M0L1pS1305->$6;
      #line 235 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS4301
      = _M0MPC15array5Array2atGfE(_M0L9thresholdS4302, _M0L1iS1322);
      _M0L6_2atmpS4300 = _M0L2vtS1308 - _M0L6_2atmpS4301;
      _M0L6_2atmpS4299 = _M0L2dtS1320 * _M0L6_2atmpS4300;
      _M0L6_2atmpS4298 = _M0L6_2atmpS4299 / _M0L6tau__aS1317;
      _M0L6_2atmpS4296 = _M0L6_2atmpS4297 + _M0L6_2atmpS4298;
      #line 235 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0MPC15array5Array3setGfE(_M0L9thresholdS4295, _M0L1iS1322, _M0L6_2atmpS4296);
      _M0L4fireS4304 = _M0L1pS1305->$5;
      _M0L1vS4307 = _M0L1pS1305->$3;
      #line 238 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS4306 = _M0MPC15array5Array2atGfE(_M0L1vS4307, _M0L1iS1322);
      _M0L6_2atmpS4305 = _M0L6_2atmpS4306 >= 0x0p+0f;
      #line 238 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0MPC15array5Array3setGbE(_M0L4fireS4304, _M0L1iS1322, _M0L6_2atmpS4305);
      _M0L1vS4308 = _M0L1pS1305->$3;
      _M0L4fireS4310 = _M0L1pS1305->$5;
      #line 239 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS4310, _M0L1iS1322)) {
        _M0L6_2atmpS4309 = 0x1.4p+4f;
      } else {
        struct _M0TPB5ArrayGfE* _M0L1vS4311 = _M0L1pS1305->$3;
        #line 239 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
        _M0L6_2atmpS4309
        = _M0MPC15array5Array2atGfE(_M0L1vS4311, _M0L1iS1322);
      }
      #line 239 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS4308, _M0L1iS1322, _M0L6_2atmpS4309);
      _M0L1wS4312 = _M0L1pS1305->$4;
      _M0L4fireS4314 = _M0L1pS1305->$5;
      #line 240 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS4314, _M0L1iS1322)) {
        struct _M0TPB5ArrayGfE* _M0L1wS4316 = _M0L1pS1305->$4;
        float _M0L6_2atmpS4315;
        #line 240 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
        _M0L6_2atmpS4315
        = _M0MPC15array5Array2atGfE(_M0L1wS4316, _M0L1iS1322);
        _M0L6_2atmpS4313 = _M0L6_2atmpS4315 + _M0L1bS1315;
      } else {
        struct _M0TPB5ArrayGfE* _M0L1wS4317 = _M0L1pS1305->$4;
        #line 240 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
        _M0L6_2atmpS4313
        = _M0MPC15array5Array2atGfE(_M0L1wS4317, _M0L1iS1322);
      }
      #line 240 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0MPC15array5Array3setGfE(_M0L1wS4312, _M0L1iS1322, _M0L6_2atmpS4313);
      _M0L9thresholdS4318 = _M0L1pS1305->$6;
      _M0L4fireS4320 = _M0L1pS1305->$5;
      #line 241 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS4320, _M0L1iS1322)) {
        struct _M0TPB5ArrayGfE* _M0L9thresholdS4322 = _M0L1pS1305->$6;
        float _M0L6_2atmpS4321;
        #line 241 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
        _M0L6_2atmpS4321
        = _M0MPC15array5Array2atGfE(_M0L9thresholdS4322, _M0L1iS1322);
        _M0L6_2atmpS4319 = _M0L6_2atmpS4321 + _M0L2atS1316;
      } else {
        struct _M0TPB5ArrayGfE* _M0L9thresholdS4323 = _M0L1pS1305->$6;
        #line 241 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
        _M0L6_2atmpS4319
        = _M0MPC15array5Array2atGfE(_M0L9thresholdS4323, _M0L1iS1322);
      }
      #line 241 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0MPC15array5Array3setGfE(_M0L9thresholdS4318, _M0L1iS1322, _M0L6_2atmpS4319);
      _M0L4tabsS4324 = _M0L1pS1305->$7;
      _M0L4fireS4326 = _M0L1pS1305->$5;
      #line 242 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS4326, _M0L1iS1322)) {
        _M0L6_2atmpS4325 = _M0L11tabs__stepsS1319;
      } else {
        struct _M0TPB5ArrayGiE* _M0L4tabsS4327 = _M0L1pS1305->$7;
        #line 242 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
        _M0L6_2atmpS4325
        = _M0MPC15array5Array2atGiE(_M0L4tabsS4327, _M0L1iS1322);
      }
      #line 242 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0MPC15array5Array3setGiE(_M0L4tabsS4324, _M0L1iS1322, _M0L6_2atmpS4325);
      goto join_1323;
      goto joinlet_5371;
      join_1323:;
      _M0L6_2atmpS4247 = _M0L1iS1322 + 1;
      _M0L1iS1322 = _M0L6_2atmpS4247;
      continue;
      joinlet_5371:;
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt31adex__sinexp__synaptic__current(
  struct _M0TP26RiantR8snn__mbt10AdExSinExp* _M0L1pS1300
) {
  int32_t _M0L1nS1299;
  int32_t _M0L7_2abindS1301;
  int32_t _M0L1iS1302;
  #line 177 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
  _M0L1nS1299 = _M0L1pS1300->$2;
  _M0L7_2abindS1301 = 0;
  _M0L1iS1302 = _M0L7_2abindS1301;
  while (1) {
    if (_M0L1iS1302 < _M0L1nS1299) {
      struct _M0TPB5ArrayGfE* _M0L9syn__currS4224 = _M0L1pS1300->$9;
      struct _M0TPB5ArrayGfE* _M0L2geS4245 = _M0L1pS1300->$10;
      float _M0L6_2atmpS4240;
      struct _M0TPB5ArrayGfE* _M0L1vS4244;
      float _M0L6_2atmpS4242;
      float _M0L4e__eS4243;
      float _M0L6_2atmpS4241;
      float _M0L6_2atmpS4237;
      struct _M0TPB5ArrayGfE* _M0L7gsyn__eS4239;
      float _M0L6_2atmpS4238;
      float _M0L6_2atmpS4226;
      struct _M0TPB5ArrayGfE* _M0L2giS4236;
      float _M0L6_2atmpS4231;
      struct _M0TPB5ArrayGfE* _M0L1vS4235;
      float _M0L6_2atmpS4233;
      float _M0L4e__iS4234;
      float _M0L6_2atmpS4232;
      float _M0L6_2atmpS4228;
      struct _M0TPB5ArrayGfE* _M0L7gsyn__iS4230;
      float _M0L6_2atmpS4229;
      float _M0L6_2atmpS4227;
      float _M0L6_2atmpS4225;
      int32_t _M0L6_2atmpS4246;
      #line 180 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS4240 = _M0MPC15array5Array2atGfE(_M0L2geS4245, _M0L1iS1302);
      _M0L1vS4244 = _M0L1pS1300->$3;
      #line 180 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS4242 = _M0MPC15array5Array2atGfE(_M0L1vS4244, _M0L1iS1302);
      _M0L4e__eS4243 = _M0L1pS1300->$16;
      _M0L6_2atmpS4241 = _M0L6_2atmpS4242 - _M0L4e__eS4243;
      _M0L6_2atmpS4237 = _M0L6_2atmpS4240 * _M0L6_2atmpS4241;
      _M0L7gsyn__eS4239 = _M0L1pS1300->$14;
      #line 180 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS4238
      = _M0MPC15array5Array2atGfE(_M0L7gsyn__eS4239, _M0L1iS1302);
      _M0L6_2atmpS4226 = _M0L6_2atmpS4237 * _M0L6_2atmpS4238;
      _M0L2giS4236 = _M0L1pS1300->$11;
      #line 181 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS4231 = _M0MPC15array5Array2atGfE(_M0L2giS4236, _M0L1iS1302);
      _M0L1vS4235 = _M0L1pS1300->$3;
      #line 181 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS4233 = _M0MPC15array5Array2atGfE(_M0L1vS4235, _M0L1iS1302);
      _M0L4e__iS4234 = _M0L1pS1300->$17;
      _M0L6_2atmpS4232 = _M0L6_2atmpS4233 - _M0L4e__iS4234;
      _M0L6_2atmpS4228 = _M0L6_2atmpS4231 * _M0L6_2atmpS4232;
      _M0L7gsyn__iS4230 = _M0L1pS1300->$15;
      #line 181 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS4229
      = _M0MPC15array5Array2atGfE(_M0L7gsyn__iS4230, _M0L1iS1302);
      _M0L6_2atmpS4227 = _M0L6_2atmpS4228 * _M0L6_2atmpS4229;
      _M0L6_2atmpS4225 = _M0L6_2atmpS4226 + _M0L6_2atmpS4227;
      #line 180 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0MPC15array5Array3setGfE(_M0L9syn__currS4224, _M0L1iS1302, _M0L6_2atmpS4225);
      _M0L6_2atmpS4246 = _M0L1iS1302 + 1;
      _M0L1iS1302 = _M0L6_2atmpS4246;
      continue;
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt28adex__sinexp__step__synapses(
  struct _M0TP26RiantR8snn__mbt10AdExSinExp* _M0L1pS1289,
  float _M0L2dtS1294
) {
  int32_t _M0L1nS1288;
  float _M0L6tau__eS1290;
  float _M0L6tau__iS1291;
  int32_t _M0L7_2abindS1292;
  int32_t _M0L1iS1293;
  int32_t _M0L7_2abindS1296;
  int32_t _M0L1iS1297;
  #line 154 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
  _M0L1nS1288 = _M0L1pS1289->$2;
  _M0L6tau__eS1290 = _M0L1pS1289->$18;
  _M0L6tau__iS1291 = _M0L1pS1289->$19;
  _M0L7_2abindS1292 = 0;
  _M0L1iS1293 = _M0L7_2abindS1292;
  while (1) {
    if (_M0L1iS1293 < _M0L1nS1288) {
      struct _M0TPB5ArrayGfE* _M0L2geS4190 = _M0L1pS1289->$10;
      struct _M0TPB5ArrayGfE* _M0L2geS4195 = _M0L1pS1289->$10;
      float _M0L6_2atmpS4192;
      struct _M0TPB5ArrayGfE* _M0L3gluS4194;
      float _M0L6_2atmpS4193;
      float _M0L6_2atmpS4191;
      struct _M0TPB5ArrayGfE* _M0L2giS4196;
      struct _M0TPB5ArrayGfE* _M0L2giS4201;
      float _M0L6_2atmpS4198;
      struct _M0TPB5ArrayGfE* _M0L4gabaS4200;
      float _M0L6_2atmpS4199;
      float _M0L6_2atmpS4197;
      struct _M0TPB5ArrayGfE* _M0L2geS4202;
      struct _M0TPB5ArrayGfE* _M0L2geS4210;
      float _M0L6_2atmpS4204;
      struct _M0TPB5ArrayGfE* _M0L2geS4209;
      float _M0L6_2atmpS4208;
      float _M0L6_2atmpS4207;
      float _M0L6_2atmpS4206;
      float _M0L6_2atmpS4205;
      float _M0L6_2atmpS4203;
      struct _M0TPB5ArrayGfE* _M0L2giS4211;
      struct _M0TPB5ArrayGfE* _M0L2giS4219;
      float _M0L6_2atmpS4213;
      struct _M0TPB5ArrayGfE* _M0L2giS4218;
      float _M0L6_2atmpS4217;
      float _M0L6_2atmpS4216;
      float _M0L6_2atmpS4215;
      float _M0L6_2atmpS4214;
      float _M0L6_2atmpS4212;
      int32_t _M0L6_2atmpS4220;
      #line 160 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS4192 = _M0MPC15array5Array2atGfE(_M0L2geS4195, _M0L1iS1293);
      _M0L3gluS4194 = _M0L1pS1289->$12;
      #line 160 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS4193
      = _M0MPC15array5Array2atGfE(_M0L3gluS4194, _M0L1iS1293);
      _M0L6_2atmpS4191 = _M0L6_2atmpS4192 + _M0L6_2atmpS4193;
      #line 160 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0MPC15array5Array3setGfE(_M0L2geS4190, _M0L1iS1293, _M0L6_2atmpS4191);
      _M0L2giS4196 = _M0L1pS1289->$11;
      _M0L2giS4201 = _M0L1pS1289->$11;
      #line 161 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS4198 = _M0MPC15array5Array2atGfE(_M0L2giS4201, _M0L1iS1293);
      _M0L4gabaS4200 = _M0L1pS1289->$13;
      #line 161 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS4199
      = _M0MPC15array5Array2atGfE(_M0L4gabaS4200, _M0L1iS1293);
      _M0L6_2atmpS4197 = _M0L6_2atmpS4198 + _M0L6_2atmpS4199;
      #line 161 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0MPC15array5Array3setGfE(_M0L2giS4196, _M0L1iS1293, _M0L6_2atmpS4197);
      _M0L2geS4202 = _M0L1pS1289->$10;
      _M0L2geS4210 = _M0L1pS1289->$10;
      #line 162 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS4204 = _M0MPC15array5Array2atGfE(_M0L2geS4210, _M0L1iS1293);
      _M0L2geS4209 = _M0L1pS1289->$10;
      #line 162 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS4208 = _M0MPC15array5Array2atGfE(_M0L2geS4209, _M0L1iS1293);
      _M0L6_2atmpS4207 = -_M0L6_2atmpS4208;
      _M0L6_2atmpS4206 = _M0L6_2atmpS4207 / _M0L6tau__eS1290;
      _M0L6_2atmpS4205 = _M0L2dtS1294 * _M0L6_2atmpS4206;
      _M0L6_2atmpS4203 = _M0L6_2atmpS4204 + _M0L6_2atmpS4205;
      #line 162 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0MPC15array5Array3setGfE(_M0L2geS4202, _M0L1iS1293, _M0L6_2atmpS4203);
      _M0L2giS4211 = _M0L1pS1289->$11;
      _M0L2giS4219 = _M0L1pS1289->$11;
      #line 163 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS4213 = _M0MPC15array5Array2atGfE(_M0L2giS4219, _M0L1iS1293);
      _M0L2giS4218 = _M0L1pS1289->$11;
      #line 163 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS4217 = _M0MPC15array5Array2atGfE(_M0L2giS4218, _M0L1iS1293);
      _M0L6_2atmpS4216 = -_M0L6_2atmpS4217;
      _M0L6_2atmpS4215 = _M0L6_2atmpS4216 / _M0L6tau__iS1291;
      _M0L6_2atmpS4214 = _M0L2dtS1294 * _M0L6_2atmpS4215;
      _M0L6_2atmpS4212 = _M0L6_2atmpS4213 + _M0L6_2atmpS4214;
      #line 163 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0MPC15array5Array3setGfE(_M0L2giS4211, _M0L1iS1293, _M0L6_2atmpS4212);
      _M0L6_2atmpS4220 = _M0L1iS1293 + 1;
      _M0L1iS1293 = _M0L6_2atmpS4220;
      continue;
    }
    break;
  }
  _M0L7_2abindS1296 = 0;
  _M0L1iS1297 = _M0L7_2abindS1296;
  while (1) {
    if (_M0L1iS1297 < _M0L1nS1288) {
      struct _M0TPB5ArrayGfE* _M0L3gluS4221 = _M0L1pS1289->$12;
      struct _M0TPB5ArrayGfE* _M0L4gabaS4222;
      int32_t _M0L6_2atmpS4223;
      #line 166 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0MPC15array5Array3setGfE(_M0L3gluS4221, _M0L1iS1297, 0x0p+0f);
      _M0L4gabaS4222 = _M0L1pS1289->$13;
      #line 167 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0MPC15array5Array3setGfE(_M0L4gabaS4222, _M0L1iS1297, 0x0p+0f);
      _M0L6_2atmpS4223 = _M0L1iS1297 + 1;
      _M0L1iS1297 = _M0L6_2atmpS4223;
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

int32_t _M0FP26RiantR8snn__mbt10step__adex(
  struct _M0TP26RiantR8snn__mbt4AdEx* _M0L1pS1267,
  float _M0L2dtS1282
) {
  int32_t _M0L1nS1266;
  struct _M0TP26RiantR8snn__mbt13AdExParameter* _M0L3p__S1268;
  float _M0L2tmS1269;
  float _M0L2vtS1270;
  float _M0L2vrS1271;
  float _M0L2elS1272;
  float _M0L1rS1273;
  float _M0L9dt__slopeS1274;
  float _M0L2twS1275;
  float _M0L1aS1276;
  float _M0L1bS1277;
  struct _M0TP26RiantR8snn__mbt13AdExPostSpike* _M0L5spikeS4189;
  float _M0L2atS1278;
  struct _M0TP26RiantR8snn__mbt13AdExPostSpike* _M0L5spikeS4188;
  float _M0L6tau__aS1279;
  struct _M0TP26RiantR8snn__mbt13AdExPostSpike* _M0L5spikeS4187;
  float _M0L11tabs__constS1280;
  float _M0L6_2atmpS4186;
  int32_t _M0L11tabs__stepsS1281;
  int32_t _M0L7_2abindS1283;
  int32_t _M0L1iS1284;
  #line 182 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
  _M0L1nS1266 = _M0L1pS1267->$2;
  _M0L3p__S1268 = _M0L1pS1267->$0;
  _M0L2tmS1269 = _M0L3p__S1268->$5;
  _M0L2vtS1270 = _M0L3p__S1268->$2;
  _M0L2vrS1271 = _M0L3p__S1268->$3;
  _M0L2elS1272 = _M0L3p__S1268->$4;
  _M0L1rS1273 = _M0L3p__S1268->$6;
  _M0L9dt__slopeS1274 = _M0L3p__S1268->$7;
  _M0L2twS1275 = _M0L3p__S1268->$8;
  _M0L1aS1276 = _M0L3p__S1268->$9;
  _M0L1bS1277 = _M0L3p__S1268->$10;
  _M0L5spikeS4189 = _M0L1pS1267->$1;
  _M0L2atS1278 = _M0L5spikeS4189->$0;
  _M0L5spikeS4188 = _M0L1pS1267->$1;
  _M0L6tau__aS1279 = _M0L5spikeS4188->$1;
  _M0L5spikeS4187 = _M0L1pS1267->$1;
  _M0L11tabs__constS1280 = _M0L5spikeS4187->$3;
  _M0L6_2atmpS4186 = _M0L11tabs__constS1280 / _M0L2dtS1282;
  #line 197 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
  _M0L11tabs__stepsS1281 = _M0MPC15float5Float7to__int(_M0L6_2atmpS4186);
  _M0L7_2abindS1283 = 0;
  _M0L1iS1284 = _M0L7_2abindS1283;
  while (1) {
    if (_M0L1iS1284 < _M0L1nS1266) {
      struct _M0TPB5ArrayGfE* _M0L1vS4099 = _M0L1pS1267->$3;
      struct _M0TPB5ArrayGbE* _M0L4fireS4101 = _M0L1pS1267->$5;
      float _M0L6_2atmpS4100;
      struct _M0TPB5ArrayGbE* _M0L4fireS4103;
      struct _M0TPB5ArrayGiE* _M0L4tabsS4104;
      struct _M0TPB5ArrayGiE* _M0L4tabsS4107;
      int32_t _M0L6_2atmpS4106;
      int32_t _M0L6_2atmpS4105;
      struct _M0TPB5ArrayGiE* _M0L4tabsS4109;
      int32_t _M0L6_2atmpS4108;
      struct _M0TPB5ArrayGfE* _M0L1wS4110;
      struct _M0TPB5ArrayGfE* _M0L1wS4122;
      float _M0L6_2atmpS4112;
      struct _M0TPB5ArrayGfE* _M0L1vS4121;
      float _M0L6_2atmpS4120;
      float _M0L6_2atmpS4119;
      float _M0L6_2atmpS4116;
      struct _M0TPB5ArrayGfE* _M0L1wS4118;
      float _M0L6_2atmpS4117;
      float _M0L6_2atmpS4115;
      float _M0L6_2atmpS4114;
      float _M0L6_2atmpS4113;
      float _M0L6_2atmpS4111;
      float _M0L9exp__termS1287;
      struct _M0TPB5ArrayGfE* _M0L1vS4123;
      struct _M0TPB5ArrayGfE* _M0L1vS4145;
      float _M0L6_2atmpS4125;
      struct _M0TPB5ArrayGfE* _M0L1vS4144;
      float _M0L6_2atmpS4143;
      float _M0L6_2atmpS4142;
      float _M0L6_2atmpS4141;
      float _M0L6_2atmpS4137;
      struct _M0TPB5ArrayGfE* _M0L9syn__currS4140;
      float _M0L6_2atmpS4139;
      float _M0L6_2atmpS4138;
      float _M0L6_2atmpS4133;
      struct _M0TPB5ArrayGfE* _M0L1wS4136;
      float _M0L6_2atmpS4135;
      float _M0L6_2atmpS4134;
      float _M0L6_2atmpS4129;
      struct _M0TPB5ArrayGfE* _M0L1iS4132;
      float _M0L6_2atmpS4131;
      float _M0L6_2atmpS4130;
      float _M0L6_2atmpS4128;
      float _M0L6_2atmpS4127;
      float _M0L6_2atmpS4126;
      float _M0L6_2atmpS4124;
      struct _M0TPB5ArrayGfE* _M0L9thresholdS4146;
      struct _M0TPB5ArrayGfE* _M0L9thresholdS4154;
      float _M0L6_2atmpS4148;
      struct _M0TPB5ArrayGfE* _M0L9thresholdS4153;
      float _M0L6_2atmpS4152;
      float _M0L6_2atmpS4151;
      float _M0L6_2atmpS4150;
      float _M0L6_2atmpS4149;
      float _M0L6_2atmpS4147;
      struct _M0TPB5ArrayGbE* _M0L4fireS4155;
      struct _M0TPB5ArrayGfE* _M0L1vS4158;
      float _M0L6_2atmpS4157;
      int32_t _M0L6_2atmpS4156;
      struct _M0TPB5ArrayGfE* _M0L1vS4159;
      struct _M0TPB5ArrayGbE* _M0L4fireS4161;
      float _M0L6_2atmpS4160;
      struct _M0TPB5ArrayGfE* _M0L1wS4163;
      struct _M0TPB5ArrayGbE* _M0L4fireS4165;
      float _M0L6_2atmpS4164;
      struct _M0TPB5ArrayGfE* _M0L9thresholdS4169;
      struct _M0TPB5ArrayGbE* _M0L4fireS4171;
      float _M0L6_2atmpS4170;
      struct _M0TPB5ArrayGiE* _M0L4tabsS4175;
      struct _M0TPB5ArrayGbE* _M0L4fireS4177;
      int32_t _M0L6_2atmpS4176;
      int32_t _M0L6_2atmpS4098;
      #line 201 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS4101, _M0L1iS1284)) {
        _M0L6_2atmpS4100 = _M0L2vrS1271;
      } else {
        struct _M0TPB5ArrayGfE* _M0L1vS4102 = _M0L1pS1267->$3;
        #line 201 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
        _M0L6_2atmpS4100
        = _M0MPC15array5Array2atGfE(_M0L1vS4102, _M0L1iS1284);
      }
      #line 201 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS4099, _M0L1iS1284, _M0L6_2atmpS4100);
      _M0L4fireS4103 = _M0L1pS1267->$5;
      #line 204 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGbE(_M0L4fireS4103, _M0L1iS1284, 0);
      _M0L4tabsS4104 = _M0L1pS1267->$7;
      _M0L4tabsS4107 = _M0L1pS1267->$7;
      #line 205 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4106
      = _M0MPC15array5Array2atGiE(_M0L4tabsS4107, _M0L1iS1284);
      _M0L6_2atmpS4105 = _M0L6_2atmpS4106 - 1;
      #line 205 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGiE(_M0L4tabsS4104, _M0L1iS1284, _M0L6_2atmpS4105);
      _M0L4tabsS4109 = _M0L1pS1267->$7;
      #line 206 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4108
      = _M0MPC15array5Array2atGiE(_M0L4tabsS4109, _M0L1iS1284);
      if (_M0L6_2atmpS4108 > 0) {
        goto join_1285;
      }
      _M0L1wS4110 = _M0L1pS1267->$4;
      _M0L1wS4122 = _M0L1pS1267->$4;
      #line 211 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4112 = _M0MPC15array5Array2atGfE(_M0L1wS4122, _M0L1iS1284);
      _M0L1vS4121 = _M0L1pS1267->$3;
      #line 211 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4120 = _M0MPC15array5Array2atGfE(_M0L1vS4121, _M0L1iS1284);
      _M0L6_2atmpS4119 = _M0L6_2atmpS4120 - _M0L2elS1272;
      _M0L6_2atmpS4116 = _M0L1aS1276 * _M0L6_2atmpS4119;
      _M0L1wS4118 = _M0L1pS1267->$4;
      #line 211 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4117 = _M0MPC15array5Array2atGfE(_M0L1wS4118, _M0L1iS1284);
      _M0L6_2atmpS4115 = _M0L6_2atmpS4116 - _M0L6_2atmpS4117;
      _M0L6_2atmpS4114 = _M0L2dtS1282 * _M0L6_2atmpS4115;
      _M0L6_2atmpS4113 = _M0L6_2atmpS4114 / _M0L2twS1275;
      _M0L6_2atmpS4111 = _M0L6_2atmpS4112 + _M0L6_2atmpS4113;
      #line 211 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGfE(_M0L1wS4110, _M0L1iS1284, _M0L6_2atmpS4111);
      if (_M0L9dt__slopeS1274 < 0x0p+0f) {
        _M0L9exp__termS1287 = 0x0p+0f;
      } else {
        struct _M0TPB5ArrayGfE* _M0L1vS4185 = _M0L1pS1267->$3;
        float _M0L6_2atmpS4182;
        struct _M0TPB5ArrayGfE* _M0L9thresholdS4184;
        float _M0L6_2atmpS4183;
        float _M0L6_2atmpS4181;
        float _M0L6_2atmpS4180;
        float _M0L6_2atmpS4179;
        #line 217 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
        _M0L6_2atmpS4182
        = _M0MPC15array5Array2atGfE(_M0L1vS4185, _M0L1iS1284);
        _M0L9thresholdS4184 = _M0L1pS1267->$6;
        #line 217 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
        _M0L6_2atmpS4183
        = _M0MPC15array5Array2atGfE(_M0L9thresholdS4184, _M0L1iS1284);
        _M0L6_2atmpS4181 = _M0L6_2atmpS4182 - _M0L6_2atmpS4183;
        _M0L6_2atmpS4180 = _M0L6_2atmpS4181 / _M0L9dt__slopeS1274;
        #line 217 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
        _M0L6_2atmpS4179 = _M0FP26RiantR8snn__mbt4expf(_M0L6_2atmpS4180);
        _M0L9exp__termS1287 = _M0L9dt__slopeS1274 * _M0L6_2atmpS4179;
      }
      _M0L1vS4123 = _M0L1pS1267->$3;
      _M0L1vS4145 = _M0L1pS1267->$3;
      #line 219 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4125 = _M0MPC15array5Array2atGfE(_M0L1vS4145, _M0L1iS1284);
      _M0L1vS4144 = _M0L1pS1267->$3;
      #line 222 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4143 = _M0MPC15array5Array2atGfE(_M0L1vS4144, _M0L1iS1284);
      _M0L6_2atmpS4142 = _M0L6_2atmpS4143 - _M0L2elS1272;
      _M0L6_2atmpS4141 = -_M0L6_2atmpS4142;
      _M0L6_2atmpS4137 = _M0L6_2atmpS4141 + _M0L9exp__termS1287;
      _M0L9syn__currS4140 = _M0L1pS1267->$9;
      #line 222 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4139
      = _M0MPC15array5Array2atGfE(_M0L9syn__currS4140, _M0L1iS1284);
      _M0L6_2atmpS4138 = _M0L1rS1273 * _M0L6_2atmpS4139;
      _M0L6_2atmpS4133 = _M0L6_2atmpS4137 - _M0L6_2atmpS4138;
      _M0L1wS4136 = _M0L1pS1267->$4;
      #line 222 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4135 = _M0MPC15array5Array2atGfE(_M0L1wS4136, _M0L1iS1284);
      _M0L6_2atmpS4134 = _M0L1rS1273 * _M0L6_2atmpS4135;
      _M0L6_2atmpS4129 = _M0L6_2atmpS4133 - _M0L6_2atmpS4134;
      _M0L1iS4132 = _M0L1pS1267->$8;
      #line 223 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4131 = _M0MPC15array5Array2atGfE(_M0L1iS4132, _M0L1iS1284);
      _M0L6_2atmpS4130 = _M0L1rS1273 * _M0L6_2atmpS4131;
      _M0L6_2atmpS4128 = _M0L6_2atmpS4129 + _M0L6_2atmpS4130;
      _M0L6_2atmpS4127 = _M0L2dtS1282 * _M0L6_2atmpS4128;
      _M0L6_2atmpS4126 = _M0L6_2atmpS4127 / _M0L2tmS1269;
      _M0L6_2atmpS4124 = _M0L6_2atmpS4125 + _M0L6_2atmpS4126;
      #line 219 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS4123, _M0L1iS1284, _M0L6_2atmpS4124);
      _M0L9thresholdS4146 = _M0L1pS1267->$6;
      _M0L9thresholdS4154 = _M0L1pS1267->$6;
      #line 227 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4148
      = _M0MPC15array5Array2atGfE(_M0L9thresholdS4154, _M0L1iS1284);
      _M0L9thresholdS4153 = _M0L1pS1267->$6;
      #line 227 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4152
      = _M0MPC15array5Array2atGfE(_M0L9thresholdS4153, _M0L1iS1284);
      _M0L6_2atmpS4151 = _M0L2vtS1270 - _M0L6_2atmpS4152;
      _M0L6_2atmpS4150 = _M0L2dtS1282 * _M0L6_2atmpS4151;
      _M0L6_2atmpS4149 = _M0L6_2atmpS4150 / _M0L6tau__aS1279;
      _M0L6_2atmpS4147 = _M0L6_2atmpS4148 + _M0L6_2atmpS4149;
      #line 227 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGfE(_M0L9thresholdS4146, _M0L1iS1284, _M0L6_2atmpS4147);
      _M0L4fireS4155 = _M0L1pS1267->$5;
      _M0L1vS4158 = _M0L1pS1267->$3;
      #line 230 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4157 = _M0MPC15array5Array2atGfE(_M0L1vS4158, _M0L1iS1284);
      _M0L6_2atmpS4156 = _M0L6_2atmpS4157 >= 0x0p+0f;
      #line 230 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGbE(_M0L4fireS4155, _M0L1iS1284, _M0L6_2atmpS4156);
      _M0L1vS4159 = _M0L1pS1267->$3;
      _M0L4fireS4161 = _M0L1pS1267->$5;
      #line 231 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS4161, _M0L1iS1284)) {
        _M0L6_2atmpS4160 = 0x1.4p+4f;
      } else {
        struct _M0TPB5ArrayGfE* _M0L1vS4162 = _M0L1pS1267->$3;
        #line 231 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
        _M0L6_2atmpS4160
        = _M0MPC15array5Array2atGfE(_M0L1vS4162, _M0L1iS1284);
      }
      #line 231 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS4159, _M0L1iS1284, _M0L6_2atmpS4160);
      _M0L1wS4163 = _M0L1pS1267->$4;
      _M0L4fireS4165 = _M0L1pS1267->$5;
      #line 232 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS4165, _M0L1iS1284)) {
        struct _M0TPB5ArrayGfE* _M0L1wS4167 = _M0L1pS1267->$4;
        float _M0L6_2atmpS4166;
        #line 232 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
        _M0L6_2atmpS4166
        = _M0MPC15array5Array2atGfE(_M0L1wS4167, _M0L1iS1284);
        _M0L6_2atmpS4164 = _M0L6_2atmpS4166 + _M0L1bS1277;
      } else {
        struct _M0TPB5ArrayGfE* _M0L1wS4168 = _M0L1pS1267->$4;
        #line 232 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
        _M0L6_2atmpS4164
        = _M0MPC15array5Array2atGfE(_M0L1wS4168, _M0L1iS1284);
      }
      #line 232 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGfE(_M0L1wS4163, _M0L1iS1284, _M0L6_2atmpS4164);
      _M0L9thresholdS4169 = _M0L1pS1267->$6;
      _M0L4fireS4171 = _M0L1pS1267->$5;
      #line 233 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS4171, _M0L1iS1284)) {
        struct _M0TPB5ArrayGfE* _M0L9thresholdS4173 = _M0L1pS1267->$6;
        float _M0L6_2atmpS4172;
        #line 233 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
        _M0L6_2atmpS4172
        = _M0MPC15array5Array2atGfE(_M0L9thresholdS4173, _M0L1iS1284);
        _M0L6_2atmpS4170 = _M0L6_2atmpS4172 + _M0L2atS1278;
      } else {
        struct _M0TPB5ArrayGfE* _M0L9thresholdS4174 = _M0L1pS1267->$6;
        #line 233 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
        _M0L6_2atmpS4170
        = _M0MPC15array5Array2atGfE(_M0L9thresholdS4174, _M0L1iS1284);
      }
      #line 233 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGfE(_M0L9thresholdS4169, _M0L1iS1284, _M0L6_2atmpS4170);
      _M0L4tabsS4175 = _M0L1pS1267->$7;
      _M0L4fireS4177 = _M0L1pS1267->$5;
      #line 234 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS4177, _M0L1iS1284)) {
        _M0L6_2atmpS4176 = _M0L11tabs__stepsS1281;
      } else {
        struct _M0TPB5ArrayGiE* _M0L4tabsS4178 = _M0L1pS1267->$7;
        #line 234 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
        _M0L6_2atmpS4176
        = _M0MPC15array5Array2atGiE(_M0L4tabsS4178, _M0L1iS1284);
      }
      #line 234 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGiE(_M0L4tabsS4175, _M0L1iS1284, _M0L6_2atmpS4176);
      goto join_1285;
      goto joinlet_5376;
      join_1285:;
      _M0L6_2atmpS4098 = _M0L1iS1284 + 1;
      _M0L1iS1284 = _M0L6_2atmpS4098;
      continue;
      joinlet_5376:;
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt23adex__synaptic__current(
  struct _M0TP26RiantR8snn__mbt4AdEx* _M0L1pS1262
) {
  int32_t _M0L1nS1261;
  int32_t _M0L7_2abindS1263;
  int32_t _M0L1iS1264;
  #line 259 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
  _M0L1nS1261 = _M0L1pS1262->$2;
  _M0L7_2abindS1263 = 0;
  _M0L1iS1264 = _M0L7_2abindS1263;
  while (1) {
    if (_M0L1iS1264 < _M0L1nS1261) {
      struct _M0TPB5ArrayGfE* _M0L9syn__currS4075 = _M0L1pS1262->$9;
      struct _M0TPB5ArrayGfE* _M0L2geS4096 = _M0L1pS1262->$10;
      float _M0L6_2atmpS4091;
      struct _M0TPB5ArrayGfE* _M0L1vS4095;
      float _M0L6_2atmpS4093;
      float _M0L4e__eS4094;
      float _M0L6_2atmpS4092;
      float _M0L6_2atmpS4088;
      struct _M0TPB5ArrayGfE* _M0L7gsyn__eS4090;
      float _M0L6_2atmpS4089;
      float _M0L6_2atmpS4077;
      struct _M0TPB5ArrayGfE* _M0L2giS4087;
      float _M0L6_2atmpS4082;
      struct _M0TPB5ArrayGfE* _M0L1vS4086;
      float _M0L6_2atmpS4084;
      float _M0L4e__iS4085;
      float _M0L6_2atmpS4083;
      float _M0L6_2atmpS4079;
      struct _M0TPB5ArrayGfE* _M0L7gsyn__iS4081;
      float _M0L6_2atmpS4080;
      float _M0L6_2atmpS4078;
      float _M0L6_2atmpS4076;
      int32_t _M0L6_2atmpS4097;
      #line 262 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4091 = _M0MPC15array5Array2atGfE(_M0L2geS4096, _M0L1iS1264);
      _M0L1vS4095 = _M0L1pS1262->$3;
      #line 262 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4093 = _M0MPC15array5Array2atGfE(_M0L1vS4095, _M0L1iS1264);
      _M0L4e__eS4094 = _M0L1pS1262->$18;
      _M0L6_2atmpS4092 = _M0L6_2atmpS4093 - _M0L4e__eS4094;
      _M0L6_2atmpS4088 = _M0L6_2atmpS4091 * _M0L6_2atmpS4092;
      _M0L7gsyn__eS4090 = _M0L1pS1262->$16;
      #line 262 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4089
      = _M0MPC15array5Array2atGfE(_M0L7gsyn__eS4090, _M0L1iS1264);
      _M0L6_2atmpS4077 = _M0L6_2atmpS4088 * _M0L6_2atmpS4089;
      _M0L2giS4087 = _M0L1pS1262->$11;
      #line 263 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4082 = _M0MPC15array5Array2atGfE(_M0L2giS4087, _M0L1iS1264);
      _M0L1vS4086 = _M0L1pS1262->$3;
      #line 263 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4084 = _M0MPC15array5Array2atGfE(_M0L1vS4086, _M0L1iS1264);
      _M0L4e__iS4085 = _M0L1pS1262->$19;
      _M0L6_2atmpS4083 = _M0L6_2atmpS4084 - _M0L4e__iS4085;
      _M0L6_2atmpS4079 = _M0L6_2atmpS4082 * _M0L6_2atmpS4083;
      _M0L7gsyn__iS4081 = _M0L1pS1262->$17;
      #line 263 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4080
      = _M0MPC15array5Array2atGfE(_M0L7gsyn__iS4081, _M0L1iS1264);
      _M0L6_2atmpS4078 = _M0L6_2atmpS4079 * _M0L6_2atmpS4080;
      _M0L6_2atmpS4076 = _M0L6_2atmpS4077 + _M0L6_2atmpS4078;
      #line 262 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGfE(_M0L9syn__currS4075, _M0L1iS1264, _M0L6_2atmpS4076);
      _M0L6_2atmpS4097 = _M0L1iS1264 + 1;
      _M0L1iS1264 = _M0L6_2atmpS4097;
      continue;
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt20adex__step__synapses(
  struct _M0TP26RiantR8snn__mbt4AdEx* _M0L1pS1253,
  float _M0L2dtS1256
) {
  int32_t _M0L1nS1252;
  int32_t _M0L7_2abindS1254;
  int32_t _M0L1iS1255;
  int32_t _M0L7_2abindS1258;
  int32_t _M0L1iS1259;
  #line 241 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
  _M0L1nS1252 = _M0L1pS1253->$2;
  _M0L7_2abindS1254 = 0;
  _M0L1iS1255 = _M0L7_2abindS1254;
  while (1) {
    if (_M0L1iS1255 < _M0L1nS1252) {
      struct _M0TPB5ArrayGfE* _M0L2heS4013 = _M0L1pS1253->$12;
      struct _M0TPB5ArrayGfE* _M0L2heS4018 = _M0L1pS1253->$12;
      float _M0L6_2atmpS4015;
      struct _M0TPB5ArrayGfE* _M0L3gluS4017;
      float _M0L6_2atmpS4016;
      float _M0L6_2atmpS4014;
      struct _M0TPB5ArrayGfE* _M0L2hiS4019;
      struct _M0TPB5ArrayGfE* _M0L2hiS4024;
      float _M0L6_2atmpS4021;
      struct _M0TPB5ArrayGfE* _M0L4gabaS4023;
      float _M0L6_2atmpS4022;
      float _M0L6_2atmpS4020;
      struct _M0TPB5ArrayGfE* _M0L2geS4025;
      struct _M0TPB5ArrayGfE* _M0L2geS4037;
      float _M0L6_2atmpS4027;
      struct _M0TPB5ArrayGfE* _M0L2geS4036;
      float _M0L6_2atmpS4035;
      float _M0L6_2atmpS4033;
      float _M0L3tdeS4034;
      float _M0L6_2atmpS4030;
      struct _M0TPB5ArrayGfE* _M0L2heS4032;
      float _M0L6_2atmpS4031;
      float _M0L6_2atmpS4029;
      float _M0L6_2atmpS4028;
      float _M0L6_2atmpS4026;
      struct _M0TPB5ArrayGfE* _M0L2heS4038;
      struct _M0TPB5ArrayGfE* _M0L2heS4047;
      float _M0L6_2atmpS4040;
      struct _M0TPB5ArrayGfE* _M0L2heS4046;
      float _M0L6_2atmpS4045;
      float _M0L6_2atmpS4043;
      float _M0L3treS4044;
      float _M0L6_2atmpS4042;
      float _M0L6_2atmpS4041;
      float _M0L6_2atmpS4039;
      struct _M0TPB5ArrayGfE* _M0L2giS4048;
      struct _M0TPB5ArrayGfE* _M0L2giS4060;
      float _M0L6_2atmpS4050;
      struct _M0TPB5ArrayGfE* _M0L2giS4059;
      float _M0L6_2atmpS4058;
      float _M0L6_2atmpS4056;
      float _M0L3tdiS4057;
      float _M0L6_2atmpS4053;
      struct _M0TPB5ArrayGfE* _M0L2hiS4055;
      float _M0L6_2atmpS4054;
      float _M0L6_2atmpS4052;
      float _M0L6_2atmpS4051;
      float _M0L6_2atmpS4049;
      struct _M0TPB5ArrayGfE* _M0L2hiS4061;
      struct _M0TPB5ArrayGfE* _M0L2hiS4070;
      float _M0L6_2atmpS4063;
      struct _M0TPB5ArrayGfE* _M0L2hiS4069;
      float _M0L6_2atmpS4068;
      float _M0L6_2atmpS4066;
      float _M0L3triS4067;
      float _M0L6_2atmpS4065;
      float _M0L6_2atmpS4064;
      float _M0L6_2atmpS4062;
      int32_t _M0L6_2atmpS4071;
      #line 244 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4015 = _M0MPC15array5Array2atGfE(_M0L2heS4018, _M0L1iS1255);
      _M0L3gluS4017 = _M0L1pS1253->$14;
      #line 244 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4016
      = _M0MPC15array5Array2atGfE(_M0L3gluS4017, _M0L1iS1255);
      _M0L6_2atmpS4014 = _M0L6_2atmpS4015 + _M0L6_2atmpS4016;
      #line 244 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGfE(_M0L2heS4013, _M0L1iS1255, _M0L6_2atmpS4014);
      _M0L2hiS4019 = _M0L1pS1253->$13;
      _M0L2hiS4024 = _M0L1pS1253->$13;
      #line 245 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4021 = _M0MPC15array5Array2atGfE(_M0L2hiS4024, _M0L1iS1255);
      _M0L4gabaS4023 = _M0L1pS1253->$15;
      #line 245 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4022
      = _M0MPC15array5Array2atGfE(_M0L4gabaS4023, _M0L1iS1255);
      _M0L6_2atmpS4020 = _M0L6_2atmpS4021 + _M0L6_2atmpS4022;
      #line 245 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGfE(_M0L2hiS4019, _M0L1iS1255, _M0L6_2atmpS4020);
      _M0L2geS4025 = _M0L1pS1253->$10;
      _M0L2geS4037 = _M0L1pS1253->$10;
      #line 246 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4027 = _M0MPC15array5Array2atGfE(_M0L2geS4037, _M0L1iS1255);
      _M0L2geS4036 = _M0L1pS1253->$10;
      #line 246 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4035 = _M0MPC15array5Array2atGfE(_M0L2geS4036, _M0L1iS1255);
      _M0L6_2atmpS4033 = -_M0L6_2atmpS4035;
      _M0L3tdeS4034 = _M0L1pS1253->$21;
      _M0L6_2atmpS4030 = _M0L6_2atmpS4033 / _M0L3tdeS4034;
      _M0L2heS4032 = _M0L1pS1253->$12;
      #line 246 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4031 = _M0MPC15array5Array2atGfE(_M0L2heS4032, _M0L1iS1255);
      _M0L6_2atmpS4029 = _M0L6_2atmpS4030 + _M0L6_2atmpS4031;
      _M0L6_2atmpS4028 = _M0L2dtS1256 * _M0L6_2atmpS4029;
      _M0L6_2atmpS4026 = _M0L6_2atmpS4027 + _M0L6_2atmpS4028;
      #line 246 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGfE(_M0L2geS4025, _M0L1iS1255, _M0L6_2atmpS4026);
      _M0L2heS4038 = _M0L1pS1253->$12;
      _M0L2heS4047 = _M0L1pS1253->$12;
      #line 247 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4040 = _M0MPC15array5Array2atGfE(_M0L2heS4047, _M0L1iS1255);
      _M0L2heS4046 = _M0L1pS1253->$12;
      #line 247 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4045 = _M0MPC15array5Array2atGfE(_M0L2heS4046, _M0L1iS1255);
      _M0L6_2atmpS4043 = -_M0L6_2atmpS4045;
      _M0L3treS4044 = _M0L1pS1253->$20;
      _M0L6_2atmpS4042 = _M0L6_2atmpS4043 / _M0L3treS4044;
      _M0L6_2atmpS4041 = _M0L2dtS1256 * _M0L6_2atmpS4042;
      _M0L6_2atmpS4039 = _M0L6_2atmpS4040 + _M0L6_2atmpS4041;
      #line 247 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGfE(_M0L2heS4038, _M0L1iS1255, _M0L6_2atmpS4039);
      _M0L2giS4048 = _M0L1pS1253->$11;
      _M0L2giS4060 = _M0L1pS1253->$11;
      #line 248 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4050 = _M0MPC15array5Array2atGfE(_M0L2giS4060, _M0L1iS1255);
      _M0L2giS4059 = _M0L1pS1253->$11;
      #line 248 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4058 = _M0MPC15array5Array2atGfE(_M0L2giS4059, _M0L1iS1255);
      _M0L6_2atmpS4056 = -_M0L6_2atmpS4058;
      _M0L3tdiS4057 = _M0L1pS1253->$23;
      _M0L6_2atmpS4053 = _M0L6_2atmpS4056 / _M0L3tdiS4057;
      _M0L2hiS4055 = _M0L1pS1253->$13;
      #line 248 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4054 = _M0MPC15array5Array2atGfE(_M0L2hiS4055, _M0L1iS1255);
      _M0L6_2atmpS4052 = _M0L6_2atmpS4053 + _M0L6_2atmpS4054;
      _M0L6_2atmpS4051 = _M0L2dtS1256 * _M0L6_2atmpS4052;
      _M0L6_2atmpS4049 = _M0L6_2atmpS4050 + _M0L6_2atmpS4051;
      #line 248 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGfE(_M0L2giS4048, _M0L1iS1255, _M0L6_2atmpS4049);
      _M0L2hiS4061 = _M0L1pS1253->$13;
      _M0L2hiS4070 = _M0L1pS1253->$13;
      #line 249 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4063 = _M0MPC15array5Array2atGfE(_M0L2hiS4070, _M0L1iS1255);
      _M0L2hiS4069 = _M0L1pS1253->$13;
      #line 249 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4068 = _M0MPC15array5Array2atGfE(_M0L2hiS4069, _M0L1iS1255);
      _M0L6_2atmpS4066 = -_M0L6_2atmpS4068;
      _M0L3triS4067 = _M0L1pS1253->$22;
      _M0L6_2atmpS4065 = _M0L6_2atmpS4066 / _M0L3triS4067;
      _M0L6_2atmpS4064 = _M0L2dtS1256 * _M0L6_2atmpS4065;
      _M0L6_2atmpS4062 = _M0L6_2atmpS4063 + _M0L6_2atmpS4064;
      #line 249 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGfE(_M0L2hiS4061, _M0L1iS1255, _M0L6_2atmpS4062);
      _M0L6_2atmpS4071 = _M0L1iS1255 + 1;
      _M0L1iS1255 = _M0L6_2atmpS4071;
      continue;
    }
    break;
  }
  _M0L7_2abindS1258 = 0;
  _M0L1iS1259 = _M0L7_2abindS1258;
  while (1) {
    if (_M0L1iS1259 < _M0L1nS1252) {
      struct _M0TPB5ArrayGfE* _M0L3gluS4072 = _M0L1pS1253->$14;
      struct _M0TPB5ArrayGfE* _M0L4gabaS4073;
      int32_t _M0L6_2atmpS4074;
      #line 252 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGfE(_M0L3gluS4072, _M0L1iS1259, 0x0p+0f);
      _M0L4gabaS4073 = _M0L1pS1253->$15;
      #line 253 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGfE(_M0L4gabaS4073, _M0L1iS1259, 0x0p+0f);
      _M0L6_2atmpS4074 = _M0L1iS1259 + 1;
      _M0L1iS1259 = _M0L6_2atmpS4074;
      continue;
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt16forward__synapse(
  struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0L1cS1228,
  float _M0L6t__nowS1239
) {
  struct _M0TPB5ArrayGfE* _M0L6delaysS4012;
  int32_t _M0L6_2atmpS4011;
  int32_t _M0L10use__delayS1227;
  struct _M0TPB5ArrayGfE* _M0L3rhoS4010;
  int32_t _M0L6_2atmpS4009;
  int32_t _M0L8use__rhoS1229;
  #line 246 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
  _M0L6delaysS4012 = _M0L1cS1228->$5;
  #line 247 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
  _M0L6_2atmpS4011 = _M0MPC15array5Array6lengthGfE(_M0L6delaysS4012);
  _M0L10use__delayS1227 = _M0L6_2atmpS4011 > 0;
  _M0L3rhoS4010 = _M0L1cS1228->$6;
  #line 248 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
  _M0L6_2atmpS4009 = _M0MPC15array5Array6lengthGfE(_M0L3rhoS4010);
  _M0L8use__rhoS1229 = _M0L6_2atmpS4009 > 0;
  if (_M0L10use__delayS1227) {
    struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS3972 = _M0L1cS1228->$0;
    struct _M0TPB5ArrayGbE* _M0L4fireS3971 = _M0L3preS3972->$5;
    int32_t _M0L6n__preS1230;
    struct _M0TPB8MutLocalGiE* _M0L1jS1231;
    #line 251 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
    _M0L6n__preS1230 = _M0MPC15array5Array6lengthGbE(_M0L4fireS3971);
    _M0L1jS1231
    = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
    Moonbit_object_header(_M0L1jS1231)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
    _M0L1jS1231->$0 = 0;
    while (1) {
      int32_t _M0L3valS3940 = _M0L1jS1231->$0;
      if (_M0L3valS3940 < _M0L6n__preS1230) {
        struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS3943 = _M0L1cS1228->$0;
        struct _M0TPB5ArrayGbE* _M0L4fireS3941 = _M0L3preS3943->$5;
        int32_t _M0L3valS3942 = _M0L1jS1231->$0;
        int32_t _M0L3valS3970;
        int32_t _M0L6_2atmpS3969;
        #line 254 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
        if (_M0MPC15array5Array2atGbE(_M0L4fireS3941, _M0L3valS3942)) {
          struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS3968 =
            _M0L1cS1228->$4;
          struct _M0TPB5ArrayGiE* _M0L6rowptrS3966 = _M0L6matrixS3968->$2;
          int32_t _M0L3valS3967 = _M0L1jS1231->$0;
          int32_t _M0L5startS1232;
          struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS3965;
          struct _M0TPB5ArrayGiE* _M0L6rowptrS3962;
          int32_t _M0L3valS3964;
          int32_t _M0L6_2atmpS3963;
          int32_t _M0L3endS1233;
          struct _M0TPB8MutLocalGiE* _M0L1sS1234;
          #line 255 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
          _M0L5startS1232
          = _M0MPC15array5Array2atGiE(_M0L6rowptrS3966, _M0L3valS3967);
          _M0L6matrixS3965 = _M0L1cS1228->$4;
          _M0L6rowptrS3962 = _M0L6matrixS3965->$2;
          _M0L3valS3964 = _M0L1jS1231->$0;
          _M0L6_2atmpS3963 = _M0L3valS3964 + 1;
          #line 256 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
          _M0L3endS1233
          = _M0MPC15array5Array2atGiE(_M0L6rowptrS3962, _M0L6_2atmpS3963);
          _M0L1sS1234
          = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
          Moonbit_object_header(_M0L1sS1234)->meta
          = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
          _M0L1sS1234->$0 = _M0L5startS1232;
          while (1) {
            int32_t _M0L3valS3944 = _M0L1sS1234->$0;
            if (_M0L3valS3944 < _M0L3endS1233) {
              struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS3961 =
                _M0L1cS1228->$4;
              struct _M0TPB5ArrayGiE* _M0L6colptrS3959 = _M0L6matrixS3961->$3;
              int32_t _M0L3valS3960 = _M0L1sS1234->$0;
              int32_t _M0L9post__idxS1235;
              struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS3958;
              struct _M0TPB5ArrayGfE* _M0L4valsS3956;
              int32_t _M0L3valS3957;
              float _M0L1wS1236;
              struct _M0TPB5ArrayGfE* _M0L6delaysS3954;
              int32_t _M0L3valS3955;
              float _M0L1dS1237;
              float _M0L9w__scaledS1238;
              int32_t _M0L3valS3950;
              int32_t _M0L6_2atmpS3949;
              #line 259 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
              _M0L9post__idxS1235
              = _M0MPC15array5Array2atGiE(_M0L6colptrS3959, _M0L3valS3960);
              _M0L6matrixS3958 = _M0L1cS1228->$4;
              _M0L4valsS3956 = _M0L6matrixS3958->$4;
              _M0L3valS3957 = _M0L1sS1234->$0;
              #line 260 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
              _M0L1wS1236
              = _M0MPC15array5Array2atGfE(_M0L4valsS3956, _M0L3valS3957);
              _M0L6delaysS3954 = _M0L1cS1228->$5;
              _M0L3valS3955 = _M0L1sS1234->$0;
              #line 261 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
              _M0L1dS1237
              = _M0MPC15array5Array2atGfE(_M0L6delaysS3954, _M0L3valS3955);
              if (_M0L8use__rhoS1229) {
                struct _M0TPB5ArrayGfE* _M0L3rhoS3952 = _M0L1cS1228->$6;
                int32_t _M0L3valS3953 = _M0L1sS1234->$0;
                float _M0L6_2atmpS3951;
                #line 263 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
                _M0L6_2atmpS3951
                = _M0MPC15array5Array2atGfE(_M0L3rhoS3952, _M0L3valS3953);
                _M0L9w__scaledS1238 = _M0L1wS1236 * _M0L6_2atmpS3951;
              } else {
                _M0L9w__scaledS1238 = _M0L1wS1236;
              }
              if (_M0L1dS1237 == 0x0p+0f) {
                #line 266 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
                _M0FP26RiantR8snn__mbt13apply__weight(_M0L1cS1228, _M0L9post__idxS1235, _M0L9w__scaledS1238);
              } else {
                struct _M0TPB5ArrayGfE* _M0L14pending__timesS3945 =
                  _M0L1cS1228->$7;
                float _M0L6_2atmpS3946 = _M0L6t__nowS1239 + _M0L1dS1237;
                struct _M0TPB5ArrayGiE* _M0L14pending__postsS3947;
                struct _M0TPB5ArrayGfE* _M0L16pending__weightsS3948;
                #line 269 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
                _M0MPC15array5Array4pushGfE(_M0L14pending__timesS3945, _M0L6_2atmpS3946);
                _M0L14pending__postsS3947 = _M0L1cS1228->$8;
                #line 270 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
                _M0MPC15array5Array4pushGiE(_M0L14pending__postsS3947, _M0L9post__idxS1235);
                _M0L16pending__weightsS3948 = _M0L1cS1228->$9;
                #line 271 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
                _M0MPC15array5Array4pushGfE(_M0L16pending__weightsS3948, _M0L9w__scaledS1238);
              }
              _M0L3valS3950 = _M0L1sS1234->$0;
              _M0L6_2atmpS3949 = _M0L3valS3950 + 1;
              _M0L1sS1234->$0 = _M0L6_2atmpS3949;
              continue;
            } else {
              moonbit_decref(_M0L1sS1234);
            }
            break;
          }
        }
        _M0L3valS3970 = _M0L1jS1231->$0;
        _M0L6_2atmpS3969 = _M0L3valS3970 + 1;
        _M0L1jS1231->$0 = _M0L6_2atmpS3969;
        continue;
      } else {
        moonbit_decref(_M0L1jS1231);
      }
      break;
    }
  } else {
    moonbit_string_t _M0L3symS4006 = _M0L1cS1228->$2;
    struct _M0TPB5ArrayGfE* _M0L6targetS1242;
    #line 280 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
    if (
      _M0L3symS4006 == (moonbit_string_t)moonbit_string_literal_1.data
      || Moonbit_array_length(_M0L3symS4006)
         == Moonbit_array_length((moonbit_string_t)moonbit_string_literal_1.data)
         && 0
            == memcmp(_M0L3symS4006, (moonbit_string_t)moonbit_string_literal_1.data, Moonbit_array_length(_M0L3symS4006) * 2)
    ) {
      struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS4007 = _M0L1cS1228->$1;
      struct _M0TPB5ArrayGfE* _M0L8_2afieldS5109 = _M0L4postS4007->$13;
      moonbit_incref(_M0L8_2afieldS5109);
      _M0L6targetS1242 = _M0L8_2afieldS5109;
    } else {
      struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS4008 = _M0L1cS1228->$1;
      struct _M0TPB5ArrayGfE* _M0L8_2afieldS5110 = _M0L4postS4008->$14;
      moonbit_incref(_M0L8_2afieldS5110);
      _M0L6targetS1242 = _M0L8_2afieldS5110;
    }
    if (_M0L8use__rhoS1229) {
      struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS4002 = _M0L1cS1228->$0;
      struct _M0TPB5ArrayGbE* _M0L4fireS4001 = _M0L3preS4002->$5;
      int32_t _M0L6n__preS1243;
      struct _M0TPB8MutLocalGiE* _M0L1jS1244;
      #line 283 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
      _M0L6n__preS1243 = _M0MPC15array5Array6lengthGbE(_M0L4fireS4001);
      _M0L1jS1244
      = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
      Moonbit_object_header(_M0L1jS1244)->meta
      = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
      _M0L1jS1244->$0 = 0;
      while (1) {
        int32_t _M0L3valS3973 = _M0L1jS1244->$0;
        if (_M0L3valS3973 < _M0L6n__preS1243) {
          struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS3976 = _M0L1cS1228->$0;
          struct _M0TPB5ArrayGbE* _M0L4fireS3974 = _M0L3preS3976->$5;
          int32_t _M0L3valS3975 = _M0L1jS1244->$0;
          int32_t _M0L3valS4000;
          int32_t _M0L6_2atmpS3999;
          #line 286 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
          if (_M0MPC15array5Array2atGbE(_M0L4fireS3974, _M0L3valS3975)) {
            struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS3998 =
              _M0L1cS1228->$4;
            struct _M0TPB5ArrayGiE* _M0L6rowptrS3996 = _M0L6matrixS3998->$2;
            int32_t _M0L3valS3997 = _M0L1jS1244->$0;
            int32_t _M0L5startS1245;
            struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS3995;
            struct _M0TPB5ArrayGiE* _M0L6rowptrS3992;
            int32_t _M0L3valS3994;
            int32_t _M0L6_2atmpS3993;
            int32_t _M0L3endS1246;
            struct _M0TPB8MutLocalGiE* _M0L1sS1247;
            #line 287 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
            _M0L5startS1245
            = _M0MPC15array5Array2atGiE(_M0L6rowptrS3996, _M0L3valS3997);
            _M0L6matrixS3995 = _M0L1cS1228->$4;
            _M0L6rowptrS3992 = _M0L6matrixS3995->$2;
            _M0L3valS3994 = _M0L1jS1244->$0;
            _M0L6_2atmpS3993 = _M0L3valS3994 + 1;
            #line 288 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
            _M0L3endS1246
            = _M0MPC15array5Array2atGiE(_M0L6rowptrS3992, _M0L6_2atmpS3993);
            _M0L1sS1247
            = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
            Moonbit_object_header(_M0L1sS1247)->meta
            = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
            _M0L1sS1247->$0 = _M0L5startS1245;
            while (1) {
              int32_t _M0L3valS3977 = _M0L1sS1247->$0;
              if (_M0L3valS3977 < _M0L3endS1246) {
                struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS3991 =
                  _M0L1cS1228->$4;
                struct _M0TPB5ArrayGiE* _M0L6colptrS3989 =
                  _M0L6matrixS3991->$3;
                int32_t _M0L3valS3990 = _M0L1sS1247->$0;
                int32_t _M0L9post__idxS1248;
                struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS3988;
                struct _M0TPB5ArrayGfE* _M0L4valsS3986;
                int32_t _M0L3valS3987;
                float _M0L6_2atmpS3982;
                struct _M0TPB5ArrayGfE* _M0L3rhoS3984;
                int32_t _M0L3valS3985;
                float _M0L6_2atmpS3983;
                float _M0L9w__scaledS1249;
                float _M0L6_2atmpS3979;
                float _M0L6_2atmpS3978;
                int32_t _M0L3valS3981;
                int32_t _M0L6_2atmpS3980;
                #line 291 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
                _M0L9post__idxS1248
                = _M0MPC15array5Array2atGiE(_M0L6colptrS3989, _M0L3valS3990);
                _M0L6matrixS3988 = _M0L1cS1228->$4;
                _M0L4valsS3986 = _M0L6matrixS3988->$4;
                _M0L3valS3987 = _M0L1sS1247->$0;
                #line 292 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
                _M0L6_2atmpS3982
                = _M0MPC15array5Array2atGfE(_M0L4valsS3986, _M0L3valS3987);
                _M0L3rhoS3984 = _M0L1cS1228->$6;
                _M0L3valS3985 = _M0L1sS1247->$0;
                #line 292 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
                _M0L6_2atmpS3983
                = _M0MPC15array5Array2atGfE(_M0L3rhoS3984, _M0L3valS3985);
                _M0L9w__scaledS1249 = _M0L6_2atmpS3982 * _M0L6_2atmpS3983;
                #line 293 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
                _M0L6_2atmpS3979
                = _M0MPC15array5Array2atGfE(_M0L6targetS1242, _M0L9post__idxS1248);
                _M0L6_2atmpS3978 = _M0L6_2atmpS3979 + _M0L9w__scaledS1249;
                #line 293 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
                _M0MPC15array5Array3setGfE(_M0L6targetS1242, _M0L9post__idxS1248, _M0L6_2atmpS3978);
                _M0L3valS3981 = _M0L1sS1247->$0;
                _M0L6_2atmpS3980 = _M0L3valS3981 + 1;
                _M0L1sS1247->$0 = _M0L6_2atmpS3980;
                continue;
              } else {
                moonbit_decref(_M0L1sS1247);
              }
              break;
            }
          }
          _M0L3valS4000 = _M0L1jS1244->$0;
          _M0L6_2atmpS3999 = _M0L3valS4000 + 1;
          _M0L1jS1244->$0 = _M0L6_2atmpS3999;
          continue;
        } else {
          moonbit_decref(_M0L1jS1244);
          moonbit_decref(_M0L6targetS1242);
        }
        break;
      }
    } else {
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS4003 =
        _M0L1cS1228->$4;
      struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS4005 = _M0L1cS1228->$0;
      struct _M0TPB5ArrayGbE* _M0L4fireS4004 = _M0L3preS4005->$5;
      #line 300 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
      _M0MP26RiantR8snn__mbt15SparseMatrixCSR7forward(_M0L6matrixS4003, _M0L4fireS4004, _M0L6targetS1242);
      moonbit_decref(_M0L6targetS1242);
    }
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt25deliver__pending__synapse(
  struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0L1cS1220,
  float _M0L6t__nowS1223
) {
  struct _M0TPB5ArrayGfE* _M0L14pending__timesS3939;
  int32_t _M0L1nS1219;
  struct _M0TPB8MutLocalGiE* _M0L4keptS1221;
  struct _M0TPB8MutLocalGiE* _M0L1kS1222;
  int32_t _M0L3valS3938;
  int32_t _M0L6_2atmpS3937;
  struct _M0TPB8MutLocalGiE* _M0L4dropS1225;
  #line 312 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
  _M0L14pending__timesS3939 = _M0L1cS1220->$7;
  #line 313 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
  _M0L1nS1219 = _M0MPC15array5Array6lengthGfE(_M0L14pending__timesS3939);
  if (_M0L1nS1219 == 0) {
    return 0;
  }
  _M0L4keptS1221
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L4keptS1221)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L4keptS1221->$0 = 0;
  _M0L1kS1222
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1kS1222)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1kS1222->$0 = 0;
  while (1) {
    int32_t _M0L3valS3900 = _M0L1kS1222->$0;
    if (_M0L3valS3900 < _M0L1nS1219) {
      struct _M0TPB5ArrayGfE* _M0L14pending__timesS3902 = _M0L1cS1220->$7;
      int32_t _M0L3valS3903 = _M0L1kS1222->$0;
      float _M0L6_2atmpS3901;
      int32_t _M0L3valS3930;
      int32_t _M0L6_2atmpS3929;
      #line 320 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
      _M0L6_2atmpS3901
      = _M0MPC15array5Array2atGfE(_M0L14pending__timesS3902, _M0L3valS3903);
      if (_M0L6_2atmpS3901 <= _M0L6t__nowS1223) {
        struct _M0TPB5ArrayGiE* _M0L14pending__postsS3908 = _M0L1cS1220->$8;
        int32_t _M0L3valS3909 = _M0L1kS1222->$0;
        int32_t _M0L6_2atmpS3904;
        struct _M0TPB5ArrayGfE* _M0L16pending__weightsS3906;
        int32_t _M0L3valS3907;
        float _M0L6_2atmpS3905;
        #line 322 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
        _M0L6_2atmpS3904
        = _M0MPC15array5Array2atGiE(_M0L14pending__postsS3908, _M0L3valS3909);
        _M0L16pending__weightsS3906 = _M0L1cS1220->$9;
        _M0L3valS3907 = _M0L1kS1222->$0;
        #line 322 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
        _M0L6_2atmpS3905
        = _M0MPC15array5Array2atGfE(_M0L16pending__weightsS3906, _M0L3valS3907);
        #line 322 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
        _M0FP26RiantR8snn__mbt13apply__weight(_M0L1cS1220, _M0L6_2atmpS3904, _M0L6_2atmpS3905);
      } else {
        int32_t _M0L3valS3910 = _M0L4keptS1221->$0;
        int32_t _M0L3valS3911 = _M0L1kS1222->$0;
        int32_t _M0L3valS3928;
        int32_t _M0L6_2atmpS3927;
        if (_M0L3valS3910 != _M0L3valS3911) {
          struct _M0TPB5ArrayGfE* _M0L14pending__timesS3912 = _M0L1cS1220->$7;
          int32_t _M0L3valS3913 = _M0L4keptS1221->$0;
          struct _M0TPB5ArrayGfE* _M0L14pending__timesS3915 = _M0L1cS1220->$7;
          int32_t _M0L3valS3916 = _M0L1kS1222->$0;
          float _M0L6_2atmpS3914;
          struct _M0TPB5ArrayGiE* _M0L14pending__postsS3917;
          int32_t _M0L3valS3918;
          struct _M0TPB5ArrayGiE* _M0L14pending__postsS3920;
          int32_t _M0L3valS3921;
          int32_t _M0L6_2atmpS3919;
          struct _M0TPB5ArrayGfE* _M0L16pending__weightsS3922;
          int32_t _M0L3valS3923;
          struct _M0TPB5ArrayGfE* _M0L16pending__weightsS3925;
          int32_t _M0L3valS3926;
          float _M0L6_2atmpS3924;
          #line 326 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
          _M0L6_2atmpS3914
          = _M0MPC15array5Array2atGfE(_M0L14pending__timesS3915, _M0L3valS3916);
          #line 326 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
          _M0MPC15array5Array3setGfE(_M0L14pending__timesS3912, _M0L3valS3913, _M0L6_2atmpS3914);
          _M0L14pending__postsS3917 = _M0L1cS1220->$8;
          _M0L3valS3918 = _M0L4keptS1221->$0;
          _M0L14pending__postsS3920 = _M0L1cS1220->$8;
          _M0L3valS3921 = _M0L1kS1222->$0;
          #line 327 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
          _M0L6_2atmpS3919
          = _M0MPC15array5Array2atGiE(_M0L14pending__postsS3920, _M0L3valS3921);
          #line 327 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
          _M0MPC15array5Array3setGiE(_M0L14pending__postsS3917, _M0L3valS3918, _M0L6_2atmpS3919);
          _M0L16pending__weightsS3922 = _M0L1cS1220->$9;
          _M0L3valS3923 = _M0L4keptS1221->$0;
          _M0L16pending__weightsS3925 = _M0L1cS1220->$9;
          _M0L3valS3926 = _M0L1kS1222->$0;
          #line 328 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
          _M0L6_2atmpS3924
          = _M0MPC15array5Array2atGfE(_M0L16pending__weightsS3925, _M0L3valS3926);
          #line 328 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
          _M0MPC15array5Array3setGfE(_M0L16pending__weightsS3922, _M0L3valS3923, _M0L6_2atmpS3924);
        }
        _M0L3valS3928 = _M0L4keptS1221->$0;
        _M0L6_2atmpS3927 = _M0L3valS3928 + 1;
        _M0L4keptS1221->$0 = _M0L6_2atmpS3927;
      }
      _M0L3valS3930 = _M0L1kS1222->$0;
      _M0L6_2atmpS3929 = _M0L3valS3930 + 1;
      _M0L1kS1222->$0 = _M0L6_2atmpS3929;
      continue;
    } else {
      moonbit_decref(_M0L1kS1222);
    }
    break;
  }
  _M0L3valS3938 = _M0L4keptS1221->$0;
  moonbit_decref(_M0L4keptS1221);
  _M0L6_2atmpS3937 = _M0L1nS1219 - _M0L3valS3938;
  _M0L4dropS1225
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L4dropS1225)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L4dropS1225->$0 = _M0L6_2atmpS3937;
  while (1) {
    int32_t _M0L3valS3931 = _M0L4dropS1225->$0;
    if (_M0L3valS3931 > 0) {
      struct _M0TPB5ArrayGfE* _M0L14pending__timesS3932 = _M0L1cS1220->$7;
      void* _M0L6_2atmpS5112;
      struct _M0TPB5ArrayGiE* _M0L14pending__postsS3933;
      struct _M0TPB5ArrayGfE* _M0L16pending__weightsS3934;
      void* _M0L6_2atmpS5111;
      int32_t _M0L3valS3936;
      int32_t _M0L6_2atmpS3935;
      #line 337 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
      _M0L6_2atmpS5112
      = _M0MPC15array5Array3popGfE(_M0L14pending__timesS3932);
      moonbit_decref(_M0L6_2atmpS5112);
      _M0L14pending__postsS3933 = _M0L1cS1220->$8;
      #line 338 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
      _M0MPC15array5Array3popGiE(_M0L14pending__postsS3933);
      _M0L16pending__weightsS3934 = _M0L1cS1220->$9;
      #line 339 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
      _M0L6_2atmpS5111
      = _M0MPC15array5Array3popGfE(_M0L16pending__weightsS3934);
      moonbit_decref(_M0L6_2atmpS5111);
      _M0L3valS3936 = _M0L4dropS1225->$0;
      _M0L6_2atmpS3935 = _M0L3valS3936 - 1;
      _M0L4dropS1225->$0 = _M0L6_2atmpS3935;
      continue;
    } else {
      moonbit_decref(_M0L4dropS1225);
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt13apply__weight(
  struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0L1cS1216,
  int32_t _M0L9post__idxS1217,
  float _M0L1wS1218
) {
  moonbit_string_t _M0L3symS3887;
  #line 346 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
  _M0L3symS3887 = _M0L1cS1216->$2;
  #line 347 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
  if (
    _M0L3symS3887 == (moonbit_string_t)moonbit_string_literal_1.data
    || Moonbit_array_length(_M0L3symS3887)
       == Moonbit_array_length((moonbit_string_t)moonbit_string_literal_1.data)
       && 0
          == memcmp(_M0L3symS3887, (moonbit_string_t)moonbit_string_literal_1.data, Moonbit_array_length(_M0L3symS3887) * 2)
  ) {
    struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS3893 = _M0L1cS1216->$1;
    struct _M0TPB5ArrayGfE* _M0L3gluS3888 = _M0L4postS3893->$13;
    struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS3892 = _M0L1cS1216->$1;
    struct _M0TPB5ArrayGfE* _M0L3gluS3891 = _M0L4postS3892->$13;
    float _M0L6_2atmpS3890;
    float _M0L6_2atmpS3889;
    #line 348 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
    _M0L6_2atmpS3890
    = _M0MPC15array5Array2atGfE(_M0L3gluS3891, _M0L9post__idxS1217);
    _M0L6_2atmpS3889 = _M0L6_2atmpS3890 + _M0L1wS1218;
    #line 348 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
    _M0MPC15array5Array3setGfE(_M0L3gluS3888, _M0L9post__idxS1217, _M0L6_2atmpS3889);
  } else {
    struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS3899 = _M0L1cS1216->$1;
    struct _M0TPB5ArrayGfE* _M0L4gabaS3894 = _M0L4postS3899->$14;
    struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS3898 = _M0L1cS1216->$1;
    struct _M0TPB5ArrayGfE* _M0L4gabaS3897 = _M0L4postS3898->$14;
    float _M0L6_2atmpS3896;
    float _M0L6_2atmpS3895;
    #line 350 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
    _M0L6_2atmpS3896
    = _M0MPC15array5Array2atGfE(_M0L4gabaS3897, _M0L9post__idxS1217);
    _M0L6_2atmpS3895 = _M0L6_2atmpS3896 + _M0L1wS1218;
    #line 350 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
    _M0MPC15array5Array3setGfE(_M0L4gabaS3894, _M0L9post__idxS1217, _M0L6_2atmpS3895);
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt11record__one(
  struct _M0TP26RiantR8snn__mbt7Monitor* _M0L1mS1213,
  float _M0L1tS1215
) {
  int32_t _M0L11step__countS3873;
  int32_t _M0L6_2atmpS3872;
  int32_t _M0L11step__countS3875;
  int32_t _M0L9rec__stepS3876;
  int32_t _M0L6_2atmpS3874;
  moonbit_string_t _M0L3symS3879;
  float _M0L1vS1214;
  struct _M0TPB5ArrayGfE* _M0L4dataS3877;
  struct _M0TPB5ArrayGfE* _M0L5timesS3878;
  #line 61 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
  _M0L11step__countS3873 = _M0L1mS1213->$6;
  _M0L6_2atmpS3872 = _M0L11step__countS3873 + 1;
  _M0L1mS1213->$6 = _M0L6_2atmpS3872;
  _M0L11step__countS3875 = _M0L1mS1213->$6;
  _M0L9rec__stepS3876 = _M0L1mS1213->$5;
  _M0L6_2atmpS3874 = _M0L11step__countS3875 % _M0L9rec__stepS3876;
  if (_M0L6_2atmpS3874 != 0) {
    return 0;
  }
  _M0L3symS3879 = _M0L1mS1213->$1;
  #line 66 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
  if (
    _M0L3symS3879 == (moonbit_string_t)moonbit_string_literal_2.data
    || Moonbit_array_length(_M0L3symS3879)
       == Moonbit_array_length((moonbit_string_t)moonbit_string_literal_2.data)
       && 0
          == memcmp(_M0L3symS3879, (moonbit_string_t)moonbit_string_literal_2.data, Moonbit_array_length(_M0L3symS3879) * 2)
  ) {
    struct _M0TP26RiantR8snn__mbt2IF* _M0L3popS3882 = _M0L1mS1213->$0;
    struct _M0TPB5ArrayGfE* _M0L1vS3880 = _M0L3popS3882->$3;
    int32_t _M0L6neuronS3881 = _M0L1mS1213->$4;
    #line 67 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
    _M0L1vS1214 = _M0MPC15array5Array2atGfE(_M0L1vS3880, _M0L6neuronS3881);
  } else {
    moonbit_string_t _M0L3symS3883 = _M0L1mS1213->$1;
    #line 68 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
    if (
      _M0L3symS3883 == (moonbit_string_t)moonbit_string_literal_3.data
      || Moonbit_array_length(_M0L3symS3883)
         == Moonbit_array_length((moonbit_string_t)moonbit_string_literal_3.data)
         && 0
            == memcmp(_M0L3symS3883, (moonbit_string_t)moonbit_string_literal_3.data, Moonbit_array_length(_M0L3symS3883) * 2)
    ) {
      struct _M0TP26RiantR8snn__mbt2IF* _M0L3popS3886 = _M0L1mS1213->$0;
      struct _M0TPB5ArrayGbE* _M0L4fireS3884 = _M0L3popS3886->$5;
      int32_t _M0L6neuronS3885 = _M0L1mS1213->$4;
      #line 69 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS3884, _M0L6neuronS3885)) {
        _M0L1vS1214 = 0x1p+0f;
      } else {
        _M0L1vS1214 = 0x0p+0f;
      }
    } else {
      _M0L1vS1214 = 0x0p+0f;
    }
  }
  _M0L4dataS3877 = _M0L1mS1213->$2;
  #line 73 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
  _M0MPC15array5Array4pushGfE(_M0L4dataS3877, _M0L1vS1214);
  _M0L5timesS3878 = _M0L1mS1213->$3;
  #line 74 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
  _M0MPC15array5Array4pushGfE(_M0L5timesS3878, _M0L1tS1215);
  return 0;
}

struct _M0TP26RiantR8snn__mbt7Monitor* _M0MP26RiantR8snn__mbt7Monitor9new__fire(
  struct _M0TP26RiantR8snn__mbt2IF* _M0L3popS1211,
  int32_t _M0L6neuronS1212
) {
  float* _M0L6_2atmpS3871;
  struct _M0TPB5ArrayGfE* _M0L6_2atmpS3868;
  float* _M0L6_2atmpS3870;
  struct _M0TPB5ArrayGfE* _M0L6_2atmpS3869;
  struct _M0TP26RiantR8snn__mbt7Monitor* _block_5386;
  #line 55 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
  _M0L6_2atmpS3871 = moonbit_empty_float_array;
  _M0L6_2atmpS3868
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_M0L6_2atmpS3868)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 21, 0);
  _M0L6_2atmpS3868->$0 = _M0L6_2atmpS3871;
  _M0L6_2atmpS3868->$1 = 0;
  _M0L6_2atmpS3870 = moonbit_empty_float_array;
  _M0L6_2atmpS3869
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_M0L6_2atmpS3869)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 21, 0);
  _M0L6_2atmpS3869->$0 = _M0L6_2atmpS3870;
  _M0L6_2atmpS3869->$1 = 0;
  moonbit_incref(_M0L3popS1211);
  _block_5386
  = (struct _M0TP26RiantR8snn__mbt7Monitor*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt7Monitor));
  Moonbit_object_header(_block_5386)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 57, 0);
  _block_5386->$0 = _M0L3popS1211;
  _block_5386->$1 = (moonbit_string_t)moonbit_string_literal_3.data;
  _block_5386->$2 = _M0L6_2atmpS3868;
  _block_5386->$3 = _M0L6_2atmpS3869;
  _block_5386->$4 = _M0L6neuronS1212;
  _block_5386->$5 = 1;
  _block_5386->$6 = 0;
  return _block_5386;
}

int32_t _M0FP26RiantR8snn__mbt17synaptic__current(
  struct _M0TP26RiantR8snn__mbt2IF* _M0L1pS1207
) {
  int32_t _M0L1nS1206;
  int32_t _M0L7_2abindS1208;
  int32_t _M0L1iS1209;
  #line 165 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L1nS1206 = _M0L1pS1207->$2;
  _M0L7_2abindS1208 = 0;
  _M0L1iS1209 = _M0L7_2abindS1208;
  while (1) {
    if (_M0L1iS1209 < _M0L1nS1206) {
      struct _M0TPB5ArrayGfE* _M0L9syn__currS3845 = _M0L1pS1207->$8;
      struct _M0TPB5ArrayGfE* _M0L2geS3866 = _M0L1pS1207->$9;
      float _M0L6_2atmpS3861;
      struct _M0TPB5ArrayGfE* _M0L1vS3865;
      float _M0L6_2atmpS3863;
      float _M0L4e__eS3864;
      float _M0L6_2atmpS3862;
      float _M0L6_2atmpS3858;
      struct _M0TPB5ArrayGfE* _M0L7gsyn__eS3860;
      float _M0L6_2atmpS3859;
      float _M0L6_2atmpS3847;
      struct _M0TPB5ArrayGfE* _M0L2giS3857;
      float _M0L6_2atmpS3852;
      struct _M0TPB5ArrayGfE* _M0L1vS3856;
      float _M0L6_2atmpS3854;
      float _M0L4e__iS3855;
      float _M0L6_2atmpS3853;
      float _M0L6_2atmpS3849;
      struct _M0TPB5ArrayGfE* _M0L7gsyn__iS3851;
      float _M0L6_2atmpS3850;
      float _M0L6_2atmpS3848;
      float _M0L6_2atmpS3846;
      int32_t _M0L6_2atmpS3867;
      #line 168 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS3861 = _M0MPC15array5Array2atGfE(_M0L2geS3866, _M0L1iS1209);
      _M0L1vS3865 = _M0L1pS1207->$3;
      #line 168 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS3863 = _M0MPC15array5Array2atGfE(_M0L1vS3865, _M0L1iS1209);
      _M0L4e__eS3864 = _M0L1pS1207->$17;
      _M0L6_2atmpS3862 = _M0L6_2atmpS3863 - _M0L4e__eS3864;
      _M0L6_2atmpS3858 = _M0L6_2atmpS3861 * _M0L6_2atmpS3862;
      _M0L7gsyn__eS3860 = _M0L1pS1207->$15;
      #line 168 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS3859
      = _M0MPC15array5Array2atGfE(_M0L7gsyn__eS3860, _M0L1iS1209);
      _M0L6_2atmpS3847 = _M0L6_2atmpS3858 * _M0L6_2atmpS3859;
      _M0L2giS3857 = _M0L1pS1207->$10;
      #line 169 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS3852 = _M0MPC15array5Array2atGfE(_M0L2giS3857, _M0L1iS1209);
      _M0L1vS3856 = _M0L1pS1207->$3;
      #line 169 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS3854 = _M0MPC15array5Array2atGfE(_M0L1vS3856, _M0L1iS1209);
      _M0L4e__iS3855 = _M0L1pS1207->$18;
      _M0L6_2atmpS3853 = _M0L6_2atmpS3854 - _M0L4e__iS3855;
      _M0L6_2atmpS3849 = _M0L6_2atmpS3852 * _M0L6_2atmpS3853;
      _M0L7gsyn__iS3851 = _M0L1pS1207->$16;
      #line 169 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS3850
      = _M0MPC15array5Array2atGfE(_M0L7gsyn__iS3851, _M0L1iS1209);
      _M0L6_2atmpS3848 = _M0L6_2atmpS3849 * _M0L6_2atmpS3850;
      _M0L6_2atmpS3846 = _M0L6_2atmpS3847 + _M0L6_2atmpS3848;
      #line 168 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L9syn__currS3845, _M0L1iS1209, _M0L6_2atmpS3846);
      _M0L6_2atmpS3867 = _M0L1iS1209 + 1;
      _M0L1iS1209 = _M0L6_2atmpS3867;
      continue;
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt14step__synapses(
  struct _M0TP26RiantR8snn__mbt2IF* _M0L1pS1198,
  float _M0L2dtS1201
) {
  int32_t _M0L1nS1197;
  int32_t _M0L7_2abindS1199;
  int32_t _M0L1iS1200;
  int32_t _M0L7_2abindS1203;
  int32_t _M0L1iS1204;
  #line 146 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L1nS1197 = _M0L1pS1198->$2;
  _M0L7_2abindS1199 = 0;
  _M0L1iS1200 = _M0L7_2abindS1199;
  while (1) {
    if (_M0L1iS1200 < _M0L1nS1197) {
      struct _M0TPB5ArrayGfE* _M0L2heS3783 = _M0L1pS1198->$11;
      struct _M0TPB5ArrayGfE* _M0L2heS3788 = _M0L1pS1198->$11;
      float _M0L6_2atmpS3785;
      struct _M0TPB5ArrayGfE* _M0L3gluS3787;
      float _M0L6_2atmpS3786;
      float _M0L6_2atmpS3784;
      struct _M0TPB5ArrayGfE* _M0L2hiS3789;
      struct _M0TPB5ArrayGfE* _M0L2hiS3794;
      float _M0L6_2atmpS3791;
      struct _M0TPB5ArrayGfE* _M0L4gabaS3793;
      float _M0L6_2atmpS3792;
      float _M0L6_2atmpS3790;
      struct _M0TPB5ArrayGfE* _M0L2geS3795;
      struct _M0TPB5ArrayGfE* _M0L2geS3807;
      float _M0L6_2atmpS3797;
      struct _M0TPB5ArrayGfE* _M0L2geS3806;
      float _M0L6_2atmpS3805;
      float _M0L6_2atmpS3803;
      float _M0L3tdeS3804;
      float _M0L6_2atmpS3800;
      struct _M0TPB5ArrayGfE* _M0L2heS3802;
      float _M0L6_2atmpS3801;
      float _M0L6_2atmpS3799;
      float _M0L6_2atmpS3798;
      float _M0L6_2atmpS3796;
      struct _M0TPB5ArrayGfE* _M0L2heS3808;
      struct _M0TPB5ArrayGfE* _M0L2heS3817;
      float _M0L6_2atmpS3810;
      struct _M0TPB5ArrayGfE* _M0L2heS3816;
      float _M0L6_2atmpS3815;
      float _M0L6_2atmpS3813;
      float _M0L3treS3814;
      float _M0L6_2atmpS3812;
      float _M0L6_2atmpS3811;
      float _M0L6_2atmpS3809;
      struct _M0TPB5ArrayGfE* _M0L2giS3818;
      struct _M0TPB5ArrayGfE* _M0L2giS3830;
      float _M0L6_2atmpS3820;
      struct _M0TPB5ArrayGfE* _M0L2giS3829;
      float _M0L6_2atmpS3828;
      float _M0L6_2atmpS3826;
      float _M0L3tdiS3827;
      float _M0L6_2atmpS3823;
      struct _M0TPB5ArrayGfE* _M0L2hiS3825;
      float _M0L6_2atmpS3824;
      float _M0L6_2atmpS3822;
      float _M0L6_2atmpS3821;
      float _M0L6_2atmpS3819;
      struct _M0TPB5ArrayGfE* _M0L2hiS3831;
      struct _M0TPB5ArrayGfE* _M0L2hiS3840;
      float _M0L6_2atmpS3833;
      struct _M0TPB5ArrayGfE* _M0L2hiS3839;
      float _M0L6_2atmpS3838;
      float _M0L6_2atmpS3836;
      float _M0L3triS3837;
      float _M0L6_2atmpS3835;
      float _M0L6_2atmpS3834;
      float _M0L6_2atmpS3832;
      int32_t _M0L6_2atmpS3841;
      #line 149 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS3785 = _M0MPC15array5Array2atGfE(_M0L2heS3788, _M0L1iS1200);
      _M0L3gluS3787 = _M0L1pS1198->$13;
      #line 149 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS3786
      = _M0MPC15array5Array2atGfE(_M0L3gluS3787, _M0L1iS1200);
      _M0L6_2atmpS3784 = _M0L6_2atmpS3785 + _M0L6_2atmpS3786;
      #line 149 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L2heS3783, _M0L1iS1200, _M0L6_2atmpS3784);
      _M0L2hiS3789 = _M0L1pS1198->$12;
      _M0L2hiS3794 = _M0L1pS1198->$12;
      #line 150 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS3791 = _M0MPC15array5Array2atGfE(_M0L2hiS3794, _M0L1iS1200);
      _M0L4gabaS3793 = _M0L1pS1198->$14;
      #line 150 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS3792
      = _M0MPC15array5Array2atGfE(_M0L4gabaS3793, _M0L1iS1200);
      _M0L6_2atmpS3790 = _M0L6_2atmpS3791 + _M0L6_2atmpS3792;
      #line 150 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L2hiS3789, _M0L1iS1200, _M0L6_2atmpS3790);
      _M0L2geS3795 = _M0L1pS1198->$9;
      _M0L2geS3807 = _M0L1pS1198->$9;
      #line 151 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS3797 = _M0MPC15array5Array2atGfE(_M0L2geS3807, _M0L1iS1200);
      _M0L2geS3806 = _M0L1pS1198->$9;
      #line 151 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS3805 = _M0MPC15array5Array2atGfE(_M0L2geS3806, _M0L1iS1200);
      _M0L6_2atmpS3803 = -_M0L6_2atmpS3805;
      _M0L3tdeS3804 = _M0L1pS1198->$20;
      _M0L6_2atmpS3800 = _M0L6_2atmpS3803 / _M0L3tdeS3804;
      _M0L2heS3802 = _M0L1pS1198->$11;
      #line 151 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS3801 = _M0MPC15array5Array2atGfE(_M0L2heS3802, _M0L1iS1200);
      _M0L6_2atmpS3799 = _M0L6_2atmpS3800 + _M0L6_2atmpS3801;
      _M0L6_2atmpS3798 = _M0L2dtS1201 * _M0L6_2atmpS3799;
      _M0L6_2atmpS3796 = _M0L6_2atmpS3797 + _M0L6_2atmpS3798;
      #line 151 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L2geS3795, _M0L1iS1200, _M0L6_2atmpS3796);
      _M0L2heS3808 = _M0L1pS1198->$11;
      _M0L2heS3817 = _M0L1pS1198->$11;
      #line 152 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS3810 = _M0MPC15array5Array2atGfE(_M0L2heS3817, _M0L1iS1200);
      _M0L2heS3816 = _M0L1pS1198->$11;
      #line 152 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS3815 = _M0MPC15array5Array2atGfE(_M0L2heS3816, _M0L1iS1200);
      _M0L6_2atmpS3813 = -_M0L6_2atmpS3815;
      _M0L3treS3814 = _M0L1pS1198->$19;
      _M0L6_2atmpS3812 = _M0L6_2atmpS3813 / _M0L3treS3814;
      _M0L6_2atmpS3811 = _M0L2dtS1201 * _M0L6_2atmpS3812;
      _M0L6_2atmpS3809 = _M0L6_2atmpS3810 + _M0L6_2atmpS3811;
      #line 152 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L2heS3808, _M0L1iS1200, _M0L6_2atmpS3809);
      _M0L2giS3818 = _M0L1pS1198->$10;
      _M0L2giS3830 = _M0L1pS1198->$10;
      #line 153 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS3820 = _M0MPC15array5Array2atGfE(_M0L2giS3830, _M0L1iS1200);
      _M0L2giS3829 = _M0L1pS1198->$10;
      #line 153 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS3828 = _M0MPC15array5Array2atGfE(_M0L2giS3829, _M0L1iS1200);
      _M0L6_2atmpS3826 = -_M0L6_2atmpS3828;
      _M0L3tdiS3827 = _M0L1pS1198->$22;
      _M0L6_2atmpS3823 = _M0L6_2atmpS3826 / _M0L3tdiS3827;
      _M0L2hiS3825 = _M0L1pS1198->$12;
      #line 153 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS3824 = _M0MPC15array5Array2atGfE(_M0L2hiS3825, _M0L1iS1200);
      _M0L6_2atmpS3822 = _M0L6_2atmpS3823 + _M0L6_2atmpS3824;
      _M0L6_2atmpS3821 = _M0L2dtS1201 * _M0L6_2atmpS3822;
      _M0L6_2atmpS3819 = _M0L6_2atmpS3820 + _M0L6_2atmpS3821;
      #line 153 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L2giS3818, _M0L1iS1200, _M0L6_2atmpS3819);
      _M0L2hiS3831 = _M0L1pS1198->$12;
      _M0L2hiS3840 = _M0L1pS1198->$12;
      #line 154 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS3833 = _M0MPC15array5Array2atGfE(_M0L2hiS3840, _M0L1iS1200);
      _M0L2hiS3839 = _M0L1pS1198->$12;
      #line 154 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS3838 = _M0MPC15array5Array2atGfE(_M0L2hiS3839, _M0L1iS1200);
      _M0L6_2atmpS3836 = -_M0L6_2atmpS3838;
      _M0L3triS3837 = _M0L1pS1198->$21;
      _M0L6_2atmpS3835 = _M0L6_2atmpS3836 / _M0L3triS3837;
      _M0L6_2atmpS3834 = _M0L2dtS1201 * _M0L6_2atmpS3835;
      _M0L6_2atmpS3832 = _M0L6_2atmpS3833 + _M0L6_2atmpS3834;
      #line 154 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L2hiS3831, _M0L1iS1200, _M0L6_2atmpS3832);
      _M0L6_2atmpS3841 = _M0L1iS1200 + 1;
      _M0L1iS1200 = _M0L6_2atmpS3841;
      continue;
    }
    break;
  }
  _M0L7_2abindS1203 = 0;
  _M0L1iS1204 = _M0L7_2abindS1203;
  while (1) {
    if (_M0L1iS1204 < _M0L1nS1197) {
      struct _M0TPB5ArrayGfE* _M0L3gluS3842 = _M0L1pS1198->$13;
      struct _M0TPB5ArrayGfE* _M0L4gabaS3843;
      int32_t _M0L6_2atmpS3844;
      #line 157 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L3gluS3842, _M0L1iS1204, 0x0p+0f);
      _M0L4gabaS3843 = _M0L1pS1198->$14;
      #line 158 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L4gabaS3843, _M0L1iS1204, 0x0p+0f);
      _M0L6_2atmpS3844 = _M0L1iS1204 + 1;
      _M0L1iS1204 = _M0L6_2atmpS3844;
      continue;
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt12step__neuron(
  struct _M0TP26RiantR8snn__mbt2IF* _M0L1pS1183,
  float _M0L2dtS1192
) {
  int32_t _M0L1nS1182;
  struct _M0TP26RiantR8snn__mbt11IFParameter* _M0L3p__S1184;
  float _M0L2tmS1185;
  float _M0L2elS1186;
  float _M0L1rS1187;
  float _M0L2vtS1188;
  float _M0L2vrS1189;
  struct _M0TP26RiantR8snn__mbt9PostSpike* _M0L5spikeS3782;
  float _M0L11tabs__constS1190;
  float _M0L6_2atmpS3781;
  int32_t _M0L11tabs__stepsS1191;
  int32_t _M0L7_2abindS1193;
  int32_t _M0L1iS1194;
  #line 176 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L1nS1182 = _M0L1pS1183->$2;
  _M0L3p__S1184 = _M0L1pS1183->$0;
  _M0L2tmS1185 = _M0L3p__S1184->$2;
  _M0L2elS1186 = _M0L3p__S1184->$5;
  _M0L1rS1187 = _M0L3p__S1184->$6;
  _M0L2vtS1188 = _M0L3p__S1184->$3;
  _M0L2vrS1189 = _M0L3p__S1184->$4;
  _M0L5spikeS3782 = _M0L1pS1183->$1;
  _M0L11tabs__constS1190 = _M0L5spikeS3782->$0;
  _M0L6_2atmpS3781 = _M0L11tabs__constS1190 / _M0L2dtS1192;
  #line 185 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L11tabs__stepsS1191 = _M0MPC15float5Float7to__int(_M0L6_2atmpS3781);
  _M0L7_2abindS1193 = 0;
  _M0L1iS1194 = _M0L7_2abindS1193;
  while (1) {
    if (_M0L1iS1194 < _M0L1nS1182) {
      struct _M0TPB5ArrayGiE* _M0L4tabsS3741 = _M0L1pS1183->$6;
      int32_t _M0L6_2atmpS3740;
      struct _M0TPB5ArrayGfE* _M0L1vS3747;
      struct _M0TPB5ArrayGfE* _M0L1vS3768;
      float _M0L6_2atmpS3749;
      float _M0L6_2atmpS3751;
      struct _M0TPB5ArrayGfE* _M0L1vS3767;
      float _M0L6_2atmpS3766;
      float _M0L6_2atmpS3765;
      float _M0L6_2atmpS3757;
      struct _M0TPB5ArrayGfE* _M0L1wS3764;
      float _M0L6_2atmpS3763;
      float _M0L6_2atmpS3760;
      struct _M0TPB5ArrayGfE* _M0L1iS3762;
      float _M0L6_2atmpS3761;
      float _M0L6_2atmpS3759;
      float _M0L6_2atmpS3758;
      float _M0L6_2atmpS3753;
      struct _M0TPB5ArrayGfE* _M0L9syn__currS3756;
      float _M0L6_2atmpS3755;
      float _M0L6_2atmpS3754;
      float _M0L6_2atmpS3752;
      float _M0L6_2atmpS3750;
      float _M0L6_2atmpS3748;
      struct _M0TPB5ArrayGbE* _M0L4fireS3769;
      struct _M0TPB5ArrayGfE* _M0L1vS3772;
      float _M0L6_2atmpS3771;
      int32_t _M0L6_2atmpS3770;
      struct _M0TPB5ArrayGfE* _M0L1vS3773;
      struct _M0TPB5ArrayGbE* _M0L4fireS3775;
      float _M0L6_2atmpS3774;
      struct _M0TPB5ArrayGiE* _M0L4tabsS3777;
      struct _M0TPB5ArrayGbE* _M0L4fireS3779;
      int32_t _M0L6_2atmpS3778;
      int32_t _M0L6_2atmpS3739;
      #line 188 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS3740
      = _M0MPC15array5Array2atGiE(_M0L4tabsS3741, _M0L1iS1194);
      if (_M0L6_2atmpS3740 > 0) {
        struct _M0TPB5ArrayGbE* _M0L4fireS3742 = _M0L1pS1183->$5;
        struct _M0TPB5ArrayGiE* _M0L4tabsS3743;
        struct _M0TPB5ArrayGiE* _M0L4tabsS3746;
        int32_t _M0L6_2atmpS3745;
        int32_t _M0L6_2atmpS3744;
        #line 189 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
        _M0MPC15array5Array3setGbE(_M0L4fireS3742, _M0L1iS1194, 0);
        _M0L4tabsS3743 = _M0L1pS1183->$6;
        _M0L4tabsS3746 = _M0L1pS1183->$6;
        #line 190 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
        _M0L6_2atmpS3745
        = _M0MPC15array5Array2atGiE(_M0L4tabsS3746, _M0L1iS1194);
        _M0L6_2atmpS3744 = _M0L6_2atmpS3745 - 1;
        #line 190 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
        _M0MPC15array5Array3setGiE(_M0L4tabsS3743, _M0L1iS1194, _M0L6_2atmpS3744);
        goto join_1195;
      }
      _M0L1vS3747 = _M0L1pS1183->$3;
      _M0L1vS3768 = _M0L1pS1183->$3;
      #line 193 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS3749 = _M0MPC15array5Array2atGfE(_M0L1vS3768, _M0L1iS1194);
      _M0L6_2atmpS3751 = _M0L2dtS1192 / _M0L2tmS1185;
      _M0L1vS3767 = _M0L1pS1183->$3;
      #line 194 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS3766 = _M0MPC15array5Array2atGfE(_M0L1vS3767, _M0L1iS1194);
      _M0L6_2atmpS3765 = _M0L6_2atmpS3766 - _M0L2elS1186;
      _M0L6_2atmpS3757 = -_M0L6_2atmpS3765;
      _M0L1wS3764 = _M0L1pS1183->$4;
      #line 194 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS3763 = _M0MPC15array5Array2atGfE(_M0L1wS3764, _M0L1iS1194);
      _M0L6_2atmpS3760 = -_M0L6_2atmpS3763;
      _M0L1iS3762 = _M0L1pS1183->$7;
      #line 194 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS3761 = _M0MPC15array5Array2atGfE(_M0L1iS3762, _M0L1iS1194);
      _M0L6_2atmpS3759 = _M0L6_2atmpS3760 + _M0L6_2atmpS3761;
      _M0L6_2atmpS3758 = _M0L1rS1187 * _M0L6_2atmpS3759;
      _M0L6_2atmpS3753 = _M0L6_2atmpS3757 + _M0L6_2atmpS3758;
      _M0L9syn__currS3756 = _M0L1pS1183->$8;
      #line 194 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS3755
      = _M0MPC15array5Array2atGfE(_M0L9syn__currS3756, _M0L1iS1194);
      _M0L6_2atmpS3754 = _M0L1rS1187 * _M0L6_2atmpS3755;
      _M0L6_2atmpS3752 = _M0L6_2atmpS3753 - _M0L6_2atmpS3754;
      _M0L6_2atmpS3750 = _M0L6_2atmpS3751 * _M0L6_2atmpS3752;
      _M0L6_2atmpS3748 = _M0L6_2atmpS3749 + _M0L6_2atmpS3750;
      #line 193 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS3747, _M0L1iS1194, _M0L6_2atmpS3748);
      _M0L4fireS3769 = _M0L1pS1183->$5;
      _M0L1vS3772 = _M0L1pS1183->$3;
      #line 195 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS3771 = _M0MPC15array5Array2atGfE(_M0L1vS3772, _M0L1iS1194);
      _M0L6_2atmpS3770 = _M0L6_2atmpS3771 > _M0L2vtS1188;
      #line 195 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGbE(_M0L4fireS3769, _M0L1iS1194, _M0L6_2atmpS3770);
      _M0L1vS3773 = _M0L1pS1183->$3;
      _M0L4fireS3775 = _M0L1pS1183->$5;
      #line 196 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS3775, _M0L1iS1194)) {
        _M0L6_2atmpS3774 = _M0L2vrS1189;
      } else {
        struct _M0TPB5ArrayGfE* _M0L1vS3776 = _M0L1pS1183->$3;
        #line 196 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
        _M0L6_2atmpS3774
        = _M0MPC15array5Array2atGfE(_M0L1vS3776, _M0L1iS1194);
      }
      #line 196 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS3773, _M0L1iS1194, _M0L6_2atmpS3774);
      _M0L4tabsS3777 = _M0L1pS1183->$6;
      _M0L4fireS3779 = _M0L1pS1183->$5;
      #line 197 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS3779, _M0L1iS1194)) {
        _M0L6_2atmpS3778 = _M0L11tabs__stepsS1191;
      } else {
        struct _M0TPB5ArrayGiE* _M0L4tabsS3780 = _M0L1pS1183->$6;
        #line 197 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
        _M0L6_2atmpS3778
        = _M0MPC15array5Array2atGiE(_M0L4tabsS3780, _M0L1iS1194);
      }
      #line 197 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGiE(_M0L4tabsS3777, _M0L1iS1194, _M0L6_2atmpS3778);
      goto join_1195;
      goto joinlet_5391;
      join_1195:;
      _M0L6_2atmpS3739 = _M0L1iS1194 + 1;
      _M0L1iS1194 = _M0L6_2atmpS3739;
      continue;
      joinlet_5391:;
    }
    break;
  }
  return 0;
}

int32_t _M0MP26RiantR8snn__mbt15SparseMatrixCSR7forward(
  struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L1mS1170,
  struct _M0TPB5ArrayGbE* _M0L9pre__fireS1173,
  struct _M0TPB5ArrayGfE* _M0L7post__gS1179
) {
  int32_t _M0L4rowsS1169;
  int32_t _M0L7_2abindS1171;
  int32_t _M0L1iS1172;
  #line 287 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  _M0L4rowsS1169 = _M0L1mS1170->$0;
  _M0L7_2abindS1171 = 0;
  _M0L1iS1172 = _M0L7_2abindS1171;
  while (1) {
    if (_M0L1iS1172 < _M0L4rowsS1169) {
      int32_t _M0L6_2atmpS3738;
      #line 294 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L9pre__fireS1173, _M0L1iS1172)) {
        struct _M0TPB5ArrayGiE* _M0L6rowptrS3737 = _M0L1mS1170->$2;
        int32_t _M0L5startS1174;
        struct _M0TPB5ArrayGiE* _M0L6rowptrS3735;
        int32_t _M0L6_2atmpS3736;
        int32_t _M0L3endS1175;
        int32_t _M0L1kS1176;
        #line 295 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
        _M0L5startS1174
        = _M0MPC15array5Array2atGiE(_M0L6rowptrS3737, _M0L1iS1172);
        _M0L6rowptrS3735 = _M0L1mS1170->$2;
        _M0L6_2atmpS3736 = _M0L1iS1172 + 1;
        #line 296 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
        _M0L3endS1175
        = _M0MPC15array5Array2atGiE(_M0L6rowptrS3735, _M0L6_2atmpS3736);
        _M0L1kS1176 = _M0L5startS1174;
        while (1) {
          if (_M0L1kS1176 < _M0L3endS1175) {
            struct _M0TPB5ArrayGiE* _M0L6colptrS3733 = _M0L1mS1170->$3;
            int32_t _M0L9post__idxS1177;
            struct _M0TPB5ArrayGfE* _M0L4valsS3732;
            float _M0L1wS1178;
            float _M0L6_2atmpS3731;
            float _M0L6_2atmpS3730;
            int32_t _M0L6_2atmpS3734;
            #line 298 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
            _M0L9post__idxS1177
            = _M0MPC15array5Array2atGiE(_M0L6colptrS3733, _M0L1kS1176);
            _M0L4valsS3732 = _M0L1mS1170->$4;
            #line 299 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
            _M0L1wS1178
            = _M0MPC15array5Array2atGfE(_M0L4valsS3732, _M0L1kS1176);
            #line 300 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
            _M0L6_2atmpS3731
            = _M0MPC15array5Array2atGfE(_M0L7post__gS1179, _M0L9post__idxS1177);
            _M0L6_2atmpS3730 = _M0L6_2atmpS3731 + _M0L1wS1178;
            #line 300 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
            _M0MPC15array5Array3setGfE(_M0L7post__gS1179, _M0L9post__idxS1177, _M0L6_2atmpS3730);
            _M0L6_2atmpS3734 = _M0L1kS1176 + 1;
            _M0L1kS1176 = _M0L6_2atmpS3734;
            continue;
          }
          break;
        }
      }
      _M0L6_2atmpS3738 = _M0L1iS1172 + 1;
      _M0L1iS1172 = _M0L6_2atmpS3738;
      continue;
    }
    break;
  }
  return 0;
}

struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0MP26RiantR8snn__mbt15SparseMatrixCSR6random(
  int32_t _M0L4rowsS1163,
  int32_t _M0L4colsS1164,
  float _M0L2muS1165,
  float _M0L5sigmaS1166,
  float _M0L1pS1167,
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS1168
) {
  #line 94 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  #line 103 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  return _M0MP26RiantR8snn__mbt15SparseMatrixCSR18random__with__rule(_M0L4rowsS1163, _M0L4colsS1164, _M0L2muS1165, _M0L5sigmaS1166, _M0L1pS1167, 0, _M0L3rngS1168);
}

struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0MP26RiantR8snn__mbt15SparseMatrixCSR18random__with__rule(
  int32_t _M0L4rowsS1077,
  int32_t _M0L4colsS1081,
  float _M0L2muS1087,
  float _M0L5sigmaS1088,
  float _M0L1pS1100,
  int32_t _M0L4ruleS1094,
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS1090
) {
  float* _M0L6_2atmpS3729;
  struct _M0TPB5ArrayGfE* _M0L6_2atmpS3728;
  struct _M0TPB5ArrayGRPB5ArrayGfEE* _M0L5denseS1076;
  int32_t _M0L7_2abindS1078;
  int32_t _M0L1iS1079;
  int32_t _M0L6_2atmpS3727;
  struct _M0TPB5ArrayGiE* _M0L6rowptrS1153;
  int32_t* _M0L6_2atmpS3726;
  struct _M0TPB5ArrayGiE* _M0L6colptrS1154;
  float* _M0L6_2atmpS3725;
  struct _M0TPB5ArrayGfE* _M0L4valsS1155;
  int32_t _M0L7_2abindS1156;
  int32_t _M0L1iS1157;
  int32_t _M0L6_2atmpS3724;
  struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _block_5413;
  #line 109 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  _M0L6_2atmpS3729 = moonbit_empty_float_array;
  _M0L6_2atmpS3728
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_M0L6_2atmpS3728)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 21, 0);
  _M0L6_2atmpS3728->$0 = _M0L6_2atmpS3729;
  _M0L6_2atmpS3728->$1 = 0;
  #line 120 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  _M0L5denseS1076
  = _M0MPC15array5Array4makeGRPB5ArrayGfEE(_M0L4rowsS1077, _M0L6_2atmpS3728);
  _M0L7_2abindS1078 = 0;
  _M0L1iS1079 = _M0L7_2abindS1078;
  while (1) {
    if (_M0L1iS1079 < _M0L4rowsS1077) {
      struct _M0TPB5ArrayGfE* _M0L3rowS1080;
      int32_t _M0L7_2abindS1082;
      int32_t _M0L1jS1083;
      int32_t _M0L6_2atmpS3682;
      #line 122 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
      _M0L3rowS1080 = _M0MPC15array5Array4makeGfE(_M0L4colsS1081, 0x0p+0f);
      _M0L7_2abindS1082 = 0;
      _M0L1jS1083 = _M0L7_2abindS1082;
      while (1) {
        if (_M0L1jS1083 < _M0L4colsS1081) {
          double _M0L2z1S1085;
          struct _M0TUddE* _M0L7_2abindS1089;
          double _M0L5_2az1S1091;
          float _M0L6_2atmpS3680;
          float _M0L6_2atmpS3679;
          float _M0L1wS1086;
          int32_t _M0L6_2atmpS3681;
          #line 131 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
          _M0L7_2abindS1089
          = _M0FP26RiantR8snn__mbt11box__muller(_M0L3rngS1090);
          _M0L5_2az1S1091 = _M0L7_2abindS1089->$0;
          moonbit_decref(_M0L7_2abindS1089);
          _M0L2z1S1085 = _M0L5_2az1S1091;
          goto join_1084;
          goto joinlet_5396;
          join_1084:;
          _M0L6_2atmpS3680 = (float)_M0L2z1S1085;
          _M0L6_2atmpS3679 = _M0L5sigmaS1088 * _M0L6_2atmpS3680;
          _M0L1wS1086 = _M0L2muS1087 + _M0L6_2atmpS3679;
          #line 133 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
          _M0MPC15array5Array3setGfE(_M0L3rowS1080, _M0L1jS1083, _M0L1wS1086);
          joinlet_5396:;
          _M0L6_2atmpS3681 = _M0L1jS1083 + 1;
          _M0L1jS1083 = _M0L6_2atmpS3681;
          continue;
        }
        break;
      }
      #line 135 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
      _M0MPC15array5Array3setGRPB5ArrayGfEE(_M0L5denseS1076, _M0L1iS1079, _M0L3rowS1080);
      _M0L6_2atmpS3682 = _M0L1iS1079 + 1;
      _M0L1iS1079 = _M0L6_2atmpS3682;
      continue;
    }
    break;
  }
  switch (_M0L4ruleS1094) {
    case 0: {
      int32_t _M0L7_2abindS1095 = 0;
      int32_t _M0L1iS1096 = _M0L7_2abindS1095;
      while (1) {
        if (_M0L1iS1096 < _M0L4rowsS1077) {
          int32_t _M0L7_2abindS1097 = 0;
          int32_t _M0L1jS1098 = _M0L7_2abindS1097;
          int32_t _M0L6_2atmpS3685;
          while (1) {
            if (_M0L1jS1098 < _M0L4colsS1081) {
              float _M0L1uS1099;
              int32_t _M0L6_2atmpS3684;
              #line 143 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
              _M0L1uS1099 = _M0FP26RiantR8snn__mbt9next__f32(_M0L3rngS1090);
              if (_M0L1uS1099 >= _M0L1pS1100) {
                struct _M0TPB5ArrayGfE* _M0L6_2atmpS3683;
                #line 145 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS3683
                = _M0MPC15array5Array2atGRPB5ArrayGfEE(_M0L5denseS1076, _M0L1iS1096);
                #line 145 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array3setGfE(_M0L6_2atmpS3683, _M0L1jS1098, 0x0p+0f);
                moonbit_decref(_M0L6_2atmpS3683);
              }
              _M0L6_2atmpS3684 = _M0L1jS1098 + 1;
              _M0L1jS1098 = _M0L6_2atmpS3684;
              continue;
            }
            break;
          }
          _M0L6_2atmpS3685 = _M0L1iS1096 + 1;
          _M0L1iS1096 = _M0L6_2atmpS3685;
          continue;
        }
        break;
      }
      break;
    }
    
    case 1: {
      float _M0L6_2atmpS3702 = (float)_M0L4rowsS1077;
      float _M0L6_2atmpS3701 = _M0L6_2atmpS3702 * _M0L1pS1100;
      int32_t _M0L7n__keepS1103;
      #line 154 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
      _M0L7n__keepS1103 = _M0MPC15float5Float7to__int(_M0L6_2atmpS3701);
      if (_M0L7n__keepS1103 > 0 && _M0L7n__keepS1103 <= _M0L4rowsS1077) {
        int32_t _M0L7_2abindS1104 = 0;
        int32_t _M0L1jS1105 = _M0L7_2abindS1104;
        while (1) {
          if (_M0L1jS1105 < _M0L4colsS1081) {
            int32_t* _M0L6_2atmpS3696 = (int32_t*)moonbit_empty_int32_array;
            struct _M0TPB5ArrayGiE* _M0L8pre__idxS1106 =
              (struct _M0TPB5ArrayGiE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGiE));
            int32_t _M0L7_2abindS1107;
            int32_t _M0L1kS1108;
            int32_t _M0L7n__dropS1110;
            int32_t _M0L7_2abindS1111;
            int32_t _M0L1kS1112;
            int32_t _M0L7_2abindS1118;
            int32_t _M0L1kS1119;
            int32_t _M0L6_2atmpS3697;
            Moonbit_object_header(_M0L8pre__idxS1106)->meta
            = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 24, 0);
            _M0L8pre__idxS1106->$0 = _M0L6_2atmpS3696;
            _M0L8pre__idxS1106->$1 = 0;
            _M0L7_2abindS1107 = 0;
            _M0L1kS1108 = _M0L7_2abindS1107;
            while (1) {
              if (_M0L1kS1108 < _M0L4rowsS1077) {
                int32_t _M0L6_2atmpS3686;
                #line 161 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array4pushGiE(_M0L8pre__idxS1106, _M0L1kS1108);
                _M0L6_2atmpS3686 = _M0L1kS1108 + 1;
                _M0L1kS1108 = _M0L6_2atmpS3686;
                continue;
              }
              break;
            }
            _M0L7n__dropS1110 = _M0L4rowsS1077 - _M0L7n__keepS1103;
            _M0L7_2abindS1111 = 0;
            _M0L1kS1112 = _M0L7_2abindS1111;
            while (1) {
              if (_M0L1kS1112 < _M0L7n__dropS1110) {
                float _M0L1uS1113;
                int32_t _M0L6_2atmpS3691;
                float _M0L6_2atmpS3690;
                float _M0L6_2atmpS3689;
                int32_t _M0L6_2atmpS3688;
                int32_t _M0L6r__idxS1114;
                int32_t _M0L10r__clampedS1115;
                int32_t _M0L3tmpS1116;
                int32_t _M0L6_2atmpS3687;
                int32_t _M0L6_2atmpS3692;
                #line 167 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L1uS1113 = _M0FP26RiantR8snn__mbt9next__f32(_M0L3rngS1090);
                _M0L6_2atmpS3691 = _M0L4rowsS1077 - _M0L1kS1112;
                _M0L6_2atmpS3690 = (float)_M0L6_2atmpS3691;
                _M0L6_2atmpS3689 = _M0L6_2atmpS3690 * _M0L1uS1113;
                #line 168 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS3688
                = _M0MPC15float5Float7to__int(_M0L6_2atmpS3689);
                _M0L6r__idxS1114 = _M0L1kS1112 + _M0L6_2atmpS3688;
                if (_M0L6r__idxS1114 >= _M0L4rowsS1077) {
                  _M0L10r__clampedS1115 = _M0L4rowsS1077 - 1;
                } else {
                  _M0L10r__clampedS1115 = _M0L6r__idxS1114;
                }
                #line 171 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L3tmpS1116
                = _M0MPC15array5Array2atGiE(_M0L8pre__idxS1106, _M0L1kS1112);
                #line 172 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS3687
                = _M0MPC15array5Array2atGiE(_M0L8pre__idxS1106, _M0L10r__clampedS1115);
                #line 172 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array3setGiE(_M0L8pre__idxS1106, _M0L1kS1112, _M0L6_2atmpS3687);
                #line 173 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array3setGiE(_M0L8pre__idxS1106, _M0L10r__clampedS1115, _M0L3tmpS1116);
                _M0L6_2atmpS3692 = _M0L1kS1112 + 1;
                _M0L1kS1112 = _M0L6_2atmpS3692;
                continue;
              }
              break;
            }
            _M0L7_2abindS1118 = 0;
            _M0L1kS1119 = _M0L7_2abindS1118;
            while (1) {
              if (_M0L1kS1119 < _M0L7n__dropS1110) {
                int32_t _M0L6_2atmpS3694;
                struct _M0TPB5ArrayGfE* _M0L6_2atmpS3693;
                int32_t _M0L6_2atmpS3695;
                #line 176 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS3694
                = _M0MPC15array5Array2atGiE(_M0L8pre__idxS1106, _M0L1kS1119);
                #line 176 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS3693
                = _M0MPC15array5Array2atGRPB5ArrayGfEE(_M0L5denseS1076, _M0L6_2atmpS3694);
                #line 176 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array3setGfE(_M0L6_2atmpS3693, _M0L1jS1105, 0x0p+0f);
                moonbit_decref(_M0L6_2atmpS3693);
                _M0L6_2atmpS3695 = _M0L1kS1119 + 1;
                _M0L1kS1119 = _M0L6_2atmpS3695;
                continue;
              } else {
                moonbit_decref(_M0L8pre__idxS1106);
              }
              break;
            }
            _M0L6_2atmpS3697 = _M0L1jS1105 + 1;
            _M0L1jS1105 = _M0L6_2atmpS3697;
            continue;
          }
          break;
        }
      } else if (_M0L7n__keepS1103 == 0) {
        int32_t _M0L7_2abindS1122 = 0;
        int32_t _M0L1iS1123 = _M0L7_2abindS1122;
        while (1) {
          if (_M0L1iS1123 < _M0L4rowsS1077) {
            int32_t _M0L7_2abindS1124 = 0;
            int32_t _M0L1jS1125 = _M0L7_2abindS1124;
            int32_t _M0L6_2atmpS3700;
            while (1) {
              if (_M0L1jS1125 < _M0L4colsS1081) {
                struct _M0TPB5ArrayGfE* _M0L6_2atmpS3698;
                int32_t _M0L6_2atmpS3699;
                #line 183 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS3698
                = _M0MPC15array5Array2atGRPB5ArrayGfEE(_M0L5denseS1076, _M0L1iS1123);
                #line 183 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array3setGfE(_M0L6_2atmpS3698, _M0L1jS1125, 0x0p+0f);
                moonbit_decref(_M0L6_2atmpS3698);
                _M0L6_2atmpS3699 = _M0L1jS1125 + 1;
                _M0L1jS1125 = _M0L6_2atmpS3699;
                continue;
              }
              break;
            }
            _M0L6_2atmpS3700 = _M0L1iS1123 + 1;
            _M0L1iS1123 = _M0L6_2atmpS3700;
            continue;
          }
          break;
        }
      }
      break;
    }
    default: {
      float _M0L6_2atmpS3719 = (float)_M0L4colsS1081;
      float _M0L6_2atmpS3718 = _M0L6_2atmpS3719 * _M0L1pS1100;
      int32_t _M0L7n__keepS1128;
      #line 191 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
      _M0L7n__keepS1128 = _M0MPC15float5Float7to__int(_M0L6_2atmpS3718);
      if (_M0L7n__keepS1128 > 0 && _M0L7n__keepS1128 <= _M0L4colsS1081) {
        int32_t _M0L7_2abindS1129 = 0;
        int32_t _M0L1iS1130 = _M0L7_2abindS1129;
        while (1) {
          if (_M0L1iS1130 < _M0L4rowsS1077) {
            int32_t* _M0L6_2atmpS3713 = (int32_t*)moonbit_empty_int32_array;
            struct _M0TPB5ArrayGiE* _M0L9post__idxS1131 =
              (struct _M0TPB5ArrayGiE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGiE));
            int32_t _M0L7_2abindS1132;
            int32_t _M0L1kS1133;
            int32_t _M0L7n__dropS1135;
            int32_t _M0L7_2abindS1136;
            int32_t _M0L1kS1137;
            int32_t _M0L7_2abindS1143;
            int32_t _M0L1kS1144;
            int32_t _M0L6_2atmpS3714;
            Moonbit_object_header(_M0L9post__idxS1131)->meta
            = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 24, 0);
            _M0L9post__idxS1131->$0 = _M0L6_2atmpS3713;
            _M0L9post__idxS1131->$1 = 0;
            _M0L7_2abindS1132 = 0;
            _M0L1kS1133 = _M0L7_2abindS1132;
            while (1) {
              if (_M0L1kS1133 < _M0L4colsS1081) {
                int32_t _M0L6_2atmpS3703;
                #line 196 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array4pushGiE(_M0L9post__idxS1131, _M0L1kS1133);
                _M0L6_2atmpS3703 = _M0L1kS1133 + 1;
                _M0L1kS1133 = _M0L6_2atmpS3703;
                continue;
              }
              break;
            }
            _M0L7n__dropS1135 = _M0L4colsS1081 - _M0L7n__keepS1128;
            _M0L7_2abindS1136 = 0;
            _M0L1kS1137 = _M0L7_2abindS1136;
            while (1) {
              if (_M0L1kS1137 < _M0L7n__dropS1135) {
                float _M0L1uS1138;
                int32_t _M0L6_2atmpS3708;
                float _M0L6_2atmpS3707;
                float _M0L6_2atmpS3706;
                int32_t _M0L6_2atmpS3705;
                int32_t _M0L6r__idxS1139;
                int32_t _M0L10r__clampedS1140;
                int32_t _M0L3tmpS1141;
                int32_t _M0L6_2atmpS3704;
                int32_t _M0L6_2atmpS3709;
                #line 200 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L1uS1138 = _M0FP26RiantR8snn__mbt9next__f32(_M0L3rngS1090);
                _M0L6_2atmpS3708 = _M0L4colsS1081 - _M0L1kS1137;
                _M0L6_2atmpS3707 = (float)_M0L6_2atmpS3708;
                _M0L6_2atmpS3706 = _M0L6_2atmpS3707 * _M0L1uS1138;
                #line 201 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS3705
                = _M0MPC15float5Float7to__int(_M0L6_2atmpS3706);
                _M0L6r__idxS1139 = _M0L1kS1137 + _M0L6_2atmpS3705;
                if (_M0L6r__idxS1139 >= _M0L4colsS1081) {
                  _M0L10r__clampedS1140 = _M0L4colsS1081 - 1;
                } else {
                  _M0L10r__clampedS1140 = _M0L6r__idxS1139;
                }
                #line 203 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L3tmpS1141
                = _M0MPC15array5Array2atGiE(_M0L9post__idxS1131, _M0L1kS1137);
                #line 204 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS3704
                = _M0MPC15array5Array2atGiE(_M0L9post__idxS1131, _M0L10r__clampedS1140);
                #line 204 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array3setGiE(_M0L9post__idxS1131, _M0L1kS1137, _M0L6_2atmpS3704);
                #line 205 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array3setGiE(_M0L9post__idxS1131, _M0L10r__clampedS1140, _M0L3tmpS1141);
                _M0L6_2atmpS3709 = _M0L1kS1137 + 1;
                _M0L1kS1137 = _M0L6_2atmpS3709;
                continue;
              }
              break;
            }
            _M0L7_2abindS1143 = 0;
            _M0L1kS1144 = _M0L7_2abindS1143;
            while (1) {
              if (_M0L1kS1144 < _M0L7n__dropS1135) {
                struct _M0TPB5ArrayGfE* _M0L6_2atmpS3710;
                int32_t _M0L6_2atmpS3711;
                int32_t _M0L6_2atmpS3712;
                #line 208 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS3710
                = _M0MPC15array5Array2atGRPB5ArrayGfEE(_M0L5denseS1076, _M0L1iS1130);
                #line 208 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS3711
                = _M0MPC15array5Array2atGiE(_M0L9post__idxS1131, _M0L1kS1144);
                #line 208 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array3setGfE(_M0L6_2atmpS3710, _M0L6_2atmpS3711, 0x0p+0f);
                moonbit_decref(_M0L6_2atmpS3710);
                _M0L6_2atmpS3712 = _M0L1kS1144 + 1;
                _M0L1kS1144 = _M0L6_2atmpS3712;
                continue;
              } else {
                moonbit_decref(_M0L9post__idxS1131);
              }
              break;
            }
            _M0L6_2atmpS3714 = _M0L1iS1130 + 1;
            _M0L1iS1130 = _M0L6_2atmpS3714;
            continue;
          }
          break;
        }
      } else if (_M0L7n__keepS1128 == 0) {
        int32_t _M0L7_2abindS1147 = 0;
        int32_t _M0L1iS1148 = _M0L7_2abindS1147;
        while (1) {
          if (_M0L1iS1148 < _M0L4rowsS1077) {
            int32_t _M0L7_2abindS1149 = 0;
            int32_t _M0L1jS1150 = _M0L7_2abindS1149;
            int32_t _M0L6_2atmpS3717;
            while (1) {
              if (_M0L1jS1150 < _M0L4colsS1081) {
                struct _M0TPB5ArrayGfE* _M0L6_2atmpS3715;
                int32_t _M0L6_2atmpS3716;
                #line 214 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS3715
                = _M0MPC15array5Array2atGRPB5ArrayGfEE(_M0L5denseS1076, _M0L1iS1148);
                #line 214 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array3setGfE(_M0L6_2atmpS3715, _M0L1jS1150, 0x0p+0f);
                moonbit_decref(_M0L6_2atmpS3715);
                _M0L6_2atmpS3716 = _M0L1jS1150 + 1;
                _M0L1jS1150 = _M0L6_2atmpS3716;
                continue;
              }
              break;
            }
            _M0L6_2atmpS3717 = _M0L1iS1148 + 1;
            _M0L1iS1148 = _M0L6_2atmpS3717;
            continue;
          }
          break;
        }
      }
      break;
    }
  }
  _M0L6_2atmpS3727 = _M0L4rowsS1077 + 1;
  #line 221 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  _M0L6rowptrS1153 = _M0MPC15array5Array4makeGiE(_M0L6_2atmpS3727, 0);
  _M0L6_2atmpS3726 = (int32_t*)moonbit_empty_int32_array;
  _M0L6colptrS1154
  = (struct _M0TPB5ArrayGiE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGiE));
  Moonbit_object_header(_M0L6colptrS1154)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 24, 0);
  _M0L6colptrS1154->$0 = _M0L6_2atmpS3726;
  _M0L6colptrS1154->$1 = 0;
  _M0L6_2atmpS3725 = moonbit_empty_float_array;
  _M0L4valsS1155
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_M0L4valsS1155)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 21, 0);
  _M0L4valsS1155->$0 = _M0L6_2atmpS3725;
  _M0L4valsS1155->$1 = 0;
  _M0L7_2abindS1156 = 0;
  _M0L1iS1157 = _M0L7_2abindS1156;
  while (1) {
    if (_M0L1iS1157 < _M0L4rowsS1077) {
      int32_t _M0L6_2atmpS3720;
      int32_t _M0L7_2abindS1158;
      int32_t _M0L1jS1159;
      int32_t _M0L6_2atmpS3723;
      #line 225 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
      _M0L6_2atmpS3720 = _M0MPC15array5Array6lengthGfE(_M0L4valsS1155);
      #line 225 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
      _M0MPC15array5Array3setGiE(_M0L6rowptrS1153, _M0L1iS1157, _M0L6_2atmpS3720);
      _M0L7_2abindS1158 = 0;
      _M0L1jS1159 = _M0L7_2abindS1158;
      while (1) {
        if (_M0L1jS1159 < _M0L4colsS1081) {
          struct _M0TPB5ArrayGfE* _M0L6_2atmpS3721;
          float _M0L1vS1160;
          int32_t _M0L6_2atmpS3722;
          #line 227 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
          _M0L6_2atmpS3721
          = _M0MPC15array5Array2atGRPB5ArrayGfEE(_M0L5denseS1076, _M0L1iS1157);
          #line 227 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
          _M0L1vS1160
          = _M0MPC15array5Array2atGfE(_M0L6_2atmpS3721, _M0L1jS1159);
          moonbit_decref(_M0L6_2atmpS3721);
          if (_M0L1vS1160 != 0x0p+0f) {
            #line 229 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
            _M0MPC15array5Array4pushGiE(_M0L6colptrS1154, _M0L1jS1159);
            #line 230 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
            _M0MPC15array5Array4pushGfE(_M0L4valsS1155, _M0L1vS1160);
          }
          _M0L6_2atmpS3722 = _M0L1jS1159 + 1;
          _M0L1jS1159 = _M0L6_2atmpS3722;
          continue;
        }
        break;
      }
      _M0L6_2atmpS3723 = _M0L1iS1157 + 1;
      _M0L1iS1157 = _M0L6_2atmpS3723;
      continue;
    } else {
      moonbit_decref(_M0L5denseS1076);
    }
    break;
  }
  #line 234 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  _M0L6_2atmpS3724 = _M0MPC15array5Array6lengthGfE(_M0L4valsS1155);
  #line 234 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  _M0MPC15array5Array3setGiE(_M0L6rowptrS1153, _M0L4rowsS1077, _M0L6_2atmpS3724);
  _block_5413
  = (struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR));
  Moonbit_object_header(_block_5413)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 63, 0);
  _block_5413->$0 = _M0L4rowsS1077;
  _block_5413->$1 = _M0L4colsS1081;
  _block_5413->$2 = _M0L6rowptrS1153;
  _block_5413->$3 = _M0L6colptrS1154;
  _block_5413->$4 = _M0L4valsS1155;
  return _block_5413;
}

int32_t _M0MP26RiantR8snn__mbt15SparseMatrixCSR3nnz(
  struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L1mS1075
) {
  struct _M0TPB5ArrayGfE* _M0L4valsS3678;
  #line 34 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  _M0L4valsS3678 = _M0L1mS1075->$4;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  return _M0MPC15array5Array6lengthGfE(_M0L4valsS3678);
}

int32_t _M0FP26RiantR8snn__mbt21stdp__symmetric__step(
  struct _M0TPB5ArrayGfE* _M0L1wS1071,
  struct _M0TPB5ArrayGbE* _M0L9pre__fireS1044,
  struct _M0TPB5ArrayGbE* _M0L10post__fireS1046,
  struct _M0TPB5ArrayGiE* _M0L6colptrS1066,
  struct _M0TPB5ArrayGiE* _M0L6rowptrS1059,
  struct _M0TP26RiantR8snn__mbt22STDPSymmetricVariables* _M0L4varsS1051,
  struct _M0TP26RiantR8snn__mbt13STDPSymmetric* _M0L5paramS1048,
  float _M0L6t__nowS1042,
  float _M0L2dtS1052
) {
  int32_t _M0L6n__preS1043;
  int32_t _M0L7n__postS1045;
  float _M0L6tau__xS3677;
  float _M0L11inv__tau__xS1047;
  float _M0L6tau__yS3676;
  float _M0L11inv__tau__yS1049;
  struct _M0TPB8MutLocalGiE* _M0L1jS1050;
  struct _M0TPB8MutLocalGiE* _M0L1iS1054;
  float _M0L4a__xS3673;
  float _M0L6tau__xS3675;
  float _M0L6_2atmpS3674;
  float _M0L7coef__xS1056;
  float _M0L4a__yS3670;
  float _M0L6tau__yS3672;
  float _M0L6_2atmpS3671;
  float _M0L7coef__yS1057;
  #line 1295 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  #line 1307 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L6n__preS1043 = _M0MPC15array5Array6lengthGbE(_M0L9pre__fireS1044);
  #line 1308 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L7n__postS1045 = _M0MPC15array5Array6lengthGbE(_M0L10post__fireS1046);
  _M0L6tau__xS3677 = _M0L5paramS1048->$2;
  _M0L11inv__tau__xS1047 = 0x1p+0f / _M0L6tau__xS3677;
  _M0L6tau__yS3676 = _M0L5paramS1048->$3;
  _M0L11inv__tau__yS1049 = 0x1p+0f / _M0L6tau__yS3676;
  _M0L1jS1050
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1jS1050)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1jS1050->$0 = 0;
  while (1) {
    int32_t _M0L3valS3547 = _M0L1jS1050->$0;
    if (_M0L3valS3547 < _M0L6n__preS1043) {
      struct _M0TPB5ArrayGfE* _M0L5tr__xS3548 = _M0L4varsS1051->$0;
      int32_t _M0L3valS3549 = _M0L1jS1050->$0;
      struct _M0TPB5ArrayGfE* _M0L5tr__xS3558 = _M0L4varsS1051->$0;
      int32_t _M0L3valS3559 = _M0L1jS1050->$0;
      float _M0L6_2atmpS3551;
      struct _M0TPB5ArrayGfE* _M0L5tr__xS3556;
      int32_t _M0L3valS3557;
      float _M0L6_2atmpS3555;
      float _M0L6_2atmpS3554;
      float _M0L6_2atmpS3553;
      float _M0L6_2atmpS3552;
      float _M0L6_2atmpS3550;
      struct _M0TPB5ArrayGfE* _M0L5tr__yS3560;
      int32_t _M0L3valS3561;
      struct _M0TPB5ArrayGfE* _M0L5tr__yS3570;
      int32_t _M0L3valS3571;
      float _M0L6_2atmpS3563;
      struct _M0TPB5ArrayGfE* _M0L5tr__yS3568;
      int32_t _M0L3valS3569;
      float _M0L6_2atmpS3567;
      float _M0L6_2atmpS3566;
      float _M0L6_2atmpS3565;
      float _M0L6_2atmpS3564;
      float _M0L6_2atmpS3562;
      int32_t _M0L3valS3572;
      int32_t _M0L3valS3586;
      int32_t _M0L6_2atmpS3585;
      #line 1315 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS3551
      = _M0MPC15array5Array2atGfE(_M0L5tr__xS3558, _M0L3valS3559);
      _M0L5tr__xS3556 = _M0L4varsS1051->$0;
      _M0L3valS3557 = _M0L1jS1050->$0;
      #line 1315 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS3555
      = _M0MPC15array5Array2atGfE(_M0L5tr__xS3556, _M0L3valS3557);
      _M0L6_2atmpS3554 = -_M0L6_2atmpS3555;
      _M0L6_2atmpS3553 = _M0L2dtS1052 * _M0L6_2atmpS3554;
      _M0L6_2atmpS3552 = _M0L6_2atmpS3553 * _M0L11inv__tau__xS1047;
      _M0L6_2atmpS3550 = _M0L6_2atmpS3551 + _M0L6_2atmpS3552;
      #line 1315 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0MPC15array5Array3setGfE(_M0L5tr__xS3548, _M0L3valS3549, _M0L6_2atmpS3550);
      _M0L5tr__yS3560 = _M0L4varsS1051->$1;
      _M0L3valS3561 = _M0L1jS1050->$0;
      _M0L5tr__yS3570 = _M0L4varsS1051->$1;
      _M0L3valS3571 = _M0L1jS1050->$0;
      #line 1316 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS3563
      = _M0MPC15array5Array2atGfE(_M0L5tr__yS3570, _M0L3valS3571);
      _M0L5tr__yS3568 = _M0L4varsS1051->$1;
      _M0L3valS3569 = _M0L1jS1050->$0;
      #line 1316 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS3567
      = _M0MPC15array5Array2atGfE(_M0L5tr__yS3568, _M0L3valS3569);
      _M0L6_2atmpS3566 = -_M0L6_2atmpS3567;
      _M0L6_2atmpS3565 = _M0L2dtS1052 * _M0L6_2atmpS3566;
      _M0L6_2atmpS3564 = _M0L6_2atmpS3565 * _M0L11inv__tau__yS1049;
      _M0L6_2atmpS3562 = _M0L6_2atmpS3563 + _M0L6_2atmpS3564;
      #line 1316 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0MPC15array5Array3setGfE(_M0L5tr__yS3560, _M0L3valS3561, _M0L6_2atmpS3562);
      _M0L3valS3572 = _M0L1jS1050->$0;
      #line 1317 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L9pre__fireS1044, _M0L3valS3572)) {
        struct _M0TPB5ArrayGfE* _M0L5tr__xS3573 = _M0L4varsS1051->$0;
        int32_t _M0L3valS3574 = _M0L1jS1050->$0;
        struct _M0TPB5ArrayGfE* _M0L5tr__xS3577 = _M0L4varsS1051->$0;
        int32_t _M0L3valS3578 = _M0L1jS1050->$0;
        float _M0L6_2atmpS3576;
        float _M0L6_2atmpS3575;
        struct _M0TPB5ArrayGfE* _M0L5tr__yS3579;
        int32_t _M0L3valS3580;
        struct _M0TPB5ArrayGfE* _M0L5tr__yS3583;
        int32_t _M0L3valS3584;
        float _M0L6_2atmpS3582;
        float _M0L6_2atmpS3581;
        #line 1318 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L6_2atmpS3576
        = _M0MPC15array5Array2atGfE(_M0L5tr__xS3577, _M0L3valS3578);
        _M0L6_2atmpS3575 = _M0L6_2atmpS3576 + 0x1p+0f;
        #line 1318 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L5tr__xS3573, _M0L3valS3574, _M0L6_2atmpS3575);
        _M0L5tr__yS3579 = _M0L4varsS1051->$1;
        _M0L3valS3580 = _M0L1jS1050->$0;
        _M0L5tr__yS3583 = _M0L4varsS1051->$1;
        _M0L3valS3584 = _M0L1jS1050->$0;
        #line 1319 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L6_2atmpS3582
        = _M0MPC15array5Array2atGfE(_M0L5tr__yS3583, _M0L3valS3584);
        _M0L6_2atmpS3581 = _M0L6_2atmpS3582 + 0x1p+0f;
        #line 1319 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L5tr__yS3579, _M0L3valS3580, _M0L6_2atmpS3581);
      }
      _M0L3valS3586 = _M0L1jS1050->$0;
      _M0L6_2atmpS3585 = _M0L3valS3586 + 1;
      _M0L1jS1050->$0 = _M0L6_2atmpS3585;
      continue;
    }
    break;
  }
  _M0L1iS1054
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1iS1054)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1iS1054->$0 = 0;
  while (1) {
    int32_t _M0L3valS3587 = _M0L1iS1054->$0;
    if (_M0L3valS3587 < _M0L7n__postS1045) {
      struct _M0TPB5ArrayGfE* _M0L5to__xS3588 = _M0L4varsS1051->$2;
      int32_t _M0L3valS3589 = _M0L1iS1054->$0;
      struct _M0TPB5ArrayGfE* _M0L5to__xS3598 = _M0L4varsS1051->$2;
      int32_t _M0L3valS3599 = _M0L1iS1054->$0;
      float _M0L6_2atmpS3591;
      struct _M0TPB5ArrayGfE* _M0L5to__xS3596;
      int32_t _M0L3valS3597;
      float _M0L6_2atmpS3595;
      float _M0L6_2atmpS3594;
      float _M0L6_2atmpS3593;
      float _M0L6_2atmpS3592;
      float _M0L6_2atmpS3590;
      struct _M0TPB5ArrayGfE* _M0L5to__yS3600;
      int32_t _M0L3valS3601;
      struct _M0TPB5ArrayGfE* _M0L5to__yS3610;
      int32_t _M0L3valS3611;
      float _M0L6_2atmpS3603;
      struct _M0TPB5ArrayGfE* _M0L5to__yS3608;
      int32_t _M0L3valS3609;
      float _M0L6_2atmpS3607;
      float _M0L6_2atmpS3606;
      float _M0L6_2atmpS3605;
      float _M0L6_2atmpS3604;
      float _M0L6_2atmpS3602;
      int32_t _M0L3valS3612;
      int32_t _M0L3valS3626;
      int32_t _M0L6_2atmpS3625;
      #line 1325 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS3591
      = _M0MPC15array5Array2atGfE(_M0L5to__xS3598, _M0L3valS3599);
      _M0L5to__xS3596 = _M0L4varsS1051->$2;
      _M0L3valS3597 = _M0L1iS1054->$0;
      #line 1325 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS3595
      = _M0MPC15array5Array2atGfE(_M0L5to__xS3596, _M0L3valS3597);
      _M0L6_2atmpS3594 = -_M0L6_2atmpS3595;
      _M0L6_2atmpS3593 = _M0L2dtS1052 * _M0L6_2atmpS3594;
      _M0L6_2atmpS3592 = _M0L6_2atmpS3593 * _M0L11inv__tau__xS1047;
      _M0L6_2atmpS3590 = _M0L6_2atmpS3591 + _M0L6_2atmpS3592;
      #line 1325 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0MPC15array5Array3setGfE(_M0L5to__xS3588, _M0L3valS3589, _M0L6_2atmpS3590);
      _M0L5to__yS3600 = _M0L4varsS1051->$3;
      _M0L3valS3601 = _M0L1iS1054->$0;
      _M0L5to__yS3610 = _M0L4varsS1051->$3;
      _M0L3valS3611 = _M0L1iS1054->$0;
      #line 1326 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS3603
      = _M0MPC15array5Array2atGfE(_M0L5to__yS3610, _M0L3valS3611);
      _M0L5to__yS3608 = _M0L4varsS1051->$3;
      _M0L3valS3609 = _M0L1iS1054->$0;
      #line 1326 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS3607
      = _M0MPC15array5Array2atGfE(_M0L5to__yS3608, _M0L3valS3609);
      _M0L6_2atmpS3606 = -_M0L6_2atmpS3607;
      _M0L6_2atmpS3605 = _M0L2dtS1052 * _M0L6_2atmpS3606;
      _M0L6_2atmpS3604 = _M0L6_2atmpS3605 * _M0L11inv__tau__yS1049;
      _M0L6_2atmpS3602 = _M0L6_2atmpS3603 + _M0L6_2atmpS3604;
      #line 1326 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0MPC15array5Array3setGfE(_M0L5to__yS3600, _M0L3valS3601, _M0L6_2atmpS3602);
      _M0L3valS3612 = _M0L1iS1054->$0;
      #line 1327 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L10post__fireS1046, _M0L3valS3612)) {
        struct _M0TPB5ArrayGfE* _M0L5to__xS3613 = _M0L4varsS1051->$2;
        int32_t _M0L3valS3614 = _M0L1iS1054->$0;
        struct _M0TPB5ArrayGfE* _M0L5to__xS3617 = _M0L4varsS1051->$2;
        int32_t _M0L3valS3618 = _M0L1iS1054->$0;
        float _M0L6_2atmpS3616;
        float _M0L6_2atmpS3615;
        struct _M0TPB5ArrayGfE* _M0L5to__yS3619;
        int32_t _M0L3valS3620;
        struct _M0TPB5ArrayGfE* _M0L5to__yS3623;
        int32_t _M0L3valS3624;
        float _M0L6_2atmpS3622;
        float _M0L6_2atmpS3621;
        #line 1328 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L6_2atmpS3616
        = _M0MPC15array5Array2atGfE(_M0L5to__xS3617, _M0L3valS3618);
        _M0L6_2atmpS3615 = _M0L6_2atmpS3616 + 0x1p+0f;
        #line 1328 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L5to__xS3613, _M0L3valS3614, _M0L6_2atmpS3615);
        _M0L5to__yS3619 = _M0L4varsS1051->$3;
        _M0L3valS3620 = _M0L1iS1054->$0;
        _M0L5to__yS3623 = _M0L4varsS1051->$3;
        _M0L3valS3624 = _M0L1iS1054->$0;
        #line 1329 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L6_2atmpS3622
        = _M0MPC15array5Array2atGfE(_M0L5to__yS3623, _M0L3valS3624);
        _M0L6_2atmpS3621 = _M0L6_2atmpS3622 + 0x1p+0f;
        #line 1329 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L5to__yS3619, _M0L3valS3620, _M0L6_2atmpS3621);
      }
      _M0L3valS3626 = _M0L1iS1054->$0;
      _M0L6_2atmpS3625 = _M0L3valS3626 + 1;
      _M0L1iS1054->$0 = _M0L6_2atmpS3625;
      continue;
    } else {
      moonbit_decref(_M0L1iS1054);
    }
    break;
  }
  _M0L4a__xS3673 = _M0L5paramS1048->$0;
  _M0L6tau__xS3675 = _M0L5paramS1048->$2;
  _M0L6_2atmpS3674 = 0x1p+1f * _M0L6tau__xS3675;
  _M0L7coef__xS1056 = _M0L4a__xS3673 / _M0L6_2atmpS3674;
  _M0L4a__yS3670 = _M0L5paramS1048->$1;
  _M0L6tau__yS3672 = _M0L5paramS1048->$3;
  _M0L6_2atmpS3671 = 0x1p+1f * _M0L6tau__yS3672;
  _M0L7coef__yS1057 = _M0L4a__yS3670 / _M0L6_2atmpS3671;
  _M0L1jS1050->$0 = 0;
  while (1) {
    int32_t _M0L3valS3627 = _M0L1jS1050->$0;
    if (_M0L3valS3627 < _M0L6n__preS1043) {
      int32_t _M0L3valS3669 = _M0L1jS1050->$0;
      int32_t _M0L5startS1058;
      int32_t _M0L3valS3668;
      int32_t _M0L6_2atmpS3667;
      int32_t _M0L3endS1060;
      int32_t _M0L3valS3666;
      int32_t _M0L10pre__firedS1061;
      struct _M0TPB5ArrayGfE* _M0L5tr__xS3664;
      int32_t _M0L3valS3665;
      float _M0L8tr__x__jS1062;
      struct _M0TPB5ArrayGfE* _M0L5tr__yS3662;
      int32_t _M0L3valS3663;
      float _M0L8tr__y__jS1063;
      struct _M0TPB8MutLocalGiE* _M0L1sS1064;
      int32_t _M0L3valS3661;
      int32_t _M0L6_2atmpS3660;
      #line 1345 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L5startS1058
      = _M0MPC15array5Array2atGiE(_M0L6rowptrS1059, _M0L3valS3669);
      _M0L3valS3668 = _M0L1jS1050->$0;
      _M0L6_2atmpS3667 = _M0L3valS3668 + 1;
      #line 1346 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L3endS1060
      = _M0MPC15array5Array2atGiE(_M0L6rowptrS1059, _M0L6_2atmpS3667);
      _M0L3valS3666 = _M0L1jS1050->$0;
      #line 1347 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L10pre__firedS1061
      = _M0MPC15array5Array2atGbE(_M0L9pre__fireS1044, _M0L3valS3666);
      _M0L5tr__xS3664 = _M0L4varsS1051->$0;
      _M0L3valS3665 = _M0L1jS1050->$0;
      #line 1348 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L8tr__x__jS1062
      = _M0MPC15array5Array2atGfE(_M0L5tr__xS3664, _M0L3valS3665);
      _M0L5tr__yS3662 = _M0L4varsS1051->$1;
      _M0L3valS3663 = _M0L1jS1050->$0;
      #line 1349 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L8tr__y__jS1063
      = _M0MPC15array5Array2atGfE(_M0L5tr__yS3662, _M0L3valS3663);
      _M0L1sS1064
      = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
      Moonbit_object_header(_M0L1sS1064)->meta
      = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
      _M0L1sS1064->$0 = _M0L5startS1058;
      while (1) {
        int32_t _M0L3valS3628 = _M0L1sS1064->$0;
        if (_M0L3valS3628 < _M0L3endS1060) {
          int32_t _M0L3valS3659 = _M0L1sS1064->$0;
          int32_t _M0L9post__idxS1065;
          int32_t _M0L11post__firedS1067;
          struct _M0TPB5ArrayGfE* _M0L5to__xS3658;
          float _M0L8to__x__iS1068;
          struct _M0TPB5ArrayGfE* _M0L5to__yS3657;
          float _M0L8to__y__iS1069;
          int32_t _M0L3valS3647;
          float _M0L6_2atmpS3645;
          float _M0L6w__minS3646;
          int32_t _M0L3valS3652;
          float _M0L6_2atmpS3650;
          float _M0L6w__maxS3651;
          int32_t _M0L3valS3656;
          int32_t _M0L6_2atmpS3655;
          #line 1352 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
          _M0L9post__idxS1065
          = _M0MPC15array5Array2atGiE(_M0L6colptrS1066, _M0L3valS3659);
          #line 1353 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
          _M0L11post__firedS1067
          = _M0MPC15array5Array2atGbE(_M0L10post__fireS1046, _M0L9post__idxS1065);
          _M0L5to__xS3658 = _M0L4varsS1051->$2;
          #line 1354 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
          _M0L8to__x__iS1068
          = _M0MPC15array5Array2atGfE(_M0L5to__xS3658, _M0L9post__idxS1065);
          _M0L5to__yS3657 = _M0L4varsS1051->$3;
          #line 1355 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
          _M0L8to__y__iS1069
          = _M0MPC15array5Array2atGfE(_M0L5to__yS3657, _M0L9post__idxS1065);
          if (_M0L10pre__firedS1061) {
            float _M0L10alpha__preS3635 = _M0L5paramS1048->$4;
            float _M0L6_2atmpS3636 = _M0L7coef__xS1056 * _M0L8to__x__iS1068;
            float _M0L6_2atmpS3633 = _M0L10alpha__preS3635 + _M0L6_2atmpS3636;
            float _M0L6_2atmpS3634 = _M0L7coef__yS1057 * _M0L8to__y__iS1069;
            float _M0L2dwS1070 = _M0L6_2atmpS3633 - _M0L6_2atmpS3634;
            int32_t _M0L3valS3629 = _M0L1sS1064->$0;
            int32_t _M0L3valS3632 = _M0L1sS1064->$0;
            float _M0L6_2atmpS3631;
            float _M0L6_2atmpS3630;
            #line 1358 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0L6_2atmpS3631
            = _M0MPC15array5Array2atGfE(_M0L1wS1071, _M0L3valS3632);
            _M0L6_2atmpS3630 = _M0L6_2atmpS3631 + _M0L2dwS1070;
            #line 1358 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0MPC15array5Array3setGfE(_M0L1wS1071, _M0L3valS3629, _M0L6_2atmpS3630);
          }
          if (_M0L11post__firedS1067) {
            float _M0L11alpha__postS3643 = _M0L5paramS1048->$5;
            float _M0L6_2atmpS3644 = _M0L7coef__xS1056 * _M0L8tr__x__jS1062;
            float _M0L6_2atmpS3641 =
              _M0L11alpha__postS3643 + _M0L6_2atmpS3644;
            float _M0L6_2atmpS3642 = _M0L7coef__yS1057 * _M0L8tr__y__jS1063;
            float _M0L2dwS1072 = _M0L6_2atmpS3641 - _M0L6_2atmpS3642;
            int32_t _M0L3valS3637 = _M0L1sS1064->$0;
            int32_t _M0L3valS3640 = _M0L1sS1064->$0;
            float _M0L6_2atmpS3639;
            float _M0L6_2atmpS3638;
            #line 1362 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0L6_2atmpS3639
            = _M0MPC15array5Array2atGfE(_M0L1wS1071, _M0L3valS3640);
            _M0L6_2atmpS3638 = _M0L6_2atmpS3639 + _M0L2dwS1072;
            #line 1362 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0MPC15array5Array3setGfE(_M0L1wS1071, _M0L3valS3637, _M0L6_2atmpS3638);
          }
          _M0L3valS3647 = _M0L1sS1064->$0;
          #line 1364 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
          _M0L6_2atmpS3645
          = _M0MPC15array5Array2atGfE(_M0L1wS1071, _M0L3valS3647);
          _M0L6w__minS3646 = _M0L5paramS1048->$7;
          if (_M0L6_2atmpS3645 < _M0L6w__minS3646) {
            int32_t _M0L3valS3648 = _M0L1sS1064->$0;
            float _M0L6w__minS3649 = _M0L5paramS1048->$7;
            #line 1364 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0MPC15array5Array3setGfE(_M0L1wS1071, _M0L3valS3648, _M0L6w__minS3649);
          }
          _M0L3valS3652 = _M0L1sS1064->$0;
          #line 1365 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
          _M0L6_2atmpS3650
          = _M0MPC15array5Array2atGfE(_M0L1wS1071, _M0L3valS3652);
          _M0L6w__maxS3651 = _M0L5paramS1048->$6;
          if (_M0L6_2atmpS3650 > _M0L6w__maxS3651) {
            int32_t _M0L3valS3653 = _M0L1sS1064->$0;
            float _M0L6w__maxS3654 = _M0L5paramS1048->$6;
            #line 1365 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0MPC15array5Array3setGfE(_M0L1wS1071, _M0L3valS3653, _M0L6w__maxS3654);
          }
          _M0L3valS3656 = _M0L1sS1064->$0;
          _M0L6_2atmpS3655 = _M0L3valS3656 + 1;
          _M0L1sS1064->$0 = _M0L6_2atmpS3655;
          continue;
        } else {
          moonbit_decref(_M0L1sS1064);
        }
        break;
      }
      _M0L3valS3661 = _M0L1jS1050->$0;
      _M0L6_2atmpS3660 = _M0L3valS3661 + 1;
      _M0L1jS1050->$0 = _M0L6_2atmpS3660;
      continue;
    } else {
      moonbit_decref(_M0L1jS1050);
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt22stdp__confavreux__step(
  struct _M0TPB5ArrayGfE* _M0L1wS1039,
  struct _M0TPB5ArrayGbE* _M0L9pre__fireS1016,
  struct _M0TPB5ArrayGbE* _M0L10post__fireS1018,
  struct _M0TPB5ArrayGiE* _M0L6colptrS1036,
  struct _M0TPB5ArrayGiE* _M0L6rowptrS1030,
  struct _M0TP26RiantR8snn__mbt13STDPVariables* _M0L4varsS1024,
  struct _M0TP26RiantR8snn__mbt18STDPConfavreux2025* _M0L5paramS1021,
  float _M0L6t__nowS1025,
  float _M0L2dtS1020
) {
  int32_t _M0L6n__preS1015;
  int32_t _M0L7n__postS1017;
  float _M0L6_2atmpS3545;
  float _M0L8tau__preS3546;
  float _M0L6_2atmpS3544;
  float _M0L10decay__preS1019;
  float _M0L6_2atmpS3542;
  float _M0L9tau__postS3543;
  float _M0L6_2atmpS3541;
  float _M0L11decay__postS1022;
  struct _M0TPB8MutLocalGiE* _M0L1jS1023;
  struct _M0TPB8MutLocalGiE* _M0L1iS1027;
  #line 1079 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  #line 1090 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L6n__preS1015 = _M0MPC15array5Array6lengthGbE(_M0L9pre__fireS1016);
  #line 1091 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L7n__postS1017 = _M0MPC15array5Array6lengthGbE(_M0L10post__fireS1018);
  _M0L6_2atmpS3545 = -_M0L2dtS1020;
  _M0L8tau__preS3546 = _M0L5paramS1021->$5;
  _M0L6_2atmpS3544 = _M0L6_2atmpS3545 / _M0L8tau__preS3546;
  #line 1092 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L10decay__preS1019 = _M0FP26RiantR8snn__mbt4expf(_M0L6_2atmpS3544);
  _M0L6_2atmpS3542 = -_M0L2dtS1020;
  _M0L9tau__postS3543 = _M0L5paramS1021->$6;
  _M0L6_2atmpS3541 = _M0L6_2atmpS3542 / _M0L9tau__postS3543;
  #line 1093 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L11decay__postS1022 = _M0FP26RiantR8snn__mbt4expf(_M0L6_2atmpS3541);
  _M0L1jS1023
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1jS1023)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1jS1023->$0 = 0;
  while (1) {
    int32_t _M0L3valS3461 = _M0L1jS1023->$0;
    if (_M0L3valS3461 < _M0L6n__preS1015) {
      struct _M0TPB5ArrayGfE* _M0L4tpreS3462 = _M0L4varsS1024->$0;
      int32_t _M0L3valS3463 = _M0L1jS1023->$0;
      struct _M0TPB5ArrayGfE* _M0L4tpreS3466 = _M0L4varsS1024->$0;
      int32_t _M0L3valS3467 = _M0L1jS1023->$0;
      float _M0L6_2atmpS3465;
      float _M0L6_2atmpS3464;
      int32_t _M0L3valS3468;
      int32_t _M0L3valS3478;
      int32_t _M0L6_2atmpS3477;
      #line 1097 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS3465
      = _M0MPC15array5Array2atGfE(_M0L4tpreS3466, _M0L3valS3467);
      _M0L6_2atmpS3464 = _M0L6_2atmpS3465 * _M0L10decay__preS1019;
      #line 1097 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0MPC15array5Array3setGfE(_M0L4tpreS3462, _M0L3valS3463, _M0L6_2atmpS3464);
      _M0L3valS3468 = _M0L1jS1023->$0;
      #line 1098 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L9pre__fireS1016, _M0L3valS3468)) {
        struct _M0TPB5ArrayGfE* _M0L4tpreS3469 = _M0L4varsS1024->$0;
        int32_t _M0L3valS3470 = _M0L1jS1023->$0;
        struct _M0TPB5ArrayGfE* _M0L4tpreS3473 = _M0L4varsS1024->$0;
        int32_t _M0L3valS3474 = _M0L1jS1023->$0;
        float _M0L6_2atmpS3472;
        float _M0L6_2atmpS3471;
        struct _M0TPB5ArrayGfE* _M0L9last__preS3475;
        int32_t _M0L3valS3476;
        #line 1099 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L6_2atmpS3472
        = _M0MPC15array5Array2atGfE(_M0L4tpreS3473, _M0L3valS3474);
        _M0L6_2atmpS3471 = _M0L6_2atmpS3472 + 0x1p+0f;
        #line 1099 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L4tpreS3469, _M0L3valS3470, _M0L6_2atmpS3471);
        _M0L9last__preS3475 = _M0L4varsS1024->$2;
        _M0L3valS3476 = _M0L1jS1023->$0;
        #line 1100 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L9last__preS3475, _M0L3valS3476, _M0L6t__nowS1025);
      }
      _M0L3valS3478 = _M0L1jS1023->$0;
      _M0L6_2atmpS3477 = _M0L3valS3478 + 1;
      _M0L1jS1023->$0 = _M0L6_2atmpS3477;
      continue;
    }
    break;
  }
  _M0L1iS1027
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1iS1027)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1iS1027->$0 = 0;
  while (1) {
    int32_t _M0L3valS3479 = _M0L1iS1027->$0;
    if (_M0L3valS3479 < _M0L7n__postS1017) {
      struct _M0TPB5ArrayGfE* _M0L5tpostS3480 = _M0L4varsS1024->$1;
      int32_t _M0L3valS3481 = _M0L1iS1027->$0;
      struct _M0TPB5ArrayGfE* _M0L5tpostS3484 = _M0L4varsS1024->$1;
      int32_t _M0L3valS3485 = _M0L1iS1027->$0;
      float _M0L6_2atmpS3483;
      float _M0L6_2atmpS3482;
      int32_t _M0L3valS3486;
      int32_t _M0L3valS3496;
      int32_t _M0L6_2atmpS3495;
      #line 1106 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS3483
      = _M0MPC15array5Array2atGfE(_M0L5tpostS3484, _M0L3valS3485);
      _M0L6_2atmpS3482 = _M0L6_2atmpS3483 * _M0L11decay__postS1022;
      #line 1106 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0MPC15array5Array3setGfE(_M0L5tpostS3480, _M0L3valS3481, _M0L6_2atmpS3482);
      _M0L3valS3486 = _M0L1iS1027->$0;
      #line 1107 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L10post__fireS1018, _M0L3valS3486)) {
        struct _M0TPB5ArrayGfE* _M0L5tpostS3487 = _M0L4varsS1024->$1;
        int32_t _M0L3valS3488 = _M0L1iS1027->$0;
        struct _M0TPB5ArrayGfE* _M0L5tpostS3491 = _M0L4varsS1024->$1;
        int32_t _M0L3valS3492 = _M0L1iS1027->$0;
        float _M0L6_2atmpS3490;
        float _M0L6_2atmpS3489;
        struct _M0TPB5ArrayGfE* _M0L10last__postS3493;
        int32_t _M0L3valS3494;
        #line 1108 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L6_2atmpS3490
        = _M0MPC15array5Array2atGfE(_M0L5tpostS3491, _M0L3valS3492);
        _M0L6_2atmpS3489 = _M0L6_2atmpS3490 + 0x1p+0f;
        #line 1108 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L5tpostS3487, _M0L3valS3488, _M0L6_2atmpS3489);
        _M0L10last__postS3493 = _M0L4varsS1024->$3;
        _M0L3valS3494 = _M0L1iS1027->$0;
        #line 1109 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L10last__postS3493, _M0L3valS3494, _M0L6t__nowS1025);
      }
      _M0L3valS3496 = _M0L1iS1027->$0;
      _M0L6_2atmpS3495 = _M0L3valS3496 + 1;
      _M0L1iS1027->$0 = _M0L6_2atmpS3495;
      continue;
    } else {
      moonbit_decref(_M0L1iS1027);
    }
    break;
  }
  _M0L1jS1023->$0 = 0;
  while (1) {
    int32_t _M0L3valS3497 = _M0L1jS1023->$0;
    if (_M0L3valS3497 < _M0L6n__preS1015) {
      int32_t _M0L3valS3540 = _M0L1jS1023->$0;
      int32_t _M0L5startS1029;
      int32_t _M0L3valS3539;
      int32_t _M0L6_2atmpS3538;
      int32_t _M0L3endS1031;
      int32_t _M0L3valS3537;
      int32_t _M0L10pre__firedS1032;
      struct _M0TPB5ArrayGfE* _M0L4tpreS3535;
      int32_t _M0L3valS3536;
      float _M0L7tpre__jS1033;
      struct _M0TPB8MutLocalGiE* _M0L1sS1034;
      int32_t _M0L3valS3534;
      int32_t _M0L6_2atmpS3533;
      #line 1119 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L5startS1029
      = _M0MPC15array5Array2atGiE(_M0L6rowptrS1030, _M0L3valS3540);
      _M0L3valS3539 = _M0L1jS1023->$0;
      _M0L6_2atmpS3538 = _M0L3valS3539 + 1;
      #line 1120 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L3endS1031
      = _M0MPC15array5Array2atGiE(_M0L6rowptrS1030, _M0L6_2atmpS3538);
      _M0L3valS3537 = _M0L1jS1023->$0;
      #line 1121 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L10pre__firedS1032
      = _M0MPC15array5Array2atGbE(_M0L9pre__fireS1016, _M0L3valS3537);
      _M0L4tpreS3535 = _M0L4varsS1024->$0;
      _M0L3valS3536 = _M0L1jS1023->$0;
      #line 1122 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L7tpre__jS1033
      = _M0MPC15array5Array2atGfE(_M0L4tpreS3535, _M0L3valS3536);
      _M0L1sS1034
      = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
      Moonbit_object_header(_M0L1sS1034)->meta
      = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
      _M0L1sS1034->$0 = _M0L5startS1029;
      while (1) {
        int32_t _M0L3valS3498 = _M0L1sS1034->$0;
        if (_M0L3valS3498 < _M0L3endS1031) {
          int32_t _M0L3valS3532 = _M0L1sS1034->$0;
          int32_t _M0L9post__idxS1035;
          int32_t _M0L11post__firedS1037;
          struct _M0TPB5ArrayGfE* _M0L5tpostS3531;
          float _M0L8tpost__iS1038;
          int32_t _M0L3valS3521;
          float _M0L6_2atmpS3519;
          float _M0L6w__minS3520;
          int32_t _M0L3valS3526;
          float _M0L6_2atmpS3524;
          float _M0L6w__maxS3525;
          int32_t _M0L3valS3530;
          int32_t _M0L6_2atmpS3529;
          #line 1125 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
          _M0L9post__idxS1035
          = _M0MPC15array5Array2atGiE(_M0L6colptrS1036, _M0L3valS3532);
          #line 1126 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
          _M0L11post__firedS1037
          = _M0MPC15array5Array2atGbE(_M0L10post__fireS1018, _M0L9post__idxS1035);
          _M0L5tpostS3531 = _M0L4varsS1024->$1;
          #line 1127 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
          _M0L8tpost__iS1038
          = _M0MPC15array5Array2atGfE(_M0L5tpostS3531, _M0L9post__idxS1035);
          if (_M0L10pre__firedS1032) {
            int32_t _M0L3valS3499 = _M0L1sS1034->$0;
            int32_t _M0L3valS3508 = _M0L1sS1034->$0;
            float _M0L6_2atmpS3501;
            float _M0L3etaS3503;
            float _M0L5kappaS3507;
            float _M0L6_2atmpS3505;
            float _M0L5alphaS3506;
            float _M0L6_2atmpS3504;
            float _M0L6_2atmpS3502;
            float _M0L6_2atmpS3500;
            #line 1130 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0L6_2atmpS3501
            = _M0MPC15array5Array2atGfE(_M0L1wS1039, _M0L3valS3508);
            _M0L3etaS3503 = _M0L5paramS1021->$0;
            _M0L5kappaS3507 = _M0L5paramS1021->$3;
            _M0L6_2atmpS3505 = _M0L5kappaS3507 * _M0L8tpost__iS1038;
            _M0L5alphaS3506 = _M0L5paramS1021->$1;
            _M0L6_2atmpS3504 = _M0L6_2atmpS3505 + _M0L5alphaS3506;
            _M0L6_2atmpS3502 = _M0L3etaS3503 * _M0L6_2atmpS3504;
            _M0L6_2atmpS3500 = _M0L6_2atmpS3501 + _M0L6_2atmpS3502;
            #line 1130 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0MPC15array5Array3setGfE(_M0L1wS1039, _M0L3valS3499, _M0L6_2atmpS3500);
          }
          if (_M0L11post__firedS1037) {
            int32_t _M0L3valS3509 = _M0L1sS1034->$0;
            int32_t _M0L3valS3518 = _M0L1sS1034->$0;
            float _M0L6_2atmpS3511;
            float _M0L3etaS3513;
            float _M0L5gammaS3517;
            float _M0L6_2atmpS3515;
            float _M0L4betaS3516;
            float _M0L6_2atmpS3514;
            float _M0L6_2atmpS3512;
            float _M0L6_2atmpS3510;
            #line 1134 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0L6_2atmpS3511
            = _M0MPC15array5Array2atGfE(_M0L1wS1039, _M0L3valS3518);
            _M0L3etaS3513 = _M0L5paramS1021->$0;
            _M0L5gammaS3517 = _M0L5paramS1021->$4;
            _M0L6_2atmpS3515 = _M0L5gammaS3517 * _M0L7tpre__jS1033;
            _M0L4betaS3516 = _M0L5paramS1021->$2;
            _M0L6_2atmpS3514 = _M0L6_2atmpS3515 + _M0L4betaS3516;
            _M0L6_2atmpS3512 = _M0L3etaS3513 * _M0L6_2atmpS3514;
            _M0L6_2atmpS3510 = _M0L6_2atmpS3511 + _M0L6_2atmpS3512;
            #line 1134 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0MPC15array5Array3setGfE(_M0L1wS1039, _M0L3valS3509, _M0L6_2atmpS3510);
          }
          _M0L3valS3521 = _M0L1sS1034->$0;
          #line 1137 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
          _M0L6_2atmpS3519
          = _M0MPC15array5Array2atGfE(_M0L1wS1039, _M0L3valS3521);
          _M0L6w__minS3520 = _M0L5paramS1021->$8;
          if (_M0L6_2atmpS3519 < _M0L6w__minS3520) {
            int32_t _M0L3valS3522 = _M0L1sS1034->$0;
            float _M0L6w__minS3523 = _M0L5paramS1021->$8;
            #line 1137 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0MPC15array5Array3setGfE(_M0L1wS1039, _M0L3valS3522, _M0L6w__minS3523);
          }
          _M0L3valS3526 = _M0L1sS1034->$0;
          #line 1138 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
          _M0L6_2atmpS3524
          = _M0MPC15array5Array2atGfE(_M0L1wS1039, _M0L3valS3526);
          _M0L6w__maxS3525 = _M0L5paramS1021->$7;
          if (_M0L6_2atmpS3524 > _M0L6w__maxS3525) {
            int32_t _M0L3valS3527 = _M0L1sS1034->$0;
            float _M0L6w__maxS3528 = _M0L5paramS1021->$7;
            #line 1138 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0MPC15array5Array3setGfE(_M0L1wS1039, _M0L3valS3527, _M0L6w__maxS3528);
          }
          _M0L3valS3530 = _M0L1sS1034->$0;
          _M0L6_2atmpS3529 = _M0L3valS3530 + 1;
          _M0L1sS1034->$0 = _M0L6_2atmpS3529;
          continue;
        } else {
          moonbit_decref(_M0L1sS1034);
        }
        break;
      }
      _M0L3valS3534 = _M0L1jS1023->$0;
      _M0L6_2atmpS3533 = _M0L3valS3534 + 1;
      _M0L1jS1023->$0 = _M0L6_2atmpS3533;
      continue;
    } else {
      moonbit_decref(_M0L1jS1023);
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt10stdp__step(
  struct _M0TPB5ArrayGfE* _M0L1wS1012,
  struct _M0TPB5ArrayGbE* _M0L9pre__fireS992,
  struct _M0TPB5ArrayGbE* _M0L10post__fireS994,
  struct _M0TPB5ArrayGiE* _M0L6colptrS1009,
  struct _M0TPB5ArrayGiE* _M0L6rowptrS1005,
  struct _M0TP26RiantR8snn__mbt13STDPVariables* _M0L4varsS990,
  struct _M0TP26RiantR8snn__mbt12STDPGerstner* _M0L5paramS997,
  float _M0L6t__nowS1000,
  float _M0L2dtS996
) {
  struct _M0TPB5ArrayGbE* _M0L6activeS3378;
  int32_t _M0L6_2atmpS3377;
  int32_t _if__result_5422;
  int32_t _M0L6n__preS991;
  int32_t _M0L7n__postS993;
  float _M0L6_2atmpS3459;
  float _M0L8tau__preS3460;
  float _M0L6_2atmpS3458;
  float _M0L10decay__preS995;
  float _M0L6_2atmpS3456;
  float _M0L9tau__postS3457;
  float _M0L6_2atmpS3455;
  float _M0L11decay__postS998;
  struct _M0TPB8MutLocalGiE* _M0L1jS999;
  struct _M0TPB8MutLocalGiE* _M0L1iS1002;
  #line 904 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L6activeS3378 = _M0L4varsS990->$4;
  #line 916 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L6_2atmpS3377 = _M0MPC15array5Array6lengthGbE(_M0L6activeS3378);
  if (_M0L6_2atmpS3377 > 0) {
    struct _M0TPB5ArrayGbE* _M0L6activeS3376 = _M0L4varsS990->$4;
    int32_t _M0L6_2atmpS3375;
    #line 916 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
    _M0L6_2atmpS3375 = _M0MPC15array5Array2atGbE(_M0L6activeS3376, 0);
    _if__result_5422 = !_M0L6_2atmpS3375;
  } else {
    _if__result_5422 = 0;
  }
  if (_if__result_5422) {
    return 0;
  }
  #line 920 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L6n__preS991 = _M0MPC15array5Array6lengthGbE(_M0L9pre__fireS992);
  #line 921 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L7n__postS993 = _M0MPC15array5Array6lengthGbE(_M0L10post__fireS994);
  _M0L6_2atmpS3459 = -_M0L2dtS996;
  _M0L8tau__preS3460 = _M0L5paramS997->$2;
  _M0L6_2atmpS3458 = _M0L6_2atmpS3459 / _M0L8tau__preS3460;
  #line 922 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L10decay__preS995 = _M0FP26RiantR8snn__mbt4expf(_M0L6_2atmpS3458);
  _M0L6_2atmpS3456 = -_M0L2dtS996;
  _M0L9tau__postS3457 = _M0L5paramS997->$3;
  _M0L6_2atmpS3455 = _M0L6_2atmpS3456 / _M0L9tau__postS3457;
  #line 923 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L11decay__postS998 = _M0FP26RiantR8snn__mbt4expf(_M0L6_2atmpS3455);
  _M0L1jS999
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1jS999)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1jS999->$0 = 0;
  while (1) {
    int32_t _M0L3valS3379 = _M0L1jS999->$0;
    if (_M0L3valS3379 < _M0L6n__preS991) {
      struct _M0TPB5ArrayGfE* _M0L4tpreS3380 = _M0L4varsS990->$0;
      int32_t _M0L3valS3381 = _M0L1jS999->$0;
      struct _M0TPB5ArrayGfE* _M0L4tpreS3384 = _M0L4varsS990->$0;
      int32_t _M0L3valS3385 = _M0L1jS999->$0;
      float _M0L6_2atmpS3383;
      float _M0L6_2atmpS3382;
      int32_t _M0L3valS3386;
      int32_t _M0L3valS3397;
      int32_t _M0L6_2atmpS3396;
      #line 926 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS3383
      = _M0MPC15array5Array2atGfE(_M0L4tpreS3384, _M0L3valS3385);
      _M0L6_2atmpS3382 = _M0L6_2atmpS3383 * _M0L10decay__preS995;
      #line 926 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0MPC15array5Array3setGfE(_M0L4tpreS3380, _M0L3valS3381, _M0L6_2atmpS3382);
      _M0L3valS3386 = _M0L1jS999->$0;
      #line 927 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L9pre__fireS992, _M0L3valS3386)) {
        struct _M0TPB5ArrayGfE* _M0L4tpreS3387 = _M0L4varsS990->$0;
        int32_t _M0L3valS3388 = _M0L1jS999->$0;
        struct _M0TPB5ArrayGfE* _M0L4tpreS3392 = _M0L4varsS990->$0;
        int32_t _M0L3valS3393 = _M0L1jS999->$0;
        float _M0L6_2atmpS3390;
        float _M0L6a__preS3391;
        float _M0L6_2atmpS3389;
        struct _M0TPB5ArrayGfE* _M0L9last__preS3394;
        int32_t _M0L3valS3395;
        #line 928 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L6_2atmpS3390
        = _M0MPC15array5Array2atGfE(_M0L4tpreS3392, _M0L3valS3393);
        _M0L6a__preS3391 = _M0L5paramS997->$0;
        _M0L6_2atmpS3389 = _M0L6_2atmpS3390 + _M0L6a__preS3391;
        #line 928 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L4tpreS3387, _M0L3valS3388, _M0L6_2atmpS3389);
        _M0L9last__preS3394 = _M0L4varsS990->$2;
        _M0L3valS3395 = _M0L1jS999->$0;
        #line 929 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L9last__preS3394, _M0L3valS3395, _M0L6t__nowS1000);
      }
      _M0L3valS3397 = _M0L1jS999->$0;
      _M0L6_2atmpS3396 = _M0L3valS3397 + 1;
      _M0L1jS999->$0 = _M0L6_2atmpS3396;
      continue;
    }
    break;
  }
  _M0L1iS1002
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1iS1002)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1iS1002->$0 = 0;
  while (1) {
    int32_t _M0L3valS3398 = _M0L1iS1002->$0;
    if (_M0L3valS3398 < _M0L7n__postS993) {
      struct _M0TPB5ArrayGfE* _M0L5tpostS3399 = _M0L4varsS990->$1;
      int32_t _M0L3valS3400 = _M0L1iS1002->$0;
      struct _M0TPB5ArrayGfE* _M0L5tpostS3403 = _M0L4varsS990->$1;
      int32_t _M0L3valS3404 = _M0L1iS1002->$0;
      float _M0L6_2atmpS3402;
      float _M0L6_2atmpS3401;
      int32_t _M0L3valS3405;
      int32_t _M0L3valS3416;
      int32_t _M0L6_2atmpS3415;
      #line 935 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS3402
      = _M0MPC15array5Array2atGfE(_M0L5tpostS3403, _M0L3valS3404);
      _M0L6_2atmpS3401 = _M0L6_2atmpS3402 * _M0L11decay__postS998;
      #line 935 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0MPC15array5Array3setGfE(_M0L5tpostS3399, _M0L3valS3400, _M0L6_2atmpS3401);
      _M0L3valS3405 = _M0L1iS1002->$0;
      #line 936 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L10post__fireS994, _M0L3valS3405)) {
        struct _M0TPB5ArrayGfE* _M0L5tpostS3406 = _M0L4varsS990->$1;
        int32_t _M0L3valS3407 = _M0L1iS1002->$0;
        struct _M0TPB5ArrayGfE* _M0L5tpostS3411 = _M0L4varsS990->$1;
        int32_t _M0L3valS3412 = _M0L1iS1002->$0;
        float _M0L6_2atmpS3409;
        float _M0L7a__postS3410;
        float _M0L6_2atmpS3408;
        struct _M0TPB5ArrayGfE* _M0L10last__postS3413;
        int32_t _M0L3valS3414;
        #line 937 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L6_2atmpS3409
        = _M0MPC15array5Array2atGfE(_M0L5tpostS3411, _M0L3valS3412);
        _M0L7a__postS3410 = _M0L5paramS997->$1;
        _M0L6_2atmpS3408 = _M0L6_2atmpS3409 + _M0L7a__postS3410;
        #line 937 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L5tpostS3406, _M0L3valS3407, _M0L6_2atmpS3408);
        _M0L10last__postS3413 = _M0L4varsS990->$3;
        _M0L3valS3414 = _M0L1iS1002->$0;
        #line 938 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L10last__postS3413, _M0L3valS3414, _M0L6t__nowS1000);
      }
      _M0L3valS3416 = _M0L1iS1002->$0;
      _M0L6_2atmpS3415 = _M0L3valS3416 + 1;
      _M0L1iS1002->$0 = _M0L6_2atmpS3415;
      continue;
    } else {
      moonbit_decref(_M0L1iS1002);
    }
    break;
  }
  _M0L1jS999->$0 = 0;
  while (1) {
    int32_t _M0L3valS3417 = _M0L1jS999->$0;
    if (_M0L3valS3417 < _M0L6n__preS991) {
      int32_t _M0L3valS3454 = _M0L1jS999->$0;
      int32_t _M0L5startS1004;
      int32_t _M0L3valS3453;
      int32_t _M0L6_2atmpS3452;
      int32_t _M0L3endS1006;
      struct _M0TPB8MutLocalGiE* _M0L1sS1007;
      int32_t _M0L3valS3451;
      int32_t _M0L6_2atmpS3450;
      #line 946 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L5startS1004
      = _M0MPC15array5Array2atGiE(_M0L6rowptrS1005, _M0L3valS3454);
      _M0L3valS3453 = _M0L1jS999->$0;
      _M0L6_2atmpS3452 = _M0L3valS3453 + 1;
      #line 947 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L3endS1006
      = _M0MPC15array5Array2atGiE(_M0L6rowptrS1005, _M0L6_2atmpS3452);
      _M0L1sS1007
      = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
      Moonbit_object_header(_M0L1sS1007)->meta
      = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
      _M0L1sS1007->$0 = _M0L5startS1004;
      while (1) {
        int32_t _M0L3valS3418 = _M0L1sS1007->$0;
        if (_M0L3valS3418 < _M0L3endS1006) {
          int32_t _M0L3valS3449 = _M0L1sS1007->$0;
          int32_t _M0L9post__idxS1008;
          int32_t _M0L3valS3448;
          int32_t _M0L10pre__firedS1010;
          int32_t _M0L11post__firedS1011;
          int32_t _M0L3valS3438;
          float _M0L6_2atmpS3436;
          float _M0L6w__minS3437;
          int32_t _M0L3valS3443;
          float _M0L6_2atmpS3441;
          float _M0L6w__maxS3442;
          int32_t _M0L3valS3447;
          int32_t _M0L6_2atmpS3446;
          #line 950 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
          _M0L9post__idxS1008
          = _M0MPC15array5Array2atGiE(_M0L6colptrS1009, _M0L3valS3449);
          _M0L3valS3448 = _M0L1jS999->$0;
          #line 951 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
          _M0L10pre__firedS1010
          = _M0MPC15array5Array2atGbE(_M0L9pre__fireS992, _M0L3valS3448);
          #line 952 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
          _M0L11post__firedS1011
          = _M0MPC15array5Array2atGbE(_M0L10post__fireS994, _M0L9post__idxS1008);
          if (_M0L10pre__firedS1010) {
            int32_t _M0L3valS3419 = _M0L1sS1007->$0;
            int32_t _M0L3valS3426 = _M0L1sS1007->$0;
            float _M0L6_2atmpS3421;
            float _M0L7a__postS3423;
            struct _M0TPB5ArrayGfE* _M0L5tpostS3425;
            float _M0L6_2atmpS3424;
            float _M0L6_2atmpS3422;
            float _M0L6_2atmpS3420;
            #line 955 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0L6_2atmpS3421
            = _M0MPC15array5Array2atGfE(_M0L1wS1012, _M0L3valS3426);
            _M0L7a__postS3423 = _M0L5paramS997->$1;
            _M0L5tpostS3425 = _M0L4varsS990->$1;
            #line 955 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0L6_2atmpS3424
            = _M0MPC15array5Array2atGfE(_M0L5tpostS3425, _M0L9post__idxS1008);
            _M0L6_2atmpS3422 = _M0L7a__postS3423 * _M0L6_2atmpS3424;
            _M0L6_2atmpS3420 = _M0L6_2atmpS3421 + _M0L6_2atmpS3422;
            #line 955 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0MPC15array5Array3setGfE(_M0L1wS1012, _M0L3valS3419, _M0L6_2atmpS3420);
          }
          if (_M0L11post__firedS1011) {
            int32_t _M0L3valS3427 = _M0L1sS1007->$0;
            int32_t _M0L3valS3435 = _M0L1sS1007->$0;
            float _M0L6_2atmpS3429;
            float _M0L6a__preS3431;
            struct _M0TPB5ArrayGfE* _M0L4tpreS3433;
            int32_t _M0L3valS3434;
            float _M0L6_2atmpS3432;
            float _M0L6_2atmpS3430;
            float _M0L6_2atmpS3428;
            #line 959 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0L6_2atmpS3429
            = _M0MPC15array5Array2atGfE(_M0L1wS1012, _M0L3valS3435);
            _M0L6a__preS3431 = _M0L5paramS997->$0;
            _M0L4tpreS3433 = _M0L4varsS990->$0;
            _M0L3valS3434 = _M0L1jS999->$0;
            #line 959 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0L6_2atmpS3432
            = _M0MPC15array5Array2atGfE(_M0L4tpreS3433, _M0L3valS3434);
            _M0L6_2atmpS3430 = _M0L6a__preS3431 * _M0L6_2atmpS3432;
            _M0L6_2atmpS3428 = _M0L6_2atmpS3429 + _M0L6_2atmpS3430;
            #line 959 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0MPC15array5Array3setGfE(_M0L1wS1012, _M0L3valS3427, _M0L6_2atmpS3428);
          }
          _M0L3valS3438 = _M0L1sS1007->$0;
          #line 962 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
          _M0L6_2atmpS3436
          = _M0MPC15array5Array2atGfE(_M0L1wS1012, _M0L3valS3438);
          _M0L6w__minS3437 = _M0L5paramS997->$5;
          if (_M0L6_2atmpS3436 < _M0L6w__minS3437) {
            int32_t _M0L3valS3439 = _M0L1sS1007->$0;
            float _M0L6w__minS3440 = _M0L5paramS997->$5;
            #line 962 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0MPC15array5Array3setGfE(_M0L1wS1012, _M0L3valS3439, _M0L6w__minS3440);
          }
          _M0L3valS3443 = _M0L1sS1007->$0;
          #line 963 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
          _M0L6_2atmpS3441
          = _M0MPC15array5Array2atGfE(_M0L1wS1012, _M0L3valS3443);
          _M0L6w__maxS3442 = _M0L5paramS997->$4;
          if (_M0L6_2atmpS3441 > _M0L6w__maxS3442) {
            int32_t _M0L3valS3444 = _M0L1sS1007->$0;
            float _M0L6w__maxS3445 = _M0L5paramS997->$4;
            #line 963 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0MPC15array5Array3setGfE(_M0L1wS1012, _M0L3valS3444, _M0L6w__maxS3445);
          }
          _M0L3valS3447 = _M0L1sS1007->$0;
          _M0L6_2atmpS3446 = _M0L3valS3447 + 1;
          _M0L1sS1007->$0 = _M0L6_2atmpS3446;
          continue;
        } else {
          moonbit_decref(_M0L1sS1007);
        }
        break;
      }
      _M0L3valS3451 = _M0L1jS999->$0;
      _M0L6_2atmpS3450 = _M0L3valS3451 + 1;
      _M0L1jS999->$0 = _M0L6_2atmpS3450;
      continue;
    } else {
      moonbit_decref(_M0L1jS999);
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt25stdp__antisymmetric__step(
  struct _M0TPB5ArrayGfE* _M0L1wS969,
  struct _M0TPB5ArrayGbE* _M0L9pre__fireS959,
  struct _M0TPB5ArrayGbE* _M0L10post__fireS961,
  struct _M0TPB5ArrayGiE* _M0L6colptrS968,
  struct _M0TPB5ArrayGiE* _M0L6rowptrS964,
  struct _M0TP26RiantR8snn__mbt26STDPAntiSymmetricVariables* _M0L4varsS971,
  struct _M0TP26RiantR8snn__mbt17STDPAntiSymmetric* _M0L5paramS970,
  float _M0L2dtS983
) {
  int32_t _M0L6n__preS958;
  int32_t _M0L7n__postS960;
  struct _M0TPB8MutLocalGiE* _M0L1jS962;
  int32_t _M0L3nnzS974;
  float _M0L4a__xS3373;
  float _M0L6tau__xS3374;
  float _M0L18a__x__over__tau__xS975;
  struct _M0TPB8MutLocalGiE* _M0L2s2S976;
  float _M0L6tau__xS3372;
  float _M0L11inv__tau__xS980;
  float _M0L6tau__yS3371;
  float _M0L11inv__tau__yS981;
  struct _M0TPB8MutLocalGiE* _M0L1iS982;
  struct _M0TPB8MutLocalGiE* _M0L2s3S988;
  #line 621 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  #line 631 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L6n__preS958 = _M0MPC15array5Array6lengthGbE(_M0L9pre__fireS959);
  #line 632 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L7n__postS960 = _M0MPC15array5Array6lengthGbE(_M0L10post__fireS961);
  _M0L1jS962
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1jS962)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1jS962->$0 = 0;
  while (1) {
    int32_t _M0L3valS3271 = _M0L1jS962->$0;
    if (_M0L3valS3271 < _M0L6n__preS958) {
      int32_t _M0L3valS3272 = _M0L1jS962->$0;
      int32_t _M0L3valS3293;
      int32_t _M0L6_2atmpS3292;
      #line 636 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L9pre__fireS959, _M0L3valS3272)) {
        int32_t _M0L3valS3291 = _M0L1jS962->$0;
        int32_t _M0L5startS963;
        int32_t _M0L3valS3290;
        int32_t _M0L6_2atmpS3289;
        int32_t _M0L3endS965;
        struct _M0TPB8MutLocalGiE* _M0L1sS966;
        #line 637 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L5startS963
        = _M0MPC15array5Array2atGiE(_M0L6rowptrS964, _M0L3valS3291);
        _M0L3valS3290 = _M0L1jS962->$0;
        _M0L6_2atmpS3289 = _M0L3valS3290 + 1;
        #line 638 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L3endS965
        = _M0MPC15array5Array2atGiE(_M0L6rowptrS964, _M0L6_2atmpS3289);
        _M0L1sS966
        = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
        Moonbit_object_header(_M0L1sS966)->meta
        = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
        _M0L1sS966->$0 = _M0L5startS963;
        while (1) {
          int32_t _M0L3valS3273 = _M0L1sS966->$0;
          if (_M0L3valS3273 < _M0L3endS965) {
            int32_t _M0L3valS3288 = _M0L1sS966->$0;
            int32_t _M0L9post__idxS967;
            int32_t _M0L3valS3274;
            int32_t _M0L3valS3285;
            float _M0L6_2atmpS3283;
            float _M0L10alpha__preS3284;
            float _M0L6_2atmpS3276;
            float _M0L4a__yS3281;
            float _M0L6tau__yS3282;
            float _M0L6_2atmpS3278;
            struct _M0TPB5ArrayGfE* _M0L5to__yS3280;
            float _M0L6_2atmpS3279;
            float _M0L6_2atmpS3277;
            float _M0L6_2atmpS3275;
            int32_t _M0L3valS3287;
            int32_t _M0L6_2atmpS3286;
            #line 641 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0L9post__idxS967
            = _M0MPC15array5Array2atGiE(_M0L6colptrS968, _M0L3valS3288);
            _M0L3valS3274 = _M0L1sS966->$0;
            _M0L3valS3285 = _M0L1sS966->$0;
            #line 642 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0L6_2atmpS3283
            = _M0MPC15array5Array2atGfE(_M0L1wS969, _M0L3valS3285);
            _M0L10alpha__preS3284 = _M0L5paramS970->$4;
            _M0L6_2atmpS3276 = _M0L6_2atmpS3283 + _M0L10alpha__preS3284;
            _M0L4a__yS3281 = _M0L5paramS970->$1;
            _M0L6tau__yS3282 = _M0L5paramS970->$3;
            _M0L6_2atmpS3278 = _M0L4a__yS3281 / _M0L6tau__yS3282;
            _M0L5to__yS3280 = _M0L4varsS971->$1;
            #line 642 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0L6_2atmpS3279
            = _M0MPC15array5Array2atGfE(_M0L5to__yS3280, _M0L9post__idxS967);
            _M0L6_2atmpS3277 = _M0L6_2atmpS3278 * _M0L6_2atmpS3279;
            _M0L6_2atmpS3275 = _M0L6_2atmpS3276 - _M0L6_2atmpS3277;
            #line 642 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0MPC15array5Array3setGfE(_M0L1wS969, _M0L3valS3274, _M0L6_2atmpS3275);
            _M0L3valS3287 = _M0L1sS966->$0;
            _M0L6_2atmpS3286 = _M0L3valS3287 + 1;
            _M0L1sS966->$0 = _M0L6_2atmpS3286;
            continue;
          } else {
            moonbit_decref(_M0L1sS966);
          }
          break;
        }
      }
      _M0L3valS3293 = _M0L1jS962->$0;
      _M0L6_2atmpS3292 = _M0L3valS3293 + 1;
      _M0L1jS962->$0 = _M0L6_2atmpS3292;
      continue;
    }
    break;
  }
  #line 649 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L3nnzS974 = _M0MPC15array5Array6lengthGfE(_M0L1wS969);
  _M0L4a__xS3373 = _M0L5paramS970->$0;
  _M0L6tau__xS3374 = _M0L5paramS970->$2;
  _M0L18a__x__over__tau__xS975 = _M0L4a__xS3373 / _M0L6tau__xS3374;
  _M0L2s2S976
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L2s2S976)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L2s2S976->$0 = 0;
  while (1) {
    int32_t _M0L3valS3294 = _M0L2s2S976->$0;
    if (_M0L3valS3294 < _M0L3nnzS974) {
      int32_t _M0L3valS3307 = _M0L2s2S976->$0;
      int32_t _M0L9post__idxS977;
      int32_t _M0L3valS3306;
      int32_t _M0L6_2atmpS3305;
      #line 653 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L9post__idxS977
      = _M0MPC15array5Array2atGiE(_M0L6colptrS968, _M0L3valS3307);
      #line 654 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      if (
        _M0MPC15array5Array2atGbE(_M0L10post__fireS961, _M0L9post__idxS977)
      ) {
        int32_t _M0L3valS3304 = _M0L2s2S976->$0;
        int32_t _M0L6j__preS978;
        int32_t _M0L3valS3295;
        int32_t _M0L3valS3303;
        float _M0L6_2atmpS3301;
        float _M0L11alpha__postS3302;
        float _M0L6_2atmpS3297;
        struct _M0TPB5ArrayGfE* _M0L5tr__xS3300;
        float _M0L6_2atmpS3299;
        float _M0L6_2atmpS3298;
        float _M0L6_2atmpS3296;
        #line 655 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L6j__preS978
        = _M0FP26RiantR8snn__mbt20find__pre__for__conn(_M0L6rowptrS964, _M0L3valS3304);
        _M0L3valS3295 = _M0L2s2S976->$0;
        _M0L3valS3303 = _M0L2s2S976->$0;
        #line 656 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L6_2atmpS3301
        = _M0MPC15array5Array2atGfE(_M0L1wS969, _M0L3valS3303);
        _M0L11alpha__postS3302 = _M0L5paramS970->$5;
        _M0L6_2atmpS3297 = _M0L6_2atmpS3301 + _M0L11alpha__postS3302;
        _M0L5tr__xS3300 = _M0L4varsS971->$0;
        #line 656 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L6_2atmpS3299
        = _M0MPC15array5Array2atGfE(_M0L5tr__xS3300, _M0L6j__preS978);
        _M0L6_2atmpS3298 = _M0L18a__x__over__tau__xS975 * _M0L6_2atmpS3299;
        _M0L6_2atmpS3296 = _M0L6_2atmpS3297 + _M0L6_2atmpS3298;
        #line 656 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L1wS969, _M0L3valS3295, _M0L6_2atmpS3296);
      }
      _M0L3valS3306 = _M0L2s2S976->$0;
      _M0L6_2atmpS3305 = _M0L3valS3306 + 1;
      _M0L2s2S976->$0 = _M0L6_2atmpS3305;
      continue;
    } else {
      moonbit_decref(_M0L2s2S976);
    }
    break;
  }
  _M0L6tau__xS3372 = _M0L5paramS970->$2;
  _M0L11inv__tau__xS980 = 0x1p+0f / _M0L6tau__xS3372;
  _M0L6tau__yS3371 = _M0L5paramS970->$3;
  _M0L11inv__tau__yS981 = 0x1p+0f / _M0L6tau__yS3371;
  _M0L1iS982
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1iS982)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1iS982->$0 = 0;
  while (1) {
    int32_t _M0L3valS3308 = _M0L1iS982->$0;
    if (_M0L3valS3308 < _M0L7n__postS960) {
      struct _M0TPB5ArrayGfE* _M0L5to__yS3309 = _M0L4varsS971->$1;
      int32_t _M0L3valS3310 = _M0L1iS982->$0;
      struct _M0TPB5ArrayGfE* _M0L5to__yS3319 = _M0L4varsS971->$1;
      int32_t _M0L3valS3320 = _M0L1iS982->$0;
      float _M0L6_2atmpS3312;
      struct _M0TPB5ArrayGfE* _M0L5to__yS3317;
      int32_t _M0L3valS3318;
      float _M0L6_2atmpS3316;
      float _M0L6_2atmpS3315;
      float _M0L6_2atmpS3314;
      float _M0L6_2atmpS3313;
      float _M0L6_2atmpS3311;
      int32_t _M0L3valS3322;
      int32_t _M0L6_2atmpS3321;
      #line 665 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS3312
      = _M0MPC15array5Array2atGfE(_M0L5to__yS3319, _M0L3valS3320);
      _M0L5to__yS3317 = _M0L4varsS971->$1;
      _M0L3valS3318 = _M0L1iS982->$0;
      #line 665 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS3316
      = _M0MPC15array5Array2atGfE(_M0L5to__yS3317, _M0L3valS3318);
      _M0L6_2atmpS3315 = -_M0L6_2atmpS3316;
      _M0L6_2atmpS3314 = _M0L2dtS983 * _M0L6_2atmpS3315;
      _M0L6_2atmpS3313 = _M0L6_2atmpS3314 * _M0L11inv__tau__yS981;
      _M0L6_2atmpS3311 = _M0L6_2atmpS3312 + _M0L6_2atmpS3313;
      #line 665 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0MPC15array5Array3setGfE(_M0L5to__yS3309, _M0L3valS3310, _M0L6_2atmpS3311);
      _M0L3valS3322 = _M0L1iS982->$0;
      _M0L6_2atmpS3321 = _M0L3valS3322 + 1;
      _M0L1iS982->$0 = _M0L6_2atmpS3321;
      continue;
    }
    break;
  }
  _M0L1jS962->$0 = 0;
  while (1) {
    int32_t _M0L3valS3323 = _M0L1jS962->$0;
    if (_M0L3valS3323 < _M0L6n__preS958) {
      struct _M0TPB5ArrayGfE* _M0L5tr__xS3324 = _M0L4varsS971->$0;
      int32_t _M0L3valS3325 = _M0L1jS962->$0;
      struct _M0TPB5ArrayGfE* _M0L5tr__xS3334 = _M0L4varsS971->$0;
      int32_t _M0L3valS3335 = _M0L1jS962->$0;
      float _M0L6_2atmpS3327;
      struct _M0TPB5ArrayGfE* _M0L5tr__xS3332;
      int32_t _M0L3valS3333;
      float _M0L6_2atmpS3331;
      float _M0L6_2atmpS3330;
      float _M0L6_2atmpS3329;
      float _M0L6_2atmpS3328;
      float _M0L6_2atmpS3326;
      int32_t _M0L3valS3337;
      int32_t _M0L6_2atmpS3336;
      #line 670 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS3327
      = _M0MPC15array5Array2atGfE(_M0L5tr__xS3334, _M0L3valS3335);
      _M0L5tr__xS3332 = _M0L4varsS971->$0;
      _M0L3valS3333 = _M0L1jS962->$0;
      #line 670 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS3331
      = _M0MPC15array5Array2atGfE(_M0L5tr__xS3332, _M0L3valS3333);
      _M0L6_2atmpS3330 = -_M0L6_2atmpS3331;
      _M0L6_2atmpS3329 = _M0L2dtS983 * _M0L6_2atmpS3330;
      _M0L6_2atmpS3328 = _M0L6_2atmpS3329 * _M0L11inv__tau__xS980;
      _M0L6_2atmpS3326 = _M0L6_2atmpS3327 + _M0L6_2atmpS3328;
      #line 670 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0MPC15array5Array3setGfE(_M0L5tr__xS3324, _M0L3valS3325, _M0L6_2atmpS3326);
      _M0L3valS3337 = _M0L1jS962->$0;
      _M0L6_2atmpS3336 = _M0L3valS3337 + 1;
      _M0L1jS962->$0 = _M0L6_2atmpS3336;
      continue;
    }
    break;
  }
  _M0L1iS982->$0 = 0;
  while (1) {
    int32_t _M0L3valS3338 = _M0L1iS982->$0;
    if (_M0L3valS3338 < _M0L7n__postS960) {
      int32_t _M0L3valS3339 = _M0L1iS982->$0;
      int32_t _M0L3valS3347;
      int32_t _M0L6_2atmpS3346;
      #line 676 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L10post__fireS961, _M0L3valS3339)) {
        struct _M0TPB5ArrayGfE* _M0L5to__yS3340 = _M0L4varsS971->$1;
        int32_t _M0L3valS3341 = _M0L1iS982->$0;
        struct _M0TPB5ArrayGfE* _M0L5to__yS3344 = _M0L4varsS971->$1;
        int32_t _M0L3valS3345 = _M0L1iS982->$0;
        float _M0L6_2atmpS3343;
        float _M0L6_2atmpS3342;
        #line 677 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L6_2atmpS3343
        = _M0MPC15array5Array2atGfE(_M0L5to__yS3344, _M0L3valS3345);
        _M0L6_2atmpS3342 = _M0L6_2atmpS3343 + 0x1p+0f;
        #line 677 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L5to__yS3340, _M0L3valS3341, _M0L6_2atmpS3342);
      }
      _M0L3valS3347 = _M0L1iS982->$0;
      _M0L6_2atmpS3346 = _M0L3valS3347 + 1;
      _M0L1iS982->$0 = _M0L6_2atmpS3346;
      continue;
    } else {
      moonbit_decref(_M0L1iS982);
    }
    break;
  }
  _M0L1jS962->$0 = 0;
  while (1) {
    int32_t _M0L3valS3348 = _M0L1jS962->$0;
    if (_M0L3valS3348 < _M0L6n__preS958) {
      int32_t _M0L3valS3349 = _M0L1jS962->$0;
      int32_t _M0L3valS3357;
      int32_t _M0L6_2atmpS3356;
      #line 683 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L9pre__fireS959, _M0L3valS3349)) {
        struct _M0TPB5ArrayGfE* _M0L5tr__xS3350 = _M0L4varsS971->$0;
        int32_t _M0L3valS3351 = _M0L1jS962->$0;
        struct _M0TPB5ArrayGfE* _M0L5tr__xS3354 = _M0L4varsS971->$0;
        int32_t _M0L3valS3355 = _M0L1jS962->$0;
        float _M0L6_2atmpS3353;
        float _M0L6_2atmpS3352;
        #line 684 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L6_2atmpS3353
        = _M0MPC15array5Array2atGfE(_M0L5tr__xS3354, _M0L3valS3355);
        _M0L6_2atmpS3352 = _M0L6_2atmpS3353 + 0x1p+0f;
        #line 684 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L5tr__xS3350, _M0L3valS3351, _M0L6_2atmpS3352);
      }
      _M0L3valS3357 = _M0L1jS962->$0;
      _M0L6_2atmpS3356 = _M0L3valS3357 + 1;
      _M0L1jS962->$0 = _M0L6_2atmpS3356;
      continue;
    } else {
      moonbit_decref(_M0L1jS962);
    }
    break;
  }
  _M0L2s3S988
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L2s3S988)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L2s3S988->$0 = 0;
  while (1) {
    int32_t _M0L3valS3358 = _M0L2s3S988->$0;
    if (_M0L3valS3358 < _M0L3nnzS974) {
      int32_t _M0L3valS3361 = _M0L2s3S988->$0;
      float _M0L6_2atmpS3359;
      float _M0L6w__minS3360;
      int32_t _M0L3valS3370;
      int32_t _M0L6_2atmpS3369;
      #line 691 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS3359 = _M0MPC15array5Array2atGfE(_M0L1wS969, _M0L3valS3361);
      _M0L6w__minS3360 = _M0L5paramS970->$7;
      if (_M0L6_2atmpS3359 < _M0L6w__minS3360) {
        int32_t _M0L3valS3362 = _M0L2s3S988->$0;
        float _M0L6w__minS3363 = _M0L5paramS970->$7;
        #line 692 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L1wS969, _M0L3valS3362, _M0L6w__minS3363);
      } else {
        int32_t _M0L3valS3366 = _M0L2s3S988->$0;
        float _M0L6_2atmpS3364;
        float _M0L6w__maxS3365;
        #line 693 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L6_2atmpS3364
        = _M0MPC15array5Array2atGfE(_M0L1wS969, _M0L3valS3366);
        _M0L6w__maxS3365 = _M0L5paramS970->$6;
        if (_M0L6_2atmpS3364 > _M0L6w__maxS3365) {
          int32_t _M0L3valS3367 = _M0L2s3S988->$0;
          float _M0L6w__maxS3368 = _M0L5paramS970->$6;
          #line 694 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
          _M0MPC15array5Array3setGfE(_M0L1wS969, _M0L3valS3367, _M0L6w__maxS3368);
        }
      }
      _M0L3valS3370 = _M0L2s3S988->$0;
      _M0L6_2atmpS3369 = _M0L3valS3370 + 1;
      _M0L2s3S988->$0 = _M0L6_2atmpS3369;
      continue;
    } else {
      moonbit_decref(_M0L2s3S988);
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt24stdp__mexican__hat__step(
  struct _M0TPB5ArrayGfE* _M0L1wS944,
  struct _M0TPB5ArrayGbE* _M0L9pre__fireS920,
  struct _M0TPB5ArrayGbE* _M0L10post__fireS922,
  struct _M0TPB5ArrayGiE* _M0L6colptrS939,
  struct _M0TPB5ArrayGiE* _M0L6rowptrS935,
  struct _M0TPB5ArrayGfE* _M0L4tpreS930,
  struct _M0TPB5ArrayGfE* _M0L5tpostS926,
  struct _M0TP26RiantR8snn__mbt14STDPMexicanHat* _M0L5paramS924,
  float _M0L2dtS927
) {
  int32_t _M0L6n__preS919;
  int32_t _M0L7n__postS921;
  float _M0L3tauS3270;
  float _M0L8inv__tauS923;
  struct _M0TPB8MutLocalGiE* _M0L1iS925;
  struct _M0TPB8MutLocalGiE* _M0L1jS929;
  int32_t _M0L3nnzS947;
  struct _M0TPB8MutLocalGiE* _M0L2s2S948;
  struct _M0TPB8MutLocalGiE* _M0L2s3S956;
  #line 449 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  #line 460 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L6n__preS919 = _M0MPC15array5Array6lengthGbE(_M0L9pre__fireS920);
  #line 461 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L7n__postS921 = _M0MPC15array5Array6lengthGbE(_M0L10post__fireS922);
  _M0L3tauS3270 = _M0L5paramS924->$1;
  _M0L8inv__tauS923 = 0x1p+0f / _M0L3tauS3270;
  _M0L1iS925
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1iS925)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1iS925->$0 = 0;
  while (1) {
    int32_t _M0L3valS3184 = _M0L1iS925->$0;
    if (_M0L3valS3184 < _M0L7n__postS921) {
      int32_t _M0L3valS3185 = _M0L1iS925->$0;
      int32_t _M0L3valS3193 = _M0L1iS925->$0;
      float _M0L6_2atmpS3187;
      int32_t _M0L3valS3192;
      float _M0L6_2atmpS3191;
      float _M0L6_2atmpS3190;
      float _M0L6_2atmpS3189;
      float _M0L6_2atmpS3188;
      float _M0L6_2atmpS3186;
      int32_t _M0L3valS3195;
      int32_t _M0L6_2atmpS3194;
      #line 466 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS3187
      = _M0MPC15array5Array2atGfE(_M0L5tpostS926, _M0L3valS3193);
      _M0L3valS3192 = _M0L1iS925->$0;
      #line 466 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS3191
      = _M0MPC15array5Array2atGfE(_M0L5tpostS926, _M0L3valS3192);
      _M0L6_2atmpS3190 = -_M0L6_2atmpS3191;
      _M0L6_2atmpS3189 = _M0L2dtS927 * _M0L6_2atmpS3190;
      _M0L6_2atmpS3188 = _M0L6_2atmpS3189 * _M0L8inv__tauS923;
      _M0L6_2atmpS3186 = _M0L6_2atmpS3187 + _M0L6_2atmpS3188;
      #line 466 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0MPC15array5Array3setGfE(_M0L5tpostS926, _M0L3valS3185, _M0L6_2atmpS3186);
      _M0L3valS3195 = _M0L1iS925->$0;
      _M0L6_2atmpS3194 = _M0L3valS3195 + 1;
      _M0L1iS925->$0 = _M0L6_2atmpS3194;
      continue;
    }
    break;
  }
  _M0L1jS929
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1jS929)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1jS929->$0 = 0;
  while (1) {
    int32_t _M0L3valS3196 = _M0L1jS929->$0;
    if (_M0L3valS3196 < _M0L6n__preS919) {
      int32_t _M0L3valS3197 = _M0L1jS929->$0;
      int32_t _M0L3valS3205 = _M0L1jS929->$0;
      float _M0L6_2atmpS3199;
      int32_t _M0L3valS3204;
      float _M0L6_2atmpS3203;
      float _M0L6_2atmpS3202;
      float _M0L6_2atmpS3201;
      float _M0L6_2atmpS3200;
      float _M0L6_2atmpS3198;
      int32_t _M0L3valS3207;
      int32_t _M0L6_2atmpS3206;
      #line 471 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS3199
      = _M0MPC15array5Array2atGfE(_M0L4tpreS930, _M0L3valS3205);
      _M0L3valS3204 = _M0L1jS929->$0;
      #line 471 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS3203
      = _M0MPC15array5Array2atGfE(_M0L4tpreS930, _M0L3valS3204);
      _M0L6_2atmpS3202 = -_M0L6_2atmpS3203;
      _M0L6_2atmpS3201 = _M0L2dtS927 * _M0L6_2atmpS3202;
      _M0L6_2atmpS3200 = _M0L6_2atmpS3201 * _M0L8inv__tauS923;
      _M0L6_2atmpS3198 = _M0L6_2atmpS3199 + _M0L6_2atmpS3200;
      #line 471 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0MPC15array5Array3setGfE(_M0L4tpreS930, _M0L3valS3197, _M0L6_2atmpS3198);
      _M0L3valS3207 = _M0L1jS929->$0;
      _M0L6_2atmpS3206 = _M0L3valS3207 + 1;
      _M0L1jS929->$0 = _M0L6_2atmpS3206;
      continue;
    }
    break;
  }
  _M0L1iS925->$0 = 0;
  while (1) {
    int32_t _M0L3valS3208 = _M0L1iS925->$0;
    if (_M0L3valS3208 < _M0L7n__postS921) {
      int32_t _M0L3valS3209 = _M0L1iS925->$0;
      int32_t _M0L3valS3215;
      int32_t _M0L6_2atmpS3214;
      #line 477 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L10post__fireS922, _M0L3valS3209)) {
        int32_t _M0L3valS3210 = _M0L1iS925->$0;
        int32_t _M0L3valS3213 = _M0L1iS925->$0;
        float _M0L6_2atmpS3212;
        float _M0L6_2atmpS3211;
        #line 478 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L6_2atmpS3212
        = _M0MPC15array5Array2atGfE(_M0L5tpostS926, _M0L3valS3213);
        _M0L6_2atmpS3211 = _M0L6_2atmpS3212 + 0x1p+0f;
        #line 478 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L5tpostS926, _M0L3valS3210, _M0L6_2atmpS3211);
      }
      _M0L3valS3215 = _M0L1iS925->$0;
      _M0L6_2atmpS3214 = _M0L3valS3215 + 1;
      _M0L1iS925->$0 = _M0L6_2atmpS3214;
      continue;
    } else {
      moonbit_decref(_M0L1iS925);
    }
    break;
  }
  _M0L1jS929->$0 = 0;
  while (1) {
    int32_t _M0L3valS3216 = _M0L1jS929->$0;
    if (_M0L3valS3216 < _M0L6n__preS919) {
      int32_t _M0L3valS3217 = _M0L1jS929->$0;
      int32_t _M0L3valS3223;
      int32_t _M0L6_2atmpS3222;
      #line 484 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L9pre__fireS920, _M0L3valS3217)) {
        int32_t _M0L3valS3218 = _M0L1jS929->$0;
        int32_t _M0L3valS3221 = _M0L1jS929->$0;
        float _M0L6_2atmpS3220;
        float _M0L6_2atmpS3219;
        #line 485 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L6_2atmpS3220
        = _M0MPC15array5Array2atGfE(_M0L4tpreS930, _M0L3valS3221);
        _M0L6_2atmpS3219 = _M0L6_2atmpS3220 + 0x1p+0f;
        #line 485 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L4tpreS930, _M0L3valS3218, _M0L6_2atmpS3219);
      }
      _M0L3valS3223 = _M0L1jS929->$0;
      _M0L6_2atmpS3222 = _M0L3valS3223 + 1;
      _M0L1jS929->$0 = _M0L6_2atmpS3222;
      continue;
    }
    break;
  }
  _M0L1jS929->$0 = 0;
  while (1) {
    int32_t _M0L3valS3224 = _M0L1jS929->$0;
    if (_M0L3valS3224 < _M0L6n__preS919) {
      int32_t _M0L3valS3225 = _M0L1jS929->$0;
      int32_t _M0L3valS3243;
      int32_t _M0L6_2atmpS3242;
      #line 492 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L9pre__fireS920, _M0L3valS3225)) {
        int32_t _M0L3valS3241 = _M0L1jS929->$0;
        int32_t _M0L5startS934;
        int32_t _M0L3valS3240;
        int32_t _M0L6_2atmpS3239;
        int32_t _M0L3endS936;
        struct _M0TPB8MutLocalGiE* _M0L1sS937;
        #line 493 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L5startS934
        = _M0MPC15array5Array2atGiE(_M0L6rowptrS935, _M0L3valS3241);
        _M0L3valS3240 = _M0L1jS929->$0;
        _M0L6_2atmpS3239 = _M0L3valS3240 + 1;
        #line 494 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L3endS936
        = _M0MPC15array5Array2atGiE(_M0L6rowptrS935, _M0L6_2atmpS3239);
        _M0L1sS937
        = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
        Moonbit_object_header(_M0L1sS937)->meta
        = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
        _M0L1sS937->$0 = _M0L5startS934;
        while (1) {
          int32_t _M0L3valS3226 = _M0L1sS937->$0;
          if (_M0L3valS3226 < _M0L3endS936) {
            int32_t _M0L3valS3238 = _M0L1sS937->$0;
            int32_t _M0L9post__idxS938;
            int32_t _M0L3valS3237;
            float _M0L6_2atmpS3235;
            float _M0L6_2atmpS3236;
            float _M0L5ratioS940;
            float _M0L3lnxS941;
            float _M0L1xS942;
            float _M0L1aS3233;
            float _M0L6_2atmpS3234;
            float _M0L2dwS943;
            int32_t _M0L3valS3227;
            int32_t _M0L3valS3230;
            float _M0L6_2atmpS3229;
            float _M0L6_2atmpS3228;
            int32_t _M0L3valS3232;
            int32_t _M0L6_2atmpS3231;
            #line 497 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0L9post__idxS938
            = _M0MPC15array5Array2atGiE(_M0L6colptrS939, _M0L3valS3238);
            _M0L3valS3237 = _M0L1jS929->$0;
            #line 498 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0L6_2atmpS3235
            = _M0MPC15array5Array2atGfE(_M0L4tpreS930, _M0L3valS3237);
            #line 498 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0L6_2atmpS3236
            = _M0MPC15array5Array2atGfE(_M0L5tpostS926, _M0L9post__idxS938);
            _M0L5ratioS940 = _M0L6_2atmpS3235 / _M0L6_2atmpS3236;
            #line 499 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0L3lnxS941 = _M0FP26RiantR8snn__mbt4logf(_M0L5ratioS940);
            _M0L1xS942 = _M0L3lnxS941 * _M0L3lnxS941;
            _M0L1aS3233 = _M0L5paramS924->$0;
            #line 501 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0L6_2atmpS3234
            = _M0FP26RiantR8snn__mbt20mexican__hat__kernel(_M0L1xS942);
            _M0L2dwS943 = _M0L1aS3233 * _M0L6_2atmpS3234;
            _M0L3valS3227 = _M0L1sS937->$0;
            _M0L3valS3230 = _M0L1sS937->$0;
            #line 502 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0L6_2atmpS3229
            = _M0MPC15array5Array2atGfE(_M0L1wS944, _M0L3valS3230);
            _M0L6_2atmpS3228 = _M0L6_2atmpS3229 + _M0L2dwS943;
            #line 502 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0MPC15array5Array3setGfE(_M0L1wS944, _M0L3valS3227, _M0L6_2atmpS3228);
            _M0L3valS3232 = _M0L1sS937->$0;
            _M0L6_2atmpS3231 = _M0L3valS3232 + 1;
            _M0L1sS937->$0 = _M0L6_2atmpS3231;
            continue;
          } else {
            moonbit_decref(_M0L1sS937);
          }
          break;
        }
      }
      _M0L3valS3243 = _M0L1jS929->$0;
      _M0L6_2atmpS3242 = _M0L3valS3243 + 1;
      _M0L1jS929->$0 = _M0L6_2atmpS3242;
      continue;
    } else {
      moonbit_decref(_M0L1jS929);
    }
    break;
  }
  #line 510 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L3nnzS947 = _M0MPC15array5Array6lengthGfE(_M0L1wS944);
  _M0L2s2S948
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L2s2S948)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L2s2S948->$0 = 0;
  while (1) {
    int32_t _M0L3valS3244 = _M0L2s2S948->$0;
    if (_M0L3valS3244 < _M0L3nnzS947) {
      int32_t _M0L3valS3256 = _M0L2s2S948->$0;
      int32_t _M0L9post__idxS949;
      int32_t _M0L3valS3255;
      int32_t _M0L6_2atmpS3254;
      #line 513 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L9post__idxS949
      = _M0MPC15array5Array2atGiE(_M0L6colptrS939, _M0L3valS3256);
      #line 514 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      if (
        _M0MPC15array5Array2atGbE(_M0L10post__fireS922, _M0L9post__idxS949)
      ) {
        int32_t _M0L3valS3253 = _M0L2s2S948->$0;
        int32_t _M0L6j__preS950;
        float _M0L6_2atmpS3251;
        float _M0L6_2atmpS3252;
        float _M0L5ratioS951;
        float _M0L3lnxS952;
        float _M0L1xS953;
        float _M0L1aS3249;
        float _M0L6_2atmpS3250;
        float _M0L2dwS954;
        int32_t _M0L3valS3245;
        int32_t _M0L3valS3248;
        float _M0L6_2atmpS3247;
        float _M0L6_2atmpS3246;
        #line 517 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L6j__preS950
        = _M0FP26RiantR8snn__mbt20find__pre__for__conn(_M0L6rowptrS935, _M0L3valS3253);
        #line 518 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L6_2atmpS3251
        = _M0MPC15array5Array2atGfE(_M0L4tpreS930, _M0L6j__preS950);
        #line 518 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L6_2atmpS3252
        = _M0MPC15array5Array2atGfE(_M0L5tpostS926, _M0L9post__idxS949);
        _M0L5ratioS951 = _M0L6_2atmpS3251 / _M0L6_2atmpS3252;
        #line 519 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L3lnxS952 = _M0FP26RiantR8snn__mbt4logf(_M0L5ratioS951);
        _M0L1xS953 = _M0L3lnxS952 * _M0L3lnxS952;
        _M0L1aS3249 = _M0L5paramS924->$0;
        #line 521 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L6_2atmpS3250
        = _M0FP26RiantR8snn__mbt20mexican__hat__kernel(_M0L1xS953);
        _M0L2dwS954 = _M0L1aS3249 * _M0L6_2atmpS3250;
        _M0L3valS3245 = _M0L2s2S948->$0;
        _M0L3valS3248 = _M0L2s2S948->$0;
        #line 522 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L6_2atmpS3247
        = _M0MPC15array5Array2atGfE(_M0L1wS944, _M0L3valS3248);
        _M0L6_2atmpS3246 = _M0L6_2atmpS3247 + _M0L2dwS954;
        #line 522 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L1wS944, _M0L3valS3245, _M0L6_2atmpS3246);
      }
      _M0L3valS3255 = _M0L2s2S948->$0;
      _M0L6_2atmpS3254 = _M0L3valS3255 + 1;
      _M0L2s2S948->$0 = _M0L6_2atmpS3254;
      continue;
    } else {
      moonbit_decref(_M0L2s2S948);
    }
    break;
  }
  _M0L2s3S956
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L2s3S956)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L2s3S956->$0 = 0;
  while (1) {
    int32_t _M0L3valS3257 = _M0L2s3S956->$0;
    if (_M0L3valS3257 < _M0L3nnzS947) {
      int32_t _M0L3valS3260 = _M0L2s3S956->$0;
      float _M0L6_2atmpS3258;
      float _M0L6w__minS3259;
      int32_t _M0L3valS3269;
      int32_t _M0L6_2atmpS3268;
      #line 529 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS3258 = _M0MPC15array5Array2atGfE(_M0L1wS944, _M0L3valS3260);
      _M0L6w__minS3259 = _M0L5paramS924->$3;
      if (_M0L6_2atmpS3258 < _M0L6w__minS3259) {
        int32_t _M0L3valS3261 = _M0L2s3S956->$0;
        float _M0L6w__minS3262 = _M0L5paramS924->$3;
        #line 530 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L1wS944, _M0L3valS3261, _M0L6w__minS3262);
      } else {
        int32_t _M0L3valS3265 = _M0L2s3S956->$0;
        float _M0L6_2atmpS3263;
        float _M0L6w__maxS3264;
        #line 531 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L6_2atmpS3263
        = _M0MPC15array5Array2atGfE(_M0L1wS944, _M0L3valS3265);
        _M0L6w__maxS3264 = _M0L5paramS924->$2;
        if (_M0L6_2atmpS3263 > _M0L6w__maxS3264) {
          int32_t _M0L3valS3266 = _M0L2s3S956->$0;
          float _M0L6w__maxS3267 = _M0L5paramS924->$2;
          #line 532 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
          _M0MPC15array5Array3setGfE(_M0L1wS944, _M0L3valS3266, _M0L6w__maxS3267);
        }
      }
      _M0L3valS3269 = _M0L2s3S956->$0;
      _M0L6_2atmpS3268 = _M0L3valS3269 + 1;
      _M0L2s3S956->$0 = _M0L6_2atmpS3268;
      continue;
    } else {
      moonbit_decref(_M0L2s3S956);
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt20find__pre__for__conn(
  struct _M0TPB5ArrayGiE* _M0L6rowptrS913,
  int32_t _M0L1sS917
) {
  int32_t _M0L6_2atmpS3183;
  int32_t _M0L1nS912;
  struct _M0TPB8MutLocalGiE* _M0L2loS914;
  struct _M0TPB8MutLocalGiE* _M0L2hiS915;
  int32_t _result_5444;
  #line 541 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  #line 542 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L6_2atmpS3183 = _M0MPC15array5Array6lengthGiE(_M0L6rowptrS913);
  _M0L1nS912 = _M0L6_2atmpS3183 - 1;
  _M0L2loS914
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L2loS914)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L2loS914->$0 = 0;
  _M0L2hiS915
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L2hiS915)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L2hiS915->$0 = _M0L1nS912;
  while (1) {
    int32_t _M0L3valS3175 = _M0L2loS914->$0;
    int32_t _M0L3valS3176 = _M0L2hiS915->$0;
    if (_M0L3valS3175 < _M0L3valS3176) {
      int32_t _M0L3valS3181 = _M0L2loS914->$0;
      int32_t _M0L3valS3182 = _M0L2hiS915->$0;
      int32_t _M0L6_2atmpS3180 = _M0L3valS3181 + _M0L3valS3182;
      int32_t _M0L6_2atmpS3179 = _M0L6_2atmpS3180 + 1;
      int32_t _M0L3midS916 = _M0L6_2atmpS3179 / 2;
      int32_t _M0L6_2atmpS3177;
      #line 547 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS3177
      = _M0MPC15array5Array2atGiE(_M0L6rowptrS913, _M0L3midS916);
      if (_M0L6_2atmpS3177 <= _M0L1sS917) {
        _M0L2loS914->$0 = _M0L3midS916;
      } else {
        int32_t _M0L6_2atmpS3178 = _M0L3midS916 - 1;
        _M0L2hiS915->$0 = _M0L6_2atmpS3178;
      }
      continue;
    } else {
      moonbit_decref(_M0L2hiS915);
    }
    break;
  }
  _result_5444 = _M0L2loS914->$0;
  moonbit_decref(_M0L2loS914);
  return _result_5444;
}

float _M0FP26RiantR8snn__mbt20mexican__hat__kernel(float _M0L1xS909) {
  #line 426 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  #line 427 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  if (_M0MPC15float5Float7is__nan(_M0L1xS909)) {
    return 0x0p+0f;
  } else {
    float _M0L6_2atmpS3174 = -_M0L1xS909;
    float _M0L3argS910 = _M0L6_2atmpS3174 / 0x1.6a09e65dc27dfp+0f;
    float _M0L6_2atmpS3172 = 0x1p+0f - _M0L1xS909;
    float _M0L6_2atmpS3173;
    float _M0L1vS911;
    #line 431 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
    _M0L6_2atmpS3173 = _M0FP26RiantR8snn__mbt4expf(_M0L3argS910);
    _M0L1vS911 = _M0L6_2atmpS3172 * _M0L6_2atmpS3173;
    #line 432 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
    if (_M0MPC15float5Float7is__nan(_M0L1vS911)) {
      return 0x0p+0f;
    } else {
      return _M0L1vS911;
    }
  }
}

struct _M0TP26RiantR8snn__mbt9STDPEntry* _M0MP26RiantR8snn__mbt9STDPEntry3new(
  int32_t _M0L11conn__indexS906,
  int32_t _M0L6n__preS907,
  int32_t _M0L7n__postS908
) {
  struct _M0TP26RiantR8snn__mbt13STDPVariables* _M0L6_2atmpS3168;
  struct _M0TP26RiantR8snn__mbt12STDPGerstner* _M0L6_2atmpS3169;
  float* _M0L6_2atmpS3171;
  struct _M0TPB5ArrayGfE* _M0L6_2atmpS3170;
  struct _M0TP26RiantR8snn__mbt9STDPEntry* _block_5445;
  #line 89 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  #line 94 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L6_2atmpS3168
  = _M0MP26RiantR8snn__mbt13STDPVariables3new(_M0L6n__preS907, _M0L7n__postS908);
  #line 95 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L6_2atmpS3169 = _M0MP26RiantR8snn__mbt12STDPGerstner3new();
  _M0L6_2atmpS3171 = (float*)moonbit_make_float_array_raw(1);
  _M0L6_2atmpS3171[0] = 0x0p+0f;
  _M0L6_2atmpS3170
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_M0L6_2atmpS3170)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 21, 0);
  _M0L6_2atmpS3170->$0 = _M0L6_2atmpS3171;
  _M0L6_2atmpS3170->$1 = 1;
  _block_5445
  = (struct _M0TP26RiantR8snn__mbt9STDPEntry*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt9STDPEntry));
  Moonbit_object_header(_block_5445)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 68, 0);
  _block_5445->$0 = _M0L11conn__indexS906;
  _block_5445->$1 = _M0L6n__preS907;
  _block_5445->$2 = _M0L7n__postS908;
  _block_5445->$3 = _M0L6_2atmpS3168;
  _block_5445->$4 = _M0L6_2atmpS3169;
  _block_5445->$5 = _M0L6_2atmpS3170;
  return _block_5445;
}

struct _M0TP26RiantR8snn__mbt13STDPVariables* _M0MP26RiantR8snn__mbt13STDPVariables3new(
  int32_t _M0L6n__preS904,
  int32_t _M0L7n__postS905
) {
  struct _M0TPB5ArrayGfE* _M0L6_2atmpS3162;
  struct _M0TPB5ArrayGfE* _M0L6_2atmpS3163;
  struct _M0TPB5ArrayGfE* _M0L6_2atmpS3164;
  struct _M0TPB5ArrayGfE* _M0L6_2atmpS3165;
  uint8_t* _M0L6_2atmpS3167;
  struct _M0TPB5ArrayGbE* _M0L6_2atmpS3166;
  struct _M0TP26RiantR8snn__mbt13STDPVariables* _block_5446;
  #line 55 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  #line 57 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L6_2atmpS3162 = _M0MPC15array5Array4makeGfE(_M0L6n__preS904, 0x0p+0f);
  #line 58 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L6_2atmpS3163 = _M0MPC15array5Array4makeGfE(_M0L7n__postS905, 0x0p+0f);
  #line 59 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L6_2atmpS3164 = _M0MPC15array5Array4makeGfE(_M0L6n__preS904, 0x0p+0f);
  #line 60 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L6_2atmpS3165 = _M0MPC15array5Array4makeGfE(_M0L7n__postS905, 0x0p+0f);
  _M0L6_2atmpS3167 = (uint8_t*)moonbit_make_bytes_raw(1);
  _M0L6_2atmpS3167[0] = 1;
  _M0L6_2atmpS3166
  = (struct _M0TPB5ArrayGbE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGbE));
  Moonbit_object_header(_M0L6_2atmpS3166)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 73, 0);
  _M0L6_2atmpS3166->$0 = _M0L6_2atmpS3167;
  _M0L6_2atmpS3166->$1 = 1;
  _block_5446
  = (struct _M0TP26RiantR8snn__mbt13STDPVariables*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt13STDPVariables));
  Moonbit_object_header(_block_5446)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 76, 0);
  _block_5446->$0 = _M0L6_2atmpS3162;
  _block_5446->$1 = _M0L6_2atmpS3163;
  _block_5446->$2 = _M0L6_2atmpS3164;
  _block_5446->$3 = _M0L6_2atmpS3165;
  _block_5446->$4 = _M0L6_2atmpS3166;
  return _block_5446;
}

struct _M0TP26RiantR8snn__mbt12STDPGerstner* _M0MP26RiantR8snn__mbt12STDPGerstner3new(
  
) {
  struct _M0TP26RiantR8snn__mbt12STDPGerstner* _block_5447;
  #line 34 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _block_5447
  = (struct _M0TP26RiantR8snn__mbt12STDPGerstner*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt12STDPGerstner));
  Moonbit_object_header(_block_5447)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_5447->$0 = 0x1.47ae147ae147bp-7f;
  _block_5447->$1 = 0x1.47ae147ae147bp-7f;
  _block_5447->$2 = 0x1.4p+4f;
  _block_5447->$3 = 0x1.4p+4f;
  _block_5447->$4 = 0x1.ep+4f;
  _block_5447->$5 = 0x0p+0f;
  return _block_5447;
}

int32_t _M0FP26RiantR8snn__mbt19stimulate__balanced(
  struct _M0TP26RiantR8snn__mbt16BalancedStimulus* _M0L1sS871,
  float _M0L4timeS869,
  float _M0L2dtS880
) {
  int32_t _M0L1nS870;
  struct _M0TP26RiantR8snn__mbt17BalancedParameter* _M0L5paramS872;
  float _M0L3kIES873;
  float _M0L4betaS874;
  float _M0L3tauS875;
  float _M0L2r0S876;
  float _M0L1wS877;
  float _M0L3wIES878;
  float _M0L6_2atmpS3161;
  float _M0L11inh__lambdaS879;
  int32_t _M0L7_2abindS881;
  int32_t _M0L1kS882;
  float _M0L6_2atmpS3160;
  float _M0L2ccS886;
  #line 128 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_balanced.mbt"
  _M0L1nS870 = _M0L1sS871->$1;
  _M0L5paramS872 = _M0L1sS871->$0;
  _M0L3kIES873 = _M0L5paramS872->$0;
  _M0L4betaS874 = _M0L5paramS872->$1;
  _M0L3tauS875 = _M0L5paramS872->$2;
  _M0L2r0S876 = _M0L5paramS872->$3;
  _M0L1wS877 = _M0L5paramS872->$4;
  _M0L3wIES878 = _M0L5paramS872->$5;
  _M0L6_2atmpS3161 = _M0L2r0S876 * _M0L3kIES873;
  _M0L11inh__lambdaS879 = _M0L6_2atmpS3161 * _M0L2dtS880;
  _M0L7_2abindS881 = 0;
  _M0L1kS882 = _M0L7_2abindS881;
  while (1) {
    if (_M0L1kS882 < _M0L1nS870) {
      struct _M0TPB5ArrayGbE* _M0L4fireS3074 = _M0L1sS871->$4;
      struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS3082;
      int32_t _M0L1mS885;
      int32_t _M0L6_2atmpS3073;
      #line 146 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_balanced.mbt"
      _M0MPC15array5Array3setGbE(_M0L4fireS3074, _M0L1kS882, 0);
      if (_M0L11inh__lambdaS879 <= 0x0p+0f) {
        goto join_883;
      }
      _M0L3rngS3082 = _M0L1sS871->$7;
      #line 150 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_balanced.mbt"
      _M0L1mS885
      = _M0FP26RiantR8snn__mbt15sample__poisson(_M0L3rngS3082, _M0L11inh__lambdaS879);
      if (_M0L1mS885 > 0) {
        struct _M0TPB5ArrayGfE* _M0L2giS3075 = _M0L1sS871->$3;
        struct _M0TPB5ArrayGfE* _M0L2giS3081 = _M0L1sS871->$3;
        float _M0L6_2atmpS3077;
        float _M0L6_2atmpS3080;
        float _M0L6_2atmpS3079;
        float _M0L6_2atmpS3078;
        float _M0L6_2atmpS3076;
        #line 152 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_balanced.mbt"
        _M0L6_2atmpS3077
        = _M0MPC15array5Array2atGfE(_M0L2giS3081, _M0L1kS882);
        _M0L6_2atmpS3080 = (float)_M0L1mS885;
        _M0L6_2atmpS3079 = _M0L1wS877 * _M0L6_2atmpS3080;
        _M0L6_2atmpS3078 = _M0L6_2atmpS3079 * _M0L3wIES878;
        _M0L6_2atmpS3076 = _M0L6_2atmpS3077 + _M0L6_2atmpS3078;
        #line 152 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_balanced.mbt"
        _M0MPC15array5Array3setGfE(_M0L2giS3075, _M0L1kS882, _M0L6_2atmpS3076);
      }
      goto join_883;
      goto joinlet_5449;
      join_883:;
      _M0L6_2atmpS3073 = _M0L1kS882 + 1;
      _M0L1kS882 = _M0L6_2atmpS3073;
      continue;
      joinlet_5449:;
    }
    break;
  }
  _M0L6_2atmpS3160 = _M0L2dtS880 / _M0L3tauS875;
  _M0L2ccS886 = 0x1p+0f - _M0L6_2atmpS3160;
  if (_M0L5paramS872->$6) {
    struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS3120 = _M0L1sS871->$7;
    double _M0L6_2atmpS3119;
    float _M0L6_2atmpS3118;
    float _M0L2reS887;
    struct _M0TPB5ArrayGfE* _M0L5noiseS3083;
    struct _M0TPB5ArrayGfE* _M0L5noiseS3088;
    float _M0L6_2atmpS3087;
    float _M0L6_2atmpS3086;
    float _M0L6_2atmpS3085;
    float _M0L6_2atmpS3084;
    struct _M0TPB5ArrayGfE* _M0L5noiseS3117;
    float _M0L6_2atmpS3116;
    float _M0L6_2atmpS3115;
    struct _M0TPB8MutLocalGfE* _M0L2nbS888;
    float _M0L3valS3089;
    float _M0L3valS3090;
    float _M0L6_2atmpS3113;
    float _M0L3valS3114;
    float _M0L6_2atmpS3110;
    struct _M0TPB5ArrayGfE* _M0L1rS3112;
    float _M0L6_2atmpS3111;
    float _M0L6_2atmpS3109;
    struct _M0TPB8MutLocalGfE* _M0L5erateS889;
    float _M0L3valS3091;
    struct _M0TPB5ArrayGfE* _M0L1rS3092;
    struct _M0TPB5ArrayGfE* _M0L1rS3099;
    float _M0L6_2atmpS3094;
    float _M0L3valS3098;
    float _M0L6_2atmpS3097;
    float _M0L6_2atmpS3096;
    float _M0L6_2atmpS3095;
    float _M0L6_2atmpS3093;
    float _M0L3valS3108;
    float _M0L11exc__lambdaS890;
    struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS3107;
    int32_t _M0L1mS891;
    #line 163 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_balanced.mbt"
    _M0L6_2atmpS3119 = _M0FP26RiantR8snn__mbt9next__f64(_M0L3rngS3120);
    _M0L6_2atmpS3118 = (float)_M0L6_2atmpS3119;
    _M0L2reS887 = _M0L6_2atmpS3118 - 0x1p-1f;
    _M0L5noiseS3083 = _M0L1sS871->$6;
    _M0L5noiseS3088 = _M0L1sS871->$6;
    #line 164 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_balanced.mbt"
    _M0L6_2atmpS3087 = _M0MPC15array5Array2atGfE(_M0L5noiseS3088, 0);
    _M0L6_2atmpS3086 = _M0L6_2atmpS3087 - _M0L2reS887;
    _M0L6_2atmpS3085 = _M0L6_2atmpS3086 * _M0L2ccS886;
    _M0L6_2atmpS3084 = _M0L6_2atmpS3085 + _M0L2reS887;
    #line 164 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_balanced.mbt"
    _M0MPC15array5Array3setGfE(_M0L5noiseS3083, 0, _M0L6_2atmpS3084);
    _M0L5noiseS3117 = _M0L1sS871->$6;
    #line 165 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_balanced.mbt"
    _M0L6_2atmpS3116 = _M0MPC15array5Array2atGfE(_M0L5noiseS3117, 0);
    _M0L6_2atmpS3115 = _M0L6_2atmpS3116 * _M0L4betaS874;
    _M0L2nbS888
    = (struct _M0TPB8MutLocalGfE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGfE));
    Moonbit_object_header(_M0L2nbS888)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
    _M0L2nbS888->$0 = _M0L6_2atmpS3115;
    _M0L3valS3089 = _M0L2nbS888->$0;
    if (_M0L3valS3089 > 0x1p+0f) {
      _M0L2nbS888->$0 = 0x1p+0f;
    }
    _M0L3valS3090 = _M0L2nbS888->$0;
    if (_M0L3valS3090 < 0x0p+0f) {
      _M0L2nbS888->$0 = 0x0p+0f;
    }
    _M0L6_2atmpS3113 = _M0L2r0S876 / 0x1p+1f;
    _M0L3valS3114 = _M0L2nbS888->$0;
    moonbit_decref(_M0L2nbS888);
    _M0L6_2atmpS3110 = _M0L6_2atmpS3113 * _M0L3valS3114;
    _M0L1rS3112 = _M0L1sS871->$5;
    #line 172 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_balanced.mbt"
    _M0L6_2atmpS3111 = _M0MPC15array5Array2atGfE(_M0L1rS3112, 0);
    _M0L6_2atmpS3109 = _M0L6_2atmpS3110 + _M0L6_2atmpS3111;
    _M0L5erateS889
    = (struct _M0TPB8MutLocalGfE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGfE));
    Moonbit_object_header(_M0L5erateS889)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
    _M0L5erateS889->$0 = _M0L6_2atmpS3109;
    _M0L3valS3091 = _M0L5erateS889->$0;
    if (_M0L3valS3091 < 0x0p+0f) {
      _M0L5erateS889->$0 = 0x0p+0f;
    }
    _M0L1rS3092 = _M0L1sS871->$5;
    _M0L1rS3099 = _M0L1sS871->$5;
    #line 176 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_balanced.mbt"
    _M0L6_2atmpS3094 = _M0MPC15array5Array2atGfE(_M0L1rS3099, 0);
    _M0L3valS3098 = _M0L5erateS889->$0;
    _M0L6_2atmpS3097 = _M0L2r0S876 - _M0L3valS3098;
    _M0L6_2atmpS3096 = _M0L6_2atmpS3097 / 0x1.9p+8f;
    _M0L6_2atmpS3095 = _M0L6_2atmpS3096 * _M0L2dtS880;
    _M0L6_2atmpS3093 = _M0L6_2atmpS3094 + _M0L6_2atmpS3095;
    #line 176 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_balanced.mbt"
    _M0MPC15array5Array3setGfE(_M0L1rS3092, 0, _M0L6_2atmpS3093);
    _M0L3valS3108 = _M0L5erateS889->$0;
    moonbit_decref(_M0L5erateS889);
    _M0L11exc__lambdaS890 = _M0L3valS3108 * _M0L2dtS880;
    _M0L3rngS3107 = _M0L1sS871->$7;
    #line 178 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_balanced.mbt"
    _M0L1mS891
    = _M0FP26RiantR8snn__mbt15sample__poisson(_M0L3rngS3107, _M0L11exc__lambdaS890);
    if (_M0L1mS891 > 0) {
      float _M0L6_2atmpS3106 = (float)_M0L1mS891;
      float _M0L3addS892 = _M0L1wS877 * _M0L6_2atmpS3106;
      int32_t _M0L7_2abindS893 = 0;
      int32_t _M0L1iS894 = _M0L7_2abindS893;
      while (1) {
        if (_M0L1iS894 < _M0L1nS870) {
          struct _M0TPB5ArrayGfE* _M0L2geS3100 = _M0L1sS871->$2;
          struct _M0TPB5ArrayGfE* _M0L2geS3103 = _M0L1sS871->$2;
          float _M0L6_2atmpS3102;
          float _M0L6_2atmpS3101;
          struct _M0TPB5ArrayGbE* _M0L4fireS3104;
          int32_t _M0L6_2atmpS3105;
          #line 182 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_balanced.mbt"
          _M0L6_2atmpS3102
          = _M0MPC15array5Array2atGfE(_M0L2geS3103, _M0L1iS894);
          _M0L6_2atmpS3101 = _M0L6_2atmpS3102 + _M0L3addS892;
          #line 182 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_balanced.mbt"
          _M0MPC15array5Array3setGfE(_M0L2geS3100, _M0L1iS894, _M0L6_2atmpS3101);
          _M0L4fireS3104 = _M0L1sS871->$4;
          #line 183 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_balanced.mbt"
          _M0MPC15array5Array3setGbE(_M0L4fireS3104, _M0L1iS894, 1);
          _M0L6_2atmpS3105 = _M0L1iS894 + 1;
          _M0L1iS894 = _M0L6_2atmpS3105;
          continue;
        }
        break;
      }
    }
  } else {
    int32_t _M0L7_2abindS896 = 0;
    int32_t _M0L1iS897 = _M0L7_2abindS896;
    while (1) {
      if (_M0L1iS897 < _M0L1nS870) {
        struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS3158 = _M0L1sS871->$7;
        double _M0L6_2atmpS3157;
        float _M0L6_2atmpS3156;
        float _M0L2reS898;
        struct _M0TPB5ArrayGfE* _M0L5noiseS3121;
        struct _M0TPB5ArrayGfE* _M0L5noiseS3126;
        float _M0L6_2atmpS3125;
        float _M0L6_2atmpS3124;
        float _M0L6_2atmpS3123;
        float _M0L6_2atmpS3122;
        struct _M0TPB5ArrayGfE* _M0L5noiseS3155;
        float _M0L6_2atmpS3154;
        float _M0L6_2atmpS3153;
        struct _M0TPB8MutLocalGfE* _M0L2nbS899;
        float _M0L3valS3127;
        float _M0L3valS3128;
        float _M0L6_2atmpS3151;
        float _M0L3valS3152;
        float _M0L6_2atmpS3148;
        struct _M0TPB5ArrayGfE* _M0L1rS3150;
        float _M0L6_2atmpS3149;
        float _M0L6_2atmpS3147;
        struct _M0TPB8MutLocalGfE* _M0L5erateS900;
        float _M0L3valS3129;
        struct _M0TPB5ArrayGfE* _M0L1rS3130;
        struct _M0TPB5ArrayGfE* _M0L1rS3137;
        float _M0L6_2atmpS3132;
        float _M0L3valS3136;
        float _M0L6_2atmpS3135;
        float _M0L6_2atmpS3134;
        float _M0L6_2atmpS3133;
        float _M0L6_2atmpS3131;
        float _M0L3valS3146;
        float _M0L11exc__lambdaS901;
        struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS3145;
        int32_t _M0L1mS902;
        int32_t _M0L6_2atmpS3159;
        #line 188 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_balanced.mbt"
        _M0L6_2atmpS3157 = _M0FP26RiantR8snn__mbt9next__f64(_M0L3rngS3158);
        _M0L6_2atmpS3156 = (float)_M0L6_2atmpS3157;
        _M0L2reS898 = _M0L6_2atmpS3156 - 0x1p-1f;
        _M0L5noiseS3121 = _M0L1sS871->$6;
        _M0L5noiseS3126 = _M0L1sS871->$6;
        #line 189 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_balanced.mbt"
        _M0L6_2atmpS3125
        = _M0MPC15array5Array2atGfE(_M0L5noiseS3126, _M0L1iS897);
        _M0L6_2atmpS3124 = _M0L6_2atmpS3125 - _M0L2reS898;
        _M0L6_2atmpS3123 = _M0L6_2atmpS3124 * _M0L2ccS886;
        _M0L6_2atmpS3122 = _M0L6_2atmpS3123 + _M0L2reS898;
        #line 189 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_balanced.mbt"
        _M0MPC15array5Array3setGfE(_M0L5noiseS3121, _M0L1iS897, _M0L6_2atmpS3122);
        _M0L5noiseS3155 = _M0L1sS871->$6;
        #line 190 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_balanced.mbt"
        _M0L6_2atmpS3154
        = _M0MPC15array5Array2atGfE(_M0L5noiseS3155, _M0L1iS897);
        _M0L6_2atmpS3153 = _M0L6_2atmpS3154 * _M0L4betaS874;
        _M0L2nbS899
        = (struct _M0TPB8MutLocalGfE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGfE));
        Moonbit_object_header(_M0L2nbS899)->meta
        = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
        _M0L2nbS899->$0 = _M0L6_2atmpS3153;
        _M0L3valS3127 = _M0L2nbS899->$0;
        if (_M0L3valS3127 > 0x1p+0f) {
          _M0L2nbS899->$0 = 0x1p+0f;
        }
        _M0L3valS3128 = _M0L2nbS899->$0;
        if (_M0L3valS3128 < 0x0p+0f) {
          _M0L2nbS899->$0 = 0x0p+0f;
        }
        _M0L6_2atmpS3151 = _M0L2r0S876 / 0x1p+1f;
        _M0L3valS3152 = _M0L2nbS899->$0;
        moonbit_decref(_M0L2nbS899);
        _M0L6_2atmpS3148 = _M0L6_2atmpS3151 * _M0L3valS3152;
        _M0L1rS3150 = _M0L1sS871->$5;
        #line 197 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_balanced.mbt"
        _M0L6_2atmpS3149 = _M0MPC15array5Array2atGfE(_M0L1rS3150, _M0L1iS897);
        _M0L6_2atmpS3147 = _M0L6_2atmpS3148 + _M0L6_2atmpS3149;
        _M0L5erateS900
        = (struct _M0TPB8MutLocalGfE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGfE));
        Moonbit_object_header(_M0L5erateS900)->meta
        = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
        _M0L5erateS900->$0 = _M0L6_2atmpS3147;
        _M0L3valS3129 = _M0L5erateS900->$0;
        if (_M0L3valS3129 < 0x0p+0f) {
          _M0L5erateS900->$0 = 0x0p+0f;
        }
        _M0L1rS3130 = _M0L1sS871->$5;
        _M0L1rS3137 = _M0L1sS871->$5;
        #line 201 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_balanced.mbt"
        _M0L6_2atmpS3132 = _M0MPC15array5Array2atGfE(_M0L1rS3137, _M0L1iS897);
        _M0L3valS3136 = _M0L5erateS900->$0;
        _M0L6_2atmpS3135 = _M0L2r0S876 - _M0L3valS3136;
        _M0L6_2atmpS3134 = _M0L6_2atmpS3135 / 0x1.9p+8f;
        _M0L6_2atmpS3133 = _M0L6_2atmpS3134 * _M0L2dtS880;
        _M0L6_2atmpS3131 = _M0L6_2atmpS3132 + _M0L6_2atmpS3133;
        #line 201 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_balanced.mbt"
        _M0MPC15array5Array3setGfE(_M0L1rS3130, _M0L1iS897, _M0L6_2atmpS3131);
        _M0L3valS3146 = _M0L5erateS900->$0;
        moonbit_decref(_M0L5erateS900);
        _M0L11exc__lambdaS901 = _M0L3valS3146 * _M0L2dtS880;
        _M0L3rngS3145 = _M0L1sS871->$7;
        #line 203 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_balanced.mbt"
        _M0L1mS902
        = _M0FP26RiantR8snn__mbt15sample__poisson(_M0L3rngS3145, _M0L11exc__lambdaS901);
        if (_M0L1mS902 > 0) {
          struct _M0TPB5ArrayGfE* _M0L2geS3138 = _M0L1sS871->$2;
          struct _M0TPB5ArrayGfE* _M0L2geS3143 = _M0L1sS871->$2;
          float _M0L6_2atmpS3140;
          float _M0L6_2atmpS3142;
          float _M0L6_2atmpS3141;
          float _M0L6_2atmpS3139;
          struct _M0TPB5ArrayGbE* _M0L4fireS3144;
          #line 205 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_balanced.mbt"
          _M0L6_2atmpS3140
          = _M0MPC15array5Array2atGfE(_M0L2geS3143, _M0L1iS897);
          _M0L6_2atmpS3142 = (float)_M0L1mS902;
          _M0L6_2atmpS3141 = _M0L1wS877 * _M0L6_2atmpS3142;
          _M0L6_2atmpS3139 = _M0L6_2atmpS3140 + _M0L6_2atmpS3141;
          #line 205 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_balanced.mbt"
          _M0MPC15array5Array3setGfE(_M0L2geS3138, _M0L1iS897, _M0L6_2atmpS3139);
          _M0L4fireS3144 = _M0L1sS871->$4;
          #line 206 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_balanced.mbt"
          _M0MPC15array5Array3setGbE(_M0L4fireS3144, _M0L1iS897, 1);
        }
        _M0L6_2atmpS3159 = _M0L1iS897 + 1;
        _M0L1iS897 = _M0L6_2atmpS3159;
        continue;
      }
      break;
    }
  }
  return 0;
}

struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0MP26RiantR8snn__mbt7Xoshiro3new(
  uint64_t _M0L4seedS867
) {
  struct _M0TUmmmmE* _M0L1sS866;
  uint64_t _M0L6_2atmpS3072;
  struct _M0TUmmmmE* _M0L1tS868;
  uint64_t _M0L6_2atmpS3068;
  uint64_t _M0L6_2atmpS3069;
  uint64_t _M0L6_2atmpS3070;
  uint64_t _M0L6_2atmpS3071;
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _block_5452;
  #line 48 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  #line 49 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L1sS866 = _M0FP26RiantR8snn__mbt10splitmix64(_M0L4seedS867);
  _M0L6_2atmpS3072 = _M0L1sS866->$3;
  #line 50 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L1tS868 = _M0FP26RiantR8snn__mbt10splitmix64(_M0L6_2atmpS3072);
  _M0L6_2atmpS3068 = _M0L1sS866->$0;
  _M0L6_2atmpS3069 = _M0L1sS866->$1;
  _M0L6_2atmpS3070 = _M0L1sS866->$2;
  moonbit_decref(_M0L1sS866);
  _M0L6_2atmpS3071 = _M0L1tS868->$0;
  moonbit_decref(_M0L1tS868);
  _block_5452
  = (struct _M0TP26RiantR8snn__mbt7Xoshiro*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt7Xoshiro));
  Moonbit_object_header(_block_5452)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_5452->$0 = _M0L6_2atmpS3068;
  _block_5452->$1 = _M0L6_2atmpS3069;
  _block_5452->$2 = _M0L6_2atmpS3070;
  _block_5452->$3 = _M0L6_2atmpS3071;
  return _block_5452;
}

struct _M0TUmmmmE* _M0FP26RiantR8snn__mbt10splitmix64(uint64_t _M0L4seedS858) {
  uint64_t _M0L2s1S857;
  uint64_t _M0L2z1S859;
  uint64_t _M0L2s2S860;
  uint64_t _M0L2z2S861;
  uint64_t _M0L2s3S862;
  uint64_t _M0L2z3S863;
  uint64_t _M0L2s4S864;
  uint64_t _M0L2z4S865;
  struct _M0TUmmmmE* _block_5453;
  #line 62 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2s1S857 = _M0L4seedS858 + 11400714819323198485ull;
  #line 64 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2z1S859 = _M0FP26RiantR8snn__mbt5mix64(_M0L2s1S857);
  _M0L2s2S860 = _M0L2s1S857 + 11400714819323198485ull;
  #line 66 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2z2S861 = _M0FP26RiantR8snn__mbt5mix64(_M0L2s2S860);
  _M0L2s3S862 = _M0L2s2S860 + 11400714819323198485ull;
  #line 68 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2z3S863 = _M0FP26RiantR8snn__mbt5mix64(_M0L2s3S862);
  _M0L2s4S864 = _M0L2s3S862 + 11400714819323198485ull;
  #line 70 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2z4S865 = _M0FP26RiantR8snn__mbt5mix64(_M0L2s4S864);
  _block_5453 = (struct _M0TUmmmmE*)moonbit_malloc(sizeof(struct _M0TUmmmmE));
  Moonbit_object_header(_block_5453)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_5453->$0 = _M0L2z1S859;
  _block_5453->$1 = _M0L2z2S861;
  _block_5453->$2 = _M0L2z3S863;
  _block_5453->$3 = _M0L2z4S865;
  return _block_5453;
}

uint64_t _M0FP26RiantR8snn__mbt5mix64(uint64_t _M0L1zS855) {
  uint64_t _M0L6_2atmpS3067;
  uint64_t _M0L6_2atmpS3066;
  uint64_t _M0L1zS854;
  uint64_t _M0L6_2atmpS3065;
  uint64_t _M0L6_2atmpS3064;
  uint64_t _M0L1zS856;
  uint64_t _M0L6_2atmpS3063;
  #line 75 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L6_2atmpS3067 = _M0L1zS855 >> 30;
  _M0L6_2atmpS3066 = _M0L1zS855 ^ _M0L6_2atmpS3067;
  _M0L1zS854 = _M0L6_2atmpS3066 * 13787848793156543929ull;
  _M0L6_2atmpS3065 = _M0L1zS854 >> 27;
  _M0L6_2atmpS3064 = _M0L1zS854 ^ _M0L6_2atmpS3065;
  _M0L1zS856 = _M0L6_2atmpS3064 * 10723151780598845931ull;
  _M0L6_2atmpS3063 = _M0L1zS856 >> 31;
  return _M0L1zS856 ^ _M0L6_2atmpS3063;
}

int32_t _M0FP26RiantR8snn__mbt25stimulate__current__array(
  struct _M0TP26RiantR8snn__mbt20CurrentStimulusArray* _M0L1sS842
) {
  struct _M0TPB5ArrayGbE* _M0L6activeS3047;
  int32_t _M0L6_2atmpS3046;
  float _M0L12noise__sigmaS3048;
  #line 123 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_current.mbt"
  _M0L6activeS3047 = _M0L1sS842->$1;
  #line 124 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_current.mbt"
  _M0L6_2atmpS3046 = _M0MPC15array5Array2atGbE(_M0L6activeS3047, 0);
  if (!_M0L6_2atmpS3046) {
    return 0;
  }
  _M0L12noise__sigmaS3048 = _M0L1sS842->$4;
  if (_M0L12noise__sigmaS3048 <= 0x0p+0f) {
    int32_t _M0L7_2abindS843 = 0;
    int32_t _M0L7_2abindS844 = _M0L1sS842->$3;
    int32_t _M0L1kS845 = _M0L7_2abindS843;
    while (1) {
      if (_M0L1kS845 < _M0L7_2abindS844) {
        struct _M0TPB5ArrayGfE* _M0L1iS3049 = _M0L1sS842->$2;
        float _M0L7i__baseS3050 = _M0L1sS842->$0;
        int32_t _M0L6_2atmpS3051;
        #line 129 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_current.mbt"
        _M0MPC15array5Array3setGfE(_M0L1iS3049, _M0L1kS845, _M0L7i__baseS3050);
        _M0L6_2atmpS3051 = _M0L1kS845 + 1;
        _M0L1kS845 = _M0L6_2atmpS3051;
        continue;
      }
      break;
    }
  } else {
    float _M0L5sigmaS847 = _M0L1sS842->$4;
    struct _M0TPB8MutLocalGiE* _M0L1kS848 =
      (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
    Moonbit_object_header(_M0L1kS848)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
    _M0L1kS848->$0 = 0;
    while (1) {
      int32_t _M0L3valS3052 = _M0L1kS848->$0;
      int32_t _M0L1nS3053 = _M0L1sS842->$3;
      if (_M0L3valS3052 < _M0L1nS3053) {
        double _M0L2z1S850;
        struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS3062 = _M0L1sS842->$5;
        struct _M0TUddE* _M0L7_2abindS851;
        double _M0L5_2az1S852;
        struct _M0TPB5ArrayGfE* _M0L1iS3054;
        int32_t _M0L3valS3055;
        float _M0L7i__baseS3057;
        float _M0L6_2atmpS3059;
        float _M0L6_2atmpS3058;
        float _M0L6_2atmpS3056;
        int32_t _M0L3valS3061;
        int32_t _M0L6_2atmpS3060;
        #line 135 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_current.mbt"
        _M0L7_2abindS851 = _M0FP26RiantR8snn__mbt11box__muller(_M0L3rngS3062);
        _M0L5_2az1S852 = _M0L7_2abindS851->$0;
        moonbit_decref(_M0L7_2abindS851);
        _M0L2z1S850 = _M0L5_2az1S852;
        goto join_849;
        goto joinlet_5456;
        join_849:;
        _M0L1iS3054 = _M0L1sS842->$2;
        _M0L3valS3055 = _M0L1kS848->$0;
        _M0L7i__baseS3057 = _M0L1sS842->$0;
        _M0L6_2atmpS3059 = (float)_M0L2z1S850;
        _M0L6_2atmpS3058 = _M0L5sigmaS847 * _M0L6_2atmpS3059;
        _M0L6_2atmpS3056 = _M0L7i__baseS3057 + _M0L6_2atmpS3058;
        #line 136 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_current.mbt"
        _M0MPC15array5Array3setGfE(_M0L1iS3054, _M0L3valS3055, _M0L6_2atmpS3056);
        _M0L3valS3061 = _M0L1kS848->$0;
        _M0L6_2atmpS3060 = _M0L3valS3061 + 1;
        _M0L1kS848->$0 = _M0L6_2atmpS3060;
        joinlet_5456:;
        continue;
      } else {
        moonbit_decref(_M0L1kS848);
      }
      break;
    }
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt22stimulate__current__if(
  struct _M0TP26RiantR8snn__mbt17CurrentStimulusIF* _M0L1sS829
) {
  struct _M0TPB5ArrayGbE* _M0L6activeS3031;
  int32_t _M0L6_2atmpS3030;
  struct _M0TP26RiantR8snn__mbt2IF* _M0L3popS830;
  int32_t _M0L1nS831;
  float _M0L12noise__sigmaS3032;
  #line 55 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_current.mbt"
  _M0L6activeS3031 = _M0L1sS829->$1;
  #line 56 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_current.mbt"
  _M0L6_2atmpS3030 = _M0MPC15array5Array2atGbE(_M0L6activeS3031, 0);
  if (!_M0L6_2atmpS3030) {
    return 0;
  }
  _M0L3popS830 = _M0L1sS829->$2;
  _M0L1nS831 = _M0L3popS830->$2;
  _M0L12noise__sigmaS3032 = _M0L1sS829->$3;
  if (_M0L12noise__sigmaS3032 <= 0x0p+0f) {
    int32_t _M0L7_2abindS832 = 0;
    int32_t _M0L1iS833 = _M0L7_2abindS832;
    while (1) {
      if (_M0L1iS833 < _M0L1nS831) {
        struct _M0TPB5ArrayGfE* _M0L1iS3033 = _M0L3popS830->$7;
        float _M0L7i__baseS3034 = _M0L1sS829->$0;
        int32_t _M0L6_2atmpS3035;
        #line 63 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_current.mbt"
        _M0MPC15array5Array3setGfE(_M0L1iS3033, _M0L1iS833, _M0L7i__baseS3034);
        _M0L6_2atmpS3035 = _M0L1iS833 + 1;
        _M0L1iS833 = _M0L6_2atmpS3035;
        continue;
      }
      break;
    }
  } else {
    float _M0L5sigmaS835 = _M0L1sS829->$3;
    struct _M0TPB8MutLocalGiE* _M0L1kS836 =
      (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
    Moonbit_object_header(_M0L1kS836)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
    _M0L1kS836->$0 = 0;
    while (1) {
      int32_t _M0L3valS3036 = _M0L1kS836->$0;
      if (_M0L3valS3036 < _M0L1nS831) {
        double _M0L2z1S838;
        struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS3045 = _M0L1sS829->$4;
        struct _M0TUddE* _M0L7_2abindS839;
        double _M0L5_2az1S840;
        struct _M0TPB5ArrayGfE* _M0L1iS3037;
        int32_t _M0L3valS3038;
        float _M0L7i__baseS3040;
        float _M0L6_2atmpS3042;
        float _M0L6_2atmpS3041;
        float _M0L6_2atmpS3039;
        int32_t _M0L3valS3044;
        int32_t _M0L6_2atmpS3043;
        #line 69 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_current.mbt"
        _M0L7_2abindS839 = _M0FP26RiantR8snn__mbt11box__muller(_M0L3rngS3045);
        _M0L5_2az1S840 = _M0L7_2abindS839->$0;
        moonbit_decref(_M0L7_2abindS839);
        _M0L2z1S838 = _M0L5_2az1S840;
        goto join_837;
        goto joinlet_5459;
        join_837:;
        _M0L1iS3037 = _M0L3popS830->$7;
        _M0L3valS3038 = _M0L1kS836->$0;
        _M0L7i__baseS3040 = _M0L1sS829->$0;
        _M0L6_2atmpS3042 = (float)_M0L2z1S838;
        _M0L6_2atmpS3041 = _M0L5sigmaS835 * _M0L6_2atmpS3042;
        _M0L6_2atmpS3039 = _M0L7i__baseS3040 + _M0L6_2atmpS3041;
        #line 70 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_current.mbt"
        _M0MPC15array5Array3setGfE(_M0L1iS3037, _M0L3valS3038, _M0L6_2atmpS3039);
        _M0L3valS3044 = _M0L1kS836->$0;
        _M0L6_2atmpS3043 = _M0L3valS3044 + 1;
        _M0L1kS836->$0 = _M0L6_2atmpS3043;
        joinlet_5459:;
        continue;
      } else {
        moonbit_decref(_M0L1kS836);
      }
      break;
    }
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt13stimulate__if(
  struct _M0TP26RiantR8snn__mbt17PoissonStimulusIF* _M0L1sS818,
  float _M0L4timeS828,
  float _M0L2dtS820
) {
  struct _M0TP26RiantR8snn__mbt12PoissonFixed* _M0L5paramS3017;
  struct _M0TPB5ArrayGbE* _M0L6activeS3016;
  int32_t _M0L6_2atmpS3015;
  struct _M0TP26RiantR8snn__mbt12PoissonFixed* _M0L5paramS3029;
  float _M0L4rateS3028;
  float _M0L6lambdaS819;
  struct _M0TPB5ArrayGiE* _M0L7_2abindS821;
  int32_t _M0L7_2abindS822;
  int32_t* _M0L7_2abindS823;
  int32_t _M0L2__S824;
  #line 111 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson.mbt"
  _M0L5paramS3017 = _M0L1sS818->$0;
  _M0L6activeS3016 = _M0L5paramS3017->$2;
  #line 116 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson.mbt"
  _M0L6_2atmpS3015 = _M0MPC15array5Array2atGbE(_M0L6activeS3016, 0);
  if (!_M0L6_2atmpS3015) {
    return 0;
  }
  _M0L5paramS3029 = _M0L1sS818->$0;
  _M0L4rateS3028 = _M0L5paramS3029->$0;
  _M0L6lambdaS819 = _M0L4rateS3028 * _M0L2dtS820;
  if (_M0L6lambdaS819 <= 0x0p+0f) {
    return 0;
  }
  _M0L7_2abindS821 = _M0L1sS818->$1;
  _M0L7_2abindS822 = _M0L7_2abindS821->$1;
  _M0L7_2abindS823 = _M0L7_2abindS821->$0;
  moonbit_incref(_M0L7_2abindS823);
  _M0L2__S824 = 0;
  while (1) {
    if (_M0L2__S824 < _M0L7_2abindS822) {
      int32_t _M0L1nS825 = (int32_t)_M0L7_2abindS823[_M0L2__S824];
      struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS3026 = _M0L1sS818->$3;
      int32_t _M0L1kS826;
      int32_t _M0L6_2atmpS3027;
      #line 124 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson.mbt"
      _M0L1kS826
      = _M0FP26RiantR8snn__mbt15sample__poisson(_M0L3rngS3026, _M0L6lambdaS819);
      if (_M0L1kS826 > 0) {
        struct _M0TPB5ArrayGfE* _M0L1gS3018 = _M0L1sS818->$2;
        struct _M0TPB5ArrayGfE* _M0L1gS3025 = _M0L1sS818->$2;
        float _M0L6_2atmpS3020;
        struct _M0TP26RiantR8snn__mbt12PoissonFixed* _M0L5paramS3024;
        float _M0L2muS3022;
        float _M0L6_2atmpS3023;
        float _M0L6_2atmpS3021;
        float _M0L6_2atmpS3019;
        #line 126 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson.mbt"
        _M0L6_2atmpS3020 = _M0MPC15array5Array2atGfE(_M0L1gS3025, _M0L1nS825);
        _M0L5paramS3024 = _M0L1sS818->$0;
        _M0L2muS3022 = _M0L5paramS3024->$1;
        _M0L6_2atmpS3023 = (float)_M0L1kS826;
        _M0L6_2atmpS3021 = _M0L2muS3022 * _M0L6_2atmpS3023;
        _M0L6_2atmpS3019 = _M0L6_2atmpS3020 + _M0L6_2atmpS3021;
        #line 126 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson.mbt"
        _M0MPC15array5Array3setGfE(_M0L1gS3018, _M0L1nS825, _M0L6_2atmpS3019);
      }
      _M0L6_2atmpS3027 = _M0L2__S824 + 1;
      _M0L2__S824 = _M0L6_2atmpS3027;
      continue;
    } else {
      moonbit_decref(_M0L7_2abindS823);
    }
    break;
  }
  return 0;
}

struct _M0TP26RiantR8snn__mbt17PoissonStimulusIF* _M0MP26RiantR8snn__mbt17PoissonStimulusIF3new(
  struct _M0TP26RiantR8snn__mbt2IF* _M0L3popS809,
  moonbit_string_t _M0L3symS815,
  float _M0L4rateS816,
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS817
) {
  int32_t _M0L1nS808;
  int32_t* _M0L6_2atmpS3014;
  struct _M0TPB5ArrayGiE* _M0L7neuronsS810;
  int32_t _M0L7_2abindS811;
  int32_t _M0L1kS812;
  struct _M0TPB5ArrayGfE* _M0L1gS814;
  struct _M0TP26RiantR8snn__mbt12PoissonFixed* _M0L6_2atmpS3013;
  struct _M0TP26RiantR8snn__mbt17PoissonStimulusIF* _block_5462;
  #line 92 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson.mbt"
  _M0L1nS808 = _M0L3popS809->$2;
  _M0L6_2atmpS3014 = (int32_t*)moonbit_empty_int32_array;
  _M0L7neuronsS810
  = (struct _M0TPB5ArrayGiE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGiE));
  Moonbit_object_header(_M0L7neuronsS810)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 24, 0);
  _M0L7neuronsS810->$0 = _M0L6_2atmpS3014;
  _M0L7neuronsS810->$1 = 0;
  _M0L7_2abindS811 = 0;
  _M0L1kS812 = _M0L7_2abindS811;
  while (1) {
    if (_M0L1kS812 < _M0L1nS808) {
      int32_t _M0L6_2atmpS3012;
      #line 101 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson.mbt"
      _M0MPC15array5Array4pushGiE(_M0L7neuronsS810, _M0L1kS812);
      _M0L6_2atmpS3012 = _M0L1kS812 + 1;
      _M0L1kS812 = _M0L6_2atmpS3012;
      continue;
    }
    break;
  }
  #line 103 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson.mbt"
  if (
    _M0L3symS815 == (moonbit_string_t)moonbit_string_literal_1.data
    || Moonbit_array_length(_M0L3symS815)
       == Moonbit_array_length((moonbit_string_t)moonbit_string_literal_1.data)
       && 0
          == memcmp(_M0L3symS815, (moonbit_string_t)moonbit_string_literal_1.data, Moonbit_array_length(_M0L3symS815) * 2)
  ) {
    struct _M0TPB5ArrayGfE* _M0L8_2afieldS5113 = _M0L3popS809->$13;
    moonbit_incref(_M0L8_2afieldS5113);
    _M0L1gS814 = _M0L8_2afieldS5113;
  } else {
    struct _M0TPB5ArrayGfE* _M0L8_2afieldS5114 = _M0L3popS809->$14;
    moonbit_incref(_M0L8_2afieldS5114);
    _M0L1gS814 = _M0L8_2afieldS5114;
  }
  #line 104 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson.mbt"
  _M0L6_2atmpS3013 = _M0MP26RiantR8snn__mbt12PoissonFixed3new(_M0L4rateS816);
  moonbit_incref(_M0L3rngS817);
  _block_5462
  = (struct _M0TP26RiantR8snn__mbt17PoissonStimulusIF*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt17PoissonStimulusIF));
  Moonbit_object_header(_block_5462)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 83, 0);
  _block_5462->$0 = _M0L6_2atmpS3013;
  _block_5462->$1 = _M0L7neuronsS810;
  _block_5462->$2 = _M0L1gS814;
  _block_5462->$3 = _M0L3rngS817;
  return _block_5462;
}

struct _M0TP26RiantR8snn__mbt12PoissonFixed* _M0MP26RiantR8snn__mbt12PoissonFixed3new(
  float _M0L4rateS807
) {
  uint8_t* _M0L6_2atmpS3011;
  struct _M0TPB5ArrayGbE* _M0L6_2atmpS3010;
  struct _M0TP26RiantR8snn__mbt12PoissonFixed* _block_5463;
  #line 23 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson.mbt"
  _M0L6_2atmpS3011 = (uint8_t*)moonbit_make_bytes_raw(1);
  _M0L6_2atmpS3011[0] = 1;
  _M0L6_2atmpS3010
  = (struct _M0TPB5ArrayGbE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGbE));
  Moonbit_object_header(_M0L6_2atmpS3010)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 73, 0);
  _M0L6_2atmpS3010->$0 = _M0L6_2atmpS3011;
  _M0L6_2atmpS3010->$1 = 1;
  _block_5463
  = (struct _M0TP26RiantR8snn__mbt12PoissonFixed*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt12PoissonFixed));
  Moonbit_object_header(_block_5463)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 89, 0);
  _block_5463->$0 = _M0L4rateS807;
  _block_5463->$1 = 0x1p+0f;
  _block_5463->$2 = _M0L6_2atmpS3010;
  return _block_5463;
}

int32_t _M0FP26RiantR8snn__mbt16stimulate__layer(
  struct _M0TP26RiantR8snn__mbt20PoissonLayerStimulus* _M0L1sS790,
  float _M0L4timeS788,
  float _M0L2dtS793
) {
  struct _M0TP26RiantR8snn__mbt12PoissonLayer* _M0L5paramS3009;
  int32_t _M0L6n__preS789;
  struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS3008;
  int32_t _M0L7n__postS791;
  struct _M0TP26RiantR8snn__mbt12PoissonLayer* _M0L5paramS3007;
  float _M0L4rateS3006;
  float _M0L6lambdaS792;
  int32_t _M0L7_2abindS794;
  int32_t _M0L1iS795;
  moonbit_string_t _M0L3symS3003;
  struct _M0TPB5ArrayGfE* _M0L9g__targetS797;
  int32_t _M0L7_2abindS798;
  int32_t _M0L1iS799;
  #line 147 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson_layer.mbt"
  _M0L5paramS3009 = _M0L1sS790->$0;
  _M0L6n__preS789 = _M0L5paramS3009->$1;
  _M0L4postS3008 = _M0L1sS790->$1;
  _M0L7n__postS791 = _M0L4postS3008->$2;
  _M0L5paramS3007 = _M0L1sS790->$0;
  _M0L4rateS3006 = _M0L5paramS3007->$0;
  _M0L6lambdaS792 = _M0L4rateS3006 * _M0L2dtS793;
  _M0L7_2abindS794 = 0;
  _M0L1iS795 = _M0L7_2abindS794;
  while (1) {
    if (_M0L1iS795 < _M0L6n__preS789) {
      struct _M0TPB5ArrayGbE* _M0L4fireS2988 = _M0L1sS790->$3;
      int32_t _M0L6_2atmpS2989;
      #line 158 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson_layer.mbt"
      _M0MPC15array5Array3setGbE(_M0L4fireS2988, _M0L1iS795, 0);
      _M0L6_2atmpS2989 = _M0L1iS795 + 1;
      _M0L1iS795 = _M0L6_2atmpS2989;
      continue;
    }
    break;
  }
  if (_M0L6lambdaS792 <= 0x0p+0f) {
    return 0;
  }
  _M0L3symS3003 = _M0L1sS790->$2;
  #line 163 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson_layer.mbt"
  if (
    _M0L3symS3003 == (moonbit_string_t)moonbit_string_literal_1.data
    || Moonbit_array_length(_M0L3symS3003)
       == Moonbit_array_length((moonbit_string_t)moonbit_string_literal_1.data)
       && 0
          == memcmp(_M0L3symS3003, (moonbit_string_t)moonbit_string_literal_1.data, Moonbit_array_length(_M0L3symS3003) * 2)
  ) {
    struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS3004 = _M0L1sS790->$1;
    struct _M0TPB5ArrayGfE* _M0L8_2afieldS5115 = _M0L4postS3004->$13;
    moonbit_incref(_M0L8_2afieldS5115);
    _M0L9g__targetS797 = _M0L8_2afieldS5115;
  } else {
    struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS3005 = _M0L1sS790->$1;
    struct _M0TPB5ArrayGfE* _M0L8_2afieldS5116 = _M0L4postS3005->$14;
    moonbit_incref(_M0L8_2afieldS5116);
    _M0L9g__targetS797 = _M0L8_2afieldS5116;
  }
  _M0L7_2abindS798 = 0;
  _M0L1iS799 = _M0L7_2abindS798;
  while (1) {
    if (_M0L1iS799 < _M0L6n__preS789) {
      struct _M0TP26RiantR8snn__mbt12PoissonLayer* _M0L5paramS2993 =
        _M0L1sS790->$0;
      struct _M0TPB5ArrayGbE* _M0L6activeS2992 = _M0L5paramS2993->$2;
      int32_t _M0L6_2atmpS2991;
      struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS3002;
      int32_t _M0L1kS802;
      int32_t _M0L6_2atmpS2990;
      moonbit_incref(_M0L6activeS2992);
      #line 165 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson_layer.mbt"
      _M0L6_2atmpS2991
      = _M0MPC15array5Array2atGbE(_M0L6activeS2992, _M0L1iS799);
      moonbit_decref(_M0L6activeS2992);
      if (!_M0L6_2atmpS2991) {
        goto join_800;
      }
      _M0L3rngS3002 = _M0L1sS790->$6;
      #line 168 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson_layer.mbt"
      _M0L1kS802
      = _M0FP26RiantR8snn__mbt15sample__poisson(_M0L3rngS3002, _M0L6lambdaS792);
      if (_M0L1kS802 > 0) {
        struct _M0TPB5ArrayGbE* _M0L4fireS2994 = _M0L1sS790->$3;
        int32_t _M0L7_2abindS803;
        int32_t _M0L1jS804;
        #line 170 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson_layer.mbt"
        _M0MPC15array5Array3setGbE(_M0L4fireS2994, _M0L1iS799, 1);
        _M0L7_2abindS803 = 0;
        _M0L1jS804 = _M0L7_2abindS803;
        while (1) {
          if (_M0L1jS804 < _M0L7n__postS791) {
            int32_t _M0L6_2atmpS3000 = _M0L1jS804 * _M0L6n__preS789;
            int32_t _M0L3idxS805 = _M0L6_2atmpS3000 + _M0L1iS799;
            struct _M0TPB5ArrayGbE* _M0L12connectivityS2995 = _M0L1sS790->$5;
            int32_t _M0L6_2atmpS3001;
            #line 173 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson_layer.mbt"
            if (
              _M0MPC15array5Array2atGbE(_M0L12connectivityS2995, _M0L3idxS805)
            ) {
              float _M0L6_2atmpS2997;
              struct _M0TPB5ArrayGfE* _M0L7weightsS2999;
              float _M0L6_2atmpS2998;
              float _M0L6_2atmpS2996;
              #line 174 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson_layer.mbt"
              _M0L6_2atmpS2997
              = _M0MPC15array5Array2atGfE(_M0L9g__targetS797, _M0L1jS804);
              _M0L7weightsS2999 = _M0L1sS790->$4;
              #line 174 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson_layer.mbt"
              _M0L6_2atmpS2998
              = _M0MPC15array5Array2atGfE(_M0L7weightsS2999, _M0L3idxS805);
              _M0L6_2atmpS2996 = _M0L6_2atmpS2997 + _M0L6_2atmpS2998;
              #line 174 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson_layer.mbt"
              _M0MPC15array5Array3setGfE(_M0L9g__targetS797, _M0L1jS804, _M0L6_2atmpS2996);
            }
            _M0L6_2atmpS3001 = _M0L1jS804 + 1;
            _M0L1jS804 = _M0L6_2atmpS3001;
            continue;
          }
          break;
        }
      }
      goto join_800;
      goto joinlet_5466;
      join_800:;
      _M0L6_2atmpS2990 = _M0L1iS799 + 1;
      _M0L1iS799 = _M0L6_2atmpS2990;
      continue;
      joinlet_5466:;
    } else {
      moonbit_decref(_M0L9g__targetS797);
    }
    break;
  }
  return 0;
}

struct _M0TUddE* _M0FP26RiantR8snn__mbt11box__muller(
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS783
) {
  double _M0L2u1S782;
  double _M0L8u1__safeS784;
  double _M0L2u2S785;
  double _M0L6_2atmpS2987;
  double _M0L6_2atmpS2986;
  double _M0L1rS786;
  double _M0L5thetaS787;
  double _M0L6_2atmpS2985;
  double _M0L6_2atmpS2982;
  double _M0L6_2atmpS2984;
  double _M0L6_2atmpS2983;
  struct _M0TUddE* _block_5468;
  #line 126 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  #line 127 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  _M0L2u1S782 = _M0FP26RiantR8snn__mbt9next__f64(_M0L3rngS783);
  if (_M0L2u1S782 < 0x1.203af9ee75616p-50) {
    _M0L8u1__safeS784 = 0x1.203af9ee75616p-50;
  } else {
    _M0L8u1__safeS784 = _M0L2u1S782;
  }
  #line 130 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  _M0L2u2S785 = _M0FP26RiantR8snn__mbt9next__f64(_M0L3rngS783);
  #line 131 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  _M0L6_2atmpS2987 = _M0FPC14math2ln(_M0L8u1__safeS784);
  _M0L6_2atmpS2986 = -0x1p+1 * _M0L6_2atmpS2987;
  #line 131 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  _M0L1rS786 = sqrt(_M0L6_2atmpS2986);
  _M0L5thetaS787 = 0x1.921fb54442d18p+2 * _M0L2u2S785;
  #line 133 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  _M0L6_2atmpS2985 = _M0FPC14math3cos(_M0L5thetaS787);
  _M0L6_2atmpS2982 = _M0L1rS786 * _M0L6_2atmpS2985;
  #line 133 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  _M0L6_2atmpS2984 = _M0FPC14math3sin(_M0L5thetaS787);
  _M0L6_2atmpS2983 = _M0L1rS786 * _M0L6_2atmpS2984;
  _block_5468 = (struct _M0TUddE*)moonbit_malloc(sizeof(struct _M0TUddE));
  Moonbit_object_header(_block_5468)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_5468->$0 = _M0L6_2atmpS2982;
  _block_5468->$1 = _M0L6_2atmpS2983;
  return _block_5468;
}

int32_t _M0FP26RiantR8snn__mbt15sample__poisson(
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS780,
  float _M0L6lambdaS774
) {
  float _M0L6_2atmpS2981;
  float _M0L6_2atmpS2980;
  double _M0L1lS775;
  struct _M0TPB8MutLocalGdE* _M0L1pS776;
  struct _M0TPB8MutLocalGiE* _M0L1kS777;
  float _M0L6_2atmpS2979;
  int32_t _M0L8ten__lamS779;
  int32_t _M0L3capS778;
  int32_t _M0L3valS2978;
  #line 55 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson.mbt"
  if (_M0L6lambdaS774 <= 0x0p+0f) {
    return 0;
  }
  _M0L6_2atmpS2981 = -_M0L6lambdaS774;
  #line 59 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson.mbt"
  _M0L6_2atmpS2980 = _M0FP26RiantR8snn__mbt4expf(_M0L6_2atmpS2981);
  _M0L1lS775 = (double)_M0L6_2atmpS2980;
  _M0L1pS776
  = (struct _M0TPB8MutLocalGdE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGdE));
  Moonbit_object_header(_M0L1pS776)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1pS776->$0 = 0x1p+0;
  _M0L1kS777
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1kS777)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1kS777->$0 = 0;
  _M0L6_2atmpS2979 = _M0L6lambdaS774 * 0x1.4p+3f;
  #line 63 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson.mbt"
  _M0L8ten__lamS779 = _M0MPC15float5Float7to__int(_M0L6_2atmpS2979);
  if (_M0L8ten__lamS779 > 100) {
    _M0L3capS778 = _M0L8ten__lamS779;
  } else {
    _M0L3capS778 = 100;
  }
  while (1) {
    int32_t _M0L3valS2970 = _M0L1kS777->$0;
    int32_t _M0L6_2atmpS2969 = _M0L3valS2970 + 1;
    double _M0L3valS2972;
    double _M0L6_2atmpS2973;
    double _M0L6_2atmpS2971;
    double _M0L3valS2974;
    int32_t _M0L3valS2976;
    _M0L1kS777->$0 = _M0L6_2atmpS2969;
    _M0L3valS2972 = _M0L1pS776->$0;
    #line 68 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson.mbt"
    _M0L6_2atmpS2973 = _M0FP26RiantR8snn__mbt9next__f64(_M0L3rngS780);
    _M0L6_2atmpS2971 = _M0L3valS2972 * _M0L6_2atmpS2973;
    _M0L1pS776->$0 = _M0L6_2atmpS2971;
    _M0L3valS2974 = _M0L1pS776->$0;
    if (_M0L3valS2974 < _M0L1lS775) {
      int32_t _M0L3valS2975;
      moonbit_decref(_M0L1pS776);
      _M0L3valS2975 = _M0L1kS777->$0;
      moonbit_decref(_M0L1kS777);
      return _M0L3valS2975 - 1;
    }
    _M0L3valS2976 = _M0L1kS777->$0;
    if (_M0L3valS2976 > _M0L3capS778) {
      int32_t _M0L3valS2977;
      moonbit_decref(_M0L1pS776);
      _M0L3valS2977 = _M0L1kS777->$0;
      moonbit_decref(_M0L1kS777);
      return _M0L3valS2977 - 1;
    }
    continue;
    break;
  }
  _M0L3valS2978 = _M0L1kS777->$0;
  moonbit_decref(_M0L1kS777);
  return _M0L3valS2978 - 1;
}

double _M0FP26RiantR8snn__mbt9next__f64(
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L1rS772
) {
  uint64_t _M0L1uS771;
  uint64_t _M0L4bitsS773;
  double _M0L6_2atmpS2968;
  #line 118 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  #line 119 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L1uS771 = _M0FP26RiantR8snn__mbt9next__u64(_M0L1rS772);
  _M0L4bitsS773 = _M0L1uS771 >> 11;
  _M0L6_2atmpS2968 = (double)_M0L4bitsS773;
  return _M0L6_2atmpS2968 * 0x1p-53;
}

int32_t _M0FP26RiantR8snn__mbt20stimulate__spiketime(
  struct _M0TP26RiantR8snn__mbt17SpikeTimeStimulus* _M0L1sS765,
  float _M0L1tS767,
  float _M0L1wS769
) {
  struct _M0TPB8MutLocalGiE* _M0L1iS764;
  #line 107 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
  _M0L1iS764
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1iS764)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1iS764->$0 = 0;
  while (1) {
    int32_t _M0L3valS2930 = _M0L1iS764->$0;
    int32_t _M0L1nS2931 = _M0L1sS765->$0;
    if (_M0L3valS2930 < _M0L1nS2931) {
      struct _M0TPB5ArrayGbE* _M0L4fireS2932 = _M0L1sS765->$4;
      int32_t _M0L3valS2933 = _M0L1iS764->$0;
      int32_t _M0L3valS2935;
      int32_t _M0L6_2atmpS2934;
      #line 115 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
      _M0MPC15array5Array3setGbE(_M0L4fireS2932, _M0L3valS2933, 0);
      _M0L3valS2935 = _M0L1iS764->$0;
      _M0L6_2atmpS2934 = _M0L3valS2935 + 1;
      _M0L1iS764->$0 = _M0L6_2atmpS2934;
      continue;
    } else {
      moonbit_decref(_M0L1iS764);
    }
    break;
  }
  while (1) {
    struct _M0TPB5ArrayGiE* _M0L11next__indexS2939 = _M0L1sS765->$3;
    int32_t _M0L6_2atmpS2938;
    int32_t _if__result_5472;
    #line 119 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
    _M0L6_2atmpS2938 = _M0MPC15array5Array2atGiE(_M0L11next__indexS2939, 0);
    if (_M0L6_2atmpS2938 >= 0) {
      struct _M0TPB5ArrayGfE* _M0L11next__spikeS2937 = _M0L1sS765->$2;
      float _M0L6_2atmpS2936;
      #line 119 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
      _M0L6_2atmpS2936 = _M0MPC15array5Array2atGfE(_M0L11next__spikeS2937, 0);
      _if__result_5472 = _M0L6_2atmpS2936 <= _M0L1tS767;
    } else {
      _if__result_5472 = 0;
    }
    if (_if__result_5472) {
      struct _M0TP26RiantR8snn__mbt18SpikeTimeParameter* _M0L5paramS2967 =
        _M0L1sS765->$1;
      struct _M0TPB5ArrayGiE* _M0L7neuronsS2964 = _M0L5paramS2967->$1;
      struct _M0TPB5ArrayGiE* _M0L11next__indexS2966 = _M0L1sS765->$3;
      int32_t _M0L6_2atmpS2965;
      int32_t _M0L1jS768;
      struct _M0TPB5ArrayGbE* _M0L4fireS2940;
      struct _M0TPB5ArrayGfE* _M0L1gS2941;
      struct _M0TPB5ArrayGfE* _M0L1gS2944;
      float _M0L6_2atmpS2943;
      float _M0L6_2atmpS2942;
      struct _M0TPB5ArrayGiE* _M0L11next__indexS2950;
      int32_t _M0L6_2atmpS2949;
      int32_t _M0L6_2atmpS2945;
      struct _M0TP26RiantR8snn__mbt18SpikeTimeParameter* _M0L5paramS2948;
      struct _M0TPB5ArrayGfE* _M0L10spiketimesS2947;
      int32_t _M0L6_2atmpS2946;
      #line 120 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
      _M0L6_2atmpS2965 = _M0MPC15array5Array2atGiE(_M0L11next__indexS2966, 0);
      #line 120 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
      _M0L1jS768
      = _M0MPC15array5Array2atGiE(_M0L7neuronsS2964, _M0L6_2atmpS2965);
      _M0L4fireS2940 = _M0L1sS765->$4;
      #line 121 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
      _M0MPC15array5Array3setGbE(_M0L4fireS2940, _M0L1jS768, 1);
      _M0L1gS2941 = _M0L1sS765->$5;
      _M0L1gS2944 = _M0L1sS765->$5;
      #line 122 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
      _M0L6_2atmpS2943 = _M0MPC15array5Array2atGfE(_M0L1gS2944, _M0L1jS768);
      _M0L6_2atmpS2942 = _M0L6_2atmpS2943 + _M0L1wS769;
      #line 122 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
      _M0MPC15array5Array3setGfE(_M0L1gS2941, _M0L1jS768, _M0L6_2atmpS2942);
      _M0L11next__indexS2950 = _M0L1sS765->$3;
      #line 124 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
      _M0L6_2atmpS2949 = _M0MPC15array5Array2atGiE(_M0L11next__indexS2950, 0);
      _M0L6_2atmpS2945 = _M0L6_2atmpS2949 + 1;
      _M0L5paramS2948 = _M0L1sS765->$1;
      _M0L10spiketimesS2947 = _M0L5paramS2948->$0;
      #line 124 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
      _M0L6_2atmpS2946 = _M0MPC15array5Array6lengthGfE(_M0L10spiketimesS2947);
      if (_M0L6_2atmpS2945 < _M0L6_2atmpS2946) {
        struct _M0TPB5ArrayGiE* _M0L11next__indexS2951 = _M0L1sS765->$3;
        struct _M0TPB5ArrayGiE* _M0L11next__indexS2954 = _M0L1sS765->$3;
        int32_t _M0L6_2atmpS2953;
        int32_t _M0L6_2atmpS2952;
        struct _M0TPB5ArrayGfE* _M0L11next__spikeS2955;
        struct _M0TP26RiantR8snn__mbt18SpikeTimeParameter* _M0L5paramS2960;
        struct _M0TPB5ArrayGfE* _M0L10spiketimesS2957;
        struct _M0TPB5ArrayGiE* _M0L11next__indexS2959;
        int32_t _M0L6_2atmpS2958;
        float _M0L6_2atmpS2956;
        #line 125 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
        _M0L6_2atmpS2953
        = _M0MPC15array5Array2atGiE(_M0L11next__indexS2954, 0);
        _M0L6_2atmpS2952 = _M0L6_2atmpS2953 + 1;
        #line 125 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
        _M0MPC15array5Array3setGiE(_M0L11next__indexS2951, 0, _M0L6_2atmpS2952);
        _M0L11next__spikeS2955 = _M0L1sS765->$2;
        _M0L5paramS2960 = _M0L1sS765->$1;
        _M0L10spiketimesS2957 = _M0L5paramS2960->$0;
        _M0L11next__indexS2959 = _M0L1sS765->$3;
        #line 126 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
        _M0L6_2atmpS2958
        = _M0MPC15array5Array2atGiE(_M0L11next__indexS2959, 0);
        #line 126 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
        _M0L6_2atmpS2956
        = _M0MPC15array5Array2atGfE(_M0L10spiketimesS2957, _M0L6_2atmpS2958);
        #line 126 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
        _M0MPC15array5Array3setGfE(_M0L11next__spikeS2955, 0, _M0L6_2atmpS2956);
      } else {
        struct _M0TPB5ArrayGfE* _M0L11next__spikeS2961 = _M0L1sS765->$2;
        float _M0L6_2atmpS2962 = 0x0p+0f / (float)MOONBIT_ZERO;
        struct _M0TPB5ArrayGiE* _M0L11next__indexS2963;
        #line 128 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
        _M0MPC15array5Array3setGfE(_M0L11next__spikeS2961, 0, _M0L6_2atmpS2962);
        _M0L11next__indexS2963 = _M0L1sS765->$3;
        #line 129 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
        _M0MPC15array5Array3setGiE(_M0L11next__indexS2963, 0, -1);
      }
      continue;
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt28markram__stp__step__timestep(
  struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0L3synS751,
  struct _M0TP26RiantR8snn__mbt19MarkramSTPVariables* _M0L4varsS749,
  struct _M0TP26RiantR8snn__mbt27MarkramSTPParameterTimestep* _M0L5paramS753,
  float _M0L6t__nowS748,
  float _M0L2dtS758
) {
  struct _M0TPB5ArrayGbE* _M0L6activeS2843;
  int32_t _M0L6_2atmpS2842;
  int32_t _if__result_5473;
  int32_t _M0L6n__preS750;
  struct _M0TPB5ArrayGfE* _M0L3rhoS2845;
  int32_t _M0L6_2atmpS2844;
  float _M0L11u__baselineS752;
  float _M0L6tau__fS2929;
  float _M0L11inv__tau__fS754;
  float _M0L6tau__dS2928;
  float _M0L11inv__tau__dS755;
  struct _M0TPB8MutLocalGiE* _M0L1jS756;
  #line 473 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
  _M0L6activeS2843 = _M0L4varsS749->$6;
  #line 482 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
  _M0L6_2atmpS2842 = _M0MPC15array5Array6lengthGbE(_M0L6activeS2843);
  if (_M0L6_2atmpS2842 > 0) {
    struct _M0TPB5ArrayGbE* _M0L6activeS2841 = _M0L4varsS749->$6;
    int32_t _M0L6_2atmpS2840;
    #line 482 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
    _M0L6_2atmpS2840 = _M0MPC15array5Array2atGbE(_M0L6activeS2841, 0);
    _if__result_5473 = !_M0L6_2atmpS2840;
  } else {
    _if__result_5473 = 0;
  }
  if (_if__result_5473) {
    return 0;
  }
  _M0L6n__preS750 = _M0L4varsS749->$0;
  _M0L3rhoS2845 = _M0L3synS751->$6;
  #line 487 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
  _M0L6_2atmpS2844 = _M0MPC15array5Array6lengthGfE(_M0L3rhoS2845);
  if (_M0L6_2atmpS2844 == 0) {
    return 0;
  }
  _M0L11u__baselineS752 = _M0L5paramS753->$0;
  _M0L6tau__fS2929 = _M0L5paramS753->$1;
  _M0L11inv__tau__fS754 = 0x1p+0f / _M0L6tau__fS2929;
  _M0L6tau__dS2928 = _M0L5paramS753->$2;
  _M0L11inv__tau__dS755 = 0x1p+0f / _M0L6tau__dS2928;
  _M0L1jS756
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1jS756)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1jS756->$0 = 0;
  while (1) {
    int32_t _M0L3valS2846 = _M0L1jS756->$0;
    if (_M0L3valS2846 < _M0L6n__preS750) {
      struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS2849 = _M0L3synS751->$0;
      struct _M0TPB5ArrayGbE* _M0L4fireS2847 = _M0L3preS2849->$5;
      int32_t _M0L3valS2848 = _M0L1jS756->$0;
      int32_t _M0L3valS2876;
      int32_t _M0L6_2atmpS2875;
      #line 496 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS2847, _M0L3valS2848)) {
        struct _M0TPB5ArrayGfE* _M0L1uS2850 = _M0L4varsS749->$2;
        int32_t _M0L3valS2851 = _M0L1jS756->$0;
        struct _M0TPB5ArrayGfE* _M0L1uS2859 = _M0L4varsS749->$2;
        int32_t _M0L3valS2860 = _M0L1jS756->$0;
        float _M0L6_2atmpS2853;
        struct _M0TPB5ArrayGfE* _M0L1uS2857;
        int32_t _M0L3valS2858;
        float _M0L6_2atmpS2856;
        float _M0L6_2atmpS2855;
        float _M0L6_2atmpS2854;
        float _M0L6_2atmpS2852;
        struct _M0TPB5ArrayGfE* _M0L1xS2861;
        int32_t _M0L3valS2862;
        struct _M0TPB5ArrayGfE* _M0L1xS2873;
        int32_t _M0L3valS2874;
        float _M0L6_2atmpS2864;
        struct _M0TPB5ArrayGfE* _M0L1uS2871;
        int32_t _M0L3valS2872;
        float _M0L6_2atmpS2870;
        float _M0L6_2atmpS2866;
        struct _M0TPB5ArrayGfE* _M0L1xS2868;
        int32_t _M0L3valS2869;
        float _M0L6_2atmpS2867;
        float _M0L6_2atmpS2865;
        float _M0L6_2atmpS2863;
        #line 497 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS2853
        = _M0MPC15array5Array2atGfE(_M0L1uS2859, _M0L3valS2860);
        _M0L1uS2857 = _M0L4varsS749->$2;
        _M0L3valS2858 = _M0L1jS756->$0;
        #line 497 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS2856
        = _M0MPC15array5Array2atGfE(_M0L1uS2857, _M0L3valS2858);
        _M0L6_2atmpS2855 = 0x1p+0f - _M0L6_2atmpS2856;
        _M0L6_2atmpS2854 = _M0L11u__baselineS752 * _M0L6_2atmpS2855;
        _M0L6_2atmpS2852 = _M0L6_2atmpS2853 + _M0L6_2atmpS2854;
        #line 497 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0MPC15array5Array3setGfE(_M0L1uS2850, _M0L3valS2851, _M0L6_2atmpS2852);
        _M0L1xS2861 = _M0L4varsS749->$3;
        _M0L3valS2862 = _M0L1jS756->$0;
        _M0L1xS2873 = _M0L4varsS749->$3;
        _M0L3valS2874 = _M0L1jS756->$0;
        #line 498 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS2864
        = _M0MPC15array5Array2atGfE(_M0L1xS2873, _M0L3valS2874);
        _M0L1uS2871 = _M0L4varsS749->$2;
        _M0L3valS2872 = _M0L1jS756->$0;
        #line 498 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS2870
        = _M0MPC15array5Array2atGfE(_M0L1uS2871, _M0L3valS2872);
        _M0L6_2atmpS2866 = -_M0L6_2atmpS2870;
        _M0L1xS2868 = _M0L4varsS749->$3;
        _M0L3valS2869 = _M0L1jS756->$0;
        #line 498 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS2867
        = _M0MPC15array5Array2atGfE(_M0L1xS2868, _M0L3valS2869);
        _M0L6_2atmpS2865 = _M0L6_2atmpS2866 * _M0L6_2atmpS2867;
        _M0L6_2atmpS2863 = _M0L6_2atmpS2864 + _M0L6_2atmpS2865;
        #line 498 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0MPC15array5Array3setGfE(_M0L1xS2861, _M0L3valS2862, _M0L6_2atmpS2863);
      }
      _M0L3valS2876 = _M0L1jS756->$0;
      _M0L6_2atmpS2875 = _M0L3valS2876 + 1;
      _M0L1jS756->$0 = _M0L6_2atmpS2875;
      continue;
    }
    break;
  }
  _M0L1jS756->$0 = 0;
  while (1) {
    int32_t _M0L3valS2877 = _M0L1jS756->$0;
    if (_M0L3valS2877 < _M0L6n__preS750) {
      struct _M0TPB5ArrayGfE* _M0L1uS2878 = _M0L4varsS749->$2;
      int32_t _M0L3valS2879 = _M0L1jS756->$0;
      struct _M0TPB5ArrayGfE* _M0L1uS2888 = _M0L4varsS749->$2;
      int32_t _M0L3valS2889 = _M0L1jS756->$0;
      float _M0L6_2atmpS2881;
      struct _M0TPB5ArrayGfE* _M0L1uS2886;
      int32_t _M0L3valS2887;
      float _M0L6_2atmpS2885;
      float _M0L6_2atmpS2884;
      float _M0L6_2atmpS2883;
      float _M0L6_2atmpS2882;
      float _M0L6_2atmpS2880;
      struct _M0TPB5ArrayGfE* _M0L1xS2890;
      int32_t _M0L3valS2891;
      struct _M0TPB5ArrayGfE* _M0L1xS2900;
      int32_t _M0L3valS2901;
      float _M0L6_2atmpS2893;
      struct _M0TPB5ArrayGfE* _M0L1xS2898;
      int32_t _M0L3valS2899;
      float _M0L6_2atmpS2897;
      float _M0L6_2atmpS2896;
      float _M0L6_2atmpS2895;
      float _M0L6_2atmpS2894;
      float _M0L6_2atmpS2892;
      struct _M0TPB5ArrayGfE* _M0L8rho__preS2902;
      int32_t _M0L3valS2903;
      struct _M0TPB5ArrayGfE* _M0L1uS2909;
      int32_t _M0L3valS2910;
      float _M0L6_2atmpS2905;
      struct _M0TPB5ArrayGfE* _M0L1xS2907;
      int32_t _M0L3valS2908;
      float _M0L6_2atmpS2906;
      float _M0L6_2atmpS2904;
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS2927;
      struct _M0TPB5ArrayGiE* _M0L6rowptrS2925;
      int32_t _M0L3valS2926;
      int32_t _M0L5startS759;
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS2924;
      struct _M0TPB5ArrayGiE* _M0L6rowptrS2921;
      int32_t _M0L3valS2923;
      int32_t _M0L6_2atmpS2922;
      int32_t _M0L3endS760;
      struct _M0TPB8MutLocalGiE* _M0L1sS761;
      int32_t _M0L3valS2920;
      int32_t _M0L6_2atmpS2919;
      #line 505 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
      _M0L6_2atmpS2881
      = _M0MPC15array5Array2atGfE(_M0L1uS2888, _M0L3valS2889);
      _M0L1uS2886 = _M0L4varsS749->$2;
      _M0L3valS2887 = _M0L1jS756->$0;
      #line 505 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
      _M0L6_2atmpS2885
      = _M0MPC15array5Array2atGfE(_M0L1uS2886, _M0L3valS2887);
      _M0L6_2atmpS2884 = _M0L11u__baselineS752 - _M0L6_2atmpS2885;
      _M0L6_2atmpS2883 = _M0L2dtS758 * _M0L6_2atmpS2884;
      _M0L6_2atmpS2882 = _M0L6_2atmpS2883 * _M0L11inv__tau__fS754;
      _M0L6_2atmpS2880 = _M0L6_2atmpS2881 + _M0L6_2atmpS2882;
      #line 505 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
      _M0MPC15array5Array3setGfE(_M0L1uS2878, _M0L3valS2879, _M0L6_2atmpS2880);
      _M0L1xS2890 = _M0L4varsS749->$3;
      _M0L3valS2891 = _M0L1jS756->$0;
      _M0L1xS2900 = _M0L4varsS749->$3;
      _M0L3valS2901 = _M0L1jS756->$0;
      #line 506 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
      _M0L6_2atmpS2893
      = _M0MPC15array5Array2atGfE(_M0L1xS2900, _M0L3valS2901);
      _M0L1xS2898 = _M0L4varsS749->$3;
      _M0L3valS2899 = _M0L1jS756->$0;
      #line 506 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
      _M0L6_2atmpS2897
      = _M0MPC15array5Array2atGfE(_M0L1xS2898, _M0L3valS2899);
      _M0L6_2atmpS2896 = 0x1p+0f - _M0L6_2atmpS2897;
      _M0L6_2atmpS2895 = _M0L2dtS758 * _M0L6_2atmpS2896;
      _M0L6_2atmpS2894 = _M0L6_2atmpS2895 * _M0L11inv__tau__dS755;
      _M0L6_2atmpS2892 = _M0L6_2atmpS2893 + _M0L6_2atmpS2894;
      #line 506 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
      _M0MPC15array5Array3setGfE(_M0L1xS2890, _M0L3valS2891, _M0L6_2atmpS2892);
      _M0L8rho__preS2902 = _M0L4varsS749->$4;
      _M0L3valS2903 = _M0L1jS756->$0;
      _M0L1uS2909 = _M0L4varsS749->$2;
      _M0L3valS2910 = _M0L1jS756->$0;
      #line 507 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
      _M0L6_2atmpS2905
      = _M0MPC15array5Array2atGfE(_M0L1uS2909, _M0L3valS2910);
      _M0L1xS2907 = _M0L4varsS749->$3;
      _M0L3valS2908 = _M0L1jS756->$0;
      #line 507 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
      _M0L6_2atmpS2906
      = _M0MPC15array5Array2atGfE(_M0L1xS2907, _M0L3valS2908);
      _M0L6_2atmpS2904 = _M0L6_2atmpS2905 * _M0L6_2atmpS2906;
      #line 507 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
      _M0MPC15array5Array3setGfE(_M0L8rho__preS2902, _M0L3valS2903, _M0L6_2atmpS2904);
      _M0L6matrixS2927 = _M0L3synS751->$4;
      _M0L6rowptrS2925 = _M0L6matrixS2927->$2;
      _M0L3valS2926 = _M0L1jS756->$0;
      #line 509 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
      _M0L5startS759
      = _M0MPC15array5Array2atGiE(_M0L6rowptrS2925, _M0L3valS2926);
      _M0L6matrixS2924 = _M0L3synS751->$4;
      _M0L6rowptrS2921 = _M0L6matrixS2924->$2;
      _M0L3valS2923 = _M0L1jS756->$0;
      _M0L6_2atmpS2922 = _M0L3valS2923 + 1;
      #line 510 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
      _M0L3endS760
      = _M0MPC15array5Array2atGiE(_M0L6rowptrS2921, _M0L6_2atmpS2922);
      _M0L1sS761
      = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
      Moonbit_object_header(_M0L1sS761)->meta
      = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
      _M0L1sS761->$0 = _M0L5startS759;
      while (1) {
        int32_t _M0L3valS2911 = _M0L1sS761->$0;
        if (_M0L3valS2911 < _M0L3endS760) {
          struct _M0TPB5ArrayGfE* _M0L3rhoS2912 = _M0L3synS751->$6;
          int32_t _M0L3valS2913 = _M0L1sS761->$0;
          struct _M0TPB5ArrayGfE* _M0L8rho__preS2915 = _M0L4varsS749->$4;
          int32_t _M0L3valS2916 = _M0L1jS756->$0;
          float _M0L6_2atmpS2914;
          int32_t _M0L3valS2918;
          int32_t _M0L6_2atmpS2917;
          #line 513 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
          _M0L6_2atmpS2914
          = _M0MPC15array5Array2atGfE(_M0L8rho__preS2915, _M0L3valS2916);
          #line 513 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
          _M0MPC15array5Array3setGfE(_M0L3rhoS2912, _M0L3valS2913, _M0L6_2atmpS2914);
          _M0L3valS2918 = _M0L1sS761->$0;
          _M0L6_2atmpS2917 = _M0L3valS2918 + 1;
          _M0L1sS761->$0 = _M0L6_2atmpS2917;
          continue;
        } else {
          moonbit_decref(_M0L1sS761);
        }
        break;
      }
      _M0L3valS2920 = _M0L1jS756->$0;
      _M0L6_2atmpS2919 = _M0L3valS2920 + 1;
      _M0L1jS756->$0 = _M0L6_2atmpS2919;
      continue;
    } else {
      moonbit_decref(_M0L1jS756);
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt23markram__stp__step__het(
  struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0L3synS732,
  struct _M0TP26RiantR8snn__mbt19MarkramSTPVariables* _M0L4varsS730,
  struct _M0TP26RiantR8snn__mbt22MarkramSTPParameterHet* _M0L5paramS735,
  float _M0L6t__nowS739
) {
  struct _M0TPB5ArrayGbE* _M0L6activeS2751;
  int32_t _M0L6_2atmpS2750;
  int32_t _if__result_5477;
  int32_t _M0L6n__preS731;
  struct _M0TPB5ArrayGfE* _M0L3rhoS2753;
  int32_t _M0L6_2atmpS2752;
  struct _M0TPB8MutLocalGiE* _M0L1jS733;
  #line 347 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
  _M0L6activeS2751 = _M0L4varsS730->$6;
  #line 353 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
  _M0L6_2atmpS2750 = _M0MPC15array5Array6lengthGbE(_M0L6activeS2751);
  if (_M0L6_2atmpS2750 > 0) {
    struct _M0TPB5ArrayGbE* _M0L6activeS2749 = _M0L4varsS730->$6;
    int32_t _M0L6_2atmpS2748;
    #line 353 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
    _M0L6_2atmpS2748 = _M0MPC15array5Array2atGbE(_M0L6activeS2749, 0);
    _if__result_5477 = !_M0L6_2atmpS2748;
  } else {
    _if__result_5477 = 0;
  }
  if (_if__result_5477) {
    return 0;
  }
  _M0L6n__preS731 = _M0L4varsS730->$0;
  _M0L3rhoS2753 = _M0L3synS732->$6;
  #line 357 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
  _M0L6_2atmpS2752 = _M0MPC15array5Array6lengthGfE(_M0L3rhoS2753);
  if (_M0L6_2atmpS2752 == 0) {
    return 0;
  }
  _M0L1jS733
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1jS733)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1jS733->$0 = 0;
  while (1) {
    int32_t _M0L3valS2754 = _M0L1jS733->$0;
    if (_M0L3valS2754 < _M0L6n__preS731) {
      struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS2757 = _M0L3synS732->$0;
      struct _M0TPB5ArrayGbE* _M0L4fireS2755 = _M0L3preS2757->$5;
      int32_t _M0L3valS2756 = _M0L1jS733->$0;
      int32_t _M0L3valS2839;
      int32_t _M0L6_2atmpS2838;
      #line 362 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS2755, _M0L3valS2756)) {
        struct _M0TPB5ArrayGfE* _M0L6tau__dS2836 = _M0L5paramS735->$0;
        int32_t _M0L3valS2837 = _M0L1jS733->$0;
        float _M0L9tau__d__jS734;
        struct _M0TPB5ArrayGfE* _M0L6tau__fS2834;
        int32_t _M0L3valS2835;
        float _M0L9tau__f__jS736;
        struct _M0TPB5ArrayGfE* _M0L1uS2832;
        int32_t _M0L3valS2833;
        float _M0L14u__baseline__jS737;
        struct _M0TPB5ArrayGfE* _M0L11last__spikeS2830;
        int32_t _M0L3valS2831;
        float _M0L6_2atmpS2829;
        float _M0L7dt__preS738;
        float _M0L7dt__preS740;
        struct _M0TPB5ArrayGfE* _M0L11last__spikeS2758;
        int32_t _M0L3valS2759;
        float _M0L6_2atmpS2828;
        float _M0L6arg__fS741;
        struct _M0TPB5ArrayGfE* _M0L1uS2760;
        int32_t _M0L3valS2761;
        struct _M0TPB5ArrayGfE* _M0L1uS2767;
        int32_t _M0L3valS2768;
        float _M0L6_2atmpS2766;
        float _M0L6_2atmpS2764;
        float _M0L6_2atmpS2765;
        float _M0L6_2atmpS2763;
        float _M0L6_2atmpS2762;
        float _M0L6_2atmpS2827;
        float _M0L6arg__dS742;
        struct _M0TPB5ArrayGfE* _M0L1xS2769;
        int32_t _M0L3valS2770;
        struct _M0TPB5ArrayGfE* _M0L1xS2776;
        int32_t _M0L3valS2777;
        float _M0L6_2atmpS2775;
        float _M0L6_2atmpS2773;
        float _M0L6_2atmpS2774;
        float _M0L6_2atmpS2772;
        float _M0L6_2atmpS2771;
        struct _M0TPB5ArrayGfE* _M0L8rho__preS2778;
        int32_t _M0L3valS2779;
        struct _M0TPB5ArrayGfE* _M0L1uS2785;
        int32_t _M0L3valS2786;
        float _M0L6_2atmpS2781;
        struct _M0TPB5ArrayGfE* _M0L1xS2783;
        int32_t _M0L3valS2784;
        float _M0L6_2atmpS2782;
        float _M0L6_2atmpS2780;
        struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS2826;
        struct _M0TPB5ArrayGiE* _M0L6rowptrS2824;
        int32_t _M0L3valS2825;
        int32_t _M0L5startS743;
        struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS2823;
        struct _M0TPB5ArrayGiE* _M0L6rowptrS2820;
        int32_t _M0L3valS2822;
        int32_t _M0L6_2atmpS2821;
        int32_t _M0L3endS744;
        struct _M0TPB8MutLocalGiE* _M0L1sS745;
        struct _M0TPB5ArrayGfE* _M0L1uS2795;
        int32_t _M0L3valS2796;
        struct _M0TPB5ArrayGfE* _M0L1uS2804;
        int32_t _M0L3valS2805;
        float _M0L6_2atmpS2798;
        struct _M0TPB5ArrayGfE* _M0L1uS2802;
        int32_t _M0L3valS2803;
        float _M0L6_2atmpS2801;
        float _M0L6_2atmpS2800;
        float _M0L6_2atmpS2799;
        float _M0L6_2atmpS2797;
        struct _M0TPB5ArrayGfE* _M0L1xS2806;
        int32_t _M0L3valS2807;
        struct _M0TPB5ArrayGfE* _M0L1xS2818;
        int32_t _M0L3valS2819;
        float _M0L6_2atmpS2809;
        struct _M0TPB5ArrayGfE* _M0L1uS2816;
        int32_t _M0L3valS2817;
        float _M0L6_2atmpS2815;
        float _M0L6_2atmpS2811;
        struct _M0TPB5ArrayGfE* _M0L1xS2813;
        int32_t _M0L3valS2814;
        float _M0L6_2atmpS2812;
        float _M0L6_2atmpS2810;
        float _M0L6_2atmpS2808;
        #line 363 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L9tau__d__jS734
        = _M0MPC15array5Array2atGfE(_M0L6tau__dS2836, _M0L3valS2837);
        _M0L6tau__fS2834 = _M0L5paramS735->$1;
        _M0L3valS2835 = _M0L1jS733->$0;
        #line 364 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L9tau__f__jS736
        = _M0MPC15array5Array2atGfE(_M0L6tau__fS2834, _M0L3valS2835);
        _M0L1uS2832 = _M0L5paramS735->$2;
        _M0L3valS2833 = _M0L1jS733->$0;
        #line 365 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L14u__baseline__jS737
        = _M0MPC15array5Array2atGfE(_M0L1uS2832, _M0L3valS2833);
        _M0L11last__spikeS2830 = _M0L4varsS730->$5;
        _M0L3valS2831 = _M0L1jS733->$0;
        #line 366 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS2829
        = _M0MPC15array5Array2atGfE(_M0L11last__spikeS2830, _M0L3valS2831);
        _M0L7dt__preS738 = _M0L6t__nowS739 - _M0L6_2atmpS2829;
        if (_M0L7dt__preS738 < 0x0p+0f) {
          _M0L7dt__preS740 = 0x0p+0f;
        } else {
          _M0L7dt__preS740 = _M0L7dt__preS738;
        }
        _M0L11last__spikeS2758 = _M0L4varsS730->$5;
        _M0L3valS2759 = _M0L1jS733->$0;
        #line 368 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0MPC15array5Array3setGfE(_M0L11last__spikeS2758, _M0L3valS2759, _M0L6t__nowS739);
        _M0L6_2atmpS2828 = -_M0L7dt__preS740;
        _M0L6arg__fS741 = _M0L6_2atmpS2828 / _M0L9tau__f__jS736;
        _M0L1uS2760 = _M0L4varsS730->$2;
        _M0L3valS2761 = _M0L1jS733->$0;
        _M0L1uS2767 = _M0L4varsS730->$2;
        _M0L3valS2768 = _M0L1jS733->$0;
        #line 370 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS2766
        = _M0MPC15array5Array2atGfE(_M0L1uS2767, _M0L3valS2768);
        _M0L6_2atmpS2764 = _M0L14u__baseline__jS737 - _M0L6_2atmpS2766;
        #line 370 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS2765 = _M0FP26RiantR8snn__mbt4expf(_M0L6arg__fS741);
        _M0L6_2atmpS2763 = _M0L6_2atmpS2764 * _M0L6_2atmpS2765;
        _M0L6_2atmpS2762 = _M0L14u__baseline__jS737 - _M0L6_2atmpS2763;
        #line 370 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0MPC15array5Array3setGfE(_M0L1uS2760, _M0L3valS2761, _M0L6_2atmpS2762);
        _M0L6_2atmpS2827 = -_M0L7dt__preS740;
        _M0L6arg__dS742 = _M0L6_2atmpS2827 / _M0L9tau__d__jS734;
        _M0L1xS2769 = _M0L4varsS730->$3;
        _M0L3valS2770 = _M0L1jS733->$0;
        _M0L1xS2776 = _M0L4varsS730->$3;
        _M0L3valS2777 = _M0L1jS733->$0;
        #line 372 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS2775
        = _M0MPC15array5Array2atGfE(_M0L1xS2776, _M0L3valS2777);
        _M0L6_2atmpS2773 = 0x1p+0f - _M0L6_2atmpS2775;
        #line 372 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS2774 = _M0FP26RiantR8snn__mbt4expf(_M0L6arg__dS742);
        _M0L6_2atmpS2772 = _M0L6_2atmpS2773 * _M0L6_2atmpS2774;
        _M0L6_2atmpS2771 = 0x1p+0f - _M0L6_2atmpS2772;
        #line 372 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0MPC15array5Array3setGfE(_M0L1xS2769, _M0L3valS2770, _M0L6_2atmpS2771);
        _M0L8rho__preS2778 = _M0L4varsS730->$4;
        _M0L3valS2779 = _M0L1jS733->$0;
        _M0L1uS2785 = _M0L4varsS730->$2;
        _M0L3valS2786 = _M0L1jS733->$0;
        #line 373 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS2781
        = _M0MPC15array5Array2atGfE(_M0L1uS2785, _M0L3valS2786);
        _M0L1xS2783 = _M0L4varsS730->$3;
        _M0L3valS2784 = _M0L1jS733->$0;
        #line 373 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS2782
        = _M0MPC15array5Array2atGfE(_M0L1xS2783, _M0L3valS2784);
        _M0L6_2atmpS2780 = _M0L6_2atmpS2781 * _M0L6_2atmpS2782;
        #line 373 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0MPC15array5Array3setGfE(_M0L8rho__preS2778, _M0L3valS2779, _M0L6_2atmpS2780);
        _M0L6matrixS2826 = _M0L3synS732->$4;
        _M0L6rowptrS2824 = _M0L6matrixS2826->$2;
        _M0L3valS2825 = _M0L1jS733->$0;
        #line 374 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L5startS743
        = _M0MPC15array5Array2atGiE(_M0L6rowptrS2824, _M0L3valS2825);
        _M0L6matrixS2823 = _M0L3synS732->$4;
        _M0L6rowptrS2820 = _M0L6matrixS2823->$2;
        _M0L3valS2822 = _M0L1jS733->$0;
        _M0L6_2atmpS2821 = _M0L3valS2822 + 1;
        #line 375 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L3endS744
        = _M0MPC15array5Array2atGiE(_M0L6rowptrS2820, _M0L6_2atmpS2821);
        _M0L1sS745
        = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
        Moonbit_object_header(_M0L1sS745)->meta
        = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
        _M0L1sS745->$0 = _M0L5startS743;
        while (1) {
          int32_t _M0L3valS2787 = _M0L1sS745->$0;
          if (_M0L3valS2787 < _M0L3endS744) {
            struct _M0TPB5ArrayGfE* _M0L3rhoS2788 = _M0L3synS732->$6;
            int32_t _M0L3valS2789 = _M0L1sS745->$0;
            struct _M0TPB5ArrayGfE* _M0L8rho__preS2791 = _M0L4varsS730->$4;
            int32_t _M0L3valS2792 = _M0L1jS733->$0;
            float _M0L6_2atmpS2790;
            int32_t _M0L3valS2794;
            int32_t _M0L6_2atmpS2793;
            #line 378 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
            _M0L6_2atmpS2790
            = _M0MPC15array5Array2atGfE(_M0L8rho__preS2791, _M0L3valS2792);
            #line 378 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
            _M0MPC15array5Array3setGfE(_M0L3rhoS2788, _M0L3valS2789, _M0L6_2atmpS2790);
            _M0L3valS2794 = _M0L1sS745->$0;
            _M0L6_2atmpS2793 = _M0L3valS2794 + 1;
            _M0L1sS745->$0 = _M0L6_2atmpS2793;
            continue;
          } else {
            moonbit_decref(_M0L1sS745);
          }
          break;
        }
        _M0L1uS2795 = _M0L4varsS730->$2;
        _M0L3valS2796 = _M0L1jS733->$0;
        _M0L1uS2804 = _M0L4varsS730->$2;
        _M0L3valS2805 = _M0L1jS733->$0;
        #line 381 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS2798
        = _M0MPC15array5Array2atGfE(_M0L1uS2804, _M0L3valS2805);
        _M0L1uS2802 = _M0L4varsS730->$2;
        _M0L3valS2803 = _M0L1jS733->$0;
        #line 381 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS2801
        = _M0MPC15array5Array2atGfE(_M0L1uS2802, _M0L3valS2803);
        _M0L6_2atmpS2800 = 0x1p+0f - _M0L6_2atmpS2801;
        _M0L6_2atmpS2799 = _M0L14u__baseline__jS737 * _M0L6_2atmpS2800;
        _M0L6_2atmpS2797 = _M0L6_2atmpS2798 + _M0L6_2atmpS2799;
        #line 381 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0MPC15array5Array3setGfE(_M0L1uS2795, _M0L3valS2796, _M0L6_2atmpS2797);
        _M0L1xS2806 = _M0L4varsS730->$3;
        _M0L3valS2807 = _M0L1jS733->$0;
        _M0L1xS2818 = _M0L4varsS730->$3;
        _M0L3valS2819 = _M0L1jS733->$0;
        #line 382 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS2809
        = _M0MPC15array5Array2atGfE(_M0L1xS2818, _M0L3valS2819);
        _M0L1uS2816 = _M0L4varsS730->$2;
        _M0L3valS2817 = _M0L1jS733->$0;
        #line 382 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS2815
        = _M0MPC15array5Array2atGfE(_M0L1uS2816, _M0L3valS2817);
        _M0L6_2atmpS2811 = -_M0L6_2atmpS2815;
        _M0L1xS2813 = _M0L4varsS730->$3;
        _M0L3valS2814 = _M0L1jS733->$0;
        #line 382 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS2812
        = _M0MPC15array5Array2atGfE(_M0L1xS2813, _M0L3valS2814);
        _M0L6_2atmpS2810 = _M0L6_2atmpS2811 * _M0L6_2atmpS2812;
        _M0L6_2atmpS2808 = _M0L6_2atmpS2809 + _M0L6_2atmpS2810;
        #line 382 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0MPC15array5Array3setGfE(_M0L1xS2806, _M0L3valS2807, _M0L6_2atmpS2808);
      }
      _M0L3valS2839 = _M0L1jS733->$0;
      _M0L6_2atmpS2838 = _M0L3valS2839 + 1;
      _M0L1jS733->$0 = _M0L6_2atmpS2838;
      continue;
    } else {
      moonbit_decref(_M0L1jS733);
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt18markram__stp__step(
  struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0L3synS714,
  struct _M0TP26RiantR8snn__mbt19MarkramSTPVariables* _M0L4varsS712,
  struct _M0TP26RiantR8snn__mbt19MarkramSTPParameter* _M0L5paramS716,
  float _M0L6t__nowS721
) {
  struct _M0TPB5ArrayGbE* _M0L6activeS2665;
  int32_t _M0L6_2atmpS2664;
  int32_t _if__result_5480;
  int32_t _M0L6n__preS713;
  struct _M0TPB5ArrayGfE* _M0L3rhoS2667;
  int32_t _M0L6_2atmpS2666;
  float _M0L6tau__fS715;
  float _M0L6tau__dS717;
  float _M0L11u__baselineS718;
  struct _M0TPB8MutLocalGiE* _M0L1jS719;
  #line 202 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
  _M0L6activeS2665 = _M0L4varsS712->$6;
  #line 209 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
  _M0L6_2atmpS2664 = _M0MPC15array5Array6lengthGbE(_M0L6activeS2665);
  if (_M0L6_2atmpS2664 > 0) {
    struct _M0TPB5ArrayGbE* _M0L6activeS2663 = _M0L4varsS712->$6;
    int32_t _M0L6_2atmpS2662;
    #line 209 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
    _M0L6_2atmpS2662 = _M0MPC15array5Array2atGbE(_M0L6activeS2663, 0);
    _if__result_5480 = !_M0L6_2atmpS2662;
  } else {
    _if__result_5480 = 0;
  }
  if (_if__result_5480) {
    return 0;
  }
  _M0L6n__preS713 = _M0L4varsS712->$0;
  _M0L3rhoS2667 = _M0L3synS714->$6;
  #line 214 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
  _M0L6_2atmpS2666 = _M0MPC15array5Array6lengthGfE(_M0L3rhoS2667);
  if (_M0L6_2atmpS2666 == 0) {
    return 0;
  }
  _M0L6tau__fS715 = _M0L5paramS716->$1;
  _M0L6tau__dS717 = _M0L5paramS716->$0;
  _M0L11u__baselineS718 = _M0L5paramS716->$2;
  _M0L1jS719
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1jS719)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1jS719->$0 = 0;
  while (1) {
    int32_t _M0L3valS2668 = _M0L1jS719->$0;
    if (_M0L3valS2668 < _M0L6n__preS713) {
      struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS2671 = _M0L3synS714->$0;
      struct _M0TPB5ArrayGbE* _M0L4fireS2669 = _M0L3preS2671->$5;
      int32_t _M0L3valS2670 = _M0L1jS719->$0;
      int32_t _M0L3valS2747;
      int32_t _M0L6_2atmpS2746;
      #line 222 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS2669, _M0L3valS2670)) {
        struct _M0TPB5ArrayGfE* _M0L11last__spikeS2744 = _M0L4varsS712->$5;
        int32_t _M0L3valS2745 = _M0L1jS719->$0;
        float _M0L6_2atmpS2743;
        float _M0L7dt__preS720;
        float _M0L7dt__preS722;
        struct _M0TPB5ArrayGfE* _M0L11last__spikeS2672;
        int32_t _M0L3valS2673;
        float _M0L6_2atmpS2742;
        float _M0L6arg__fS723;
        struct _M0TPB5ArrayGfE* _M0L1uS2674;
        int32_t _M0L3valS2675;
        struct _M0TPB5ArrayGfE* _M0L1uS2681;
        int32_t _M0L3valS2682;
        float _M0L6_2atmpS2680;
        float _M0L6_2atmpS2678;
        float _M0L6_2atmpS2679;
        float _M0L6_2atmpS2677;
        float _M0L6_2atmpS2676;
        float _M0L6_2atmpS2741;
        float _M0L6arg__dS724;
        struct _M0TPB5ArrayGfE* _M0L1xS2683;
        int32_t _M0L3valS2684;
        struct _M0TPB5ArrayGfE* _M0L1xS2690;
        int32_t _M0L3valS2691;
        float _M0L6_2atmpS2689;
        float _M0L6_2atmpS2687;
        float _M0L6_2atmpS2688;
        float _M0L6_2atmpS2686;
        float _M0L6_2atmpS2685;
        struct _M0TPB5ArrayGfE* _M0L8rho__preS2692;
        int32_t _M0L3valS2693;
        struct _M0TPB5ArrayGfE* _M0L1uS2699;
        int32_t _M0L3valS2700;
        float _M0L6_2atmpS2695;
        struct _M0TPB5ArrayGfE* _M0L1xS2697;
        int32_t _M0L3valS2698;
        float _M0L6_2atmpS2696;
        float _M0L6_2atmpS2694;
        struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS2740;
        struct _M0TPB5ArrayGiE* _M0L6rowptrS2738;
        int32_t _M0L3valS2739;
        int32_t _M0L5startS725;
        struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS2737;
        struct _M0TPB5ArrayGiE* _M0L6rowptrS2734;
        int32_t _M0L3valS2736;
        int32_t _M0L6_2atmpS2735;
        int32_t _M0L3endS726;
        struct _M0TPB8MutLocalGiE* _M0L1sS727;
        struct _M0TPB5ArrayGfE* _M0L1uS2709;
        int32_t _M0L3valS2710;
        struct _M0TPB5ArrayGfE* _M0L1uS2718;
        int32_t _M0L3valS2719;
        float _M0L6_2atmpS2712;
        struct _M0TPB5ArrayGfE* _M0L1uS2716;
        int32_t _M0L3valS2717;
        float _M0L6_2atmpS2715;
        float _M0L6_2atmpS2714;
        float _M0L6_2atmpS2713;
        float _M0L6_2atmpS2711;
        struct _M0TPB5ArrayGfE* _M0L1xS2720;
        int32_t _M0L3valS2721;
        struct _M0TPB5ArrayGfE* _M0L1xS2732;
        int32_t _M0L3valS2733;
        float _M0L6_2atmpS2723;
        struct _M0TPB5ArrayGfE* _M0L1uS2730;
        int32_t _M0L3valS2731;
        float _M0L6_2atmpS2729;
        float _M0L6_2atmpS2725;
        struct _M0TPB5ArrayGfE* _M0L1xS2727;
        int32_t _M0L3valS2728;
        float _M0L6_2atmpS2726;
        float _M0L6_2atmpS2724;
        float _M0L6_2atmpS2722;
        #line 224 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS2743
        = _M0MPC15array5Array2atGfE(_M0L11last__spikeS2744, _M0L3valS2745);
        _M0L7dt__preS720 = _M0L6t__nowS721 - _M0L6_2atmpS2743;
        if (_M0L7dt__preS720 < 0x0p+0f) {
          _M0L7dt__preS722 = 0x0p+0f;
        } else {
          _M0L7dt__preS722 = _M0L7dt__preS720;
        }
        _M0L11last__spikeS2672 = _M0L4varsS712->$5;
        _M0L3valS2673 = _M0L1jS719->$0;
        #line 226 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0MPC15array5Array3setGfE(_M0L11last__spikeS2672, _M0L3valS2673, _M0L6t__nowS721);
        _M0L6_2atmpS2742 = -_M0L7dt__preS722;
        _M0L6arg__fS723 = _M0L6_2atmpS2742 / _M0L6tau__fS715;
        _M0L1uS2674 = _M0L4varsS712->$2;
        _M0L3valS2675 = _M0L1jS719->$0;
        _M0L1uS2681 = _M0L4varsS712->$2;
        _M0L3valS2682 = _M0L1jS719->$0;
        #line 229 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS2680
        = _M0MPC15array5Array2atGfE(_M0L1uS2681, _M0L3valS2682);
        _M0L6_2atmpS2678 = _M0L11u__baselineS718 - _M0L6_2atmpS2680;
        #line 229 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS2679 = _M0FP26RiantR8snn__mbt4expf(_M0L6arg__fS723);
        _M0L6_2atmpS2677 = _M0L6_2atmpS2678 * _M0L6_2atmpS2679;
        _M0L6_2atmpS2676 = _M0L11u__baselineS718 - _M0L6_2atmpS2677;
        #line 229 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0MPC15array5Array3setGfE(_M0L1uS2674, _M0L3valS2675, _M0L6_2atmpS2676);
        _M0L6_2atmpS2741 = -_M0L7dt__preS722;
        _M0L6arg__dS724 = _M0L6_2atmpS2741 / _M0L6tau__dS717;
        _M0L1xS2683 = _M0L4varsS712->$3;
        _M0L3valS2684 = _M0L1jS719->$0;
        _M0L1xS2690 = _M0L4varsS712->$3;
        _M0L3valS2691 = _M0L1jS719->$0;
        #line 232 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS2689
        = _M0MPC15array5Array2atGfE(_M0L1xS2690, _M0L3valS2691);
        _M0L6_2atmpS2687 = 0x1p+0f - _M0L6_2atmpS2689;
        #line 232 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS2688 = _M0FP26RiantR8snn__mbt4expf(_M0L6arg__dS724);
        _M0L6_2atmpS2686 = _M0L6_2atmpS2687 * _M0L6_2atmpS2688;
        _M0L6_2atmpS2685 = 0x1p+0f - _M0L6_2atmpS2686;
        #line 232 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0MPC15array5Array3setGfE(_M0L1xS2683, _M0L3valS2684, _M0L6_2atmpS2685);
        _M0L8rho__preS2692 = _M0L4varsS712->$4;
        _M0L3valS2693 = _M0L1jS719->$0;
        _M0L1uS2699 = _M0L4varsS712->$2;
        _M0L3valS2700 = _M0L1jS719->$0;
        #line 234 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS2695
        = _M0MPC15array5Array2atGfE(_M0L1uS2699, _M0L3valS2700);
        _M0L1xS2697 = _M0L4varsS712->$3;
        _M0L3valS2698 = _M0L1jS719->$0;
        #line 234 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS2696
        = _M0MPC15array5Array2atGfE(_M0L1xS2697, _M0L3valS2698);
        _M0L6_2atmpS2694 = _M0L6_2atmpS2695 * _M0L6_2atmpS2696;
        #line 234 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0MPC15array5Array3setGfE(_M0L8rho__preS2692, _M0L3valS2693, _M0L6_2atmpS2694);
        _M0L6matrixS2740 = _M0L3synS714->$4;
        _M0L6rowptrS2738 = _M0L6matrixS2740->$2;
        _M0L3valS2739 = _M0L1jS719->$0;
        #line 236 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L5startS725
        = _M0MPC15array5Array2atGiE(_M0L6rowptrS2738, _M0L3valS2739);
        _M0L6matrixS2737 = _M0L3synS714->$4;
        _M0L6rowptrS2734 = _M0L6matrixS2737->$2;
        _M0L3valS2736 = _M0L1jS719->$0;
        _M0L6_2atmpS2735 = _M0L3valS2736 + 1;
        #line 237 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L3endS726
        = _M0MPC15array5Array2atGiE(_M0L6rowptrS2734, _M0L6_2atmpS2735);
        _M0L1sS727
        = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
        Moonbit_object_header(_M0L1sS727)->meta
        = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
        _M0L1sS727->$0 = _M0L5startS725;
        while (1) {
          int32_t _M0L3valS2701 = _M0L1sS727->$0;
          if (_M0L3valS2701 < _M0L3endS726) {
            struct _M0TPB5ArrayGfE* _M0L3rhoS2702 = _M0L3synS714->$6;
            int32_t _M0L3valS2703 = _M0L1sS727->$0;
            struct _M0TPB5ArrayGfE* _M0L8rho__preS2705 = _M0L4varsS712->$4;
            int32_t _M0L3valS2706 = _M0L1jS719->$0;
            float _M0L6_2atmpS2704;
            int32_t _M0L3valS2708;
            int32_t _M0L6_2atmpS2707;
            #line 240 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
            _M0L6_2atmpS2704
            = _M0MPC15array5Array2atGfE(_M0L8rho__preS2705, _M0L3valS2706);
            #line 240 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
            _M0MPC15array5Array3setGfE(_M0L3rhoS2702, _M0L3valS2703, _M0L6_2atmpS2704);
            _M0L3valS2708 = _M0L1sS727->$0;
            _M0L6_2atmpS2707 = _M0L3valS2708 + 1;
            _M0L1sS727->$0 = _M0L6_2atmpS2707;
            continue;
          } else {
            moonbit_decref(_M0L1sS727);
          }
          break;
        }
        _M0L1uS2709 = _M0L4varsS712->$2;
        _M0L3valS2710 = _M0L1jS719->$0;
        _M0L1uS2718 = _M0L4varsS712->$2;
        _M0L3valS2719 = _M0L1jS719->$0;
        #line 244 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS2712
        = _M0MPC15array5Array2atGfE(_M0L1uS2718, _M0L3valS2719);
        _M0L1uS2716 = _M0L4varsS712->$2;
        _M0L3valS2717 = _M0L1jS719->$0;
        #line 244 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS2715
        = _M0MPC15array5Array2atGfE(_M0L1uS2716, _M0L3valS2717);
        _M0L6_2atmpS2714 = 0x1p+0f - _M0L6_2atmpS2715;
        _M0L6_2atmpS2713 = _M0L11u__baselineS718 * _M0L6_2atmpS2714;
        _M0L6_2atmpS2711 = _M0L6_2atmpS2712 + _M0L6_2atmpS2713;
        #line 244 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0MPC15array5Array3setGfE(_M0L1uS2709, _M0L3valS2710, _M0L6_2atmpS2711);
        _M0L1xS2720 = _M0L4varsS712->$3;
        _M0L3valS2721 = _M0L1jS719->$0;
        _M0L1xS2732 = _M0L4varsS712->$3;
        _M0L3valS2733 = _M0L1jS719->$0;
        #line 245 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS2723
        = _M0MPC15array5Array2atGfE(_M0L1xS2732, _M0L3valS2733);
        _M0L1uS2730 = _M0L4varsS712->$2;
        _M0L3valS2731 = _M0L1jS719->$0;
        #line 245 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS2729
        = _M0MPC15array5Array2atGfE(_M0L1uS2730, _M0L3valS2731);
        _M0L6_2atmpS2725 = -_M0L6_2atmpS2729;
        _M0L1xS2727 = _M0L4varsS712->$3;
        _M0L3valS2728 = _M0L1jS719->$0;
        #line 245 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS2726
        = _M0MPC15array5Array2atGfE(_M0L1xS2727, _M0L3valS2728);
        _M0L6_2atmpS2724 = _M0L6_2atmpS2725 * _M0L6_2atmpS2726;
        _M0L6_2atmpS2722 = _M0L6_2atmpS2723 + _M0L6_2atmpS2724;
        #line 245 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0MPC15array5Array3setGfE(_M0L1xS2720, _M0L3valS2721, _M0L6_2atmpS2722);
      }
      _M0L3valS2747 = _M0L1jS719->$0;
      _M0L6_2atmpS2746 = _M0L3valS2747 + 1;
      _M0L1jS719->$0 = _M0L6_2atmpS2746;
      continue;
    } else {
      moonbit_decref(_M0L1jS719);
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt12update__time(
  struct _M0TP26RiantR8snn__mbt4Time* _M0L1tS710,
  float _M0L2dtS711
) {
  struct _M0TPB5ArrayGfE* _M0L1tS2654;
  struct _M0TPB5ArrayGfE* _M0L1tS2657;
  float _M0L6_2atmpS2656;
  float _M0L6_2atmpS2655;
  struct _M0TPB5ArrayGiE* _M0L2ttS2658;
  struct _M0TPB5ArrayGiE* _M0L2ttS2661;
  int32_t _M0L6_2atmpS2660;
  int32_t _M0L6_2atmpS2659;
  #line 89 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\time.mbt"
  _M0L1tS2654 = _M0L1tS710->$0;
  _M0L1tS2657 = _M0L1tS710->$0;
  #line 90 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\time.mbt"
  _M0L6_2atmpS2656 = _M0MPC15array5Array2atGfE(_M0L1tS2657, 0);
  _M0L6_2atmpS2655 = _M0L6_2atmpS2656 + _M0L2dtS711;
  #line 90 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\time.mbt"
  _M0MPC15array5Array3setGfE(_M0L1tS2654, 0, _M0L6_2atmpS2655);
  _M0L2ttS2658 = _M0L1tS710->$1;
  _M0L2ttS2661 = _M0L1tS710->$1;
  #line 91 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\time.mbt"
  _M0L6_2atmpS2660 = _M0MPC15array5Array2atGiE(_M0L2ttS2661, 0);
  _M0L6_2atmpS2659 = _M0L6_2atmpS2660 + 1;
  #line 91 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\time.mbt"
  _M0MPC15array5Array3setGiE(_M0L2ttS2658, 0, _M0L6_2atmpS2659);
  return 0;
}

float _M0FP26RiantR8snn__mbt9get__time(
  struct _M0TP26RiantR8snn__mbt4Time* _M0L1tS709
) {
  struct _M0TPB5ArrayGfE* _M0L1tS2653;
  #line 49 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\time.mbt"
  _M0L1tS2653 = _M0L1tS709->$0;
  #line 50 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\time.mbt"
  return _M0MPC15array5Array2atGfE(_M0L1tS2653, 0);
}

struct _M0TP26RiantR8snn__mbt4Time* _M0MP26RiantR8snn__mbt4Time3new() {
  float* _M0L6_2atmpS2652;
  struct _M0TPB5ArrayGfE* _M0L6_2atmpS2649;
  int32_t* _M0L6_2atmpS2651;
  struct _M0TPB5ArrayGiE* _M0L6_2atmpS2650;
  struct _M0TP26RiantR8snn__mbt4Time* _block_5483;
  #line 34 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\time.mbt"
  _M0L6_2atmpS2652 = (float*)moonbit_make_float_array_raw(1);
  _M0L6_2atmpS2652[0] = 0x0p+0f;
  _M0L6_2atmpS2649
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_M0L6_2atmpS2649)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 21, 0);
  _M0L6_2atmpS2649->$0 = _M0L6_2atmpS2652;
  _M0L6_2atmpS2649->$1 = 1;
  _M0L6_2atmpS2651 = (int32_t*)moonbit_make_int32_array_raw(1);
  _M0L6_2atmpS2651[0] = 0;
  _M0L6_2atmpS2650
  = (struct _M0TPB5ArrayGiE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGiE));
  Moonbit_object_header(_M0L6_2atmpS2650)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 24, 0);
  _M0L6_2atmpS2650->$0 = _M0L6_2atmpS2651;
  _M0L6_2atmpS2650->$1 = 1;
  _block_5483
  = (struct _M0TP26RiantR8snn__mbt4Time*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt4Time));
  Moonbit_object_header(_block_5483)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 92, 0);
  _block_5483->$0 = _M0L6_2atmpS2649;
  _block_5483->$1 = _M0L6_2atmpS2650;
  _block_5483->$2 = 0x1p-3f;
  return _block_5483;
}

float _M0FP26RiantR8snn__mbt9next__f32(
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L1rS707
) {
  uint32_t _M0L1uS706;
  uint32_t _M0L4bitsS708;
  double _M0L6_2atmpS2648;
  double _M0L6_2atmpS2647;
  #line 128 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  #line 129 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L1uS706 = _M0FP26RiantR8snn__mbt9next__u32(_M0L1rS707);
  _M0L4bitsS708 = _M0L1uS706 >> 8;
  _M0L6_2atmpS2648 = (double)_M0L4bitsS708;
  _M0L6_2atmpS2647 = _M0L6_2atmpS2648 * 0x1p-24;
  return (float)_M0L6_2atmpS2647;
}

uint32_t _M0FP26RiantR8snn__mbt9next__u32(
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L1rS705
) {
  uint64_t _M0L1uS704;
  uint64_t _M0L6_2atmpS2646;
  #line 110 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  #line 111 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L1uS704 = _M0FP26RiantR8snn__mbt9next__u64(_M0L1rS705);
  _M0L6_2atmpS2646 = _M0L1uS704 >> 32;
  return (uint32_t)_M0L6_2atmpS2646;
}

uint64_t _M0FP26RiantR8snn__mbt9next__u64(
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L1rS697
) {
  uint64_t _M0L2s0S696;
  uint64_t _M0L2s1S698;
  uint64_t _M0L2s2S699;
  uint64_t _M0L2s3S700;
  uint64_t _M0L3tmpS701;
  uint64_t _M0L6_2atmpS2645;
  uint64_t _M0L3resS702;
  uint64_t _M0L1tS703;
  uint64_t _M0L6_2atmpS2635;
  uint64_t _M0L6_2atmpS2636;
  uint64_t _M0L2s2S2638;
  uint64_t _M0L6_2atmpS2637;
  uint64_t _M0L2s3S2640;
  uint64_t _M0L6_2atmpS2639;
  uint64_t _M0L2s2S2642;
  uint64_t _M0L6_2atmpS2641;
  uint64_t _M0L2s3S2644;
  uint64_t _M0L6_2atmpS2643;
  #line 90 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2s0S696 = _M0L1rS697->$0;
  _M0L2s1S698 = _M0L1rS697->$1;
  _M0L2s2S699 = _M0L1rS697->$2;
  _M0L2s3S700 = _M0L1rS697->$3;
  _M0L3tmpS701 = _M0L2s0S696 + _M0L2s3S700;
  #line 96 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L6_2atmpS2645 = _M0FP26RiantR8snn__mbt4rotl(_M0L3tmpS701, 23);
  _M0L3resS702 = _M0L6_2atmpS2645 + _M0L2s0S696;
  _M0L1tS703 = _M0L2s1S698 << 17;
  _M0L6_2atmpS2635 = _M0L2s2S699 ^ _M0L2s0S696;
  _M0L1rS697->$2 = _M0L6_2atmpS2635;
  _M0L6_2atmpS2636 = _M0L2s3S700 ^ _M0L2s1S698;
  _M0L1rS697->$3 = _M0L6_2atmpS2636;
  _M0L2s2S2638 = _M0L1rS697->$2;
  _M0L6_2atmpS2637 = _M0L2s1S698 ^ _M0L2s2S2638;
  _M0L1rS697->$1 = _M0L6_2atmpS2637;
  _M0L2s3S2640 = _M0L1rS697->$3;
  _M0L6_2atmpS2639 = _M0L2s0S696 ^ _M0L2s3S2640;
  _M0L1rS697->$0 = _M0L6_2atmpS2639;
  _M0L2s2S2642 = _M0L1rS697->$2;
  _M0L6_2atmpS2641 = _M0L2s2S2642 ^ _M0L1tS703;
  _M0L1rS697->$2 = _M0L6_2atmpS2641;
  _M0L2s3S2644 = _M0L1rS697->$3;
  #line 103 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L6_2atmpS2643 = _M0FP26RiantR8snn__mbt4rotl(_M0L2s3S2644, 45);
  _M0L1rS697->$3 = _M0L6_2atmpS2643;
  return _M0L3resS702;
}

uint64_t _M0FP26RiantR8snn__mbt4rotl(uint64_t _M0L1xS694, int32_t _M0L1kS695) {
  uint64_t _M0L6_2atmpS2632;
  int32_t _M0L6_2atmpS2634;
  uint64_t _M0L6_2atmpS2633;
  #line 83 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L6_2atmpS2632 = _M0L1xS694 << (_M0L1kS695 & 63);
  _M0L6_2atmpS2634 = 64 - _M0L1kS695;
  _M0L6_2atmpS2633 = _M0L1xS694 >> (_M0L6_2atmpS2634 & 63);
  return _M0L6_2atmpS2632 | _M0L6_2atmpS2633;
}

float _M0MP26RiantR8snn__mbt7Monitor12firing__rate(
  struct _M0TP26RiantR8snn__mbt7Monitor* _M0L1mS690
) {
  struct _M0TPB5ArrayGfE* _M0L4dataS2631;
  int32_t _M0L1nS689;
  struct _M0TPB5ArrayGfE* _M0L5timesS2630;
  int32_t _M0L4n__tS691;
  struct _M0TPB5ArrayGfE* _M0L5timesS2628;
  int32_t _M0L6_2atmpS2629;
  float _M0L6_2atmpS2625;
  struct _M0TPB5ArrayGfE* _M0L5timesS2627;
  float _M0L6_2atmpS2626;
  float _M0L9total__msS692;
  int32_t _M0L9n__spikesS693;
  float _M0L6_2atmpS2624;
  float _M0L6_2atmpS2623;
  #line 38 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\vecplot.mbt"
  _M0L4dataS2631 = _M0L1mS690->$2;
  #line 39 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\vecplot.mbt"
  _M0L1nS689 = _M0MPC15array5Array6lengthGfE(_M0L4dataS2631);
  if (_M0L1nS689 < 2) {
    return 0x0p+0f;
  }
  _M0L5timesS2630 = _M0L1mS690->$3;
  #line 43 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\vecplot.mbt"
  _M0L4n__tS691 = _M0MPC15array5Array6lengthGfE(_M0L5timesS2630);
  if (_M0L4n__tS691 < 2) {
    return 0x0p+0f;
  }
  _M0L5timesS2628 = _M0L1mS690->$3;
  _M0L6_2atmpS2629 = _M0L4n__tS691 - 1;
  #line 47 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\vecplot.mbt"
  _M0L6_2atmpS2625
  = _M0MPC15array5Array2atGfE(_M0L5timesS2628, _M0L6_2atmpS2629);
  _M0L5timesS2627 = _M0L1mS690->$3;
  #line 47 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\vecplot.mbt"
  _M0L6_2atmpS2626 = _M0MPC15array5Array2atGfE(_M0L5timesS2627, 0);
  _M0L9total__msS692 = _M0L6_2atmpS2625 - _M0L6_2atmpS2626;
  if (_M0L9total__msS692 <= 0x0p+0f) {
    return 0x0p+0f;
  }
  #line 51 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\vecplot.mbt"
  _M0L9n__spikesS693
  = _M0MP26RiantR8snn__mbt7Monitor13count__spikes(_M0L1mS690);
  _M0L6_2atmpS2624 = (float)_M0L9n__spikesS693;
  _M0L6_2atmpS2623 = _M0L6_2atmpS2624 * 0x1.f4p+9f;
  return _M0L6_2atmpS2623 / _M0L9total__msS692;
}

int32_t _M0MP26RiantR8snn__mbt7Monitor13count__spikes(
  struct _M0TP26RiantR8snn__mbt7Monitor* _M0L1mS682
) {
  struct _M0TPB5ArrayGfE* _M0L4dataS2622;
  int32_t _M0L1nS681;
  struct _M0TPB8MutLocalGiE* _M0L5countS683;
  struct _M0TPB8MutLocalGfE* _M0L4prevS684;
  int32_t _M0L7_2abindS685;
  int32_t _M0L1iS686;
  int32_t _result_5485;
  #line 17 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\vecplot.mbt"
  _M0L4dataS2622 = _M0L1mS682->$2;
  #line 18 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\vecplot.mbt"
  _M0L1nS681 = _M0MPC15array5Array6lengthGfE(_M0L4dataS2622);
  if (_M0L1nS681 == 0) {
    return 0;
  }
  _M0L5countS683
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L5countS683)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L5countS683->$0 = 0;
  _M0L4prevS684
  = (struct _M0TPB8MutLocalGfE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGfE));
  Moonbit_object_header(_M0L4prevS684)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L4prevS684->$0 = 0x0p+0f;
  _M0L7_2abindS685 = 0;
  _M0L1iS686 = _M0L7_2abindS685;
  while (1) {
    if (_M0L1iS686 < _M0L1nS681) {
      struct _M0TPB5ArrayGfE* _M0L4dataS2620 = _M0L1mS682->$2;
      float _M0L3curS687;
      float _M0L3valS2617;
      int32_t _M0L6_2atmpS2621;
      #line 25 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\vecplot.mbt"
      _M0L3curS687 = _M0MPC15array5Array2atGfE(_M0L4dataS2620, _M0L1iS686);
      _M0L3valS2617 = _M0L4prevS684->$0;
      if (_M0L3valS2617 < 0x1p-1f && _M0L3curS687 >= 0x1p-1f) {
        int32_t _M0L3valS2619 = _M0L5countS683->$0;
        int32_t _M0L6_2atmpS2618 = _M0L3valS2619 + 1;
        _M0L5countS683->$0 = _M0L6_2atmpS2618;
      }
      _M0L4prevS684->$0 = _M0L3curS687;
      _M0L6_2atmpS2621 = _M0L1iS686 + 1;
      _M0L1iS686 = _M0L6_2atmpS2621;
      continue;
    } else {
      moonbit_decref(_M0L4prevS684);
    }
    break;
  }
  _result_5485 = _M0L5countS683->$0;
  moonbit_decref(_M0L5countS683);
  return _result_5485;
}

double _M0FPC14math2ln(double _M0L1xS667) {
  struct _M0TUdiE* _M0L7_2abindS668;
  double _M0L5_2af1S669;
  int32_t _M0L5_2akiS670;
  double _M0L1fS672;
  double _M0L1kS673;
  double _M0L6_2atmpS2610;
  double _M0L1sS674;
  double _M0L2s2S675;
  double _M0L2s4S676;
  double _M0L6_2atmpS2609;
  double _M0L6_2atmpS2608;
  double _M0L6_2atmpS2607;
  double _M0L6_2atmpS2606;
  double _M0L6_2atmpS2605;
  double _M0L6_2atmpS2604;
  double _M0L2t1S677;
  double _M0L6_2atmpS2603;
  double _M0L6_2atmpS2602;
  double _M0L6_2atmpS2601;
  double _M0L6_2atmpS2600;
  double _M0L2t2S678;
  double _M0L1rS679;
  double _M0L6_2atmpS2599;
  double _M0L4hfsqS680;
  double _M0L6_2atmpS2592;
  double _M0L6_2atmpS2598;
  double _M0L6_2atmpS2596;
  double _M0L6_2atmpS2597;
  double _M0L6_2atmpS2595;
  double _M0L6_2atmpS2594;
  double _M0L6_2atmpS2593;
  #line 55 "C:\\Users\\31379\\.moon\\lib\\core\\math\\log_double_nonjs.mbt"
  if (_M0L1xS667 < 0x0p+0) {
    return _M0FPC16double14not__a__number;
  } else {
    #line 65 "C:\\Users\\31379\\.moon\\lib\\core\\math\\log_double_nonjs.mbt"
    #line 65 "C:\\Users\\31379\\.moon\\lib\\core\\math\\log_double_nonjs.mbt"
    if (
      _M0MPC16double6Double7is__nan(_M0L1xS667)
      || _M0MPC16double6Double7is__inf(_M0L1xS667)
    ) {
      return _M0L1xS667;
    } else if (_M0L1xS667 == 0x0p+0) {
      return _M0FPC16double13neg__infinity;
    }
  }
  #line 70 "C:\\Users\\31379\\.moon\\lib\\core\\math\\log_double_nonjs.mbt"
  _M0L7_2abindS668 = _M0FPC14math5frexp(_M0L1xS667);
  _M0L5_2af1S669 = _M0L7_2abindS668->$0;
  _M0L5_2akiS670 = _M0L7_2abindS668->$1;
  moonbit_decref(_M0L7_2abindS668);
  if (_M0L5_2af1S669 < 0x1.6a09e667f3bcdp-1) {
    double _M0L6_2atmpS2614 = _M0L5_2af1S669 * 0x1p+1;
    double _M0L6_2atmpS2611 = _M0L6_2atmpS2614 - 0x1p+0;
    int32_t _M0L6_2atmpS2613 = _M0L5_2akiS670 - 1;
    double _M0L6_2atmpS2612 = (double)_M0L6_2atmpS2613;
    _M0L1fS672 = _M0L6_2atmpS2611;
    _M0L1kS673 = _M0L6_2atmpS2612;
    goto join_671;
  } else {
    double _M0L6_2atmpS2615 = _M0L5_2af1S669 - 0x1p+0;
    double _M0L6_2atmpS2616 = (double)_M0L5_2akiS670;
    _M0L1fS672 = _M0L6_2atmpS2615;
    _M0L1kS673 = _M0L6_2atmpS2616;
    goto join_671;
  }
  join_671:;
  _M0L6_2atmpS2610 = 0x1p+1 + _M0L1fS672;
  _M0L1sS674 = _M0L1fS672 / _M0L6_2atmpS2610;
  _M0L2s2S675 = _M0L1sS674 * _M0L1sS674;
  _M0L2s4S676 = _M0L2s2S675 * _M0L2s2S675;
  _M0L6_2atmpS2609 = _M0L2s4S676 * 0x1.2f112df3e5244p-3;
  _M0L6_2atmpS2608 = 0x1.7466496cb03dep-3 + _M0L6_2atmpS2609;
  _M0L6_2atmpS2607 = _M0L2s4S676 * _M0L6_2atmpS2608;
  _M0L6_2atmpS2606 = 0x1.2492494229359p-2 + _M0L6_2atmpS2607;
  _M0L6_2atmpS2605 = _M0L2s4S676 * _M0L6_2atmpS2606;
  _M0L6_2atmpS2604 = 0x1.5555555555593p-1 + _M0L6_2atmpS2605;
  _M0L2t1S677 = _M0L2s2S675 * _M0L6_2atmpS2604;
  _M0L6_2atmpS2603 = _M0L2s4S676 * 0x1.39a09d078c69fp-3;
  _M0L6_2atmpS2602 = 0x1.c71c51d8e78afp-3 + _M0L6_2atmpS2603;
  _M0L6_2atmpS2601 = _M0L2s4S676 * _M0L6_2atmpS2602;
  _M0L6_2atmpS2600 = 0x1.999999997fa04p-2 + _M0L6_2atmpS2601;
  _M0L2t2S678 = _M0L2s4S676 * _M0L6_2atmpS2600;
  _M0L1rS679 = _M0L2t1S677 + _M0L2t2S678;
  _M0L6_2atmpS2599 = 0x1p-1 * _M0L1fS672;
  _M0L4hfsqS680 = _M0L6_2atmpS2599 * _M0L1fS672;
  _M0L6_2atmpS2592 = _M0L1kS673 * 0x1.62e42feep-1;
  _M0L6_2atmpS2598 = _M0L4hfsqS680 + _M0L1rS679;
  _M0L6_2atmpS2596 = _M0L1sS674 * _M0L6_2atmpS2598;
  _M0L6_2atmpS2597 = _M0L1kS673 * 0x1.a39ef35793c76p-33;
  _M0L6_2atmpS2595 = _M0L6_2atmpS2596 + _M0L6_2atmpS2597;
  _M0L6_2atmpS2594 = _M0L4hfsqS680 - _M0L6_2atmpS2595;
  _M0L6_2atmpS2593 = _M0L6_2atmpS2594 - _M0L1fS672;
  return _M0L6_2atmpS2592 - _M0L6_2atmpS2593;
}

struct _M0TUdiE* _M0FPC14math5frexp(double _M0L1fS660) {
  struct _M0TUdiE* _M0L7_2abindS661;
  double _M0L10_2anorm__fS662;
  int32_t _M0L6_2aexpS663;
  uint64_t _M0L1uS664;
  uint64_t _M0L6_2atmpS2591;
  uint64_t _M0L6_2atmpS2590;
  int32_t _M0L6_2atmpS2589;
  int32_t _M0L6_2atmpS2588;
  int32_t _M0L3expS665;
  uint64_t _M0L6_2atmpS2587;
  uint64_t _M0L6_2atmpS2586;
  uint64_t _M0L6_2atmpS2585;
  double _M0L4fracS666;
  struct _M0TUdiE* _block_5488;
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\math\\utils.mbt"
  #line 134 "C:\\Users\\31379\\.moon\\lib\\core\\math\\utils.mbt"
  #line 134 "C:\\Users\\31379\\.moon\\lib\\core\\math\\utils.mbt"
  if (
    _M0L1fS660 == 0x0p+0
    || _M0MPC16double6Double7is__inf(_M0L1fS660)
    || _M0MPC16double6Double7is__nan(_M0L1fS660)
  ) {
    struct _M0TUdiE* _block_5487 =
      (struct _M0TUdiE*)moonbit_malloc(sizeof(struct _M0TUdiE));
    Moonbit_object_header(_block_5487)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
    _block_5487->$0 = _M0L1fS660;
    _block_5487->$1 = 0;
    return _block_5487;
  }
  #line 137 "C:\\Users\\31379\\.moon\\lib\\core\\math\\utils.mbt"
  _M0L7_2abindS661 = _M0FPC14math9normalize(_M0L1fS660);
  _M0L10_2anorm__fS662 = _M0L7_2abindS661->$0;
  _M0L6_2aexpS663 = _M0L7_2abindS661->$1;
  moonbit_decref(_M0L7_2abindS661);
  _M0L1uS664 = *(int64_t*)&_M0L10_2anorm__fS662;
  _M0L6_2atmpS2591 = _M0L1uS664 >> 52;
  _M0L6_2atmpS2590 = _M0L6_2atmpS2591 & 2047ull;
  _M0L6_2atmpS2589 = (int32_t)_M0L6_2atmpS2590;
  _M0L6_2atmpS2588 = _M0L6_2aexpS663 + _M0L6_2atmpS2589;
  _M0L3expS665 = _M0L6_2atmpS2588 - 1022;
  _M0L6_2atmpS2587 = ~9218868437227405312ull;
  _M0L6_2atmpS2586 = _M0L1uS664 & _M0L6_2atmpS2587;
  _M0L6_2atmpS2585 = _M0L6_2atmpS2586 | 4602678819172646912ull;
  _M0L4fracS666 = *(double*)&_M0L6_2atmpS2585;
  _block_5488 = (struct _M0TUdiE*)moonbit_malloc(sizeof(struct _M0TUdiE));
  Moonbit_object_header(_block_5488)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_5488->$0 = _M0L4fracS666;
  _block_5488->$1 = _M0L3expS665;
  return _block_5488;
}

struct _M0TUdiE* _M0FPC14math9normalize(double _M0L1fS659) {
  double _M0L6_2atmpS2582;
  struct _M0TUdiE* _block_5490;
  #line 125 "C:\\Users\\31379\\.moon\\lib\\core\\math\\utils.mbt"
  #line 126 "C:\\Users\\31379\\.moon\\lib\\core\\math\\utils.mbt"
  _M0L6_2atmpS2582 = fabs(_M0L1fS659);
  if (_M0L6_2atmpS2582 < _M0FPC16double13min__positive) {
    double _M0L6_2atmpS2584 = (double)4503599627370496ll;
    double _M0L6_2atmpS2583 = _M0L1fS659 * _M0L6_2atmpS2584;
    struct _M0TUdiE* _block_5489 =
      (struct _M0TUdiE*)moonbit_malloc(sizeof(struct _M0TUdiE));
    Moonbit_object_header(_block_5489)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
    _block_5489->$0 = _M0L6_2atmpS2583;
    _block_5489->$1 = -52;
    return _block_5489;
  }
  _block_5490 = (struct _M0TUdiE*)moonbit_malloc(sizeof(struct _M0TUdiE));
  Moonbit_object_header(_block_5490)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_5490->$0 = _M0L1fS659;
  _block_5490->$1 = 0;
  return _block_5490;
}

int32_t _M0MPC15float5Float7is__nan(float _M0L4selfS658) {
  #line 208 "C:\\Users\\31379\\.moon\\lib\\core\\float\\methods.mbt"
  return _M0L4selfS658 != _M0L4selfS658;
}

moonbit_string_t _M0IPC15float5FloatPB4Show10to__string(float _M0L4selfS657) {
  double _M0L6_2atmpS2581;
  #line 16 "C:\\Users\\31379\\.moon\\lib\\core\\float\\methods.mbt"
  _M0L6_2atmpS2581 = (double)_M0L4selfS657;
  #line 17 "C:\\Users\\31379\\.moon\\lib\\core\\float\\methods.mbt"
  return _M0MPC16double6Double10to__string(_M0L6_2atmpS2581);
}

int32_t _M0MPC15float5Float7to__int(float _M0L4selfS656) {
  #line 43 "C:\\Users\\31379\\.moon\\lib\\core\\float\\to_int.mbt"
  if (_M0L4selfS656 != _M0L4selfS656) {
    return 0;
  } else if (_M0L4selfS656 >= 0x1.fffffffcp+30f) {
    return 2147483647;
  } else if (_M0L4selfS656 <= -0x1p+31f) {
    return (int32_t)0x80000000;
  } else {
    return (int32_t)_M0L4selfS656;
  }
}

struct _M0TPB5ArrayGfE* _M0MPC15array5Array4makeGfE(
  int32_t _M0L3lenS637,
  float _M0L4elemS639
) {
  struct _M0TPB5ArrayGfE* _M0L3arrS636;
  int32_t _M0L1iS638;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3arrS636 = _M0MPC15array5Array20unsafe__make__uninitGfE(_M0L3lenS637);
  _M0L1iS638 = 0;
  while (1) {
    if (_M0L1iS638 < _M0L3lenS637) {
      float* _M0L3bufS2573 = _M0L3arrS636->$0;
      int32_t _M0L6_2atmpS2574;
      _M0L3bufS2573[_M0L1iS638] = _M0L4elemS639;
      _M0L6_2atmpS2574 = _M0L1iS638 + 1;
      _M0L1iS638 = _M0L6_2atmpS2574;
      continue;
    }
    break;
  }
  return _M0L3arrS636;
}

struct _M0TPB5ArrayGbE* _M0MPC15array5Array4makeGbE(
  int32_t _M0L3lenS642,
  int32_t _M0L4elemS644
) {
  struct _M0TPB5ArrayGbE* _M0L3arrS641;
  int32_t _M0L1iS643;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3arrS641 = _M0MPC15array5Array20unsafe__make__uninitGbE(_M0L3lenS642);
  _M0L1iS643 = 0;
  while (1) {
    if (_M0L1iS643 < _M0L3lenS642) {
      uint8_t* _M0L3bufS2575 = _M0L3arrS641->$0;
      int32_t _M0L6_2atmpS2576;
      _M0L3bufS2575[_M0L1iS643] = _M0L4elemS644;
      _M0L6_2atmpS2576 = _M0L1iS643 + 1;
      _M0L1iS643 = _M0L6_2atmpS2576;
      continue;
    }
    break;
  }
  return _M0L3arrS641;
}

struct _M0TPB5ArrayGiE* _M0MPC15array5Array4makeGiE(
  int32_t _M0L3lenS647,
  int32_t _M0L4elemS649
) {
  struct _M0TPB5ArrayGiE* _M0L3arrS646;
  int32_t _M0L1iS648;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3arrS646 = _M0MPC15array5Array20unsafe__make__uninitGiE(_M0L3lenS647);
  _M0L1iS648 = 0;
  while (1) {
    if (_M0L1iS648 < _M0L3lenS647) {
      int32_t* _M0L3bufS2577 = _M0L3arrS646->$0;
      int32_t _M0L6_2atmpS2578;
      _M0L3bufS2577[_M0L1iS648] = _M0L4elemS649;
      _M0L6_2atmpS2578 = _M0L1iS648 + 1;
      _M0L1iS648 = _M0L6_2atmpS2578;
      continue;
    }
    break;
  }
  return _M0L3arrS646;
}

struct _M0TPB5ArrayGRPB5ArrayGfEE* _M0MPC15array5Array4makeGRPB5ArrayGfEE(
  int32_t _M0L3lenS652,
  struct _M0TPB5ArrayGfE* _M0L4elemS654
) {
  struct _M0TPB5ArrayGRPB5ArrayGfEE* _M0L3arrS651;
  int32_t _M0L1iS653;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3arrS651
  = _M0MPC15array5Array20unsafe__make__uninitGRPB5ArrayGfEE(_M0L3lenS652);
  _M0L1iS653 = 0;
  while (1) {
    if (_M0L1iS653 < _M0L3lenS652) {
      struct _M0TPB5ArrayGfE** _M0L3bufS2579 = _M0L3arrS651->$0;
      struct _M0TPB5ArrayGfE* _M0L6_2aoldS5117 =
        (struct _M0TPB5ArrayGfE*)_M0L3bufS2579[_M0L1iS653];
      int32_t _M0L6_2atmpS2580;
      moonbit_incref(_M0L4elemS654);
      if (_M0L6_2aoldS5117) {
        moonbit_decref(_M0L6_2aoldS5117);
      }
      _M0L3bufS2579[_M0L1iS653] = _M0L4elemS654;
      _M0L6_2atmpS2580 = _M0L1iS653 + 1;
      _M0L1iS653 = _M0L6_2atmpS2580;
      continue;
    } else {
      moonbit_decref(_M0L4elemS654);
    }
    break;
  }
  return _M0L3arrS651;
}

int32_t _M0MPC15array5Array3setGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS621,
  int32_t _M0L5indexS622,
  float _M0L5valueS623
) {
  int32_t _M0L3lenS620;
  #line 262 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS620 = _M0L4selfS621->$1;
  if (_M0L5indexS622 >= 0 && _M0L5indexS622 < _M0L3lenS620) {
    float* _M0L6_2atmpS2569;
    #line 268 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS2569 = _M0MPC15array5Array6bufferGfE(_M0L4selfS621);
    _M0L6_2atmpS2569[_M0L5indexS622] = _M0L5valueS623;
    moonbit_decref(_M0L6_2atmpS2569);
  } else {
    #line 267 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
  return 0;
}

int32_t _M0MPC15array5Array3setGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS625,
  int32_t _M0L5indexS626,
  int32_t _M0L5valueS627
) {
  int32_t _M0L3lenS624;
  #line 262 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS624 = _M0L4selfS625->$1;
  if (_M0L5indexS626 >= 0 && _M0L5indexS626 < _M0L3lenS624) {
    int32_t* _M0L6_2atmpS2570;
    #line 268 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS2570 = _M0MPC15array5Array6bufferGiE(_M0L4selfS625);
    _M0L6_2atmpS2570[_M0L5indexS626] = _M0L5valueS627;
    moonbit_decref(_M0L6_2atmpS2570);
  } else {
    #line 267 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
  return 0;
}

int32_t _M0MPC15array5Array3setGRPB5ArrayGfEE(
  struct _M0TPB5ArrayGRPB5ArrayGfEE* _M0L4selfS629,
  int32_t _M0L5indexS630,
  struct _M0TPB5ArrayGfE* _M0L5valueS631
) {
  int32_t _M0L3lenS628;
  #line 262 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS628 = _M0L4selfS629->$1;
  if (_M0L5indexS630 >= 0 && _M0L5indexS630 < _M0L3lenS628) {
    struct _M0TPB5ArrayGfE** _M0L6_2atmpS2571;
    struct _M0TPB5ArrayGfE* _M0L6_2aoldS5118;
    #line 268 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS2571
    = _M0MPC15array5Array6bufferGRPB5ArrayGfEE(_M0L4selfS629);
    _M0L6_2aoldS5118
    = (struct _M0TPB5ArrayGfE*)_M0L6_2atmpS2571[_M0L5indexS630];
    if (_M0L6_2aoldS5118) {
      moonbit_decref(_M0L6_2aoldS5118);
    }
    _M0L6_2atmpS2571[_M0L5indexS630] = _M0L5valueS631;
    moonbit_decref(_M0L6_2atmpS2571);
  } else {
    moonbit_decref(_M0L5valueS631);
    #line 267 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
  return 0;
}

int32_t _M0MPC15array5Array3setGbE(
  struct _M0TPB5ArrayGbE* _M0L4selfS633,
  int32_t _M0L5indexS634,
  int32_t _M0L5valueS635
) {
  int32_t _M0L3lenS632;
  #line 262 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS632 = _M0L4selfS633->$1;
  if (_M0L5indexS634 >= 0 && _M0L5indexS634 < _M0L3lenS632) {
    uint8_t* _M0L6_2atmpS2572;
    #line 268 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS2572 = _M0MPC15array5Array6bufferGbE(_M0L4selfS633);
    _M0L6_2atmpS2572[_M0L5indexS634] = _M0L5valueS635;
    moonbit_decref(_M0L6_2atmpS2572);
  } else {
    #line 267 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
  return 0;
}

void* _M0MPC15array5Array3popGfE(struct _M0TPB5ArrayGfE* _M0L4selfS613) {
  int32_t _M0L3lenS612;
  #line 592 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L3lenS612 = _M0L4selfS613->$1;
  if (_M0L3lenS612 == 0) {
    return (struct moonbit_object*)&moonbit_constant_constructor_0 + 1;
  } else {
    int32_t _M0L5indexS614 = _M0L3lenS612 - 1;
    float* _M0L3bufS2567 = _M0L4selfS613->$0;
    float _M0L1vS615 = (float)_M0L3bufS2567[_M0L5indexS614];
    void* _block_5495;
    _M0L4selfS613->$1 = _M0L5indexS614;
    _block_5495
    = (void*)moonbit_malloc(sizeof(struct _M0DTPC16option6OptionGfE4Some));
    Moonbit_object_header(_block_5495)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 1);
    ((struct _M0DTPC16option6OptionGfE4Some*)_block_5495)->$0 = _M0L1vS615;
    return _block_5495;
  }
}

int64_t _M0MPC15array5Array3popGiE(struct _M0TPB5ArrayGiE* _M0L4selfS617) {
  int32_t _M0L3lenS616;
  #line 592 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L3lenS616 = _M0L4selfS617->$1;
  if (_M0L3lenS616 == 0) {
    return 4294967296ll;
  } else {
    int32_t _M0L5indexS618 = _M0L3lenS616 - 1;
    int32_t* _M0L3bufS2568 = _M0L4selfS617->$0;
    int32_t _M0L1vS619 = (int32_t)_M0L3bufS2568[_M0L5indexS618];
    _M0L4selfS617->$1 = _M0L5indexS618;
    return (int64_t)_M0L1vS619;
  }
}

float _M0MPC15array5Array2atGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS598,
  int32_t _M0L5indexS599
) {
  int32_t _M0L3lenS597;
  #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS597 = _M0L4selfS598->$1;
  if (_M0L5indexS599 >= 0 && _M0L5indexS599 < _M0L3lenS597) {
    float* _M0L6_2atmpS2562;
    float _result_5496;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS2562 = _M0MPC15array5Array6bufferGfE(_M0L4selfS598);
    _result_5496 = (float)_M0L6_2atmpS2562[_M0L5indexS599];
    moonbit_decref(_M0L6_2atmpS2562);
    return _result_5496;
  } else {
    #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
}

struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0MPC15array5Array2atGRP26RiantR8snn__mbt14SpikingSynapseE(
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt14SpikingSynapseE* _M0L4selfS601,
  int32_t _M0L5indexS602
) {
  int32_t _M0L3lenS600;
  #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS600 = _M0L4selfS601->$1;
  if (_M0L5indexS602 >= 0 && _M0L5indexS602 < _M0L3lenS600) {
    struct _M0TP26RiantR8snn__mbt14SpikingSynapse** _M0L6_2atmpS2563;
    struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0L6_2atmpS5119;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS2563
    = _M0MPC15array5Array6bufferGRP26RiantR8snn__mbt14SpikingSynapseE(_M0L4selfS601);
    _M0L6_2atmpS5119
    = (struct _M0TP26RiantR8snn__mbt14SpikingSynapse*)_M0L6_2atmpS2563[
        _M0L5indexS602
      ];
    if (_M0L6_2atmpS5119) {
      moonbit_incref(_M0L6_2atmpS5119);
    }
    moonbit_decref(_M0L6_2atmpS2563);
    return _M0L6_2atmpS5119;
  } else {
    #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
}

int32_t _M0MPC15array5Array2atGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS604,
  int32_t _M0L5indexS605
) {
  int32_t _M0L3lenS603;
  #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS603 = _M0L4selfS604->$1;
  if (_M0L5indexS605 >= 0 && _M0L5indexS605 < _M0L3lenS603) {
    int32_t* _M0L6_2atmpS2564;
    int32_t _result_5497;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS2564 = _M0MPC15array5Array6bufferGiE(_M0L4selfS604);
    _result_5497 = (int32_t)_M0L6_2atmpS2564[_M0L5indexS605];
    moonbit_decref(_M0L6_2atmpS2564);
    return _result_5497;
  } else {
    #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
}

int32_t _M0MPC15array5Array2atGbE(
  struct _M0TPB5ArrayGbE* _M0L4selfS607,
  int32_t _M0L5indexS608
) {
  int32_t _M0L3lenS606;
  #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS606 = _M0L4selfS607->$1;
  if (_M0L5indexS608 >= 0 && _M0L5indexS608 < _M0L3lenS606) {
    uint8_t* _M0L6_2atmpS2565;
    int32_t _result_5498;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS2565 = _M0MPC15array5Array6bufferGbE(_M0L4selfS607);
    _result_5498 = (int32_t)_M0L6_2atmpS2565[_M0L5indexS608];
    moonbit_decref(_M0L6_2atmpS2565);
    return _result_5498;
  } else {
    #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
}

struct _M0TPB5ArrayGfE* _M0MPC15array5Array2atGRPB5ArrayGfEE(
  struct _M0TPB5ArrayGRPB5ArrayGfEE* _M0L4selfS610,
  int32_t _M0L5indexS611
) {
  int32_t _M0L3lenS609;
  #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS609 = _M0L4selfS610->$1;
  if (_M0L5indexS611 >= 0 && _M0L5indexS611 < _M0L3lenS609) {
    struct _M0TPB5ArrayGfE** _M0L6_2atmpS2566;
    struct _M0TPB5ArrayGfE* _M0L6_2atmpS5120;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS2566
    = _M0MPC15array5Array6bufferGRPB5ArrayGfEE(_M0L4selfS610);
    _M0L6_2atmpS5120
    = (struct _M0TPB5ArrayGfE*)_M0L6_2atmpS2566[_M0L5indexS611];
    if (_M0L6_2atmpS5120) {
      moonbit_incref(_M0L6_2atmpS5120);
    }
    moonbit_decref(_M0L6_2atmpS2566);
    return _M0L6_2atmpS5120;
  } else {
    #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
}

int32_t _M0FPB7printlnGsE(moonbit_string_t _M0L5inputS596) {
  moonbit_string_t _M0L6_2atmpS2561;
  #line 36 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\console.mbt"
  #line 37 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\console.mbt"
  _M0L6_2atmpS2561 = _M0IPC16string6StringPB4Show10to__string(_M0L5inputS596);
  #line 37 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\console.mbt"
  moonbit_println(_M0L6_2atmpS2561);
  moonbit_decref(_M0L6_2atmpS2561);
  return 0;
}

moonbit_string_t _M0MPC16double6Double10to__string(double _M0L4selfS595) {
  #line 296 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double.mbt"
  #line 298 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double.mbt"
  return _M0FPB15ryu__to__string(_M0L4selfS595);
}

int32_t _M0MPC16double6Double7is__inf(double _M0L4selfS594) {
  #line 221 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double.mbt"
  return _M0L4selfS594 > _M0FPB18double__max__value
         || _M0L4selfS594 < _M0FPB18double__min__value;
}

int32_t _M0MPC16double6Double7is__nan(double _M0L4selfS593) {
  #line 196 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double.mbt"
  return _M0L4selfS593 != _M0L4selfS593;
}

moonbit_string_t _M0FPB15ryu__to__string(double _M0L3valS578) {
  uint64_t _M0L4bitsS581;
  uint64_t _M0L6_2atmpS2560;
  uint64_t _M0L6_2atmpS2559;
  int32_t _M0L8ieeeSignS582;
  uint64_t _M0L12ieeeMantissaS583;
  uint64_t _M0L6_2atmpS2558;
  uint64_t _M0L6_2atmpS2557;
  int32_t _M0L12ieeeExponentS584;
  struct _M0TPB17FloatingDecimal64* _M0L7_2abindS585;
  struct _M0TPB17FloatingDecimal64* _M0L1vS586;
  moonbit_string_t _result_5500;
  #line 668 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  if (_M0L3valS578 == 0x0p+0) {
    return (moonbit_string_t)moonbit_string_literal_4.data;
  }
  if (_M0L3valS578 >= -0x1p+53 && _M0L3valS578 <= 0x1p+53) {
    if (_M0L3valS578 >= -0x1p+31 && _M0L3valS578 <= 0x1.fffffffcp+30) {
      int32_t _M0L1iS579;
      double _M0L6_2atmpS2546;
      #line 683 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0L1iS579 = _M0MPC16double6Double7to__int(_M0L3valS578);
      _M0L6_2atmpS2546 = (double)_M0L1iS579;
      if (_M0L6_2atmpS2546 == _M0L3valS578) {
        #line 685 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        return _M0MPC13int3Int18to__string_2einner(_M0L1iS579, 10);
      }
    } else {
      int64_t _M0L1iS580;
      double _M0L6_2atmpS2547;
      #line 688 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0L1iS580 = _M0MPC16double6Double9to__int64(_M0L3valS578);
      _M0L6_2atmpS2547 = (double)_M0L1iS580;
      if (_M0L6_2atmpS2547 == _M0L3valS578) {
        #line 690 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        return _M0MPC15int645Int6418to__string_2einner(_M0L1iS580, 10);
      }
    }
  }
  _M0L4bitsS581 = *(int64_t*)&_M0L3valS578;
  _M0L6_2atmpS2560 = _M0L4bitsS581 >> 63;
  _M0L6_2atmpS2559 = _M0L6_2atmpS2560 & 1ull;
  _M0L8ieeeSignS582 = _M0L6_2atmpS2559 != 0ull;
  _M0L12ieeeMantissaS583 = _M0L4bitsS581 & 4503599627370495ull;
  _M0L6_2atmpS2558 = _M0L4bitsS581 >> 52;
  _M0L6_2atmpS2557 = _M0L6_2atmpS2558 & 2047ull;
  _M0L12ieeeExponentS584 = (int32_t)_M0L6_2atmpS2557;
  if (
    _M0L12ieeeExponentS584 == 2047
    || _M0L12ieeeExponentS584 == 0 && _M0L12ieeeMantissaS583 == 0ull
  ) {
    int32_t _M0L6_2atmpS2548 = _M0L12ieeeExponentS584 != 0;
    int32_t _M0L6_2atmpS2549 = _M0L12ieeeMantissaS583 != 0ull;
    #line 707 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    return _M0FPB18copy__special__str(_M0L8ieeeSignS582, _M0L6_2atmpS2548, _M0L6_2atmpS2549);
  }
  #line 709 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS585
  = _M0FPB15d2d__small__int(_M0L12ieeeMantissaS583, _M0L12ieeeExponentS584);
  if (_M0L7_2abindS585 == 0) {
    uint32_t _M0L6_2atmpS2550;
    if (_M0L7_2abindS585) {
      moonbit_decref(_M0L7_2abindS585);
    }
    _M0L6_2atmpS2550 = *(uint32_t*)&_M0L12ieeeExponentS584;
    #line 719 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L1vS586 = _M0FPB3d2d(_M0L12ieeeMantissaS583, _M0L6_2atmpS2550);
  } else {
    struct _M0TPB17FloatingDecimal64* _M0L7_2aSomeS587 = _M0L7_2abindS585;
    struct _M0TPB17FloatingDecimal64* _M0L4_2afS588 = _M0L7_2aSomeS587;
    struct _M0TPB17FloatingDecimal64* _M0L1xS589 = _M0L4_2afS588;
    while (1) {
      uint64_t _M0L8mantissaS2556 = _M0L1xS589->$0;
      uint64_t _M0L1qS590 = _M0L8mantissaS2556 / 10ull;
      uint64_t _M0L8mantissaS2554 = _M0L1xS589->$0;
      uint64_t _M0L6_2atmpS2555 = 10ull * _M0L1qS590;
      uint64_t _M0L1rS591 = _M0L8mantissaS2554 - _M0L6_2atmpS2555;
      int32_t _M0L8exponentS2553;
      int32_t _M0L6_2atmpS2552;
      struct _M0TPB17FloatingDecimal64* _M0L6_2atmpS2551;
      if (_M0L1rS591 != 0ull) {
        _M0L1vS586 = _M0L1xS589;
        break;
      }
      _M0L8exponentS2553 = _M0L1xS589->$1;
      moonbit_decref(_M0L1xS589);
      _M0L6_2atmpS2552 = _M0L8exponentS2553 + 1;
      _M0L6_2atmpS2551
      = (struct _M0TPB17FloatingDecimal64*)moonbit_malloc(sizeof(struct _M0TPB17FloatingDecimal64));
      Moonbit_object_header(_M0L6_2atmpS2551)->meta
      = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
      _M0L6_2atmpS2551->$0 = _M0L1qS590;
      _M0L6_2atmpS2551->$1 = _M0L6_2atmpS2552;
      _M0L1xS589 = _M0L6_2atmpS2551;
      continue;
      break;
    }
  }
  #line 721 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _result_5500 = _M0FPB9to__chars(_M0L1vS586, _M0L8ieeeSignS582);
  moonbit_decref(_M0L1vS586);
  return _result_5500;
}

struct _M0TPB17FloatingDecimal64* _M0FPB15d2d__small__int(
  uint64_t _M0L12ieeeMantissaS573,
  int32_t _M0L12ieeeExponentS575
) {
  uint64_t _M0L2m2S572;
  int32_t _M0L6_2atmpS2545;
  int32_t _M0L2e2S574;
  int32_t _M0L6_2atmpS2544;
  uint64_t _M0L6_2atmpS2543;
  uint64_t _M0L4maskS576;
  uint64_t _M0L8fractionS577;
  int32_t _M0L6_2atmpS2542;
  uint64_t _M0L6_2atmpS2541;
  struct _M0TPB17FloatingDecimal64* _M0L6_2atmpS2540;
  #line 637 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L2m2S572 = 4503599627370496ull | _M0L12ieeeMantissaS573;
  _M0L6_2atmpS2545 = _M0L12ieeeExponentS575 - 1023;
  _M0L2e2S574 = _M0L6_2atmpS2545 - 52;
  if (_M0L2e2S574 > 0) {
    return 0;
  }
  if (_M0L2e2S574 < -52) {
    return 0;
  }
  _M0L6_2atmpS2544 = -_M0L2e2S574;
  _M0L6_2atmpS2543 = 1ull << (_M0L6_2atmpS2544 & 63);
  _M0L4maskS576 = _M0L6_2atmpS2543 - 1ull;
  _M0L8fractionS577 = _M0L2m2S572 & _M0L4maskS576;
  if (_M0L8fractionS577 != 0ull) {
    return 0;
  }
  _M0L6_2atmpS2542 = -_M0L2e2S574;
  _M0L6_2atmpS2541 = _M0L2m2S572 >> (_M0L6_2atmpS2542 & 63);
  _M0L6_2atmpS2540
  = (struct _M0TPB17FloatingDecimal64*)moonbit_malloc(sizeof(struct _M0TPB17FloatingDecimal64));
  Moonbit_object_header(_M0L6_2atmpS2540)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L6_2atmpS2540->$0 = _M0L6_2atmpS2541;
  _M0L6_2atmpS2540->$1 = 0;
  return _M0L6_2atmpS2540;
}

moonbit_string_t _M0FPB9to__chars(
  struct _M0TPB17FloatingDecimal64* _M0L1vS540,
  int32_t _M0L4signS538
) {
  moonbit_bytes_t _M0L6resultS536;
  int32_t _M0Lm5indexS537;
  uint64_t _M0L6outputS539;
  int32_t _M0L7olengthS541;
  int32_t _M0L8exponentS2539;
  int32_t _M0L6_2atmpS2538;
  int32_t _M0Lm3expS542;
  int32_t _M0L6_2atmpS2537;
  int32_t _M0L6_2atmpS2535;
  int32_t _M0L18scientificNotationS543;
  #line 530 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6resultS536 = (moonbit_bytes_t)moonbit_make_bytes(25, 0);
  _M0Lm5indexS537 = 0;
  if (_M0L4signS538) {
    int32_t _M0L6_2atmpS2409 = _M0Lm5indexS537;
    int32_t _M0L6_2atmpS2410;
    if (
      _M0L6_2atmpS2409 < 0
      || _M0L6_2atmpS2409 >= Moonbit_array_length(_M0L6resultS536)
    ) {
      #line 535 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      moonbit_panic();
    }
    _M0L6resultS536[_M0L6_2atmpS2409] = 45;
    _M0L6_2atmpS2410 = _M0Lm5indexS537;
    _M0Lm5indexS537 = _M0L6_2atmpS2410 + 1;
  }
  _M0L6outputS539 = _M0L1vS540->$0;
  #line 539 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7olengthS541 = _M0FPB17decimal__length17(_M0L6outputS539);
  _M0L8exponentS2539 = _M0L1vS540->$1;
  _M0L6_2atmpS2538 = _M0L8exponentS2539 + _M0L7olengthS541;
  _M0Lm3expS542 = _M0L6_2atmpS2538 - 1;
  _M0L6_2atmpS2537 = _M0Lm3expS542;
  if (_M0L6_2atmpS2537 >= -6) {
    int32_t _M0L6_2atmpS2536 = _M0Lm3expS542;
    _M0L6_2atmpS2535 = _M0L6_2atmpS2536 < 21;
  } else {
    _M0L6_2atmpS2535 = 0;
  }
  _M0L18scientificNotationS543 = !_M0L6_2atmpS2535;
  if (_M0L18scientificNotationS543) {
    int32_t _M0L7_2abindS544 = _M0L7olengthS541 - 1;
    uint64_t _M0L6outputS545;
    int32_t _M0L1iS546 = 0;
    uint64_t _M0L6outputS547 = _M0L6outputS539;
    int32_t _M0L6_2atmpS2411;
    int32_t _M0L6_2atmpS2415;
    int32_t _M0L6_2atmpS2414;
    int32_t _M0L6_2atmpS2413;
    int32_t _M0L6_2atmpS2412;
    int32_t _M0L6_2atmpS2419;
    int32_t _M0L6_2atmpS2420;
    int32_t _M0L6_2atmpS2421;
    int32_t _M0L6_2atmpS2422;
    int32_t _M0L6_2atmpS2423;
    int32_t _M0L6_2atmpS2429;
    int32_t _M0L6_2atmpS2462;
    moonbit_string_t _result_5502;
    while (1) {
      if (_M0L1iS546 < _M0L7_2abindS544) {
        uint64_t _M0L1cS548 = _M0L6outputS547 % 10ull;
        int32_t _M0L6_2atmpS2468 = _M0Lm5indexS537;
        int32_t _M0L6_2atmpS2467 = _M0L6_2atmpS2468 + _M0L7olengthS541;
        int32_t _M0L6_2atmpS2463 = _M0L6_2atmpS2467 - _M0L1iS546;
        int32_t _M0L6_2atmpS2466 = (int32_t)_M0L1cS548;
        int32_t _M0L6_2atmpS2465 = 48 + _M0L6_2atmpS2466;
        int32_t _M0L6_2atmpS2464 = _M0L6_2atmpS2465 & 0xff;
        int32_t _M0L6_2atmpS2469;
        uint64_t _M0L6_2atmpS2470;
        if (
          _M0L6_2atmpS2463 < 0
          || _M0L6_2atmpS2463 >= Moonbit_array_length(_M0L6resultS536)
        ) {
          #line 547 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
          moonbit_panic();
        }
        _M0L6resultS536[_M0L6_2atmpS2463] = _M0L6_2atmpS2464;
        _M0L6_2atmpS2469 = _M0L1iS546 + 1;
        _M0L6_2atmpS2470 = _M0L6outputS547 / 10ull;
        _M0L1iS546 = _M0L6_2atmpS2469;
        _M0L6outputS547 = _M0L6_2atmpS2470;
        continue;
      } else {
        _M0L6outputS545 = _M0L6outputS547;
      }
      break;
    }
    _M0L6_2atmpS2411 = _M0Lm5indexS537;
    _M0L6_2atmpS2415 = (int32_t)_M0L6outputS545;
    _M0L6_2atmpS2414 = _M0L6_2atmpS2415 % 10;
    _M0L6_2atmpS2413 = 48 + _M0L6_2atmpS2414;
    _M0L6_2atmpS2412 = _M0L6_2atmpS2413 & 0xff;
    if (
      _M0L6_2atmpS2411 < 0
      || _M0L6_2atmpS2411 >= Moonbit_array_length(_M0L6resultS536)
    ) {
      #line 552 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      moonbit_panic();
    }
    _M0L6resultS536[_M0L6_2atmpS2411] = _M0L6_2atmpS2412;
    if (_M0L7olengthS541 > 1) {
      int32_t _M0L6_2atmpS2417 = _M0Lm5indexS537;
      int32_t _M0L6_2atmpS2416 = _M0L6_2atmpS2417 + 1;
      if (
        _M0L6_2atmpS2416 < 0
        || _M0L6_2atmpS2416 >= Moonbit_array_length(_M0L6resultS536)
      ) {
        #line 554 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS536[_M0L6_2atmpS2416] = 46;
    } else {
      int32_t _M0L6_2atmpS2418 = _M0Lm5indexS537;
      _M0Lm5indexS537 = _M0L6_2atmpS2418 - 1;
    }
    _M0L6_2atmpS2419 = _M0Lm5indexS537;
    _M0L6_2atmpS2420 = _M0L7olengthS541 + 1;
    _M0Lm5indexS537 = _M0L6_2atmpS2419 + _M0L6_2atmpS2420;
    _M0L6_2atmpS2421 = _M0Lm5indexS537;
    if (
      _M0L6_2atmpS2421 < 0
      || _M0L6_2atmpS2421 >= Moonbit_array_length(_M0L6resultS536)
    ) {
      #line 562 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      moonbit_panic();
    }
    _M0L6resultS536[_M0L6_2atmpS2421] = 101;
    _M0L6_2atmpS2422 = _M0Lm5indexS537;
    _M0Lm5indexS537 = _M0L6_2atmpS2422 + 1;
    _M0L6_2atmpS2423 = _M0Lm3expS542;
    if (_M0L6_2atmpS2423 < 0) {
      int32_t _M0L6_2atmpS2424 = _M0Lm5indexS537;
      int32_t _M0L6_2atmpS2425;
      int32_t _M0L6_2atmpS2426;
      if (
        _M0L6_2atmpS2424 < 0
        || _M0L6_2atmpS2424 >= Moonbit_array_length(_M0L6resultS536)
      ) {
        #line 565 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS536[_M0L6_2atmpS2424] = 45;
      _M0L6_2atmpS2425 = _M0Lm5indexS537;
      _M0Lm5indexS537 = _M0L6_2atmpS2425 + 1;
      _M0L6_2atmpS2426 = _M0Lm3expS542;
      _M0Lm3expS542 = -_M0L6_2atmpS2426;
    } else {
      int32_t _M0L6_2atmpS2427 = _M0Lm5indexS537;
      int32_t _M0L6_2atmpS2428;
      if (
        _M0L6_2atmpS2427 < 0
        || _M0L6_2atmpS2427 >= Moonbit_array_length(_M0L6resultS536)
      ) {
        #line 569 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS536[_M0L6_2atmpS2427] = 43;
      _M0L6_2atmpS2428 = _M0Lm5indexS537;
      _M0Lm5indexS537 = _M0L6_2atmpS2428 + 1;
    }
    _M0L6_2atmpS2429 = _M0Lm3expS542;
    if (_M0L6_2atmpS2429 >= 100) {
      int32_t _M0L6_2atmpS2445 = _M0Lm3expS542;
      int32_t _M0L1aS550 = _M0L6_2atmpS2445 / 100;
      int32_t _M0L6_2atmpS2444 = _M0Lm3expS542;
      int32_t _M0L6_2atmpS2443 = _M0L6_2atmpS2444 / 10;
      int32_t _M0L1bS551 = _M0L6_2atmpS2443 % 10;
      int32_t _M0L6_2atmpS2442 = _M0Lm3expS542;
      int32_t _M0L1cS552 = _M0L6_2atmpS2442 % 10;
      int32_t _M0L6_2atmpS2430 = _M0Lm5indexS537;
      int32_t _M0L6_2atmpS2432 = 48 + _M0L1aS550;
      int32_t _M0L6_2atmpS2431 = _M0L6_2atmpS2432 & 0xff;
      int32_t _M0L6_2atmpS2436;
      int32_t _M0L6_2atmpS2433;
      int32_t _M0L6_2atmpS2435;
      int32_t _M0L6_2atmpS2434;
      int32_t _M0L6_2atmpS2440;
      int32_t _M0L6_2atmpS2437;
      int32_t _M0L6_2atmpS2439;
      int32_t _M0L6_2atmpS2438;
      int32_t _M0L6_2atmpS2441;
      if (
        _M0L6_2atmpS2430 < 0
        || _M0L6_2atmpS2430 >= Moonbit_array_length(_M0L6resultS536)
      ) {
        #line 576 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS536[_M0L6_2atmpS2430] = _M0L6_2atmpS2431;
      _M0L6_2atmpS2436 = _M0Lm5indexS537;
      _M0L6_2atmpS2433 = _M0L6_2atmpS2436 + 1;
      _M0L6_2atmpS2435 = 48 + _M0L1bS551;
      _M0L6_2atmpS2434 = _M0L6_2atmpS2435 & 0xff;
      if (
        _M0L6_2atmpS2433 < 0
        || _M0L6_2atmpS2433 >= Moonbit_array_length(_M0L6resultS536)
      ) {
        #line 577 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS536[_M0L6_2atmpS2433] = _M0L6_2atmpS2434;
      _M0L6_2atmpS2440 = _M0Lm5indexS537;
      _M0L6_2atmpS2437 = _M0L6_2atmpS2440 + 2;
      _M0L6_2atmpS2439 = 48 + _M0L1cS552;
      _M0L6_2atmpS2438 = _M0L6_2atmpS2439 & 0xff;
      if (
        _M0L6_2atmpS2437 < 0
        || _M0L6_2atmpS2437 >= Moonbit_array_length(_M0L6resultS536)
      ) {
        #line 578 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS536[_M0L6_2atmpS2437] = _M0L6_2atmpS2438;
      _M0L6_2atmpS2441 = _M0Lm5indexS537;
      _M0Lm5indexS537 = _M0L6_2atmpS2441 + 3;
    } else {
      int32_t _M0L6_2atmpS2446 = _M0Lm3expS542;
      if (_M0L6_2atmpS2446 >= 10) {
        int32_t _M0L6_2atmpS2456 = _M0Lm3expS542;
        int32_t _M0L1aS553 = _M0L6_2atmpS2456 / 10;
        int32_t _M0L6_2atmpS2455 = _M0Lm3expS542;
        int32_t _M0L1bS554 = _M0L6_2atmpS2455 % 10;
        int32_t _M0L6_2atmpS2447 = _M0Lm5indexS537;
        int32_t _M0L6_2atmpS2449 = 48 + _M0L1aS553;
        int32_t _M0L6_2atmpS2448 = _M0L6_2atmpS2449 & 0xff;
        int32_t _M0L6_2atmpS2453;
        int32_t _M0L6_2atmpS2450;
        int32_t _M0L6_2atmpS2452;
        int32_t _M0L6_2atmpS2451;
        int32_t _M0L6_2atmpS2454;
        if (
          _M0L6_2atmpS2447 < 0
          || _M0L6_2atmpS2447 >= Moonbit_array_length(_M0L6resultS536)
        ) {
          #line 583 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
          moonbit_panic();
        }
        _M0L6resultS536[_M0L6_2atmpS2447] = _M0L6_2atmpS2448;
        _M0L6_2atmpS2453 = _M0Lm5indexS537;
        _M0L6_2atmpS2450 = _M0L6_2atmpS2453 + 1;
        _M0L6_2atmpS2452 = 48 + _M0L1bS554;
        _M0L6_2atmpS2451 = _M0L6_2atmpS2452 & 0xff;
        if (
          _M0L6_2atmpS2450 < 0
          || _M0L6_2atmpS2450 >= Moonbit_array_length(_M0L6resultS536)
        ) {
          #line 584 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
          moonbit_panic();
        }
        _M0L6resultS536[_M0L6_2atmpS2450] = _M0L6_2atmpS2451;
        _M0L6_2atmpS2454 = _M0Lm5indexS537;
        _M0Lm5indexS537 = _M0L6_2atmpS2454 + 2;
      } else {
        int32_t _M0L6_2atmpS2457 = _M0Lm5indexS537;
        int32_t _M0L6_2atmpS2460 = _M0Lm3expS542;
        int32_t _M0L6_2atmpS2459 = 48 + _M0L6_2atmpS2460;
        int32_t _M0L6_2atmpS2458 = _M0L6_2atmpS2459 & 0xff;
        int32_t _M0L6_2atmpS2461;
        if (
          _M0L6_2atmpS2457 < 0
          || _M0L6_2atmpS2457 >= Moonbit_array_length(_M0L6resultS536)
        ) {
          #line 587 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
          moonbit_panic();
        }
        _M0L6resultS536[_M0L6_2atmpS2457] = _M0L6_2atmpS2458;
        _M0L6_2atmpS2461 = _M0Lm5indexS537;
        _M0Lm5indexS537 = _M0L6_2atmpS2461 + 1;
      }
    }
    _M0L6_2atmpS2462 = _M0Lm5indexS537;
    #line 590 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _result_5502
    = _M0FPB19string__from__bytes(_M0L6resultS536, 0, _M0L6_2atmpS2462);
    moonbit_decref(_M0L6resultS536);
    return _result_5502;
  } else {
    int32_t _M0L6_2atmpS2471 = _M0Lm3expS542;
    int32_t _M0L6_2atmpS2534;
    moonbit_string_t _result_5508;
    if (_M0L6_2atmpS2471 < 0) {
      int32_t _M0L6_2atmpS2472 = _M0Lm5indexS537;
      int32_t _M0L6_2atmpS2474;
      int32_t _M0L6_2atmpS2473;
      int32_t _M0L6_2atmpS2475;
      int32_t _M0L1iS555;
      int32_t _M0L6_2atmpS2490;
      int32_t _M0L6_2atmpS2492;
      int32_t _M0L6_2atmpS2491;
      int32_t _M0L7currentS557;
      int32_t _M0L1iS558;
      uint64_t _M0L6outputS559;
      if (
        _M0L6_2atmpS2472 < 0
        || _M0L6_2atmpS2472 >= Moonbit_array_length(_M0L6resultS536)
      ) {
        #line 595 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS536[_M0L6_2atmpS2472] = 48;
      _M0L6_2atmpS2474 = _M0Lm5indexS537;
      _M0L6_2atmpS2473 = _M0L6_2atmpS2474 + 1;
      if (
        _M0L6_2atmpS2473 < 0
        || _M0L6_2atmpS2473 >= Moonbit_array_length(_M0L6resultS536)
      ) {
        #line 596 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS536[_M0L6_2atmpS2473] = 46;
      _M0L6_2atmpS2475 = _M0Lm5indexS537;
      _M0Lm5indexS537 = _M0L6_2atmpS2475 + 2;
      _M0L1iS555 = -1;
      while (1) {
        int32_t _M0L6_2atmpS2476 = _M0Lm3expS542;
        if (_M0L1iS555 > _M0L6_2atmpS2476) {
          int32_t _M0L6_2atmpS2479 = _M0Lm5indexS537;
          int32_t _M0L6_2atmpS2478 = _M0L6_2atmpS2479 - _M0L1iS555;
          int32_t _M0L6_2atmpS2477 = _M0L6_2atmpS2478 - 1;
          int32_t _M0L6_2atmpS2480;
          if (
            _M0L6_2atmpS2477 < 0
            || _M0L6_2atmpS2477 >= Moonbit_array_length(_M0L6resultS536)
          ) {
            #line 599 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
            moonbit_panic();
          }
          _M0L6resultS536[_M0L6_2atmpS2477] = 48;
          _M0L6_2atmpS2480 = _M0L1iS555 - 1;
          _M0L1iS555 = _M0L6_2atmpS2480;
          continue;
        }
        break;
      }
      _M0L6_2atmpS2490 = _M0Lm5indexS537;
      _M0L6_2atmpS2492 = _M0Lm3expS542;
      _M0L6_2atmpS2491 = -1 - _M0L6_2atmpS2492;
      _M0L7currentS557 = _M0L6_2atmpS2490 + _M0L6_2atmpS2491;
      _M0L1iS558 = 0;
      _M0L6outputS559 = _M0L6outputS539;
      while (1) {
        if (_M0L1iS558 < _M0L7olengthS541) {
          int32_t _M0L6_2atmpS2487 = _M0L7currentS557 + _M0L7olengthS541;
          int32_t _M0L6_2atmpS2486 = _M0L6_2atmpS2487 - _M0L1iS558;
          int32_t _M0L6_2atmpS2481 = _M0L6_2atmpS2486 - 1;
          uint64_t _M0L6_2atmpS2485 = _M0L6outputS559 % 10ull;
          int32_t _M0L6_2atmpS2484 = (int32_t)_M0L6_2atmpS2485;
          int32_t _M0L6_2atmpS2483 = 48 + _M0L6_2atmpS2484;
          int32_t _M0L6_2atmpS2482 = _M0L6_2atmpS2483 & 0xff;
          int32_t _M0L6_2atmpS2488;
          uint64_t _M0L6_2atmpS2489;
          if (
            _M0L6_2atmpS2481 < 0
            || _M0L6_2atmpS2481 >= Moonbit_array_length(_M0L6resultS536)
          ) {
            #line 603 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
            moonbit_panic();
          }
          _M0L6resultS536[_M0L6_2atmpS2481] = _M0L6_2atmpS2482;
          _M0L6_2atmpS2488 = _M0L1iS558 + 1;
          _M0L6_2atmpS2489 = _M0L6outputS559 / 10ull;
          _M0L1iS558 = _M0L6_2atmpS2488;
          _M0L6outputS559 = _M0L6_2atmpS2489;
          continue;
        }
        break;
      }
      _M0Lm5indexS537 = _M0L7currentS557 + _M0L7olengthS541;
    } else {
      int32_t _M0L6_2atmpS2494 = _M0Lm3expS542;
      int32_t _M0L6_2atmpS2493 = _M0L6_2atmpS2494 + 1;
      if (_M0L6_2atmpS2493 >= _M0L7olengthS541) {
        int32_t _M0L1iS561 = 0;
        uint64_t _M0L6outputS562 = _M0L6outputS539;
        int32_t _M0L6_2atmpS2505;
        int32_t _M0L6_2atmpS2510;
        int32_t _M0L7_2abindS564;
        int32_t _M0L1iS565;
        int32_t _M0L6_2atmpS2511;
        int32_t _M0L6_2atmpS2514;
        int32_t _M0L6_2atmpS2513;
        int32_t _M0L6_2atmpS2512;
        while (1) {
          if (_M0L1iS561 < _M0L7olengthS541) {
            int32_t _M0L6_2atmpS2502 = _M0Lm5indexS537;
            int32_t _M0L6_2atmpS2501 = _M0L6_2atmpS2502 + _M0L7olengthS541;
            int32_t _M0L6_2atmpS2500 = _M0L6_2atmpS2501 - _M0L1iS561;
            int32_t _M0L6_2atmpS2495 = _M0L6_2atmpS2500 - 1;
            uint64_t _M0L6_2atmpS2499 = _M0L6outputS562 % 10ull;
            int32_t _M0L6_2atmpS2498 = (int32_t)_M0L6_2atmpS2499;
            int32_t _M0L6_2atmpS2497 = 48 + _M0L6_2atmpS2498;
            int32_t _M0L6_2atmpS2496 = _M0L6_2atmpS2497 & 0xff;
            int32_t _M0L6_2atmpS2503;
            uint64_t _M0L6_2atmpS2504;
            if (
              _M0L6_2atmpS2495 < 0
              || _M0L6_2atmpS2495 >= Moonbit_array_length(_M0L6resultS536)
            ) {
              #line 610 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
              moonbit_panic();
            }
            _M0L6resultS536[_M0L6_2atmpS2495] = _M0L6_2atmpS2496;
            _M0L6_2atmpS2503 = _M0L1iS561 + 1;
            _M0L6_2atmpS2504 = _M0L6outputS562 / 10ull;
            _M0L1iS561 = _M0L6_2atmpS2503;
            _M0L6outputS562 = _M0L6_2atmpS2504;
            continue;
          }
          break;
        }
        _M0L6_2atmpS2505 = _M0Lm5indexS537;
        _M0Lm5indexS537 = _M0L6_2atmpS2505 + _M0L7olengthS541;
        _M0L6_2atmpS2510 = _M0Lm3expS542;
        _M0L7_2abindS564 = _M0L6_2atmpS2510 + 1;
        _M0L1iS565 = _M0L7olengthS541;
        while (1) {
          if (_M0L1iS565 < _M0L7_2abindS564) {
            int32_t _M0L6_2atmpS2508 = _M0Lm5indexS537;
            int32_t _M0L6_2atmpS2507 = _M0L6_2atmpS2508 + _M0L1iS565;
            int32_t _M0L6_2atmpS2506 = _M0L6_2atmpS2507 - _M0L7olengthS541;
            int32_t _M0L6_2atmpS2509;
            if (
              _M0L6_2atmpS2506 < 0
              || _M0L6_2atmpS2506 >= Moonbit_array_length(_M0L6resultS536)
            ) {
              #line 615 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
              moonbit_panic();
            }
            _M0L6resultS536[_M0L6_2atmpS2506] = 48;
            _M0L6_2atmpS2509 = _M0L1iS565 + 1;
            _M0L1iS565 = _M0L6_2atmpS2509;
            continue;
          }
          break;
        }
        _M0L6_2atmpS2511 = _M0Lm5indexS537;
        _M0L6_2atmpS2514 = _M0Lm3expS542;
        _M0L6_2atmpS2513 = _M0L6_2atmpS2514 + 1;
        _M0L6_2atmpS2512 = _M0L6_2atmpS2513 - _M0L7olengthS541;
        _M0Lm5indexS537 = _M0L6_2atmpS2511 + _M0L6_2atmpS2512;
      } else {
        int32_t _M0L6_2atmpS2531 = _M0Lm5indexS537;
        int32_t _M0L6_2atmpS2530 = _M0L6_2atmpS2531 + 1;
        int32_t _M0L1iS567 = 0;
        int32_t _M0L7currentS568 = _M0L6_2atmpS2530;
        uint64_t _M0L6outputS569 = _M0L6outputS539;
        int32_t _M0L6_2atmpS2532;
        int32_t _M0L6_2atmpS2533;
        while (1) {
          if (_M0L1iS567 < _M0L7olengthS541) {
            int32_t _M0L6_2atmpS2526 = _M0L7olengthS541 - _M0L1iS567;
            int32_t _M0L6_2atmpS2524 = _M0L6_2atmpS2526 - 1;
            int32_t _M0L6_2atmpS2525 = _M0Lm3expS542;
            int32_t _M0L7currentS570;
            int32_t _M0L6_2atmpS2521;
            int32_t _M0L6_2atmpS2520;
            int32_t _M0L6_2atmpS2515;
            uint64_t _M0L6_2atmpS2519;
            int32_t _M0L6_2atmpS2518;
            int32_t _M0L6_2atmpS2517;
            int32_t _M0L6_2atmpS2516;
            int32_t _M0L6_2atmpS2522;
            uint64_t _M0L6_2atmpS2523;
            if (_M0L6_2atmpS2524 == _M0L6_2atmpS2525) {
              int32_t _M0L6_2atmpS2529 = _M0L7currentS568 + _M0L7olengthS541;
              int32_t _M0L6_2atmpS2528 = _M0L6_2atmpS2529 - _M0L1iS567;
              int32_t _M0L6_2atmpS2527 = _M0L6_2atmpS2528 - 1;
              if (
                _M0L6_2atmpS2527 < 0
                || _M0L6_2atmpS2527 >= Moonbit_array_length(_M0L6resultS536)
              ) {
                #line 622 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
                moonbit_panic();
              }
              _M0L6resultS536[_M0L6_2atmpS2527] = 46;
              _M0L7currentS570 = _M0L7currentS568 - 1;
            } else {
              _M0L7currentS570 = _M0L7currentS568;
            }
            _M0L6_2atmpS2521 = _M0L7currentS570 + _M0L7olengthS541;
            _M0L6_2atmpS2520 = _M0L6_2atmpS2521 - _M0L1iS567;
            _M0L6_2atmpS2515 = _M0L6_2atmpS2520 - 1;
            _M0L6_2atmpS2519 = _M0L6outputS569 % 10ull;
            _M0L6_2atmpS2518 = (int32_t)_M0L6_2atmpS2519;
            _M0L6_2atmpS2517 = 48 + _M0L6_2atmpS2518;
            _M0L6_2atmpS2516 = _M0L6_2atmpS2517 & 0xff;
            if (
              _M0L6_2atmpS2515 < 0
              || _M0L6_2atmpS2515 >= Moonbit_array_length(_M0L6resultS536)
            ) {
              #line 627 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
              moonbit_panic();
            }
            _M0L6resultS536[_M0L6_2atmpS2515] = _M0L6_2atmpS2516;
            _M0L6_2atmpS2522 = _M0L1iS567 + 1;
            _M0L6_2atmpS2523 = _M0L6outputS569 / 10ull;
            _M0L1iS567 = _M0L6_2atmpS2522;
            _M0L7currentS568 = _M0L7currentS570;
            _M0L6outputS569 = _M0L6_2atmpS2523;
            continue;
          }
          break;
        }
        _M0L6_2atmpS2532 = _M0Lm5indexS537;
        _M0L6_2atmpS2533 = _M0L7olengthS541 + 1;
        _M0Lm5indexS537 = _M0L6_2atmpS2532 + _M0L6_2atmpS2533;
      }
    }
    _M0L6_2atmpS2534 = _M0Lm5indexS537;
    #line 632 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _result_5508
    = _M0FPB19string__from__bytes(_M0L6resultS536, 0, _M0L6_2atmpS2534);
    moonbit_decref(_M0L6resultS536);
    return _result_5508;
  }
}

struct _M0TPB17FloatingDecimal64* _M0FPB3d2d(
  uint64_t _M0L12ieeeMantissaS482,
  uint32_t _M0L12ieeeExponentS481
) {
  int32_t _M0Lm2e2S479;
  uint64_t _M0Lm2m2S480;
  uint64_t _M0L6_2atmpS2408;
  uint64_t _M0L6_2atmpS2407;
  int32_t _M0L4evenS483;
  uint64_t _M0L6_2atmpS2406;
  uint64_t _M0L2mvS484;
  int32_t _M0L7mmShiftS485;
  uint64_t _M0Lm2vrS486;
  uint64_t _M0Lm2vpS487;
  uint64_t _M0Lm2vmS488;
  int32_t _M0Lm3e10S489;
  int32_t _M0Lm17vmIsTrailingZerosS490;
  int32_t _M0Lm17vrIsTrailingZerosS491;
  int32_t _M0L6_2atmpS2308;
  int32_t _M0Lm7removedS510;
  int32_t _M0Lm16lastRemovedDigitS511;
  uint64_t _M0Lm6outputS512;
  int32_t _M0L6_2atmpS2404;
  int32_t _M0L6_2atmpS2405;
  int32_t _M0L3expS535;
  uint64_t _M0L6_2atmpS2403;
  struct _M0TPB17FloatingDecimal64* _block_5514;
  #line 347 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0Lm2e2S479 = 0;
  _M0Lm2m2S480 = 0ull;
  if (_M0L12ieeeExponentS481 == 0u) {
    _M0Lm2e2S479 = -1076;
    _M0Lm2m2S480 = _M0L12ieeeMantissaS482;
  } else {
    int32_t _M0L6_2atmpS2307 = *(int32_t*)&_M0L12ieeeExponentS481;
    int32_t _M0L6_2atmpS2306 = _M0L6_2atmpS2307 - 1023;
    int32_t _M0L6_2atmpS2305 = _M0L6_2atmpS2306 - 52;
    _M0Lm2e2S479 = _M0L6_2atmpS2305 - 2;
    _M0Lm2m2S480 = 4503599627370496ull | _M0L12ieeeMantissaS482;
  }
  _M0L6_2atmpS2408 = _M0Lm2m2S480;
  _M0L6_2atmpS2407 = _M0L6_2atmpS2408 & 1ull;
  _M0L4evenS483 = _M0L6_2atmpS2407 == 0ull;
  _M0L6_2atmpS2406 = _M0Lm2m2S480;
  _M0L2mvS484 = 4ull * _M0L6_2atmpS2406;
  _M0L7mmShiftS485
  = _M0L12ieeeMantissaS482 != 0ull || _M0L12ieeeExponentS481 <= 1u;
  _M0Lm2vrS486 = 0ull;
  _M0Lm2vpS487 = 0ull;
  _M0Lm2vmS488 = 0ull;
  _M0Lm3e10S489 = 0;
  _M0Lm17vmIsTrailingZerosS490 = 0;
  _M0Lm17vrIsTrailingZerosS491 = 0;
  _M0L6_2atmpS2308 = _M0Lm2e2S479;
  if (_M0L6_2atmpS2308 >= 0) {
    int32_t _M0L6_2atmpS2330 = _M0Lm2e2S479;
    int32_t _M0L6_2atmpS2326;
    int32_t _M0L6_2atmpS2329;
    int32_t _M0L6_2atmpS2328;
    int32_t _M0L6_2atmpS2327;
    int32_t _M0L1qS492;
    int32_t _M0L6_2atmpS2325;
    int32_t _M0L6_2atmpS2324;
    int32_t _M0L1kS493;
    int32_t _M0L6_2atmpS2323;
    int32_t _M0L6_2atmpS2322;
    int32_t _M0L6_2atmpS2321;
    int32_t _M0L1iS494;
    struct _M0TPB8Pow5Pair _M0L4pow5S495;
    uint64_t _M0L6_2atmpS2320;
    struct _M0TPB19MulShiftAll64Result _M0L7_2abindS496;
    uint64_t _M0L8_2avrOutS497;
    uint64_t _M0L8_2avpOutS498;
    uint64_t _M0L8_2avmOutS499;
    #line 383 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS2326 = _M0FPB9log10Pow2(_M0L6_2atmpS2330);
    _M0L6_2atmpS2329 = _M0Lm2e2S479;
    _M0L6_2atmpS2328 = _M0L6_2atmpS2329 > 3;
    #line 383 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS2327 = _M0MPC14bool4Bool7to__int(_M0L6_2atmpS2328);
    _M0L1qS492 = _M0L6_2atmpS2326 - _M0L6_2atmpS2327;
    _M0Lm3e10S489 = _M0L1qS492;
    #line 385 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS2325 = _M0FPB8pow5bits(_M0L1qS492);
    _M0L6_2atmpS2324 = 125 + _M0L6_2atmpS2325;
    _M0L1kS493 = _M0L6_2atmpS2324 - 1;
    _M0L6_2atmpS2323 = _M0Lm2e2S479;
    _M0L6_2atmpS2322 = -_M0L6_2atmpS2323;
    _M0L6_2atmpS2321 = _M0L6_2atmpS2322 + _M0L1qS492;
    _M0L1iS494 = _M0L6_2atmpS2321 + _M0L1kS493;
    #line 387 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L4pow5S495 = _M0FPB22double__computeInvPow5(_M0L1qS492);
    _M0L6_2atmpS2320 = _M0Lm2m2S480;
    #line 388 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L7_2abindS496
    = _M0FPB13mulShiftAll64(_M0L6_2atmpS2320, _M0L4pow5S495, _M0L1iS494, _M0L7mmShiftS485);
    _M0L8_2avrOutS497 = _M0L7_2abindS496.$0;
    _M0L8_2avpOutS498 = _M0L7_2abindS496.$1;
    _M0L8_2avmOutS499 = _M0L7_2abindS496.$2;
    _M0Lm2vrS486 = _M0L8_2avrOutS497;
    _M0Lm2vpS487 = _M0L8_2avpOutS498;
    _M0Lm2vmS488 = _M0L8_2avmOutS499;
    if (_M0L1qS492 <= 21) {
      int32_t _M0L6_2atmpS2316 = (int32_t)_M0L2mvS484;
      uint64_t _M0L6_2atmpS2319 = _M0L2mvS484 / 5ull;
      int32_t _M0L6_2atmpS2318 = (int32_t)_M0L6_2atmpS2319;
      int32_t _M0L6_2atmpS2317 = 5 * _M0L6_2atmpS2318;
      int32_t _M0L6mvMod5S500 = _M0L6_2atmpS2316 - _M0L6_2atmpS2317;
      if (_M0L6mvMod5S500 == 0) {
        #line 400 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0Lm17vrIsTrailingZerosS491
        = _M0FPB18multipleOfPowerOf5(_M0L2mvS484, _M0L1qS492);
      } else if (_M0L4evenS483) {
        uint64_t _M0L6_2atmpS2310 = _M0L2mvS484 - 1ull;
        uint64_t _M0L6_2atmpS2311;
        uint64_t _M0L6_2atmpS2309;
        #line 406 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0L6_2atmpS2311 = _M0MPC14bool4Bool10to__uint64(_M0L7mmShiftS485);
        _M0L6_2atmpS2309 = _M0L6_2atmpS2310 - _M0L6_2atmpS2311;
        #line 405 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0Lm17vmIsTrailingZerosS490
        = _M0FPB18multipleOfPowerOf5(_M0L6_2atmpS2309, _M0L1qS492);
      } else {
        uint64_t _M0L6_2atmpS2312 = _M0Lm2vpS487;
        uint64_t _M0L6_2atmpS2315 = _M0L2mvS484 + 2ull;
        int32_t _M0L6_2atmpS2314;
        uint64_t _M0L6_2atmpS2313;
        #line 410 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0L6_2atmpS2314
        = _M0FPB18multipleOfPowerOf5(_M0L6_2atmpS2315, _M0L1qS492);
        #line 410 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0L6_2atmpS2313 = _M0MPC14bool4Bool10to__uint64(_M0L6_2atmpS2314);
        _M0Lm2vpS487 = _M0L6_2atmpS2312 - _M0L6_2atmpS2313;
      }
    }
  } else {
    int32_t _M0L6_2atmpS2344 = _M0Lm2e2S479;
    int32_t _M0L6_2atmpS2343 = -_M0L6_2atmpS2344;
    int32_t _M0L6_2atmpS2338;
    int32_t _M0L6_2atmpS2342;
    int32_t _M0L6_2atmpS2341;
    int32_t _M0L6_2atmpS2340;
    int32_t _M0L6_2atmpS2339;
    int32_t _M0L1qS501;
    int32_t _M0L6_2atmpS2331;
    int32_t _M0L6_2atmpS2337;
    int32_t _M0L6_2atmpS2336;
    int32_t _M0L1iS502;
    int32_t _M0L6_2atmpS2335;
    int32_t _M0L1kS503;
    int32_t _M0L1jS504;
    struct _M0TPB8Pow5Pair _M0L4pow5S505;
    uint64_t _M0L6_2atmpS2334;
    struct _M0TPB19MulShiftAll64Result _M0L7_2abindS506;
    uint64_t _M0L8_2avrOutS507;
    uint64_t _M0L8_2avpOutS508;
    uint64_t _M0L8_2avmOutS509;
    #line 415 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS2338 = _M0FPB9log10Pow5(_M0L6_2atmpS2343);
    _M0L6_2atmpS2342 = _M0Lm2e2S479;
    _M0L6_2atmpS2341 = -_M0L6_2atmpS2342;
    _M0L6_2atmpS2340 = _M0L6_2atmpS2341 > 1;
    #line 415 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS2339 = _M0MPC14bool4Bool7to__int(_M0L6_2atmpS2340);
    _M0L1qS501 = _M0L6_2atmpS2338 - _M0L6_2atmpS2339;
    _M0L6_2atmpS2331 = _M0Lm2e2S479;
    _M0Lm3e10S489 = _M0L1qS501 + _M0L6_2atmpS2331;
    _M0L6_2atmpS2337 = _M0Lm2e2S479;
    _M0L6_2atmpS2336 = -_M0L6_2atmpS2337;
    _M0L1iS502 = _M0L6_2atmpS2336 - _M0L1qS501;
    #line 418 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS2335 = _M0FPB8pow5bits(_M0L1iS502);
    _M0L1kS503 = _M0L6_2atmpS2335 - 125;
    _M0L1jS504 = _M0L1qS501 - _M0L1kS503;
    #line 420 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L4pow5S505 = _M0FPB19double__computePow5(_M0L1iS502);
    _M0L6_2atmpS2334 = _M0Lm2m2S480;
    #line 421 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L7_2abindS506
    = _M0FPB13mulShiftAll64(_M0L6_2atmpS2334, _M0L4pow5S505, _M0L1jS504, _M0L7mmShiftS485);
    _M0L8_2avrOutS507 = _M0L7_2abindS506.$0;
    _M0L8_2avpOutS508 = _M0L7_2abindS506.$1;
    _M0L8_2avmOutS509 = _M0L7_2abindS506.$2;
    _M0Lm2vrS486 = _M0L8_2avrOutS507;
    _M0Lm2vpS487 = _M0L8_2avpOutS508;
    _M0Lm2vmS488 = _M0L8_2avmOutS509;
    if (_M0L1qS501 <= 1) {
      _M0Lm17vrIsTrailingZerosS491 = 1;
      if (_M0L4evenS483) {
        int32_t _M0L6_2atmpS2332;
        #line 432 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0L6_2atmpS2332 = _M0MPC14bool4Bool7to__int(_M0L7mmShiftS485);
        _M0Lm17vmIsTrailingZerosS490 = _M0L6_2atmpS2332 == 1;
      } else {
        uint64_t _M0L6_2atmpS2333 = _M0Lm2vpS487;
        _M0Lm2vpS487 = _M0L6_2atmpS2333 - 1ull;
      }
    } else if (_M0L1qS501 < 63) {
      #line 437 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0Lm17vrIsTrailingZerosS491
      = _M0FPB18multipleOfPowerOf2(_M0L2mvS484, _M0L1qS501);
    }
  }
  _M0Lm7removedS510 = 0;
  _M0Lm16lastRemovedDigitS511 = 0;
  _M0Lm6outputS512 = 0ull;
  if (_M0Lm17vmIsTrailingZerosS490 || _M0Lm17vrIsTrailingZerosS491) {
    int32_t _if__result_5511;
    uint64_t _M0L6_2atmpS2374;
    uint64_t _M0L6_2atmpS2380;
    uint64_t _M0L6_2atmpS2381;
    int32_t _if__result_5512;
    int32_t _M0L6_2atmpS2377;
    int64_t _M0L6_2atmpS2376;
    uint64_t _M0L6_2atmpS2375;
    while (1) {
      uint64_t _M0L6_2atmpS2357 = _M0Lm2vpS487;
      uint64_t _M0L7vpDiv10S513 = _M0L6_2atmpS2357 / 10ull;
      uint64_t _M0L6_2atmpS2356 = _M0Lm2vmS488;
      uint64_t _M0L7vmDiv10S514 = _M0L6_2atmpS2356 / 10ull;
      uint64_t _M0L6_2atmpS2355;
      int32_t _M0L6_2atmpS2352;
      int32_t _M0L6_2atmpS2354;
      int32_t _M0L6_2atmpS2353;
      int32_t _M0L7vmMod10S516;
      uint64_t _M0L6_2atmpS2351;
      uint64_t _M0L7vrDiv10S517;
      uint64_t _M0L6_2atmpS2350;
      int32_t _M0L6_2atmpS2347;
      int32_t _M0L6_2atmpS2349;
      int32_t _M0L6_2atmpS2348;
      int32_t _M0L7vrMod10S518;
      int32_t _M0L6_2atmpS2346;
      if (_M0L7vpDiv10S513 <= _M0L7vmDiv10S514) {
        break;
      }
      _M0L6_2atmpS2355 = _M0Lm2vmS488;
      _M0L6_2atmpS2352 = (int32_t)_M0L6_2atmpS2355;
      _M0L6_2atmpS2354 = (int32_t)_M0L7vmDiv10S514;
      _M0L6_2atmpS2353 = 10 * _M0L6_2atmpS2354;
      _M0L7vmMod10S516 = _M0L6_2atmpS2352 - _M0L6_2atmpS2353;
      _M0L6_2atmpS2351 = _M0Lm2vrS486;
      _M0L7vrDiv10S517 = _M0L6_2atmpS2351 / 10ull;
      _M0L6_2atmpS2350 = _M0Lm2vrS486;
      _M0L6_2atmpS2347 = (int32_t)_M0L6_2atmpS2350;
      _M0L6_2atmpS2349 = (int32_t)_M0L7vrDiv10S517;
      _M0L6_2atmpS2348 = 10 * _M0L6_2atmpS2349;
      _M0L7vrMod10S518 = _M0L6_2atmpS2347 - _M0L6_2atmpS2348;
      _M0Lm17vmIsTrailingZerosS490
      = _M0Lm17vmIsTrailingZerosS490 && _M0L7vmMod10S516 == 0;
      if (_M0Lm17vrIsTrailingZerosS491) {
        int32_t _M0L6_2atmpS2345 = _M0Lm16lastRemovedDigitS511;
        _M0Lm17vrIsTrailingZerosS491 = _M0L6_2atmpS2345 == 0;
      } else {
        _M0Lm17vrIsTrailingZerosS491 = 0;
      }
      _M0Lm16lastRemovedDigitS511 = _M0L7vrMod10S518;
      _M0Lm2vrS486 = _M0L7vrDiv10S517;
      _M0Lm2vpS487 = _M0L7vpDiv10S513;
      _M0Lm2vmS488 = _M0L7vmDiv10S514;
      _M0L6_2atmpS2346 = _M0Lm7removedS510;
      _M0Lm7removedS510 = _M0L6_2atmpS2346 + 1;
      continue;
      break;
    }
    if (_M0Lm17vmIsTrailingZerosS490) {
      while (1) {
        uint64_t _M0L6_2atmpS2370 = _M0Lm2vmS488;
        uint64_t _M0L7vmDiv10S519 = _M0L6_2atmpS2370 / 10ull;
        uint64_t _M0L6_2atmpS2369 = _M0Lm2vmS488;
        int32_t _M0L6_2atmpS2366 = (int32_t)_M0L6_2atmpS2369;
        int32_t _M0L6_2atmpS2368 = (int32_t)_M0L7vmDiv10S519;
        int32_t _M0L6_2atmpS2367 = 10 * _M0L6_2atmpS2368;
        int32_t _M0L7vmMod10S520 = _M0L6_2atmpS2366 - _M0L6_2atmpS2367;
        uint64_t _M0L6_2atmpS2365;
        uint64_t _M0L7vpDiv10S522;
        uint64_t _M0L6_2atmpS2364;
        uint64_t _M0L7vrDiv10S523;
        uint64_t _M0L6_2atmpS2363;
        int32_t _M0L6_2atmpS2360;
        int32_t _M0L6_2atmpS2362;
        int32_t _M0L6_2atmpS2361;
        int32_t _M0L7vrMod10S524;
        int32_t _M0L6_2atmpS2359;
        if (_M0L7vmMod10S520 != 0) {
          break;
        }
        _M0L6_2atmpS2365 = _M0Lm2vpS487;
        _M0L7vpDiv10S522 = _M0L6_2atmpS2365 / 10ull;
        _M0L6_2atmpS2364 = _M0Lm2vrS486;
        _M0L7vrDiv10S523 = _M0L6_2atmpS2364 / 10ull;
        _M0L6_2atmpS2363 = _M0Lm2vrS486;
        _M0L6_2atmpS2360 = (int32_t)_M0L6_2atmpS2363;
        _M0L6_2atmpS2362 = (int32_t)_M0L7vrDiv10S523;
        _M0L6_2atmpS2361 = 10 * _M0L6_2atmpS2362;
        _M0L7vrMod10S524 = _M0L6_2atmpS2360 - _M0L6_2atmpS2361;
        if (_M0Lm17vrIsTrailingZerosS491) {
          int32_t _M0L6_2atmpS2358 = _M0Lm16lastRemovedDigitS511;
          _M0Lm17vrIsTrailingZerosS491 = _M0L6_2atmpS2358 == 0;
        } else {
          _M0Lm17vrIsTrailingZerosS491 = 0;
        }
        _M0Lm16lastRemovedDigitS511 = _M0L7vrMod10S524;
        _M0Lm2vrS486 = _M0L7vrDiv10S523;
        _M0Lm2vpS487 = _M0L7vpDiv10S522;
        _M0Lm2vmS488 = _M0L7vmDiv10S519;
        _M0L6_2atmpS2359 = _M0Lm7removedS510;
        _M0Lm7removedS510 = _M0L6_2atmpS2359 + 1;
        continue;
        break;
      }
    }
    if (_M0Lm17vrIsTrailingZerosS491) {
      int32_t _M0L6_2atmpS2373 = _M0Lm16lastRemovedDigitS511;
      if (_M0L6_2atmpS2373 == 5) {
        uint64_t _M0L6_2atmpS2372 = _M0Lm2vrS486;
        uint64_t _M0L6_2atmpS2371 = _M0L6_2atmpS2372 % 2ull;
        _if__result_5511 = _M0L6_2atmpS2371 == 0ull;
      } else {
        _if__result_5511 = 0;
      }
    } else {
      _if__result_5511 = 0;
    }
    if (_if__result_5511) {
      _M0Lm16lastRemovedDigitS511 = 4;
    }
    _M0L6_2atmpS2374 = _M0Lm2vrS486;
    _M0L6_2atmpS2380 = _M0Lm2vrS486;
    _M0L6_2atmpS2381 = _M0Lm2vmS488;
    if (_M0L6_2atmpS2380 == _M0L6_2atmpS2381) {
      if (!_M0L4evenS483) {
        _if__result_5512 = 1;
      } else {
        int32_t _M0L6_2atmpS2379 = _M0Lm17vmIsTrailingZerosS490;
        _if__result_5512 = !_M0L6_2atmpS2379;
      }
    } else {
      _if__result_5512 = 0;
    }
    if (_if__result_5512) {
      _M0L6_2atmpS2377 = 1;
    } else {
      int32_t _M0L6_2atmpS2378 = _M0Lm16lastRemovedDigitS511;
      _M0L6_2atmpS2377 = _M0L6_2atmpS2378 >= 5;
    }
    #line 487 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS2376 = _M0MPC14bool4Bool9to__int64(_M0L6_2atmpS2377);
    _M0L6_2atmpS2375 = *(uint64_t*)&_M0L6_2atmpS2376;
    _M0Lm6outputS512 = _M0L6_2atmpS2374 + _M0L6_2atmpS2375;
  } else {
    int32_t _M0Lm7roundUpS525 = 0;
    uint64_t _M0L6_2atmpS2402 = _M0Lm2vpS487;
    uint64_t _M0L8vpDiv100S526 = _M0L6_2atmpS2402 / 100ull;
    uint64_t _M0L6_2atmpS2401 = _M0Lm2vmS488;
    uint64_t _M0L8vmDiv100S527 = _M0L6_2atmpS2401 / 100ull;
    uint64_t _M0L6_2atmpS2396;
    uint64_t _M0L6_2atmpS2399;
    uint64_t _M0L6_2atmpS2400;
    int32_t _M0L6_2atmpS2398;
    uint64_t _M0L6_2atmpS2397;
    if (_M0L8vpDiv100S526 > _M0L8vmDiv100S527) {
      uint64_t _M0L6_2atmpS2387 = _M0Lm2vrS486;
      uint64_t _M0L8vrDiv100S528 = _M0L6_2atmpS2387 / 100ull;
      uint64_t _M0L6_2atmpS2386 = _M0Lm2vrS486;
      int32_t _M0L6_2atmpS2383 = (int32_t)_M0L6_2atmpS2386;
      int32_t _M0L6_2atmpS2385 = (int32_t)_M0L8vrDiv100S528;
      int32_t _M0L6_2atmpS2384 = 100 * _M0L6_2atmpS2385;
      int32_t _M0L8vrMod100S529 = _M0L6_2atmpS2383 - _M0L6_2atmpS2384;
      int32_t _M0L6_2atmpS2382;
      _M0Lm7roundUpS525 = _M0L8vrMod100S529 >= 50;
      _M0Lm2vrS486 = _M0L8vrDiv100S528;
      _M0Lm2vpS487 = _M0L8vpDiv100S526;
      _M0Lm2vmS488 = _M0L8vmDiv100S527;
      _M0L6_2atmpS2382 = _M0Lm7removedS510;
      _M0Lm7removedS510 = _M0L6_2atmpS2382 + 2;
    }
    while (1) {
      uint64_t _M0L6_2atmpS2395 = _M0Lm2vpS487;
      uint64_t _M0L7vpDiv10S530 = _M0L6_2atmpS2395 / 10ull;
      uint64_t _M0L6_2atmpS2394 = _M0Lm2vmS488;
      uint64_t _M0L7vmDiv10S531 = _M0L6_2atmpS2394 / 10ull;
      uint64_t _M0L6_2atmpS2393;
      uint64_t _M0L7vrDiv10S533;
      uint64_t _M0L6_2atmpS2392;
      int32_t _M0L6_2atmpS2389;
      int32_t _M0L6_2atmpS2391;
      int32_t _M0L6_2atmpS2390;
      int32_t _M0L7vrMod10S534;
      int32_t _M0L6_2atmpS2388;
      if (_M0L7vpDiv10S530 <= _M0L7vmDiv10S531) {
        break;
      }
      _M0L6_2atmpS2393 = _M0Lm2vrS486;
      _M0L7vrDiv10S533 = _M0L6_2atmpS2393 / 10ull;
      _M0L6_2atmpS2392 = _M0Lm2vrS486;
      _M0L6_2atmpS2389 = (int32_t)_M0L6_2atmpS2392;
      _M0L6_2atmpS2391 = (int32_t)_M0L7vrDiv10S533;
      _M0L6_2atmpS2390 = 10 * _M0L6_2atmpS2391;
      _M0L7vrMod10S534 = _M0L6_2atmpS2389 - _M0L6_2atmpS2390;
      _M0Lm7roundUpS525 = _M0L7vrMod10S534 >= 5;
      _M0Lm2vrS486 = _M0L7vrDiv10S533;
      _M0Lm2vpS487 = _M0L7vpDiv10S530;
      _M0Lm2vmS488 = _M0L7vmDiv10S531;
      _M0L6_2atmpS2388 = _M0Lm7removedS510;
      _M0Lm7removedS510 = _M0L6_2atmpS2388 + 1;
      continue;
      break;
    }
    _M0L6_2atmpS2396 = _M0Lm2vrS486;
    _M0L6_2atmpS2399 = _M0Lm2vrS486;
    _M0L6_2atmpS2400 = _M0Lm2vmS488;
    _M0L6_2atmpS2398
    = _M0L6_2atmpS2399 == _M0L6_2atmpS2400 || _M0Lm7roundUpS525;
    #line 522 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS2397 = _M0MPC14bool4Bool10to__uint64(_M0L6_2atmpS2398);
    _M0Lm6outputS512 = _M0L6_2atmpS2396 + _M0L6_2atmpS2397;
  }
  _M0L6_2atmpS2404 = _M0Lm3e10S489;
  _M0L6_2atmpS2405 = _M0Lm7removedS510;
  _M0L3expS535 = _M0L6_2atmpS2404 + _M0L6_2atmpS2405;
  _M0L6_2atmpS2403 = _M0Lm6outputS512;
  _block_5514
  = (struct _M0TPB17FloatingDecimal64*)moonbit_malloc(sizeof(struct _M0TPB17FloatingDecimal64));
  Moonbit_object_header(_block_5514)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_5514->$0 = _M0L6_2atmpS2403;
  _block_5514->$1 = _M0L3expS535;
  return _block_5514;
}

uint64_t _M0MPC14bool4Bool10to__uint64(int32_t _M0L4selfS478) {
  #line 110 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bool.mbt"
  if (_M0L4selfS478) {
    return 1ull;
  } else {
    return 0ull;
  }
}

int64_t _M0MPC14bool4Bool9to__int64(int32_t _M0L4selfS477) {
  #line 58 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bool.mbt"
  if (_M0L4selfS477) {
    return 1ll;
  } else {
    return 0ll;
  }
}

int32_t _M0MPC14bool4Bool7to__int(int32_t _M0L4selfS476) {
  #line 32 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bool.mbt"
  if (_M0L4selfS476) {
    return 1;
  } else {
    return 0;
  }
}

int32_t _M0FPB17decimal__length17(uint64_t _M0L1vS475) {
  #line 280 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  if (_M0L1vS475 >= 10000000000000000ull) {
    return 17;
  }
  if (_M0L1vS475 >= 1000000000000000ull) {
    return 16;
  }
  if (_M0L1vS475 >= 100000000000000ull) {
    return 15;
  }
  if (_M0L1vS475 >= 10000000000000ull) {
    return 14;
  }
  if (_M0L1vS475 >= 1000000000000ull) {
    return 13;
  }
  if (_M0L1vS475 >= 100000000000ull) {
    return 12;
  }
  if (_M0L1vS475 >= 10000000000ull) {
    return 11;
  }
  if (_M0L1vS475 >= 1000000000ull) {
    return 10;
  }
  if (_M0L1vS475 >= 100000000ull) {
    return 9;
  }
  if (_M0L1vS475 >= 10000000ull) {
    return 8;
  }
  if (_M0L1vS475 >= 1000000ull) {
    return 7;
  }
  if (_M0L1vS475 >= 100000ull) {
    return 6;
  }
  if (_M0L1vS475 >= 10000ull) {
    return 5;
  }
  if (_M0L1vS475 >= 1000ull) {
    return 4;
  }
  if (_M0L1vS475 >= 100ull) {
    return 3;
  }
  if (_M0L1vS475 >= 10ull) {
    return 2;
  }
  return 1;
}

struct _M0TPB8Pow5Pair _M0FPB22double__computeInvPow5(int32_t _M0L1iS458) {
  int32_t _M0L6_2atmpS2304;
  int32_t _M0L6_2atmpS2303;
  int32_t _M0L4baseS457;
  int32_t _M0L5base2S459;
  int32_t _M0L6offsetS460;
  int32_t _M0L6_2atmpS2302;
  uint64_t _M0L4mul0S461;
  int32_t _M0L6_2atmpS2301;
  int32_t _M0L6_2atmpS2300;
  uint64_t _M0L4mul1S462;
  uint64_t _M0L1mS463;
  struct _M0TPB7Umul128 _M0L7_2abindS464;
  uint64_t _M0L7_2alow1S465;
  uint64_t _M0L8_2ahigh1S466;
  struct _M0TPB7Umul128 _M0L7_2abindS467;
  uint64_t _M0L7_2alow0S468;
  uint64_t _M0L8_2ahigh0S469;
  uint64_t _M0L3sumS470;
  uint64_t _M0Lm5high1S471;
  int32_t _M0L6_2atmpS2298;
  int32_t _M0L6_2atmpS2299;
  int32_t _M0L5deltaS472;
  uint64_t _M0L6_2atmpS2297;
  uint64_t _M0L6_2atmpS2289;
  int32_t _M0L6_2atmpS2296;
  uint32_t _M0L6_2atmpS2293;
  int32_t _M0L6_2atmpS2295;
  int32_t _M0L6_2atmpS2294;
  uint32_t _M0L6_2atmpS2292;
  uint32_t _M0L6_2atmpS2291;
  uint64_t _M0L6_2atmpS2290;
  uint64_t _M0L1aS473;
  uint64_t _M0L6_2atmpS2288;
  uint64_t _M0L1bS474;
  #line 239 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS2304 = _M0L1iS458 + 26;
  _M0L6_2atmpS2303 = _M0L6_2atmpS2304 - 1;
  _M0L4baseS457 = _M0L6_2atmpS2303 / 26;
  _M0L5base2S459 = _M0L4baseS457 * 26;
  _M0L6offsetS460 = _M0L5base2S459 - _M0L1iS458;
  _M0L6_2atmpS2302 = _M0L4baseS457 * 2;
  #line 243 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4mul0S461
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB26gDOUBLE__POW5__INV__SPLIT2, _M0L6_2atmpS2302);
  _M0L6_2atmpS2301 = _M0L4baseS457 * 2;
  _M0L6_2atmpS2300 = _M0L6_2atmpS2301 + 1;
  #line 244 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4mul1S462
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB26gDOUBLE__POW5__INV__SPLIT2, _M0L6_2atmpS2300);
  if (_M0L6offsetS460 == 0) {
    return (struct _M0TPB8Pow5Pair){.$0 = _M0L4mul0S461, .$1 = _M0L4mul1S462};
  }
  #line 248 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L1mS463
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB20gDOUBLE__POW5__TABLE, _M0L6offsetS460);
  #line 249 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS464 = _M0FPB7umul128(_M0L1mS463, _M0L4mul1S462);
  _M0L7_2alow1S465 = _M0L7_2abindS464.$0;
  _M0L8_2ahigh1S466 = _M0L7_2abindS464.$1;
  #line 250 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS467 = _M0FPB7umul128(_M0L1mS463, _M0L4mul0S461);
  _M0L7_2alow0S468 = _M0L7_2abindS467.$0;
  _M0L8_2ahigh0S469 = _M0L7_2abindS467.$1;
  _M0L3sumS470 = _M0L8_2ahigh0S469 + _M0L7_2alow1S465;
  _M0Lm5high1S471 = _M0L8_2ahigh1S466;
  if (_M0L3sumS470 < _M0L8_2ahigh0S469) {
    uint64_t _M0L6_2atmpS2287 = _M0Lm5high1S471;
    _M0Lm5high1S471 = _M0L6_2atmpS2287 + 1ull;
  }
  #line 256 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS2298 = _M0FPB8pow5bits(_M0L5base2S459);
  #line 256 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS2299 = _M0FPB8pow5bits(_M0L1iS458);
  _M0L5deltaS472 = _M0L6_2atmpS2298 - _M0L6_2atmpS2299;
  #line 257 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS2297
  = _M0FPB13shiftright128(_M0L7_2alow0S468, _M0L3sumS470, _M0L5deltaS472);
  _M0L6_2atmpS2289 = _M0L6_2atmpS2297 + 1ull;
  _M0L6_2atmpS2296 = _M0L1iS458 / 16;
  #line 259 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS2293
  = _M0MPC15array13ReadOnlyArray2atGjE(_M0FPB19gPOW5__INV__OFFSETS, _M0L6_2atmpS2296);
  _M0L6_2atmpS2295 = _M0L1iS458 % 16;
  _M0L6_2atmpS2294 = _M0L6_2atmpS2295 << 1;
  _M0L6_2atmpS2292 = _M0L6_2atmpS2293 >> (_M0L6_2atmpS2294 & 31);
  _M0L6_2atmpS2291 = _M0L6_2atmpS2292 & 3u;
  #line 259 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS2290 = _M0MPC14uint4UInt10to__uint64(_M0L6_2atmpS2291);
  _M0L1aS473 = _M0L6_2atmpS2289 + _M0L6_2atmpS2290;
  _M0L6_2atmpS2288 = _M0Lm5high1S471;
  #line 260 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L1bS474
  = _M0FPB13shiftright128(_M0L3sumS470, _M0L6_2atmpS2288, _M0L5deltaS472);
  return (struct _M0TPB8Pow5Pair){.$0 = _M0L1aS473, .$1 = _M0L1bS474};
}

struct _M0TPB8Pow5Pair _M0FPB19double__computePow5(int32_t _M0L1iS440) {
  int32_t _M0L4baseS439;
  int32_t _M0L5base2S441;
  int32_t _M0L6offsetS442;
  int32_t _M0L6_2atmpS2286;
  uint64_t _M0L4mul0S443;
  int32_t _M0L6_2atmpS2285;
  int32_t _M0L6_2atmpS2284;
  uint64_t _M0L4mul1S444;
  uint64_t _M0L1mS445;
  struct _M0TPB7Umul128 _M0L7_2abindS446;
  uint64_t _M0L7_2alow1S447;
  uint64_t _M0L8_2ahigh1S448;
  struct _M0TPB7Umul128 _M0L7_2abindS449;
  uint64_t _M0L7_2alow0S450;
  uint64_t _M0L8_2ahigh0S451;
  uint64_t _M0L3sumS452;
  uint64_t _M0Lm5high1S453;
  int32_t _M0L6_2atmpS2282;
  int32_t _M0L6_2atmpS2283;
  int32_t _M0L5deltaS454;
  uint64_t _M0L6_2atmpS2274;
  int32_t _M0L6_2atmpS2281;
  uint32_t _M0L6_2atmpS2278;
  int32_t _M0L6_2atmpS2280;
  int32_t _M0L6_2atmpS2279;
  uint32_t _M0L6_2atmpS2277;
  uint32_t _M0L6_2atmpS2276;
  uint64_t _M0L6_2atmpS2275;
  uint64_t _M0L1aS455;
  uint64_t _M0L6_2atmpS2273;
  uint64_t _M0L1bS456;
  #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4baseS439 = _M0L1iS440 / 26;
  _M0L5base2S441 = _M0L4baseS439 * 26;
  _M0L6offsetS442 = _M0L1iS440 - _M0L5base2S441;
  _M0L6_2atmpS2286 = _M0L4baseS439 * 2;
  #line 217 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4mul0S443
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB21gDOUBLE__POW5__SPLIT2, _M0L6_2atmpS2286);
  _M0L6_2atmpS2285 = _M0L4baseS439 * 2;
  _M0L6_2atmpS2284 = _M0L6_2atmpS2285 + 1;
  #line 218 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4mul1S444
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB21gDOUBLE__POW5__SPLIT2, _M0L6_2atmpS2284);
  if (_M0L6offsetS442 == 0) {
    return (struct _M0TPB8Pow5Pair){.$0 = _M0L4mul0S443, .$1 = _M0L4mul1S444};
  }
  #line 222 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L1mS445
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB20gDOUBLE__POW5__TABLE, _M0L6offsetS442);
  #line 223 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS446 = _M0FPB7umul128(_M0L1mS445, _M0L4mul1S444);
  _M0L7_2alow1S447 = _M0L7_2abindS446.$0;
  _M0L8_2ahigh1S448 = _M0L7_2abindS446.$1;
  #line 224 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS449 = _M0FPB7umul128(_M0L1mS445, _M0L4mul0S443);
  _M0L7_2alow0S450 = _M0L7_2abindS449.$0;
  _M0L8_2ahigh0S451 = _M0L7_2abindS449.$1;
  _M0L3sumS452 = _M0L8_2ahigh0S451 + _M0L7_2alow1S447;
  _M0Lm5high1S453 = _M0L8_2ahigh1S448;
  if (_M0L3sumS452 < _M0L8_2ahigh0S451) {
    uint64_t _M0L6_2atmpS2272 = _M0Lm5high1S453;
    _M0Lm5high1S453 = _M0L6_2atmpS2272 + 1ull;
  }
  #line 230 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS2282 = _M0FPB8pow5bits(_M0L1iS440);
  #line 230 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS2283 = _M0FPB8pow5bits(_M0L5base2S441);
  _M0L5deltaS454 = _M0L6_2atmpS2282 - _M0L6_2atmpS2283;
  #line 231 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS2274
  = _M0FPB13shiftright128(_M0L7_2alow0S450, _M0L3sumS452, _M0L5deltaS454);
  _M0L6_2atmpS2281 = _M0L1iS440 / 16;
  #line 232 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS2278
  = _M0MPC15array13ReadOnlyArray2atGjE(_M0FPB14gPOW5__OFFSETS, _M0L6_2atmpS2281);
  _M0L6_2atmpS2280 = _M0L1iS440 % 16;
  _M0L6_2atmpS2279 = _M0L6_2atmpS2280 << 1;
  _M0L6_2atmpS2277 = _M0L6_2atmpS2278 >> (_M0L6_2atmpS2279 & 31);
  _M0L6_2atmpS2276 = _M0L6_2atmpS2277 & 3u;
  #line 232 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS2275 = _M0MPC14uint4UInt10to__uint64(_M0L6_2atmpS2276);
  _M0L1aS455 = _M0L6_2atmpS2274 + _M0L6_2atmpS2275;
  _M0L6_2atmpS2273 = _M0Lm5high1S453;
  #line 233 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L1bS456
  = _M0FPB13shiftright128(_M0L3sumS452, _M0L6_2atmpS2273, _M0L5deltaS454);
  return (struct _M0TPB8Pow5Pair){.$0 = _M0L1aS455, .$1 = _M0L1bS456};
}

struct _M0TPB19MulShiftAll64Result _M0FPB13mulShiftAll64(
  uint64_t _M0L1mS413,
  struct _M0TPB8Pow5Pair _M0L3mulS410,
  int32_t _M0L1jS426,
  int32_t _M0L7mmShiftS428
) {
  uint64_t _M0L7_2amul0S409;
  uint64_t _M0L7_2amul1S411;
  uint64_t _M0L1mS412;
  struct _M0TPB7Umul128 _M0L7_2abindS414;
  uint64_t _M0L5_2aloS415;
  uint64_t _M0L6_2atmpS416;
  struct _M0TPB7Umul128 _M0L7_2abindS417;
  uint64_t _M0L6_2alo2S418;
  uint64_t _M0L6_2ahi2S419;
  uint64_t _M0L3midS420;
  uint64_t _M0L6_2atmpS2271;
  uint64_t _M0L2hiS421;
  uint64_t _M0L3lo2S422;
  uint64_t _M0L6_2atmpS2269;
  uint64_t _M0L6_2atmpS2270;
  uint64_t _M0L4mid2S423;
  uint64_t _M0L6_2atmpS2268;
  uint64_t _M0L3hi2S424;
  int32_t _M0L6_2atmpS2267;
  int32_t _M0L6_2atmpS2266;
  uint64_t _M0L2vpS425;
  uint64_t _M0Lm2vmS427;
  int32_t _M0L6_2atmpS2265;
  int32_t _M0L6_2atmpS2264;
  uint64_t _M0L2vrS438;
  uint64_t _M0L6_2atmpS2263;
  #line 129 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2amul0S409 = _M0L3mulS410.$0;
  _M0L7_2amul1S411 = _M0L3mulS410.$1;
  _M0L1mS412 = _M0L1mS413 << 1;
  #line 137 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS414 = _M0FPB7umul128(_M0L1mS412, _M0L7_2amul0S409);
  _M0L5_2aloS415 = _M0L7_2abindS414.$0;
  _M0L6_2atmpS416 = _M0L7_2abindS414.$1;
  #line 138 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS417 = _M0FPB7umul128(_M0L1mS412, _M0L7_2amul1S411);
  _M0L6_2alo2S418 = _M0L7_2abindS417.$0;
  _M0L6_2ahi2S419 = _M0L7_2abindS417.$1;
  _M0L3midS420 = _M0L6_2atmpS416 + _M0L6_2alo2S418;
  if (_M0L3midS420 < _M0L6_2atmpS416) {
    _M0L6_2atmpS2271 = 1ull;
  } else {
    _M0L6_2atmpS2271 = 0ull;
  }
  _M0L2hiS421 = _M0L6_2ahi2S419 + _M0L6_2atmpS2271;
  _M0L3lo2S422 = _M0L5_2aloS415 + _M0L7_2amul0S409;
  _M0L6_2atmpS2269 = _M0L3midS420 + _M0L7_2amul1S411;
  if (_M0L3lo2S422 < _M0L5_2aloS415) {
    _M0L6_2atmpS2270 = 1ull;
  } else {
    _M0L6_2atmpS2270 = 0ull;
  }
  _M0L4mid2S423 = _M0L6_2atmpS2269 + _M0L6_2atmpS2270;
  if (_M0L4mid2S423 < _M0L3midS420) {
    _M0L6_2atmpS2268 = 1ull;
  } else {
    _M0L6_2atmpS2268 = 0ull;
  }
  _M0L3hi2S424 = _M0L2hiS421 + _M0L6_2atmpS2268;
  _M0L6_2atmpS2267 = _M0L1jS426 - 64;
  _M0L6_2atmpS2266 = _M0L6_2atmpS2267 - 1;
  #line 144 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L2vpS425
  = _M0FPB13shiftright128(_M0L4mid2S423, _M0L3hi2S424, _M0L6_2atmpS2266);
  _M0Lm2vmS427 = 0ull;
  if (_M0L7mmShiftS428) {
    uint64_t _M0L3lo3S429 = _M0L5_2aloS415 - _M0L7_2amul0S409;
    uint64_t _M0L6_2atmpS2253 = _M0L3midS420 - _M0L7_2amul1S411;
    uint64_t _M0L6_2atmpS2254;
    uint64_t _M0L4mid3S430;
    uint64_t _M0L6_2atmpS2252;
    uint64_t _M0L3hi3S431;
    int32_t _M0L6_2atmpS2251;
    int32_t _M0L6_2atmpS2250;
    if (_M0L5_2aloS415 < _M0L3lo3S429) {
      _M0L6_2atmpS2254 = 1ull;
    } else {
      _M0L6_2atmpS2254 = 0ull;
    }
    _M0L4mid3S430 = _M0L6_2atmpS2253 - _M0L6_2atmpS2254;
    if (_M0L3midS420 < _M0L4mid3S430) {
      _M0L6_2atmpS2252 = 1ull;
    } else {
      _M0L6_2atmpS2252 = 0ull;
    }
    _M0L3hi3S431 = _M0L2hiS421 - _M0L6_2atmpS2252;
    _M0L6_2atmpS2251 = _M0L1jS426 - 64;
    _M0L6_2atmpS2250 = _M0L6_2atmpS2251 - 1;
    #line 150 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0Lm2vmS427
    = _M0FPB13shiftright128(_M0L4mid3S430, _M0L3hi3S431, _M0L6_2atmpS2250);
  } else {
    uint64_t _M0L3lo3S432 = _M0L5_2aloS415 + _M0L5_2aloS415;
    uint64_t _M0L6_2atmpS2261 = _M0L3midS420 + _M0L3midS420;
    uint64_t _M0L6_2atmpS2262;
    uint64_t _M0L4mid3S433;
    uint64_t _M0L6_2atmpS2259;
    uint64_t _M0L6_2atmpS2260;
    uint64_t _M0L3hi3S434;
    uint64_t _M0L3lo4S435;
    uint64_t _M0L6_2atmpS2257;
    uint64_t _M0L6_2atmpS2258;
    uint64_t _M0L4mid4S436;
    uint64_t _M0L6_2atmpS2256;
    uint64_t _M0L3hi4S437;
    int32_t _M0L6_2atmpS2255;
    if (_M0L3lo3S432 < _M0L5_2aloS415) {
      _M0L6_2atmpS2262 = 1ull;
    } else {
      _M0L6_2atmpS2262 = 0ull;
    }
    _M0L4mid3S433 = _M0L6_2atmpS2261 + _M0L6_2atmpS2262;
    _M0L6_2atmpS2259 = _M0L2hiS421 + _M0L2hiS421;
    if (_M0L4mid3S433 < _M0L3midS420) {
      _M0L6_2atmpS2260 = 1ull;
    } else {
      _M0L6_2atmpS2260 = 0ull;
    }
    _M0L3hi3S434 = _M0L6_2atmpS2259 + _M0L6_2atmpS2260;
    _M0L3lo4S435 = _M0L3lo3S432 - _M0L7_2amul0S409;
    _M0L6_2atmpS2257 = _M0L4mid3S433 - _M0L7_2amul1S411;
    if (_M0L3lo3S432 < _M0L3lo4S435) {
      _M0L6_2atmpS2258 = 1ull;
    } else {
      _M0L6_2atmpS2258 = 0ull;
    }
    _M0L4mid4S436 = _M0L6_2atmpS2257 - _M0L6_2atmpS2258;
    if (_M0L4mid3S433 < _M0L4mid4S436) {
      _M0L6_2atmpS2256 = 1ull;
    } else {
      _M0L6_2atmpS2256 = 0ull;
    }
    _M0L3hi4S437 = _M0L3hi3S434 - _M0L6_2atmpS2256;
    _M0L6_2atmpS2255 = _M0L1jS426 - 64;
    #line 158 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0Lm2vmS427
    = _M0FPB13shiftright128(_M0L4mid4S436, _M0L3hi4S437, _M0L6_2atmpS2255);
  }
  _M0L6_2atmpS2265 = _M0L1jS426 - 64;
  _M0L6_2atmpS2264 = _M0L6_2atmpS2265 - 1;
  #line 160 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L2vrS438
  = _M0FPB13shiftright128(_M0L3midS420, _M0L2hiS421, _M0L6_2atmpS2264);
  _M0L6_2atmpS2263 = _M0Lm2vmS427;
  return (struct _M0TPB19MulShiftAll64Result){.$0 = _M0L2vrS438,
                                                .$1 = _M0L2vpS425,
                                                .$2 = _M0L6_2atmpS2263};
}

int32_t _M0FPB18multipleOfPowerOf2(
  uint64_t _M0L5valueS407,
  int32_t _M0L1pS408
) {
  uint64_t _M0L6_2atmpS2249;
  uint64_t _M0L6_2atmpS2248;
  uint64_t _M0L6_2atmpS2247;
  #line 124 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS2249 = 1ull << (_M0L1pS408 & 63);
  _M0L6_2atmpS2248 = _M0L6_2atmpS2249 - 1ull;
  _M0L6_2atmpS2247 = _M0L5valueS407 & _M0L6_2atmpS2248;
  return _M0L6_2atmpS2247 == 0ull;
}

int32_t _M0FPB18multipleOfPowerOf5(
  uint64_t _M0L5valueS405,
  int32_t _M0L1pS406
) {
  int32_t _M0L6_2atmpS2246;
  #line 119 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  #line 120 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS2246 = _M0FPB10pow5Factor(_M0L5valueS405);
  return _M0L6_2atmpS2246 >= _M0L1pS406;
}

int32_t _M0FPB10pow5Factor(uint64_t _M0L5valueS400) {
  uint64_t _M0L6_2atmpS2237;
  uint64_t _M0L6_2atmpS2238;
  uint64_t _M0L6_2atmpS2239;
  uint64_t _M0L6_2atmpS2240;
  uint64_t _M0L6_2atmpS2245;
  int32_t _M0L5countS401;
  uint64_t _M0L1vS402;
  #line 94 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS2237 = _M0L5valueS400 % 5ull;
  if (_M0L6_2atmpS2237 != 0ull) {
    return 0;
  }
  _M0L6_2atmpS2238 = _M0L5valueS400 % 25ull;
  if (_M0L6_2atmpS2238 != 0ull) {
    return 1;
  }
  _M0L6_2atmpS2239 = _M0L5valueS400 % 125ull;
  if (_M0L6_2atmpS2239 != 0ull) {
    return 2;
  }
  _M0L6_2atmpS2240 = _M0L5valueS400 % 625ull;
  if (_M0L6_2atmpS2240 != 0ull) {
    return 3;
  }
  _M0L6_2atmpS2245 = _M0L5valueS400 / 625ull;
  _M0L5countS401 = 4;
  _M0L1vS402 = _M0L6_2atmpS2245;
  while (1) {
    if (_M0L1vS402 > 0ull) {
      uint64_t _M0L6_2atmpS2241 = _M0L1vS402 % 5ull;
      int32_t _M0L6_2atmpS2242;
      uint64_t _M0L6_2atmpS2243;
      if (_M0L6_2atmpS2241 != 0ull) {
        return _M0L5countS401;
      }
      _M0L6_2atmpS2242 = _M0L5countS401 + 1;
      _M0L6_2atmpS2243 = _M0L1vS402 / 5ull;
      _M0L5countS401 = _M0L6_2atmpS2242;
      _M0L1vS402 = _M0L6_2atmpS2243;
      continue;
    } else {
      struct _M0TPB13StringBuilder* _M0L18_2astring__builderS404;
      moonbit_string_t _M0L6_2atmpS2244;
      int32_t _result_5516;
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0L18_2astring__builderS404
      = _M0MPB13StringBuilder21StringBuilder_2einner(25);
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS404, (moonbit_string_t)moonbit_string_literal_5.data);
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0MPB13StringBuilder13write__objectGmE(_M0L18_2astring__builderS404, _M0L5valueS400);
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0L6_2atmpS2244
      = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS404);
      moonbit_decref(_M0L18_2astring__builderS404);
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _result_5516 = _M0FPC15abort5abortGiE(_M0L6_2atmpS2244);
      moonbit_decref(_M0L6_2atmpS2244);
      return _result_5516;
    }
    break;
  }
}

uint64_t _M0FPB13shiftright128(
  uint64_t _M0L2loS399,
  uint64_t _M0L2hiS397,
  int32_t _M0L4distS398
) {
  int32_t _M0L6_2atmpS2236;
  uint64_t _M0L6_2atmpS2234;
  uint64_t _M0L6_2atmpS2235;
  #line 89 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS2236 = 64 - _M0L4distS398;
  _M0L6_2atmpS2234 = _M0L2hiS397 << (_M0L6_2atmpS2236 & 63);
  _M0L6_2atmpS2235 = _M0L2loS399 >> (_M0L4distS398 & 63);
  return _M0L6_2atmpS2234 | _M0L6_2atmpS2235;
}

struct _M0TPB7Umul128 _M0FPB7umul128(
  uint64_t _M0L1aS387,
  uint64_t _M0L1bS390
) {
  uint64_t _M0L3aLoS386;
  uint64_t _M0L3aHiS388;
  uint64_t _M0L3bLoS389;
  uint64_t _M0L3bHiS391;
  uint64_t _M0L1xS392;
  uint64_t _M0L6_2atmpS2232;
  uint64_t _M0L6_2atmpS2233;
  uint64_t _M0L1yS393;
  uint64_t _M0L6_2atmpS2230;
  uint64_t _M0L6_2atmpS2231;
  uint64_t _M0L1zS394;
  uint64_t _M0L6_2atmpS2228;
  uint64_t _M0L6_2atmpS2229;
  uint64_t _M0L6_2atmpS2226;
  uint64_t _M0L6_2atmpS2227;
  uint64_t _M0L1wS395;
  uint64_t _M0L2loS396;
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L3aLoS386 = _M0L1aS387 & 4294967295ull;
  _M0L3aHiS388 = _M0L1aS387 >> 32;
  _M0L3bLoS389 = _M0L1bS390 & 4294967295ull;
  _M0L3bHiS391 = _M0L1bS390 >> 32;
  _M0L1xS392 = _M0L3aLoS386 * _M0L3bLoS389;
  _M0L6_2atmpS2232 = _M0L3aHiS388 * _M0L3bLoS389;
  _M0L6_2atmpS2233 = _M0L1xS392 >> 32;
  _M0L1yS393 = _M0L6_2atmpS2232 + _M0L6_2atmpS2233;
  _M0L6_2atmpS2230 = _M0L3aLoS386 * _M0L3bHiS391;
  _M0L6_2atmpS2231 = _M0L1yS393 & 4294967295ull;
  _M0L1zS394 = _M0L6_2atmpS2230 + _M0L6_2atmpS2231;
  _M0L6_2atmpS2228 = _M0L3aHiS388 * _M0L3bHiS391;
  _M0L6_2atmpS2229 = _M0L1yS393 >> 32;
  _M0L6_2atmpS2226 = _M0L6_2atmpS2228 + _M0L6_2atmpS2229;
  _M0L6_2atmpS2227 = _M0L1zS394 >> 32;
  _M0L1wS395 = _M0L6_2atmpS2226 + _M0L6_2atmpS2227;
  _M0L2loS396 = _M0L1aS387 * _M0L1bS390;
  return (struct _M0TPB7Umul128){.$0 = _M0L2loS396, .$1 = _M0L1wS395};
}

moonbit_string_t _M0FPB19string__from__bytes(
  moonbit_bytes_t _M0L5bytesS384,
  int32_t _M0L4fromS381,
  int32_t _M0L2toS380
) {
  int32_t _M0L3lenS379;
  int32_t _M0L6_2atmpS2225;
  uint16_t* _M0L6bufferS382;
  int32_t _M0L1iS383;
  #line 52 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L3lenS379 = _M0L2toS380 - _M0L4fromS381;
  #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS2225 = _M0IPC16uint166UInt16PB7Default7default();
  _M0L6bufferS382
  = (uint16_t*)moonbit_make_string(_M0L3lenS379, _M0L6_2atmpS2225);
  _M0L1iS383 = 0;
  while (1) {
    if (_M0L1iS383 < _M0L3lenS379) {
      int32_t _M0L6_2atmpS2223 = _M0L4fromS381 + _M0L1iS383;
      int32_t _M0L6_2atmpS2222;
      int32_t _M0L6_2atmpS2221;
      int32_t _M0L6_2atmpS2224;
      if (
        _M0L6_2atmpS2223 < 0
        || _M0L6_2atmpS2223 >= Moonbit_array_length(_M0L5bytesS384)
      ) {
        #line 56 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6_2atmpS2222 = (int32_t)_M0L5bytesS384[_M0L6_2atmpS2223];
      _M0L6_2atmpS2221 = (uint16_t)_M0L6_2atmpS2222;
      if (
        _M0L1iS383 < 0 || _M0L1iS383 >= Moonbit_array_length(_M0L6bufferS382)
      ) {
        #line 56 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6bufferS382[_M0L1iS383] = _M0L6_2atmpS2221;
      _M0L6_2atmpS2224 = _M0L1iS383 + 1;
      _M0L1iS383 = _M0L6_2atmpS2224;
      continue;
    }
    break;
  }
  return _M0L6bufferS382;
}

int32_t _M0FPB9log10Pow2(int32_t _M0L1eS378) {
  int32_t _M0L6_2atmpS2220;
  uint32_t _M0L6_2atmpS2219;
  uint32_t _M0L6_2atmpS2218;
  #line 44 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS2220 = _M0L1eS378 * 78913;
  _M0L6_2atmpS2219 = *(uint32_t*)&_M0L6_2atmpS2220;
  _M0L6_2atmpS2218 = _M0L6_2atmpS2219 >> 18;
  return *(int32_t*)&_M0L6_2atmpS2218;
}

int32_t _M0FPB9log10Pow5(int32_t _M0L1eS377) {
  int32_t _M0L6_2atmpS2217;
  uint32_t _M0L6_2atmpS2216;
  uint32_t _M0L6_2atmpS2215;
  #line 37 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS2217 = _M0L1eS377 * 732923;
  _M0L6_2atmpS2216 = *(uint32_t*)&_M0L6_2atmpS2217;
  _M0L6_2atmpS2215 = _M0L6_2atmpS2216 >> 20;
  return *(int32_t*)&_M0L6_2atmpS2215;
}

moonbit_string_t _M0FPB18copy__special__str(
  int32_t _M0L4signS375,
  int32_t _M0L8exponentS376,
  int32_t _M0L8mantissaS373
) {
  moonbit_string_t _M0L1sS374;
  moonbit_string_t _result_5519;
  #line 23 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  if (_M0L8mantissaS373) {
    return (moonbit_string_t)moonbit_string_literal_6.data;
  }
  if (_M0L4signS375) {
    _M0L1sS374 = (moonbit_string_t)moonbit_string_literal_7.data;
  } else {
    _M0L1sS374 = (moonbit_string_t)moonbit_string_literal_0.data;
  }
  if (_M0L8exponentS376) {
    moonbit_string_t _result_5518;
    #line 29 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _result_5518
    = moonbit_add_string(_M0L1sS374, (moonbit_string_t)moonbit_string_literal_8.data);
    moonbit_decref(_M0L1sS374);
    return _result_5518;
  }
  #line 31 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _result_5519
  = moonbit_add_string(_M0L1sS374, (moonbit_string_t)moonbit_string_literal_9.data);
  moonbit_decref(_M0L1sS374);
  return _result_5519;
}

int32_t _M0FPB8pow5bits(int32_t _M0L1eS372) {
  int32_t _M0L6_2atmpS2214;
  uint32_t _M0L6_2atmpS2213;
  uint32_t _M0L6_2atmpS2212;
  int32_t _M0L6_2atmpS2211;
  #line 18 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS2214 = _M0L1eS372 * 1217359;
  _M0L6_2atmpS2213 = *(uint32_t*)&_M0L6_2atmpS2214;
  _M0L6_2atmpS2212 = _M0L6_2atmpS2213 >> 19;
  _M0L6_2atmpS2211 = *(int32_t*)&_M0L6_2atmpS2212;
  return _M0L6_2atmpS2211 + 1;
}

int32_t _M0MPC16double6Double7to__int(double _M0L4selfS371) {
  #line 43 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_to_int.mbt"
  if (_M0L4selfS371 != _M0L4selfS371) {
    return 0;
  } else if (_M0L4selfS371 >= 0x1.fffffffcp+30) {
    return 2147483647;
  } else if (_M0L4selfS371 <= -0x1p+31) {
    return (int32_t)0x80000000;
  } else {
    return (int32_t)_M0L4selfS371;
  }
}

int64_t _M0MPC16double6Double9to__int64(double _M0L4selfS370) {
  #line 46 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_to_int64_native.mbt"
  if (_M0L4selfS370 != _M0L4selfS370) {
    return 0ll;
  } else if (_M0L4selfS370 >= 0x1p+63) {
    return 9223372036854775807ll;
  } else if (_M0L4selfS370 <= -0x1p+63) {
    return (int64_t)0x8000000000000000ll;
  } else {
    return (int64_t)_M0L4selfS370;
  }
}

struct _M0TPB5ArrayGfE* _M0MPC15array5Array20unsafe__make__uninitGfE(
  int32_t _M0L3lenS366
) {
  float* _M0L6_2atmpS2207;
  struct _M0TPB5ArrayGfE* _block_5520;
  #line 30 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS2207 = (float*)moonbit_make_float_array_raw(_M0L3lenS366);
  _block_5520
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_block_5520)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 21, 0);
  _block_5520->$0 = _M0L6_2atmpS2207;
  _block_5520->$1 = _M0L3lenS366;
  return _block_5520;
}

struct _M0TPB5ArrayGbE* _M0MPC15array5Array20unsafe__make__uninitGbE(
  int32_t _M0L3lenS367
) {
  uint8_t* _M0L6_2atmpS2208;
  struct _M0TPB5ArrayGbE* _block_5521;
  #line 30 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS2208 = (uint8_t*)moonbit_make_bytes_raw(_M0L3lenS367);
  _block_5521
  = (struct _M0TPB5ArrayGbE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGbE));
  Moonbit_object_header(_block_5521)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 73, 0);
  _block_5521->$0 = _M0L6_2atmpS2208;
  _block_5521->$1 = _M0L3lenS367;
  return _block_5521;
}

struct _M0TPB5ArrayGiE* _M0MPC15array5Array20unsafe__make__uninitGiE(
  int32_t _M0L3lenS368
) {
  int32_t* _M0L6_2atmpS2209;
  struct _M0TPB5ArrayGiE* _block_5522;
  #line 30 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS2209 = (int32_t*)moonbit_make_int32_array_raw(_M0L3lenS368);
  _block_5522
  = (struct _M0TPB5ArrayGiE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGiE));
  Moonbit_object_header(_block_5522)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 24, 0);
  _block_5522->$0 = _M0L6_2atmpS2209;
  _block_5522->$1 = _M0L3lenS368;
  return _block_5522;
}

struct _M0TPB5ArrayGRPB5ArrayGfEE* _M0MPC15array5Array20unsafe__make__uninitGRPB5ArrayGfEE(
  int32_t _M0L3lenS369
) {
  struct _M0TPB5ArrayGfE** _M0L6_2atmpS2210;
  struct _M0TPB5ArrayGRPB5ArrayGfEE* _block_5523;
  #line 30 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS2210
  = (struct _M0TPB5ArrayGfE**)moonbit_make_ref_array(_M0L3lenS369, 0);
  _block_5523
  = (struct _M0TPB5ArrayGRPB5ArrayGfEE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGRPB5ArrayGfEE));
  Moonbit_object_header(_block_5523)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 96, 0);
  _block_5523->$0 = _M0L6_2atmpS2210;
  _block_5523->$1 = _M0L3lenS369;
  return _block_5523;
}

uint64_t _M0MPC15array13ReadOnlyArray2atGmE(
  uint64_t* _M0L4selfS362,
  int32_t _M0L5indexS363
) {
  uint64_t* _M0L6_2atmpS2205;
  #line 38 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\readonlyarray.mbt"
  _M0L6_2atmpS2205 = _M0L4selfS362;
  if (
    _M0L5indexS363 < 0
    || _M0L5indexS363 >= Moonbit_array_length(_M0L6_2atmpS2205)
  ) {
    #line 40 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\readonlyarray.mbt"
    moonbit_panic();
  }
  return (uint64_t)_M0L6_2atmpS2205[_M0L5indexS363];
}

uint32_t _M0MPC15array13ReadOnlyArray2atGjE(
  uint32_t* _M0L4selfS364,
  int32_t _M0L5indexS365
) {
  uint32_t* _M0L6_2atmpS2206;
  #line 38 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\readonlyarray.mbt"
  _M0L6_2atmpS2206 = _M0L4selfS364;
  if (
    _M0L5indexS365 < 0
    || _M0L5indexS365 >= Moonbit_array_length(_M0L6_2atmpS2206)
  ) {
    #line 40 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\readonlyarray.mbt"
    moonbit_panic();
  }
  return (uint32_t)_M0L6_2atmpS2206[_M0L5indexS365];
}

moonbit_string_t _M0IPC16uint646UInt64PB4Show10to__string(
  uint64_t _M0L4selfS361
) {
  #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  #line 51 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  return _M0MPC16uint646UInt6418to__string_2einner(_M0L4selfS361, 10);
}

moonbit_string_t _M0IPC13int3IntPB4Show10to__string(int32_t _M0L4selfS360) {
  #line 35 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  #line 36 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  return _M0MPC13int3Int18to__string_2einner(_M0L4selfS360, 10);
}

uint64_t _M0MPC14uint4UInt10to__uint64(uint32_t _M0L4selfS359) {
  #line 2576 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  return (uint64_t)_M0L4selfS359;
}

int32_t _M0MPC15array5Array4pushGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS353,
  int32_t _M0L5valueS355
) {
  int32_t _M0L3lenS2191;
  int32_t* _M0L6_2atmpS2193;
  int32_t _M0L6_2atmpS2192;
  int32_t _M0L6lengthS354;
  int32_t* _M0L3bufS2196;
  int32_t _M0L6_2atmpS2197;
  #line 406 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L3lenS2191 = _M0L4selfS353->$1;
  #line 408 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS2193 = _M0MPC15array5Array6bufferGiE(_M0L4selfS353);
  _M0L6_2atmpS2192 = Moonbit_array_length(_M0L6_2atmpS2193);
  moonbit_decref(_M0L6_2atmpS2193);
  if (_M0L3lenS2191 == _M0L6_2atmpS2192) {
    int32_t _M0L3lenS2195 = _M0L4selfS353->$1;
    int32_t _M0L6_2atmpS2194 = _M0L3lenS2195 + 1;
    #line 409 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0MPC15array5Array7reallocGiE(_M0L4selfS353, _M0L6_2atmpS2194);
  }
  _M0L6lengthS354 = _M0L4selfS353->$1;
  _M0L3bufS2196 = _M0L4selfS353->$0;
  _M0L3bufS2196[_M0L6lengthS354] = _M0L5valueS355;
  _M0L6_2atmpS2197 = _M0L6lengthS354 + 1;
  _M0L4selfS353->$1 = _M0L6_2atmpS2197;
  return 0;
}

int32_t _M0MPC15array5Array4pushGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS356,
  float _M0L5valueS358
) {
  int32_t _M0L3lenS2198;
  float* _M0L6_2atmpS2200;
  int32_t _M0L6_2atmpS2199;
  int32_t _M0L6lengthS357;
  float* _M0L3bufS2203;
  int32_t _M0L6_2atmpS2204;
  #line 406 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L3lenS2198 = _M0L4selfS356->$1;
  #line 408 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS2200 = _M0MPC15array5Array6bufferGfE(_M0L4selfS356);
  _M0L6_2atmpS2199 = Moonbit_array_length(_M0L6_2atmpS2200);
  moonbit_decref(_M0L6_2atmpS2200);
  if (_M0L3lenS2198 == _M0L6_2atmpS2199) {
    int32_t _M0L3lenS2202 = _M0L4selfS356->$1;
    int32_t _M0L6_2atmpS2201 = _M0L3lenS2202 + 1;
    #line 409 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0MPC15array5Array7reallocGfE(_M0L4selfS356, _M0L6_2atmpS2201);
  }
  _M0L6lengthS357 = _M0L4selfS356->$1;
  _M0L3bufS2203 = _M0L4selfS356->$0;
  _M0L3bufS2203[_M0L6lengthS357] = _M0L5valueS358;
  _M0L6_2atmpS2204 = _M0L6lengthS357 + 1;
  _M0L4selfS356->$1 = _M0L6_2atmpS2204;
  return 0;
}

int32_t _M0MPC15array5Array7reallocGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS346,
  int32_t _M0L8requiredS348
) {
  int32_t _M0L8old__capS345;
  int32_t _M0L3lenS2189;
  int32_t _M0L8new__capS347;
  #line 302 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  #line 304 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__capS345 = _M0MPC15array5Array8capacityGiE(_M0L4selfS346);
  _M0L3lenS2189 = _M0L4selfS346->$1;
  #line 305 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__capS347
  = _M0FPB23array__growth__capacity(_M0L8old__capS345, _M0L3lenS2189, _M0L8requiredS348);
  #line 306 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0MPC15array5Array14resize__bufferGiE(_M0L4selfS346, _M0L8new__capS347);
  return 0;
}

int32_t _M0MPC15array5Array7reallocGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS350,
  int32_t _M0L8requiredS352
) {
  int32_t _M0L8old__capS349;
  int32_t _M0L3lenS2190;
  int32_t _M0L8new__capS351;
  #line 302 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  #line 304 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__capS349 = _M0MPC15array5Array8capacityGfE(_M0L4selfS350);
  _M0L3lenS2190 = _M0L4selfS350->$1;
  #line 305 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__capS351
  = _M0FPB23array__growth__capacity(_M0L8old__capS349, _M0L3lenS2190, _M0L8requiredS352);
  #line 306 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0MPC15array5Array14resize__bufferGfE(_M0L4selfS350, _M0L8new__capS351);
  return 0;
}

int32_t _M0MPC15array5Array14resize__bufferGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS334,
  int32_t _M0L13new__capacityS337
) {
  int32_t* _M0L8old__bufS333;
  int32_t _M0L3lenS335;
  int32_t _M0L9copy__lenS336;
  int32_t* _M0L8new__bufS338;
  int32_t* _M0L6_2aoldS5121;
  #line 242 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__bufS333 = _M0L4selfS334->$0;
  _M0L3lenS335 = _M0L4selfS334->$1;
  if (_M0L3lenS335 < _M0L13new__capacityS337) {
    _M0L9copy__lenS336 = _M0L3lenS335;
  } else {
    _M0L9copy__lenS336 = _M0L13new__capacityS337;
  }
  moonbit_incref(_M0L8old__bufS333);
  #line 249 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__bufS338
  = _M0MPB18UninitializedArray23make__and__blit_2einnerGiE(_M0L8old__bufS333, _M0L13new__capacityS337, _M0L9copy__lenS336, 0, 0);
  _M0L6_2aoldS5121 = _M0L4selfS334->$0;
  moonbit_decref(_M0L6_2aoldS5121);
  _M0L4selfS334->$0 = _M0L8new__bufS338;
  return 0;
}

int32_t _M0MPC15array5Array14resize__bufferGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS340,
  int32_t _M0L13new__capacityS343
) {
  float* _M0L8old__bufS339;
  int32_t _M0L3lenS341;
  int32_t _M0L9copy__lenS342;
  float* _M0L8new__bufS344;
  float* _M0L6_2aoldS5122;
  #line 242 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__bufS339 = _M0L4selfS340->$0;
  _M0L3lenS341 = _M0L4selfS340->$1;
  if (_M0L3lenS341 < _M0L13new__capacityS343) {
    _M0L9copy__lenS342 = _M0L3lenS341;
  } else {
    _M0L9copy__lenS342 = _M0L13new__capacityS343;
  }
  moonbit_incref(_M0L8old__bufS339);
  #line 249 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__bufS344
  = _M0MPB18UninitializedArray23make__and__blit_2einnerGfE(_M0L8old__bufS339, _M0L13new__capacityS343, _M0L9copy__lenS342, 0, 0);
  _M0L6_2aoldS5122 = _M0L4selfS340->$0;
  moonbit_decref(_M0L6_2aoldS5122);
  _M0L4selfS340->$0 = _M0L8new__bufS344;
  return 0;
}

int32_t _M0MPC15array5Array8capacityGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS331
) {
  int32_t* _M0L6_2atmpS2187;
  int32_t _result_5524;
  #line 132 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L6_2atmpS2187 = _M0MPC15array5Array6bufferGiE(_M0L4selfS331);
  _result_5524 = Moonbit_array_length(_M0L6_2atmpS2187);
  moonbit_decref(_M0L6_2atmpS2187);
  return _result_5524;
}

int32_t _M0MPC15array5Array8capacityGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS332
) {
  float* _M0L6_2atmpS2188;
  int32_t _result_5525;
  #line 132 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L6_2atmpS2188 = _M0MPC15array5Array6bufferGfE(_M0L4selfS332);
  _result_5525 = Moonbit_array_length(_M0L6_2atmpS2188);
  moonbit_decref(_M0L6_2atmpS2188);
  return _result_5525;
}

int32_t _M0FPB23array__growth__capacity(
  int32_t _M0L7currentS327,
  int32_t _M0L3lenS325,
  int32_t _M0L8requiredS324
) {
  int32_t _M0L5startS326;
  int32_t _M0L5spaceS328;
  #line 200 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  if (_M0L8requiredS324 < _M0L3lenS325) {
    #line 203 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_10.data);
  }
  if (_M0L7currentS327 == 0) {
    _M0L5startS326 = 8;
  } else {
    _M0L5startS326 = _M0L7currentS327;
  }
  _M0L5spaceS328 = _M0L5startS326;
  while (1) {
    if (_M0L5spaceS328 < _M0L8requiredS324) {
      int32_t _M0L4nextS329 = _M0L5spaceS328 * 2;
      if (_M0L4nextS329 <= _M0L5spaceS328) {
        return _M0L8requiredS324;
      }
      _M0L5spaceS328 = _M0L4nextS329;
      continue;
    } else {
      return _M0L5spaceS328;
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

int32_t _M0MPC15array5Array6lengthGiE(struct _M0TPB5ArrayGiE* _M0L4selfS323) {
  #line 147 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  return _M0L4selfS323->$1;
}

float* _M0MPC15array5Array6bufferGfE(struct _M0TPB5ArrayGfE* _M0L4selfS316) {
  float* _M0L8_2afieldS5123;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS5123 = _M0L4selfS316->$0;
  moonbit_incref(_M0L8_2afieldS5123);
  return _M0L8_2afieldS5123;
}

int32_t* _M0MPC15array5Array6bufferGiE(struct _M0TPB5ArrayGiE* _M0L4selfS317) {
  int32_t* _M0L8_2afieldS5124;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS5124 = _M0L4selfS317->$0;
  moonbit_incref(_M0L8_2afieldS5124);
  return _M0L8_2afieldS5124;
}

struct _M0TP26RiantR8snn__mbt14SpikingSynapse** _M0MPC15array5Array6bufferGRP26RiantR8snn__mbt14SpikingSynapseE(
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt14SpikingSynapseE* _M0L4selfS318
) {
  struct _M0TP26RiantR8snn__mbt14SpikingSynapse** _M0L8_2afieldS5125;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS5125 = _M0L4selfS318->$0;
  moonbit_incref(_M0L8_2afieldS5125);
  return _M0L8_2afieldS5125;
}

uint8_t* _M0MPC15array5Array6bufferGbE(struct _M0TPB5ArrayGbE* _M0L4selfS319) {
  uint8_t* _M0L8_2afieldS5126;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS5126 = _M0L4selfS319->$0;
  moonbit_incref(_M0L8_2afieldS5126);
  return _M0L8_2afieldS5126;
}

struct _M0TPB5ArrayGfE** _M0MPC15array5Array6bufferGRPB5ArrayGfEE(
  struct _M0TPB5ArrayGRPB5ArrayGfEE* _M0L4selfS320
) {
  struct _M0TPB5ArrayGfE** _M0L8_2afieldS5127;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS5127 = _M0L4selfS320->$0;
  moonbit_incref(_M0L8_2afieldS5127);
  return _M0L8_2afieldS5127;
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
  int32_t _M0L3endS2185;
  int32_t _M0L5startS2186;
  int32_t _M0L8str__lenS311;
  int32_t _M0L3lenS2184;
  int32_t _M0L8requiredS313;
  uint16_t* _M0L4dataS2177;
  int32_t _M0L6_2atmpS2176;
  int32_t _if__result_5527;
  uint16_t* _M0L4dataS2178;
  int32_t _M0L3lenS2179;
  moonbit_string_t _M0L6_2atmpS2180;
  int32_t _M0L6_2atmpS2181;
  int32_t _M0L3lenS2183;
  int32_t _M0L6_2atmpS2182;
  #line 158 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L3endS2185 = _M0L3strS312.$2;
  _M0L5startS2186 = _M0L3strS312.$1;
  _M0L8str__lenS311 = _M0L3endS2185 - _M0L5startS2186;
  if (_M0L8str__lenS311 == 0) {
    return 0;
  }
  _M0L3lenS2184 = _M0L4selfS314->$1;
  _M0L8requiredS313 = _M0L3lenS2184 + _M0L8str__lenS311;
  _M0L4dataS2177 = _M0L4selfS314->$0;
  _M0L6_2atmpS2176 = Moonbit_array_length(_M0L4dataS2177);
  if (_M0L8requiredS313 > _M0L6_2atmpS2176) {
    _if__result_5527 = 1;
  } else {
    int32_t _M0L3lenS2175 = _M0L4selfS314->$1;
    _if__result_5527 = _M0L8requiredS313 < _M0L3lenS2175;
  }
  if (_if__result_5527) {
    #line 168 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0MPB13StringBuilder4grow(_M0L4selfS314, _M0L8requiredS313);
  }
  _M0L4dataS2178 = _M0L4selfS314->$0;
  _M0L3lenS2179 = _M0L4selfS314->$1;
  moonbit_incref(_M0L4dataS2178);
  #line 172 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L6_2atmpS2180 = _M0MPC16string10StringView4data(_M0L3strS312);
  #line 173 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L6_2atmpS2181 = _M0MPC16string10StringView13start__offset(_M0L3strS312);
  #line 170 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0MPC15array10FixedArray26unsafe__blit__from__string(_M0L4dataS2178, _M0L3lenS2179, _M0L6_2atmpS2180, _M0L6_2atmpS2181, _M0L8str__lenS311);
  moonbit_decref(_M0L4dataS2178);
  moonbit_decref(_M0L6_2atmpS2180);
  _M0L3lenS2183 = _M0L4selfS314->$1;
  _M0L6_2atmpS2182 = _M0L3lenS2183 + _M0L8str__lenS311;
  _M0L4selfS314->$1 = _M0L6_2atmpS2182;
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
    int64_t _M0L6_2atmpS2174 = -_M0L4selfS286;
    _M0L3numS288 = *(uint64_t*)&_M0L6_2atmpS2174;
  } else {
    _M0L3numS288 = *(uint64_t*)&_M0L4selfS286;
  }
  switch (_M0L5radixS285) {
    case 10: {
      int32_t _M0L10digit__lenS290;
      int32_t _M0L6_2atmpS2171;
      int32_t _M0L10total__lenS291;
      uint16_t* _M0L6bufferS292;
      int32_t _M0L12digit__startS293;
      #line 573 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS290 = _M0FPB12dec__count64(_M0L3numS288);
      if (_M0L12is__negativeS287) {
        _M0L6_2atmpS2171 = 1;
      } else {
        _M0L6_2atmpS2171 = 0;
      }
      _M0L10total__lenS291 = _M0L10digit__lenS290 + _M0L6_2atmpS2171;
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
      int32_t _M0L6_2atmpS2172;
      int32_t _M0L10total__lenS295;
      uint16_t* _M0L6bufferS296;
      int32_t _M0L12digit__startS297;
      #line 581 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS294 = _M0FPB12hex__count64(_M0L3numS288);
      if (_M0L12is__negativeS287) {
        _M0L6_2atmpS2172 = 1;
      } else {
        _M0L6_2atmpS2172 = 0;
      }
      _M0L10total__lenS295 = _M0L10digit__lenS294 + _M0L6_2atmpS2172;
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
      int32_t _M0L6_2atmpS2173;
      int32_t _M0L10total__lenS299;
      uint16_t* _M0L6bufferS300;
      int32_t _M0L12digit__startS301;
      #line 589 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS298
      = _M0FPB14radix__count64(_M0L3numS288, _M0L5radixS285);
      if (_M0L12is__negativeS287) {
        _M0L6_2atmpS2173 = 1;
      } else {
        _M0L6_2atmpS2173 = 0;
      }
      _M0L10total__lenS299 = _M0L10digit__lenS298 + _M0L6_2atmpS2173;
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
  int32_t _M0L6_2atmpS2170;
  uint64_t _M0L3numS261;
  int32_t _M0L6offsetS262;
  #line 493 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L6_2atmpS2170 = _M0L10total__lenS284 - _M0L12digit__startS272;
  _M0L3numS261 = _M0L3numS283;
  _M0L6offsetS262 = _M0L6_2atmpS2170;
  while (1) {
    if (_M0L3numS261 >= 10000ull) {
      uint64_t _M0L1tS263 = _M0L3numS261 / 10000ull;
      uint64_t _M0L6_2atmpS2147 = _M0L3numS261 % 10000ull;
      int32_t _M0L1rS264 = (int32_t)_M0L6_2atmpS2147;
      int32_t _M0L2d1S265 = _M0L1rS264 / 100;
      int32_t _M0L2d2S266 = _M0L1rS264 % 100;
      int32_t _M0L6_2atmpS2146 = _M0L2d1S265 / 10;
      int32_t _M0L6_2atmpS2145 = 48 + _M0L6_2atmpS2146;
      int32_t _M0L6d1__hiS267 = (uint16_t)_M0L6_2atmpS2145;
      int32_t _M0L6_2atmpS2144 = _M0L2d1S265 % 10;
      int32_t _M0L6_2atmpS2143 = 48 + _M0L6_2atmpS2144;
      int32_t _M0L6d1__loS268 = (uint16_t)_M0L6_2atmpS2143;
      int32_t _M0L6_2atmpS2142 = _M0L2d2S266 / 10;
      int32_t _M0L6_2atmpS2141 = 48 + _M0L6_2atmpS2142;
      int32_t _M0L6d2__hiS269 = (uint16_t)_M0L6_2atmpS2141;
      int32_t _M0L6_2atmpS2140 = _M0L2d2S266 % 10;
      int32_t _M0L6_2atmpS2139 = 48 + _M0L6_2atmpS2140;
      int32_t _M0L6d2__loS270 = (uint16_t)_M0L6_2atmpS2139;
      int32_t _M0L6_2atmpS2131 = _M0L12digit__startS272 + _M0L6offsetS262;
      int32_t _M0L6_2atmpS2130 = _M0L6_2atmpS2131 - 4;
      int32_t _M0L6_2atmpS2133;
      int32_t _M0L6_2atmpS2132;
      int32_t _M0L6_2atmpS2135;
      int32_t _M0L6_2atmpS2134;
      int32_t _M0L6_2atmpS2137;
      int32_t _M0L6_2atmpS2136;
      int32_t _M0L6_2atmpS2138;
      _M0L6bufferS271[_M0L6_2atmpS2130] = _M0L6d1__hiS267;
      _M0L6_2atmpS2133 = _M0L12digit__startS272 + _M0L6offsetS262;
      _M0L6_2atmpS2132 = _M0L6_2atmpS2133 - 3;
      _M0L6bufferS271[_M0L6_2atmpS2132] = _M0L6d1__loS268;
      _M0L6_2atmpS2135 = _M0L12digit__startS272 + _M0L6offsetS262;
      _M0L6_2atmpS2134 = _M0L6_2atmpS2135 - 2;
      _M0L6bufferS271[_M0L6_2atmpS2134] = _M0L6d2__hiS269;
      _M0L6_2atmpS2137 = _M0L12digit__startS272 + _M0L6offsetS262;
      _M0L6_2atmpS2136 = _M0L6_2atmpS2137 - 1;
      _M0L6bufferS271[_M0L6_2atmpS2136] = _M0L6d2__loS270;
      _M0L6_2atmpS2138 = _M0L6offsetS262 - 4;
      _M0L3numS261 = _M0L1tS263;
      _M0L6offsetS262 = _M0L6_2atmpS2138;
      continue;
    } else {
      int32_t _M0L6_2atmpS2169 = (int32_t)_M0L3numS261;
      int32_t _M0L9remainingS274 = _M0L6_2atmpS2169;
      int32_t _M0L6offsetS275 = _M0L6offsetS262;
      while (1) {
        if (_M0L9remainingS274 >= 100) {
          int32_t _M0L1tS276 = _M0L9remainingS274 / 100;
          int32_t _M0L1dS277 = _M0L9remainingS274 % 100;
          int32_t _M0L6_2atmpS2156 = _M0L1dS277 / 10;
          int32_t _M0L6_2atmpS2155 = 48 + _M0L6_2atmpS2156;
          int32_t _M0L5d__hiS278 = (uint16_t)_M0L6_2atmpS2155;
          int32_t _M0L6_2atmpS2154 = _M0L1dS277 % 10;
          int32_t _M0L6_2atmpS2153 = 48 + _M0L6_2atmpS2154;
          int32_t _M0L5d__loS279 = (uint16_t)_M0L6_2atmpS2153;
          int32_t _M0L6_2atmpS2149 = _M0L12digit__startS272 + _M0L6offsetS275;
          int32_t _M0L6_2atmpS2148 = _M0L6_2atmpS2149 - 2;
          int32_t _M0L6_2atmpS2151;
          int32_t _M0L6_2atmpS2150;
          int32_t _M0L6_2atmpS2152;
          _M0L6bufferS271[_M0L6_2atmpS2148] = _M0L5d__hiS278;
          _M0L6_2atmpS2151 = _M0L12digit__startS272 + _M0L6offsetS275;
          _M0L6_2atmpS2150 = _M0L6_2atmpS2151 - 1;
          _M0L6bufferS271[_M0L6_2atmpS2150] = _M0L5d__loS279;
          _M0L6_2atmpS2152 = _M0L6offsetS275 - 2;
          _M0L9remainingS274 = _M0L1tS276;
          _M0L6offsetS275 = _M0L6_2atmpS2152;
          continue;
        } else if (_M0L9remainingS274 >= 10) {
          int32_t _M0L6_2atmpS2164 = _M0L9remainingS274 / 10;
          int32_t _M0L6_2atmpS2163 = 48 + _M0L6_2atmpS2164;
          int32_t _M0L5d__hiS281 = (uint16_t)_M0L6_2atmpS2163;
          int32_t _M0L6_2atmpS2162 = _M0L9remainingS274 % 10;
          int32_t _M0L6_2atmpS2161 = 48 + _M0L6_2atmpS2162;
          int32_t _M0L5d__loS282 = (uint16_t)_M0L6_2atmpS2161;
          int32_t _M0L6_2atmpS2158 = _M0L12digit__startS272 + _M0L6offsetS275;
          int32_t _M0L6_2atmpS2157 = _M0L6_2atmpS2158 - 2;
          int32_t _M0L6_2atmpS2160;
          int32_t _M0L6_2atmpS2159;
          _M0L6bufferS271[_M0L6_2atmpS2157] = _M0L5d__hiS281;
          _M0L6_2atmpS2160 = _M0L12digit__startS272 + _M0L6offsetS275;
          _M0L6_2atmpS2159 = _M0L6_2atmpS2160 - 1;
          _M0L6bufferS271[_M0L6_2atmpS2159] = _M0L5d__loS282;
        } else {
          int32_t _M0L6_2atmpS2168 = _M0L12digit__startS272 + _M0L6offsetS275;
          int32_t _M0L6_2atmpS2165 = _M0L6_2atmpS2168 - 1;
          int32_t _M0L6_2atmpS2167 = 48 + _M0L9remainingS274;
          int32_t _M0L6_2atmpS2166 = (uint16_t)_M0L6_2atmpS2167;
          _M0L6bufferS271[_M0L6_2atmpS2165] = _M0L6_2atmpS2166;
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
  int32_t _M0L6_2atmpS2115;
  int32_t _M0L6_2atmpS2114;
  #line 462 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  #line 470 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L4baseS244 = _M0MPC13int3Int10to__uint64(_M0L5radixS245);
  _M0L6_2atmpS2115 = _M0L5radixS245 - 1;
  _M0L6_2atmpS2114 = _M0L5radixS245 & _M0L6_2atmpS2115;
  if (_M0L6_2atmpS2114 == 0) {
    int32_t _M0L5shiftS246;
    uint64_t _M0L4maskS247;
    int32_t _M0L6_2atmpS2122;
    int32_t _M0L6offsetS248;
    uint64_t _M0L1nS249;
    #line 473 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0L5shiftS246 = moonbit_ctz32(_M0L5radixS245);
    _M0L4maskS247 = _M0L4baseS244 - 1ull;
    _M0L6_2atmpS2122 = _M0L10total__lenS254 - _M0L12digit__startS252;
    _M0L6offsetS248 = _M0L6_2atmpS2122;
    _M0L1nS249 = _M0L3numS255;
    while (1) {
      if (_M0L1nS249 > 0ull) {
        uint64_t _M0L6_2atmpS2121 = _M0L1nS249 & _M0L4maskS247;
        int32_t _M0L5digitS250 = (int32_t)_M0L6_2atmpS2121;
        int32_t _M0L6_2atmpS2118 = _M0L12digit__startS252 + _M0L6offsetS248;
        int32_t _M0L6_2atmpS2116 = _M0L6_2atmpS2118 - 1;
        int32_t _M0L6_2atmpS2117 =
          ((moonbit_string_t)moonbit_string_literal_12.data)[_M0L5digitS250];
        int32_t _M0L6_2atmpS2119;
        uint64_t _M0L6_2atmpS2120;
        _M0L6bufferS251[_M0L6_2atmpS2116] = _M0L6_2atmpS2117;
        _M0L6_2atmpS2119 = _M0L6offsetS248 - 1;
        _M0L6_2atmpS2120 = _M0L1nS249 >> (_M0L5shiftS246 & 63);
        _M0L6offsetS248 = _M0L6_2atmpS2119;
        _M0L1nS249 = _M0L6_2atmpS2120;
        continue;
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS2129 = _M0L10total__lenS254 - _M0L12digit__startS252;
    int32_t _M0L6offsetS256 = _M0L6_2atmpS2129;
    uint64_t _M0L1nS257 = _M0L3numS255;
    while (1) {
      if (_M0L1nS257 > 0ull) {
        uint64_t _M0L1qS258 = _M0L1nS257 / _M0L4baseS244;
        uint64_t _M0L6_2atmpS2128 = _M0L1qS258 * _M0L4baseS244;
        uint64_t _M0L6_2atmpS2127 = _M0L1nS257 - _M0L6_2atmpS2128;
        int32_t _M0L5digitS259 = (int32_t)_M0L6_2atmpS2127;
        int32_t _M0L6_2atmpS2125 = _M0L12digit__startS252 + _M0L6offsetS256;
        int32_t _M0L6_2atmpS2123 = _M0L6_2atmpS2125 - 1;
        int32_t _M0L6_2atmpS2124 =
          ((moonbit_string_t)moonbit_string_literal_12.data)[_M0L5digitS259];
        int32_t _M0L6_2atmpS2126;
        _M0L6bufferS251[_M0L6_2atmpS2123] = _M0L6_2atmpS2124;
        _M0L6_2atmpS2126 = _M0L6offsetS256 - 1;
        _M0L6offsetS256 = _M0L6_2atmpS2126;
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
  int32_t _M0L6_2atmpS2113;
  int32_t _M0L6offsetS233;
  uint64_t _M0L1nS234;
  #line 434 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L6_2atmpS2113 = _M0L10total__lenS242 - _M0L12digit__startS239;
  _M0L6offsetS233 = _M0L6_2atmpS2113;
  _M0L1nS234 = _M0L3numS243;
  while (1) {
    if (_M0L6offsetS233 >= 2) {
      uint64_t _M0L6_2atmpS2110 = _M0L1nS234 & 255ull;
      int32_t _M0L9byte__valS235 = (int32_t)_M0L6_2atmpS2110;
      int32_t _M0L2hiS236 = _M0L9byte__valS235 / 16;
      int32_t _M0L2loS237 = _M0L9byte__valS235 % 16;
      int32_t _M0L6_2atmpS2104 = _M0L12digit__startS239 + _M0L6offsetS233;
      int32_t _M0L6_2atmpS2102 = _M0L6_2atmpS2104 - 2;
      int32_t _M0L6_2atmpS2103 =
        ((moonbit_string_t)moonbit_string_literal_12.data)[_M0L2hiS236];
      int32_t _M0L6_2atmpS2107;
      int32_t _M0L6_2atmpS2105;
      int32_t _M0L6_2atmpS2106;
      int32_t _M0L6_2atmpS2108;
      uint64_t _M0L6_2atmpS2109;
      _M0L6bufferS238[_M0L6_2atmpS2102] = _M0L6_2atmpS2103;
      _M0L6_2atmpS2107 = _M0L12digit__startS239 + _M0L6offsetS233;
      _M0L6_2atmpS2105 = _M0L6_2atmpS2107 - 1;
      _M0L6_2atmpS2106
      = ((moonbit_string_t)moonbit_string_literal_12.data)[
        _M0L2loS237
      ];
      _M0L6bufferS238[_M0L6_2atmpS2105] = _M0L6_2atmpS2106;
      _M0L6_2atmpS2108 = _M0L6offsetS233 - 2;
      _M0L6_2atmpS2109 = _M0L1nS234 >> 8;
      _M0L6offsetS233 = _M0L6_2atmpS2108;
      _M0L1nS234 = _M0L6_2atmpS2109;
      continue;
    } else if (_M0L6offsetS233 == 1) {
      uint64_t _M0L6_2atmpS2112 = _M0L1nS234 & 15ull;
      int32_t _M0L6nibbleS241 = (int32_t)_M0L6_2atmpS2112;
      int32_t _M0L6_2atmpS2111 =
        ((moonbit_string_t)moonbit_string_literal_12.data)[_M0L6nibbleS241];
      _M0L6bufferS238[_M0L12digit__startS239] = _M0L6_2atmpS2111;
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
      uint64_t _M0L6_2atmpS2100 = _M0L3numS230 / _M0L4baseS228;
      int32_t _M0L6_2atmpS2101 = _M0L5countS231 + 1;
      _M0L3numS230 = _M0L6_2atmpS2100;
      _M0L5countS231 = _M0L6_2atmpS2101;
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
    int32_t _M0L6_2atmpS2099;
    int32_t _M0L6_2atmpS2098;
    #line 412 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0L14leading__zerosS226 = moonbit_clz64(_M0L5valueS225);
    _M0L6_2atmpS2099 = 63 - _M0L14leading__zerosS226;
    _M0L6_2atmpS2098 = _M0L6_2atmpS2099 / 4;
    return _M0L6_2atmpS2098 + 1;
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
    int32_t _M0L6_2atmpS2097 = -_M0L4selfS208;
    _M0L3numS210 = *(uint32_t*)&_M0L6_2atmpS2097;
  } else {
    _M0L3numS210 = *(uint32_t*)&_M0L4selfS208;
  }
  switch (_M0L5radixS207) {
    case 10: {
      int32_t _M0L10digit__lenS212;
      int32_t _M0L6_2atmpS2094;
      int32_t _M0L10total__lenS213;
      uint16_t* _M0L6bufferS214;
      int32_t _M0L12digit__startS215;
      #line 235 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS212 = _M0FPB12dec__count32(_M0L3numS210);
      if (_M0L12is__negativeS209) {
        _M0L6_2atmpS2094 = 1;
      } else {
        _M0L6_2atmpS2094 = 0;
      }
      _M0L10total__lenS213 = _M0L10digit__lenS212 + _M0L6_2atmpS2094;
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
      int32_t _M0L6_2atmpS2095;
      int32_t _M0L10total__lenS217;
      uint16_t* _M0L6bufferS218;
      int32_t _M0L12digit__startS219;
      #line 243 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS216 = _M0FPB12hex__count32(_M0L3numS210);
      if (_M0L12is__negativeS209) {
        _M0L6_2atmpS2095 = 1;
      } else {
        _M0L6_2atmpS2095 = 0;
      }
      _M0L10total__lenS217 = _M0L10digit__lenS216 + _M0L6_2atmpS2095;
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
      int32_t _M0L6_2atmpS2096;
      int32_t _M0L10total__lenS221;
      uint16_t* _M0L6bufferS222;
      int32_t _M0L12digit__startS223;
      #line 251 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS220
      = _M0FPB14radix__count32(_M0L3numS210, _M0L5radixS207);
      if (_M0L12is__negativeS209) {
        _M0L6_2atmpS2096 = 1;
      } else {
        _M0L6_2atmpS2096 = 0;
      }
      _M0L10total__lenS221 = _M0L10digit__lenS220 + _M0L6_2atmpS2096;
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
      uint32_t _M0L6_2atmpS2092 = _M0L3numS204 / _M0L4baseS202;
      int32_t _M0L6_2atmpS2093 = _M0L5countS205 + 1;
      _M0L3numS204 = _M0L6_2atmpS2092;
      _M0L5countS205 = _M0L6_2atmpS2093;
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
    int32_t _M0L6_2atmpS2091;
    int32_t _M0L6_2atmpS2090;
    #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0L14leading__zerosS200 = moonbit_clz32(_M0L5valueS199);
    _M0L6_2atmpS2091 = 31 - _M0L14leading__zerosS200;
    _M0L6_2atmpS2090 = _M0L6_2atmpS2091 / 4;
    return _M0L6_2atmpS2090 + 1;
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
  int32_t _M0L6_2atmpS2089;
  uint32_t _M0L3numS174;
  int32_t _M0L6offsetS175;
  #line 88 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L6_2atmpS2089 = _M0L10total__lenS197 - _M0L12digit__startS185;
  _M0L3numS174 = _M0L3numS196;
  _M0L6offsetS175 = _M0L6_2atmpS2089;
  while (1) {
    if (_M0L3numS174 >= 10000u) {
      uint32_t _M0L1tS176 = _M0L3numS174 / 10000u;
      uint32_t _M0L6_2atmpS2066 = _M0L3numS174 % 10000u;
      int32_t _M0L1rS177 = *(int32_t*)&_M0L6_2atmpS2066;
      int32_t _M0L2d1S178 = _M0L1rS177 / 100;
      int32_t _M0L2d2S179 = _M0L1rS177 % 100;
      int32_t _M0L6_2atmpS2065 = _M0L2d1S178 / 10;
      int32_t _M0L6_2atmpS2064 = 48 + _M0L6_2atmpS2065;
      int32_t _M0L6d1__hiS180 = (uint16_t)_M0L6_2atmpS2064;
      int32_t _M0L6_2atmpS2063 = _M0L2d1S178 % 10;
      int32_t _M0L6_2atmpS2062 = 48 + _M0L6_2atmpS2063;
      int32_t _M0L6d1__loS181 = (uint16_t)_M0L6_2atmpS2062;
      int32_t _M0L6_2atmpS2061 = _M0L2d2S179 / 10;
      int32_t _M0L6_2atmpS2060 = 48 + _M0L6_2atmpS2061;
      int32_t _M0L6d2__hiS182 = (uint16_t)_M0L6_2atmpS2060;
      int32_t _M0L6_2atmpS2059 = _M0L2d2S179 % 10;
      int32_t _M0L6_2atmpS2058 = 48 + _M0L6_2atmpS2059;
      int32_t _M0L6d2__loS183 = (uint16_t)_M0L6_2atmpS2058;
      int32_t _M0L6_2atmpS2050 = _M0L12digit__startS185 + _M0L6offsetS175;
      int32_t _M0L6_2atmpS2049 = _M0L6_2atmpS2050 - 4;
      int32_t _M0L6_2atmpS2052;
      int32_t _M0L6_2atmpS2051;
      int32_t _M0L6_2atmpS2054;
      int32_t _M0L6_2atmpS2053;
      int32_t _M0L6_2atmpS2056;
      int32_t _M0L6_2atmpS2055;
      int32_t _M0L6_2atmpS2057;
      _M0L6bufferS184[_M0L6_2atmpS2049] = _M0L6d1__hiS180;
      _M0L6_2atmpS2052 = _M0L12digit__startS185 + _M0L6offsetS175;
      _M0L6_2atmpS2051 = _M0L6_2atmpS2052 - 3;
      _M0L6bufferS184[_M0L6_2atmpS2051] = _M0L6d1__loS181;
      _M0L6_2atmpS2054 = _M0L12digit__startS185 + _M0L6offsetS175;
      _M0L6_2atmpS2053 = _M0L6_2atmpS2054 - 2;
      _M0L6bufferS184[_M0L6_2atmpS2053] = _M0L6d2__hiS182;
      _M0L6_2atmpS2056 = _M0L12digit__startS185 + _M0L6offsetS175;
      _M0L6_2atmpS2055 = _M0L6_2atmpS2056 - 1;
      _M0L6bufferS184[_M0L6_2atmpS2055] = _M0L6d2__loS183;
      _M0L6_2atmpS2057 = _M0L6offsetS175 - 4;
      _M0L3numS174 = _M0L1tS176;
      _M0L6offsetS175 = _M0L6_2atmpS2057;
      continue;
    } else {
      int32_t _M0L6_2atmpS2088 = *(int32_t*)&_M0L3numS174;
      int32_t _M0L9remainingS187 = _M0L6_2atmpS2088;
      int32_t _M0L6offsetS188 = _M0L6offsetS175;
      while (1) {
        if (_M0L9remainingS187 >= 100) {
          int32_t _M0L1tS189 = _M0L9remainingS187 / 100;
          int32_t _M0L1dS190 = _M0L9remainingS187 % 100;
          int32_t _M0L6_2atmpS2075 = _M0L1dS190 / 10;
          int32_t _M0L6_2atmpS2074 = 48 + _M0L6_2atmpS2075;
          int32_t _M0L5d__hiS191 = (uint16_t)_M0L6_2atmpS2074;
          int32_t _M0L6_2atmpS2073 = _M0L1dS190 % 10;
          int32_t _M0L6_2atmpS2072 = 48 + _M0L6_2atmpS2073;
          int32_t _M0L5d__loS192 = (uint16_t)_M0L6_2atmpS2072;
          int32_t _M0L6_2atmpS2068 = _M0L12digit__startS185 + _M0L6offsetS188;
          int32_t _M0L6_2atmpS2067 = _M0L6_2atmpS2068 - 2;
          int32_t _M0L6_2atmpS2070;
          int32_t _M0L6_2atmpS2069;
          int32_t _M0L6_2atmpS2071;
          _M0L6bufferS184[_M0L6_2atmpS2067] = _M0L5d__hiS191;
          _M0L6_2atmpS2070 = _M0L12digit__startS185 + _M0L6offsetS188;
          _M0L6_2atmpS2069 = _M0L6_2atmpS2070 - 1;
          _M0L6bufferS184[_M0L6_2atmpS2069] = _M0L5d__loS192;
          _M0L6_2atmpS2071 = _M0L6offsetS188 - 2;
          _M0L9remainingS187 = _M0L1tS189;
          _M0L6offsetS188 = _M0L6_2atmpS2071;
          continue;
        } else if (_M0L9remainingS187 >= 10) {
          int32_t _M0L6_2atmpS2083 = _M0L9remainingS187 / 10;
          int32_t _M0L6_2atmpS2082 = 48 + _M0L6_2atmpS2083;
          int32_t _M0L5d__hiS194 = (uint16_t)_M0L6_2atmpS2082;
          int32_t _M0L6_2atmpS2081 = _M0L9remainingS187 % 10;
          int32_t _M0L6_2atmpS2080 = 48 + _M0L6_2atmpS2081;
          int32_t _M0L5d__loS195 = (uint16_t)_M0L6_2atmpS2080;
          int32_t _M0L6_2atmpS2077 = _M0L12digit__startS185 + _M0L6offsetS188;
          int32_t _M0L6_2atmpS2076 = _M0L6_2atmpS2077 - 2;
          int32_t _M0L6_2atmpS2079;
          int32_t _M0L6_2atmpS2078;
          _M0L6bufferS184[_M0L6_2atmpS2076] = _M0L5d__hiS194;
          _M0L6_2atmpS2079 = _M0L12digit__startS185 + _M0L6offsetS188;
          _M0L6_2atmpS2078 = _M0L6_2atmpS2079 - 1;
          _M0L6bufferS184[_M0L6_2atmpS2078] = _M0L5d__loS195;
        } else {
          int32_t _M0L6_2atmpS2087 = _M0L12digit__startS185 + _M0L6offsetS188;
          int32_t _M0L6_2atmpS2084 = _M0L6_2atmpS2087 - 1;
          int32_t _M0L6_2atmpS2086 = 48 + _M0L9remainingS187;
          int32_t _M0L6_2atmpS2085 = (uint16_t)_M0L6_2atmpS2086;
          _M0L6bufferS184[_M0L6_2atmpS2084] = _M0L6_2atmpS2085;
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
  int32_t _M0L6_2atmpS2034;
  int32_t _M0L6_2atmpS2033;
  #line 57 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L4baseS157 = *(uint32_t*)&_M0L5radixS158;
  _M0L6_2atmpS2034 = _M0L5radixS158 - 1;
  _M0L6_2atmpS2033 = _M0L5radixS158 & _M0L6_2atmpS2034;
  if (_M0L6_2atmpS2033 == 0) {
    int32_t _M0L5shiftS159;
    uint32_t _M0L4maskS160;
    int32_t _M0L6_2atmpS2041;
    int32_t _M0L6offsetS161;
    uint32_t _M0L1nS162;
    #line 68 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0L5shiftS159 = moonbit_ctz32(_M0L5radixS158);
    _M0L4maskS160 = _M0L4baseS157 - 1u;
    _M0L6_2atmpS2041 = _M0L10total__lenS167 - _M0L12digit__startS165;
    _M0L6offsetS161 = _M0L6_2atmpS2041;
    _M0L1nS162 = _M0L3numS168;
    while (1) {
      if (_M0L1nS162 > 0u) {
        uint32_t _M0L6_2atmpS2040 = _M0L1nS162 & _M0L4maskS160;
        int32_t _M0L5digitS163 = *(int32_t*)&_M0L6_2atmpS2040;
        int32_t _M0L6_2atmpS2037 = _M0L12digit__startS165 + _M0L6offsetS161;
        int32_t _M0L6_2atmpS2035 = _M0L6_2atmpS2037 - 1;
        int32_t _M0L6_2atmpS2036 =
          ((moonbit_string_t)moonbit_string_literal_12.data)[_M0L5digitS163];
        int32_t _M0L6_2atmpS2038;
        uint32_t _M0L6_2atmpS2039;
        _M0L6bufferS164[_M0L6_2atmpS2035] = _M0L6_2atmpS2036;
        _M0L6_2atmpS2038 = _M0L6offsetS161 - 1;
        _M0L6_2atmpS2039 = _M0L1nS162 >> (_M0L5shiftS159 & 31);
        _M0L6offsetS161 = _M0L6_2atmpS2038;
        _M0L1nS162 = _M0L6_2atmpS2039;
        continue;
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS2048 = _M0L10total__lenS167 - _M0L12digit__startS165;
    int32_t _M0L6offsetS169 = _M0L6_2atmpS2048;
    uint32_t _M0L1nS170 = _M0L3numS168;
    while (1) {
      if (_M0L1nS170 > 0u) {
        uint32_t _M0L1qS171 = _M0L1nS170 / _M0L4baseS157;
        uint32_t _M0L6_2atmpS2047 = _M0L1qS171 * _M0L4baseS157;
        uint32_t _M0L6_2atmpS2046 = _M0L1nS170 - _M0L6_2atmpS2047;
        int32_t _M0L5digitS172 = *(int32_t*)&_M0L6_2atmpS2046;
        int32_t _M0L6_2atmpS2044 = _M0L12digit__startS165 + _M0L6offsetS169;
        int32_t _M0L6_2atmpS2042 = _M0L6_2atmpS2044 - 1;
        int32_t _M0L6_2atmpS2043 =
          ((moonbit_string_t)moonbit_string_literal_12.data)[_M0L5digitS172];
        int32_t _M0L6_2atmpS2045;
        _M0L6bufferS164[_M0L6_2atmpS2042] = _M0L6_2atmpS2043;
        _M0L6_2atmpS2045 = _M0L6offsetS169 - 1;
        _M0L6offsetS169 = _M0L6_2atmpS2045;
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
  int32_t _M0L6_2atmpS2032;
  int32_t _M0L6offsetS146;
  uint32_t _M0L1nS147;
  #line 29 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L6_2atmpS2032 = _M0L10total__lenS155 - _M0L12digit__startS152;
  _M0L6offsetS146 = _M0L6_2atmpS2032;
  _M0L1nS147 = _M0L3numS156;
  while (1) {
    if (_M0L6offsetS146 >= 2) {
      uint32_t _M0L6_2atmpS2029 = _M0L1nS147 & 255u;
      int32_t _M0L9byte__valS148 = *(int32_t*)&_M0L6_2atmpS2029;
      int32_t _M0L2hiS149 = _M0L9byte__valS148 / 16;
      int32_t _M0L2loS150 = _M0L9byte__valS148 % 16;
      int32_t _M0L6_2atmpS2023 = _M0L12digit__startS152 + _M0L6offsetS146;
      int32_t _M0L6_2atmpS2021 = _M0L6_2atmpS2023 - 2;
      int32_t _M0L6_2atmpS2022 =
        ((moonbit_string_t)moonbit_string_literal_12.data)[_M0L2hiS149];
      int32_t _M0L6_2atmpS2026;
      int32_t _M0L6_2atmpS2024;
      int32_t _M0L6_2atmpS2025;
      int32_t _M0L6_2atmpS2027;
      uint32_t _M0L6_2atmpS2028;
      _M0L6bufferS151[_M0L6_2atmpS2021] = _M0L6_2atmpS2022;
      _M0L6_2atmpS2026 = _M0L12digit__startS152 + _M0L6offsetS146;
      _M0L6_2atmpS2024 = _M0L6_2atmpS2026 - 1;
      _M0L6_2atmpS2025
      = ((moonbit_string_t)moonbit_string_literal_12.data)[
        _M0L2loS150
      ];
      _M0L6bufferS151[_M0L6_2atmpS2024] = _M0L6_2atmpS2025;
      _M0L6_2atmpS2027 = _M0L6offsetS146 - 2;
      _M0L6_2atmpS2028 = _M0L1nS147 >> 8;
      _M0L6offsetS146 = _M0L6_2atmpS2027;
      _M0L1nS147 = _M0L6_2atmpS2028;
      continue;
    } else if (_M0L6offsetS146 == 1) {
      uint32_t _M0L6_2atmpS2031 = _M0L1nS147 & 15u;
      int32_t _M0L6nibbleS154 = *(int32_t*)&_M0L6_2atmpS2031;
      int32_t _M0L6_2atmpS2030 =
        ((moonbit_string_t)moonbit_string_literal_12.data)[_M0L6nibbleS154];
      _M0L6bufferS151[_M0L12digit__startS152] = _M0L6_2atmpS2030;
    }
    break;
  }
  return 0;
}

int32_t _M0IP016_24default__implPB4Show6outputGiE(
  int32_t _M0L4selfS143,
  struct _M0TPB6Logger _M0L6loggerS142
) {
  moonbit_string_t _M0L6_2atmpS2019;
  #line 159 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  #line 160 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS2019 = _M0IPC13int3IntPB4Show10to__string(_M0L4selfS143);
  #line 160 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6loggerS142.$0->$method_0(_M0L6loggerS142.$1, _M0L6_2atmpS2019);
  moonbit_decref(_M0L6_2atmpS2019);
  return 0;
}

int32_t _M0IP016_24default__implPB4Show6outputGmE(
  uint64_t _M0L4selfS145,
  struct _M0TPB6Logger _M0L6loggerS144
) {
  moonbit_string_t _M0L6_2atmpS2020;
  #line 159 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  #line 160 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS2020 = _M0IPC16uint646UInt64PB4Show10to__string(_M0L4selfS145);
  #line 160 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6loggerS144.$0->$method_0(_M0L6loggerS144.$1, _M0L6_2atmpS2020);
  moonbit_decref(_M0L6_2atmpS2020);
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
  moonbit_string_t _M0L8_2afieldS5128;
  #line 92 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
  _M0L8_2afieldS5128 = _M0L4selfS140.$0;
  moonbit_incref(_M0L8_2afieldS5128);
  return _M0L8_2afieldS5128;
}

int32_t _M0IP016_24default__implPB6Logger16write__substringGRPB13StringBuilderE(
  struct _M0TPB13StringBuilder* _M0L4selfS136,
  moonbit_string_t _M0L5valueS137,
  int32_t _M0L5startS138,
  int32_t _M0L3lenS139
) {
  int32_t _M0L6_2atmpS2018;
  int64_t _M0L6_2atmpS2017;
  struct _M0TPC16string10StringView _M0L6_2atmpS2016;
  #line 122 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS2018 = _M0L5startS138 + _M0L3lenS139;
  _M0L6_2atmpS2017 = (int64_t)_M0L6_2atmpS2018;
  #line 123 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS2016
  = _M0MPC16string6String11sub_2einner(_M0L5valueS137, _M0L5startS138, _M0L6_2atmpS2017);
  #line 123 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0IPB13StringBuilderPB6Logger11write__view(_M0L4selfS136, _M0L6_2atmpS2016);
  moonbit_decref(_M0L6_2atmpS2016.$0);
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
  goto joinlet_5540;
  join_130:;
  _M0L3endS129 = _M0L3endS131;
  joinlet_5540:;
  if (
    _M0L5startS135 >= 0
    && _M0L5startS135 <= _M0L3endS129
    && _M0L3endS129 <= _M0L3lenS127
  ) {
    if (_M0L5startS135 < _M0L3lenS127) {
      int32_t _M0L6_2atmpS2013 = _M0L4selfS128[_M0L5startS135];
      int32_t _M0L6_2atmpS2012;
      #line 931 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
      _M0L6_2atmpS2012
      = _M0MPC16uint166UInt1623is__trailing__surrogate(_M0L6_2atmpS2013);
      if (!_M0L6_2atmpS2012) {
        
      } else {
        #line 931 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
        moonbit_panic();
      }
    }
    if (_M0L3endS129 < _M0L3lenS127) {
      int32_t _M0L6_2atmpS2015 = _M0L4selfS128[_M0L3endS129];
      int32_t _M0L6_2atmpS2014;
      #line 934 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
      _M0L6_2atmpS2014
      = _M0MPC16uint166UInt1623is__trailing__surrogate(_M0L6_2atmpS2015);
      if (!_M0L6_2atmpS2014) {
        
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
  struct _M0TPB6Logger _M0L6_2atmpS2011;
  #line 116 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  moonbit_incref(_M0L4selfS126);
  _M0L6_2atmpS2011
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS126
  };
  #line 117 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L4showS125.$0->$method_0(_M0L4showS125.$1, _M0L6_2atmpS2011);
  if (_M0L6_2atmpS2011.$1) {
    moonbit_decref(_M0L6_2atmpS2011.$1);
  }
  return 0;
}

int32_t _M0IP016_24default__implPB6Logger28write__string__interpolationGRPB13StringBuilderE(
  struct _M0TPB13StringBuilder* _M0L4selfS124,
  struct _M0TPB4Show _M0L4showS123
) {
  struct _M0TPB6Logger _M0L6_2atmpS2010;
  #line 111 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  moonbit_incref(_M0L4selfS124);
  _M0L6_2atmpS2010
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS124
  };
  #line 112 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L4showS123.$0->$method_0(_M0L4showS123.$1, _M0L6_2atmpS2010);
  if (_M0L6_2atmpS2010.$1) {
    moonbit_decref(_M0L6_2atmpS2010.$1);
  }
  return 0;
}

uint64_t _M0MPC13int3Int10to__uint64(int32_t _M0L4selfS122) {
  int64_t _M0L6_2atmpS2009;
  #line 989 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS2009 = (int64_t)_M0L4selfS122;
  return *(uint64_t*)&_M0L6_2atmpS2009;
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
  int32_t _M0L3lenS2008;
  int32_t _M0L8requiredS119;
  uint16_t* _M0L4dataS2003;
  int32_t _M0L6_2atmpS2002;
  int32_t _if__result_5541;
  uint16_t* _M0L4dataS2004;
  int32_t _M0L3lenS2005;
  int32_t _M0L3lenS2007;
  int32_t _M0L6_2atmpS2006;
  #line 105 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L8str__lenS117 = Moonbit_array_length(_M0L3strS118);
  if (_M0L8str__lenS117 == 0) {
    return 0;
  }
  _M0L3lenS2008 = _M0L4selfS120->$1;
  _M0L8requiredS119 = _M0L3lenS2008 + _M0L8str__lenS117;
  _M0L4dataS2003 = _M0L4selfS120->$0;
  _M0L6_2atmpS2002 = Moonbit_array_length(_M0L4dataS2003);
  if (_M0L8requiredS119 > _M0L6_2atmpS2002) {
    _if__result_5541 = 1;
  } else {
    int32_t _M0L3lenS2001 = _M0L4selfS120->$1;
    _if__result_5541 = _M0L8requiredS119 < _M0L3lenS2001;
  }
  if (_if__result_5541) {
    #line 112 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0MPB13StringBuilder4grow(_M0L4selfS120, _M0L8requiredS119);
  }
  _M0L4dataS2004 = _M0L4selfS120->$0;
  _M0L3lenS2005 = _M0L4selfS120->$1;
  moonbit_incref(_M0L4dataS2004);
  #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0MPC15array10FixedArray26unsafe__blit__from__string(_M0L4dataS2004, _M0L3lenS2005, _M0L3strS118, 0, _M0L8str__lenS117);
  moonbit_decref(_M0L4dataS2004);
  _M0L3lenS2007 = _M0L4selfS120->$1;
  _M0L6_2atmpS2006 = _M0L3lenS2007 + _M0L8str__lenS117;
  _M0L4selfS120->$1 = _M0L6_2atmpS2006;
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
      int32_t _M0L6_2atmpS1998 = _M0L3strS114[_M0L1iS111];
      int32_t _M0L6_2atmpS1999;
      int32_t _M0L6_2atmpS2000;
      _M0L4selfS113[_M0L1jS112] = _M0L6_2atmpS1998;
      _M0L6_2atmpS1999 = _M0L1iS111 + 1;
      _M0L6_2atmpS2000 = _M0L1jS112 + 1;
      _M0L1iS111 = _M0L6_2atmpS1999;
      _M0L1jS112 = _M0L6_2atmpS2000;
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
    int32_t _M0L3lenS1969 = _M0L4selfS106->$1;
    uint16_t* _M0L4dataS1971 = _M0L4selfS106->$0;
    int32_t _M0L6_2atmpS1970 = Moonbit_array_length(_M0L4dataS1971);
    uint16_t* _M0L4dataS1974;
    int32_t _M0L3lenS1975;
    int32_t _M0L6_2atmpS1976;
    int32_t _M0L3lenS1978;
    int32_t _M0L6_2atmpS1977;
    if (_M0L3lenS1969 >= _M0L6_2atmpS1970) {
      int32_t _M0L3lenS1973 = _M0L4selfS106->$1;
      int32_t _M0L6_2atmpS1972 = _M0L3lenS1973 + 1;
      #line 124 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0MPB13StringBuilder4grow(_M0L4selfS106, _M0L6_2atmpS1972);
    }
    _M0L4dataS1974 = _M0L4selfS106->$0;
    _M0L3lenS1975 = _M0L4selfS106->$1;
    moonbit_incref(_M0L4dataS1974);
    #line 126 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0L6_2atmpS1976 = _M0MPC14uint4UInt10to__uint16(_M0L4codeS104);
    if (
      _M0L3lenS1975 < 0
      || _M0L3lenS1975 >= Moonbit_array_length(_M0L4dataS1974)
    ) {
      #line 126 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      moonbit_panic();
    }
    _M0L4dataS1974[_M0L3lenS1975] = _M0L6_2atmpS1976;
    moonbit_decref(_M0L4dataS1974);
    _M0L3lenS1978 = _M0L4selfS106->$1;
    _M0L6_2atmpS1977 = _M0L3lenS1978 + 1;
    _M0L4selfS106->$1 = _M0L6_2atmpS1977;
  } else if (_M0L4codeS104 <= 1114111u) {
    uint16_t* _M0L4dataS1982 = _M0L4selfS106->$0;
    int32_t _M0L6_2atmpS1980 = Moonbit_array_length(_M0L4dataS1982);
    int32_t _M0L3lenS1981 = _M0L4selfS106->$1;
    int32_t _M0L6_2atmpS1979 = _M0L6_2atmpS1980 - _M0L3lenS1981;
    uint32_t _M0L4codeS107;
    uint16_t* _M0L4dataS1985;
    int32_t _M0L3lenS1986;
    uint32_t _M0L6_2atmpS1989;
    uint32_t _M0L6_2atmpS1988;
    int32_t _M0L6_2atmpS1987;
    uint16_t* _M0L4dataS1990;
    int32_t _M0L3lenS1995;
    int32_t _M0L6_2atmpS1991;
    uint32_t _M0L6_2atmpS1994;
    uint32_t _M0L6_2atmpS1993;
    int32_t _M0L6_2atmpS1992;
    int32_t _M0L3lenS1997;
    int32_t _M0L6_2atmpS1996;
    if (_M0L6_2atmpS1979 < 2) {
      int32_t _M0L3lenS1984 = _M0L4selfS106->$1;
      int32_t _M0L6_2atmpS1983 = _M0L3lenS1984 + 2;
      #line 130 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0MPB13StringBuilder4grow(_M0L4selfS106, _M0L6_2atmpS1983);
    }
    _M0L4codeS107 = _M0L4codeS104 - 65536u;
    _M0L4dataS1985 = _M0L4selfS106->$0;
    _M0L3lenS1986 = _M0L4selfS106->$1;
    _M0L6_2atmpS1989 = _M0L4codeS107 >> 10;
    _M0L6_2atmpS1988 = 55296u + _M0L6_2atmpS1989;
    moonbit_incref(_M0L4dataS1985);
    #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0L6_2atmpS1987 = _M0MPC14uint4UInt10to__uint16(_M0L6_2atmpS1988);
    if (
      _M0L3lenS1986 < 0
      || _M0L3lenS1986 >= Moonbit_array_length(_M0L4dataS1985)
    ) {
      #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      moonbit_panic();
    }
    _M0L4dataS1985[_M0L3lenS1986] = _M0L6_2atmpS1987;
    moonbit_decref(_M0L4dataS1985);
    _M0L4dataS1990 = _M0L4selfS106->$0;
    _M0L3lenS1995 = _M0L4selfS106->$1;
    _M0L6_2atmpS1991 = _M0L3lenS1995 + 1;
    _M0L6_2atmpS1994 = _M0L4codeS107 & 1023u;
    _M0L6_2atmpS1993 = 56320u + _M0L6_2atmpS1994;
    moonbit_incref(_M0L4dataS1990);
    #line 134 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0L6_2atmpS1992 = _M0MPC14uint4UInt10to__uint16(_M0L6_2atmpS1993);
    if (
      _M0L6_2atmpS1991 < 0
      || _M0L6_2atmpS1991 >= Moonbit_array_length(_M0L4dataS1990)
    ) {
      #line 134 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      moonbit_panic();
    }
    _M0L4dataS1990[_M0L6_2atmpS1991] = _M0L6_2atmpS1992;
    moonbit_decref(_M0L4dataS1990);
    _M0L3lenS1997 = _M0L4selfS106->$1;
    _M0L6_2atmpS1996 = _M0L3lenS1997 + 2;
    _M0L4selfS106->$1 = _M0L6_2atmpS1996;
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
  uint16_t* _M0L4dataS1968;
  int32_t _M0L6_2atmpS1966;
  int32_t _M0L3lenS1967;
  int32_t _M0L13new__capacityS100;
  uint16_t* _M0L4dataS1963;
  int32_t _M0L6_2atmpS1964;
  int32_t _M0L3lenS1965;
  uint16_t* _M0L9new__dataS103;
  uint16_t* _M0L6_2aoldS5129;
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L4dataS1968 = _M0L4selfS101->$0;
  _M0L6_2atmpS1966 = Moonbit_array_length(_M0L4dataS1968);
  _M0L3lenS1967 = _M0L4selfS101->$1;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L13new__capacityS100
  = _M0FPB31stringbuilder__growth__capacity(_M0L6_2atmpS1966, _M0L3lenS1967, _M0L8requiredS102);
  _M0L4dataS1963 = _M0L4selfS101->$0;
  moonbit_incref(_M0L4dataS1963);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L6_2atmpS1964 = _M0IPC16uint166UInt16PB7Default7default();
  _M0L3lenS1965 = _M0L4selfS101->$1;
  #line 80 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L9new__dataS103
  = _M0MPC15array10FixedArray23make__and__blit_2einnerGkE(_M0L4dataS1963, _M0L13new__capacityS100, _M0L6_2atmpS1964, _M0L3lenS1965, 0, 0);
  _M0L6_2aoldS5129 = _M0L4selfS101->$0;
  moonbit_decref(_M0L6_2aoldS5129);
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
  int32_t _M0L6_2atmpS1962;
  #line 2785 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS1962 = *(int32_t*)&_M0L4selfS93;
  return (uint16_t)_M0L6_2atmpS1962;
}

uint32_t _M0MPC14char4Char8to__uint(int32_t _M0L4selfS92) {
  int32_t _M0L6_2atmpS1961;
  #line 1335 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS1961 = _M0L4selfS92;
  return *(uint32_t*)&_M0L6_2atmpS1961;
}

moonbit_string_t _M0MPB13StringBuilder10to__string(
  struct _M0TPB13StringBuilder* _M0L4selfS90
) {
  int32_t _M0L3lenS1952;
  #line 181 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L3lenS1952 = _M0L4selfS90->$1;
  if (_M0L3lenS1952 == 0) {
    return (moonbit_string_t)moonbit_string_literal_0.data;
  } else {
    int32_t _M0L3lenS1953 = _M0L4selfS90->$1;
    uint16_t* _M0L4dataS1955 = _M0L4selfS90->$0;
    int32_t _M0L6_2atmpS1954 = Moonbit_array_length(_M0L4dataS1955);
    if (_M0L3lenS1953 == _M0L6_2atmpS1954) {
      uint16_t* _M0L4dataS1956 = _M0L4selfS90->$0;
      moonbit_incref(_M0L4dataS1956);
      return _M0L4dataS1956;
    } else {
      uint16_t* _M0L4dataS1957 = _M0L4selfS90->$0;
      int32_t _M0L3lenS1958 = _M0L4selfS90->$1;
      int32_t _M0L6_2atmpS1959;
      int32_t _M0L3lenS1960;
      uint16_t* _M0L4dataS91;
      moonbit_incref(_M0L4dataS1957);
      #line 190 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0L6_2atmpS1959 = _M0IPC16uint166UInt16PB7Default7default();
      _M0L3lenS1960 = _M0L4selfS90->$1;
      #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0L4dataS91
      = _M0MPC15array10FixedArray23make__and__blit_2einnerGkE(_M0L4dataS1957, _M0L3lenS1958, _M0L6_2atmpS1959, _M0L3lenS1960, 0, 0);
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
  int32_t _if__result_5544;
  #line 97 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
  if (_M0L13allocate__lenS83 >= 0) {
    if (_M0L3lenS84 >= 0) {
      if (_M0L11src__offsetS85 >= 0) {
        if (_M0L11dst__offsetS86 >= 0) {
          int32_t _M0L6_2atmpS1948 = _M0L11src__offsetS85 + _M0L3lenS84;
          int32_t _M0L6_2atmpS1949 = Moonbit_array_length(_M0L3srcS87);
          if (_M0L6_2atmpS1948 <= _M0L6_2atmpS1949) {
            int32_t _M0L6_2atmpS1947 = _M0L11dst__offsetS86 + _M0L3lenS84;
            _if__result_5544 = _M0L6_2atmpS1947 <= _M0L13allocate__lenS83;
          } else {
            _if__result_5544 = 0;
          }
        } else {
          _if__result_5544 = 0;
        }
      } else {
        _if__result_5544 = 0;
      }
    } else {
      _if__result_5544 = 0;
    }
  } else {
    _if__result_5544 = 0;
  }
  if (_if__result_5544) {
    #line 116 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    return _M0MPC15array10FixedArray23unsafe__make__and__blitGkE(_M0L3srcS87, _M0L13allocate__lenS83, _M0L4initS88, _M0L11src__offsetS85, _M0L11dst__offsetS86, _M0L3lenS84);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS89;
    int32_t _M0L6_2atmpS1951;
    moonbit_string_t _M0L6_2atmpS1950;
    uint16_t* _result_5545;
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
    _M0L6_2atmpS1951 = Moonbit_array_length(_M0L3srcS87);
    moonbit_decref(_M0L3srcS87);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS89, _M0L6_2atmpS1951);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0L6_2atmpS1950
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS89);
    moonbit_decref(_M0L18_2astring__builderS89);
    #line 112 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _result_5545 = _M0FPC15abort5abortGAkE(_M0L6_2atmpS1950);
    moonbit_decref(_M0L6_2atmpS1950);
    return _result_5545;
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
  struct _M0TPB13StringBuilder* _block_5546;
  #line 32 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  if (_M0L10size__hintS74 < 1) {
    _M0L7initialS73 = 1;
  } else {
    int32_t _M0L6_2atmpS1946 = _M0L10size__hintS74 + 1;
    _M0L7initialS73 = _M0L6_2atmpS1946 / 2;
  }
  _M0L4dataS75 = (uint16_t*)moonbit_make_string(_M0L7initialS73, 0);
  _block_5546
  = (struct _M0TPB13StringBuilder*)moonbit_malloc(sizeof(struct _M0TPB13StringBuilder));
  Moonbit_object_header(_block_5546)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 99, 0);
  _block_5546->$0 = _M0L4dataS75;
  _block_5546->$1 = 0;
  return _block_5546;
}

int32_t* _M0MPB18UninitializedArray23make__and__blit_2einnerGiE(
  int32_t* _M0L3srcS65,
  int32_t _M0L13allocate__lenS61,
  int32_t _M0L3lenS62,
  int32_t _M0L11src__offsetS63,
  int32_t _M0L11dst__offsetS64
) {
  int32_t _if__result_5547;
  #line 167 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  if (_M0L13allocate__lenS61 >= 0) {
    if (_M0L3lenS62 >= 0) {
      if (_M0L11src__offsetS63 >= 0) {
        if (_M0L11dst__offsetS64 >= 0) {
          int32_t _M0L6_2atmpS1937 = _M0L11src__offsetS63 + _M0L3lenS62;
          int32_t _M0L6_2atmpS1938;
          #line 179 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
          _M0L6_2atmpS1938
          = _M0MPB18UninitializedArray6lengthGiE(_M0L3srcS65);
          if (_M0L6_2atmpS1937 <= _M0L6_2atmpS1938) {
            int32_t _M0L6_2atmpS1936 = _M0L11dst__offsetS64 + _M0L3lenS62;
            _if__result_5547 = _M0L6_2atmpS1936 <= _M0L13allocate__lenS61;
          } else {
            _if__result_5547 = 0;
          }
        } else {
          _if__result_5547 = 0;
        }
      } else {
        _if__result_5547 = 0;
      }
    } else {
      _if__result_5547 = 0;
    }
  } else {
    _if__result_5547 = 0;
  }
  if (_if__result_5547) {
    #line 185 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    return _M0MPB18UninitializedArray23unsafe__make__and__blitGiE(_M0L3srcS65, _M0L13allocate__lenS61, _M0L11src__offsetS63, _M0L11dst__offsetS64, _M0L3lenS62);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS66;
    int32_t _M0L6_2atmpS1940;
    moonbit_string_t _M0L6_2atmpS1939;
    int32_t* _result_5548;
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
    _M0L6_2atmpS1940 = _M0MPB18UninitializedArray6lengthGiE(_M0L3srcS65);
    moonbit_decref(_M0L3srcS65);
    #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS66, _M0L6_2atmpS1940);
    #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS1939
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS66);
    moonbit_decref(_M0L18_2astring__builderS66);
    #line 181 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _result_5548
    = _M0FPC15abort5abortGRPB18UninitializedArrayGiEE(_M0L6_2atmpS1939);
    moonbit_decref(_M0L6_2atmpS1939);
    return _result_5548;
  }
}

float* _M0MPB18UninitializedArray23make__and__blit_2einnerGfE(
  float* _M0L3srcS71,
  int32_t _M0L13allocate__lenS67,
  int32_t _M0L3lenS68,
  int32_t _M0L11src__offsetS69,
  int32_t _M0L11dst__offsetS70
) {
  int32_t _if__result_5549;
  #line 167 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  if (_M0L13allocate__lenS67 >= 0) {
    if (_M0L3lenS68 >= 0) {
      if (_M0L11src__offsetS69 >= 0) {
        if (_M0L11dst__offsetS70 >= 0) {
          int32_t _M0L6_2atmpS1942 = _M0L11src__offsetS69 + _M0L3lenS68;
          int32_t _M0L6_2atmpS1943;
          #line 179 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
          _M0L6_2atmpS1943
          = _M0MPB18UninitializedArray6lengthGfE(_M0L3srcS71);
          if (_M0L6_2atmpS1942 <= _M0L6_2atmpS1943) {
            int32_t _M0L6_2atmpS1941 = _M0L11dst__offsetS70 + _M0L3lenS68;
            _if__result_5549 = _M0L6_2atmpS1941 <= _M0L13allocate__lenS67;
          } else {
            _if__result_5549 = 0;
          }
        } else {
          _if__result_5549 = 0;
        }
      } else {
        _if__result_5549 = 0;
      }
    } else {
      _if__result_5549 = 0;
    }
  } else {
    _if__result_5549 = 0;
  }
  if (_if__result_5549) {
    #line 185 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    return _M0MPB18UninitializedArray23unsafe__make__and__blitGfE(_M0L3srcS71, _M0L13allocate__lenS67, _M0L11src__offsetS69, _M0L11dst__offsetS70, _M0L3lenS68);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS72;
    int32_t _M0L6_2atmpS1945;
    moonbit_string_t _M0L6_2atmpS1944;
    float* _result_5550;
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
    _M0L6_2atmpS1945 = _M0MPB18UninitializedArray6lengthGfE(_M0L3srcS71);
    moonbit_decref(_M0L3srcS71);
    #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS72, _M0L6_2atmpS1945);
    #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS1944
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS72);
    moonbit_decref(_M0L18_2astring__builderS72);
    #line 181 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _result_5550
    = _M0FPC15abort5abortGRPB18UninitializedArrayGfEE(_M0L6_2atmpS1944);
    moonbit_decref(_M0L6_2atmpS1944);
    return _result_5550;
  }
}

int32_t _M0MPB13StringBuilder13write__objectGiE(
  struct _M0TPB13StringBuilder* _M0L4selfS58,
  int32_t _M0L3objS57
) {
  struct _M0TPB6Logger _M0L6_2atmpS1934;
  #line 17 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  moonbit_incref(_M0L4selfS58);
  _M0L6_2atmpS1934
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS58
  };
  #line 23 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  _M0IP016_24default__implPB4Show6outputGiE(_M0L3objS57, _M0L6_2atmpS1934);
  if (_M0L6_2atmpS1934.$1) {
    moonbit_decref(_M0L6_2atmpS1934.$1);
  }
  return 0;
}

int32_t _M0MPB13StringBuilder13write__objectGmE(
  struct _M0TPB13StringBuilder* _M0L4selfS60,
  uint64_t _M0L3objS59
) {
  struct _M0TPB6Logger _M0L6_2atmpS1935;
  #line 17 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  moonbit_incref(_M0L4selfS60);
  _M0L6_2atmpS1935
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS60
  };
  #line 23 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  _M0IP016_24default__implPB4Show6outputGmE(_M0L3objS59, _M0L6_2atmpS1935);
  if (_M0L6_2atmpS1935.$1) {
    moonbit_decref(_M0L6_2atmpS1935.$1);
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
        int32_t _M0L6_2atmpS1907 = _M0L11dst__offsetS10 + _M0L1iS12;
        int32_t _M0L6_2atmpS1909 = _M0L11src__offsetS11 + _M0L1iS12;
        int32_t _M0L6_2atmpS1908;
        int32_t _M0L6_2atmpS1910;
        if (
          _M0L6_2atmpS1909 < 0
          || _M0L6_2atmpS1909 >= Moonbit_array_length(_M0L3srcS9)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1908 = (int32_t)_M0L3srcS9[_M0L6_2atmpS1909];
        if (
          _M0L6_2atmpS1907 < 0
          || _M0L6_2atmpS1907 >= Moonbit_array_length(_M0L3dstS8)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS8[_M0L6_2atmpS1907] = _M0L6_2atmpS1908;
        _M0L6_2atmpS1910 = _M0L1iS12 + 1;
        _M0L1iS12 = _M0L6_2atmpS1910;
        continue;
      } else {
        moonbit_decref(_M0L3srcS9);
        moonbit_decref(_M0L3dstS8);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1915 = _M0L3lenS13 - 1;
    int32_t _M0L1iS15 = _M0L6_2atmpS1915;
    while (1) {
      if (_M0L1iS15 >= 0) {
        int32_t _M0L6_2atmpS1911 = _M0L11dst__offsetS10 + _M0L1iS15;
        int32_t _M0L6_2atmpS1913 = _M0L11src__offsetS11 + _M0L1iS15;
        int32_t _M0L6_2atmpS1912;
        int32_t _M0L6_2atmpS1914;
        if (
          _M0L6_2atmpS1913 < 0
          || _M0L6_2atmpS1913 >= Moonbit_array_length(_M0L3srcS9)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1912 = (int32_t)_M0L3srcS9[_M0L6_2atmpS1913];
        if (
          _M0L6_2atmpS1911 < 0
          || _M0L6_2atmpS1911 >= Moonbit_array_length(_M0L3dstS8)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS8[_M0L6_2atmpS1911] = _M0L6_2atmpS1912;
        _M0L6_2atmpS1914 = _M0L1iS15 - 1;
        _M0L1iS15 = _M0L6_2atmpS1914;
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

int32_t _M0MPC15array10FixedArray12unsafe__blitGkE(
  uint16_t* _M0L3dstS17,
  int32_t _M0L11dst__offsetS19,
  uint16_t* _M0L3srcS18,
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
        int32_t _M0L6_2atmpS1916 = _M0L11dst__offsetS19 + _M0L1iS21;
        int32_t _M0L6_2atmpS1918 = _M0L11src__offsetS20 + _M0L1iS21;
        int32_t _M0L6_2atmpS1917;
        int32_t _M0L6_2atmpS1919;
        if (
          _M0L6_2atmpS1918 < 0
          || _M0L6_2atmpS1918 >= Moonbit_array_length(_M0L3srcS18)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1917 = (int32_t)_M0L3srcS18[_M0L6_2atmpS1918];
        if (
          _M0L6_2atmpS1916 < 0
          || _M0L6_2atmpS1916 >= Moonbit_array_length(_M0L3dstS17)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS17[_M0L6_2atmpS1916] = _M0L6_2atmpS1917;
        _M0L6_2atmpS1919 = _M0L1iS21 + 1;
        _M0L1iS21 = _M0L6_2atmpS1919;
        continue;
      } else {
        moonbit_decref(_M0L3srcS18);
        moonbit_decref(_M0L3dstS17);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1924 = _M0L3lenS22 - 1;
    int32_t _M0L1iS24 = _M0L6_2atmpS1924;
    while (1) {
      if (_M0L1iS24 >= 0) {
        int32_t _M0L6_2atmpS1920 = _M0L11dst__offsetS19 + _M0L1iS24;
        int32_t _M0L6_2atmpS1922 = _M0L11src__offsetS20 + _M0L1iS24;
        int32_t _M0L6_2atmpS1921;
        int32_t _M0L6_2atmpS1923;
        if (
          _M0L6_2atmpS1922 < 0
          || _M0L6_2atmpS1922 >= Moonbit_array_length(_M0L3srcS18)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1921 = (int32_t)_M0L3srcS18[_M0L6_2atmpS1922];
        if (
          _M0L6_2atmpS1920 < 0
          || _M0L6_2atmpS1920 >= Moonbit_array_length(_M0L3dstS17)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS17[_M0L6_2atmpS1920] = _M0L6_2atmpS1921;
        _M0L6_2atmpS1923 = _M0L1iS24 - 1;
        _M0L1iS24 = _M0L6_2atmpS1923;
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

int32_t _M0MPC15array10FixedArray12unsafe__blitGRPB17UnsafeMaybeUninitGfEE(
  float* _M0L3dstS26,
  int32_t _M0L11dst__offsetS28,
  float* _M0L3srcS27,
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
        int32_t _M0L6_2atmpS1925 = _M0L11dst__offsetS28 + _M0L1iS30;
        int32_t _M0L6_2atmpS1927 = _M0L11src__offsetS29 + _M0L1iS30;
        float _M0L6_2atmpS1926;
        int32_t _M0L6_2atmpS1928;
        if (
          _M0L6_2atmpS1927 < 0
          || _M0L6_2atmpS1927 >= Moonbit_array_length(_M0L3srcS27)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1926 = (float)_M0L3srcS27[_M0L6_2atmpS1927];
        if (
          _M0L6_2atmpS1925 < 0
          || _M0L6_2atmpS1925 >= Moonbit_array_length(_M0L3dstS26)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS26[_M0L6_2atmpS1925] = _M0L6_2atmpS1926;
        _M0L6_2atmpS1928 = _M0L1iS30 + 1;
        _M0L1iS30 = _M0L6_2atmpS1928;
        continue;
      } else {
        moonbit_decref(_M0L3srcS27);
        moonbit_decref(_M0L3dstS26);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1933 = _M0L3lenS31 - 1;
    int32_t _M0L1iS33 = _M0L6_2atmpS1933;
    while (1) {
      if (_M0L1iS33 >= 0) {
        int32_t _M0L6_2atmpS1929 = _M0L11dst__offsetS28 + _M0L1iS33;
        int32_t _M0L6_2atmpS1931 = _M0L11src__offsetS29 + _M0L1iS33;
        float _M0L6_2atmpS1930;
        int32_t _M0L6_2atmpS1932;
        if (
          _M0L6_2atmpS1931 < 0
          || _M0L6_2atmpS1931 >= Moonbit_array_length(_M0L3srcS27)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1930 = (float)_M0L3srcS27[_M0L6_2atmpS1931];
        if (
          _M0L6_2atmpS1929 < 0
          || _M0L6_2atmpS1929 >= Moonbit_array_length(_M0L3dstS26)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS26[_M0L6_2atmpS1929] = _M0L6_2atmpS1930;
        _M0L6_2atmpS1932 = _M0L1iS33 - 1;
        _M0L1iS33 = _M0L6_2atmpS1932;
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

int32_t* _M0FPC15abort5abortGRPB18UninitializedArrayGiEE(
  moonbit_string_t _M0L3msgS2
) {
  #line 47 "C:\\Users\\31379\\.moon\\lib\\core\\abort\\abort.mbt"
  #line 49 "C:\\Users\\31379\\.moon\\lib\\core\\abort\\abort.mbt"
  moonbit_println(_M0L3msgS2);
  #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\abort\\abort.mbt"
  moonbit_panic();
}

float* _M0FPC15abort5abortGRPB18UninitializedArrayGfEE(
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
  void* _M0L11_2aobj__ptrS1824,
  struct _M0TPB4Show _M0L8_2aparamS1823
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS1822 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS1824;
  _M0IP016_24default__implPB6Logger5writeGRPB13StringBuilderE(_M0L7_2aselfS1822, _M0L8_2aparamS1823);
  return 0;
}

int32_t _M0IP016_24default__implPB6Logger84write__string__interpolation_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLoggerGRPB13StringBuilderE(
  void* _M0L11_2aobj__ptrS1821,
  struct _M0TPB4Show _M0L8_2aparamS1820
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS1819 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS1821;
  _M0IP016_24default__implPB6Logger28write__string__interpolationGRPB13StringBuilderE(_M0L7_2aselfS1819, _M0L8_2aparamS1820);
  return 0;
}

int32_t _M0IPB13StringBuilderPB6Logger67write__char_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLogger(
  void* _M0L11_2aobj__ptrS1818,
  int32_t _M0L8_2aparamS1817
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS1816 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS1818;
  _M0IPB13StringBuilderPB6Logger11write__char(_M0L7_2aselfS1816, _M0L8_2aparamS1817);
  return 0;
}

int32_t _M0IPB13StringBuilderPB6Logger67write__view_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLogger(
  void* _M0L11_2aobj__ptrS1815,
  struct _M0TPC16string10StringView _M0L8_2aparamS1814
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS1813 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS1815;
  _M0IPB13StringBuilderPB6Logger11write__view(_M0L7_2aselfS1813, _M0L8_2aparamS1814);
  return 0;
}

int32_t _M0IP016_24default__implPB6Logger72write__substring_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLoggerGRPB13StringBuilderE(
  void* _M0L11_2aobj__ptrS1812,
  moonbit_string_t _M0L8_2aparamS1809,
  int32_t _M0L8_2aparamS1810,
  int32_t _M0L8_2aparamS1811
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS1808 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS1812;
  _M0IP016_24default__implPB6Logger16write__substringGRPB13StringBuilderE(_M0L7_2aselfS1808, _M0L8_2aparamS1809, _M0L8_2aparamS1810, _M0L8_2aparamS1811);
  return 0;
}

int32_t _M0IPB13StringBuilderPB6Logger69write__string_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLogger(
  void* _M0L11_2aobj__ptrS1807,
  moonbit_string_t _M0L8_2aparamS1806
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS1805 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS1807;
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L7_2aselfS1805, _M0L8_2aparamS1806);
  return 0;
}

void moonbit_init() {
  moonbit_layout_table = moonbit_layout_table_data;
  int64_t _tmp_5557 = 9218868437227405311ll;
  int64_t _tmp_5558;
  int64_t _tmp_5559;
  int64_t _tmp_5560;
  int64_t _tmp_5561;
  _M0FPB18double__max__value = *(double*)&_tmp_5557;
  _tmp_5558 = -4503599627370497ll;
  _M0FPB18double__min__value = *(double*)&_tmp_5558;
  _tmp_5559 = 9221120237041090561ll;
  _M0FPC16double14not__a__number = *(double*)&_tmp_5559;
  _tmp_5560 = -4503599627370496ll;
  _M0FPC16double13neg__infinity = *(double*)&_tmp_5560;
  _tmp_5561 = 4503599627370496ll;
  _M0FPC16double13min__positive = *(double*)&_tmp_5561;
}

int main(int argc, char** argv) {
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS1762;
  int32_t _M0L3ne1S1763;
  int32_t _M0L3ne2S1764;
  float _M0L2dtS1765;
  struct _M0TP26RiantR8snn__mbt11IFParameter* _M0L6_2atmpS1906;
  struct _M0TP26RiantR8snn__mbt2IF* _M0L2e1S1766;
  struct _M0TP26RiantR8snn__mbt11IFParameter* _M0L6_2atmpS1905;
  struct _M0TP26RiantR8snn__mbt2IF* _M0L2e2S1767;
  int32_t _M0L7_2abindS1768;
  int32_t _M0L1kS1769;
  int32_t _M0L7_2abindS1771;
  int32_t _M0L1kS1772;
  struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0L3w11S1774;
  struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0L3w12S1775;
  struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0L3w21S1776;
  struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0L3w22S1777;
  struct _M0TP26RiantR8snn__mbt9STDPEntry* _M0L6_2atmpS1904;
  void* _M0L9stdp__w11S1778;
  struct _M0TP26RiantR8snn__mbt9STDPEntry* _M0L6_2atmpS1903;
  void* _M0L9stdp__w22S1779;
  struct _M0TP26RiantR8snn__mbt17PoissonStimulusIF* _M0L8stim__e1S1780;
  struct _M0TP26RiantR8snn__mbt17PoissonStimulusIF* _M0L8stim__e2S1781;
  struct _M0TP26RiantR8snn__mbt7Monitor* _M0L6fm__e1S1782;
  struct _M0TP26RiantR8snn__mbt7Monitor* _M0L6fm__e2S1783;
  void* _M0L4IF__S1901;
  void* _M0L4IF__S1902;
  void** _M0L6_2atmpS1900;
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt6AnyPopE* _M0L6_2atmpS1885;
  struct _M0TP26RiantR8snn__mbt14SpikingSynapse** _M0L6_2atmpS1899;
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt14SpikingSynapseE* _M0L6_2atmpS1886;
  void* _M0L11PoissonIF__S1897;
  void* _M0L11PoissonIF__S1898;
  void** _M0L6_2atmpS1896;
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt7AnyStimE* _M0L6_2atmpS1895;
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt7AnyStimE* _M0L6_2atmpS1887;
  struct _M0TP26RiantR8snn__mbt7Monitor** _M0L6_2atmpS1894;
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt7MonitorE* _M0L6_2atmpS1893;
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt7MonitorE* _M0L6_2atmpS1888;
  void** _M0L6_2atmpS1892;
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt13STDPEntryKindE* _M0L6_2atmpS1891;
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt13STDPEntryKindE* _M0L6_2atmpS1889;
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt12STPEntryKindE* _M0L6_2atmpS1890;
  struct _M0TP26RiantR8snn__mbt18HeterogeneousModel* _M0L5modelS1784;
  float _M0L12duration__msS1785;
  float _M0L6_2atmpS1884;
  int32_t _M0L5stepsS1786;
  int32_t _M0L7_2abindS1787;
  int32_t _M0L2__S1788;
  float _M0L6e1__hzS1790;
  float _M0L6e2__hzS1791;
  struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS1883;
  int32_t _M0L6_2atmpS1882;
  float _M0L6_2atmpS1881;
  float _M0L17w11__sum__initialS1792;
  struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS1880;
  int32_t _M0L6_2atmpS1879;
  float _M0L6_2atmpS1878;
  float _M0L17w22__sum__initialS1793;
  struct _M0TPB8MutLocalGfE* _M0L8w11__sumS1794;
  struct _M0TPB8MutLocalGfE* _M0L8w22__sumS1795;
  struct _M0TPB8MutLocalGiE* _M0L1kS1796;
  struct _M0TPB8MutLocalGiE* _M0L2k2S1798;
  moonbit_string_t _M0L6_2atmpS1856;
  moonbit_string_t _M0L6_2atmpS1855;
  moonbit_string_t _M0L6_2atmpS1854;
  moonbit_string_t _M0L6_2atmpS1859;
  moonbit_string_t _M0L6_2atmpS1858;
  moonbit_string_t _M0L6_2atmpS1857;
  struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS1863;
  int32_t _M0L6_2acntS5283;
  int32_t _M0L6_2atmpS1862;
  moonbit_string_t _M0L6_2atmpS1861;
  moonbit_string_t _M0L6_2atmpS1860;
  struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS1867;
  int32_t _M0L6_2acntS5294;
  int32_t _M0L6_2atmpS1866;
  moonbit_string_t _M0L6_2atmpS1865;
  moonbit_string_t _M0L6_2atmpS1864;
  moonbit_string_t _M0L6_2atmpS1869;
  moonbit_string_t _M0L6_2atmpS1868;
  moonbit_string_t _M0L6_2atmpS1871;
  moonbit_string_t _M0L6_2atmpS1870;
  float _M0L3valS1874;
  moonbit_string_t _M0L6_2atmpS1873;
  moonbit_string_t _M0L6_2atmpS1872;
  float _M0L3valS1877;
  moonbit_string_t _M0L6_2atmpS1876;
  moonbit_string_t _M0L6_2atmpS1875;
  moonbit_runtime_init(argc, argv);
  moonbit_init();
  #line 24 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lagzi2022\\main.mbt"
  _M0L3rngS1762 = _M0MP26RiantR8snn__mbt7Xoshiro7default();
  _M0L3ne1S1763 = 16;
  _M0L3ne2S1764 = 16;
  _M0L2dtS1765 = 0x1p-3f;
  #line 31 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lagzi2022\\main.mbt"
  _M0L6_2atmpS1906 = _M0MP26RiantR8snn__mbt11IFParameter3new();
  #line 31 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lagzi2022\\main.mbt"
  _M0L2e1S1766
  = _M0MP26RiantR8snn__mbt2IF3new(_M0L3ne1S1763, _M0L6_2atmpS1906, _M0L3rngS1762);
  moonbit_decref(_M0L6_2atmpS1906);
  #line 32 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lagzi2022\\main.mbt"
  _M0L6_2atmpS1905 = _M0MP26RiantR8snn__mbt11IFParameter3new();
  #line 32 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lagzi2022\\main.mbt"
  _M0L2e2S1767
  = _M0MP26RiantR8snn__mbt2IF3new(_M0L3ne2S1764, _M0L6_2atmpS1905, _M0L3rngS1762);
  moonbit_decref(_M0L6_2atmpS1905);
  _M0L7_2abindS1768 = 0;
  _M0L1kS1769 = _M0L7_2abindS1768;
  while (1) {
    if (_M0L1kS1769 < _M0L3ne1S1763) {
      struct _M0TPB5ArrayGfE* _M0L1iS1825 = _M0L2e1S1766->$7;
      int32_t _M0L6_2atmpS1826;
      moonbit_incref(_M0L1iS1825);
      #line 36 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lagzi2022\\main.mbt"
      _M0MPC15array5Array3setGfE(_M0L1iS1825, _M0L1kS1769, 0x1.5ep+8f);
      moonbit_decref(_M0L1iS1825);
      _M0L6_2atmpS1826 = _M0L1kS1769 + 1;
      _M0L1kS1769 = _M0L6_2atmpS1826;
      continue;
    }
    break;
  }
  _M0L7_2abindS1771 = 0;
  _M0L1kS1772 = _M0L7_2abindS1771;
  while (1) {
    if (_M0L1kS1772 < _M0L3ne2S1764) {
      struct _M0TPB5ArrayGfE* _M0L1iS1827 = _M0L2e2S1767->$7;
      int32_t _M0L6_2atmpS1828;
      moonbit_incref(_M0L1iS1827);
      #line 39 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lagzi2022\\main.mbt"
      _M0MPC15array5Array3setGfE(_M0L1iS1827, _M0L1kS1772, 0x1.5ep+8f);
      moonbit_decref(_M0L1iS1827);
      _M0L6_2atmpS1828 = _M0L1kS1772 + 1;
      _M0L1kS1772 = _M0L6_2atmpS1828;
      continue;
    }
    break;
  }
  #line 43 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lagzi2022\\main.mbt"
  _M0L3w11S1774
  = _M0MP26RiantR8snn__mbt14SpikingSynapse6random(_M0L2e1S1766, _M0L2e1S1766, (moonbit_string_t)moonbit_string_literal_1.data, 0x1.4p+3f, 0x0p+0f, 0x1.999999999999ap-3f, _M0L3rngS1762);
  #line 45 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lagzi2022\\main.mbt"
  _M0L3w12S1775
  = _M0MP26RiantR8snn__mbt14SpikingSynapse6random(_M0L2e1S1766, _M0L2e2S1767, (moonbit_string_t)moonbit_string_literal_1.data, 0x1.4p+3f, 0x0p+0f, 0x1.999999999999ap-3f, _M0L3rngS1762);
  #line 47 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lagzi2022\\main.mbt"
  _M0L3w21S1776
  = _M0MP26RiantR8snn__mbt14SpikingSynapse6random(_M0L2e2S1767, _M0L2e1S1766, (moonbit_string_t)moonbit_string_literal_1.data, 0x1.4p+3f, 0x0p+0f, 0x1.999999999999ap-3f, _M0L3rngS1762);
  #line 49 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lagzi2022\\main.mbt"
  _M0L3w22S1777
  = _M0MP26RiantR8snn__mbt14SpikingSynapse6random(_M0L2e2S1767, _M0L2e2S1767, (moonbit_string_t)moonbit_string_literal_1.data, 0x1.4p+3f, 0x0p+0f, 0x1.999999999999ap-3f, _M0L3rngS1762);
  #line 53 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lagzi2022\\main.mbt"
  _M0L6_2atmpS1904
  = _M0MP26RiantR8snn__mbt9STDPEntry3new(0, _M0L3ne1S1763, _M0L3ne1S1763);
  _M0L9stdp__w11S1778
  = (void*)moonbit_malloc(sizeof(struct _M0DTP26RiantR8snn__mbt13STDPEntryKind10Gerstner__));
  Moonbit_object_header(_M0L9stdp__w11S1778)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 102, 0);
  ((struct _M0DTP26RiantR8snn__mbt13STDPEntryKind10Gerstner__*)_M0L9stdp__w11S1778)->$0
  = _M0L6_2atmpS1904;
  #line 54 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lagzi2022\\main.mbt"
  _M0L6_2atmpS1903
  = _M0MP26RiantR8snn__mbt9STDPEntry3new(3, _M0L3ne2S1764, _M0L3ne2S1764);
  _M0L9stdp__w22S1779
  = (void*)moonbit_malloc(sizeof(struct _M0DTP26RiantR8snn__mbt13STDPEntryKind10Gerstner__));
  Moonbit_object_header(_M0L9stdp__w22S1779)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 102, 0);
  ((struct _M0DTP26RiantR8snn__mbt13STDPEntryKind10Gerstner__*)_M0L9stdp__w22S1779)->$0
  = _M0L6_2atmpS1903;
  #line 57 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lagzi2022\\main.mbt"
  _M0L8stim__e1S1780
  = _M0MP26RiantR8snn__mbt17PoissonStimulusIF3new(_M0L2e1S1766, (moonbit_string_t)moonbit_string_literal_1.data, 0x1p-1f, _M0L3rngS1762);
  #line 58 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lagzi2022\\main.mbt"
  _M0L8stim__e2S1781
  = _M0MP26RiantR8snn__mbt17PoissonStimulusIF3new(_M0L2e2S1767, (moonbit_string_t)moonbit_string_literal_1.data, 0x1p-1f, _M0L3rngS1762);
  moonbit_decref(_M0L3rngS1762);
  #line 61 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lagzi2022\\main.mbt"
  _M0L6fm__e1S1782
  = _M0MP26RiantR8snn__mbt7Monitor9new__fire(_M0L2e1S1766, 0);
  #line 62 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lagzi2022\\main.mbt"
  _M0L6fm__e2S1783
  = _M0MP26RiantR8snn__mbt7Monitor9new__fire(_M0L2e2S1767, 0);
  _M0L4IF__S1901
  = (void*)moonbit_malloc(sizeof(struct _M0DTP26RiantR8snn__mbt6AnyPop4IF__));
  Moonbit_object_header(_M0L4IF__S1901)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 105, 0);
  ((struct _M0DTP26RiantR8snn__mbt6AnyPop4IF__*)_M0L4IF__S1901)->$0
  = _M0L2e1S1766;
  _M0L4IF__S1902
  = (void*)moonbit_malloc(sizeof(struct _M0DTP26RiantR8snn__mbt6AnyPop4IF__));
  Moonbit_object_header(_M0L4IF__S1902)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 105, 0);
  ((struct _M0DTP26RiantR8snn__mbt6AnyPop4IF__*)_M0L4IF__S1902)->$0
  = _M0L2e2S1767;
  _M0L6_2atmpS1900 = (void**)moonbit_make_ref_array_raw(2);
  _M0L6_2atmpS1900[0] = _M0L4IF__S1901;
  _M0L6_2atmpS1900[1] = _M0L4IF__S1902;
  _M0L6_2atmpS1885
  = (struct _M0TPB5ArrayGRP26RiantR8snn__mbt6AnyPopE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGRP26RiantR8snn__mbt6AnyPopE));
  Moonbit_object_header(_M0L6_2atmpS1885)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 108, 0);
  _M0L6_2atmpS1885->$0 = _M0L6_2atmpS1900;
  _M0L6_2atmpS1885->$1 = 2;
  moonbit_incref(_M0L3w11S1774);
  moonbit_incref(_M0L3w22S1777);
  _M0L6_2atmpS1899
  = (struct _M0TP26RiantR8snn__mbt14SpikingSynapse**)moonbit_make_ref_array_raw(4);
  _M0L6_2atmpS1899[0] = _M0L3w11S1774;
  _M0L6_2atmpS1899[1] = _M0L3w12S1775;
  _M0L6_2atmpS1899[2] = _M0L3w21S1776;
  _M0L6_2atmpS1899[3] = _M0L3w22S1777;
  _M0L6_2atmpS1886
  = (struct _M0TPB5ArrayGRP26RiantR8snn__mbt14SpikingSynapseE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGRP26RiantR8snn__mbt14SpikingSynapseE));
  Moonbit_object_header(_M0L6_2atmpS1886)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 111, 0);
  _M0L6_2atmpS1886->$0 = _M0L6_2atmpS1899;
  _M0L6_2atmpS1886->$1 = 4;
  _M0L11PoissonIF__S1897
  = (void*)moonbit_malloc(sizeof(struct _M0DTP26RiantR8snn__mbt7AnyStim11PoissonIF__));
  Moonbit_object_header(_M0L11PoissonIF__S1897)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 114, 0);
  ((struct _M0DTP26RiantR8snn__mbt7AnyStim11PoissonIF__*)_M0L11PoissonIF__S1897)->$0
  = _M0L8stim__e1S1780;
  _M0L11PoissonIF__S1898
  = (void*)moonbit_malloc(sizeof(struct _M0DTP26RiantR8snn__mbt7AnyStim11PoissonIF__));
  Moonbit_object_header(_M0L11PoissonIF__S1898)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 114, 0);
  ((struct _M0DTP26RiantR8snn__mbt7AnyStim11PoissonIF__*)_M0L11PoissonIF__S1898)->$0
  = _M0L8stim__e2S1781;
  _M0L6_2atmpS1896 = (void**)moonbit_make_ref_array_raw(2);
  _M0L6_2atmpS1896[0] = _M0L11PoissonIF__S1897;
  _M0L6_2atmpS1896[1] = _M0L11PoissonIF__S1898;
  _M0L6_2atmpS1895
  = (struct _M0TPB5ArrayGRP26RiantR8snn__mbt7AnyStimE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGRP26RiantR8snn__mbt7AnyStimE));
  Moonbit_object_header(_M0L6_2atmpS1895)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 0, 0);
  _M0L6_2atmpS1895->$0 = _M0L6_2atmpS1896;
  _M0L6_2atmpS1895->$1 = 2;
  _M0L6_2atmpS1887 = _M0L6_2atmpS1895;
  moonbit_incref(_M0L6fm__e1S1782);
  moonbit_incref(_M0L6fm__e2S1783);
  _M0L6_2atmpS1894
  = (struct _M0TP26RiantR8snn__mbt7Monitor**)moonbit_make_ref_array_raw(2);
  _M0L6_2atmpS1894[0] = _M0L6fm__e1S1782;
  _M0L6_2atmpS1894[1] = _M0L6fm__e2S1783;
  _M0L6_2atmpS1893
  = (struct _M0TPB5ArrayGRP26RiantR8snn__mbt7MonitorE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGRP26RiantR8snn__mbt7MonitorE));
  Moonbit_object_header(_M0L6_2atmpS1893)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 3, 0);
  _M0L6_2atmpS1893->$0 = _M0L6_2atmpS1894;
  _M0L6_2atmpS1893->$1 = 2;
  _M0L6_2atmpS1888 = _M0L6_2atmpS1893;
  _M0L6_2atmpS1892 = (void**)moonbit_make_ref_array_raw(2);
  _M0L6_2atmpS1892[0] = _M0L9stdp__w11S1778;
  _M0L6_2atmpS1892[1] = _M0L9stdp__w22S1779;
  _M0L6_2atmpS1891
  = (struct _M0TPB5ArrayGRP26RiantR8snn__mbt13STDPEntryKindE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGRP26RiantR8snn__mbt13STDPEntryKindE));
  Moonbit_object_header(_M0L6_2atmpS1891)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 6, 0);
  _M0L6_2atmpS1891->$0 = _M0L6_2atmpS1892;
  _M0L6_2atmpS1891->$1 = 2;
  _M0L6_2atmpS1889 = _M0L6_2atmpS1891;
  _M0L6_2atmpS1890 = 0;
  #line 63 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lagzi2022\\main.mbt"
  _M0L5modelS1784
  = _M0FP26RiantR8snn__mbt7compose(_M0L6_2atmpS1885, _M0L6_2atmpS1886, _M0L6_2atmpS1887, _M0L6_2atmpS1888, _M0L6_2atmpS1889, _M0L6_2atmpS1890);
  moonbit_decref(_M0L6_2atmpS1885);
  moonbit_decref(_M0L6_2atmpS1886);
  if (_M0L6_2atmpS1887) {
    moonbit_decref(_M0L6_2atmpS1887);
  }
  if (_M0L6_2atmpS1888) {
    moonbit_decref(_M0L6_2atmpS1888);
  }
  if (_M0L6_2atmpS1889) {
    moonbit_decref(_M0L6_2atmpS1889);
  }
  if (_M0L6_2atmpS1890) {
    moonbit_decref(_M0L6_2atmpS1890);
  }
  _M0L12duration__msS1785 = 0x1.f4p+9f;
  _M0L6_2atmpS1884 = _M0L12duration__msS1785 / _M0L2dtS1765;
  #line 73 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lagzi2022\\main.mbt"
  _M0L5stepsS1786 = _M0MPC15float5Float7to__int(_M0L6_2atmpS1884);
  _M0L7_2abindS1787 = 0;
  _M0L2__S1788 = _M0L7_2abindS1787;
  while (1) {
    if (_M0L2__S1788 < _M0L5stepsS1786) {
      int32_t _M0L6_2atmpS1829;
      #line 75 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lagzi2022\\main.mbt"
      _M0FP26RiantR8snn__mbt19step__heterogeneous(_M0L5modelS1784, _M0L2dtS1765);
      _M0L6_2atmpS1829 = _M0L2__S1788 + 1;
      _M0L2__S1788 = _M0L6_2atmpS1829;
      continue;
    } else {
      moonbit_decref(_M0L5modelS1784);
    }
    break;
  }
  #line 78 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lagzi2022\\main.mbt"
  _M0L6e1__hzS1790
  = _M0MP26RiantR8snn__mbt7Monitor12firing__rate(_M0L6fm__e1S1782);
  moonbit_decref(_M0L6fm__e1S1782);
  #line 79 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lagzi2022\\main.mbt"
  _M0L6e2__hzS1791
  = _M0MP26RiantR8snn__mbt7Monitor12firing__rate(_M0L6fm__e2S1783);
  moonbit_decref(_M0L6fm__e2S1783);
  _M0L6matrixS1883 = _M0L3w11S1774->$4;
  moonbit_incref(_M0L6matrixS1883);
  #line 80 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lagzi2022\\main.mbt"
  _M0L6_2atmpS1882
  = _M0MP26RiantR8snn__mbt15SparseMatrixCSR3nnz(_M0L6matrixS1883);
  moonbit_decref(_M0L6matrixS1883);
  _M0L6_2atmpS1881 = (float)_M0L6_2atmpS1882;
  _M0L17w11__sum__initialS1792 = _M0L6_2atmpS1881 * 0x1.4p+3f;
  _M0L6matrixS1880 = _M0L3w22S1777->$4;
  moonbit_incref(_M0L6matrixS1880);
  #line 81 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lagzi2022\\main.mbt"
  _M0L6_2atmpS1879
  = _M0MP26RiantR8snn__mbt15SparseMatrixCSR3nnz(_M0L6matrixS1880);
  moonbit_decref(_M0L6matrixS1880);
  _M0L6_2atmpS1878 = (float)_M0L6_2atmpS1879;
  _M0L17w22__sum__initialS1793 = _M0L6_2atmpS1878 * 0x1.4p+3f;
  _M0L8w11__sumS1794
  = (struct _M0TPB8MutLocalGfE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGfE));
  Moonbit_object_header(_M0L8w11__sumS1794)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L8w11__sumS1794->$0 = 0x0p+0f;
  _M0L8w22__sumS1795
  = (struct _M0TPB8MutLocalGfE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGfE));
  Moonbit_object_header(_M0L8w22__sumS1795)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L8w22__sumS1795->$0 = 0x0p+0f;
  _M0L1kS1796
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1kS1796)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1kS1796->$0 = 0;
  while (1) {
    int32_t _M0L3valS1830 = _M0L1kS1796->$0;
    struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS1833 =
      _M0L3w11S1774->$4;
    struct _M0TPB5ArrayGfE* _M0L4valsS1832 = _M0L6matrixS1833->$4;
    int32_t _M0L6_2atmpS1831;
    moonbit_incref(_M0L4valsS1832);
    #line 85 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lagzi2022\\main.mbt"
    _M0L6_2atmpS1831 = _M0MPC15array5Array6lengthGfE(_M0L4valsS1832);
    moonbit_decref(_M0L4valsS1832);
    if (_M0L3valS1830 < _M0L6_2atmpS1831) {
      float _M0L3valS1835 = _M0L8w11__sumS1794->$0;
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS1839 =
        _M0L3w11S1774->$4;
      struct _M0TPB5ArrayGfE* _M0L4valsS1837 = _M0L6matrixS1839->$4;
      int32_t _M0L3valS1838 = _M0L1kS1796->$0;
      float _M0L6_2atmpS1836;
      float _M0L6_2atmpS1834;
      int32_t _M0L3valS1841;
      int32_t _M0L6_2atmpS1840;
      moonbit_incref(_M0L4valsS1837);
      #line 86 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lagzi2022\\main.mbt"
      _M0L6_2atmpS1836
      = _M0MPC15array5Array2atGfE(_M0L4valsS1837, _M0L3valS1838);
      moonbit_decref(_M0L4valsS1837);
      _M0L6_2atmpS1834 = _M0L3valS1835 + _M0L6_2atmpS1836;
      _M0L8w11__sumS1794->$0 = _M0L6_2atmpS1834;
      _M0L3valS1841 = _M0L1kS1796->$0;
      _M0L6_2atmpS1840 = _M0L3valS1841 + 1;
      _M0L1kS1796->$0 = _M0L6_2atmpS1840;
      continue;
    } else {
      moonbit_decref(_M0L1kS1796);
    }
    break;
  }
  _M0L2k2S1798
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L2k2S1798)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L2k2S1798->$0 = 0;
  while (1) {
    int32_t _M0L3valS1842 = _M0L2k2S1798->$0;
    struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS1845 =
      _M0L3w22S1777->$4;
    struct _M0TPB5ArrayGfE* _M0L4valsS1844 = _M0L6matrixS1845->$4;
    int32_t _M0L6_2atmpS1843;
    moonbit_incref(_M0L4valsS1844);
    #line 90 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lagzi2022\\main.mbt"
    _M0L6_2atmpS1843 = _M0MPC15array5Array6lengthGfE(_M0L4valsS1844);
    moonbit_decref(_M0L4valsS1844);
    if (_M0L3valS1842 < _M0L6_2atmpS1843) {
      float _M0L3valS1847 = _M0L8w22__sumS1795->$0;
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS1851 =
        _M0L3w22S1777->$4;
      struct _M0TPB5ArrayGfE* _M0L4valsS1849 = _M0L6matrixS1851->$4;
      int32_t _M0L3valS1850 = _M0L2k2S1798->$0;
      float _M0L6_2atmpS1848;
      float _M0L6_2atmpS1846;
      int32_t _M0L3valS1853;
      int32_t _M0L6_2atmpS1852;
      moonbit_incref(_M0L4valsS1849);
      #line 91 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lagzi2022\\main.mbt"
      _M0L6_2atmpS1848
      = _M0MPC15array5Array2atGfE(_M0L4valsS1849, _M0L3valS1850);
      moonbit_decref(_M0L4valsS1849);
      _M0L6_2atmpS1846 = _M0L3valS1847 + _M0L6_2atmpS1848;
      _M0L8w22__sumS1795->$0 = _M0L6_2atmpS1846;
      _M0L3valS1853 = _M0L2k2S1798->$0;
      _M0L6_2atmpS1852 = _M0L3valS1853 + 1;
      _M0L2k2S1798->$0 = _M0L6_2atmpS1852;
      continue;
    } else {
      moonbit_decref(_M0L2k2S1798);
    }
    break;
  }
  #line 97 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lagzi2022\\main.mbt"
  _M0FPB7printlnGsE((moonbit_string_t)moonbit_string_literal_20.data);
  #line 98 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lagzi2022\\main.mbt"
  _M0L6_2atmpS1856 = _M0MPC13int3Int18to__string_2einner(_M0L3ne1S1763, 10);
  #line 98 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lagzi2022\\main.mbt"
  _M0L6_2atmpS1855
  = moonbit_add_string((moonbit_string_t)moonbit_string_literal_21.data, _M0L6_2atmpS1856);
  moonbit_decref(_M0L6_2atmpS1856);
  #line 98 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lagzi2022\\main.mbt"
  _M0L6_2atmpS1854
  = moonbit_add_string(_M0L6_2atmpS1855, (moonbit_string_t)moonbit_string_literal_22.data);
  moonbit_decref(_M0L6_2atmpS1855);
  #line 98 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lagzi2022\\main.mbt"
  _M0FPB7printlnGsE(_M0L6_2atmpS1854);
  moonbit_decref(_M0L6_2atmpS1854);
  #line 99 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lagzi2022\\main.mbt"
  _M0L6_2atmpS1859 = _M0MPC13int3Int18to__string_2einner(_M0L3ne2S1764, 10);
  #line 99 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lagzi2022\\main.mbt"
  _M0L6_2atmpS1858
  = moonbit_add_string((moonbit_string_t)moonbit_string_literal_23.data, _M0L6_2atmpS1859);
  moonbit_decref(_M0L6_2atmpS1859);
  #line 99 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lagzi2022\\main.mbt"
  _M0L6_2atmpS1857
  = moonbit_add_string(_M0L6_2atmpS1858, (moonbit_string_t)moonbit_string_literal_22.data);
  moonbit_decref(_M0L6_2atmpS1858);
  #line 99 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lagzi2022\\main.mbt"
  _M0FPB7printlnGsE(_M0L6_2atmpS1857);
  moonbit_decref(_M0L6_2atmpS1857);
  _M0L6matrixS1863 = _M0L3w11S1774->$4;
  _M0L6_2acntS5283 = Moonbit_rc_count(Moonbit_object_header(_M0L3w11S1774));
  if (_M0L6_2acntS5283 > 1) {
    int32_t _M0L11_2anew__cntS5293 = _M0L6_2acntS5283 - 1;
    Moonbit_set_rc_count(Moonbit_object_header(_M0L3w11S1774), _M0L11_2anew__cntS5293);
    moonbit_incref(_M0L6matrixS1863);
  } else if (_M0L6_2acntS5283 == 1) {
    struct _M0TPB5ArrayGfE* _M0L8_2afieldS5292 = _M0L3w11S1774->$9;
    struct _M0TPB5ArrayGiE* _M0L8_2afieldS5291;
    struct _M0TPB5ArrayGfE* _M0L8_2afieldS5290;
    struct _M0TPB5ArrayGfE* _M0L8_2afieldS5289;
    struct _M0TPB5ArrayGfE* _M0L8_2afieldS5288;
    moonbit_string_t _M0L8_2afieldS5287;
    moonbit_string_t _M0L8_2afieldS5286;
    struct _M0TP26RiantR8snn__mbt2IF* _M0L8_2afieldS5285;
    struct _M0TP26RiantR8snn__mbt2IF* _M0L8_2afieldS5284;
    moonbit_decref(_M0L8_2afieldS5292);
    _M0L8_2afieldS5291 = _M0L3w11S1774->$8;
    moonbit_decref(_M0L8_2afieldS5291);
    _M0L8_2afieldS5290 = _M0L3w11S1774->$7;
    moonbit_decref(_M0L8_2afieldS5290);
    _M0L8_2afieldS5289 = _M0L3w11S1774->$6;
    moonbit_decref(_M0L8_2afieldS5289);
    _M0L8_2afieldS5288 = _M0L3w11S1774->$5;
    moonbit_decref(_M0L8_2afieldS5288);
    _M0L8_2afieldS5287 = _M0L3w11S1774->$3;
    moonbit_decref(_M0L8_2afieldS5287);
    _M0L8_2afieldS5286 = _M0L3w11S1774->$2;
    moonbit_decref(_M0L8_2afieldS5286);
    _M0L8_2afieldS5285 = _M0L3w11S1774->$1;
    moonbit_decref(_M0L8_2afieldS5285);
    _M0L8_2afieldS5284 = _M0L3w11S1774->$0;
    moonbit_decref(_M0L8_2afieldS5284);
    #line 89 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lagzi2022\\main.mbt"
    moonbit_free(_M0L3w11S1774);
  }
  #line 100 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lagzi2022\\main.mbt"
  _M0L6_2atmpS1862
  = _M0MP26RiantR8snn__mbt15SparseMatrixCSR3nnz(_M0L6matrixS1863);
  moonbit_decref(_M0L6matrixS1863);
  #line 100 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lagzi2022\\main.mbt"
  _M0L6_2atmpS1861
  = _M0MPC13int3Int18to__string_2einner(_M0L6_2atmpS1862, 10);
  #line 100 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lagzi2022\\main.mbt"
  _M0L6_2atmpS1860
  = moonbit_add_string((moonbit_string_t)moonbit_string_literal_24.data, _M0L6_2atmpS1861);
  moonbit_decref(_M0L6_2atmpS1861);
  #line 100 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lagzi2022\\main.mbt"
  _M0FPB7printlnGsE(_M0L6_2atmpS1860);
  moonbit_decref(_M0L6_2atmpS1860);
  _M0L6matrixS1867 = _M0L3w22S1777->$4;
  _M0L6_2acntS5294 = Moonbit_rc_count(Moonbit_object_header(_M0L3w22S1777));
  if (_M0L6_2acntS5294 > 1) {
    int32_t _M0L11_2anew__cntS5304 = _M0L6_2acntS5294 - 1;
    Moonbit_set_rc_count(Moonbit_object_header(_M0L3w22S1777), _M0L11_2anew__cntS5304);
    moonbit_incref(_M0L6matrixS1867);
  } else if (_M0L6_2acntS5294 == 1) {
    struct _M0TPB5ArrayGfE* _M0L8_2afieldS5303 = _M0L3w22S1777->$9;
    struct _M0TPB5ArrayGiE* _M0L8_2afieldS5302;
    struct _M0TPB5ArrayGfE* _M0L8_2afieldS5301;
    struct _M0TPB5ArrayGfE* _M0L8_2afieldS5300;
    struct _M0TPB5ArrayGfE* _M0L8_2afieldS5299;
    moonbit_string_t _M0L8_2afieldS5298;
    moonbit_string_t _M0L8_2afieldS5297;
    struct _M0TP26RiantR8snn__mbt2IF* _M0L8_2afieldS5296;
    struct _M0TP26RiantR8snn__mbt2IF* _M0L8_2afieldS5295;
    moonbit_decref(_M0L8_2afieldS5303);
    _M0L8_2afieldS5302 = _M0L3w22S1777->$8;
    moonbit_decref(_M0L8_2afieldS5302);
    _M0L8_2afieldS5301 = _M0L3w22S1777->$7;
    moonbit_decref(_M0L8_2afieldS5301);
    _M0L8_2afieldS5300 = _M0L3w22S1777->$6;
    moonbit_decref(_M0L8_2afieldS5300);
    _M0L8_2afieldS5299 = _M0L3w22S1777->$5;
    moonbit_decref(_M0L8_2afieldS5299);
    _M0L8_2afieldS5298 = _M0L3w22S1777->$3;
    moonbit_decref(_M0L8_2afieldS5298);
    _M0L8_2afieldS5297 = _M0L3w22S1777->$2;
    moonbit_decref(_M0L8_2afieldS5297);
    _M0L8_2afieldS5296 = _M0L3w22S1777->$1;
    moonbit_decref(_M0L8_2afieldS5296);
    _M0L8_2afieldS5295 = _M0L3w22S1777->$0;
    moonbit_decref(_M0L8_2afieldS5295);
    #line 89 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lagzi2022\\main.mbt"
    moonbit_free(_M0L3w22S1777);
  }
  #line 101 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lagzi2022\\main.mbt"
  _M0L6_2atmpS1866
  = _M0MP26RiantR8snn__mbt15SparseMatrixCSR3nnz(_M0L6matrixS1867);
  moonbit_decref(_M0L6matrixS1867);
  #line 101 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lagzi2022\\main.mbt"
  _M0L6_2atmpS1865
  = _M0MPC13int3Int18to__string_2einner(_M0L6_2atmpS1866, 10);
  #line 101 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lagzi2022\\main.mbt"
  _M0L6_2atmpS1864
  = moonbit_add_string((moonbit_string_t)moonbit_string_literal_25.data, _M0L6_2atmpS1865);
  moonbit_decref(_M0L6_2atmpS1865);
  #line 101 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lagzi2022\\main.mbt"
  _M0FPB7printlnGsE(_M0L6_2atmpS1864);
  moonbit_decref(_M0L6_2atmpS1864);
  #line 102 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lagzi2022\\main.mbt"
  _M0FPB7printlnGsE((moonbit_string_t)moonbit_string_literal_26.data);
  #line 103 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lagzi2022\\main.mbt"
  _M0FPB7printlnGsE((moonbit_string_t)moonbit_string_literal_27.data);
  #line 104 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lagzi2022\\main.mbt"
  _M0L6_2atmpS1869 = _M0IPC15float5FloatPB4Show10to__string(_M0L6e1__hzS1790);
  #line 104 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lagzi2022\\main.mbt"
  _M0L6_2atmpS1868
  = moonbit_add_string((moonbit_string_t)moonbit_string_literal_28.data, _M0L6_2atmpS1869);
  moonbit_decref(_M0L6_2atmpS1869);
  #line 104 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lagzi2022\\main.mbt"
  _M0FPB7printlnGsE(_M0L6_2atmpS1868);
  moonbit_decref(_M0L6_2atmpS1868);
  #line 105 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lagzi2022\\main.mbt"
  _M0L6_2atmpS1871 = _M0IPC15float5FloatPB4Show10to__string(_M0L6e2__hzS1791);
  #line 105 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lagzi2022\\main.mbt"
  _M0L6_2atmpS1870
  = moonbit_add_string((moonbit_string_t)moonbit_string_literal_29.data, _M0L6_2atmpS1871);
  moonbit_decref(_M0L6_2atmpS1871);
  #line 105 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lagzi2022\\main.mbt"
  _M0FPB7printlnGsE(_M0L6_2atmpS1870);
  moonbit_decref(_M0L6_2atmpS1870);
  _M0L3valS1874 = _M0L8w11__sumS1794->$0;
  moonbit_decref(_M0L8w11__sumS1794);
  #line 106 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lagzi2022\\main.mbt"
  _M0L6_2atmpS1873 = _M0IPC15float5FloatPB4Show10to__string(_M0L3valS1874);
  #line 106 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lagzi2022\\main.mbt"
  _M0L6_2atmpS1872
  = moonbit_add_string((moonbit_string_t)moonbit_string_literal_30.data, _M0L6_2atmpS1873);
  moonbit_decref(_M0L6_2atmpS1873);
  #line 106 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lagzi2022\\main.mbt"
  _M0FPB7printlnGsE(_M0L6_2atmpS1872);
  moonbit_decref(_M0L6_2atmpS1872);
  _M0L3valS1877 = _M0L8w22__sumS1795->$0;
  moonbit_decref(_M0L8w22__sumS1795);
  #line 107 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lagzi2022\\main.mbt"
  _M0L6_2atmpS1876 = _M0IPC15float5FloatPB4Show10to__string(_M0L3valS1877);
  #line 107 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lagzi2022\\main.mbt"
  _M0L6_2atmpS1875
  = moonbit_add_string((moonbit_string_t)moonbit_string_literal_31.data, _M0L6_2atmpS1876);
  moonbit_decref(_M0L6_2atmpS1876);
  #line 107 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lagzi2022\\main.mbt"
  _M0FPB7printlnGsE(_M0L6_2atmpS1875);
  moonbit_decref(_M0L6_2atmpS1875);
  return 0;
}