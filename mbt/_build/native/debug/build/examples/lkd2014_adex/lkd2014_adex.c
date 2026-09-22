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

struct _M0TP26RiantR8snn__mbt17MonitorAdExSinExp;

struct _M0TP26RiantR8snn__mbt13AdExPostSpike;

struct _M0TPB4Show;

struct _M0TPB13StringBuilder;

struct _M0TPB5ArrayGfE;

struct _M0TPB17FloatingDecimal64;

struct _M0TP26RiantR8snn__mbt9PostSpike;

struct _M0TP26RiantR8snn__mbt17PoissonStimulusIF;

struct _M0TP26RiantR8snn__mbt10AdExSinExp;

struct _M0TP26RiantR8snn__mbt19AdExSinExpParameter;

struct _M0TPB5ArrayGbE;

struct _M0BTPB6Logger;

struct _M0TPB8MutLocalGdE;

struct _M0BTPB4Show;

struct _M0TPC16string10StringView;

struct _M0TP26RiantR8snn__mbt2IF;

struct _M0KTPB6LoggerTPB13StringBuilder;

struct _M0TPB6Logger;

struct _M0TP26RiantR8snn__mbt12PoissonFixed;

struct _M0TP26RiantR8snn__mbt7Xoshiro;

struct _M0TUmmmmE;

struct _M0TPB5ArrayGiE;

struct _M0TPB19MulShiftAll64Result;

struct _M0TP26RiantR8snn__mbt11IFParameter;

struct _M0TPB7Umul128;

struct _M0TPB8Pow5Pair;

struct _M0TPB8MutLocalGiE {
  int32_t $0;
  
};

struct _M0TP26RiantR8snn__mbt17MonitorAdExSinExp {
  struct _M0TP26RiantR8snn__mbt10AdExSinExp* $0;
  moonbit_string_t $1;
  struct _M0TPB5ArrayGfE* $2;
  struct _M0TPB5ArrayGfE* $3;
  int32_t $4;
  
};

struct _M0TP26RiantR8snn__mbt13AdExPostSpike {
  float $0;
  float $1;
  float $2;
  float $3;
  float $4;
  
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

struct _M0TP26RiantR8snn__mbt9PostSpike {
  float $0;
  
};

struct _M0TP26RiantR8snn__mbt17PoissonStimulusIF {
  struct _M0TP26RiantR8snn__mbt12PoissonFixed* $0;
  struct _M0TPB5ArrayGiE* $1;
  struct _M0TPB5ArrayGfE* $2;
  struct _M0TP26RiantR8snn__mbt7Xoshiro* $3;
  
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

struct _M0TPB8MutLocalGdE {
  double $0;
  
};

struct _M0BTPB4Show {
  int32_t(* $method_0)(void*, struct _M0TPB6Logger);
  moonbit_string_t(* $method_1)(void*);
  
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

struct _M0TP26RiantR8snn__mbt12PoissonFixed {
  float $0;
  float $1;
  struct _M0TPB5ArrayGbE* $2;
  
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

struct _M0TPB7Umul128 {
  uint64_t $0;
  uint64_t $1;
  
};

struct _M0TPB8Pow5Pair {
  uint64_t $0;
  uint64_t $1;
  
};

int32_t _M0FP26RiantR8snn__mbt19record__one__sinexp(
  struct _M0TP26RiantR8snn__mbt17MonitorAdExSinExp*,
  float
);

struct _M0TP26RiantR8snn__mbt17MonitorAdExSinExp* _M0MP26RiantR8snn__mbt17MonitorAdExSinExp9new__fire(
  struct _M0TP26RiantR8snn__mbt10AdExSinExp*,
  int32_t
);

struct _M0TP26RiantR8snn__mbt10AdExSinExp* _M0MP26RiantR8snn__mbt10AdExSinExp3new(
  int32_t,
  struct _M0TP26RiantR8snn__mbt19AdExSinExpParameter*,
  struct _M0TP26RiantR8snn__mbt7Xoshiro*
);

struct _M0TP26RiantR8snn__mbt10AdExSinExp* _M0MP26RiantR8snn__mbt10AdExSinExp16new__with__spike(
  int32_t,
  struct _M0TP26RiantR8snn__mbt19AdExSinExpParameter*,
  struct _M0TP26RiantR8snn__mbt13AdExPostSpike*,
  struct _M0TP26RiantR8snn__mbt7Xoshiro*
);

struct _M0TP26RiantR8snn__mbt19AdExSinExpParameter* _M0MP26RiantR8snn__mbt19AdExSinExpParameter9lkd__adex(
  
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

struct _M0TP26RiantR8snn__mbt13AdExPostSpike* _M0MP26RiantR8snn__mbt13AdExPostSpike3new(
  
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

float _M0FP26RiantR8snn__mbt21monitor__firing__rate(struct _M0TPB5ArrayGfE*);

int32_t _M0FP26RiantR8snn__mbt22monitor__count__spikes(
  struct _M0TPB5ArrayGfE*
);

int32_t _M0FP26RiantR8snn__mbt10count__nnz(struct _M0TPB5ArrayGfE*);

int32_t _M0FP26RiantR8snn__mbt18simulate__step__if(
  struct _M0TP26RiantR8snn__mbt2IF*,
  float
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

int32_t _M0FP26RiantR8snn__mbt20route__pre__to__post(
  struct _M0TP26RiantR8snn__mbt2IF*,
  struct _M0TP26RiantR8snn__mbt10AdExSinExp*,
  struct _M0TPB5ArrayGfE*,
  int32_t,
  int32_t
);

struct _M0TPB5ArrayGfE* _M0FP26RiantR8snn__mbt21make__random__weights(
  int32_t,
  int32_t,
  float,
  float,
  struct _M0TP26RiantR8snn__mbt7Xoshiro*
);

struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0MP26RiantR8snn__mbt7Xoshiro3new(
  uint64_t
);

struct _M0TUmmmmE* _M0FP26RiantR8snn__mbt10splitmix64(uint64_t);

uint64_t _M0FP26RiantR8snn__mbt5mix64(uint64_t);

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

int32_t _M0FP26RiantR8snn__mbt15sample__poisson(
  struct _M0TP26RiantR8snn__mbt7Xoshiro*,
  float
);

double _M0FP26RiantR8snn__mbt9next__f64(
  struct _M0TP26RiantR8snn__mbt7Xoshiro*
);

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

moonbit_string_t _M0IPC15float5FloatPB4Show10to__string(float);

int32_t _M0MPC15float5Float7to__int(float);

struct _M0TPB5ArrayGfE* _M0MPC15array5Array4makeGfE(int32_t, float);

struct _M0TPB5ArrayGbE* _M0MPC15array5Array4makeGbE(int32_t, int32_t);

struct _M0TPB5ArrayGiE* _M0MPC15array5Array4makeGiE(int32_t, int32_t);

int32_t _M0MPC15array5Array3setGfE(struct _M0TPB5ArrayGfE*, int32_t, float);

int32_t _M0MPC15array5Array3setGbE(struct _M0TPB5ArrayGbE*, int32_t, int32_t);

int32_t _M0MPC15array5Array3setGiE(struct _M0TPB5ArrayGiE*, int32_t, int32_t);

int32_t _M0MPC15array5Array2atGbE(struct _M0TPB5ArrayGbE*, int32_t);

float _M0MPC15array5Array2atGfE(struct _M0TPB5ArrayGfE*, int32_t);

int32_t _M0MPC15array5Array2atGiE(struct _M0TPB5ArrayGiE*, int32_t);

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

float expf(float);

struct { int32_t rc; uint32_t meta; uint16_t const data[1]; 
} const moonbit_string_literal_8 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 0, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[16]; 
} const moonbit_string_literal_18 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 15, 44, 32, 
    100, 115, 116, 95, 111, 102, 102, 115, 101, 116, 32, 61, 32, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[19]; 
} const moonbit_string_literal_14 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 18, 105, 110, 
    118, 97, 108, 105, 100, 32, 99, 111, 100, 101, 32, 112, 111, 105, 
    110, 116, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[2]; 
} const moonbit_string_literal_7 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 1, 45, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[19]; 
} const moonbit_string_literal_31 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 18, 32, 99, 
    111, 110, 110, 115, 32, 40, 956, 61, 52, 56, 46, 55, 32, 110, 83, 
    41, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[45]; 
} const moonbit_string_literal_25 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 44, 32, 65, 
    100, 69, 120, 83, 105, 110, 69, 120, 112, 32, 110, 101, 117, 114, 
    111, 110, 115, 32, 40, 69, 108, 61, 45, 54, 50, 44, 32, 86, 116, 
    61, 45, 53, 50, 44, 32, 86, 114, 61, 45, 54, 48, 41, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[31]; 
} const moonbit_string_literal_12 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 30, 114, 97, 
    100, 105, 120, 32, 109, 117, 115, 116, 32, 98, 101, 32, 98, 101, 
    116, 119, 101, 101, 110, 32, 50, 32, 97, 110, 100, 32, 51, 54, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[9]; 
} const moonbit_string_literal_9 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 8, 73, 110, 
    102, 105, 110, 105, 116, 121, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[4]; 
} const moonbit_string_literal_6 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 3, 78, 97, 78, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[2]; 
} const moonbit_string_literal_2 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 1, 119, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[6]; 
} const moonbit_string_literal_22 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 5, 32, 32, 
    69, 58, 32, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[2]; 
} const moonbit_string_literal_4 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 1, 48, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[39]; 
} const moonbit_string_literal_35 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 38, 108, 107, 
    100, 50, 48, 49, 52, 95, 97, 100, 101, 120, 46, 109, 98, 116, 58, 
    32, 115, 105, 109, 117, 108, 97, 116, 105, 111, 110, 32, 100, 111, 
    110, 101, 32, 40, 49, 115, 41, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[19]; 
} const moonbit_string_literal_27 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 18, 32, 99, 
    111, 110, 110, 115, 32, 40, 956, 61, 50, 46, 55, 54, 32, 110, 83, 
    41, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[45]; 
} const moonbit_string_literal_23 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 44, 32, 65, 
    100, 69, 120, 83, 105, 110, 69, 120, 112, 32, 110, 101, 117, 114, 
    111, 110, 115, 32, 40, 69, 108, 61, 45, 55, 48, 44, 32, 86, 116, 
    61, 45, 53, 50, 44, 32, 86, 114, 61, 45, 54, 48, 41, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[9]; 
} const moonbit_string_literal_19 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 8, 44, 32, 
    108, 101, 110, 32, 61, 32, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[5]; 
} const moonbit_string_literal_38 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 4, 32, 72, 
    122, 41, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[37]; 
} const moonbit_string_literal_16 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 36, 98, 111, 
    117, 110, 100, 115, 32, 99, 104, 101, 99, 107, 32, 102, 97, 105, 
    108, 101, 100, 58, 32, 97, 108, 108, 111, 99, 97, 116, 101, 95, 108, 
    101, 110, 32, 61, 32, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[19]; 
} const moonbit_string_literal_33 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 18, 32, 99, 
    111, 110, 110, 115, 32, 40, 956, 61, 49, 54, 46, 50, 32, 110, 83, 
    41, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[7]; 
} const moonbit_string_literal_30 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 6, 32, 32, 
    73, 69, 58, 32, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[16]; 
} const moonbit_string_literal_36 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 15, 32, 32, 
    69, 91, 48, 93, 32, 115, 112, 105, 107, 101, 115, 58, 32, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[53]; 
} const moonbit_string_literal_34 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 52, 32, 32, 
    80, 111, 105, 115, 115, 111, 110, 58, 32, 52, 46, 53, 72, 122, 32, 
    111, 110, 32, 69, 44, 32, 50, 46, 53, 72, 122, 32, 111, 110, 32, 
    73, 32, 40, 115, 99, 97, 108, 101, 100, 32, 100, 111, 119, 110, 32, 
    49, 48, 48, 215, 41, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[3]; 
} const moonbit_string_literal_3 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 2, 103, 101, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[57]; 
} const moonbit_string_literal_21 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 56, 108, 107, 
    100, 50, 48, 49, 52, 95, 97, 100, 101, 120, 46, 109, 98, 116, 58, 
    32, 76, 75, 68, 32, 50, 48, 49, 52, 32, 118, 83, 84, 68, 80, 32, 
    110, 101, 116, 119, 111, 114, 107, 32, 119, 105, 116, 104, 32, 65, 
    100, 69, 120, 83, 105, 110, 69, 120, 112, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[7]; 
} const moonbit_string_literal_28 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 6, 32, 32, 
    69, 73, 58, 32, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[37]; 
} const moonbit_string_literal_13 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 36, 48, 49, 
    50, 51, 52, 53, 54, 55, 56, 57, 97, 98, 99, 100, 101, 102, 103, 104, 
    105, 106, 107, 108, 109, 110, 111, 112, 113, 114, 115, 116, 117, 
    118, 119, 120, 121, 122, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[16]; 
} const moonbit_string_literal_39 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 15, 32, 32, 
    73, 91, 48, 93, 32, 115, 112, 105, 107, 101, 115, 58, 32, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[3]; 
} const moonbit_string_literal_37 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 2, 32, 40, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[5]; 
} const moonbit_string_literal_1 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 4, 102, 105, 
    114, 101, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[16]; 
} const moonbit_string_literal_20 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 15, 44, 32, 
    115, 114, 99, 46, 108, 101, 110, 103, 116, 104, 32, 61, 32, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[6]; 
} const moonbit_string_literal_24 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 5, 32, 32, 
    73, 58, 32, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[16]; 
} const moonbit_string_literal_17 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 15, 44, 32, 
    115, 114, 99, 95, 111, 102, 102, 115, 101, 116, 32, 61, 32, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[2]; 
} const moonbit_string_literal_0 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 1, 118, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[19]; 
} const moonbit_string_literal_29 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 18, 32, 99, 
    111, 110, 110, 115, 32, 40, 956, 61, 49, 46, 50, 55, 32, 110, 83, 
    41, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[24]; 
} const moonbit_string_literal_11 =
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

struct { int32_t rc; uint32_t meta; uint16_t const data[7]; 
} const moonbit_string_literal_32 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 6, 32, 32, 
    73, 73, 58, 32, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[32]; 
} const moonbit_string_literal_15 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 31, 83, 116, 
    114, 105, 110, 103, 66, 117, 105, 108, 100, 101, 114, 32, 99, 97, 
    112, 97, 99, 105, 116, 121, 32, 111, 118, 101, 114, 102, 108, 111, 
    119, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[4]; 
} const moonbit_string_literal_10 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 3, 48, 46, 48, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[7]; 
} const moonbit_string_literal_26 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 6, 32, 32, 
    69, 69, 58, 32, 0
  };

uint32_t const moonbit_layout_table_data[62] =
  {
    sizeof(struct _M0TPB5ArrayGfE) / 4, 1,
    offsetof(struct _M0TPB5ArrayGfE, $0) / 4 * 2,
    sizeof(struct _M0TP26RiantR8snn__mbt17MonitorAdExSinExp) / 4, 4,
    offsetof(struct _M0TP26RiantR8snn__mbt17MonitorAdExSinExp, $0) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt17MonitorAdExSinExp, $1) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt17MonitorAdExSinExp, $2) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt17MonitorAdExSinExp, $3) / 4 * 2,
    sizeof(struct _M0TP26RiantR8snn__mbt10AdExSinExp) / 4, 15,
    offsetof(struct _M0TP26RiantR8snn__mbt10AdExSinExp, $0) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt10AdExSinExp, $1) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt10AdExSinExp, $3) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt10AdExSinExp, $4) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt10AdExSinExp, $5) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt10AdExSinExp, $6) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt10AdExSinExp, $7) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt10AdExSinExp, $8) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt10AdExSinExp, $9) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt10AdExSinExp, $10) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt10AdExSinExp, $11) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt10AdExSinExp, $12) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt10AdExSinExp, $13) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt10AdExSinExp, $14) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt10AdExSinExp, $15) / 4 * 2,
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
    sizeof(struct _M0TPB5ArrayGiE) / 4, 1,
    offsetof(struct _M0TPB5ArrayGiE, $0) / 4 * 2,
    sizeof(struct _M0TP26RiantR8snn__mbt17PoissonStimulusIF) / 4, 4,
    offsetof(struct _M0TP26RiantR8snn__mbt17PoissonStimulusIF, $0) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt17PoissonStimulusIF, $1) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt17PoissonStimulusIF, $2) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt17PoissonStimulusIF, $3) / 4 * 2,
    sizeof(struct _M0TPB5ArrayGbE) / 4, 1,
    offsetof(struct _M0TPB5ArrayGbE, $0) / 4 * 2,
    sizeof(struct _M0TP26RiantR8snn__mbt12PoissonFixed) / 4, 1,
    offsetof(struct _M0TP26RiantR8snn__mbt12PoissonFixed, $2) / 4 * 2,
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

int32_t _M0FP26RiantR8snn__mbt19record__one__sinexp(
  struct _M0TP26RiantR8snn__mbt17MonitorAdExSinExp* _M0L1mS859,
  float _M0L1tS860
) {
  moonbit_string_t _M0L3symS2029;
  float _M0L1vS858;
  struct _M0TPB5ArrayGfE* _M0L4dataS2027;
  struct _M0TPB5ArrayGfE* _M0L5timesS2028;
  #line 276 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
  _M0L3symS2029 = _M0L1mS859->$1;
  #line 277 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
  if (
    _M0L3symS2029 == (moonbit_string_t)moonbit_string_literal_0.data
    || Moonbit_array_length(_M0L3symS2029)
       == Moonbit_array_length((moonbit_string_t)moonbit_string_literal_0.data)
       && 0
          == memcmp(_M0L3symS2029, (moonbit_string_t)moonbit_string_literal_0.data, Moonbit_array_length(_M0L3symS2029) * 2)
  ) {
    struct _M0TP26RiantR8snn__mbt10AdExSinExp* _M0L3popS2032 = _M0L1mS859->$0;
    struct _M0TPB5ArrayGfE* _M0L1vS2030 = _M0L3popS2032->$3;
    int32_t _M0L6neuronS2031 = _M0L1mS859->$4;
    #line 278 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
    _M0L1vS858 = _M0MPC15array5Array2atGfE(_M0L1vS2030, _M0L6neuronS2031);
  } else {
    moonbit_string_t _M0L3symS2033 = _M0L1mS859->$1;
    #line 279 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
    if (
      _M0L3symS2033 == (moonbit_string_t)moonbit_string_literal_1.data
      || Moonbit_array_length(_M0L3symS2033)
         == Moonbit_array_length((moonbit_string_t)moonbit_string_literal_1.data)
         && 0
            == memcmp(_M0L3symS2033, (moonbit_string_t)moonbit_string_literal_1.data, Moonbit_array_length(_M0L3symS2033) * 2)
    ) {
      struct _M0TP26RiantR8snn__mbt10AdExSinExp* _M0L3popS2036 =
        _M0L1mS859->$0;
      struct _M0TPB5ArrayGbE* _M0L4fireS2034 = _M0L3popS2036->$5;
      int32_t _M0L6neuronS2035 = _M0L1mS859->$4;
      #line 280 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS2034, _M0L6neuronS2035)) {
        _M0L1vS858 = 0x1p+0f;
      } else {
        _M0L1vS858 = 0x0p+0f;
      }
    } else {
      moonbit_string_t _M0L3symS2037 = _M0L1mS859->$1;
      #line 281 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      if (
        _M0L3symS2037 == (moonbit_string_t)moonbit_string_literal_2.data
        || Moonbit_array_length(_M0L3symS2037)
           == Moonbit_array_length((moonbit_string_t)moonbit_string_literal_2.data)
           && 0
              == memcmp(_M0L3symS2037, (moonbit_string_t)moonbit_string_literal_2.data, Moonbit_array_length(_M0L3symS2037) * 2)
      ) {
        struct _M0TP26RiantR8snn__mbt10AdExSinExp* _M0L3popS2040 =
          _M0L1mS859->$0;
        struct _M0TPB5ArrayGfE* _M0L1wS2038 = _M0L3popS2040->$4;
        int32_t _M0L6neuronS2039 = _M0L1mS859->$4;
        #line 282 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
        _M0L1vS858 = _M0MPC15array5Array2atGfE(_M0L1wS2038, _M0L6neuronS2039);
      } else {
        _M0L1vS858 = 0x0p+0f;
      }
    }
  }
  _M0L4dataS2027 = _M0L1mS859->$2;
  #line 286 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
  _M0MPC15array5Array4pushGfE(_M0L4dataS2027, _M0L1vS858);
  _M0L5timesS2028 = _M0L1mS859->$3;
  #line 287 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
  _M0MPC15array5Array4pushGfE(_M0L5timesS2028, _M0L1tS860);
  return 0;
}

struct _M0TP26RiantR8snn__mbt17MonitorAdExSinExp* _M0MP26RiantR8snn__mbt17MonitorAdExSinExp9new__fire(
  struct _M0TP26RiantR8snn__mbt10AdExSinExp* _M0L3popS856,
  int32_t _M0L6neuronS857
) {
  float* _M0L6_2atmpS2026;
  struct _M0TPB5ArrayGfE* _M0L6_2atmpS2023;
  float* _M0L6_2atmpS2025;
  struct _M0TPB5ArrayGfE* _M0L6_2atmpS2024;
  struct _M0TP26RiantR8snn__mbt17MonitorAdExSinExp* _block_2060;
  #line 270 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
  _M0L6_2atmpS2026 = moonbit_empty_float_array;
  _M0L6_2atmpS2023
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_M0L6_2atmpS2023)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 0, 0);
  _M0L6_2atmpS2023->$0 = _M0L6_2atmpS2026;
  _M0L6_2atmpS2023->$1 = 0;
  _M0L6_2atmpS2025 = moonbit_empty_float_array;
  _M0L6_2atmpS2024
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_M0L6_2atmpS2024)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 0, 0);
  _M0L6_2atmpS2024->$0 = _M0L6_2atmpS2025;
  _M0L6_2atmpS2024->$1 = 0;
  moonbit_incref(_M0L3popS856);
  _block_2060
  = (struct _M0TP26RiantR8snn__mbt17MonitorAdExSinExp*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt17MonitorAdExSinExp));
  Moonbit_object_header(_block_2060)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 3, 0);
  _block_2060->$0 = _M0L3popS856;
  _block_2060->$1 = (moonbit_string_t)moonbit_string_literal_1.data;
  _block_2060->$2 = _M0L6_2atmpS2023;
  _block_2060->$3 = _M0L6_2atmpS2024;
  _block_2060->$4 = _M0L6neuronS857;
  return _block_2060;
}

struct _M0TP26RiantR8snn__mbt10AdExSinExp* _M0MP26RiantR8snn__mbt10AdExSinExp3new(
  int32_t _M0L1nS853,
  struct _M0TP26RiantR8snn__mbt19AdExSinExpParameter* _M0L5paramS854,
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS855
) {
  struct _M0TP26RiantR8snn__mbt13AdExPostSpike* _M0L6_2atmpS2022;
  struct _M0TP26RiantR8snn__mbt10AdExSinExp* _result_2061;
  #line 106 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
  #line 111 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
  _M0L6_2atmpS2022 = _M0MP26RiantR8snn__mbt13AdExPostSpike3new();
  #line 111 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
  _result_2061
  = _M0MP26RiantR8snn__mbt10AdExSinExp16new__with__spike(_M0L1nS853, _M0L5paramS854, _M0L6_2atmpS2022, _M0L3rngS855);
  moonbit_decref(_M0L6_2atmpS2022);
  return _result_2061;
}

struct _M0TP26RiantR8snn__mbt10AdExSinExp* _M0MP26RiantR8snn__mbt10AdExSinExp16new__with__spike(
  int32_t _M0L1nS833,
  struct _M0TP26RiantR8snn__mbt19AdExSinExpParameter* _M0L5paramS835,
  struct _M0TP26RiantR8snn__mbt13AdExPostSpike* _M0L5spikeS852,
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS838
) {
  struct _M0TPB5ArrayGfE* _M0L1vS832;
  float _M0L2vtS2020;
  float _M0L2vrS2021;
  float _M0L6spreadS834;
  int32_t _M0L7_2abindS836;
  int32_t _M0L1kS837;
  struct _M0TPB5ArrayGfE* _M0L1wS840;
  struct _M0TPB5ArrayGbE* _M0L4fireS841;
  float _M0L2vtS2019;
  struct _M0TPB5ArrayGfE* _M0L9thresholdS842;
  struct _M0TPB5ArrayGiE* _M0L4tabsS843;
  struct _M0TPB5ArrayGfE* _M0L1iS844;
  struct _M0TPB5ArrayGfE* _M0L9syn__currS845;
  struct _M0TPB5ArrayGfE* _M0L2geS846;
  struct _M0TPB5ArrayGfE* _M0L2giS847;
  struct _M0TPB5ArrayGfE* _M0L3gluS848;
  struct _M0TPB5ArrayGfE* _M0L4gabaS849;
  struct _M0TPB5ArrayGfE* _M0L7gsyn__eS850;
  struct _M0TPB5ArrayGfE* _M0L7gsyn__iS851;
  struct _M0TP26RiantR8snn__mbt10AdExSinExp* _block_2063;
  #line 116 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
  #line 122 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
  _M0L1vS832 = _M0MPC15array5Array4makeGfE(_M0L1nS833, 0x0p+0f);
  _M0L2vtS2020 = _M0L5paramS835->$2;
  _M0L2vrS2021 = _M0L5paramS835->$3;
  _M0L6spreadS834 = _M0L2vtS2020 - _M0L2vrS2021;
  _M0L7_2abindS836 = 0;
  _M0L1kS837 = _M0L7_2abindS836;
  while (1) {
    if (_M0L1kS837 < _M0L1nS833) {
      float _M0L2vrS2015 = _M0L5paramS835->$3;
      float _M0L6_2atmpS2017;
      float _M0L6_2atmpS2016;
      float _M0L6_2atmpS2014;
      int32_t _M0L6_2atmpS2018;
      #line 125 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS2017 = _M0FP26RiantR8snn__mbt9next__f32(_M0L3rngS838);
      _M0L6_2atmpS2016 = _M0L6_2atmpS2017 * _M0L6spreadS834;
      _M0L6_2atmpS2014 = _M0L2vrS2015 + _M0L6_2atmpS2016;
      #line 125 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS832, _M0L1kS837, _M0L6_2atmpS2014);
      _M0L6_2atmpS2018 = _M0L1kS837 + 1;
      _M0L1kS837 = _M0L6_2atmpS2018;
      continue;
    }
    break;
  }
  #line 127 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
  _M0L1wS840 = _M0MPC15array5Array4makeGfE(_M0L1nS833, 0x0p+0f);
  #line 128 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
  _M0L4fireS841 = _M0MPC15array5Array4makeGbE(_M0L1nS833, 0);
  _M0L2vtS2019 = _M0L5paramS835->$2;
  #line 129 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
  _M0L9thresholdS842 = _M0MPC15array5Array4makeGfE(_M0L1nS833, _M0L2vtS2019);
  #line 130 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
  _M0L4tabsS843 = _M0MPC15array5Array4makeGiE(_M0L1nS833, 1);
  #line 131 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
  _M0L1iS844 = _M0MPC15array5Array4makeGfE(_M0L1nS833, 0x0p+0f);
  #line 132 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
  _M0L9syn__currS845 = _M0MPC15array5Array4makeGfE(_M0L1nS833, 0x0p+0f);
  #line 133 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
  _M0L2geS846 = _M0MPC15array5Array4makeGfE(_M0L1nS833, 0x0p+0f);
  #line 134 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
  _M0L2giS847 = _M0MPC15array5Array4makeGfE(_M0L1nS833, 0x0p+0f);
  #line 135 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
  _M0L3gluS848 = _M0MPC15array5Array4makeGfE(_M0L1nS833, 0x0p+0f);
  #line 136 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
  _M0L4gabaS849 = _M0MPC15array5Array4makeGfE(_M0L1nS833, 0x0p+0f);
  #line 137 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
  _M0L7gsyn__eS850 = _M0MPC15array5Array4makeGfE(_M0L1nS833, 0x1p+0f);
  #line 138 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
  _M0L7gsyn__iS851 = _M0MPC15array5Array4makeGfE(_M0L1nS833, 0x1p+0f);
  moonbit_incref(_M0L5paramS835);
  moonbit_incref(_M0L5spikeS852);
  _block_2063
  = (struct _M0TP26RiantR8snn__mbt10AdExSinExp*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt10AdExSinExp));
  Moonbit_object_header(_block_2063)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 9, 0);
  _block_2063->$0 = _M0L5paramS835;
  _block_2063->$1 = _M0L5spikeS852;
  _block_2063->$2 = _M0L1nS833;
  _block_2063->$3 = _M0L1vS832;
  _block_2063->$4 = _M0L1wS840;
  _block_2063->$5 = _M0L4fireS841;
  _block_2063->$6 = _M0L9thresholdS842;
  _block_2063->$7 = _M0L4tabsS843;
  _block_2063->$8 = _M0L1iS844;
  _block_2063->$9 = _M0L9syn__currS845;
  _block_2063->$10 = _M0L2geS846;
  _block_2063->$11 = _M0L2giS847;
  _block_2063->$12 = _M0L3gluS848;
  _block_2063->$13 = _M0L4gabaS849;
  _block_2063->$14 = _M0L7gsyn__eS850;
  _block_2063->$15 = _M0L7gsyn__iS851;
  _block_2063->$16 = 0x0p+0f;
  _block_2063->$17 = -0x1.2cp+6f;
  _block_2063->$18 = 0x1.8p+2f;
  _block_2063->$19 = 0x1p+1f;
  return _block_2063;
}

struct _M0TP26RiantR8snn__mbt19AdExSinExpParameter* _M0MP26RiantR8snn__mbt19AdExSinExpParameter9lkd__adex(
  
) {
  float _M0L1cS828;
  float _M0L2glS829;
  float _M0L2tmS830;
  float _M0L1rS831;
  struct _M0TP26RiantR8snn__mbt19AdExSinExpParameter* _block_2064;
  #line 64 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
  _M0L1cS828 = 0x1.2cp+8f;
  _M0L2glS829 = 0x1.ep+3f;
  _M0L2tmS830 = _M0L1cS828 / _M0L2glS829;
  _M0L1rS831 = 0x1p+0f / _M0L2glS829;
  _block_2064
  = (struct _M0TP26RiantR8snn__mbt19AdExSinExpParameter*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt19AdExSinExpParameter));
  Moonbit_object_header(_block_2064)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_2064->$0 = _M0L1cS828;
  _block_2064->$1 = _M0L2glS829;
  _block_2064->$2 = -0x1.ap+5f;
  _block_2064->$3 = -0x1.ep+5f;
  _block_2064->$4 = -0x1.18p+6f;
  _block_2064->$5 = _M0L2tmS830;
  _block_2064->$6 = _M0L1rS831;
  _block_2064->$7 = 0x1p+1f;
  _block_2064->$8 = 0x1.2p+7f;
  _block_2064->$9 = 0x1p+2f;
  _block_2064->$10 = 0x1.42p+6f;
  return _block_2064;
}

struct _M0TP26RiantR8snn__mbt11IFParameter* _M0MP26RiantR8snn__mbt11IFParameter3new(
  
) {
  float _M0L1cS826;
  float _M0L2glS827;
  struct _M0TP26RiantR8snn__mbt11IFParameter* _block_2065;
  #line 39 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L1cS826 = -0x1p+0f;
  _M0L2glS827 = -0x1p+0f;
  _block_2065
  = (struct _M0TP26RiantR8snn__mbt11IFParameter*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt11IFParameter));
  Moonbit_object_header(_block_2065)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_2065->$0 = _M0L1cS826;
  _block_2065->$1 = _M0L2glS827;
  _block_2065->$2 = 0x1.ep+3f;
  _block_2065->$3 = -0x1.9p+5f;
  _block_2065->$4 = -0x1.ep+5f;
  _block_2065->$5 = -0x1.18p+6f;
  _block_2065->$6 = 0x1.eb851eb851eb8p-5f;
  _block_2065->$7 = 0x1p+1f;
  _block_2065->$8 = 0x0p+0f;
  _block_2065->$9 = 0x0p+0f;
  _block_2065->$10 = 0x0p+0f;
  return _block_2065;
}

struct _M0TP26RiantR8snn__mbt2IF* _M0MP26RiantR8snn__mbt2IF3new(
  int32_t _M0L1nS800,
  struct _M0TP26RiantR8snn__mbt11IFParameter* _M0L5paramS802,
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS805
) {
  struct _M0TPB5ArrayGfE* _M0L1vS799;
  float _M0L2vtS2012;
  float _M0L2vrS2013;
  float _M0L6spreadS801;
  int32_t _M0L7_2abindS803;
  int32_t _M0L1kS804;
  struct _M0TPB5ArrayGfE* _M0L1wS807;
  struct _M0TPB5ArrayGbE* _M0L4fireS808;
  struct _M0TPB5ArrayGiE* _M0L4tabsS809;
  struct _M0TPB5ArrayGfE* _M0L1iS810;
  struct _M0TPB5ArrayGfE* _M0L9syn__currS811;
  struct _M0TPB5ArrayGfE* _M0L2geS812;
  struct _M0TPB5ArrayGfE* _M0L2giS813;
  struct _M0TPB5ArrayGfE* _M0L2heS814;
  struct _M0TPB5ArrayGfE* _M0L2hiS815;
  struct _M0TPB5ArrayGfE* _M0L3gluS816;
  struct _M0TPB5ArrayGfE* _M0L4gabaS817;
  struct _M0TPB5ArrayGfE* _M0L7gsyn__eS818;
  struct _M0TPB5ArrayGfE* _M0L7gsyn__iS819;
  float _M0L4e__eS820;
  float _M0L4e__iS821;
  float _M0L3treS822;
  float _M0L3tdeS823;
  float _M0L3triS824;
  float _M0L3tdiS825;
  struct _M0TP26RiantR8snn__mbt9PostSpike* _M0L6_2atmpS2011;
  struct _M0TP26RiantR8snn__mbt2IF* _block_2067;
  #line 113 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  #line 114 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L1vS799 = _M0MPC15array5Array4makeGfE(_M0L1nS800, 0x0p+0f);
  _M0L2vtS2012 = _M0L5paramS802->$3;
  _M0L2vrS2013 = _M0L5paramS802->$4;
  _M0L6spreadS801 = _M0L2vtS2012 - _M0L2vrS2013;
  _M0L7_2abindS803 = 0;
  _M0L1kS804 = _M0L7_2abindS803;
  while (1) {
    if (_M0L1kS804 < _M0L1nS800) {
      float _M0L2vrS2007 = _M0L5paramS802->$4;
      float _M0L6_2atmpS2009;
      float _M0L6_2atmpS2008;
      float _M0L6_2atmpS2006;
      int32_t _M0L6_2atmpS2010;
      #line 117 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2009 = _M0FP26RiantR8snn__mbt9next__f32(_M0L3rngS805);
      _M0L6_2atmpS2008 = _M0L6_2atmpS2009 * _M0L6spreadS801;
      _M0L6_2atmpS2006 = _M0L2vrS2007 + _M0L6_2atmpS2008;
      #line 117 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS799, _M0L1kS804, _M0L6_2atmpS2006);
      _M0L6_2atmpS2010 = _M0L1kS804 + 1;
      _M0L1kS804 = _M0L6_2atmpS2010;
      continue;
    }
    break;
  }
  #line 119 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L1wS807 = _M0MPC15array5Array4makeGfE(_M0L1nS800, 0x0p+0f);
  #line 120 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L4fireS808 = _M0MPC15array5Array4makeGbE(_M0L1nS800, 0);
  #line 121 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L4tabsS809 = _M0MPC15array5Array4makeGiE(_M0L1nS800, 0);
  #line 122 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L1iS810 = _M0MPC15array5Array4makeGfE(_M0L1nS800, 0x0p+0f);
  #line 123 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L9syn__currS811 = _M0MPC15array5Array4makeGfE(_M0L1nS800, 0x0p+0f);
  #line 124 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L2geS812 = _M0MPC15array5Array4makeGfE(_M0L1nS800, 0x0p+0f);
  #line 125 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L2giS813 = _M0MPC15array5Array4makeGfE(_M0L1nS800, 0x0p+0f);
  #line 126 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L2heS814 = _M0MPC15array5Array4makeGfE(_M0L1nS800, 0x0p+0f);
  #line 127 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L2hiS815 = _M0MPC15array5Array4makeGfE(_M0L1nS800, 0x0p+0f);
  #line 128 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L3gluS816 = _M0MPC15array5Array4makeGfE(_M0L1nS800, 0x0p+0f);
  #line 129 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L4gabaS817 = _M0MPC15array5Array4makeGfE(_M0L1nS800, 0x0p+0f);
  #line 130 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L7gsyn__eS818 = _M0MPC15array5Array4makeGfE(_M0L1nS800, 0x1p+0f);
  #line 131 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L7gsyn__iS819 = _M0MPC15array5Array4makeGfE(_M0L1nS800, 0x1p+0f);
  _M0L4e__eS820 = 0x0p+0f;
  _M0L4e__iS821 = -0x1.2cp+6f;
  _M0L3treS822 = 0x1p+0f;
  _M0L3tdeS823 = 0x1.8p+2f;
  _M0L3triS824 = 0x1p-1f;
  _M0L3tdiS825 = 0x1p+1f;
  #line 138 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L6_2atmpS2011 = _M0MP26RiantR8snn__mbt9PostSpike3new();
  moonbit_incref(_M0L5paramS802);
  _block_2067
  = (struct _M0TP26RiantR8snn__mbt2IF*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt2IF));
  Moonbit_object_header(_block_2067)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 26, 0);
  _block_2067->$0 = _M0L5paramS802;
  _block_2067->$1 = _M0L6_2atmpS2011;
  _block_2067->$2 = _M0L1nS800;
  _block_2067->$3 = _M0L1vS799;
  _block_2067->$4 = _M0L1wS807;
  _block_2067->$5 = _M0L4fireS808;
  _block_2067->$6 = _M0L4tabsS809;
  _block_2067->$7 = _M0L1iS810;
  _block_2067->$8 = _M0L9syn__currS811;
  _block_2067->$9 = _M0L2geS812;
  _block_2067->$10 = _M0L2giS813;
  _block_2067->$11 = _M0L2heS814;
  _block_2067->$12 = _M0L2hiS815;
  _block_2067->$13 = _M0L3gluS816;
  _block_2067->$14 = _M0L4gabaS817;
  _block_2067->$15 = _M0L7gsyn__eS818;
  _block_2067->$16 = _M0L7gsyn__iS819;
  _block_2067->$17 = _M0L4e__eS820;
  _block_2067->$18 = _M0L4e__iS821;
  _block_2067->$19 = _M0L3treS822;
  _block_2067->$20 = _M0L3tdeS823;
  _block_2067->$21 = _M0L3triS824;
  _block_2067->$22 = _M0L3tdiS825;
  return _block_2067;
}

struct _M0TP26RiantR8snn__mbt9PostSpike* _M0MP26RiantR8snn__mbt9PostSpike3new(
  
) {
  struct _M0TP26RiantR8snn__mbt9PostSpike* _block_2068;
  #line 75 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _block_2068
  = (struct _M0TP26RiantR8snn__mbt9PostSpike*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt9PostSpike));
  Moonbit_object_header(_block_2068)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_2068->$0 = 0x1p+1f;
  return _block_2068;
}

struct _M0TP26RiantR8snn__mbt13AdExPostSpike* _M0MP26RiantR8snn__mbt13AdExPostSpike3new(
  
) {
  struct _M0TP26RiantR8snn__mbt13AdExPostSpike* _block_2069;
  #line 94 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
  _block_2069
  = (struct _M0TP26RiantR8snn__mbt13AdExPostSpike*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt13AdExPostSpike));
  Moonbit_object_header(_block_2069)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_2069->$0 = 0x0p+0f;
  _block_2069->$1 = 0x1.4p+3f;
  _block_2069->$2 = 0x1.4p+3f;
  _block_2069->$3 = 0x1p+0f;
  _block_2069->$4 = 0x1p+0f;
  return _block_2069;
}

int32_t _M0FP26RiantR8snn__mbt18step__adex__sinexp(
  struct _M0TP26RiantR8snn__mbt10AdExSinExp* _M0L1pS778,
  float _M0L2dtS793
) {
  int32_t _M0L1nS777;
  struct _M0TP26RiantR8snn__mbt19AdExSinExpParameter* _M0L3p__S779;
  float _M0L2tmS780;
  float _M0L2vtS781;
  float _M0L2vrS782;
  float _M0L2elS783;
  float _M0L1rS784;
  float _M0L9dt__slopeS785;
  float _M0L2twS786;
  float _M0L1aS787;
  float _M0L1bS788;
  struct _M0TP26RiantR8snn__mbt13AdExPostSpike* _M0L5spikeS2005;
  float _M0L2atS789;
  struct _M0TP26RiantR8snn__mbt13AdExPostSpike* _M0L5spikeS2004;
  float _M0L6tau__aS790;
  struct _M0TP26RiantR8snn__mbt13AdExPostSpike* _M0L5spikeS2003;
  float _M0L11tabs__constS791;
  float _M0L6_2atmpS2002;
  int32_t _M0L11tabs__stepsS792;
  int32_t _M0L7_2abindS794;
  int32_t _M0L1iS795;
  #line 190 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
  _M0L1nS777 = _M0L1pS778->$2;
  _M0L3p__S779 = _M0L1pS778->$0;
  _M0L2tmS780 = _M0L3p__S779->$5;
  _M0L2vtS781 = _M0L3p__S779->$2;
  _M0L2vrS782 = _M0L3p__S779->$3;
  _M0L2elS783 = _M0L3p__S779->$4;
  _M0L1rS784 = _M0L3p__S779->$6;
  _M0L9dt__slopeS785 = _M0L3p__S779->$7;
  _M0L2twS786 = _M0L3p__S779->$8;
  _M0L1aS787 = _M0L3p__S779->$9;
  _M0L1bS788 = _M0L3p__S779->$10;
  _M0L5spikeS2005 = _M0L1pS778->$1;
  _M0L2atS789 = _M0L5spikeS2005->$0;
  _M0L5spikeS2004 = _M0L1pS778->$1;
  _M0L6tau__aS790 = _M0L5spikeS2004->$1;
  _M0L5spikeS2003 = _M0L1pS778->$1;
  _M0L11tabs__constS791 = _M0L5spikeS2003->$3;
  _M0L6_2atmpS2002 = _M0L11tabs__constS791 / _M0L2dtS793;
  #line 205 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
  _M0L11tabs__stepsS792 = _M0MPC15float5Float7to__int(_M0L6_2atmpS2002);
  _M0L7_2abindS794 = 0;
  _M0L1iS795 = _M0L7_2abindS794;
  while (1) {
    if (_M0L1iS795 < _M0L1nS777) {
      struct _M0TPB5ArrayGfE* _M0L1vS1915 = _M0L1pS778->$3;
      struct _M0TPB5ArrayGbE* _M0L4fireS1917 = _M0L1pS778->$5;
      float _M0L6_2atmpS1916;
      struct _M0TPB5ArrayGbE* _M0L4fireS1919;
      struct _M0TPB5ArrayGiE* _M0L4tabsS1920;
      struct _M0TPB5ArrayGiE* _M0L4tabsS1923;
      int32_t _M0L6_2atmpS1922;
      int32_t _M0L6_2atmpS1921;
      struct _M0TPB5ArrayGiE* _M0L4tabsS1925;
      int32_t _M0L6_2atmpS1924;
      struct _M0TPB5ArrayGfE* _M0L1wS1926;
      struct _M0TPB5ArrayGfE* _M0L1wS1938;
      float _M0L6_2atmpS1928;
      struct _M0TPB5ArrayGfE* _M0L1vS1937;
      float _M0L6_2atmpS1936;
      float _M0L6_2atmpS1935;
      float _M0L6_2atmpS1932;
      struct _M0TPB5ArrayGfE* _M0L1wS1934;
      float _M0L6_2atmpS1933;
      float _M0L6_2atmpS1931;
      float _M0L6_2atmpS1930;
      float _M0L6_2atmpS1929;
      float _M0L6_2atmpS1927;
      float _M0L9exp__termS798;
      struct _M0TPB5ArrayGfE* _M0L1vS1939;
      struct _M0TPB5ArrayGfE* _M0L1vS1961;
      float _M0L6_2atmpS1941;
      struct _M0TPB5ArrayGfE* _M0L1vS1960;
      float _M0L6_2atmpS1959;
      float _M0L6_2atmpS1958;
      float _M0L6_2atmpS1957;
      float _M0L6_2atmpS1953;
      struct _M0TPB5ArrayGfE* _M0L9syn__currS1956;
      float _M0L6_2atmpS1955;
      float _M0L6_2atmpS1954;
      float _M0L6_2atmpS1949;
      struct _M0TPB5ArrayGfE* _M0L1wS1952;
      float _M0L6_2atmpS1951;
      float _M0L6_2atmpS1950;
      float _M0L6_2atmpS1945;
      struct _M0TPB5ArrayGfE* _M0L1iS1948;
      float _M0L6_2atmpS1947;
      float _M0L6_2atmpS1946;
      float _M0L6_2atmpS1944;
      float _M0L6_2atmpS1943;
      float _M0L6_2atmpS1942;
      float _M0L6_2atmpS1940;
      struct _M0TPB5ArrayGfE* _M0L9thresholdS1962;
      struct _M0TPB5ArrayGfE* _M0L9thresholdS1970;
      float _M0L6_2atmpS1964;
      struct _M0TPB5ArrayGfE* _M0L9thresholdS1969;
      float _M0L6_2atmpS1968;
      float _M0L6_2atmpS1967;
      float _M0L6_2atmpS1966;
      float _M0L6_2atmpS1965;
      float _M0L6_2atmpS1963;
      struct _M0TPB5ArrayGbE* _M0L4fireS1971;
      struct _M0TPB5ArrayGfE* _M0L1vS1974;
      float _M0L6_2atmpS1973;
      int32_t _M0L6_2atmpS1972;
      struct _M0TPB5ArrayGfE* _M0L1vS1975;
      struct _M0TPB5ArrayGbE* _M0L4fireS1977;
      float _M0L6_2atmpS1976;
      struct _M0TPB5ArrayGfE* _M0L1wS1979;
      struct _M0TPB5ArrayGbE* _M0L4fireS1981;
      float _M0L6_2atmpS1980;
      struct _M0TPB5ArrayGfE* _M0L9thresholdS1985;
      struct _M0TPB5ArrayGbE* _M0L4fireS1987;
      float _M0L6_2atmpS1986;
      struct _M0TPB5ArrayGiE* _M0L4tabsS1991;
      struct _M0TPB5ArrayGbE* _M0L4fireS1993;
      int32_t _M0L6_2atmpS1992;
      int32_t _M0L6_2atmpS1914;
      #line 209 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS1917, _M0L1iS795)) {
        _M0L6_2atmpS1916 = _M0L2vrS782;
      } else {
        struct _M0TPB5ArrayGfE* _M0L1vS1918 = _M0L1pS778->$3;
        #line 209 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
        _M0L6_2atmpS1916 = _M0MPC15array5Array2atGfE(_M0L1vS1918, _M0L1iS795);
      }
      #line 209 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS1915, _M0L1iS795, _M0L6_2atmpS1916);
      _M0L4fireS1919 = _M0L1pS778->$5;
      #line 212 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0MPC15array5Array3setGbE(_M0L4fireS1919, _M0L1iS795, 0);
      _M0L4tabsS1920 = _M0L1pS778->$7;
      _M0L4tabsS1923 = _M0L1pS778->$7;
      #line 213 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS1922
      = _M0MPC15array5Array2atGiE(_M0L4tabsS1923, _M0L1iS795);
      _M0L6_2atmpS1921 = _M0L6_2atmpS1922 - 1;
      #line 213 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0MPC15array5Array3setGiE(_M0L4tabsS1920, _M0L1iS795, _M0L6_2atmpS1921);
      _M0L4tabsS1925 = _M0L1pS778->$7;
      #line 214 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS1924
      = _M0MPC15array5Array2atGiE(_M0L4tabsS1925, _M0L1iS795);
      if (_M0L6_2atmpS1924 > 0) {
        goto join_796;
      }
      _M0L1wS1926 = _M0L1pS778->$4;
      _M0L1wS1938 = _M0L1pS778->$4;
      #line 219 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS1928 = _M0MPC15array5Array2atGfE(_M0L1wS1938, _M0L1iS795);
      _M0L1vS1937 = _M0L1pS778->$3;
      #line 219 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS1936 = _M0MPC15array5Array2atGfE(_M0L1vS1937, _M0L1iS795);
      _M0L6_2atmpS1935 = _M0L6_2atmpS1936 - _M0L2elS783;
      _M0L6_2atmpS1932 = _M0L1aS787 * _M0L6_2atmpS1935;
      _M0L1wS1934 = _M0L1pS778->$4;
      #line 219 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS1933 = _M0MPC15array5Array2atGfE(_M0L1wS1934, _M0L1iS795);
      _M0L6_2atmpS1931 = _M0L6_2atmpS1932 - _M0L6_2atmpS1933;
      _M0L6_2atmpS1930 = _M0L2dtS793 * _M0L6_2atmpS1931;
      _M0L6_2atmpS1929 = _M0L6_2atmpS1930 / _M0L2twS786;
      _M0L6_2atmpS1927 = _M0L6_2atmpS1928 + _M0L6_2atmpS1929;
      #line 219 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0MPC15array5Array3setGfE(_M0L1wS1926, _M0L1iS795, _M0L6_2atmpS1927);
      if (_M0L9dt__slopeS785 < 0x0p+0f) {
        _M0L9exp__termS798 = 0x0p+0f;
      } else {
        struct _M0TPB5ArrayGfE* _M0L1vS2001 = _M0L1pS778->$3;
        float _M0L6_2atmpS1998;
        struct _M0TPB5ArrayGfE* _M0L9thresholdS2000;
        float _M0L6_2atmpS1999;
        float _M0L6_2atmpS1997;
        float _M0L6_2atmpS1996;
        float _M0L6_2atmpS1995;
        #line 225 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
        _M0L6_2atmpS1998 = _M0MPC15array5Array2atGfE(_M0L1vS2001, _M0L1iS795);
        _M0L9thresholdS2000 = _M0L1pS778->$6;
        #line 225 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
        _M0L6_2atmpS1999
        = _M0MPC15array5Array2atGfE(_M0L9thresholdS2000, _M0L1iS795);
        _M0L6_2atmpS1997 = _M0L6_2atmpS1998 - _M0L6_2atmpS1999;
        _M0L6_2atmpS1996 = _M0L6_2atmpS1997 / _M0L9dt__slopeS785;
        #line 225 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
        _M0L6_2atmpS1995 = _M0FP26RiantR8snn__mbt4expf(_M0L6_2atmpS1996);
        _M0L9exp__termS798 = _M0L9dt__slopeS785 * _M0L6_2atmpS1995;
      }
      _M0L1vS1939 = _M0L1pS778->$3;
      _M0L1vS1961 = _M0L1pS778->$3;
      #line 227 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS1941 = _M0MPC15array5Array2atGfE(_M0L1vS1961, _M0L1iS795);
      _M0L1vS1960 = _M0L1pS778->$3;
      #line 230 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS1959 = _M0MPC15array5Array2atGfE(_M0L1vS1960, _M0L1iS795);
      _M0L6_2atmpS1958 = _M0L6_2atmpS1959 - _M0L2elS783;
      _M0L6_2atmpS1957 = -_M0L6_2atmpS1958;
      _M0L6_2atmpS1953 = _M0L6_2atmpS1957 + _M0L9exp__termS798;
      _M0L9syn__currS1956 = _M0L1pS778->$9;
      #line 230 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS1955
      = _M0MPC15array5Array2atGfE(_M0L9syn__currS1956, _M0L1iS795);
      _M0L6_2atmpS1954 = _M0L1rS784 * _M0L6_2atmpS1955;
      _M0L6_2atmpS1949 = _M0L6_2atmpS1953 - _M0L6_2atmpS1954;
      _M0L1wS1952 = _M0L1pS778->$4;
      #line 230 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS1951 = _M0MPC15array5Array2atGfE(_M0L1wS1952, _M0L1iS795);
      _M0L6_2atmpS1950 = _M0L1rS784 * _M0L6_2atmpS1951;
      _M0L6_2atmpS1945 = _M0L6_2atmpS1949 - _M0L6_2atmpS1950;
      _M0L1iS1948 = _M0L1pS778->$8;
      #line 231 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS1947 = _M0MPC15array5Array2atGfE(_M0L1iS1948, _M0L1iS795);
      _M0L6_2atmpS1946 = _M0L1rS784 * _M0L6_2atmpS1947;
      _M0L6_2atmpS1944 = _M0L6_2atmpS1945 + _M0L6_2atmpS1946;
      _M0L6_2atmpS1943 = _M0L2dtS793 * _M0L6_2atmpS1944;
      _M0L6_2atmpS1942 = _M0L6_2atmpS1943 / _M0L2tmS780;
      _M0L6_2atmpS1940 = _M0L6_2atmpS1941 + _M0L6_2atmpS1942;
      #line 227 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS1939, _M0L1iS795, _M0L6_2atmpS1940);
      _M0L9thresholdS1962 = _M0L1pS778->$6;
      _M0L9thresholdS1970 = _M0L1pS778->$6;
      #line 235 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS1964
      = _M0MPC15array5Array2atGfE(_M0L9thresholdS1970, _M0L1iS795);
      _M0L9thresholdS1969 = _M0L1pS778->$6;
      #line 235 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS1968
      = _M0MPC15array5Array2atGfE(_M0L9thresholdS1969, _M0L1iS795);
      _M0L6_2atmpS1967 = _M0L2vtS781 - _M0L6_2atmpS1968;
      _M0L6_2atmpS1966 = _M0L2dtS793 * _M0L6_2atmpS1967;
      _M0L6_2atmpS1965 = _M0L6_2atmpS1966 / _M0L6tau__aS790;
      _M0L6_2atmpS1963 = _M0L6_2atmpS1964 + _M0L6_2atmpS1965;
      #line 235 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0MPC15array5Array3setGfE(_M0L9thresholdS1962, _M0L1iS795, _M0L6_2atmpS1963);
      _M0L4fireS1971 = _M0L1pS778->$5;
      _M0L1vS1974 = _M0L1pS778->$3;
      #line 238 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS1973 = _M0MPC15array5Array2atGfE(_M0L1vS1974, _M0L1iS795);
      _M0L6_2atmpS1972 = _M0L6_2atmpS1973 >= 0x0p+0f;
      #line 238 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0MPC15array5Array3setGbE(_M0L4fireS1971, _M0L1iS795, _M0L6_2atmpS1972);
      _M0L1vS1975 = _M0L1pS778->$3;
      _M0L4fireS1977 = _M0L1pS778->$5;
      #line 239 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS1977, _M0L1iS795)) {
        _M0L6_2atmpS1976 = 0x1.4p+4f;
      } else {
        struct _M0TPB5ArrayGfE* _M0L1vS1978 = _M0L1pS778->$3;
        #line 239 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
        _M0L6_2atmpS1976 = _M0MPC15array5Array2atGfE(_M0L1vS1978, _M0L1iS795);
      }
      #line 239 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS1975, _M0L1iS795, _M0L6_2atmpS1976);
      _M0L1wS1979 = _M0L1pS778->$4;
      _M0L4fireS1981 = _M0L1pS778->$5;
      #line 240 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS1981, _M0L1iS795)) {
        struct _M0TPB5ArrayGfE* _M0L1wS1983 = _M0L1pS778->$4;
        float _M0L6_2atmpS1982;
        #line 240 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
        _M0L6_2atmpS1982 = _M0MPC15array5Array2atGfE(_M0L1wS1983, _M0L1iS795);
        _M0L6_2atmpS1980 = _M0L6_2atmpS1982 + _M0L1bS788;
      } else {
        struct _M0TPB5ArrayGfE* _M0L1wS1984 = _M0L1pS778->$4;
        #line 240 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
        _M0L6_2atmpS1980 = _M0MPC15array5Array2atGfE(_M0L1wS1984, _M0L1iS795);
      }
      #line 240 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0MPC15array5Array3setGfE(_M0L1wS1979, _M0L1iS795, _M0L6_2atmpS1980);
      _M0L9thresholdS1985 = _M0L1pS778->$6;
      _M0L4fireS1987 = _M0L1pS778->$5;
      #line 241 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS1987, _M0L1iS795)) {
        struct _M0TPB5ArrayGfE* _M0L9thresholdS1989 = _M0L1pS778->$6;
        float _M0L6_2atmpS1988;
        #line 241 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
        _M0L6_2atmpS1988
        = _M0MPC15array5Array2atGfE(_M0L9thresholdS1989, _M0L1iS795);
        _M0L6_2atmpS1986 = _M0L6_2atmpS1988 + _M0L2atS789;
      } else {
        struct _M0TPB5ArrayGfE* _M0L9thresholdS1990 = _M0L1pS778->$6;
        #line 241 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
        _M0L6_2atmpS1986
        = _M0MPC15array5Array2atGfE(_M0L9thresholdS1990, _M0L1iS795);
      }
      #line 241 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0MPC15array5Array3setGfE(_M0L9thresholdS1985, _M0L1iS795, _M0L6_2atmpS1986);
      _M0L4tabsS1991 = _M0L1pS778->$7;
      _M0L4fireS1993 = _M0L1pS778->$5;
      #line 242 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS1993, _M0L1iS795)) {
        _M0L6_2atmpS1992 = _M0L11tabs__stepsS792;
      } else {
        struct _M0TPB5ArrayGiE* _M0L4tabsS1994 = _M0L1pS778->$7;
        #line 242 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
        _M0L6_2atmpS1992
        = _M0MPC15array5Array2atGiE(_M0L4tabsS1994, _M0L1iS795);
      }
      #line 242 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0MPC15array5Array3setGiE(_M0L4tabsS1991, _M0L1iS795, _M0L6_2atmpS1992);
      goto join_796;
      goto joinlet_2071;
      join_796:;
      _M0L6_2atmpS1914 = _M0L1iS795 + 1;
      _M0L1iS795 = _M0L6_2atmpS1914;
      continue;
      joinlet_2071:;
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt31adex__sinexp__synaptic__current(
  struct _M0TP26RiantR8snn__mbt10AdExSinExp* _M0L1pS773
) {
  int32_t _M0L1nS772;
  int32_t _M0L7_2abindS774;
  int32_t _M0L1iS775;
  #line 177 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
  _M0L1nS772 = _M0L1pS773->$2;
  _M0L7_2abindS774 = 0;
  _M0L1iS775 = _M0L7_2abindS774;
  while (1) {
    if (_M0L1iS775 < _M0L1nS772) {
      struct _M0TPB5ArrayGfE* _M0L9syn__currS1891 = _M0L1pS773->$9;
      struct _M0TPB5ArrayGfE* _M0L2geS1912 = _M0L1pS773->$10;
      float _M0L6_2atmpS1907;
      struct _M0TPB5ArrayGfE* _M0L1vS1911;
      float _M0L6_2atmpS1909;
      float _M0L4e__eS1910;
      float _M0L6_2atmpS1908;
      float _M0L6_2atmpS1904;
      struct _M0TPB5ArrayGfE* _M0L7gsyn__eS1906;
      float _M0L6_2atmpS1905;
      float _M0L6_2atmpS1893;
      struct _M0TPB5ArrayGfE* _M0L2giS1903;
      float _M0L6_2atmpS1898;
      struct _M0TPB5ArrayGfE* _M0L1vS1902;
      float _M0L6_2atmpS1900;
      float _M0L4e__iS1901;
      float _M0L6_2atmpS1899;
      float _M0L6_2atmpS1895;
      struct _M0TPB5ArrayGfE* _M0L7gsyn__iS1897;
      float _M0L6_2atmpS1896;
      float _M0L6_2atmpS1894;
      float _M0L6_2atmpS1892;
      int32_t _M0L6_2atmpS1913;
      #line 180 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS1907 = _M0MPC15array5Array2atGfE(_M0L2geS1912, _M0L1iS775);
      _M0L1vS1911 = _M0L1pS773->$3;
      #line 180 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS1909 = _M0MPC15array5Array2atGfE(_M0L1vS1911, _M0L1iS775);
      _M0L4e__eS1910 = _M0L1pS773->$16;
      _M0L6_2atmpS1908 = _M0L6_2atmpS1909 - _M0L4e__eS1910;
      _M0L6_2atmpS1904 = _M0L6_2atmpS1907 * _M0L6_2atmpS1908;
      _M0L7gsyn__eS1906 = _M0L1pS773->$14;
      #line 180 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS1905
      = _M0MPC15array5Array2atGfE(_M0L7gsyn__eS1906, _M0L1iS775);
      _M0L6_2atmpS1893 = _M0L6_2atmpS1904 * _M0L6_2atmpS1905;
      _M0L2giS1903 = _M0L1pS773->$11;
      #line 181 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS1898 = _M0MPC15array5Array2atGfE(_M0L2giS1903, _M0L1iS775);
      _M0L1vS1902 = _M0L1pS773->$3;
      #line 181 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS1900 = _M0MPC15array5Array2atGfE(_M0L1vS1902, _M0L1iS775);
      _M0L4e__iS1901 = _M0L1pS773->$17;
      _M0L6_2atmpS1899 = _M0L6_2atmpS1900 - _M0L4e__iS1901;
      _M0L6_2atmpS1895 = _M0L6_2atmpS1898 * _M0L6_2atmpS1899;
      _M0L7gsyn__iS1897 = _M0L1pS773->$15;
      #line 181 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS1896
      = _M0MPC15array5Array2atGfE(_M0L7gsyn__iS1897, _M0L1iS775);
      _M0L6_2atmpS1894 = _M0L6_2atmpS1895 * _M0L6_2atmpS1896;
      _M0L6_2atmpS1892 = _M0L6_2atmpS1893 + _M0L6_2atmpS1894;
      #line 180 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0MPC15array5Array3setGfE(_M0L9syn__currS1891, _M0L1iS775, _M0L6_2atmpS1892);
      _M0L6_2atmpS1913 = _M0L1iS775 + 1;
      _M0L1iS775 = _M0L6_2atmpS1913;
      continue;
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt28adex__sinexp__step__synapses(
  struct _M0TP26RiantR8snn__mbt10AdExSinExp* _M0L1pS762,
  float _M0L2dtS767
) {
  int32_t _M0L1nS761;
  float _M0L6tau__eS763;
  float _M0L6tau__iS764;
  int32_t _M0L7_2abindS765;
  int32_t _M0L1iS766;
  int32_t _M0L7_2abindS769;
  int32_t _M0L1iS770;
  #line 154 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
  _M0L1nS761 = _M0L1pS762->$2;
  _M0L6tau__eS763 = _M0L1pS762->$18;
  _M0L6tau__iS764 = _M0L1pS762->$19;
  _M0L7_2abindS765 = 0;
  _M0L1iS766 = _M0L7_2abindS765;
  while (1) {
    if (_M0L1iS766 < _M0L1nS761) {
      struct _M0TPB5ArrayGfE* _M0L2geS1857 = _M0L1pS762->$10;
      struct _M0TPB5ArrayGfE* _M0L2geS1862 = _M0L1pS762->$10;
      float _M0L6_2atmpS1859;
      struct _M0TPB5ArrayGfE* _M0L3gluS1861;
      float _M0L6_2atmpS1860;
      float _M0L6_2atmpS1858;
      struct _M0TPB5ArrayGfE* _M0L2giS1863;
      struct _M0TPB5ArrayGfE* _M0L2giS1868;
      float _M0L6_2atmpS1865;
      struct _M0TPB5ArrayGfE* _M0L4gabaS1867;
      float _M0L6_2atmpS1866;
      float _M0L6_2atmpS1864;
      struct _M0TPB5ArrayGfE* _M0L2geS1869;
      struct _M0TPB5ArrayGfE* _M0L2geS1877;
      float _M0L6_2atmpS1871;
      struct _M0TPB5ArrayGfE* _M0L2geS1876;
      float _M0L6_2atmpS1875;
      float _M0L6_2atmpS1874;
      float _M0L6_2atmpS1873;
      float _M0L6_2atmpS1872;
      float _M0L6_2atmpS1870;
      struct _M0TPB5ArrayGfE* _M0L2giS1878;
      struct _M0TPB5ArrayGfE* _M0L2giS1886;
      float _M0L6_2atmpS1880;
      struct _M0TPB5ArrayGfE* _M0L2giS1885;
      float _M0L6_2atmpS1884;
      float _M0L6_2atmpS1883;
      float _M0L6_2atmpS1882;
      float _M0L6_2atmpS1881;
      float _M0L6_2atmpS1879;
      int32_t _M0L6_2atmpS1887;
      #line 160 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS1859 = _M0MPC15array5Array2atGfE(_M0L2geS1862, _M0L1iS766);
      _M0L3gluS1861 = _M0L1pS762->$12;
      #line 160 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS1860 = _M0MPC15array5Array2atGfE(_M0L3gluS1861, _M0L1iS766);
      _M0L6_2atmpS1858 = _M0L6_2atmpS1859 + _M0L6_2atmpS1860;
      #line 160 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0MPC15array5Array3setGfE(_M0L2geS1857, _M0L1iS766, _M0L6_2atmpS1858);
      _M0L2giS1863 = _M0L1pS762->$11;
      _M0L2giS1868 = _M0L1pS762->$11;
      #line 161 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS1865 = _M0MPC15array5Array2atGfE(_M0L2giS1868, _M0L1iS766);
      _M0L4gabaS1867 = _M0L1pS762->$13;
      #line 161 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS1866
      = _M0MPC15array5Array2atGfE(_M0L4gabaS1867, _M0L1iS766);
      _M0L6_2atmpS1864 = _M0L6_2atmpS1865 + _M0L6_2atmpS1866;
      #line 161 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0MPC15array5Array3setGfE(_M0L2giS1863, _M0L1iS766, _M0L6_2atmpS1864);
      _M0L2geS1869 = _M0L1pS762->$10;
      _M0L2geS1877 = _M0L1pS762->$10;
      #line 162 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS1871 = _M0MPC15array5Array2atGfE(_M0L2geS1877, _M0L1iS766);
      _M0L2geS1876 = _M0L1pS762->$10;
      #line 162 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS1875 = _M0MPC15array5Array2atGfE(_M0L2geS1876, _M0L1iS766);
      _M0L6_2atmpS1874 = -_M0L6_2atmpS1875;
      _M0L6_2atmpS1873 = _M0L6_2atmpS1874 / _M0L6tau__eS763;
      _M0L6_2atmpS1872 = _M0L2dtS767 * _M0L6_2atmpS1873;
      _M0L6_2atmpS1870 = _M0L6_2atmpS1871 + _M0L6_2atmpS1872;
      #line 162 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0MPC15array5Array3setGfE(_M0L2geS1869, _M0L1iS766, _M0L6_2atmpS1870);
      _M0L2giS1878 = _M0L1pS762->$11;
      _M0L2giS1886 = _M0L1pS762->$11;
      #line 163 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS1880 = _M0MPC15array5Array2atGfE(_M0L2giS1886, _M0L1iS766);
      _M0L2giS1885 = _M0L1pS762->$11;
      #line 163 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS1884 = _M0MPC15array5Array2atGfE(_M0L2giS1885, _M0L1iS766);
      _M0L6_2atmpS1883 = -_M0L6_2atmpS1884;
      _M0L6_2atmpS1882 = _M0L6_2atmpS1883 / _M0L6tau__iS764;
      _M0L6_2atmpS1881 = _M0L2dtS767 * _M0L6_2atmpS1882;
      _M0L6_2atmpS1879 = _M0L6_2atmpS1880 + _M0L6_2atmpS1881;
      #line 163 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0MPC15array5Array3setGfE(_M0L2giS1878, _M0L1iS766, _M0L6_2atmpS1879);
      _M0L6_2atmpS1887 = _M0L1iS766 + 1;
      _M0L1iS766 = _M0L6_2atmpS1887;
      continue;
    }
    break;
  }
  _M0L7_2abindS769 = 0;
  _M0L1iS770 = _M0L7_2abindS769;
  while (1) {
    if (_M0L1iS770 < _M0L1nS761) {
      struct _M0TPB5ArrayGfE* _M0L3gluS1888 = _M0L1pS762->$12;
      struct _M0TPB5ArrayGfE* _M0L4gabaS1889;
      int32_t _M0L6_2atmpS1890;
      #line 166 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0MPC15array5Array3setGfE(_M0L3gluS1888, _M0L1iS770, 0x0p+0f);
      _M0L4gabaS1889 = _M0L1pS762->$13;
      #line 167 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0MPC15array5Array3setGfE(_M0L4gabaS1889, _M0L1iS770, 0x0p+0f);
      _M0L6_2atmpS1890 = _M0L1iS770 + 1;
      _M0L1iS770 = _M0L6_2atmpS1890;
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

float _M0FP26RiantR8snn__mbt21monitor__firing__rate(
  struct _M0TPB5ArrayGfE* _M0L4dataS759
) {
  int32_t _M0L1nS758;
  int32_t _M0L5countS760;
  float _M0L6_2atmpS1856;
  float _M0L6_2atmpS1854;
  float _M0L6_2atmpS1855;
  #line 110 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sinexp_helpers.mbt"
  #line 111 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sinexp_helpers.mbt"
  _M0L1nS758 = _M0MPC15array5Array6lengthGfE(_M0L4dataS759);
  if (_M0L1nS758 == 0) {
    return 0x0p+0f;
  }
  #line 115 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sinexp_helpers.mbt"
  _M0L5countS760
  = _M0FP26RiantR8snn__mbt22monitor__count__spikes(_M0L4dataS759);
  _M0L6_2atmpS1856 = (float)_M0L5countS760;
  _M0L6_2atmpS1854 = _M0L6_2atmpS1856 * 0x1.f4p+12f;
  _M0L6_2atmpS1855 = (float)_M0L1nS758;
  return _M0L6_2atmpS1854 / _M0L6_2atmpS1855;
}

int32_t _M0FP26RiantR8snn__mbt22monitor__count__spikes(
  struct _M0TPB5ArrayGfE* _M0L4dataS755
) {
  struct _M0TPB8MutLocalGiE* _M0L1nS752;
  int32_t _M0L7_2abindS753;
  int32_t _M0L7_2abindS754;
  int32_t _M0L1kS756;
  int32_t _result_2076;
  #line 94 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sinexp_helpers.mbt"
  _M0L1nS752
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1nS752)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1nS752->$0 = 0;
  _M0L7_2abindS753 = 0;
  #line 96 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sinexp_helpers.mbt"
  _M0L7_2abindS754 = _M0MPC15array5Array6lengthGfE(_M0L4dataS755);
  _M0L1kS756 = _M0L7_2abindS753;
  while (1) {
    if (_M0L1kS756 < _M0L7_2abindS754) {
      float _M0L6_2atmpS1850;
      int32_t _M0L6_2atmpS1853;
      #line 97 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sinexp_helpers.mbt"
      _M0L6_2atmpS1850 = _M0MPC15array5Array2atGfE(_M0L4dataS755, _M0L1kS756);
      if (_M0L6_2atmpS1850 > 0x0p+0f) {
        int32_t _M0L3valS1852 = _M0L1nS752->$0;
        int32_t _M0L6_2atmpS1851 = _M0L3valS1852 + 1;
        _M0L1nS752->$0 = _M0L6_2atmpS1851;
      }
      _M0L6_2atmpS1853 = _M0L1kS756 + 1;
      _M0L1kS756 = _M0L6_2atmpS1853;
      continue;
    }
    break;
  }
  _result_2076 = _M0L1nS752->$0;
  moonbit_decref(_M0L1nS752);
  return _result_2076;
}

int32_t _M0FP26RiantR8snn__mbt10count__nnz(
  struct _M0TPB5ArrayGfE* _M0L3arrS749
) {
  struct _M0TPB8MutLocalGiE* _M0L1nS746;
  int32_t _M0L7_2abindS747;
  int32_t _M0L7_2abindS748;
  int32_t _M0L1kS750;
  int32_t _result_2078;
  #line 80 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sinexp_helpers.mbt"
  _M0L1nS746
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1nS746)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1nS746->$0 = 0;
  _M0L7_2abindS747 = 0;
  #line 82 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sinexp_helpers.mbt"
  _M0L7_2abindS748 = _M0MPC15array5Array6lengthGfE(_M0L3arrS749);
  _M0L1kS750 = _M0L7_2abindS747;
  while (1) {
    if (_M0L1kS750 < _M0L7_2abindS748) {
      float _M0L6_2atmpS1846;
      int32_t _M0L6_2atmpS1849;
      #line 83 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sinexp_helpers.mbt"
      _M0L6_2atmpS1846 = _M0MPC15array5Array2atGfE(_M0L3arrS749, _M0L1kS750);
      if (_M0L6_2atmpS1846 != 0x0p+0f) {
        int32_t _M0L3valS1848 = _M0L1nS746->$0;
        int32_t _M0L6_2atmpS1847 = _M0L3valS1848 + 1;
        _M0L1nS746->$0 = _M0L6_2atmpS1847;
      }
      _M0L6_2atmpS1849 = _M0L1kS750 + 1;
      _M0L1kS750 = _M0L6_2atmpS1849;
      continue;
    }
    break;
  }
  _result_2078 = _M0L1nS746->$0;
  moonbit_decref(_M0L1nS746);
  return _result_2078;
}

int32_t _M0FP26RiantR8snn__mbt18simulate__step__if(
  struct _M0TP26RiantR8snn__mbt2IF* _M0L3popS744,
  float _M0L2dtS745
) {
  #line 72 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sinexp_helpers.mbt"
  #line 73 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sinexp_helpers.mbt"
  _M0FP26RiantR8snn__mbt14step__synapses(_M0L3popS744, _M0L2dtS745);
  #line 74 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sinexp_helpers.mbt"
  _M0FP26RiantR8snn__mbt17synaptic__current(_M0L3popS744);
  #line 75 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sinexp_helpers.mbt"
  _M0FP26RiantR8snn__mbt12step__neuron(_M0L3popS744, _M0L2dtS745);
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt17synaptic__current(
  struct _M0TP26RiantR8snn__mbt2IF* _M0L1pS740
) {
  int32_t _M0L1nS739;
  int32_t _M0L7_2abindS741;
  int32_t _M0L1iS742;
  #line 165 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L1nS739 = _M0L1pS740->$2;
  _M0L7_2abindS741 = 0;
  _M0L1iS742 = _M0L7_2abindS741;
  while (1) {
    if (_M0L1iS742 < _M0L1nS739) {
      struct _M0TPB5ArrayGfE* _M0L9syn__currS1823 = _M0L1pS740->$8;
      struct _M0TPB5ArrayGfE* _M0L2geS1844 = _M0L1pS740->$9;
      float _M0L6_2atmpS1839;
      struct _M0TPB5ArrayGfE* _M0L1vS1843;
      float _M0L6_2atmpS1841;
      float _M0L4e__eS1842;
      float _M0L6_2atmpS1840;
      float _M0L6_2atmpS1836;
      struct _M0TPB5ArrayGfE* _M0L7gsyn__eS1838;
      float _M0L6_2atmpS1837;
      float _M0L6_2atmpS1825;
      struct _M0TPB5ArrayGfE* _M0L2giS1835;
      float _M0L6_2atmpS1830;
      struct _M0TPB5ArrayGfE* _M0L1vS1834;
      float _M0L6_2atmpS1832;
      float _M0L4e__iS1833;
      float _M0L6_2atmpS1831;
      float _M0L6_2atmpS1827;
      struct _M0TPB5ArrayGfE* _M0L7gsyn__iS1829;
      float _M0L6_2atmpS1828;
      float _M0L6_2atmpS1826;
      float _M0L6_2atmpS1824;
      int32_t _M0L6_2atmpS1845;
      #line 168 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1839 = _M0MPC15array5Array2atGfE(_M0L2geS1844, _M0L1iS742);
      _M0L1vS1843 = _M0L1pS740->$3;
      #line 168 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1841 = _M0MPC15array5Array2atGfE(_M0L1vS1843, _M0L1iS742);
      _M0L4e__eS1842 = _M0L1pS740->$17;
      _M0L6_2atmpS1840 = _M0L6_2atmpS1841 - _M0L4e__eS1842;
      _M0L6_2atmpS1836 = _M0L6_2atmpS1839 * _M0L6_2atmpS1840;
      _M0L7gsyn__eS1838 = _M0L1pS740->$15;
      #line 168 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1837
      = _M0MPC15array5Array2atGfE(_M0L7gsyn__eS1838, _M0L1iS742);
      _M0L6_2atmpS1825 = _M0L6_2atmpS1836 * _M0L6_2atmpS1837;
      _M0L2giS1835 = _M0L1pS740->$10;
      #line 169 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1830 = _M0MPC15array5Array2atGfE(_M0L2giS1835, _M0L1iS742);
      _M0L1vS1834 = _M0L1pS740->$3;
      #line 169 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1832 = _M0MPC15array5Array2atGfE(_M0L1vS1834, _M0L1iS742);
      _M0L4e__iS1833 = _M0L1pS740->$18;
      _M0L6_2atmpS1831 = _M0L6_2atmpS1832 - _M0L4e__iS1833;
      _M0L6_2atmpS1827 = _M0L6_2atmpS1830 * _M0L6_2atmpS1831;
      _M0L7gsyn__iS1829 = _M0L1pS740->$16;
      #line 169 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1828
      = _M0MPC15array5Array2atGfE(_M0L7gsyn__iS1829, _M0L1iS742);
      _M0L6_2atmpS1826 = _M0L6_2atmpS1827 * _M0L6_2atmpS1828;
      _M0L6_2atmpS1824 = _M0L6_2atmpS1825 + _M0L6_2atmpS1826;
      #line 168 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L9syn__currS1823, _M0L1iS742, _M0L6_2atmpS1824);
      _M0L6_2atmpS1845 = _M0L1iS742 + 1;
      _M0L1iS742 = _M0L6_2atmpS1845;
      continue;
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt14step__synapses(
  struct _M0TP26RiantR8snn__mbt2IF* _M0L1pS731,
  float _M0L2dtS734
) {
  int32_t _M0L1nS730;
  int32_t _M0L7_2abindS732;
  int32_t _M0L1iS733;
  int32_t _M0L7_2abindS736;
  int32_t _M0L1iS737;
  #line 146 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L1nS730 = _M0L1pS731->$2;
  _M0L7_2abindS732 = 0;
  _M0L1iS733 = _M0L7_2abindS732;
  while (1) {
    if (_M0L1iS733 < _M0L1nS730) {
      struct _M0TPB5ArrayGfE* _M0L2heS1761 = _M0L1pS731->$11;
      struct _M0TPB5ArrayGfE* _M0L2heS1766 = _M0L1pS731->$11;
      float _M0L6_2atmpS1763;
      struct _M0TPB5ArrayGfE* _M0L3gluS1765;
      float _M0L6_2atmpS1764;
      float _M0L6_2atmpS1762;
      struct _M0TPB5ArrayGfE* _M0L2hiS1767;
      struct _M0TPB5ArrayGfE* _M0L2hiS1772;
      float _M0L6_2atmpS1769;
      struct _M0TPB5ArrayGfE* _M0L4gabaS1771;
      float _M0L6_2atmpS1770;
      float _M0L6_2atmpS1768;
      struct _M0TPB5ArrayGfE* _M0L2geS1773;
      struct _M0TPB5ArrayGfE* _M0L2geS1785;
      float _M0L6_2atmpS1775;
      struct _M0TPB5ArrayGfE* _M0L2geS1784;
      float _M0L6_2atmpS1783;
      float _M0L6_2atmpS1781;
      float _M0L3tdeS1782;
      float _M0L6_2atmpS1778;
      struct _M0TPB5ArrayGfE* _M0L2heS1780;
      float _M0L6_2atmpS1779;
      float _M0L6_2atmpS1777;
      float _M0L6_2atmpS1776;
      float _M0L6_2atmpS1774;
      struct _M0TPB5ArrayGfE* _M0L2heS1786;
      struct _M0TPB5ArrayGfE* _M0L2heS1795;
      float _M0L6_2atmpS1788;
      struct _M0TPB5ArrayGfE* _M0L2heS1794;
      float _M0L6_2atmpS1793;
      float _M0L6_2atmpS1791;
      float _M0L3treS1792;
      float _M0L6_2atmpS1790;
      float _M0L6_2atmpS1789;
      float _M0L6_2atmpS1787;
      struct _M0TPB5ArrayGfE* _M0L2giS1796;
      struct _M0TPB5ArrayGfE* _M0L2giS1808;
      float _M0L6_2atmpS1798;
      struct _M0TPB5ArrayGfE* _M0L2giS1807;
      float _M0L6_2atmpS1806;
      float _M0L6_2atmpS1804;
      float _M0L3tdiS1805;
      float _M0L6_2atmpS1801;
      struct _M0TPB5ArrayGfE* _M0L2hiS1803;
      float _M0L6_2atmpS1802;
      float _M0L6_2atmpS1800;
      float _M0L6_2atmpS1799;
      float _M0L6_2atmpS1797;
      struct _M0TPB5ArrayGfE* _M0L2hiS1809;
      struct _M0TPB5ArrayGfE* _M0L2hiS1818;
      float _M0L6_2atmpS1811;
      struct _M0TPB5ArrayGfE* _M0L2hiS1817;
      float _M0L6_2atmpS1816;
      float _M0L6_2atmpS1814;
      float _M0L3triS1815;
      float _M0L6_2atmpS1813;
      float _M0L6_2atmpS1812;
      float _M0L6_2atmpS1810;
      int32_t _M0L6_2atmpS1819;
      #line 149 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1763 = _M0MPC15array5Array2atGfE(_M0L2heS1766, _M0L1iS733);
      _M0L3gluS1765 = _M0L1pS731->$13;
      #line 149 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1764 = _M0MPC15array5Array2atGfE(_M0L3gluS1765, _M0L1iS733);
      _M0L6_2atmpS1762 = _M0L6_2atmpS1763 + _M0L6_2atmpS1764;
      #line 149 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L2heS1761, _M0L1iS733, _M0L6_2atmpS1762);
      _M0L2hiS1767 = _M0L1pS731->$12;
      _M0L2hiS1772 = _M0L1pS731->$12;
      #line 150 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1769 = _M0MPC15array5Array2atGfE(_M0L2hiS1772, _M0L1iS733);
      _M0L4gabaS1771 = _M0L1pS731->$14;
      #line 150 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1770
      = _M0MPC15array5Array2atGfE(_M0L4gabaS1771, _M0L1iS733);
      _M0L6_2atmpS1768 = _M0L6_2atmpS1769 + _M0L6_2atmpS1770;
      #line 150 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L2hiS1767, _M0L1iS733, _M0L6_2atmpS1768);
      _M0L2geS1773 = _M0L1pS731->$9;
      _M0L2geS1785 = _M0L1pS731->$9;
      #line 151 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1775 = _M0MPC15array5Array2atGfE(_M0L2geS1785, _M0L1iS733);
      _M0L2geS1784 = _M0L1pS731->$9;
      #line 151 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1783 = _M0MPC15array5Array2atGfE(_M0L2geS1784, _M0L1iS733);
      _M0L6_2atmpS1781 = -_M0L6_2atmpS1783;
      _M0L3tdeS1782 = _M0L1pS731->$20;
      _M0L6_2atmpS1778 = _M0L6_2atmpS1781 / _M0L3tdeS1782;
      _M0L2heS1780 = _M0L1pS731->$11;
      #line 151 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1779 = _M0MPC15array5Array2atGfE(_M0L2heS1780, _M0L1iS733);
      _M0L6_2atmpS1777 = _M0L6_2atmpS1778 + _M0L6_2atmpS1779;
      _M0L6_2atmpS1776 = _M0L2dtS734 * _M0L6_2atmpS1777;
      _M0L6_2atmpS1774 = _M0L6_2atmpS1775 + _M0L6_2atmpS1776;
      #line 151 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L2geS1773, _M0L1iS733, _M0L6_2atmpS1774);
      _M0L2heS1786 = _M0L1pS731->$11;
      _M0L2heS1795 = _M0L1pS731->$11;
      #line 152 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1788 = _M0MPC15array5Array2atGfE(_M0L2heS1795, _M0L1iS733);
      _M0L2heS1794 = _M0L1pS731->$11;
      #line 152 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1793 = _M0MPC15array5Array2atGfE(_M0L2heS1794, _M0L1iS733);
      _M0L6_2atmpS1791 = -_M0L6_2atmpS1793;
      _M0L3treS1792 = _M0L1pS731->$19;
      _M0L6_2atmpS1790 = _M0L6_2atmpS1791 / _M0L3treS1792;
      _M0L6_2atmpS1789 = _M0L2dtS734 * _M0L6_2atmpS1790;
      _M0L6_2atmpS1787 = _M0L6_2atmpS1788 + _M0L6_2atmpS1789;
      #line 152 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L2heS1786, _M0L1iS733, _M0L6_2atmpS1787);
      _M0L2giS1796 = _M0L1pS731->$10;
      _M0L2giS1808 = _M0L1pS731->$10;
      #line 153 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1798 = _M0MPC15array5Array2atGfE(_M0L2giS1808, _M0L1iS733);
      _M0L2giS1807 = _M0L1pS731->$10;
      #line 153 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1806 = _M0MPC15array5Array2atGfE(_M0L2giS1807, _M0L1iS733);
      _M0L6_2atmpS1804 = -_M0L6_2atmpS1806;
      _M0L3tdiS1805 = _M0L1pS731->$22;
      _M0L6_2atmpS1801 = _M0L6_2atmpS1804 / _M0L3tdiS1805;
      _M0L2hiS1803 = _M0L1pS731->$12;
      #line 153 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1802 = _M0MPC15array5Array2atGfE(_M0L2hiS1803, _M0L1iS733);
      _M0L6_2atmpS1800 = _M0L6_2atmpS1801 + _M0L6_2atmpS1802;
      _M0L6_2atmpS1799 = _M0L2dtS734 * _M0L6_2atmpS1800;
      _M0L6_2atmpS1797 = _M0L6_2atmpS1798 + _M0L6_2atmpS1799;
      #line 153 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L2giS1796, _M0L1iS733, _M0L6_2atmpS1797);
      _M0L2hiS1809 = _M0L1pS731->$12;
      _M0L2hiS1818 = _M0L1pS731->$12;
      #line 154 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1811 = _M0MPC15array5Array2atGfE(_M0L2hiS1818, _M0L1iS733);
      _M0L2hiS1817 = _M0L1pS731->$12;
      #line 154 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1816 = _M0MPC15array5Array2atGfE(_M0L2hiS1817, _M0L1iS733);
      _M0L6_2atmpS1814 = -_M0L6_2atmpS1816;
      _M0L3triS1815 = _M0L1pS731->$21;
      _M0L6_2atmpS1813 = _M0L6_2atmpS1814 / _M0L3triS1815;
      _M0L6_2atmpS1812 = _M0L2dtS734 * _M0L6_2atmpS1813;
      _M0L6_2atmpS1810 = _M0L6_2atmpS1811 + _M0L6_2atmpS1812;
      #line 154 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L2hiS1809, _M0L1iS733, _M0L6_2atmpS1810);
      _M0L6_2atmpS1819 = _M0L1iS733 + 1;
      _M0L1iS733 = _M0L6_2atmpS1819;
      continue;
    }
    break;
  }
  _M0L7_2abindS736 = 0;
  _M0L1iS737 = _M0L7_2abindS736;
  while (1) {
    if (_M0L1iS737 < _M0L1nS730) {
      struct _M0TPB5ArrayGfE* _M0L3gluS1820 = _M0L1pS731->$13;
      struct _M0TPB5ArrayGfE* _M0L4gabaS1821;
      int32_t _M0L6_2atmpS1822;
      #line 157 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L3gluS1820, _M0L1iS737, 0x0p+0f);
      _M0L4gabaS1821 = _M0L1pS731->$14;
      #line 158 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L4gabaS1821, _M0L1iS737, 0x0p+0f);
      _M0L6_2atmpS1822 = _M0L1iS737 + 1;
      _M0L1iS737 = _M0L6_2atmpS1822;
      continue;
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt12step__neuron(
  struct _M0TP26RiantR8snn__mbt2IF* _M0L1pS716,
  float _M0L2dtS725
) {
  int32_t _M0L1nS715;
  struct _M0TP26RiantR8snn__mbt11IFParameter* _M0L3p__S717;
  float _M0L2tmS718;
  float _M0L2elS719;
  float _M0L1rS720;
  float _M0L2vtS721;
  float _M0L2vrS722;
  struct _M0TP26RiantR8snn__mbt9PostSpike* _M0L5spikeS1760;
  float _M0L11tabs__constS723;
  float _M0L6_2atmpS1759;
  int32_t _M0L11tabs__stepsS724;
  int32_t _M0L7_2abindS726;
  int32_t _M0L1iS727;
  #line 176 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L1nS715 = _M0L1pS716->$2;
  _M0L3p__S717 = _M0L1pS716->$0;
  _M0L2tmS718 = _M0L3p__S717->$2;
  _M0L2elS719 = _M0L3p__S717->$5;
  _M0L1rS720 = _M0L3p__S717->$6;
  _M0L2vtS721 = _M0L3p__S717->$3;
  _M0L2vrS722 = _M0L3p__S717->$4;
  _M0L5spikeS1760 = _M0L1pS716->$1;
  _M0L11tabs__constS723 = _M0L5spikeS1760->$0;
  _M0L6_2atmpS1759 = _M0L11tabs__constS723 / _M0L2dtS725;
  #line 185 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L11tabs__stepsS724 = _M0MPC15float5Float7to__int(_M0L6_2atmpS1759);
  _M0L7_2abindS726 = 0;
  _M0L1iS727 = _M0L7_2abindS726;
  while (1) {
    if (_M0L1iS727 < _M0L1nS715) {
      struct _M0TPB5ArrayGiE* _M0L4tabsS1719 = _M0L1pS716->$6;
      int32_t _M0L6_2atmpS1718;
      struct _M0TPB5ArrayGfE* _M0L1vS1725;
      struct _M0TPB5ArrayGfE* _M0L1vS1746;
      float _M0L6_2atmpS1727;
      float _M0L6_2atmpS1729;
      struct _M0TPB5ArrayGfE* _M0L1vS1745;
      float _M0L6_2atmpS1744;
      float _M0L6_2atmpS1743;
      float _M0L6_2atmpS1735;
      struct _M0TPB5ArrayGfE* _M0L1wS1742;
      float _M0L6_2atmpS1741;
      float _M0L6_2atmpS1738;
      struct _M0TPB5ArrayGfE* _M0L1iS1740;
      float _M0L6_2atmpS1739;
      float _M0L6_2atmpS1737;
      float _M0L6_2atmpS1736;
      float _M0L6_2atmpS1731;
      struct _M0TPB5ArrayGfE* _M0L9syn__currS1734;
      float _M0L6_2atmpS1733;
      float _M0L6_2atmpS1732;
      float _M0L6_2atmpS1730;
      float _M0L6_2atmpS1728;
      float _M0L6_2atmpS1726;
      struct _M0TPB5ArrayGbE* _M0L4fireS1747;
      struct _M0TPB5ArrayGfE* _M0L1vS1750;
      float _M0L6_2atmpS1749;
      int32_t _M0L6_2atmpS1748;
      struct _M0TPB5ArrayGfE* _M0L1vS1751;
      struct _M0TPB5ArrayGbE* _M0L4fireS1753;
      float _M0L6_2atmpS1752;
      struct _M0TPB5ArrayGiE* _M0L4tabsS1755;
      struct _M0TPB5ArrayGbE* _M0L4fireS1757;
      int32_t _M0L6_2atmpS1756;
      int32_t _M0L6_2atmpS1717;
      #line 188 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1718
      = _M0MPC15array5Array2atGiE(_M0L4tabsS1719, _M0L1iS727);
      if (_M0L6_2atmpS1718 > 0) {
        struct _M0TPB5ArrayGbE* _M0L4fireS1720 = _M0L1pS716->$5;
        struct _M0TPB5ArrayGiE* _M0L4tabsS1721;
        struct _M0TPB5ArrayGiE* _M0L4tabsS1724;
        int32_t _M0L6_2atmpS1723;
        int32_t _M0L6_2atmpS1722;
        #line 189 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
        _M0MPC15array5Array3setGbE(_M0L4fireS1720, _M0L1iS727, 0);
        _M0L4tabsS1721 = _M0L1pS716->$6;
        _M0L4tabsS1724 = _M0L1pS716->$6;
        #line 190 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
        _M0L6_2atmpS1723
        = _M0MPC15array5Array2atGiE(_M0L4tabsS1724, _M0L1iS727);
        _M0L6_2atmpS1722 = _M0L6_2atmpS1723 - 1;
        #line 190 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
        _M0MPC15array5Array3setGiE(_M0L4tabsS1721, _M0L1iS727, _M0L6_2atmpS1722);
        goto join_728;
      }
      _M0L1vS1725 = _M0L1pS716->$3;
      _M0L1vS1746 = _M0L1pS716->$3;
      #line 193 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1727 = _M0MPC15array5Array2atGfE(_M0L1vS1746, _M0L1iS727);
      _M0L6_2atmpS1729 = _M0L2dtS725 / _M0L2tmS718;
      _M0L1vS1745 = _M0L1pS716->$3;
      #line 194 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1744 = _M0MPC15array5Array2atGfE(_M0L1vS1745, _M0L1iS727);
      _M0L6_2atmpS1743 = _M0L6_2atmpS1744 - _M0L2elS719;
      _M0L6_2atmpS1735 = -_M0L6_2atmpS1743;
      _M0L1wS1742 = _M0L1pS716->$4;
      #line 194 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1741 = _M0MPC15array5Array2atGfE(_M0L1wS1742, _M0L1iS727);
      _M0L6_2atmpS1738 = -_M0L6_2atmpS1741;
      _M0L1iS1740 = _M0L1pS716->$7;
      #line 194 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1739 = _M0MPC15array5Array2atGfE(_M0L1iS1740, _M0L1iS727);
      _M0L6_2atmpS1737 = _M0L6_2atmpS1738 + _M0L6_2atmpS1739;
      _M0L6_2atmpS1736 = _M0L1rS720 * _M0L6_2atmpS1737;
      _M0L6_2atmpS1731 = _M0L6_2atmpS1735 + _M0L6_2atmpS1736;
      _M0L9syn__currS1734 = _M0L1pS716->$8;
      #line 194 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1733
      = _M0MPC15array5Array2atGfE(_M0L9syn__currS1734, _M0L1iS727);
      _M0L6_2atmpS1732 = _M0L1rS720 * _M0L6_2atmpS1733;
      _M0L6_2atmpS1730 = _M0L6_2atmpS1731 - _M0L6_2atmpS1732;
      _M0L6_2atmpS1728 = _M0L6_2atmpS1729 * _M0L6_2atmpS1730;
      _M0L6_2atmpS1726 = _M0L6_2atmpS1727 + _M0L6_2atmpS1728;
      #line 193 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS1725, _M0L1iS727, _M0L6_2atmpS1726);
      _M0L4fireS1747 = _M0L1pS716->$5;
      _M0L1vS1750 = _M0L1pS716->$3;
      #line 195 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1749 = _M0MPC15array5Array2atGfE(_M0L1vS1750, _M0L1iS727);
      _M0L6_2atmpS1748 = _M0L6_2atmpS1749 > _M0L2vtS721;
      #line 195 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGbE(_M0L4fireS1747, _M0L1iS727, _M0L6_2atmpS1748);
      _M0L1vS1751 = _M0L1pS716->$3;
      _M0L4fireS1753 = _M0L1pS716->$5;
      #line 196 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS1753, _M0L1iS727)) {
        _M0L6_2atmpS1752 = _M0L2vrS722;
      } else {
        struct _M0TPB5ArrayGfE* _M0L1vS1754 = _M0L1pS716->$3;
        #line 196 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
        _M0L6_2atmpS1752 = _M0MPC15array5Array2atGfE(_M0L1vS1754, _M0L1iS727);
      }
      #line 196 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS1751, _M0L1iS727, _M0L6_2atmpS1752);
      _M0L4tabsS1755 = _M0L1pS716->$6;
      _M0L4fireS1757 = _M0L1pS716->$5;
      #line 197 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS1757, _M0L1iS727)) {
        _M0L6_2atmpS1756 = _M0L11tabs__stepsS724;
      } else {
        struct _M0TPB5ArrayGiE* _M0L4tabsS1758 = _M0L1pS716->$6;
        #line 197 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
        _M0L6_2atmpS1756
        = _M0MPC15array5Array2atGiE(_M0L4tabsS1758, _M0L1iS727);
      }
      #line 197 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGiE(_M0L4tabsS1755, _M0L1iS727, _M0L6_2atmpS1756);
      goto join_728;
      goto joinlet_2083;
      join_728:;
      _M0L6_2atmpS1717 = _M0L1iS727 + 1;
      _M0L1iS727 = _M0L6_2atmpS1717;
      continue;
      joinlet_2083:;
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt20route__pre__to__post(
  struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS702,
  struct _M0TP26RiantR8snn__mbt10AdExSinExp* _M0L4postS704,
  struct _M0TPB5ArrayGfE* _M0L7weightsS710,
  int32_t _M0L3excS711,
  int32_t _M0L3inhS712
) {
  int32_t _M0L6n__preS701;
  int32_t _M0L7n__postS703;
  int32_t _M0L7_2abindS705;
  int32_t _M0L1iS706;
  #line 42 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sinexp_helpers.mbt"
  _M0L6n__preS701 = _M0L3preS702->$2;
  _M0L7n__postS703 = _M0L4postS704->$2;
  _M0L7_2abindS705 = 0;
  _M0L1iS706 = _M0L7_2abindS705;
  while (1) {
    if (_M0L1iS706 < _M0L6n__preS701) {
      struct _M0TPB5ArrayGbE* _M0L4fireS1704 = _M0L3preS702->$5;
      int32_t _M0L6_2atmpS1716;
      #line 52 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sinexp_helpers.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS1704, _M0L1iS706)) {
        int32_t _M0L7_2abindS707 = 0;
        int32_t _M0L1jS708 = _M0L7_2abindS707;
        while (1) {
          if (_M0L1jS708 < _M0L7n__postS703) {
            int32_t _M0L6_2atmpS1714 = _M0L1iS706 * _M0L7n__postS703;
            int32_t _M0L6_2atmpS1713 = _M0L6_2atmpS1714 + _M0L1jS708;
            float _M0L1wS709;
            int32_t _M0L6_2atmpS1715;
            #line 54 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sinexp_helpers.mbt"
            _M0L1wS709
            = _M0MPC15array5Array2atGfE(_M0L7weightsS710, _M0L6_2atmpS1713);
            if (_M0L1wS709 != 0x0p+0f) {
              if (_M0L3excS711) {
                struct _M0TPB5ArrayGfE* _M0L3gluS1705 = _M0L4postS704->$12;
                struct _M0TPB5ArrayGfE* _M0L3gluS1708 = _M0L4postS704->$12;
                float _M0L6_2atmpS1707;
                float _M0L6_2atmpS1706;
                #line 57 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sinexp_helpers.mbt"
                _M0L6_2atmpS1707
                = _M0MPC15array5Array2atGfE(_M0L3gluS1708, _M0L1jS708);
                _M0L6_2atmpS1706 = _M0L6_2atmpS1707 + _M0L1wS709;
                #line 57 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sinexp_helpers.mbt"
                _M0MPC15array5Array3setGfE(_M0L3gluS1705, _M0L1jS708, _M0L6_2atmpS1706);
              }
              if (_M0L3inhS712) {
                struct _M0TPB5ArrayGfE* _M0L4gabaS1709 = _M0L4postS704->$13;
                struct _M0TPB5ArrayGfE* _M0L4gabaS1712 = _M0L4postS704->$13;
                float _M0L6_2atmpS1711;
                float _M0L6_2atmpS1710;
                #line 60 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sinexp_helpers.mbt"
                _M0L6_2atmpS1711
                = _M0MPC15array5Array2atGfE(_M0L4gabaS1712, _M0L1jS708);
                _M0L6_2atmpS1710 = _M0L6_2atmpS1711 + _M0L1wS709;
                #line 60 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sinexp_helpers.mbt"
                _M0MPC15array5Array3setGfE(_M0L4gabaS1709, _M0L1jS708, _M0L6_2atmpS1710);
              }
            }
            _M0L6_2atmpS1715 = _M0L1jS708 + 1;
            _M0L1jS708 = _M0L6_2atmpS1715;
            continue;
          }
          break;
        }
      }
      _M0L6_2atmpS1716 = _M0L1iS706 + 1;
      _M0L1iS706 = _M0L6_2atmpS1716;
      continue;
    }
    break;
  }
  return 0;
}

struct _M0TPB5ArrayGfE* _M0FP26RiantR8snn__mbt21make__random__weights(
  int32_t _M0L6n__preS692,
  int32_t _M0L7n__postS693,
  float _M0L2muS699,
  float _M0L1pS698,
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS697
) {
  int32_t _M0L5totalS691;
  struct _M0TPB5ArrayGfE* _M0L3arrS694;
  int32_t _M0L7_2abindS695;
  int32_t _M0L1kS696;
  #line 20 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sinexp_helpers.mbt"
  _M0L5totalS691 = _M0L6n__preS692 * _M0L7n__postS693;
  #line 28 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sinexp_helpers.mbt"
  _M0L3arrS694 = _M0MPC15array5Array4makeGfE(_M0L5totalS691, 0x0p+0f);
  _M0L7_2abindS695 = 0;
  _M0L1kS696 = _M0L7_2abindS695;
  while (1) {
    if (_M0L1kS696 < _M0L5totalS691) {
      float _M0L6_2atmpS1702;
      int32_t _M0L6_2atmpS1703;
      #line 30 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sinexp_helpers.mbt"
      _M0L6_2atmpS1702 = _M0FP26RiantR8snn__mbt9next__f32(_M0L3rngS697);
      if (_M0L6_2atmpS1702 < _M0L1pS698) {
        #line 31 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sinexp_helpers.mbt"
        _M0MPC15array5Array3setGfE(_M0L3arrS694, _M0L1kS696, _M0L2muS699);
      }
      _M0L6_2atmpS1703 = _M0L1kS696 + 1;
      _M0L1kS696 = _M0L6_2atmpS1703;
      continue;
    }
    break;
  }
  return _M0L3arrS694;
}

struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0MP26RiantR8snn__mbt7Xoshiro3new(
  uint64_t _M0L4seedS689
) {
  struct _M0TUmmmmE* _M0L1sS688;
  uint64_t _M0L6_2atmpS1701;
  struct _M0TUmmmmE* _M0L1tS690;
  uint64_t _M0L6_2atmpS1697;
  uint64_t _M0L6_2atmpS1698;
  uint64_t _M0L6_2atmpS1699;
  uint64_t _M0L6_2atmpS1700;
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _block_2087;
  #line 48 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  #line 49 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L1sS688 = _M0FP26RiantR8snn__mbt10splitmix64(_M0L4seedS689);
  _M0L6_2atmpS1701 = _M0L1sS688->$3;
  #line 50 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L1tS690 = _M0FP26RiantR8snn__mbt10splitmix64(_M0L6_2atmpS1701);
  _M0L6_2atmpS1697 = _M0L1sS688->$0;
  _M0L6_2atmpS1698 = _M0L1sS688->$1;
  _M0L6_2atmpS1699 = _M0L1sS688->$2;
  moonbit_decref(_M0L1sS688);
  _M0L6_2atmpS1700 = _M0L1tS690->$0;
  moonbit_decref(_M0L1tS690);
  _block_2087
  = (struct _M0TP26RiantR8snn__mbt7Xoshiro*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt7Xoshiro));
  Moonbit_object_header(_block_2087)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_2087->$0 = _M0L6_2atmpS1697;
  _block_2087->$1 = _M0L6_2atmpS1698;
  _block_2087->$2 = _M0L6_2atmpS1699;
  _block_2087->$3 = _M0L6_2atmpS1700;
  return _block_2087;
}

struct _M0TUmmmmE* _M0FP26RiantR8snn__mbt10splitmix64(uint64_t _M0L4seedS680) {
  uint64_t _M0L2s1S679;
  uint64_t _M0L2z1S681;
  uint64_t _M0L2s2S682;
  uint64_t _M0L2z2S683;
  uint64_t _M0L2s3S684;
  uint64_t _M0L2z3S685;
  uint64_t _M0L2s4S686;
  uint64_t _M0L2z4S687;
  struct _M0TUmmmmE* _block_2088;
  #line 62 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2s1S679 = _M0L4seedS680 + 11400714819323198485ull;
  #line 64 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2z1S681 = _M0FP26RiantR8snn__mbt5mix64(_M0L2s1S679);
  _M0L2s2S682 = _M0L2s1S679 + 11400714819323198485ull;
  #line 66 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2z2S683 = _M0FP26RiantR8snn__mbt5mix64(_M0L2s2S682);
  _M0L2s3S684 = _M0L2s2S682 + 11400714819323198485ull;
  #line 68 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2z3S685 = _M0FP26RiantR8snn__mbt5mix64(_M0L2s3S684);
  _M0L2s4S686 = _M0L2s3S684 + 11400714819323198485ull;
  #line 70 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2z4S687 = _M0FP26RiantR8snn__mbt5mix64(_M0L2s4S686);
  _block_2088 = (struct _M0TUmmmmE*)moonbit_malloc(sizeof(struct _M0TUmmmmE));
  Moonbit_object_header(_block_2088)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_2088->$0 = _M0L2z1S681;
  _block_2088->$1 = _M0L2z2S683;
  _block_2088->$2 = _M0L2z3S685;
  _block_2088->$3 = _M0L2z4S687;
  return _block_2088;
}

uint64_t _M0FP26RiantR8snn__mbt5mix64(uint64_t _M0L1zS677) {
  uint64_t _M0L6_2atmpS1696;
  uint64_t _M0L6_2atmpS1695;
  uint64_t _M0L1zS676;
  uint64_t _M0L6_2atmpS1694;
  uint64_t _M0L6_2atmpS1693;
  uint64_t _M0L1zS678;
  uint64_t _M0L6_2atmpS1692;
  #line 75 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L6_2atmpS1696 = _M0L1zS677 >> 30;
  _M0L6_2atmpS1695 = _M0L1zS677 ^ _M0L6_2atmpS1696;
  _M0L1zS676 = _M0L6_2atmpS1695 * 13787848793156543929ull;
  _M0L6_2atmpS1694 = _M0L1zS676 >> 27;
  _M0L6_2atmpS1693 = _M0L1zS676 ^ _M0L6_2atmpS1694;
  _M0L1zS678 = _M0L6_2atmpS1693 * 10723151780598845931ull;
  _M0L6_2atmpS1692 = _M0L1zS678 >> 31;
  return _M0L1zS678 ^ _M0L6_2atmpS1692;
}

int32_t _M0FP26RiantR8snn__mbt13stimulate__if(
  struct _M0TP26RiantR8snn__mbt17PoissonStimulusIF* _M0L1sS665,
  float _M0L4timeS675,
  float _M0L2dtS667
) {
  struct _M0TP26RiantR8snn__mbt12PoissonFixed* _M0L5paramS1679;
  struct _M0TPB5ArrayGbE* _M0L6activeS1678;
  int32_t _M0L6_2atmpS1677;
  struct _M0TP26RiantR8snn__mbt12PoissonFixed* _M0L5paramS1691;
  float _M0L4rateS1690;
  float _M0L6lambdaS666;
  struct _M0TPB5ArrayGiE* _M0L7_2abindS668;
  int32_t _M0L7_2abindS669;
  int32_t* _M0L7_2abindS670;
  int32_t _M0L2__S671;
  #line 111 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson.mbt"
  _M0L5paramS1679 = _M0L1sS665->$0;
  _M0L6activeS1678 = _M0L5paramS1679->$2;
  #line 116 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson.mbt"
  _M0L6_2atmpS1677 = _M0MPC15array5Array2atGbE(_M0L6activeS1678, 0);
  if (!_M0L6_2atmpS1677) {
    return 0;
  }
  _M0L5paramS1691 = _M0L1sS665->$0;
  _M0L4rateS1690 = _M0L5paramS1691->$0;
  _M0L6lambdaS666 = _M0L4rateS1690 * _M0L2dtS667;
  if (_M0L6lambdaS666 <= 0x0p+0f) {
    return 0;
  }
  _M0L7_2abindS668 = _M0L1sS665->$1;
  _M0L7_2abindS669 = _M0L7_2abindS668->$1;
  _M0L7_2abindS670 = _M0L7_2abindS668->$0;
  moonbit_incref(_M0L7_2abindS670);
  _M0L2__S671 = 0;
  while (1) {
    if (_M0L2__S671 < _M0L7_2abindS669) {
      int32_t _M0L1nS672 = (int32_t)_M0L7_2abindS670[_M0L2__S671];
      struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS1688 = _M0L1sS665->$3;
      int32_t _M0L1kS673;
      int32_t _M0L6_2atmpS1689;
      #line 124 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson.mbt"
      _M0L1kS673
      = _M0FP26RiantR8snn__mbt15sample__poisson(_M0L3rngS1688, _M0L6lambdaS666);
      if (_M0L1kS673 > 0) {
        struct _M0TPB5ArrayGfE* _M0L1gS1680 = _M0L1sS665->$2;
        struct _M0TPB5ArrayGfE* _M0L1gS1687 = _M0L1sS665->$2;
        float _M0L6_2atmpS1682;
        struct _M0TP26RiantR8snn__mbt12PoissonFixed* _M0L5paramS1686;
        float _M0L2muS1684;
        float _M0L6_2atmpS1685;
        float _M0L6_2atmpS1683;
        float _M0L6_2atmpS1681;
        #line 126 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson.mbt"
        _M0L6_2atmpS1682 = _M0MPC15array5Array2atGfE(_M0L1gS1687, _M0L1nS672);
        _M0L5paramS1686 = _M0L1sS665->$0;
        _M0L2muS1684 = _M0L5paramS1686->$1;
        _M0L6_2atmpS1685 = (float)_M0L1kS673;
        _M0L6_2atmpS1683 = _M0L2muS1684 * _M0L6_2atmpS1685;
        _M0L6_2atmpS1681 = _M0L6_2atmpS1682 + _M0L6_2atmpS1683;
        #line 126 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson.mbt"
        _M0MPC15array5Array3setGfE(_M0L1gS1680, _M0L1nS672, _M0L6_2atmpS1681);
      }
      _M0L6_2atmpS1689 = _M0L2__S671 + 1;
      _M0L2__S671 = _M0L6_2atmpS1689;
      continue;
    } else {
      moonbit_decref(_M0L7_2abindS670);
    }
    break;
  }
  return 0;
}

struct _M0TP26RiantR8snn__mbt17PoissonStimulusIF* _M0MP26RiantR8snn__mbt17PoissonStimulusIF3new(
  struct _M0TP26RiantR8snn__mbt2IF* _M0L3popS656,
  moonbit_string_t _M0L3symS662,
  float _M0L4rateS663,
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS664
) {
  int32_t _M0L1nS655;
  int32_t* _M0L6_2atmpS1676;
  struct _M0TPB5ArrayGiE* _M0L7neuronsS657;
  int32_t _M0L7_2abindS658;
  int32_t _M0L1kS659;
  struct _M0TPB5ArrayGfE* _M0L1gS661;
  struct _M0TP26RiantR8snn__mbt12PoissonFixed* _M0L6_2atmpS1675;
  struct _M0TP26RiantR8snn__mbt17PoissonStimulusIF* _block_2091;
  #line 92 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson.mbt"
  _M0L1nS655 = _M0L3popS656->$2;
  _M0L6_2atmpS1676 = (int32_t*)moonbit_empty_int32_array;
  _M0L7neuronsS657
  = (struct _M0TPB5ArrayGiE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGiE));
  Moonbit_object_header(_M0L7neuronsS657)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 44, 0);
  _M0L7neuronsS657->$0 = _M0L6_2atmpS1676;
  _M0L7neuronsS657->$1 = 0;
  _M0L7_2abindS658 = 0;
  _M0L1kS659 = _M0L7_2abindS658;
  while (1) {
    if (_M0L1kS659 < _M0L1nS655) {
      int32_t _M0L6_2atmpS1674;
      #line 101 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson.mbt"
      _M0MPC15array5Array4pushGiE(_M0L7neuronsS657, _M0L1kS659);
      _M0L6_2atmpS1674 = _M0L1kS659 + 1;
      _M0L1kS659 = _M0L6_2atmpS1674;
      continue;
    }
    break;
  }
  #line 103 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson.mbt"
  if (
    _M0L3symS662 == (moonbit_string_t)moonbit_string_literal_3.data
    || Moonbit_array_length(_M0L3symS662)
       == Moonbit_array_length((moonbit_string_t)moonbit_string_literal_3.data)
       && 0
          == memcmp(_M0L3symS662, (moonbit_string_t)moonbit_string_literal_3.data, Moonbit_array_length(_M0L3symS662) * 2)
  ) {
    struct _M0TPB5ArrayGfE* _M0L8_2afieldS2041 = _M0L3popS656->$13;
    moonbit_incref(_M0L8_2afieldS2041);
    _M0L1gS661 = _M0L8_2afieldS2041;
  } else {
    struct _M0TPB5ArrayGfE* _M0L8_2afieldS2042 = _M0L3popS656->$14;
    moonbit_incref(_M0L8_2afieldS2042);
    _M0L1gS661 = _M0L8_2afieldS2042;
  }
  #line 104 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson.mbt"
  _M0L6_2atmpS1675 = _M0MP26RiantR8snn__mbt12PoissonFixed3new(_M0L4rateS663);
  moonbit_incref(_M0L3rngS664);
  _block_2091
  = (struct _M0TP26RiantR8snn__mbt17PoissonStimulusIF*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt17PoissonStimulusIF));
  Moonbit_object_header(_block_2091)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 47, 0);
  _block_2091->$0 = _M0L6_2atmpS1675;
  _block_2091->$1 = _M0L7neuronsS657;
  _block_2091->$2 = _M0L1gS661;
  _block_2091->$3 = _M0L3rngS664;
  return _block_2091;
}

struct _M0TP26RiantR8snn__mbt12PoissonFixed* _M0MP26RiantR8snn__mbt12PoissonFixed3new(
  float _M0L4rateS654
) {
  uint8_t* _M0L6_2atmpS1673;
  struct _M0TPB5ArrayGbE* _M0L6_2atmpS1672;
  struct _M0TP26RiantR8snn__mbt12PoissonFixed* _block_2092;
  #line 23 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson.mbt"
  _M0L6_2atmpS1673 = (uint8_t*)moonbit_make_bytes_raw(1);
  _M0L6_2atmpS1673[0] = 1;
  _M0L6_2atmpS1672
  = (struct _M0TPB5ArrayGbE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGbE));
  Moonbit_object_header(_M0L6_2atmpS1672)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 53, 0);
  _M0L6_2atmpS1672->$0 = _M0L6_2atmpS1673;
  _M0L6_2atmpS1672->$1 = 1;
  _block_2092
  = (struct _M0TP26RiantR8snn__mbt12PoissonFixed*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt12PoissonFixed));
  Moonbit_object_header(_block_2092)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 56, 0);
  _block_2092->$0 = _M0L4rateS654;
  _block_2092->$1 = 0x1p+0f;
  _block_2092->$2 = _M0L6_2atmpS1672;
  return _block_2092;
}

int32_t _M0FP26RiantR8snn__mbt15sample__poisson(
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS652,
  float _M0L6lambdaS646
) {
  float _M0L6_2atmpS1671;
  float _M0L6_2atmpS1670;
  double _M0L1lS647;
  struct _M0TPB8MutLocalGdE* _M0L1pS648;
  struct _M0TPB8MutLocalGiE* _M0L1kS649;
  float _M0L6_2atmpS1669;
  int32_t _M0L8ten__lamS651;
  int32_t _M0L3capS650;
  int32_t _M0L3valS1668;
  #line 55 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson.mbt"
  if (_M0L6lambdaS646 <= 0x0p+0f) {
    return 0;
  }
  _M0L6_2atmpS1671 = -_M0L6lambdaS646;
  #line 59 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson.mbt"
  _M0L6_2atmpS1670 = _M0FP26RiantR8snn__mbt4expf(_M0L6_2atmpS1671);
  _M0L1lS647 = (double)_M0L6_2atmpS1670;
  _M0L1pS648
  = (struct _M0TPB8MutLocalGdE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGdE));
  Moonbit_object_header(_M0L1pS648)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1pS648->$0 = 0x1p+0;
  _M0L1kS649
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1kS649)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1kS649->$0 = 0;
  _M0L6_2atmpS1669 = _M0L6lambdaS646 * 0x1.4p+3f;
  #line 63 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson.mbt"
  _M0L8ten__lamS651 = _M0MPC15float5Float7to__int(_M0L6_2atmpS1669);
  if (_M0L8ten__lamS651 > 100) {
    _M0L3capS650 = _M0L8ten__lamS651;
  } else {
    _M0L3capS650 = 100;
  }
  while (1) {
    int32_t _M0L3valS1660 = _M0L1kS649->$0;
    int32_t _M0L6_2atmpS1659 = _M0L3valS1660 + 1;
    double _M0L3valS1662;
    double _M0L6_2atmpS1663;
    double _M0L6_2atmpS1661;
    double _M0L3valS1664;
    int32_t _M0L3valS1666;
    _M0L1kS649->$0 = _M0L6_2atmpS1659;
    _M0L3valS1662 = _M0L1pS648->$0;
    #line 68 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson.mbt"
    _M0L6_2atmpS1663 = _M0FP26RiantR8snn__mbt9next__f64(_M0L3rngS652);
    _M0L6_2atmpS1661 = _M0L3valS1662 * _M0L6_2atmpS1663;
    _M0L1pS648->$0 = _M0L6_2atmpS1661;
    _M0L3valS1664 = _M0L1pS648->$0;
    if (_M0L3valS1664 < _M0L1lS647) {
      int32_t _M0L3valS1665;
      moonbit_decref(_M0L1pS648);
      _M0L3valS1665 = _M0L1kS649->$0;
      moonbit_decref(_M0L1kS649);
      return _M0L3valS1665 - 1;
    }
    _M0L3valS1666 = _M0L1kS649->$0;
    if (_M0L3valS1666 > _M0L3capS650) {
      int32_t _M0L3valS1667;
      moonbit_decref(_M0L1pS648);
      _M0L3valS1667 = _M0L1kS649->$0;
      moonbit_decref(_M0L1kS649);
      return _M0L3valS1667 - 1;
    }
    continue;
    break;
  }
  _M0L3valS1668 = _M0L1kS649->$0;
  moonbit_decref(_M0L1kS649);
  return _M0L3valS1668 - 1;
}

double _M0FP26RiantR8snn__mbt9next__f64(
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L1rS644
) {
  uint64_t _M0L1uS643;
  uint64_t _M0L4bitsS645;
  double _M0L6_2atmpS1658;
  #line 118 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  #line 119 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L1uS643 = _M0FP26RiantR8snn__mbt9next__u64(_M0L1rS644);
  _M0L4bitsS645 = _M0L1uS643 >> 11;
  _M0L6_2atmpS1658 = (double)_M0L4bitsS645;
  return _M0L6_2atmpS1658 * 0x1p-53;
}

float _M0FP26RiantR8snn__mbt9next__f32(
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L1rS641
) {
  uint32_t _M0L1uS640;
  uint32_t _M0L4bitsS642;
  double _M0L6_2atmpS1657;
  double _M0L6_2atmpS1656;
  #line 128 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  #line 129 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L1uS640 = _M0FP26RiantR8snn__mbt9next__u32(_M0L1rS641);
  _M0L4bitsS642 = _M0L1uS640 >> 8;
  _M0L6_2atmpS1657 = (double)_M0L4bitsS642;
  _M0L6_2atmpS1656 = _M0L6_2atmpS1657 * 0x1p-24;
  return (float)_M0L6_2atmpS1656;
}

uint32_t _M0FP26RiantR8snn__mbt9next__u32(
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L1rS639
) {
  uint64_t _M0L1uS638;
  uint64_t _M0L6_2atmpS1655;
  #line 110 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  #line 111 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L1uS638 = _M0FP26RiantR8snn__mbt9next__u64(_M0L1rS639);
  _M0L6_2atmpS1655 = _M0L1uS638 >> 32;
  return (uint32_t)_M0L6_2atmpS1655;
}

uint64_t _M0FP26RiantR8snn__mbt9next__u64(
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L1rS631
) {
  uint64_t _M0L2s0S630;
  uint64_t _M0L2s1S632;
  uint64_t _M0L2s2S633;
  uint64_t _M0L2s3S634;
  uint64_t _M0L3tmpS635;
  uint64_t _M0L6_2atmpS1654;
  uint64_t _M0L3resS636;
  uint64_t _M0L1tS637;
  uint64_t _M0L6_2atmpS1644;
  uint64_t _M0L6_2atmpS1645;
  uint64_t _M0L2s2S1647;
  uint64_t _M0L6_2atmpS1646;
  uint64_t _M0L2s3S1649;
  uint64_t _M0L6_2atmpS1648;
  uint64_t _M0L2s2S1651;
  uint64_t _M0L6_2atmpS1650;
  uint64_t _M0L2s3S1653;
  uint64_t _M0L6_2atmpS1652;
  #line 90 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2s0S630 = _M0L1rS631->$0;
  _M0L2s1S632 = _M0L1rS631->$1;
  _M0L2s2S633 = _M0L1rS631->$2;
  _M0L2s3S634 = _M0L1rS631->$3;
  _M0L3tmpS635 = _M0L2s0S630 + _M0L2s3S634;
  #line 96 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L6_2atmpS1654 = _M0FP26RiantR8snn__mbt4rotl(_M0L3tmpS635, 23);
  _M0L3resS636 = _M0L6_2atmpS1654 + _M0L2s0S630;
  _M0L1tS637 = _M0L2s1S632 << 17;
  _M0L6_2atmpS1644 = _M0L2s2S633 ^ _M0L2s0S630;
  _M0L1rS631->$2 = _M0L6_2atmpS1644;
  _M0L6_2atmpS1645 = _M0L2s3S634 ^ _M0L2s1S632;
  _M0L1rS631->$3 = _M0L6_2atmpS1645;
  _M0L2s2S1647 = _M0L1rS631->$2;
  _M0L6_2atmpS1646 = _M0L2s1S632 ^ _M0L2s2S1647;
  _M0L1rS631->$1 = _M0L6_2atmpS1646;
  _M0L2s3S1649 = _M0L1rS631->$3;
  _M0L6_2atmpS1648 = _M0L2s0S630 ^ _M0L2s3S1649;
  _M0L1rS631->$0 = _M0L6_2atmpS1648;
  _M0L2s2S1651 = _M0L1rS631->$2;
  _M0L6_2atmpS1650 = _M0L2s2S1651 ^ _M0L1tS637;
  _M0L1rS631->$2 = _M0L6_2atmpS1650;
  _M0L2s3S1653 = _M0L1rS631->$3;
  #line 103 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L6_2atmpS1652 = _M0FP26RiantR8snn__mbt4rotl(_M0L2s3S1653, 45);
  _M0L1rS631->$3 = _M0L6_2atmpS1652;
  return _M0L3resS636;
}

uint64_t _M0FP26RiantR8snn__mbt4rotl(uint64_t _M0L1xS628, int32_t _M0L1kS629) {
  uint64_t _M0L6_2atmpS1641;
  int32_t _M0L6_2atmpS1643;
  uint64_t _M0L6_2atmpS1642;
  #line 83 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L6_2atmpS1641 = _M0L1xS628 << (_M0L1kS629 & 63);
  _M0L6_2atmpS1643 = 64 - _M0L1kS629;
  _M0L6_2atmpS1642 = _M0L1xS628 >> (_M0L6_2atmpS1643 & 63);
  return _M0L6_2atmpS1641 | _M0L6_2atmpS1642;
}

moonbit_string_t _M0IPC15float5FloatPB4Show10to__string(float _M0L4selfS627) {
  double _M0L6_2atmpS1640;
  #line 16 "C:\\Users\\31379\\.moon\\lib\\core\\float\\methods.mbt"
  _M0L6_2atmpS1640 = (double)_M0L4selfS627;
  #line 17 "C:\\Users\\31379\\.moon\\lib\\core\\float\\methods.mbt"
  return _M0MPC16double6Double10to__string(_M0L6_2atmpS1640);
}

int32_t _M0MPC15float5Float7to__int(float _M0L4selfS626) {
  #line 43 "C:\\Users\\31379\\.moon\\lib\\core\\float\\to_int.mbt"
  if (_M0L4selfS626 != _M0L4selfS626) {
    return 0;
  } else if (_M0L4selfS626 >= 0x1.fffffffcp+30f) {
    return 2147483647;
  } else if (_M0L4selfS626 <= -0x1p+31f) {
    return (int32_t)0x80000000;
  } else {
    return (int32_t)_M0L4selfS626;
  }
}

struct _M0TPB5ArrayGfE* _M0MPC15array5Array4makeGfE(
  int32_t _M0L3lenS612,
  float _M0L4elemS614
) {
  struct _M0TPB5ArrayGfE* _M0L3arrS611;
  int32_t _M0L1iS613;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3arrS611 = _M0MPC15array5Array20unsafe__make__uninitGfE(_M0L3lenS612);
  _M0L1iS613 = 0;
  while (1) {
    if (_M0L1iS613 < _M0L3lenS612) {
      float* _M0L3bufS1634 = _M0L3arrS611->$0;
      int32_t _M0L6_2atmpS1635;
      _M0L3bufS1634[_M0L1iS613] = _M0L4elemS614;
      _M0L6_2atmpS1635 = _M0L1iS613 + 1;
      _M0L1iS613 = _M0L6_2atmpS1635;
      continue;
    }
    break;
  }
  return _M0L3arrS611;
}

struct _M0TPB5ArrayGbE* _M0MPC15array5Array4makeGbE(
  int32_t _M0L3lenS617,
  int32_t _M0L4elemS619
) {
  struct _M0TPB5ArrayGbE* _M0L3arrS616;
  int32_t _M0L1iS618;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3arrS616 = _M0MPC15array5Array20unsafe__make__uninitGbE(_M0L3lenS617);
  _M0L1iS618 = 0;
  while (1) {
    if (_M0L1iS618 < _M0L3lenS617) {
      uint8_t* _M0L3bufS1636 = _M0L3arrS616->$0;
      int32_t _M0L6_2atmpS1637;
      _M0L3bufS1636[_M0L1iS618] = _M0L4elemS619;
      _M0L6_2atmpS1637 = _M0L1iS618 + 1;
      _M0L1iS618 = _M0L6_2atmpS1637;
      continue;
    }
    break;
  }
  return _M0L3arrS616;
}

struct _M0TPB5ArrayGiE* _M0MPC15array5Array4makeGiE(
  int32_t _M0L3lenS622,
  int32_t _M0L4elemS624
) {
  struct _M0TPB5ArrayGiE* _M0L3arrS621;
  int32_t _M0L1iS623;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3arrS621 = _M0MPC15array5Array20unsafe__make__uninitGiE(_M0L3lenS622);
  _M0L1iS623 = 0;
  while (1) {
    if (_M0L1iS623 < _M0L3lenS622) {
      int32_t* _M0L3bufS1638 = _M0L3arrS621->$0;
      int32_t _M0L6_2atmpS1639;
      _M0L3bufS1638[_M0L1iS623] = _M0L4elemS624;
      _M0L6_2atmpS1639 = _M0L1iS623 + 1;
      _M0L1iS623 = _M0L6_2atmpS1639;
      continue;
    }
    break;
  }
  return _M0L3arrS621;
}

int32_t _M0MPC15array5Array3setGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS600,
  int32_t _M0L5indexS601,
  float _M0L5valueS602
) {
  int32_t _M0L3lenS599;
  #line 262 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS599 = _M0L4selfS600->$1;
  if (_M0L5indexS601 >= 0 && _M0L5indexS601 < _M0L3lenS599) {
    float* _M0L6_2atmpS1631;
    #line 268 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS1631 = _M0MPC15array5Array6bufferGfE(_M0L4selfS600);
    _M0L6_2atmpS1631[_M0L5indexS601] = _M0L5valueS602;
    moonbit_decref(_M0L6_2atmpS1631);
  } else {
    #line 267 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
  return 0;
}

int32_t _M0MPC15array5Array3setGbE(
  struct _M0TPB5ArrayGbE* _M0L4selfS604,
  int32_t _M0L5indexS605,
  int32_t _M0L5valueS606
) {
  int32_t _M0L3lenS603;
  #line 262 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS603 = _M0L4selfS604->$1;
  if (_M0L5indexS605 >= 0 && _M0L5indexS605 < _M0L3lenS603) {
    uint8_t* _M0L6_2atmpS1632;
    #line 268 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS1632 = _M0MPC15array5Array6bufferGbE(_M0L4selfS604);
    _M0L6_2atmpS1632[_M0L5indexS605] = _M0L5valueS606;
    moonbit_decref(_M0L6_2atmpS1632);
  } else {
    #line 267 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
  return 0;
}

int32_t _M0MPC15array5Array3setGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS608,
  int32_t _M0L5indexS609,
  int32_t _M0L5valueS610
) {
  int32_t _M0L3lenS607;
  #line 262 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS607 = _M0L4selfS608->$1;
  if (_M0L5indexS609 >= 0 && _M0L5indexS609 < _M0L3lenS607) {
    int32_t* _M0L6_2atmpS1633;
    #line 268 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS1633 = _M0MPC15array5Array6bufferGiE(_M0L4selfS608);
    _M0L6_2atmpS1633[_M0L5indexS609] = _M0L5valueS610;
    moonbit_decref(_M0L6_2atmpS1633);
  } else {
    #line 267 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
  return 0;
}

int32_t _M0MPC15array5Array2atGbE(
  struct _M0TPB5ArrayGbE* _M0L4selfS591,
  int32_t _M0L5indexS592
) {
  int32_t _M0L3lenS590;
  #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS590 = _M0L4selfS591->$1;
  if (_M0L5indexS592 >= 0 && _M0L5indexS592 < _M0L3lenS590) {
    uint8_t* _M0L6_2atmpS1628;
    int32_t _result_2097;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS1628 = _M0MPC15array5Array6bufferGbE(_M0L4selfS591);
    _result_2097 = (int32_t)_M0L6_2atmpS1628[_M0L5indexS592];
    moonbit_decref(_M0L6_2atmpS1628);
    return _result_2097;
  } else {
    #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
}

float _M0MPC15array5Array2atGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS594,
  int32_t _M0L5indexS595
) {
  int32_t _M0L3lenS593;
  #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS593 = _M0L4selfS594->$1;
  if (_M0L5indexS595 >= 0 && _M0L5indexS595 < _M0L3lenS593) {
    float* _M0L6_2atmpS1629;
    float _result_2098;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS1629 = _M0MPC15array5Array6bufferGfE(_M0L4selfS594);
    _result_2098 = (float)_M0L6_2atmpS1629[_M0L5indexS595];
    moonbit_decref(_M0L6_2atmpS1629);
    return _result_2098;
  } else {
    #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
}

int32_t _M0MPC15array5Array2atGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS597,
  int32_t _M0L5indexS598
) {
  int32_t _M0L3lenS596;
  #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS596 = _M0L4selfS597->$1;
  if (_M0L5indexS598 >= 0 && _M0L5indexS598 < _M0L3lenS596) {
    int32_t* _M0L6_2atmpS1630;
    int32_t _result_2099;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS1630 = _M0MPC15array5Array6bufferGiE(_M0L4selfS597);
    _result_2099 = (int32_t)_M0L6_2atmpS1630[_M0L5indexS598];
    moonbit_decref(_M0L6_2atmpS1630);
    return _result_2099;
  } else {
    #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
}

int32_t _M0FPB7printlnGsE(moonbit_string_t _M0L5inputS589) {
  moonbit_string_t _M0L6_2atmpS1627;
  #line 36 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\console.mbt"
  #line 37 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\console.mbt"
  _M0L6_2atmpS1627 = _M0IPC16string6StringPB4Show10to__string(_M0L5inputS589);
  #line 37 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\console.mbt"
  moonbit_println(_M0L6_2atmpS1627);
  moonbit_decref(_M0L6_2atmpS1627);
  return 0;
}

moonbit_string_t _M0MPC16double6Double10to__string(double _M0L4selfS588) {
  #line 296 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double.mbt"
  #line 298 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double.mbt"
  return _M0FPB15ryu__to__string(_M0L4selfS588);
}

moonbit_string_t _M0FPB15ryu__to__string(double _M0L3valS573) {
  uint64_t _M0L4bitsS576;
  uint64_t _M0L6_2atmpS1626;
  uint64_t _M0L6_2atmpS1625;
  int32_t _M0L8ieeeSignS577;
  uint64_t _M0L12ieeeMantissaS578;
  uint64_t _M0L6_2atmpS1624;
  uint64_t _M0L6_2atmpS1623;
  int32_t _M0L12ieeeExponentS579;
  struct _M0TPB17FloatingDecimal64* _M0L7_2abindS580;
  struct _M0TPB17FloatingDecimal64* _M0L1vS581;
  moonbit_string_t _result_2101;
  #line 668 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  if (_M0L3valS573 == 0x0p+0) {
    return (moonbit_string_t)moonbit_string_literal_4.data;
  }
  if (_M0L3valS573 >= -0x1p+53 && _M0L3valS573 <= 0x1p+53) {
    if (_M0L3valS573 >= -0x1p+31 && _M0L3valS573 <= 0x1.fffffffcp+30) {
      int32_t _M0L1iS574;
      double _M0L6_2atmpS1612;
      #line 683 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0L1iS574 = _M0MPC16double6Double7to__int(_M0L3valS573);
      _M0L6_2atmpS1612 = (double)_M0L1iS574;
      if (_M0L6_2atmpS1612 == _M0L3valS573) {
        #line 685 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        return _M0MPC13int3Int18to__string_2einner(_M0L1iS574, 10);
      }
    } else {
      int64_t _M0L1iS575;
      double _M0L6_2atmpS1613;
      #line 688 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0L1iS575 = _M0MPC16double6Double9to__int64(_M0L3valS573);
      _M0L6_2atmpS1613 = (double)_M0L1iS575;
      if (_M0L6_2atmpS1613 == _M0L3valS573) {
        #line 690 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        return _M0MPC15int645Int6418to__string_2einner(_M0L1iS575, 10);
      }
    }
  }
  _M0L4bitsS576 = *(int64_t*)&_M0L3valS573;
  _M0L6_2atmpS1626 = _M0L4bitsS576 >> 63;
  _M0L6_2atmpS1625 = _M0L6_2atmpS1626 & 1ull;
  _M0L8ieeeSignS577 = _M0L6_2atmpS1625 != 0ull;
  _M0L12ieeeMantissaS578 = _M0L4bitsS576 & 4503599627370495ull;
  _M0L6_2atmpS1624 = _M0L4bitsS576 >> 52;
  _M0L6_2atmpS1623 = _M0L6_2atmpS1624 & 2047ull;
  _M0L12ieeeExponentS579 = (int32_t)_M0L6_2atmpS1623;
  if (
    _M0L12ieeeExponentS579 == 2047
    || _M0L12ieeeExponentS579 == 0 && _M0L12ieeeMantissaS578 == 0ull
  ) {
    int32_t _M0L6_2atmpS1614 = _M0L12ieeeExponentS579 != 0;
    int32_t _M0L6_2atmpS1615 = _M0L12ieeeMantissaS578 != 0ull;
    #line 707 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    return _M0FPB18copy__special__str(_M0L8ieeeSignS577, _M0L6_2atmpS1614, _M0L6_2atmpS1615);
  }
  #line 709 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS580
  = _M0FPB15d2d__small__int(_M0L12ieeeMantissaS578, _M0L12ieeeExponentS579);
  if (_M0L7_2abindS580 == 0) {
    uint32_t _M0L6_2atmpS1616;
    if (_M0L7_2abindS580) {
      moonbit_decref(_M0L7_2abindS580);
    }
    _M0L6_2atmpS1616 = *(uint32_t*)&_M0L12ieeeExponentS579;
    #line 719 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L1vS581 = _M0FPB3d2d(_M0L12ieeeMantissaS578, _M0L6_2atmpS1616);
  } else {
    struct _M0TPB17FloatingDecimal64* _M0L7_2aSomeS582 = _M0L7_2abindS580;
    struct _M0TPB17FloatingDecimal64* _M0L4_2afS583 = _M0L7_2aSomeS582;
    struct _M0TPB17FloatingDecimal64* _M0L1xS584 = _M0L4_2afS583;
    while (1) {
      uint64_t _M0L8mantissaS1622 = _M0L1xS584->$0;
      uint64_t _M0L1qS585 = _M0L8mantissaS1622 / 10ull;
      uint64_t _M0L8mantissaS1620 = _M0L1xS584->$0;
      uint64_t _M0L6_2atmpS1621 = 10ull * _M0L1qS585;
      uint64_t _M0L1rS586 = _M0L8mantissaS1620 - _M0L6_2atmpS1621;
      int32_t _M0L8exponentS1619;
      int32_t _M0L6_2atmpS1618;
      struct _M0TPB17FloatingDecimal64* _M0L6_2atmpS1617;
      if (_M0L1rS586 != 0ull) {
        _M0L1vS581 = _M0L1xS584;
        break;
      }
      _M0L8exponentS1619 = _M0L1xS584->$1;
      moonbit_decref(_M0L1xS584);
      _M0L6_2atmpS1618 = _M0L8exponentS1619 + 1;
      _M0L6_2atmpS1617
      = (struct _M0TPB17FloatingDecimal64*)moonbit_malloc(sizeof(struct _M0TPB17FloatingDecimal64));
      Moonbit_object_header(_M0L6_2atmpS1617)->meta
      = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
      _M0L6_2atmpS1617->$0 = _M0L1qS585;
      _M0L6_2atmpS1617->$1 = _M0L6_2atmpS1618;
      _M0L1xS584 = _M0L6_2atmpS1617;
      continue;
      break;
    }
  }
  #line 721 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _result_2101 = _M0FPB9to__chars(_M0L1vS581, _M0L8ieeeSignS577);
  moonbit_decref(_M0L1vS581);
  return _result_2101;
}

struct _M0TPB17FloatingDecimal64* _M0FPB15d2d__small__int(
  uint64_t _M0L12ieeeMantissaS568,
  int32_t _M0L12ieeeExponentS570
) {
  uint64_t _M0L2m2S567;
  int32_t _M0L6_2atmpS1611;
  int32_t _M0L2e2S569;
  int32_t _M0L6_2atmpS1610;
  uint64_t _M0L6_2atmpS1609;
  uint64_t _M0L4maskS571;
  uint64_t _M0L8fractionS572;
  int32_t _M0L6_2atmpS1608;
  uint64_t _M0L6_2atmpS1607;
  struct _M0TPB17FloatingDecimal64* _M0L6_2atmpS1606;
  #line 637 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L2m2S567 = 4503599627370496ull | _M0L12ieeeMantissaS568;
  _M0L6_2atmpS1611 = _M0L12ieeeExponentS570 - 1023;
  _M0L2e2S569 = _M0L6_2atmpS1611 - 52;
  if (_M0L2e2S569 > 0) {
    return 0;
  }
  if (_M0L2e2S569 < -52) {
    return 0;
  }
  _M0L6_2atmpS1610 = -_M0L2e2S569;
  _M0L6_2atmpS1609 = 1ull << (_M0L6_2atmpS1610 & 63);
  _M0L4maskS571 = _M0L6_2atmpS1609 - 1ull;
  _M0L8fractionS572 = _M0L2m2S567 & _M0L4maskS571;
  if (_M0L8fractionS572 != 0ull) {
    return 0;
  }
  _M0L6_2atmpS1608 = -_M0L2e2S569;
  _M0L6_2atmpS1607 = _M0L2m2S567 >> (_M0L6_2atmpS1608 & 63);
  _M0L6_2atmpS1606
  = (struct _M0TPB17FloatingDecimal64*)moonbit_malloc(sizeof(struct _M0TPB17FloatingDecimal64));
  Moonbit_object_header(_M0L6_2atmpS1606)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L6_2atmpS1606->$0 = _M0L6_2atmpS1607;
  _M0L6_2atmpS1606->$1 = 0;
  return _M0L6_2atmpS1606;
}

moonbit_string_t _M0FPB9to__chars(
  struct _M0TPB17FloatingDecimal64* _M0L1vS535,
  int32_t _M0L4signS533
) {
  moonbit_bytes_t _M0L6resultS531;
  int32_t _M0Lm5indexS532;
  uint64_t _M0L6outputS534;
  int32_t _M0L7olengthS536;
  int32_t _M0L8exponentS1605;
  int32_t _M0L6_2atmpS1604;
  int32_t _M0Lm3expS537;
  int32_t _M0L6_2atmpS1603;
  int32_t _M0L6_2atmpS1601;
  int32_t _M0L18scientificNotationS538;
  #line 530 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6resultS531 = (moonbit_bytes_t)moonbit_make_bytes(25, 0);
  _M0Lm5indexS532 = 0;
  if (_M0L4signS533) {
    int32_t _M0L6_2atmpS1475 = _M0Lm5indexS532;
    int32_t _M0L6_2atmpS1476;
    if (
      _M0L6_2atmpS1475 < 0
      || _M0L6_2atmpS1475 >= Moonbit_array_length(_M0L6resultS531)
    ) {
      #line 535 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      moonbit_panic();
    }
    _M0L6resultS531[_M0L6_2atmpS1475] = 45;
    _M0L6_2atmpS1476 = _M0Lm5indexS532;
    _M0Lm5indexS532 = _M0L6_2atmpS1476 + 1;
  }
  _M0L6outputS534 = _M0L1vS535->$0;
  #line 539 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7olengthS536 = _M0FPB17decimal__length17(_M0L6outputS534);
  _M0L8exponentS1605 = _M0L1vS535->$1;
  _M0L6_2atmpS1604 = _M0L8exponentS1605 + _M0L7olengthS536;
  _M0Lm3expS537 = _M0L6_2atmpS1604 - 1;
  _M0L6_2atmpS1603 = _M0Lm3expS537;
  if (_M0L6_2atmpS1603 >= -6) {
    int32_t _M0L6_2atmpS1602 = _M0Lm3expS537;
    _M0L6_2atmpS1601 = _M0L6_2atmpS1602 < 21;
  } else {
    _M0L6_2atmpS1601 = 0;
  }
  _M0L18scientificNotationS538 = !_M0L6_2atmpS1601;
  if (_M0L18scientificNotationS538) {
    int32_t _M0L7_2abindS539 = _M0L7olengthS536 - 1;
    uint64_t _M0L6outputS540;
    int32_t _M0L1iS541 = 0;
    uint64_t _M0L6outputS542 = _M0L6outputS534;
    int32_t _M0L6_2atmpS1477;
    int32_t _M0L6_2atmpS1481;
    int32_t _M0L6_2atmpS1480;
    int32_t _M0L6_2atmpS1479;
    int32_t _M0L6_2atmpS1478;
    int32_t _M0L6_2atmpS1485;
    int32_t _M0L6_2atmpS1486;
    int32_t _M0L6_2atmpS1487;
    int32_t _M0L6_2atmpS1488;
    int32_t _M0L6_2atmpS1489;
    int32_t _M0L6_2atmpS1495;
    int32_t _M0L6_2atmpS1528;
    moonbit_string_t _result_2103;
    while (1) {
      if (_M0L1iS541 < _M0L7_2abindS539) {
        uint64_t _M0L1cS543 = _M0L6outputS542 % 10ull;
        int32_t _M0L6_2atmpS1534 = _M0Lm5indexS532;
        int32_t _M0L6_2atmpS1533 = _M0L6_2atmpS1534 + _M0L7olengthS536;
        int32_t _M0L6_2atmpS1529 = _M0L6_2atmpS1533 - _M0L1iS541;
        int32_t _M0L6_2atmpS1532 = (int32_t)_M0L1cS543;
        int32_t _M0L6_2atmpS1531 = 48 + _M0L6_2atmpS1532;
        int32_t _M0L6_2atmpS1530 = _M0L6_2atmpS1531 & 0xff;
        int32_t _M0L6_2atmpS1535;
        uint64_t _M0L6_2atmpS1536;
        if (
          _M0L6_2atmpS1529 < 0
          || _M0L6_2atmpS1529 >= Moonbit_array_length(_M0L6resultS531)
        ) {
          #line 547 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
          moonbit_panic();
        }
        _M0L6resultS531[_M0L6_2atmpS1529] = _M0L6_2atmpS1530;
        _M0L6_2atmpS1535 = _M0L1iS541 + 1;
        _M0L6_2atmpS1536 = _M0L6outputS542 / 10ull;
        _M0L1iS541 = _M0L6_2atmpS1535;
        _M0L6outputS542 = _M0L6_2atmpS1536;
        continue;
      } else {
        _M0L6outputS540 = _M0L6outputS542;
      }
      break;
    }
    _M0L6_2atmpS1477 = _M0Lm5indexS532;
    _M0L6_2atmpS1481 = (int32_t)_M0L6outputS540;
    _M0L6_2atmpS1480 = _M0L6_2atmpS1481 % 10;
    _M0L6_2atmpS1479 = 48 + _M0L6_2atmpS1480;
    _M0L6_2atmpS1478 = _M0L6_2atmpS1479 & 0xff;
    if (
      _M0L6_2atmpS1477 < 0
      || _M0L6_2atmpS1477 >= Moonbit_array_length(_M0L6resultS531)
    ) {
      #line 552 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      moonbit_panic();
    }
    _M0L6resultS531[_M0L6_2atmpS1477] = _M0L6_2atmpS1478;
    if (_M0L7olengthS536 > 1) {
      int32_t _M0L6_2atmpS1483 = _M0Lm5indexS532;
      int32_t _M0L6_2atmpS1482 = _M0L6_2atmpS1483 + 1;
      if (
        _M0L6_2atmpS1482 < 0
        || _M0L6_2atmpS1482 >= Moonbit_array_length(_M0L6resultS531)
      ) {
        #line 554 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS531[_M0L6_2atmpS1482] = 46;
    } else {
      int32_t _M0L6_2atmpS1484 = _M0Lm5indexS532;
      _M0Lm5indexS532 = _M0L6_2atmpS1484 - 1;
    }
    _M0L6_2atmpS1485 = _M0Lm5indexS532;
    _M0L6_2atmpS1486 = _M0L7olengthS536 + 1;
    _M0Lm5indexS532 = _M0L6_2atmpS1485 + _M0L6_2atmpS1486;
    _M0L6_2atmpS1487 = _M0Lm5indexS532;
    if (
      _M0L6_2atmpS1487 < 0
      || _M0L6_2atmpS1487 >= Moonbit_array_length(_M0L6resultS531)
    ) {
      #line 562 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      moonbit_panic();
    }
    _M0L6resultS531[_M0L6_2atmpS1487] = 101;
    _M0L6_2atmpS1488 = _M0Lm5indexS532;
    _M0Lm5indexS532 = _M0L6_2atmpS1488 + 1;
    _M0L6_2atmpS1489 = _M0Lm3expS537;
    if (_M0L6_2atmpS1489 < 0) {
      int32_t _M0L6_2atmpS1490 = _M0Lm5indexS532;
      int32_t _M0L6_2atmpS1491;
      int32_t _M0L6_2atmpS1492;
      if (
        _M0L6_2atmpS1490 < 0
        || _M0L6_2atmpS1490 >= Moonbit_array_length(_M0L6resultS531)
      ) {
        #line 565 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS531[_M0L6_2atmpS1490] = 45;
      _M0L6_2atmpS1491 = _M0Lm5indexS532;
      _M0Lm5indexS532 = _M0L6_2atmpS1491 + 1;
      _M0L6_2atmpS1492 = _M0Lm3expS537;
      _M0Lm3expS537 = -_M0L6_2atmpS1492;
    } else {
      int32_t _M0L6_2atmpS1493 = _M0Lm5indexS532;
      int32_t _M0L6_2atmpS1494;
      if (
        _M0L6_2atmpS1493 < 0
        || _M0L6_2atmpS1493 >= Moonbit_array_length(_M0L6resultS531)
      ) {
        #line 569 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS531[_M0L6_2atmpS1493] = 43;
      _M0L6_2atmpS1494 = _M0Lm5indexS532;
      _M0Lm5indexS532 = _M0L6_2atmpS1494 + 1;
    }
    _M0L6_2atmpS1495 = _M0Lm3expS537;
    if (_M0L6_2atmpS1495 >= 100) {
      int32_t _M0L6_2atmpS1511 = _M0Lm3expS537;
      int32_t _M0L1aS545 = _M0L6_2atmpS1511 / 100;
      int32_t _M0L6_2atmpS1510 = _M0Lm3expS537;
      int32_t _M0L6_2atmpS1509 = _M0L6_2atmpS1510 / 10;
      int32_t _M0L1bS546 = _M0L6_2atmpS1509 % 10;
      int32_t _M0L6_2atmpS1508 = _M0Lm3expS537;
      int32_t _M0L1cS547 = _M0L6_2atmpS1508 % 10;
      int32_t _M0L6_2atmpS1496 = _M0Lm5indexS532;
      int32_t _M0L6_2atmpS1498 = 48 + _M0L1aS545;
      int32_t _M0L6_2atmpS1497 = _M0L6_2atmpS1498 & 0xff;
      int32_t _M0L6_2atmpS1502;
      int32_t _M0L6_2atmpS1499;
      int32_t _M0L6_2atmpS1501;
      int32_t _M0L6_2atmpS1500;
      int32_t _M0L6_2atmpS1506;
      int32_t _M0L6_2atmpS1503;
      int32_t _M0L6_2atmpS1505;
      int32_t _M0L6_2atmpS1504;
      int32_t _M0L6_2atmpS1507;
      if (
        _M0L6_2atmpS1496 < 0
        || _M0L6_2atmpS1496 >= Moonbit_array_length(_M0L6resultS531)
      ) {
        #line 576 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS531[_M0L6_2atmpS1496] = _M0L6_2atmpS1497;
      _M0L6_2atmpS1502 = _M0Lm5indexS532;
      _M0L6_2atmpS1499 = _M0L6_2atmpS1502 + 1;
      _M0L6_2atmpS1501 = 48 + _M0L1bS546;
      _M0L6_2atmpS1500 = _M0L6_2atmpS1501 & 0xff;
      if (
        _M0L6_2atmpS1499 < 0
        || _M0L6_2atmpS1499 >= Moonbit_array_length(_M0L6resultS531)
      ) {
        #line 577 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS531[_M0L6_2atmpS1499] = _M0L6_2atmpS1500;
      _M0L6_2atmpS1506 = _M0Lm5indexS532;
      _M0L6_2atmpS1503 = _M0L6_2atmpS1506 + 2;
      _M0L6_2atmpS1505 = 48 + _M0L1cS547;
      _M0L6_2atmpS1504 = _M0L6_2atmpS1505 & 0xff;
      if (
        _M0L6_2atmpS1503 < 0
        || _M0L6_2atmpS1503 >= Moonbit_array_length(_M0L6resultS531)
      ) {
        #line 578 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS531[_M0L6_2atmpS1503] = _M0L6_2atmpS1504;
      _M0L6_2atmpS1507 = _M0Lm5indexS532;
      _M0Lm5indexS532 = _M0L6_2atmpS1507 + 3;
    } else {
      int32_t _M0L6_2atmpS1512 = _M0Lm3expS537;
      if (_M0L6_2atmpS1512 >= 10) {
        int32_t _M0L6_2atmpS1522 = _M0Lm3expS537;
        int32_t _M0L1aS548 = _M0L6_2atmpS1522 / 10;
        int32_t _M0L6_2atmpS1521 = _M0Lm3expS537;
        int32_t _M0L1bS549 = _M0L6_2atmpS1521 % 10;
        int32_t _M0L6_2atmpS1513 = _M0Lm5indexS532;
        int32_t _M0L6_2atmpS1515 = 48 + _M0L1aS548;
        int32_t _M0L6_2atmpS1514 = _M0L6_2atmpS1515 & 0xff;
        int32_t _M0L6_2atmpS1519;
        int32_t _M0L6_2atmpS1516;
        int32_t _M0L6_2atmpS1518;
        int32_t _M0L6_2atmpS1517;
        int32_t _M0L6_2atmpS1520;
        if (
          _M0L6_2atmpS1513 < 0
          || _M0L6_2atmpS1513 >= Moonbit_array_length(_M0L6resultS531)
        ) {
          #line 583 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
          moonbit_panic();
        }
        _M0L6resultS531[_M0L6_2atmpS1513] = _M0L6_2atmpS1514;
        _M0L6_2atmpS1519 = _M0Lm5indexS532;
        _M0L6_2atmpS1516 = _M0L6_2atmpS1519 + 1;
        _M0L6_2atmpS1518 = 48 + _M0L1bS549;
        _M0L6_2atmpS1517 = _M0L6_2atmpS1518 & 0xff;
        if (
          _M0L6_2atmpS1516 < 0
          || _M0L6_2atmpS1516 >= Moonbit_array_length(_M0L6resultS531)
        ) {
          #line 584 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
          moonbit_panic();
        }
        _M0L6resultS531[_M0L6_2atmpS1516] = _M0L6_2atmpS1517;
        _M0L6_2atmpS1520 = _M0Lm5indexS532;
        _M0Lm5indexS532 = _M0L6_2atmpS1520 + 2;
      } else {
        int32_t _M0L6_2atmpS1523 = _M0Lm5indexS532;
        int32_t _M0L6_2atmpS1526 = _M0Lm3expS537;
        int32_t _M0L6_2atmpS1525 = 48 + _M0L6_2atmpS1526;
        int32_t _M0L6_2atmpS1524 = _M0L6_2atmpS1525 & 0xff;
        int32_t _M0L6_2atmpS1527;
        if (
          _M0L6_2atmpS1523 < 0
          || _M0L6_2atmpS1523 >= Moonbit_array_length(_M0L6resultS531)
        ) {
          #line 587 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
          moonbit_panic();
        }
        _M0L6resultS531[_M0L6_2atmpS1523] = _M0L6_2atmpS1524;
        _M0L6_2atmpS1527 = _M0Lm5indexS532;
        _M0Lm5indexS532 = _M0L6_2atmpS1527 + 1;
      }
    }
    _M0L6_2atmpS1528 = _M0Lm5indexS532;
    #line 590 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _result_2103
    = _M0FPB19string__from__bytes(_M0L6resultS531, 0, _M0L6_2atmpS1528);
    moonbit_decref(_M0L6resultS531);
    return _result_2103;
  } else {
    int32_t _M0L6_2atmpS1537 = _M0Lm3expS537;
    int32_t _M0L6_2atmpS1600;
    moonbit_string_t _result_2109;
    if (_M0L6_2atmpS1537 < 0) {
      int32_t _M0L6_2atmpS1538 = _M0Lm5indexS532;
      int32_t _M0L6_2atmpS1540;
      int32_t _M0L6_2atmpS1539;
      int32_t _M0L6_2atmpS1541;
      int32_t _M0L1iS550;
      int32_t _M0L6_2atmpS1556;
      int32_t _M0L6_2atmpS1558;
      int32_t _M0L6_2atmpS1557;
      int32_t _M0L7currentS552;
      int32_t _M0L1iS553;
      uint64_t _M0L6outputS554;
      if (
        _M0L6_2atmpS1538 < 0
        || _M0L6_2atmpS1538 >= Moonbit_array_length(_M0L6resultS531)
      ) {
        #line 595 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS531[_M0L6_2atmpS1538] = 48;
      _M0L6_2atmpS1540 = _M0Lm5indexS532;
      _M0L6_2atmpS1539 = _M0L6_2atmpS1540 + 1;
      if (
        _M0L6_2atmpS1539 < 0
        || _M0L6_2atmpS1539 >= Moonbit_array_length(_M0L6resultS531)
      ) {
        #line 596 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS531[_M0L6_2atmpS1539] = 46;
      _M0L6_2atmpS1541 = _M0Lm5indexS532;
      _M0Lm5indexS532 = _M0L6_2atmpS1541 + 2;
      _M0L1iS550 = -1;
      while (1) {
        int32_t _M0L6_2atmpS1542 = _M0Lm3expS537;
        if (_M0L1iS550 > _M0L6_2atmpS1542) {
          int32_t _M0L6_2atmpS1545 = _M0Lm5indexS532;
          int32_t _M0L6_2atmpS1544 = _M0L6_2atmpS1545 - _M0L1iS550;
          int32_t _M0L6_2atmpS1543 = _M0L6_2atmpS1544 - 1;
          int32_t _M0L6_2atmpS1546;
          if (
            _M0L6_2atmpS1543 < 0
            || _M0L6_2atmpS1543 >= Moonbit_array_length(_M0L6resultS531)
          ) {
            #line 599 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
            moonbit_panic();
          }
          _M0L6resultS531[_M0L6_2atmpS1543] = 48;
          _M0L6_2atmpS1546 = _M0L1iS550 - 1;
          _M0L1iS550 = _M0L6_2atmpS1546;
          continue;
        }
        break;
      }
      _M0L6_2atmpS1556 = _M0Lm5indexS532;
      _M0L6_2atmpS1558 = _M0Lm3expS537;
      _M0L6_2atmpS1557 = -1 - _M0L6_2atmpS1558;
      _M0L7currentS552 = _M0L6_2atmpS1556 + _M0L6_2atmpS1557;
      _M0L1iS553 = 0;
      _M0L6outputS554 = _M0L6outputS534;
      while (1) {
        if (_M0L1iS553 < _M0L7olengthS536) {
          int32_t _M0L6_2atmpS1553 = _M0L7currentS552 + _M0L7olengthS536;
          int32_t _M0L6_2atmpS1552 = _M0L6_2atmpS1553 - _M0L1iS553;
          int32_t _M0L6_2atmpS1547 = _M0L6_2atmpS1552 - 1;
          uint64_t _M0L6_2atmpS1551 = _M0L6outputS554 % 10ull;
          int32_t _M0L6_2atmpS1550 = (int32_t)_M0L6_2atmpS1551;
          int32_t _M0L6_2atmpS1549 = 48 + _M0L6_2atmpS1550;
          int32_t _M0L6_2atmpS1548 = _M0L6_2atmpS1549 & 0xff;
          int32_t _M0L6_2atmpS1554;
          uint64_t _M0L6_2atmpS1555;
          if (
            _M0L6_2atmpS1547 < 0
            || _M0L6_2atmpS1547 >= Moonbit_array_length(_M0L6resultS531)
          ) {
            #line 603 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
            moonbit_panic();
          }
          _M0L6resultS531[_M0L6_2atmpS1547] = _M0L6_2atmpS1548;
          _M0L6_2atmpS1554 = _M0L1iS553 + 1;
          _M0L6_2atmpS1555 = _M0L6outputS554 / 10ull;
          _M0L1iS553 = _M0L6_2atmpS1554;
          _M0L6outputS554 = _M0L6_2atmpS1555;
          continue;
        }
        break;
      }
      _M0Lm5indexS532 = _M0L7currentS552 + _M0L7olengthS536;
    } else {
      int32_t _M0L6_2atmpS1560 = _M0Lm3expS537;
      int32_t _M0L6_2atmpS1559 = _M0L6_2atmpS1560 + 1;
      if (_M0L6_2atmpS1559 >= _M0L7olengthS536) {
        int32_t _M0L1iS556 = 0;
        uint64_t _M0L6outputS557 = _M0L6outputS534;
        int32_t _M0L6_2atmpS1571;
        int32_t _M0L6_2atmpS1576;
        int32_t _M0L7_2abindS559;
        int32_t _M0L1iS560;
        int32_t _M0L6_2atmpS1577;
        int32_t _M0L6_2atmpS1580;
        int32_t _M0L6_2atmpS1579;
        int32_t _M0L6_2atmpS1578;
        while (1) {
          if (_M0L1iS556 < _M0L7olengthS536) {
            int32_t _M0L6_2atmpS1568 = _M0Lm5indexS532;
            int32_t _M0L6_2atmpS1567 = _M0L6_2atmpS1568 + _M0L7olengthS536;
            int32_t _M0L6_2atmpS1566 = _M0L6_2atmpS1567 - _M0L1iS556;
            int32_t _M0L6_2atmpS1561 = _M0L6_2atmpS1566 - 1;
            uint64_t _M0L6_2atmpS1565 = _M0L6outputS557 % 10ull;
            int32_t _M0L6_2atmpS1564 = (int32_t)_M0L6_2atmpS1565;
            int32_t _M0L6_2atmpS1563 = 48 + _M0L6_2atmpS1564;
            int32_t _M0L6_2atmpS1562 = _M0L6_2atmpS1563 & 0xff;
            int32_t _M0L6_2atmpS1569;
            uint64_t _M0L6_2atmpS1570;
            if (
              _M0L6_2atmpS1561 < 0
              || _M0L6_2atmpS1561 >= Moonbit_array_length(_M0L6resultS531)
            ) {
              #line 610 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
              moonbit_panic();
            }
            _M0L6resultS531[_M0L6_2atmpS1561] = _M0L6_2atmpS1562;
            _M0L6_2atmpS1569 = _M0L1iS556 + 1;
            _M0L6_2atmpS1570 = _M0L6outputS557 / 10ull;
            _M0L1iS556 = _M0L6_2atmpS1569;
            _M0L6outputS557 = _M0L6_2atmpS1570;
            continue;
          }
          break;
        }
        _M0L6_2atmpS1571 = _M0Lm5indexS532;
        _M0Lm5indexS532 = _M0L6_2atmpS1571 + _M0L7olengthS536;
        _M0L6_2atmpS1576 = _M0Lm3expS537;
        _M0L7_2abindS559 = _M0L6_2atmpS1576 + 1;
        _M0L1iS560 = _M0L7olengthS536;
        while (1) {
          if (_M0L1iS560 < _M0L7_2abindS559) {
            int32_t _M0L6_2atmpS1574 = _M0Lm5indexS532;
            int32_t _M0L6_2atmpS1573 = _M0L6_2atmpS1574 + _M0L1iS560;
            int32_t _M0L6_2atmpS1572 = _M0L6_2atmpS1573 - _M0L7olengthS536;
            int32_t _M0L6_2atmpS1575;
            if (
              _M0L6_2atmpS1572 < 0
              || _M0L6_2atmpS1572 >= Moonbit_array_length(_M0L6resultS531)
            ) {
              #line 615 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
              moonbit_panic();
            }
            _M0L6resultS531[_M0L6_2atmpS1572] = 48;
            _M0L6_2atmpS1575 = _M0L1iS560 + 1;
            _M0L1iS560 = _M0L6_2atmpS1575;
            continue;
          }
          break;
        }
        _M0L6_2atmpS1577 = _M0Lm5indexS532;
        _M0L6_2atmpS1580 = _M0Lm3expS537;
        _M0L6_2atmpS1579 = _M0L6_2atmpS1580 + 1;
        _M0L6_2atmpS1578 = _M0L6_2atmpS1579 - _M0L7olengthS536;
        _M0Lm5indexS532 = _M0L6_2atmpS1577 + _M0L6_2atmpS1578;
      } else {
        int32_t _M0L6_2atmpS1597 = _M0Lm5indexS532;
        int32_t _M0L6_2atmpS1596 = _M0L6_2atmpS1597 + 1;
        int32_t _M0L1iS562 = 0;
        int32_t _M0L7currentS563 = _M0L6_2atmpS1596;
        uint64_t _M0L6outputS564 = _M0L6outputS534;
        int32_t _M0L6_2atmpS1598;
        int32_t _M0L6_2atmpS1599;
        while (1) {
          if (_M0L1iS562 < _M0L7olengthS536) {
            int32_t _M0L6_2atmpS1592 = _M0L7olengthS536 - _M0L1iS562;
            int32_t _M0L6_2atmpS1590 = _M0L6_2atmpS1592 - 1;
            int32_t _M0L6_2atmpS1591 = _M0Lm3expS537;
            int32_t _M0L7currentS565;
            int32_t _M0L6_2atmpS1587;
            int32_t _M0L6_2atmpS1586;
            int32_t _M0L6_2atmpS1581;
            uint64_t _M0L6_2atmpS1585;
            int32_t _M0L6_2atmpS1584;
            int32_t _M0L6_2atmpS1583;
            int32_t _M0L6_2atmpS1582;
            int32_t _M0L6_2atmpS1588;
            uint64_t _M0L6_2atmpS1589;
            if (_M0L6_2atmpS1590 == _M0L6_2atmpS1591) {
              int32_t _M0L6_2atmpS1595 = _M0L7currentS563 + _M0L7olengthS536;
              int32_t _M0L6_2atmpS1594 = _M0L6_2atmpS1595 - _M0L1iS562;
              int32_t _M0L6_2atmpS1593 = _M0L6_2atmpS1594 - 1;
              if (
                _M0L6_2atmpS1593 < 0
                || _M0L6_2atmpS1593 >= Moonbit_array_length(_M0L6resultS531)
              ) {
                #line 622 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
                moonbit_panic();
              }
              _M0L6resultS531[_M0L6_2atmpS1593] = 46;
              _M0L7currentS565 = _M0L7currentS563 - 1;
            } else {
              _M0L7currentS565 = _M0L7currentS563;
            }
            _M0L6_2atmpS1587 = _M0L7currentS565 + _M0L7olengthS536;
            _M0L6_2atmpS1586 = _M0L6_2atmpS1587 - _M0L1iS562;
            _M0L6_2atmpS1581 = _M0L6_2atmpS1586 - 1;
            _M0L6_2atmpS1585 = _M0L6outputS564 % 10ull;
            _M0L6_2atmpS1584 = (int32_t)_M0L6_2atmpS1585;
            _M0L6_2atmpS1583 = 48 + _M0L6_2atmpS1584;
            _M0L6_2atmpS1582 = _M0L6_2atmpS1583 & 0xff;
            if (
              _M0L6_2atmpS1581 < 0
              || _M0L6_2atmpS1581 >= Moonbit_array_length(_M0L6resultS531)
            ) {
              #line 627 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
              moonbit_panic();
            }
            _M0L6resultS531[_M0L6_2atmpS1581] = _M0L6_2atmpS1582;
            _M0L6_2atmpS1588 = _M0L1iS562 + 1;
            _M0L6_2atmpS1589 = _M0L6outputS564 / 10ull;
            _M0L1iS562 = _M0L6_2atmpS1588;
            _M0L7currentS563 = _M0L7currentS565;
            _M0L6outputS564 = _M0L6_2atmpS1589;
            continue;
          }
          break;
        }
        _M0L6_2atmpS1598 = _M0Lm5indexS532;
        _M0L6_2atmpS1599 = _M0L7olengthS536 + 1;
        _M0Lm5indexS532 = _M0L6_2atmpS1598 + _M0L6_2atmpS1599;
      }
    }
    _M0L6_2atmpS1600 = _M0Lm5indexS532;
    #line 632 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _result_2109
    = _M0FPB19string__from__bytes(_M0L6resultS531, 0, _M0L6_2atmpS1600);
    moonbit_decref(_M0L6resultS531);
    return _result_2109;
  }
}

struct _M0TPB17FloatingDecimal64* _M0FPB3d2d(
  uint64_t _M0L12ieeeMantissaS477,
  uint32_t _M0L12ieeeExponentS476
) {
  int32_t _M0Lm2e2S474;
  uint64_t _M0Lm2m2S475;
  uint64_t _M0L6_2atmpS1474;
  uint64_t _M0L6_2atmpS1473;
  int32_t _M0L4evenS478;
  uint64_t _M0L6_2atmpS1472;
  uint64_t _M0L2mvS479;
  int32_t _M0L7mmShiftS480;
  uint64_t _M0Lm2vrS481;
  uint64_t _M0Lm2vpS482;
  uint64_t _M0Lm2vmS483;
  int32_t _M0Lm3e10S484;
  int32_t _M0Lm17vmIsTrailingZerosS485;
  int32_t _M0Lm17vrIsTrailingZerosS486;
  int32_t _M0L6_2atmpS1374;
  int32_t _M0Lm7removedS505;
  int32_t _M0Lm16lastRemovedDigitS506;
  uint64_t _M0Lm6outputS507;
  int32_t _M0L6_2atmpS1470;
  int32_t _M0L6_2atmpS1471;
  int32_t _M0L3expS530;
  uint64_t _M0L6_2atmpS1469;
  struct _M0TPB17FloatingDecimal64* _block_2115;
  #line 347 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0Lm2e2S474 = 0;
  _M0Lm2m2S475 = 0ull;
  if (_M0L12ieeeExponentS476 == 0u) {
    _M0Lm2e2S474 = -1076;
    _M0Lm2m2S475 = _M0L12ieeeMantissaS477;
  } else {
    int32_t _M0L6_2atmpS1373 = *(int32_t*)&_M0L12ieeeExponentS476;
    int32_t _M0L6_2atmpS1372 = _M0L6_2atmpS1373 - 1023;
    int32_t _M0L6_2atmpS1371 = _M0L6_2atmpS1372 - 52;
    _M0Lm2e2S474 = _M0L6_2atmpS1371 - 2;
    _M0Lm2m2S475 = 4503599627370496ull | _M0L12ieeeMantissaS477;
  }
  _M0L6_2atmpS1474 = _M0Lm2m2S475;
  _M0L6_2atmpS1473 = _M0L6_2atmpS1474 & 1ull;
  _M0L4evenS478 = _M0L6_2atmpS1473 == 0ull;
  _M0L6_2atmpS1472 = _M0Lm2m2S475;
  _M0L2mvS479 = 4ull * _M0L6_2atmpS1472;
  _M0L7mmShiftS480
  = _M0L12ieeeMantissaS477 != 0ull || _M0L12ieeeExponentS476 <= 1u;
  _M0Lm2vrS481 = 0ull;
  _M0Lm2vpS482 = 0ull;
  _M0Lm2vmS483 = 0ull;
  _M0Lm3e10S484 = 0;
  _M0Lm17vmIsTrailingZerosS485 = 0;
  _M0Lm17vrIsTrailingZerosS486 = 0;
  _M0L6_2atmpS1374 = _M0Lm2e2S474;
  if (_M0L6_2atmpS1374 >= 0) {
    int32_t _M0L6_2atmpS1396 = _M0Lm2e2S474;
    int32_t _M0L6_2atmpS1392;
    int32_t _M0L6_2atmpS1395;
    int32_t _M0L6_2atmpS1394;
    int32_t _M0L6_2atmpS1393;
    int32_t _M0L1qS487;
    int32_t _M0L6_2atmpS1391;
    int32_t _M0L6_2atmpS1390;
    int32_t _M0L1kS488;
    int32_t _M0L6_2atmpS1389;
    int32_t _M0L6_2atmpS1388;
    int32_t _M0L6_2atmpS1387;
    int32_t _M0L1iS489;
    struct _M0TPB8Pow5Pair _M0L4pow5S490;
    uint64_t _M0L6_2atmpS1386;
    struct _M0TPB19MulShiftAll64Result _M0L7_2abindS491;
    uint64_t _M0L8_2avrOutS492;
    uint64_t _M0L8_2avpOutS493;
    uint64_t _M0L8_2avmOutS494;
    #line 383 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1392 = _M0FPB9log10Pow2(_M0L6_2atmpS1396);
    _M0L6_2atmpS1395 = _M0Lm2e2S474;
    _M0L6_2atmpS1394 = _M0L6_2atmpS1395 > 3;
    #line 383 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1393 = _M0MPC14bool4Bool7to__int(_M0L6_2atmpS1394);
    _M0L1qS487 = _M0L6_2atmpS1392 - _M0L6_2atmpS1393;
    _M0Lm3e10S484 = _M0L1qS487;
    #line 385 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1391 = _M0FPB8pow5bits(_M0L1qS487);
    _M0L6_2atmpS1390 = 125 + _M0L6_2atmpS1391;
    _M0L1kS488 = _M0L6_2atmpS1390 - 1;
    _M0L6_2atmpS1389 = _M0Lm2e2S474;
    _M0L6_2atmpS1388 = -_M0L6_2atmpS1389;
    _M0L6_2atmpS1387 = _M0L6_2atmpS1388 + _M0L1qS487;
    _M0L1iS489 = _M0L6_2atmpS1387 + _M0L1kS488;
    #line 387 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L4pow5S490 = _M0FPB22double__computeInvPow5(_M0L1qS487);
    _M0L6_2atmpS1386 = _M0Lm2m2S475;
    #line 388 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L7_2abindS491
    = _M0FPB13mulShiftAll64(_M0L6_2atmpS1386, _M0L4pow5S490, _M0L1iS489, _M0L7mmShiftS480);
    _M0L8_2avrOutS492 = _M0L7_2abindS491.$0;
    _M0L8_2avpOutS493 = _M0L7_2abindS491.$1;
    _M0L8_2avmOutS494 = _M0L7_2abindS491.$2;
    _M0Lm2vrS481 = _M0L8_2avrOutS492;
    _M0Lm2vpS482 = _M0L8_2avpOutS493;
    _M0Lm2vmS483 = _M0L8_2avmOutS494;
    if (_M0L1qS487 <= 21) {
      int32_t _M0L6_2atmpS1382 = (int32_t)_M0L2mvS479;
      uint64_t _M0L6_2atmpS1385 = _M0L2mvS479 / 5ull;
      int32_t _M0L6_2atmpS1384 = (int32_t)_M0L6_2atmpS1385;
      int32_t _M0L6_2atmpS1383 = 5 * _M0L6_2atmpS1384;
      int32_t _M0L6mvMod5S495 = _M0L6_2atmpS1382 - _M0L6_2atmpS1383;
      if (_M0L6mvMod5S495 == 0) {
        #line 400 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0Lm17vrIsTrailingZerosS486
        = _M0FPB18multipleOfPowerOf5(_M0L2mvS479, _M0L1qS487);
      } else if (_M0L4evenS478) {
        uint64_t _M0L6_2atmpS1376 = _M0L2mvS479 - 1ull;
        uint64_t _M0L6_2atmpS1377;
        uint64_t _M0L6_2atmpS1375;
        #line 406 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0L6_2atmpS1377 = _M0MPC14bool4Bool10to__uint64(_M0L7mmShiftS480);
        _M0L6_2atmpS1375 = _M0L6_2atmpS1376 - _M0L6_2atmpS1377;
        #line 405 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0Lm17vmIsTrailingZerosS485
        = _M0FPB18multipleOfPowerOf5(_M0L6_2atmpS1375, _M0L1qS487);
      } else {
        uint64_t _M0L6_2atmpS1378 = _M0Lm2vpS482;
        uint64_t _M0L6_2atmpS1381 = _M0L2mvS479 + 2ull;
        int32_t _M0L6_2atmpS1380;
        uint64_t _M0L6_2atmpS1379;
        #line 410 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0L6_2atmpS1380
        = _M0FPB18multipleOfPowerOf5(_M0L6_2atmpS1381, _M0L1qS487);
        #line 410 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0L6_2atmpS1379 = _M0MPC14bool4Bool10to__uint64(_M0L6_2atmpS1380);
        _M0Lm2vpS482 = _M0L6_2atmpS1378 - _M0L6_2atmpS1379;
      }
    }
  } else {
    int32_t _M0L6_2atmpS1410 = _M0Lm2e2S474;
    int32_t _M0L6_2atmpS1409 = -_M0L6_2atmpS1410;
    int32_t _M0L6_2atmpS1404;
    int32_t _M0L6_2atmpS1408;
    int32_t _M0L6_2atmpS1407;
    int32_t _M0L6_2atmpS1406;
    int32_t _M0L6_2atmpS1405;
    int32_t _M0L1qS496;
    int32_t _M0L6_2atmpS1397;
    int32_t _M0L6_2atmpS1403;
    int32_t _M0L6_2atmpS1402;
    int32_t _M0L1iS497;
    int32_t _M0L6_2atmpS1401;
    int32_t _M0L1kS498;
    int32_t _M0L1jS499;
    struct _M0TPB8Pow5Pair _M0L4pow5S500;
    uint64_t _M0L6_2atmpS1400;
    struct _M0TPB19MulShiftAll64Result _M0L7_2abindS501;
    uint64_t _M0L8_2avrOutS502;
    uint64_t _M0L8_2avpOutS503;
    uint64_t _M0L8_2avmOutS504;
    #line 415 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1404 = _M0FPB9log10Pow5(_M0L6_2atmpS1409);
    _M0L6_2atmpS1408 = _M0Lm2e2S474;
    _M0L6_2atmpS1407 = -_M0L6_2atmpS1408;
    _M0L6_2atmpS1406 = _M0L6_2atmpS1407 > 1;
    #line 415 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1405 = _M0MPC14bool4Bool7to__int(_M0L6_2atmpS1406);
    _M0L1qS496 = _M0L6_2atmpS1404 - _M0L6_2atmpS1405;
    _M0L6_2atmpS1397 = _M0Lm2e2S474;
    _M0Lm3e10S484 = _M0L1qS496 + _M0L6_2atmpS1397;
    _M0L6_2atmpS1403 = _M0Lm2e2S474;
    _M0L6_2atmpS1402 = -_M0L6_2atmpS1403;
    _M0L1iS497 = _M0L6_2atmpS1402 - _M0L1qS496;
    #line 418 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1401 = _M0FPB8pow5bits(_M0L1iS497);
    _M0L1kS498 = _M0L6_2atmpS1401 - 125;
    _M0L1jS499 = _M0L1qS496 - _M0L1kS498;
    #line 420 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L4pow5S500 = _M0FPB19double__computePow5(_M0L1iS497);
    _M0L6_2atmpS1400 = _M0Lm2m2S475;
    #line 421 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L7_2abindS501
    = _M0FPB13mulShiftAll64(_M0L6_2atmpS1400, _M0L4pow5S500, _M0L1jS499, _M0L7mmShiftS480);
    _M0L8_2avrOutS502 = _M0L7_2abindS501.$0;
    _M0L8_2avpOutS503 = _M0L7_2abindS501.$1;
    _M0L8_2avmOutS504 = _M0L7_2abindS501.$2;
    _M0Lm2vrS481 = _M0L8_2avrOutS502;
    _M0Lm2vpS482 = _M0L8_2avpOutS503;
    _M0Lm2vmS483 = _M0L8_2avmOutS504;
    if (_M0L1qS496 <= 1) {
      _M0Lm17vrIsTrailingZerosS486 = 1;
      if (_M0L4evenS478) {
        int32_t _M0L6_2atmpS1398;
        #line 432 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0L6_2atmpS1398 = _M0MPC14bool4Bool7to__int(_M0L7mmShiftS480);
        _M0Lm17vmIsTrailingZerosS485 = _M0L6_2atmpS1398 == 1;
      } else {
        uint64_t _M0L6_2atmpS1399 = _M0Lm2vpS482;
        _M0Lm2vpS482 = _M0L6_2atmpS1399 - 1ull;
      }
    } else if (_M0L1qS496 < 63) {
      #line 437 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0Lm17vrIsTrailingZerosS486
      = _M0FPB18multipleOfPowerOf2(_M0L2mvS479, _M0L1qS496);
    }
  }
  _M0Lm7removedS505 = 0;
  _M0Lm16lastRemovedDigitS506 = 0;
  _M0Lm6outputS507 = 0ull;
  if (_M0Lm17vmIsTrailingZerosS485 || _M0Lm17vrIsTrailingZerosS486) {
    int32_t _if__result_2112;
    uint64_t _M0L6_2atmpS1440;
    uint64_t _M0L6_2atmpS1446;
    uint64_t _M0L6_2atmpS1447;
    int32_t _if__result_2113;
    int32_t _M0L6_2atmpS1443;
    int64_t _M0L6_2atmpS1442;
    uint64_t _M0L6_2atmpS1441;
    while (1) {
      uint64_t _M0L6_2atmpS1423 = _M0Lm2vpS482;
      uint64_t _M0L7vpDiv10S508 = _M0L6_2atmpS1423 / 10ull;
      uint64_t _M0L6_2atmpS1422 = _M0Lm2vmS483;
      uint64_t _M0L7vmDiv10S509 = _M0L6_2atmpS1422 / 10ull;
      uint64_t _M0L6_2atmpS1421;
      int32_t _M0L6_2atmpS1418;
      int32_t _M0L6_2atmpS1420;
      int32_t _M0L6_2atmpS1419;
      int32_t _M0L7vmMod10S511;
      uint64_t _M0L6_2atmpS1417;
      uint64_t _M0L7vrDiv10S512;
      uint64_t _M0L6_2atmpS1416;
      int32_t _M0L6_2atmpS1413;
      int32_t _M0L6_2atmpS1415;
      int32_t _M0L6_2atmpS1414;
      int32_t _M0L7vrMod10S513;
      int32_t _M0L6_2atmpS1412;
      if (_M0L7vpDiv10S508 <= _M0L7vmDiv10S509) {
        break;
      }
      _M0L6_2atmpS1421 = _M0Lm2vmS483;
      _M0L6_2atmpS1418 = (int32_t)_M0L6_2atmpS1421;
      _M0L6_2atmpS1420 = (int32_t)_M0L7vmDiv10S509;
      _M0L6_2atmpS1419 = 10 * _M0L6_2atmpS1420;
      _M0L7vmMod10S511 = _M0L6_2atmpS1418 - _M0L6_2atmpS1419;
      _M0L6_2atmpS1417 = _M0Lm2vrS481;
      _M0L7vrDiv10S512 = _M0L6_2atmpS1417 / 10ull;
      _M0L6_2atmpS1416 = _M0Lm2vrS481;
      _M0L6_2atmpS1413 = (int32_t)_M0L6_2atmpS1416;
      _M0L6_2atmpS1415 = (int32_t)_M0L7vrDiv10S512;
      _M0L6_2atmpS1414 = 10 * _M0L6_2atmpS1415;
      _M0L7vrMod10S513 = _M0L6_2atmpS1413 - _M0L6_2atmpS1414;
      _M0Lm17vmIsTrailingZerosS485
      = _M0Lm17vmIsTrailingZerosS485 && _M0L7vmMod10S511 == 0;
      if (_M0Lm17vrIsTrailingZerosS486) {
        int32_t _M0L6_2atmpS1411 = _M0Lm16lastRemovedDigitS506;
        _M0Lm17vrIsTrailingZerosS486 = _M0L6_2atmpS1411 == 0;
      } else {
        _M0Lm17vrIsTrailingZerosS486 = 0;
      }
      _M0Lm16lastRemovedDigitS506 = _M0L7vrMod10S513;
      _M0Lm2vrS481 = _M0L7vrDiv10S512;
      _M0Lm2vpS482 = _M0L7vpDiv10S508;
      _M0Lm2vmS483 = _M0L7vmDiv10S509;
      _M0L6_2atmpS1412 = _M0Lm7removedS505;
      _M0Lm7removedS505 = _M0L6_2atmpS1412 + 1;
      continue;
      break;
    }
    if (_M0Lm17vmIsTrailingZerosS485) {
      while (1) {
        uint64_t _M0L6_2atmpS1436 = _M0Lm2vmS483;
        uint64_t _M0L7vmDiv10S514 = _M0L6_2atmpS1436 / 10ull;
        uint64_t _M0L6_2atmpS1435 = _M0Lm2vmS483;
        int32_t _M0L6_2atmpS1432 = (int32_t)_M0L6_2atmpS1435;
        int32_t _M0L6_2atmpS1434 = (int32_t)_M0L7vmDiv10S514;
        int32_t _M0L6_2atmpS1433 = 10 * _M0L6_2atmpS1434;
        int32_t _M0L7vmMod10S515 = _M0L6_2atmpS1432 - _M0L6_2atmpS1433;
        uint64_t _M0L6_2atmpS1431;
        uint64_t _M0L7vpDiv10S517;
        uint64_t _M0L6_2atmpS1430;
        uint64_t _M0L7vrDiv10S518;
        uint64_t _M0L6_2atmpS1429;
        int32_t _M0L6_2atmpS1426;
        int32_t _M0L6_2atmpS1428;
        int32_t _M0L6_2atmpS1427;
        int32_t _M0L7vrMod10S519;
        int32_t _M0L6_2atmpS1425;
        if (_M0L7vmMod10S515 != 0) {
          break;
        }
        _M0L6_2atmpS1431 = _M0Lm2vpS482;
        _M0L7vpDiv10S517 = _M0L6_2atmpS1431 / 10ull;
        _M0L6_2atmpS1430 = _M0Lm2vrS481;
        _M0L7vrDiv10S518 = _M0L6_2atmpS1430 / 10ull;
        _M0L6_2atmpS1429 = _M0Lm2vrS481;
        _M0L6_2atmpS1426 = (int32_t)_M0L6_2atmpS1429;
        _M0L6_2atmpS1428 = (int32_t)_M0L7vrDiv10S518;
        _M0L6_2atmpS1427 = 10 * _M0L6_2atmpS1428;
        _M0L7vrMod10S519 = _M0L6_2atmpS1426 - _M0L6_2atmpS1427;
        if (_M0Lm17vrIsTrailingZerosS486) {
          int32_t _M0L6_2atmpS1424 = _M0Lm16lastRemovedDigitS506;
          _M0Lm17vrIsTrailingZerosS486 = _M0L6_2atmpS1424 == 0;
        } else {
          _M0Lm17vrIsTrailingZerosS486 = 0;
        }
        _M0Lm16lastRemovedDigitS506 = _M0L7vrMod10S519;
        _M0Lm2vrS481 = _M0L7vrDiv10S518;
        _M0Lm2vpS482 = _M0L7vpDiv10S517;
        _M0Lm2vmS483 = _M0L7vmDiv10S514;
        _M0L6_2atmpS1425 = _M0Lm7removedS505;
        _M0Lm7removedS505 = _M0L6_2atmpS1425 + 1;
        continue;
        break;
      }
    }
    if (_M0Lm17vrIsTrailingZerosS486) {
      int32_t _M0L6_2atmpS1439 = _M0Lm16lastRemovedDigitS506;
      if (_M0L6_2atmpS1439 == 5) {
        uint64_t _M0L6_2atmpS1438 = _M0Lm2vrS481;
        uint64_t _M0L6_2atmpS1437 = _M0L6_2atmpS1438 % 2ull;
        _if__result_2112 = _M0L6_2atmpS1437 == 0ull;
      } else {
        _if__result_2112 = 0;
      }
    } else {
      _if__result_2112 = 0;
    }
    if (_if__result_2112) {
      _M0Lm16lastRemovedDigitS506 = 4;
    }
    _M0L6_2atmpS1440 = _M0Lm2vrS481;
    _M0L6_2atmpS1446 = _M0Lm2vrS481;
    _M0L6_2atmpS1447 = _M0Lm2vmS483;
    if (_M0L6_2atmpS1446 == _M0L6_2atmpS1447) {
      if (!_M0L4evenS478) {
        _if__result_2113 = 1;
      } else {
        int32_t _M0L6_2atmpS1445 = _M0Lm17vmIsTrailingZerosS485;
        _if__result_2113 = !_M0L6_2atmpS1445;
      }
    } else {
      _if__result_2113 = 0;
    }
    if (_if__result_2113) {
      _M0L6_2atmpS1443 = 1;
    } else {
      int32_t _M0L6_2atmpS1444 = _M0Lm16lastRemovedDigitS506;
      _M0L6_2atmpS1443 = _M0L6_2atmpS1444 >= 5;
    }
    #line 487 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1442 = _M0MPC14bool4Bool9to__int64(_M0L6_2atmpS1443);
    _M0L6_2atmpS1441 = *(uint64_t*)&_M0L6_2atmpS1442;
    _M0Lm6outputS507 = _M0L6_2atmpS1440 + _M0L6_2atmpS1441;
  } else {
    int32_t _M0Lm7roundUpS520 = 0;
    uint64_t _M0L6_2atmpS1468 = _M0Lm2vpS482;
    uint64_t _M0L8vpDiv100S521 = _M0L6_2atmpS1468 / 100ull;
    uint64_t _M0L6_2atmpS1467 = _M0Lm2vmS483;
    uint64_t _M0L8vmDiv100S522 = _M0L6_2atmpS1467 / 100ull;
    uint64_t _M0L6_2atmpS1462;
    uint64_t _M0L6_2atmpS1465;
    uint64_t _M0L6_2atmpS1466;
    int32_t _M0L6_2atmpS1464;
    uint64_t _M0L6_2atmpS1463;
    if (_M0L8vpDiv100S521 > _M0L8vmDiv100S522) {
      uint64_t _M0L6_2atmpS1453 = _M0Lm2vrS481;
      uint64_t _M0L8vrDiv100S523 = _M0L6_2atmpS1453 / 100ull;
      uint64_t _M0L6_2atmpS1452 = _M0Lm2vrS481;
      int32_t _M0L6_2atmpS1449 = (int32_t)_M0L6_2atmpS1452;
      int32_t _M0L6_2atmpS1451 = (int32_t)_M0L8vrDiv100S523;
      int32_t _M0L6_2atmpS1450 = 100 * _M0L6_2atmpS1451;
      int32_t _M0L8vrMod100S524 = _M0L6_2atmpS1449 - _M0L6_2atmpS1450;
      int32_t _M0L6_2atmpS1448;
      _M0Lm7roundUpS520 = _M0L8vrMod100S524 >= 50;
      _M0Lm2vrS481 = _M0L8vrDiv100S523;
      _M0Lm2vpS482 = _M0L8vpDiv100S521;
      _M0Lm2vmS483 = _M0L8vmDiv100S522;
      _M0L6_2atmpS1448 = _M0Lm7removedS505;
      _M0Lm7removedS505 = _M0L6_2atmpS1448 + 2;
    }
    while (1) {
      uint64_t _M0L6_2atmpS1461 = _M0Lm2vpS482;
      uint64_t _M0L7vpDiv10S525 = _M0L6_2atmpS1461 / 10ull;
      uint64_t _M0L6_2atmpS1460 = _M0Lm2vmS483;
      uint64_t _M0L7vmDiv10S526 = _M0L6_2atmpS1460 / 10ull;
      uint64_t _M0L6_2atmpS1459;
      uint64_t _M0L7vrDiv10S528;
      uint64_t _M0L6_2atmpS1458;
      int32_t _M0L6_2atmpS1455;
      int32_t _M0L6_2atmpS1457;
      int32_t _M0L6_2atmpS1456;
      int32_t _M0L7vrMod10S529;
      int32_t _M0L6_2atmpS1454;
      if (_M0L7vpDiv10S525 <= _M0L7vmDiv10S526) {
        break;
      }
      _M0L6_2atmpS1459 = _M0Lm2vrS481;
      _M0L7vrDiv10S528 = _M0L6_2atmpS1459 / 10ull;
      _M0L6_2atmpS1458 = _M0Lm2vrS481;
      _M0L6_2atmpS1455 = (int32_t)_M0L6_2atmpS1458;
      _M0L6_2atmpS1457 = (int32_t)_M0L7vrDiv10S528;
      _M0L6_2atmpS1456 = 10 * _M0L6_2atmpS1457;
      _M0L7vrMod10S529 = _M0L6_2atmpS1455 - _M0L6_2atmpS1456;
      _M0Lm7roundUpS520 = _M0L7vrMod10S529 >= 5;
      _M0Lm2vrS481 = _M0L7vrDiv10S528;
      _M0Lm2vpS482 = _M0L7vpDiv10S525;
      _M0Lm2vmS483 = _M0L7vmDiv10S526;
      _M0L6_2atmpS1454 = _M0Lm7removedS505;
      _M0Lm7removedS505 = _M0L6_2atmpS1454 + 1;
      continue;
      break;
    }
    _M0L6_2atmpS1462 = _M0Lm2vrS481;
    _M0L6_2atmpS1465 = _M0Lm2vrS481;
    _M0L6_2atmpS1466 = _M0Lm2vmS483;
    _M0L6_2atmpS1464
    = _M0L6_2atmpS1465 == _M0L6_2atmpS1466 || _M0Lm7roundUpS520;
    #line 522 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1463 = _M0MPC14bool4Bool10to__uint64(_M0L6_2atmpS1464);
    _M0Lm6outputS507 = _M0L6_2atmpS1462 + _M0L6_2atmpS1463;
  }
  _M0L6_2atmpS1470 = _M0Lm3e10S484;
  _M0L6_2atmpS1471 = _M0Lm7removedS505;
  _M0L3expS530 = _M0L6_2atmpS1470 + _M0L6_2atmpS1471;
  _M0L6_2atmpS1469 = _M0Lm6outputS507;
  _block_2115
  = (struct _M0TPB17FloatingDecimal64*)moonbit_malloc(sizeof(struct _M0TPB17FloatingDecimal64));
  Moonbit_object_header(_block_2115)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_2115->$0 = _M0L6_2atmpS1469;
  _block_2115->$1 = _M0L3expS530;
  return _block_2115;
}

uint64_t _M0MPC14bool4Bool10to__uint64(int32_t _M0L4selfS473) {
  #line 110 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bool.mbt"
  if (_M0L4selfS473) {
    return 1ull;
  } else {
    return 0ull;
  }
}

int64_t _M0MPC14bool4Bool9to__int64(int32_t _M0L4selfS472) {
  #line 58 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bool.mbt"
  if (_M0L4selfS472) {
    return 1ll;
  } else {
    return 0ll;
  }
}

int32_t _M0MPC14bool4Bool7to__int(int32_t _M0L4selfS471) {
  #line 32 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bool.mbt"
  if (_M0L4selfS471) {
    return 1;
  } else {
    return 0;
  }
}

int32_t _M0FPB17decimal__length17(uint64_t _M0L1vS470) {
  #line 280 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  if (_M0L1vS470 >= 10000000000000000ull) {
    return 17;
  }
  if (_M0L1vS470 >= 1000000000000000ull) {
    return 16;
  }
  if (_M0L1vS470 >= 100000000000000ull) {
    return 15;
  }
  if (_M0L1vS470 >= 10000000000000ull) {
    return 14;
  }
  if (_M0L1vS470 >= 1000000000000ull) {
    return 13;
  }
  if (_M0L1vS470 >= 100000000000ull) {
    return 12;
  }
  if (_M0L1vS470 >= 10000000000ull) {
    return 11;
  }
  if (_M0L1vS470 >= 1000000000ull) {
    return 10;
  }
  if (_M0L1vS470 >= 100000000ull) {
    return 9;
  }
  if (_M0L1vS470 >= 10000000ull) {
    return 8;
  }
  if (_M0L1vS470 >= 1000000ull) {
    return 7;
  }
  if (_M0L1vS470 >= 100000ull) {
    return 6;
  }
  if (_M0L1vS470 >= 10000ull) {
    return 5;
  }
  if (_M0L1vS470 >= 1000ull) {
    return 4;
  }
  if (_M0L1vS470 >= 100ull) {
    return 3;
  }
  if (_M0L1vS470 >= 10ull) {
    return 2;
  }
  return 1;
}

struct _M0TPB8Pow5Pair _M0FPB22double__computeInvPow5(int32_t _M0L1iS453) {
  int32_t _M0L6_2atmpS1370;
  int32_t _M0L6_2atmpS1369;
  int32_t _M0L4baseS452;
  int32_t _M0L5base2S454;
  int32_t _M0L6offsetS455;
  int32_t _M0L6_2atmpS1368;
  uint64_t _M0L4mul0S456;
  int32_t _M0L6_2atmpS1367;
  int32_t _M0L6_2atmpS1366;
  uint64_t _M0L4mul1S457;
  uint64_t _M0L1mS458;
  struct _M0TPB7Umul128 _M0L7_2abindS459;
  uint64_t _M0L7_2alow1S460;
  uint64_t _M0L8_2ahigh1S461;
  struct _M0TPB7Umul128 _M0L7_2abindS462;
  uint64_t _M0L7_2alow0S463;
  uint64_t _M0L8_2ahigh0S464;
  uint64_t _M0L3sumS465;
  uint64_t _M0Lm5high1S466;
  int32_t _M0L6_2atmpS1364;
  int32_t _M0L6_2atmpS1365;
  int32_t _M0L5deltaS467;
  uint64_t _M0L6_2atmpS1363;
  uint64_t _M0L6_2atmpS1355;
  int32_t _M0L6_2atmpS1362;
  uint32_t _M0L6_2atmpS1359;
  int32_t _M0L6_2atmpS1361;
  int32_t _M0L6_2atmpS1360;
  uint32_t _M0L6_2atmpS1358;
  uint32_t _M0L6_2atmpS1357;
  uint64_t _M0L6_2atmpS1356;
  uint64_t _M0L1aS468;
  uint64_t _M0L6_2atmpS1354;
  uint64_t _M0L1bS469;
  #line 239 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1370 = _M0L1iS453 + 26;
  _M0L6_2atmpS1369 = _M0L6_2atmpS1370 - 1;
  _M0L4baseS452 = _M0L6_2atmpS1369 / 26;
  _M0L5base2S454 = _M0L4baseS452 * 26;
  _M0L6offsetS455 = _M0L5base2S454 - _M0L1iS453;
  _M0L6_2atmpS1368 = _M0L4baseS452 * 2;
  #line 243 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4mul0S456
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB26gDOUBLE__POW5__INV__SPLIT2, _M0L6_2atmpS1368);
  _M0L6_2atmpS1367 = _M0L4baseS452 * 2;
  _M0L6_2atmpS1366 = _M0L6_2atmpS1367 + 1;
  #line 244 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4mul1S457
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB26gDOUBLE__POW5__INV__SPLIT2, _M0L6_2atmpS1366);
  if (_M0L6offsetS455 == 0) {
    return (struct _M0TPB8Pow5Pair){.$0 = _M0L4mul0S456, .$1 = _M0L4mul1S457};
  }
  #line 248 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L1mS458
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB20gDOUBLE__POW5__TABLE, _M0L6offsetS455);
  #line 249 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS459 = _M0FPB7umul128(_M0L1mS458, _M0L4mul1S457);
  _M0L7_2alow1S460 = _M0L7_2abindS459.$0;
  _M0L8_2ahigh1S461 = _M0L7_2abindS459.$1;
  #line 250 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS462 = _M0FPB7umul128(_M0L1mS458, _M0L4mul0S456);
  _M0L7_2alow0S463 = _M0L7_2abindS462.$0;
  _M0L8_2ahigh0S464 = _M0L7_2abindS462.$1;
  _M0L3sumS465 = _M0L8_2ahigh0S464 + _M0L7_2alow1S460;
  _M0Lm5high1S466 = _M0L8_2ahigh1S461;
  if (_M0L3sumS465 < _M0L8_2ahigh0S464) {
    uint64_t _M0L6_2atmpS1353 = _M0Lm5high1S466;
    _M0Lm5high1S466 = _M0L6_2atmpS1353 + 1ull;
  }
  #line 256 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1364 = _M0FPB8pow5bits(_M0L5base2S454);
  #line 256 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1365 = _M0FPB8pow5bits(_M0L1iS453);
  _M0L5deltaS467 = _M0L6_2atmpS1364 - _M0L6_2atmpS1365;
  #line 257 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1363
  = _M0FPB13shiftright128(_M0L7_2alow0S463, _M0L3sumS465, _M0L5deltaS467);
  _M0L6_2atmpS1355 = _M0L6_2atmpS1363 + 1ull;
  _M0L6_2atmpS1362 = _M0L1iS453 / 16;
  #line 259 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1359
  = _M0MPC15array13ReadOnlyArray2atGjE(_M0FPB19gPOW5__INV__OFFSETS, _M0L6_2atmpS1362);
  _M0L6_2atmpS1361 = _M0L1iS453 % 16;
  _M0L6_2atmpS1360 = _M0L6_2atmpS1361 << 1;
  _M0L6_2atmpS1358 = _M0L6_2atmpS1359 >> (_M0L6_2atmpS1360 & 31);
  _M0L6_2atmpS1357 = _M0L6_2atmpS1358 & 3u;
  #line 259 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1356 = _M0MPC14uint4UInt10to__uint64(_M0L6_2atmpS1357);
  _M0L1aS468 = _M0L6_2atmpS1355 + _M0L6_2atmpS1356;
  _M0L6_2atmpS1354 = _M0Lm5high1S466;
  #line 260 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L1bS469
  = _M0FPB13shiftright128(_M0L3sumS465, _M0L6_2atmpS1354, _M0L5deltaS467);
  return (struct _M0TPB8Pow5Pair){.$0 = _M0L1aS468, .$1 = _M0L1bS469};
}

struct _M0TPB8Pow5Pair _M0FPB19double__computePow5(int32_t _M0L1iS435) {
  int32_t _M0L4baseS434;
  int32_t _M0L5base2S436;
  int32_t _M0L6offsetS437;
  int32_t _M0L6_2atmpS1352;
  uint64_t _M0L4mul0S438;
  int32_t _M0L6_2atmpS1351;
  int32_t _M0L6_2atmpS1350;
  uint64_t _M0L4mul1S439;
  uint64_t _M0L1mS440;
  struct _M0TPB7Umul128 _M0L7_2abindS441;
  uint64_t _M0L7_2alow1S442;
  uint64_t _M0L8_2ahigh1S443;
  struct _M0TPB7Umul128 _M0L7_2abindS444;
  uint64_t _M0L7_2alow0S445;
  uint64_t _M0L8_2ahigh0S446;
  uint64_t _M0L3sumS447;
  uint64_t _M0Lm5high1S448;
  int32_t _M0L6_2atmpS1348;
  int32_t _M0L6_2atmpS1349;
  int32_t _M0L5deltaS449;
  uint64_t _M0L6_2atmpS1340;
  int32_t _M0L6_2atmpS1347;
  uint32_t _M0L6_2atmpS1344;
  int32_t _M0L6_2atmpS1346;
  int32_t _M0L6_2atmpS1345;
  uint32_t _M0L6_2atmpS1343;
  uint32_t _M0L6_2atmpS1342;
  uint64_t _M0L6_2atmpS1341;
  uint64_t _M0L1aS450;
  uint64_t _M0L6_2atmpS1339;
  uint64_t _M0L1bS451;
  #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4baseS434 = _M0L1iS435 / 26;
  _M0L5base2S436 = _M0L4baseS434 * 26;
  _M0L6offsetS437 = _M0L1iS435 - _M0L5base2S436;
  _M0L6_2atmpS1352 = _M0L4baseS434 * 2;
  #line 217 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4mul0S438
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB21gDOUBLE__POW5__SPLIT2, _M0L6_2atmpS1352);
  _M0L6_2atmpS1351 = _M0L4baseS434 * 2;
  _M0L6_2atmpS1350 = _M0L6_2atmpS1351 + 1;
  #line 218 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4mul1S439
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB21gDOUBLE__POW5__SPLIT2, _M0L6_2atmpS1350);
  if (_M0L6offsetS437 == 0) {
    return (struct _M0TPB8Pow5Pair){.$0 = _M0L4mul0S438, .$1 = _M0L4mul1S439};
  }
  #line 222 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L1mS440
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB20gDOUBLE__POW5__TABLE, _M0L6offsetS437);
  #line 223 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS441 = _M0FPB7umul128(_M0L1mS440, _M0L4mul1S439);
  _M0L7_2alow1S442 = _M0L7_2abindS441.$0;
  _M0L8_2ahigh1S443 = _M0L7_2abindS441.$1;
  #line 224 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS444 = _M0FPB7umul128(_M0L1mS440, _M0L4mul0S438);
  _M0L7_2alow0S445 = _M0L7_2abindS444.$0;
  _M0L8_2ahigh0S446 = _M0L7_2abindS444.$1;
  _M0L3sumS447 = _M0L8_2ahigh0S446 + _M0L7_2alow1S442;
  _M0Lm5high1S448 = _M0L8_2ahigh1S443;
  if (_M0L3sumS447 < _M0L8_2ahigh0S446) {
    uint64_t _M0L6_2atmpS1338 = _M0Lm5high1S448;
    _M0Lm5high1S448 = _M0L6_2atmpS1338 + 1ull;
  }
  #line 230 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1348 = _M0FPB8pow5bits(_M0L1iS435);
  #line 230 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1349 = _M0FPB8pow5bits(_M0L5base2S436);
  _M0L5deltaS449 = _M0L6_2atmpS1348 - _M0L6_2atmpS1349;
  #line 231 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1340
  = _M0FPB13shiftright128(_M0L7_2alow0S445, _M0L3sumS447, _M0L5deltaS449);
  _M0L6_2atmpS1347 = _M0L1iS435 / 16;
  #line 232 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1344
  = _M0MPC15array13ReadOnlyArray2atGjE(_M0FPB14gPOW5__OFFSETS, _M0L6_2atmpS1347);
  _M0L6_2atmpS1346 = _M0L1iS435 % 16;
  _M0L6_2atmpS1345 = _M0L6_2atmpS1346 << 1;
  _M0L6_2atmpS1343 = _M0L6_2atmpS1344 >> (_M0L6_2atmpS1345 & 31);
  _M0L6_2atmpS1342 = _M0L6_2atmpS1343 & 3u;
  #line 232 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1341 = _M0MPC14uint4UInt10to__uint64(_M0L6_2atmpS1342);
  _M0L1aS450 = _M0L6_2atmpS1340 + _M0L6_2atmpS1341;
  _M0L6_2atmpS1339 = _M0Lm5high1S448;
  #line 233 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L1bS451
  = _M0FPB13shiftright128(_M0L3sumS447, _M0L6_2atmpS1339, _M0L5deltaS449);
  return (struct _M0TPB8Pow5Pair){.$0 = _M0L1aS450, .$1 = _M0L1bS451};
}

struct _M0TPB19MulShiftAll64Result _M0FPB13mulShiftAll64(
  uint64_t _M0L1mS408,
  struct _M0TPB8Pow5Pair _M0L3mulS405,
  int32_t _M0L1jS421,
  int32_t _M0L7mmShiftS423
) {
  uint64_t _M0L7_2amul0S404;
  uint64_t _M0L7_2amul1S406;
  uint64_t _M0L1mS407;
  struct _M0TPB7Umul128 _M0L7_2abindS409;
  uint64_t _M0L5_2aloS410;
  uint64_t _M0L6_2atmpS411;
  struct _M0TPB7Umul128 _M0L7_2abindS412;
  uint64_t _M0L6_2alo2S413;
  uint64_t _M0L6_2ahi2S414;
  uint64_t _M0L3midS415;
  uint64_t _M0L6_2atmpS1337;
  uint64_t _M0L2hiS416;
  uint64_t _M0L3lo2S417;
  uint64_t _M0L6_2atmpS1335;
  uint64_t _M0L6_2atmpS1336;
  uint64_t _M0L4mid2S418;
  uint64_t _M0L6_2atmpS1334;
  uint64_t _M0L3hi2S419;
  int32_t _M0L6_2atmpS1333;
  int32_t _M0L6_2atmpS1332;
  uint64_t _M0L2vpS420;
  uint64_t _M0Lm2vmS422;
  int32_t _M0L6_2atmpS1331;
  int32_t _M0L6_2atmpS1330;
  uint64_t _M0L2vrS433;
  uint64_t _M0L6_2atmpS1329;
  #line 129 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2amul0S404 = _M0L3mulS405.$0;
  _M0L7_2amul1S406 = _M0L3mulS405.$1;
  _M0L1mS407 = _M0L1mS408 << 1;
  #line 137 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS409 = _M0FPB7umul128(_M0L1mS407, _M0L7_2amul0S404);
  _M0L5_2aloS410 = _M0L7_2abindS409.$0;
  _M0L6_2atmpS411 = _M0L7_2abindS409.$1;
  #line 138 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS412 = _M0FPB7umul128(_M0L1mS407, _M0L7_2amul1S406);
  _M0L6_2alo2S413 = _M0L7_2abindS412.$0;
  _M0L6_2ahi2S414 = _M0L7_2abindS412.$1;
  _M0L3midS415 = _M0L6_2atmpS411 + _M0L6_2alo2S413;
  if (_M0L3midS415 < _M0L6_2atmpS411) {
    _M0L6_2atmpS1337 = 1ull;
  } else {
    _M0L6_2atmpS1337 = 0ull;
  }
  _M0L2hiS416 = _M0L6_2ahi2S414 + _M0L6_2atmpS1337;
  _M0L3lo2S417 = _M0L5_2aloS410 + _M0L7_2amul0S404;
  _M0L6_2atmpS1335 = _M0L3midS415 + _M0L7_2amul1S406;
  if (_M0L3lo2S417 < _M0L5_2aloS410) {
    _M0L6_2atmpS1336 = 1ull;
  } else {
    _M0L6_2atmpS1336 = 0ull;
  }
  _M0L4mid2S418 = _M0L6_2atmpS1335 + _M0L6_2atmpS1336;
  if (_M0L4mid2S418 < _M0L3midS415) {
    _M0L6_2atmpS1334 = 1ull;
  } else {
    _M0L6_2atmpS1334 = 0ull;
  }
  _M0L3hi2S419 = _M0L2hiS416 + _M0L6_2atmpS1334;
  _M0L6_2atmpS1333 = _M0L1jS421 - 64;
  _M0L6_2atmpS1332 = _M0L6_2atmpS1333 - 1;
  #line 144 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L2vpS420
  = _M0FPB13shiftright128(_M0L4mid2S418, _M0L3hi2S419, _M0L6_2atmpS1332);
  _M0Lm2vmS422 = 0ull;
  if (_M0L7mmShiftS423) {
    uint64_t _M0L3lo3S424 = _M0L5_2aloS410 - _M0L7_2amul0S404;
    uint64_t _M0L6_2atmpS1319 = _M0L3midS415 - _M0L7_2amul1S406;
    uint64_t _M0L6_2atmpS1320;
    uint64_t _M0L4mid3S425;
    uint64_t _M0L6_2atmpS1318;
    uint64_t _M0L3hi3S426;
    int32_t _M0L6_2atmpS1317;
    int32_t _M0L6_2atmpS1316;
    if (_M0L5_2aloS410 < _M0L3lo3S424) {
      _M0L6_2atmpS1320 = 1ull;
    } else {
      _M0L6_2atmpS1320 = 0ull;
    }
    _M0L4mid3S425 = _M0L6_2atmpS1319 - _M0L6_2atmpS1320;
    if (_M0L3midS415 < _M0L4mid3S425) {
      _M0L6_2atmpS1318 = 1ull;
    } else {
      _M0L6_2atmpS1318 = 0ull;
    }
    _M0L3hi3S426 = _M0L2hiS416 - _M0L6_2atmpS1318;
    _M0L6_2atmpS1317 = _M0L1jS421 - 64;
    _M0L6_2atmpS1316 = _M0L6_2atmpS1317 - 1;
    #line 150 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0Lm2vmS422
    = _M0FPB13shiftright128(_M0L4mid3S425, _M0L3hi3S426, _M0L6_2atmpS1316);
  } else {
    uint64_t _M0L3lo3S427 = _M0L5_2aloS410 + _M0L5_2aloS410;
    uint64_t _M0L6_2atmpS1327 = _M0L3midS415 + _M0L3midS415;
    uint64_t _M0L6_2atmpS1328;
    uint64_t _M0L4mid3S428;
    uint64_t _M0L6_2atmpS1325;
    uint64_t _M0L6_2atmpS1326;
    uint64_t _M0L3hi3S429;
    uint64_t _M0L3lo4S430;
    uint64_t _M0L6_2atmpS1323;
    uint64_t _M0L6_2atmpS1324;
    uint64_t _M0L4mid4S431;
    uint64_t _M0L6_2atmpS1322;
    uint64_t _M0L3hi4S432;
    int32_t _M0L6_2atmpS1321;
    if (_M0L3lo3S427 < _M0L5_2aloS410) {
      _M0L6_2atmpS1328 = 1ull;
    } else {
      _M0L6_2atmpS1328 = 0ull;
    }
    _M0L4mid3S428 = _M0L6_2atmpS1327 + _M0L6_2atmpS1328;
    _M0L6_2atmpS1325 = _M0L2hiS416 + _M0L2hiS416;
    if (_M0L4mid3S428 < _M0L3midS415) {
      _M0L6_2atmpS1326 = 1ull;
    } else {
      _M0L6_2atmpS1326 = 0ull;
    }
    _M0L3hi3S429 = _M0L6_2atmpS1325 + _M0L6_2atmpS1326;
    _M0L3lo4S430 = _M0L3lo3S427 - _M0L7_2amul0S404;
    _M0L6_2atmpS1323 = _M0L4mid3S428 - _M0L7_2amul1S406;
    if (_M0L3lo3S427 < _M0L3lo4S430) {
      _M0L6_2atmpS1324 = 1ull;
    } else {
      _M0L6_2atmpS1324 = 0ull;
    }
    _M0L4mid4S431 = _M0L6_2atmpS1323 - _M0L6_2atmpS1324;
    if (_M0L4mid3S428 < _M0L4mid4S431) {
      _M0L6_2atmpS1322 = 1ull;
    } else {
      _M0L6_2atmpS1322 = 0ull;
    }
    _M0L3hi4S432 = _M0L3hi3S429 - _M0L6_2atmpS1322;
    _M0L6_2atmpS1321 = _M0L1jS421 - 64;
    #line 158 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0Lm2vmS422
    = _M0FPB13shiftright128(_M0L4mid4S431, _M0L3hi4S432, _M0L6_2atmpS1321);
  }
  _M0L6_2atmpS1331 = _M0L1jS421 - 64;
  _M0L6_2atmpS1330 = _M0L6_2atmpS1331 - 1;
  #line 160 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L2vrS433
  = _M0FPB13shiftright128(_M0L3midS415, _M0L2hiS416, _M0L6_2atmpS1330);
  _M0L6_2atmpS1329 = _M0Lm2vmS422;
  return (struct _M0TPB19MulShiftAll64Result){.$0 = _M0L2vrS433,
                                                .$1 = _M0L2vpS420,
                                                .$2 = _M0L6_2atmpS1329};
}

int32_t _M0FPB18multipleOfPowerOf2(
  uint64_t _M0L5valueS402,
  int32_t _M0L1pS403
) {
  uint64_t _M0L6_2atmpS1315;
  uint64_t _M0L6_2atmpS1314;
  uint64_t _M0L6_2atmpS1313;
  #line 124 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1315 = 1ull << (_M0L1pS403 & 63);
  _M0L6_2atmpS1314 = _M0L6_2atmpS1315 - 1ull;
  _M0L6_2atmpS1313 = _M0L5valueS402 & _M0L6_2atmpS1314;
  return _M0L6_2atmpS1313 == 0ull;
}

int32_t _M0FPB18multipleOfPowerOf5(
  uint64_t _M0L5valueS400,
  int32_t _M0L1pS401
) {
  int32_t _M0L6_2atmpS1312;
  #line 119 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  #line 120 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1312 = _M0FPB10pow5Factor(_M0L5valueS400);
  return _M0L6_2atmpS1312 >= _M0L1pS401;
}

int32_t _M0FPB10pow5Factor(uint64_t _M0L5valueS395) {
  uint64_t _M0L6_2atmpS1303;
  uint64_t _M0L6_2atmpS1304;
  uint64_t _M0L6_2atmpS1305;
  uint64_t _M0L6_2atmpS1306;
  uint64_t _M0L6_2atmpS1311;
  int32_t _M0L5countS396;
  uint64_t _M0L1vS397;
  #line 94 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1303 = _M0L5valueS395 % 5ull;
  if (_M0L6_2atmpS1303 != 0ull) {
    return 0;
  }
  _M0L6_2atmpS1304 = _M0L5valueS395 % 25ull;
  if (_M0L6_2atmpS1304 != 0ull) {
    return 1;
  }
  _M0L6_2atmpS1305 = _M0L5valueS395 % 125ull;
  if (_M0L6_2atmpS1305 != 0ull) {
    return 2;
  }
  _M0L6_2atmpS1306 = _M0L5valueS395 % 625ull;
  if (_M0L6_2atmpS1306 != 0ull) {
    return 3;
  }
  _M0L6_2atmpS1311 = _M0L5valueS395 / 625ull;
  _M0L5countS396 = 4;
  _M0L1vS397 = _M0L6_2atmpS1311;
  while (1) {
    if (_M0L1vS397 > 0ull) {
      uint64_t _M0L6_2atmpS1307 = _M0L1vS397 % 5ull;
      int32_t _M0L6_2atmpS1308;
      uint64_t _M0L6_2atmpS1309;
      if (_M0L6_2atmpS1307 != 0ull) {
        return _M0L5countS396;
      }
      _M0L6_2atmpS1308 = _M0L5countS396 + 1;
      _M0L6_2atmpS1309 = _M0L1vS397 / 5ull;
      _M0L5countS396 = _M0L6_2atmpS1308;
      _M0L1vS397 = _M0L6_2atmpS1309;
      continue;
    } else {
      struct _M0TPB13StringBuilder* _M0L18_2astring__builderS399;
      moonbit_string_t _M0L6_2atmpS1310;
      int32_t _result_2117;
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0L18_2astring__builderS399
      = _M0MPB13StringBuilder21StringBuilder_2einner(25);
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS399, (moonbit_string_t)moonbit_string_literal_5.data);
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0MPB13StringBuilder13write__objectGmE(_M0L18_2astring__builderS399, _M0L5valueS395);
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0L6_2atmpS1310
      = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS399);
      moonbit_decref(_M0L18_2astring__builderS399);
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _result_2117 = _M0FPC15abort5abortGiE(_M0L6_2atmpS1310);
      moonbit_decref(_M0L6_2atmpS1310);
      return _result_2117;
    }
    break;
  }
}

uint64_t _M0FPB13shiftright128(
  uint64_t _M0L2loS394,
  uint64_t _M0L2hiS392,
  int32_t _M0L4distS393
) {
  int32_t _M0L6_2atmpS1302;
  uint64_t _M0L6_2atmpS1300;
  uint64_t _M0L6_2atmpS1301;
  #line 89 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1302 = 64 - _M0L4distS393;
  _M0L6_2atmpS1300 = _M0L2hiS392 << (_M0L6_2atmpS1302 & 63);
  _M0L6_2atmpS1301 = _M0L2loS394 >> (_M0L4distS393 & 63);
  return _M0L6_2atmpS1300 | _M0L6_2atmpS1301;
}

struct _M0TPB7Umul128 _M0FPB7umul128(
  uint64_t _M0L1aS382,
  uint64_t _M0L1bS385
) {
  uint64_t _M0L3aLoS381;
  uint64_t _M0L3aHiS383;
  uint64_t _M0L3bLoS384;
  uint64_t _M0L3bHiS386;
  uint64_t _M0L1xS387;
  uint64_t _M0L6_2atmpS1298;
  uint64_t _M0L6_2atmpS1299;
  uint64_t _M0L1yS388;
  uint64_t _M0L6_2atmpS1296;
  uint64_t _M0L6_2atmpS1297;
  uint64_t _M0L1zS389;
  uint64_t _M0L6_2atmpS1294;
  uint64_t _M0L6_2atmpS1295;
  uint64_t _M0L6_2atmpS1292;
  uint64_t _M0L6_2atmpS1293;
  uint64_t _M0L1wS390;
  uint64_t _M0L2loS391;
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L3aLoS381 = _M0L1aS382 & 4294967295ull;
  _M0L3aHiS383 = _M0L1aS382 >> 32;
  _M0L3bLoS384 = _M0L1bS385 & 4294967295ull;
  _M0L3bHiS386 = _M0L1bS385 >> 32;
  _M0L1xS387 = _M0L3aLoS381 * _M0L3bLoS384;
  _M0L6_2atmpS1298 = _M0L3aHiS383 * _M0L3bLoS384;
  _M0L6_2atmpS1299 = _M0L1xS387 >> 32;
  _M0L1yS388 = _M0L6_2atmpS1298 + _M0L6_2atmpS1299;
  _M0L6_2atmpS1296 = _M0L3aLoS381 * _M0L3bHiS386;
  _M0L6_2atmpS1297 = _M0L1yS388 & 4294967295ull;
  _M0L1zS389 = _M0L6_2atmpS1296 + _M0L6_2atmpS1297;
  _M0L6_2atmpS1294 = _M0L3aHiS383 * _M0L3bHiS386;
  _M0L6_2atmpS1295 = _M0L1yS388 >> 32;
  _M0L6_2atmpS1292 = _M0L6_2atmpS1294 + _M0L6_2atmpS1295;
  _M0L6_2atmpS1293 = _M0L1zS389 >> 32;
  _M0L1wS390 = _M0L6_2atmpS1292 + _M0L6_2atmpS1293;
  _M0L2loS391 = _M0L1aS382 * _M0L1bS385;
  return (struct _M0TPB7Umul128){.$0 = _M0L2loS391, .$1 = _M0L1wS390};
}

moonbit_string_t _M0FPB19string__from__bytes(
  moonbit_bytes_t _M0L5bytesS379,
  int32_t _M0L4fromS376,
  int32_t _M0L2toS375
) {
  int32_t _M0L3lenS374;
  int32_t _M0L6_2atmpS1291;
  uint16_t* _M0L6bufferS377;
  int32_t _M0L1iS378;
  #line 52 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L3lenS374 = _M0L2toS375 - _M0L4fromS376;
  #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1291 = _M0IPC16uint166UInt16PB7Default7default();
  _M0L6bufferS377
  = (uint16_t*)moonbit_make_string(_M0L3lenS374, _M0L6_2atmpS1291);
  _M0L1iS378 = 0;
  while (1) {
    if (_M0L1iS378 < _M0L3lenS374) {
      int32_t _M0L6_2atmpS1289 = _M0L4fromS376 + _M0L1iS378;
      int32_t _M0L6_2atmpS1288;
      int32_t _M0L6_2atmpS1287;
      int32_t _M0L6_2atmpS1290;
      if (
        _M0L6_2atmpS1289 < 0
        || _M0L6_2atmpS1289 >= Moonbit_array_length(_M0L5bytesS379)
      ) {
        #line 56 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6_2atmpS1288 = (int32_t)_M0L5bytesS379[_M0L6_2atmpS1289];
      _M0L6_2atmpS1287 = (uint16_t)_M0L6_2atmpS1288;
      if (
        _M0L1iS378 < 0 || _M0L1iS378 >= Moonbit_array_length(_M0L6bufferS377)
      ) {
        #line 56 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6bufferS377[_M0L1iS378] = _M0L6_2atmpS1287;
      _M0L6_2atmpS1290 = _M0L1iS378 + 1;
      _M0L1iS378 = _M0L6_2atmpS1290;
      continue;
    }
    break;
  }
  return _M0L6bufferS377;
}

int32_t _M0FPB9log10Pow2(int32_t _M0L1eS373) {
  int32_t _M0L6_2atmpS1286;
  uint32_t _M0L6_2atmpS1285;
  uint32_t _M0L6_2atmpS1284;
  #line 44 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1286 = _M0L1eS373 * 78913;
  _M0L6_2atmpS1285 = *(uint32_t*)&_M0L6_2atmpS1286;
  _M0L6_2atmpS1284 = _M0L6_2atmpS1285 >> 18;
  return *(int32_t*)&_M0L6_2atmpS1284;
}

int32_t _M0FPB9log10Pow5(int32_t _M0L1eS372) {
  int32_t _M0L6_2atmpS1283;
  uint32_t _M0L6_2atmpS1282;
  uint32_t _M0L6_2atmpS1281;
  #line 37 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1283 = _M0L1eS372 * 732923;
  _M0L6_2atmpS1282 = *(uint32_t*)&_M0L6_2atmpS1283;
  _M0L6_2atmpS1281 = _M0L6_2atmpS1282 >> 20;
  return *(int32_t*)&_M0L6_2atmpS1281;
}

moonbit_string_t _M0FPB18copy__special__str(
  int32_t _M0L4signS370,
  int32_t _M0L8exponentS371,
  int32_t _M0L8mantissaS368
) {
  moonbit_string_t _M0L1sS369;
  moonbit_string_t _result_2120;
  #line 23 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  if (_M0L8mantissaS368) {
    return (moonbit_string_t)moonbit_string_literal_6.data;
  }
  if (_M0L4signS370) {
    _M0L1sS369 = (moonbit_string_t)moonbit_string_literal_7.data;
  } else {
    _M0L1sS369 = (moonbit_string_t)moonbit_string_literal_8.data;
  }
  if (_M0L8exponentS371) {
    moonbit_string_t _result_2119;
    #line 29 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _result_2119
    = moonbit_add_string(_M0L1sS369, (moonbit_string_t)moonbit_string_literal_9.data);
    moonbit_decref(_M0L1sS369);
    return _result_2119;
  }
  #line 31 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _result_2120
  = moonbit_add_string(_M0L1sS369, (moonbit_string_t)moonbit_string_literal_10.data);
  moonbit_decref(_M0L1sS369);
  return _result_2120;
}

int32_t _M0FPB8pow5bits(int32_t _M0L1eS367) {
  int32_t _M0L6_2atmpS1280;
  uint32_t _M0L6_2atmpS1279;
  uint32_t _M0L6_2atmpS1278;
  int32_t _M0L6_2atmpS1277;
  #line 18 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1280 = _M0L1eS367 * 1217359;
  _M0L6_2atmpS1279 = *(uint32_t*)&_M0L6_2atmpS1280;
  _M0L6_2atmpS1278 = _M0L6_2atmpS1279 >> 19;
  _M0L6_2atmpS1277 = *(int32_t*)&_M0L6_2atmpS1278;
  return _M0L6_2atmpS1277 + 1;
}

int32_t _M0MPC16double6Double7to__int(double _M0L4selfS366) {
  #line 43 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_to_int.mbt"
  if (_M0L4selfS366 != _M0L4selfS366) {
    return 0;
  } else if (_M0L4selfS366 >= 0x1.fffffffcp+30) {
    return 2147483647;
  } else if (_M0L4selfS366 <= -0x1p+31) {
    return (int32_t)0x80000000;
  } else {
    return (int32_t)_M0L4selfS366;
  }
}

int64_t _M0MPC16double6Double9to__int64(double _M0L4selfS365) {
  #line 46 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_to_int64_native.mbt"
  if (_M0L4selfS365 != _M0L4selfS365) {
    return 0ll;
  } else if (_M0L4selfS365 >= 0x1p+63) {
    return 9223372036854775807ll;
  } else if (_M0L4selfS365 <= -0x1p+63) {
    return (int64_t)0x8000000000000000ll;
  } else {
    return (int64_t)_M0L4selfS365;
  }
}

struct _M0TPB5ArrayGfE* _M0MPC15array5Array20unsafe__make__uninitGfE(
  int32_t _M0L3lenS362
) {
  float* _M0L6_2atmpS1274;
  struct _M0TPB5ArrayGfE* _block_2121;
  #line 30 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1274 = (float*)moonbit_make_float_array_raw(_M0L3lenS362);
  _block_2121
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_block_2121)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 0, 0);
  _block_2121->$0 = _M0L6_2atmpS1274;
  _block_2121->$1 = _M0L3lenS362;
  return _block_2121;
}

struct _M0TPB5ArrayGbE* _M0MPC15array5Array20unsafe__make__uninitGbE(
  int32_t _M0L3lenS363
) {
  uint8_t* _M0L6_2atmpS1275;
  struct _M0TPB5ArrayGbE* _block_2122;
  #line 30 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1275 = (uint8_t*)moonbit_make_bytes_raw(_M0L3lenS363);
  _block_2122
  = (struct _M0TPB5ArrayGbE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGbE));
  Moonbit_object_header(_block_2122)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 53, 0);
  _block_2122->$0 = _M0L6_2atmpS1275;
  _block_2122->$1 = _M0L3lenS363;
  return _block_2122;
}

struct _M0TPB5ArrayGiE* _M0MPC15array5Array20unsafe__make__uninitGiE(
  int32_t _M0L3lenS364
) {
  int32_t* _M0L6_2atmpS1276;
  struct _M0TPB5ArrayGiE* _block_2123;
  #line 30 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1276 = (int32_t*)moonbit_make_int32_array_raw(_M0L3lenS364);
  _block_2123
  = (struct _M0TPB5ArrayGiE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGiE));
  Moonbit_object_header(_block_2123)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 44, 0);
  _block_2123->$0 = _M0L6_2atmpS1276;
  _block_2123->$1 = _M0L3lenS364;
  return _block_2123;
}

uint64_t _M0MPC15array13ReadOnlyArray2atGmE(
  uint64_t* _M0L4selfS358,
  int32_t _M0L5indexS359
) {
  uint64_t* _M0L6_2atmpS1272;
  #line 38 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\readonlyarray.mbt"
  _M0L6_2atmpS1272 = _M0L4selfS358;
  if (
    _M0L5indexS359 < 0
    || _M0L5indexS359 >= Moonbit_array_length(_M0L6_2atmpS1272)
  ) {
    #line 40 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\readonlyarray.mbt"
    moonbit_panic();
  }
  return (uint64_t)_M0L6_2atmpS1272[_M0L5indexS359];
}

uint32_t _M0MPC15array13ReadOnlyArray2atGjE(
  uint32_t* _M0L4selfS360,
  int32_t _M0L5indexS361
) {
  uint32_t* _M0L6_2atmpS1273;
  #line 38 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\readonlyarray.mbt"
  _M0L6_2atmpS1273 = _M0L4selfS360;
  if (
    _M0L5indexS361 < 0
    || _M0L5indexS361 >= Moonbit_array_length(_M0L6_2atmpS1273)
  ) {
    #line 40 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\readonlyarray.mbt"
    moonbit_panic();
  }
  return (uint32_t)_M0L6_2atmpS1273[_M0L5indexS361];
}

moonbit_string_t _M0IPC16uint646UInt64PB4Show10to__string(
  uint64_t _M0L4selfS357
) {
  #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  #line 51 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  return _M0MPC16uint646UInt6418to__string_2einner(_M0L4selfS357, 10);
}

moonbit_string_t _M0IPC13int3IntPB4Show10to__string(int32_t _M0L4selfS356) {
  #line 35 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  #line 36 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  return _M0MPC13int3Int18to__string_2einner(_M0L4selfS356, 10);
}

uint64_t _M0MPC14uint4UInt10to__uint64(uint32_t _M0L4selfS355) {
  #line 2576 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  return (uint64_t)_M0L4selfS355;
}

int32_t _M0MPC15array5Array4pushGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS349,
  int32_t _M0L5valueS351
) {
  int32_t _M0L3lenS1258;
  int32_t* _M0L6_2atmpS1260;
  int32_t _M0L6_2atmpS1259;
  int32_t _M0L6lengthS350;
  int32_t* _M0L3bufS1263;
  int32_t _M0L6_2atmpS1264;
  #line 406 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L3lenS1258 = _M0L4selfS349->$1;
  #line 408 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1260 = _M0MPC15array5Array6bufferGiE(_M0L4selfS349);
  _M0L6_2atmpS1259 = Moonbit_array_length(_M0L6_2atmpS1260);
  moonbit_decref(_M0L6_2atmpS1260);
  if (_M0L3lenS1258 == _M0L6_2atmpS1259) {
    int32_t _M0L3lenS1262 = _M0L4selfS349->$1;
    int32_t _M0L6_2atmpS1261 = _M0L3lenS1262 + 1;
    #line 409 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0MPC15array5Array7reallocGiE(_M0L4selfS349, _M0L6_2atmpS1261);
  }
  _M0L6lengthS350 = _M0L4selfS349->$1;
  _M0L3bufS1263 = _M0L4selfS349->$0;
  _M0L3bufS1263[_M0L6lengthS350] = _M0L5valueS351;
  _M0L6_2atmpS1264 = _M0L6lengthS350 + 1;
  _M0L4selfS349->$1 = _M0L6_2atmpS1264;
  return 0;
}

int32_t _M0MPC15array5Array4pushGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS352,
  float _M0L5valueS354
) {
  int32_t _M0L3lenS1265;
  float* _M0L6_2atmpS1267;
  int32_t _M0L6_2atmpS1266;
  int32_t _M0L6lengthS353;
  float* _M0L3bufS1270;
  int32_t _M0L6_2atmpS1271;
  #line 406 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L3lenS1265 = _M0L4selfS352->$1;
  #line 408 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1267 = _M0MPC15array5Array6bufferGfE(_M0L4selfS352);
  _M0L6_2atmpS1266 = Moonbit_array_length(_M0L6_2atmpS1267);
  moonbit_decref(_M0L6_2atmpS1267);
  if (_M0L3lenS1265 == _M0L6_2atmpS1266) {
    int32_t _M0L3lenS1269 = _M0L4selfS352->$1;
    int32_t _M0L6_2atmpS1268 = _M0L3lenS1269 + 1;
    #line 409 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0MPC15array5Array7reallocGfE(_M0L4selfS352, _M0L6_2atmpS1268);
  }
  _M0L6lengthS353 = _M0L4selfS352->$1;
  _M0L3bufS1270 = _M0L4selfS352->$0;
  _M0L3bufS1270[_M0L6lengthS353] = _M0L5valueS354;
  _M0L6_2atmpS1271 = _M0L6lengthS353 + 1;
  _M0L4selfS352->$1 = _M0L6_2atmpS1271;
  return 0;
}

int32_t _M0MPC15array5Array7reallocGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS342,
  int32_t _M0L8requiredS344
) {
  int32_t _M0L8old__capS341;
  int32_t _M0L3lenS1256;
  int32_t _M0L8new__capS343;
  #line 302 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  #line 304 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__capS341 = _M0MPC15array5Array8capacityGiE(_M0L4selfS342);
  _M0L3lenS1256 = _M0L4selfS342->$1;
  #line 305 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__capS343
  = _M0FPB23array__growth__capacity(_M0L8old__capS341, _M0L3lenS1256, _M0L8requiredS344);
  #line 306 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0MPC15array5Array14resize__bufferGiE(_M0L4selfS342, _M0L8new__capS343);
  return 0;
}

int32_t _M0MPC15array5Array7reallocGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS346,
  int32_t _M0L8requiredS348
) {
  int32_t _M0L8old__capS345;
  int32_t _M0L3lenS1257;
  int32_t _M0L8new__capS347;
  #line 302 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  #line 304 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__capS345 = _M0MPC15array5Array8capacityGfE(_M0L4selfS346);
  _M0L3lenS1257 = _M0L4selfS346->$1;
  #line 305 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__capS347
  = _M0FPB23array__growth__capacity(_M0L8old__capS345, _M0L3lenS1257, _M0L8requiredS348);
  #line 306 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0MPC15array5Array14resize__bufferGfE(_M0L4selfS346, _M0L8new__capS347);
  return 0;
}

int32_t _M0MPC15array5Array14resize__bufferGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS330,
  int32_t _M0L13new__capacityS333
) {
  int32_t* _M0L8old__bufS329;
  int32_t _M0L3lenS331;
  int32_t _M0L9copy__lenS332;
  int32_t* _M0L8new__bufS334;
  int32_t* _M0L6_2aoldS2043;
  #line 242 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__bufS329 = _M0L4selfS330->$0;
  _M0L3lenS331 = _M0L4selfS330->$1;
  if (_M0L3lenS331 < _M0L13new__capacityS333) {
    _M0L9copy__lenS332 = _M0L3lenS331;
  } else {
    _M0L9copy__lenS332 = _M0L13new__capacityS333;
  }
  moonbit_incref(_M0L8old__bufS329);
  #line 249 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__bufS334
  = _M0MPB18UninitializedArray23make__and__blit_2einnerGiE(_M0L8old__bufS329, _M0L13new__capacityS333, _M0L9copy__lenS332, 0, 0);
  _M0L6_2aoldS2043 = _M0L4selfS330->$0;
  moonbit_decref(_M0L6_2aoldS2043);
  _M0L4selfS330->$0 = _M0L8new__bufS334;
  return 0;
}

int32_t _M0MPC15array5Array14resize__bufferGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS336,
  int32_t _M0L13new__capacityS339
) {
  float* _M0L8old__bufS335;
  int32_t _M0L3lenS337;
  int32_t _M0L9copy__lenS338;
  float* _M0L8new__bufS340;
  float* _M0L6_2aoldS2044;
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
  = _M0MPB18UninitializedArray23make__and__blit_2einnerGfE(_M0L8old__bufS335, _M0L13new__capacityS339, _M0L9copy__lenS338, 0, 0);
  _M0L6_2aoldS2044 = _M0L4selfS336->$0;
  moonbit_decref(_M0L6_2aoldS2044);
  _M0L4selfS336->$0 = _M0L8new__bufS340;
  return 0;
}

int32_t _M0MPC15array5Array8capacityGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS327
) {
  int32_t* _M0L6_2atmpS1254;
  int32_t _result_2124;
  #line 132 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L6_2atmpS1254 = _M0MPC15array5Array6bufferGiE(_M0L4selfS327);
  _result_2124 = Moonbit_array_length(_M0L6_2atmpS1254);
  moonbit_decref(_M0L6_2atmpS1254);
  return _result_2124;
}

int32_t _M0MPC15array5Array8capacityGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS328
) {
  float* _M0L6_2atmpS1255;
  int32_t _result_2125;
  #line 132 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L6_2atmpS1255 = _M0MPC15array5Array6bufferGfE(_M0L4selfS328);
  _result_2125 = Moonbit_array_length(_M0L6_2atmpS1255);
  moonbit_decref(_M0L6_2atmpS1255);
  return _result_2125;
}

int32_t _M0FPB23array__growth__capacity(
  int32_t _M0L7currentS323,
  int32_t _M0L3lenS321,
  int32_t _M0L8requiredS320
) {
  int32_t _M0L5startS322;
  int32_t _M0L5spaceS324;
  #line 200 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  if (_M0L8requiredS320 < _M0L3lenS321) {
    #line 203 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_11.data);
  }
  if (_M0L7currentS323 == 0) {
    _M0L5startS322 = 8;
  } else {
    _M0L5startS322 = _M0L7currentS323;
  }
  _M0L5spaceS324 = _M0L5startS322;
  while (1) {
    if (_M0L5spaceS324 < _M0L8requiredS320) {
      int32_t _M0L4nextS325 = _M0L5spaceS324 * 2;
      if (_M0L4nextS325 <= _M0L5spaceS324) {
        return _M0L8requiredS320;
      }
      _M0L5spaceS324 = _M0L4nextS325;
      continue;
    } else {
      return _M0L5spaceS324;
    }
    break;
  }
}

int32_t _M0MPC15array5Array6lengthGfE(struct _M0TPB5ArrayGfE* _M0L4selfS319) {
  #line 147 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  return _M0L4selfS319->$1;
}

float* _M0MPC15array5Array6bufferGfE(struct _M0TPB5ArrayGfE* _M0L4selfS316) {
  float* _M0L8_2afieldS2045;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS2045 = _M0L4selfS316->$0;
  moonbit_incref(_M0L8_2afieldS2045);
  return _M0L8_2afieldS2045;
}

int32_t* _M0MPC15array5Array6bufferGiE(struct _M0TPB5ArrayGiE* _M0L4selfS317) {
  int32_t* _M0L8_2afieldS2046;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS2046 = _M0L4selfS317->$0;
  moonbit_incref(_M0L8_2afieldS2046);
  return _M0L8_2afieldS2046;
}

uint8_t* _M0MPC15array5Array6bufferGbE(struct _M0TPB5ArrayGbE* _M0L4selfS318) {
  uint8_t* _M0L8_2afieldS2047;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS2047 = _M0L4selfS318->$0;
  moonbit_incref(_M0L8_2afieldS2047);
  return _M0L8_2afieldS2047;
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
  int32_t _M0L3endS1252;
  int32_t _M0L5startS1253;
  int32_t _M0L8str__lenS311;
  int32_t _M0L3lenS1251;
  int32_t _M0L8requiredS313;
  uint16_t* _M0L4dataS1244;
  int32_t _M0L6_2atmpS1243;
  int32_t _if__result_2127;
  uint16_t* _M0L4dataS1245;
  int32_t _M0L3lenS1246;
  moonbit_string_t _M0L6_2atmpS1247;
  int32_t _M0L6_2atmpS1248;
  int32_t _M0L3lenS1250;
  int32_t _M0L6_2atmpS1249;
  #line 158 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L3endS1252 = _M0L3strS312.$2;
  _M0L5startS1253 = _M0L3strS312.$1;
  _M0L8str__lenS311 = _M0L3endS1252 - _M0L5startS1253;
  if (_M0L8str__lenS311 == 0) {
    return 0;
  }
  _M0L3lenS1251 = _M0L4selfS314->$1;
  _M0L8requiredS313 = _M0L3lenS1251 + _M0L8str__lenS311;
  _M0L4dataS1244 = _M0L4selfS314->$0;
  _M0L6_2atmpS1243 = Moonbit_array_length(_M0L4dataS1244);
  if (_M0L8requiredS313 > _M0L6_2atmpS1243) {
    _if__result_2127 = 1;
  } else {
    int32_t _M0L3lenS1242 = _M0L4selfS314->$1;
    _if__result_2127 = _M0L8requiredS313 < _M0L3lenS1242;
  }
  if (_if__result_2127) {
    #line 168 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0MPB13StringBuilder4grow(_M0L4selfS314, _M0L8requiredS313);
  }
  _M0L4dataS1245 = _M0L4selfS314->$0;
  _M0L3lenS1246 = _M0L4selfS314->$1;
  moonbit_incref(_M0L4dataS1245);
  #line 172 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L6_2atmpS1247 = _M0MPC16string10StringView4data(_M0L3strS312);
  #line 173 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L6_2atmpS1248 = _M0MPC16string10StringView13start__offset(_M0L3strS312);
  #line 170 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0MPC15array10FixedArray26unsafe__blit__from__string(_M0L4dataS1245, _M0L3lenS1246, _M0L6_2atmpS1247, _M0L6_2atmpS1248, _M0L8str__lenS311);
  moonbit_decref(_M0L4dataS1245);
  moonbit_decref(_M0L6_2atmpS1247);
  _M0L3lenS1250 = _M0L4selfS314->$1;
  _M0L6_2atmpS1249 = _M0L3lenS1250 + _M0L8str__lenS311;
  _M0L4selfS314->$1 = _M0L6_2atmpS1249;
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
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_12.data);
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
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_12.data);
  }
  if (_M0L4selfS286 == 0ll) {
    return (moonbit_string_t)moonbit_string_literal_4.data;
  }
  _M0L12is__negativeS287 = _M0L4selfS286 < 0ll;
  if (_M0L12is__negativeS287) {
    int64_t _M0L6_2atmpS1241 = -_M0L4selfS286;
    _M0L3numS288 = *(uint64_t*)&_M0L6_2atmpS1241;
  } else {
    _M0L3numS288 = *(uint64_t*)&_M0L4selfS286;
  }
  switch (_M0L5radixS285) {
    case 10: {
      int32_t _M0L10digit__lenS290;
      int32_t _M0L6_2atmpS1238;
      int32_t _M0L10total__lenS291;
      uint16_t* _M0L6bufferS292;
      int32_t _M0L12digit__startS293;
      #line 573 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS290 = _M0FPB12dec__count64(_M0L3numS288);
      if (_M0L12is__negativeS287) {
        _M0L6_2atmpS1238 = 1;
      } else {
        _M0L6_2atmpS1238 = 0;
      }
      _M0L10total__lenS291 = _M0L10digit__lenS290 + _M0L6_2atmpS1238;
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
      int32_t _M0L6_2atmpS1239;
      int32_t _M0L10total__lenS295;
      uint16_t* _M0L6bufferS296;
      int32_t _M0L12digit__startS297;
      #line 581 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS294 = _M0FPB12hex__count64(_M0L3numS288);
      if (_M0L12is__negativeS287) {
        _M0L6_2atmpS1239 = 1;
      } else {
        _M0L6_2atmpS1239 = 0;
      }
      _M0L10total__lenS295 = _M0L10digit__lenS294 + _M0L6_2atmpS1239;
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
      int32_t _M0L6_2atmpS1240;
      int32_t _M0L10total__lenS299;
      uint16_t* _M0L6bufferS300;
      int32_t _M0L12digit__startS301;
      #line 589 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS298
      = _M0FPB14radix__count64(_M0L3numS288, _M0L5radixS285);
      if (_M0L12is__negativeS287) {
        _M0L6_2atmpS1240 = 1;
      } else {
        _M0L6_2atmpS1240 = 0;
      }
      _M0L10total__lenS299 = _M0L10digit__lenS298 + _M0L6_2atmpS1240;
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
  int32_t _M0L6_2atmpS1237;
  uint64_t _M0L3numS261;
  int32_t _M0L6offsetS262;
  #line 493 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L6_2atmpS1237 = _M0L10total__lenS284 - _M0L12digit__startS272;
  _M0L3numS261 = _M0L3numS283;
  _M0L6offsetS262 = _M0L6_2atmpS1237;
  while (1) {
    if (_M0L3numS261 >= 10000ull) {
      uint64_t _M0L1tS263 = _M0L3numS261 / 10000ull;
      uint64_t _M0L6_2atmpS1214 = _M0L3numS261 % 10000ull;
      int32_t _M0L1rS264 = (int32_t)_M0L6_2atmpS1214;
      int32_t _M0L2d1S265 = _M0L1rS264 / 100;
      int32_t _M0L2d2S266 = _M0L1rS264 % 100;
      int32_t _M0L6_2atmpS1213 = _M0L2d1S265 / 10;
      int32_t _M0L6_2atmpS1212 = 48 + _M0L6_2atmpS1213;
      int32_t _M0L6d1__hiS267 = (uint16_t)_M0L6_2atmpS1212;
      int32_t _M0L6_2atmpS1211 = _M0L2d1S265 % 10;
      int32_t _M0L6_2atmpS1210 = 48 + _M0L6_2atmpS1211;
      int32_t _M0L6d1__loS268 = (uint16_t)_M0L6_2atmpS1210;
      int32_t _M0L6_2atmpS1209 = _M0L2d2S266 / 10;
      int32_t _M0L6_2atmpS1208 = 48 + _M0L6_2atmpS1209;
      int32_t _M0L6d2__hiS269 = (uint16_t)_M0L6_2atmpS1208;
      int32_t _M0L6_2atmpS1207 = _M0L2d2S266 % 10;
      int32_t _M0L6_2atmpS1206 = 48 + _M0L6_2atmpS1207;
      int32_t _M0L6d2__loS270 = (uint16_t)_M0L6_2atmpS1206;
      int32_t _M0L6_2atmpS1198 = _M0L12digit__startS272 + _M0L6offsetS262;
      int32_t _M0L6_2atmpS1197 = _M0L6_2atmpS1198 - 4;
      int32_t _M0L6_2atmpS1200;
      int32_t _M0L6_2atmpS1199;
      int32_t _M0L6_2atmpS1202;
      int32_t _M0L6_2atmpS1201;
      int32_t _M0L6_2atmpS1204;
      int32_t _M0L6_2atmpS1203;
      int32_t _M0L6_2atmpS1205;
      _M0L6bufferS271[_M0L6_2atmpS1197] = _M0L6d1__hiS267;
      _M0L6_2atmpS1200 = _M0L12digit__startS272 + _M0L6offsetS262;
      _M0L6_2atmpS1199 = _M0L6_2atmpS1200 - 3;
      _M0L6bufferS271[_M0L6_2atmpS1199] = _M0L6d1__loS268;
      _M0L6_2atmpS1202 = _M0L12digit__startS272 + _M0L6offsetS262;
      _M0L6_2atmpS1201 = _M0L6_2atmpS1202 - 2;
      _M0L6bufferS271[_M0L6_2atmpS1201] = _M0L6d2__hiS269;
      _M0L6_2atmpS1204 = _M0L12digit__startS272 + _M0L6offsetS262;
      _M0L6_2atmpS1203 = _M0L6_2atmpS1204 - 1;
      _M0L6bufferS271[_M0L6_2atmpS1203] = _M0L6d2__loS270;
      _M0L6_2atmpS1205 = _M0L6offsetS262 - 4;
      _M0L3numS261 = _M0L1tS263;
      _M0L6offsetS262 = _M0L6_2atmpS1205;
      continue;
    } else {
      int32_t _M0L6_2atmpS1236 = (int32_t)_M0L3numS261;
      int32_t _M0L9remainingS274 = _M0L6_2atmpS1236;
      int32_t _M0L6offsetS275 = _M0L6offsetS262;
      while (1) {
        if (_M0L9remainingS274 >= 100) {
          int32_t _M0L1tS276 = _M0L9remainingS274 / 100;
          int32_t _M0L1dS277 = _M0L9remainingS274 % 100;
          int32_t _M0L6_2atmpS1223 = _M0L1dS277 / 10;
          int32_t _M0L6_2atmpS1222 = 48 + _M0L6_2atmpS1223;
          int32_t _M0L5d__hiS278 = (uint16_t)_M0L6_2atmpS1222;
          int32_t _M0L6_2atmpS1221 = _M0L1dS277 % 10;
          int32_t _M0L6_2atmpS1220 = 48 + _M0L6_2atmpS1221;
          int32_t _M0L5d__loS279 = (uint16_t)_M0L6_2atmpS1220;
          int32_t _M0L6_2atmpS1216 = _M0L12digit__startS272 + _M0L6offsetS275;
          int32_t _M0L6_2atmpS1215 = _M0L6_2atmpS1216 - 2;
          int32_t _M0L6_2atmpS1218;
          int32_t _M0L6_2atmpS1217;
          int32_t _M0L6_2atmpS1219;
          _M0L6bufferS271[_M0L6_2atmpS1215] = _M0L5d__hiS278;
          _M0L6_2atmpS1218 = _M0L12digit__startS272 + _M0L6offsetS275;
          _M0L6_2atmpS1217 = _M0L6_2atmpS1218 - 1;
          _M0L6bufferS271[_M0L6_2atmpS1217] = _M0L5d__loS279;
          _M0L6_2atmpS1219 = _M0L6offsetS275 - 2;
          _M0L9remainingS274 = _M0L1tS276;
          _M0L6offsetS275 = _M0L6_2atmpS1219;
          continue;
        } else if (_M0L9remainingS274 >= 10) {
          int32_t _M0L6_2atmpS1231 = _M0L9remainingS274 / 10;
          int32_t _M0L6_2atmpS1230 = 48 + _M0L6_2atmpS1231;
          int32_t _M0L5d__hiS281 = (uint16_t)_M0L6_2atmpS1230;
          int32_t _M0L6_2atmpS1229 = _M0L9remainingS274 % 10;
          int32_t _M0L6_2atmpS1228 = 48 + _M0L6_2atmpS1229;
          int32_t _M0L5d__loS282 = (uint16_t)_M0L6_2atmpS1228;
          int32_t _M0L6_2atmpS1225 = _M0L12digit__startS272 + _M0L6offsetS275;
          int32_t _M0L6_2atmpS1224 = _M0L6_2atmpS1225 - 2;
          int32_t _M0L6_2atmpS1227;
          int32_t _M0L6_2atmpS1226;
          _M0L6bufferS271[_M0L6_2atmpS1224] = _M0L5d__hiS281;
          _M0L6_2atmpS1227 = _M0L12digit__startS272 + _M0L6offsetS275;
          _M0L6_2atmpS1226 = _M0L6_2atmpS1227 - 1;
          _M0L6bufferS271[_M0L6_2atmpS1226] = _M0L5d__loS282;
        } else {
          int32_t _M0L6_2atmpS1235 = _M0L12digit__startS272 + _M0L6offsetS275;
          int32_t _M0L6_2atmpS1232 = _M0L6_2atmpS1235 - 1;
          int32_t _M0L6_2atmpS1234 = 48 + _M0L9remainingS274;
          int32_t _M0L6_2atmpS1233 = (uint16_t)_M0L6_2atmpS1234;
          _M0L6bufferS271[_M0L6_2atmpS1232] = _M0L6_2atmpS1233;
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
  int32_t _M0L6_2atmpS1182;
  int32_t _M0L6_2atmpS1181;
  #line 462 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  #line 470 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L4baseS244 = _M0MPC13int3Int10to__uint64(_M0L5radixS245);
  _M0L6_2atmpS1182 = _M0L5radixS245 - 1;
  _M0L6_2atmpS1181 = _M0L5radixS245 & _M0L6_2atmpS1182;
  if (_M0L6_2atmpS1181 == 0) {
    int32_t _M0L5shiftS246;
    uint64_t _M0L4maskS247;
    int32_t _M0L6_2atmpS1189;
    int32_t _M0L6offsetS248;
    uint64_t _M0L1nS249;
    #line 473 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0L5shiftS246 = moonbit_ctz32(_M0L5radixS245);
    _M0L4maskS247 = _M0L4baseS244 - 1ull;
    _M0L6_2atmpS1189 = _M0L10total__lenS254 - _M0L12digit__startS252;
    _M0L6offsetS248 = _M0L6_2atmpS1189;
    _M0L1nS249 = _M0L3numS255;
    while (1) {
      if (_M0L1nS249 > 0ull) {
        uint64_t _M0L6_2atmpS1188 = _M0L1nS249 & _M0L4maskS247;
        int32_t _M0L5digitS250 = (int32_t)_M0L6_2atmpS1188;
        int32_t _M0L6_2atmpS1185 = _M0L12digit__startS252 + _M0L6offsetS248;
        int32_t _M0L6_2atmpS1183 = _M0L6_2atmpS1185 - 1;
        int32_t _M0L6_2atmpS1184 =
          ((moonbit_string_t)moonbit_string_literal_13.data)[_M0L5digitS250];
        int32_t _M0L6_2atmpS1186;
        uint64_t _M0L6_2atmpS1187;
        _M0L6bufferS251[_M0L6_2atmpS1183] = _M0L6_2atmpS1184;
        _M0L6_2atmpS1186 = _M0L6offsetS248 - 1;
        _M0L6_2atmpS1187 = _M0L1nS249 >> (_M0L5shiftS246 & 63);
        _M0L6offsetS248 = _M0L6_2atmpS1186;
        _M0L1nS249 = _M0L6_2atmpS1187;
        continue;
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1196 = _M0L10total__lenS254 - _M0L12digit__startS252;
    int32_t _M0L6offsetS256 = _M0L6_2atmpS1196;
    uint64_t _M0L1nS257 = _M0L3numS255;
    while (1) {
      if (_M0L1nS257 > 0ull) {
        uint64_t _M0L1qS258 = _M0L1nS257 / _M0L4baseS244;
        uint64_t _M0L6_2atmpS1195 = _M0L1qS258 * _M0L4baseS244;
        uint64_t _M0L6_2atmpS1194 = _M0L1nS257 - _M0L6_2atmpS1195;
        int32_t _M0L5digitS259 = (int32_t)_M0L6_2atmpS1194;
        int32_t _M0L6_2atmpS1192 = _M0L12digit__startS252 + _M0L6offsetS256;
        int32_t _M0L6_2atmpS1190 = _M0L6_2atmpS1192 - 1;
        int32_t _M0L6_2atmpS1191 =
          ((moonbit_string_t)moonbit_string_literal_13.data)[_M0L5digitS259];
        int32_t _M0L6_2atmpS1193;
        _M0L6bufferS251[_M0L6_2atmpS1190] = _M0L6_2atmpS1191;
        _M0L6_2atmpS1193 = _M0L6offsetS256 - 1;
        _M0L6offsetS256 = _M0L6_2atmpS1193;
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
  int32_t _M0L6_2atmpS1180;
  int32_t _M0L6offsetS233;
  uint64_t _M0L1nS234;
  #line 434 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L6_2atmpS1180 = _M0L10total__lenS242 - _M0L12digit__startS239;
  _M0L6offsetS233 = _M0L6_2atmpS1180;
  _M0L1nS234 = _M0L3numS243;
  while (1) {
    if (_M0L6offsetS233 >= 2) {
      uint64_t _M0L6_2atmpS1177 = _M0L1nS234 & 255ull;
      int32_t _M0L9byte__valS235 = (int32_t)_M0L6_2atmpS1177;
      int32_t _M0L2hiS236 = _M0L9byte__valS235 / 16;
      int32_t _M0L2loS237 = _M0L9byte__valS235 % 16;
      int32_t _M0L6_2atmpS1171 = _M0L12digit__startS239 + _M0L6offsetS233;
      int32_t _M0L6_2atmpS1169 = _M0L6_2atmpS1171 - 2;
      int32_t _M0L6_2atmpS1170 =
        ((moonbit_string_t)moonbit_string_literal_13.data)[_M0L2hiS236];
      int32_t _M0L6_2atmpS1174;
      int32_t _M0L6_2atmpS1172;
      int32_t _M0L6_2atmpS1173;
      int32_t _M0L6_2atmpS1175;
      uint64_t _M0L6_2atmpS1176;
      _M0L6bufferS238[_M0L6_2atmpS1169] = _M0L6_2atmpS1170;
      _M0L6_2atmpS1174 = _M0L12digit__startS239 + _M0L6offsetS233;
      _M0L6_2atmpS1172 = _M0L6_2atmpS1174 - 1;
      _M0L6_2atmpS1173
      = ((moonbit_string_t)moonbit_string_literal_13.data)[
        _M0L2loS237
      ];
      _M0L6bufferS238[_M0L6_2atmpS1172] = _M0L6_2atmpS1173;
      _M0L6_2atmpS1175 = _M0L6offsetS233 - 2;
      _M0L6_2atmpS1176 = _M0L1nS234 >> 8;
      _M0L6offsetS233 = _M0L6_2atmpS1175;
      _M0L1nS234 = _M0L6_2atmpS1176;
      continue;
    } else if (_M0L6offsetS233 == 1) {
      uint64_t _M0L6_2atmpS1179 = _M0L1nS234 & 15ull;
      int32_t _M0L6nibbleS241 = (int32_t)_M0L6_2atmpS1179;
      int32_t _M0L6_2atmpS1178 =
        ((moonbit_string_t)moonbit_string_literal_13.data)[_M0L6nibbleS241];
      _M0L6bufferS238[_M0L12digit__startS239] = _M0L6_2atmpS1178;
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
      uint64_t _M0L6_2atmpS1167 = _M0L3numS230 / _M0L4baseS228;
      int32_t _M0L6_2atmpS1168 = _M0L5countS231 + 1;
      _M0L3numS230 = _M0L6_2atmpS1167;
      _M0L5countS231 = _M0L6_2atmpS1168;
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
    int32_t _M0L6_2atmpS1166;
    int32_t _M0L6_2atmpS1165;
    #line 412 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0L14leading__zerosS226 = moonbit_clz64(_M0L5valueS225);
    _M0L6_2atmpS1166 = 63 - _M0L14leading__zerosS226;
    _M0L6_2atmpS1165 = _M0L6_2atmpS1166 / 4;
    return _M0L6_2atmpS1165 + 1;
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
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_12.data);
  }
  if (_M0L4selfS208 == 0) {
    return (moonbit_string_t)moonbit_string_literal_4.data;
  }
  _M0L12is__negativeS209 = _M0L4selfS208 < 0;
  if (_M0L12is__negativeS209) {
    int32_t _M0L6_2atmpS1164 = -_M0L4selfS208;
    _M0L3numS210 = *(uint32_t*)&_M0L6_2atmpS1164;
  } else {
    _M0L3numS210 = *(uint32_t*)&_M0L4selfS208;
  }
  switch (_M0L5radixS207) {
    case 10: {
      int32_t _M0L10digit__lenS212;
      int32_t _M0L6_2atmpS1161;
      int32_t _M0L10total__lenS213;
      uint16_t* _M0L6bufferS214;
      int32_t _M0L12digit__startS215;
      #line 235 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS212 = _M0FPB12dec__count32(_M0L3numS210);
      if (_M0L12is__negativeS209) {
        _M0L6_2atmpS1161 = 1;
      } else {
        _M0L6_2atmpS1161 = 0;
      }
      _M0L10total__lenS213 = _M0L10digit__lenS212 + _M0L6_2atmpS1161;
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
      int32_t _M0L6_2atmpS1162;
      int32_t _M0L10total__lenS217;
      uint16_t* _M0L6bufferS218;
      int32_t _M0L12digit__startS219;
      #line 243 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS216 = _M0FPB12hex__count32(_M0L3numS210);
      if (_M0L12is__negativeS209) {
        _M0L6_2atmpS1162 = 1;
      } else {
        _M0L6_2atmpS1162 = 0;
      }
      _M0L10total__lenS217 = _M0L10digit__lenS216 + _M0L6_2atmpS1162;
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
      int32_t _M0L6_2atmpS1163;
      int32_t _M0L10total__lenS221;
      uint16_t* _M0L6bufferS222;
      int32_t _M0L12digit__startS223;
      #line 251 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS220
      = _M0FPB14radix__count32(_M0L3numS210, _M0L5radixS207);
      if (_M0L12is__negativeS209) {
        _M0L6_2atmpS1163 = 1;
      } else {
        _M0L6_2atmpS1163 = 0;
      }
      _M0L10total__lenS221 = _M0L10digit__lenS220 + _M0L6_2atmpS1163;
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
      uint32_t _M0L6_2atmpS1159 = _M0L3numS204 / _M0L4baseS202;
      int32_t _M0L6_2atmpS1160 = _M0L5countS205 + 1;
      _M0L3numS204 = _M0L6_2atmpS1159;
      _M0L5countS205 = _M0L6_2atmpS1160;
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
    int32_t _M0L6_2atmpS1158;
    int32_t _M0L6_2atmpS1157;
    #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0L14leading__zerosS200 = moonbit_clz32(_M0L5valueS199);
    _M0L6_2atmpS1158 = 31 - _M0L14leading__zerosS200;
    _M0L6_2atmpS1157 = _M0L6_2atmpS1158 / 4;
    return _M0L6_2atmpS1157 + 1;
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
  int32_t _M0L6_2atmpS1156;
  uint32_t _M0L3numS174;
  int32_t _M0L6offsetS175;
  #line 88 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L6_2atmpS1156 = _M0L10total__lenS197 - _M0L12digit__startS185;
  _M0L3numS174 = _M0L3numS196;
  _M0L6offsetS175 = _M0L6_2atmpS1156;
  while (1) {
    if (_M0L3numS174 >= 10000u) {
      uint32_t _M0L1tS176 = _M0L3numS174 / 10000u;
      uint32_t _M0L6_2atmpS1133 = _M0L3numS174 % 10000u;
      int32_t _M0L1rS177 = *(int32_t*)&_M0L6_2atmpS1133;
      int32_t _M0L2d1S178 = _M0L1rS177 / 100;
      int32_t _M0L2d2S179 = _M0L1rS177 % 100;
      int32_t _M0L6_2atmpS1132 = _M0L2d1S178 / 10;
      int32_t _M0L6_2atmpS1131 = 48 + _M0L6_2atmpS1132;
      int32_t _M0L6d1__hiS180 = (uint16_t)_M0L6_2atmpS1131;
      int32_t _M0L6_2atmpS1130 = _M0L2d1S178 % 10;
      int32_t _M0L6_2atmpS1129 = 48 + _M0L6_2atmpS1130;
      int32_t _M0L6d1__loS181 = (uint16_t)_M0L6_2atmpS1129;
      int32_t _M0L6_2atmpS1128 = _M0L2d2S179 / 10;
      int32_t _M0L6_2atmpS1127 = 48 + _M0L6_2atmpS1128;
      int32_t _M0L6d2__hiS182 = (uint16_t)_M0L6_2atmpS1127;
      int32_t _M0L6_2atmpS1126 = _M0L2d2S179 % 10;
      int32_t _M0L6_2atmpS1125 = 48 + _M0L6_2atmpS1126;
      int32_t _M0L6d2__loS183 = (uint16_t)_M0L6_2atmpS1125;
      int32_t _M0L6_2atmpS1117 = _M0L12digit__startS185 + _M0L6offsetS175;
      int32_t _M0L6_2atmpS1116 = _M0L6_2atmpS1117 - 4;
      int32_t _M0L6_2atmpS1119;
      int32_t _M0L6_2atmpS1118;
      int32_t _M0L6_2atmpS1121;
      int32_t _M0L6_2atmpS1120;
      int32_t _M0L6_2atmpS1123;
      int32_t _M0L6_2atmpS1122;
      int32_t _M0L6_2atmpS1124;
      _M0L6bufferS184[_M0L6_2atmpS1116] = _M0L6d1__hiS180;
      _M0L6_2atmpS1119 = _M0L12digit__startS185 + _M0L6offsetS175;
      _M0L6_2atmpS1118 = _M0L6_2atmpS1119 - 3;
      _M0L6bufferS184[_M0L6_2atmpS1118] = _M0L6d1__loS181;
      _M0L6_2atmpS1121 = _M0L12digit__startS185 + _M0L6offsetS175;
      _M0L6_2atmpS1120 = _M0L6_2atmpS1121 - 2;
      _M0L6bufferS184[_M0L6_2atmpS1120] = _M0L6d2__hiS182;
      _M0L6_2atmpS1123 = _M0L12digit__startS185 + _M0L6offsetS175;
      _M0L6_2atmpS1122 = _M0L6_2atmpS1123 - 1;
      _M0L6bufferS184[_M0L6_2atmpS1122] = _M0L6d2__loS183;
      _M0L6_2atmpS1124 = _M0L6offsetS175 - 4;
      _M0L3numS174 = _M0L1tS176;
      _M0L6offsetS175 = _M0L6_2atmpS1124;
      continue;
    } else {
      int32_t _M0L6_2atmpS1155 = *(int32_t*)&_M0L3numS174;
      int32_t _M0L9remainingS187 = _M0L6_2atmpS1155;
      int32_t _M0L6offsetS188 = _M0L6offsetS175;
      while (1) {
        if (_M0L9remainingS187 >= 100) {
          int32_t _M0L1tS189 = _M0L9remainingS187 / 100;
          int32_t _M0L1dS190 = _M0L9remainingS187 % 100;
          int32_t _M0L6_2atmpS1142 = _M0L1dS190 / 10;
          int32_t _M0L6_2atmpS1141 = 48 + _M0L6_2atmpS1142;
          int32_t _M0L5d__hiS191 = (uint16_t)_M0L6_2atmpS1141;
          int32_t _M0L6_2atmpS1140 = _M0L1dS190 % 10;
          int32_t _M0L6_2atmpS1139 = 48 + _M0L6_2atmpS1140;
          int32_t _M0L5d__loS192 = (uint16_t)_M0L6_2atmpS1139;
          int32_t _M0L6_2atmpS1135 = _M0L12digit__startS185 + _M0L6offsetS188;
          int32_t _M0L6_2atmpS1134 = _M0L6_2atmpS1135 - 2;
          int32_t _M0L6_2atmpS1137;
          int32_t _M0L6_2atmpS1136;
          int32_t _M0L6_2atmpS1138;
          _M0L6bufferS184[_M0L6_2atmpS1134] = _M0L5d__hiS191;
          _M0L6_2atmpS1137 = _M0L12digit__startS185 + _M0L6offsetS188;
          _M0L6_2atmpS1136 = _M0L6_2atmpS1137 - 1;
          _M0L6bufferS184[_M0L6_2atmpS1136] = _M0L5d__loS192;
          _M0L6_2atmpS1138 = _M0L6offsetS188 - 2;
          _M0L9remainingS187 = _M0L1tS189;
          _M0L6offsetS188 = _M0L6_2atmpS1138;
          continue;
        } else if (_M0L9remainingS187 >= 10) {
          int32_t _M0L6_2atmpS1150 = _M0L9remainingS187 / 10;
          int32_t _M0L6_2atmpS1149 = 48 + _M0L6_2atmpS1150;
          int32_t _M0L5d__hiS194 = (uint16_t)_M0L6_2atmpS1149;
          int32_t _M0L6_2atmpS1148 = _M0L9remainingS187 % 10;
          int32_t _M0L6_2atmpS1147 = 48 + _M0L6_2atmpS1148;
          int32_t _M0L5d__loS195 = (uint16_t)_M0L6_2atmpS1147;
          int32_t _M0L6_2atmpS1144 = _M0L12digit__startS185 + _M0L6offsetS188;
          int32_t _M0L6_2atmpS1143 = _M0L6_2atmpS1144 - 2;
          int32_t _M0L6_2atmpS1146;
          int32_t _M0L6_2atmpS1145;
          _M0L6bufferS184[_M0L6_2atmpS1143] = _M0L5d__hiS194;
          _M0L6_2atmpS1146 = _M0L12digit__startS185 + _M0L6offsetS188;
          _M0L6_2atmpS1145 = _M0L6_2atmpS1146 - 1;
          _M0L6bufferS184[_M0L6_2atmpS1145] = _M0L5d__loS195;
        } else {
          int32_t _M0L6_2atmpS1154 = _M0L12digit__startS185 + _M0L6offsetS188;
          int32_t _M0L6_2atmpS1151 = _M0L6_2atmpS1154 - 1;
          int32_t _M0L6_2atmpS1153 = 48 + _M0L9remainingS187;
          int32_t _M0L6_2atmpS1152 = (uint16_t)_M0L6_2atmpS1153;
          _M0L6bufferS184[_M0L6_2atmpS1151] = _M0L6_2atmpS1152;
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
  int32_t _M0L6_2atmpS1101;
  int32_t _M0L6_2atmpS1100;
  #line 57 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L4baseS157 = *(uint32_t*)&_M0L5radixS158;
  _M0L6_2atmpS1101 = _M0L5radixS158 - 1;
  _M0L6_2atmpS1100 = _M0L5radixS158 & _M0L6_2atmpS1101;
  if (_M0L6_2atmpS1100 == 0) {
    int32_t _M0L5shiftS159;
    uint32_t _M0L4maskS160;
    int32_t _M0L6_2atmpS1108;
    int32_t _M0L6offsetS161;
    uint32_t _M0L1nS162;
    #line 68 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0L5shiftS159 = moonbit_ctz32(_M0L5radixS158);
    _M0L4maskS160 = _M0L4baseS157 - 1u;
    _M0L6_2atmpS1108 = _M0L10total__lenS167 - _M0L12digit__startS165;
    _M0L6offsetS161 = _M0L6_2atmpS1108;
    _M0L1nS162 = _M0L3numS168;
    while (1) {
      if (_M0L1nS162 > 0u) {
        uint32_t _M0L6_2atmpS1107 = _M0L1nS162 & _M0L4maskS160;
        int32_t _M0L5digitS163 = *(int32_t*)&_M0L6_2atmpS1107;
        int32_t _M0L6_2atmpS1104 = _M0L12digit__startS165 + _M0L6offsetS161;
        int32_t _M0L6_2atmpS1102 = _M0L6_2atmpS1104 - 1;
        int32_t _M0L6_2atmpS1103 =
          ((moonbit_string_t)moonbit_string_literal_13.data)[_M0L5digitS163];
        int32_t _M0L6_2atmpS1105;
        uint32_t _M0L6_2atmpS1106;
        _M0L6bufferS164[_M0L6_2atmpS1102] = _M0L6_2atmpS1103;
        _M0L6_2atmpS1105 = _M0L6offsetS161 - 1;
        _M0L6_2atmpS1106 = _M0L1nS162 >> (_M0L5shiftS159 & 31);
        _M0L6offsetS161 = _M0L6_2atmpS1105;
        _M0L1nS162 = _M0L6_2atmpS1106;
        continue;
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1115 = _M0L10total__lenS167 - _M0L12digit__startS165;
    int32_t _M0L6offsetS169 = _M0L6_2atmpS1115;
    uint32_t _M0L1nS170 = _M0L3numS168;
    while (1) {
      if (_M0L1nS170 > 0u) {
        uint32_t _M0L1qS171 = _M0L1nS170 / _M0L4baseS157;
        uint32_t _M0L6_2atmpS1114 = _M0L1qS171 * _M0L4baseS157;
        uint32_t _M0L6_2atmpS1113 = _M0L1nS170 - _M0L6_2atmpS1114;
        int32_t _M0L5digitS172 = *(int32_t*)&_M0L6_2atmpS1113;
        int32_t _M0L6_2atmpS1111 = _M0L12digit__startS165 + _M0L6offsetS169;
        int32_t _M0L6_2atmpS1109 = _M0L6_2atmpS1111 - 1;
        int32_t _M0L6_2atmpS1110 =
          ((moonbit_string_t)moonbit_string_literal_13.data)[_M0L5digitS172];
        int32_t _M0L6_2atmpS1112;
        _M0L6bufferS164[_M0L6_2atmpS1109] = _M0L6_2atmpS1110;
        _M0L6_2atmpS1112 = _M0L6offsetS169 - 1;
        _M0L6offsetS169 = _M0L6_2atmpS1112;
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
  int32_t _M0L6_2atmpS1099;
  int32_t _M0L6offsetS146;
  uint32_t _M0L1nS147;
  #line 29 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L6_2atmpS1099 = _M0L10total__lenS155 - _M0L12digit__startS152;
  _M0L6offsetS146 = _M0L6_2atmpS1099;
  _M0L1nS147 = _M0L3numS156;
  while (1) {
    if (_M0L6offsetS146 >= 2) {
      uint32_t _M0L6_2atmpS1096 = _M0L1nS147 & 255u;
      int32_t _M0L9byte__valS148 = *(int32_t*)&_M0L6_2atmpS1096;
      int32_t _M0L2hiS149 = _M0L9byte__valS148 / 16;
      int32_t _M0L2loS150 = _M0L9byte__valS148 % 16;
      int32_t _M0L6_2atmpS1090 = _M0L12digit__startS152 + _M0L6offsetS146;
      int32_t _M0L6_2atmpS1088 = _M0L6_2atmpS1090 - 2;
      int32_t _M0L6_2atmpS1089 =
        ((moonbit_string_t)moonbit_string_literal_13.data)[_M0L2hiS149];
      int32_t _M0L6_2atmpS1093;
      int32_t _M0L6_2atmpS1091;
      int32_t _M0L6_2atmpS1092;
      int32_t _M0L6_2atmpS1094;
      uint32_t _M0L6_2atmpS1095;
      _M0L6bufferS151[_M0L6_2atmpS1088] = _M0L6_2atmpS1089;
      _M0L6_2atmpS1093 = _M0L12digit__startS152 + _M0L6offsetS146;
      _M0L6_2atmpS1091 = _M0L6_2atmpS1093 - 1;
      _M0L6_2atmpS1092
      = ((moonbit_string_t)moonbit_string_literal_13.data)[
        _M0L2loS150
      ];
      _M0L6bufferS151[_M0L6_2atmpS1091] = _M0L6_2atmpS1092;
      _M0L6_2atmpS1094 = _M0L6offsetS146 - 2;
      _M0L6_2atmpS1095 = _M0L1nS147 >> 8;
      _M0L6offsetS146 = _M0L6_2atmpS1094;
      _M0L1nS147 = _M0L6_2atmpS1095;
      continue;
    } else if (_M0L6offsetS146 == 1) {
      uint32_t _M0L6_2atmpS1098 = _M0L1nS147 & 15u;
      int32_t _M0L6nibbleS154 = *(int32_t*)&_M0L6_2atmpS1098;
      int32_t _M0L6_2atmpS1097 =
        ((moonbit_string_t)moonbit_string_literal_13.data)[_M0L6nibbleS154];
      _M0L6bufferS151[_M0L12digit__startS152] = _M0L6_2atmpS1097;
    }
    break;
  }
  return 0;
}

int32_t _M0IP016_24default__implPB4Show6outputGiE(
  int32_t _M0L4selfS143,
  struct _M0TPB6Logger _M0L6loggerS142
) {
  moonbit_string_t _M0L6_2atmpS1086;
  #line 159 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  #line 160 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS1086 = _M0IPC13int3IntPB4Show10to__string(_M0L4selfS143);
  #line 160 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6loggerS142.$0->$method_0(_M0L6loggerS142.$1, _M0L6_2atmpS1086);
  moonbit_decref(_M0L6_2atmpS1086);
  return 0;
}

int32_t _M0IP016_24default__implPB4Show6outputGmE(
  uint64_t _M0L4selfS145,
  struct _M0TPB6Logger _M0L6loggerS144
) {
  moonbit_string_t _M0L6_2atmpS1087;
  #line 159 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  #line 160 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS1087 = _M0IPC16uint646UInt64PB4Show10to__string(_M0L4selfS145);
  #line 160 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6loggerS144.$0->$method_0(_M0L6loggerS144.$1, _M0L6_2atmpS1087);
  moonbit_decref(_M0L6_2atmpS1087);
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
  moonbit_string_t _M0L8_2afieldS2048;
  #line 92 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
  _M0L8_2afieldS2048 = _M0L4selfS140.$0;
  moonbit_incref(_M0L8_2afieldS2048);
  return _M0L8_2afieldS2048;
}

int32_t _M0IP016_24default__implPB6Logger16write__substringGRPB13StringBuilderE(
  struct _M0TPB13StringBuilder* _M0L4selfS136,
  moonbit_string_t _M0L5valueS137,
  int32_t _M0L5startS138,
  int32_t _M0L3lenS139
) {
  int32_t _M0L6_2atmpS1085;
  int64_t _M0L6_2atmpS1084;
  struct _M0TPC16string10StringView _M0L6_2atmpS1083;
  #line 122 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS1085 = _M0L5startS138 + _M0L3lenS139;
  _M0L6_2atmpS1084 = (int64_t)_M0L6_2atmpS1085;
  #line 123 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS1083
  = _M0MPC16string6String11sub_2einner(_M0L5valueS137, _M0L5startS138, _M0L6_2atmpS1084);
  #line 123 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0IPB13StringBuilderPB6Logger11write__view(_M0L4selfS136, _M0L6_2atmpS1083);
  moonbit_decref(_M0L6_2atmpS1083.$0);
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
  goto joinlet_2140;
  join_130:;
  _M0L3endS129 = _M0L3endS131;
  joinlet_2140:;
  if (
    _M0L5startS135 >= 0
    && _M0L5startS135 <= _M0L3endS129
    && _M0L3endS129 <= _M0L3lenS127
  ) {
    if (_M0L5startS135 < _M0L3lenS127) {
      int32_t _M0L6_2atmpS1080 = _M0L4selfS128[_M0L5startS135];
      int32_t _M0L6_2atmpS1079;
      #line 931 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
      _M0L6_2atmpS1079
      = _M0MPC16uint166UInt1623is__trailing__surrogate(_M0L6_2atmpS1080);
      if (!_M0L6_2atmpS1079) {
        
      } else {
        #line 931 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
        moonbit_panic();
      }
    }
    if (_M0L3endS129 < _M0L3lenS127) {
      int32_t _M0L6_2atmpS1082 = _M0L4selfS128[_M0L3endS129];
      int32_t _M0L6_2atmpS1081;
      #line 934 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
      _M0L6_2atmpS1081
      = _M0MPC16uint166UInt1623is__trailing__surrogate(_M0L6_2atmpS1082);
      if (!_M0L6_2atmpS1081) {
        
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
  struct _M0TPB6Logger _M0L6_2atmpS1078;
  #line 116 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  moonbit_incref(_M0L4selfS126);
  _M0L6_2atmpS1078
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS126
  };
  #line 117 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L4showS125.$0->$method_0(_M0L4showS125.$1, _M0L6_2atmpS1078);
  if (_M0L6_2atmpS1078.$1) {
    moonbit_decref(_M0L6_2atmpS1078.$1);
  }
  return 0;
}

int32_t _M0IP016_24default__implPB6Logger28write__string__interpolationGRPB13StringBuilderE(
  struct _M0TPB13StringBuilder* _M0L4selfS124,
  struct _M0TPB4Show _M0L4showS123
) {
  struct _M0TPB6Logger _M0L6_2atmpS1077;
  #line 111 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  moonbit_incref(_M0L4selfS124);
  _M0L6_2atmpS1077
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS124
  };
  #line 112 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L4showS123.$0->$method_0(_M0L4showS123.$1, _M0L6_2atmpS1077);
  if (_M0L6_2atmpS1077.$1) {
    moonbit_decref(_M0L6_2atmpS1077.$1);
  }
  return 0;
}

uint64_t _M0MPC13int3Int10to__uint64(int32_t _M0L4selfS122) {
  int64_t _M0L6_2atmpS1076;
  #line 989 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS1076 = (int64_t)_M0L4selfS122;
  return *(uint64_t*)&_M0L6_2atmpS1076;
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
  int32_t _M0L3lenS1075;
  int32_t _M0L8requiredS119;
  uint16_t* _M0L4dataS1070;
  int32_t _M0L6_2atmpS1069;
  int32_t _if__result_2141;
  uint16_t* _M0L4dataS1071;
  int32_t _M0L3lenS1072;
  int32_t _M0L3lenS1074;
  int32_t _M0L6_2atmpS1073;
  #line 105 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L8str__lenS117 = Moonbit_array_length(_M0L3strS118);
  if (_M0L8str__lenS117 == 0) {
    return 0;
  }
  _M0L3lenS1075 = _M0L4selfS120->$1;
  _M0L8requiredS119 = _M0L3lenS1075 + _M0L8str__lenS117;
  _M0L4dataS1070 = _M0L4selfS120->$0;
  _M0L6_2atmpS1069 = Moonbit_array_length(_M0L4dataS1070);
  if (_M0L8requiredS119 > _M0L6_2atmpS1069) {
    _if__result_2141 = 1;
  } else {
    int32_t _M0L3lenS1068 = _M0L4selfS120->$1;
    _if__result_2141 = _M0L8requiredS119 < _M0L3lenS1068;
  }
  if (_if__result_2141) {
    #line 112 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0MPB13StringBuilder4grow(_M0L4selfS120, _M0L8requiredS119);
  }
  _M0L4dataS1071 = _M0L4selfS120->$0;
  _M0L3lenS1072 = _M0L4selfS120->$1;
  moonbit_incref(_M0L4dataS1071);
  #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0MPC15array10FixedArray26unsafe__blit__from__string(_M0L4dataS1071, _M0L3lenS1072, _M0L3strS118, 0, _M0L8str__lenS117);
  moonbit_decref(_M0L4dataS1071);
  _M0L3lenS1074 = _M0L4selfS120->$1;
  _M0L6_2atmpS1073 = _M0L3lenS1074 + _M0L8str__lenS117;
  _M0L4selfS120->$1 = _M0L6_2atmpS1073;
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
      int32_t _M0L6_2atmpS1065 = _M0L3strS114[_M0L1iS111];
      int32_t _M0L6_2atmpS1066;
      int32_t _M0L6_2atmpS1067;
      _M0L4selfS113[_M0L1jS112] = _M0L6_2atmpS1065;
      _M0L6_2atmpS1066 = _M0L1iS111 + 1;
      _M0L6_2atmpS1067 = _M0L1jS112 + 1;
      _M0L1iS111 = _M0L6_2atmpS1066;
      _M0L1jS112 = _M0L6_2atmpS1067;
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
    int32_t _M0L3lenS1036 = _M0L4selfS106->$1;
    uint16_t* _M0L4dataS1038 = _M0L4selfS106->$0;
    int32_t _M0L6_2atmpS1037 = Moonbit_array_length(_M0L4dataS1038);
    uint16_t* _M0L4dataS1041;
    int32_t _M0L3lenS1042;
    int32_t _M0L6_2atmpS1043;
    int32_t _M0L3lenS1045;
    int32_t _M0L6_2atmpS1044;
    if (_M0L3lenS1036 >= _M0L6_2atmpS1037) {
      int32_t _M0L3lenS1040 = _M0L4selfS106->$1;
      int32_t _M0L6_2atmpS1039 = _M0L3lenS1040 + 1;
      #line 124 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0MPB13StringBuilder4grow(_M0L4selfS106, _M0L6_2atmpS1039);
    }
    _M0L4dataS1041 = _M0L4selfS106->$0;
    _M0L3lenS1042 = _M0L4selfS106->$1;
    moonbit_incref(_M0L4dataS1041);
    #line 126 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0L6_2atmpS1043 = _M0MPC14uint4UInt10to__uint16(_M0L4codeS104);
    if (
      _M0L3lenS1042 < 0
      || _M0L3lenS1042 >= Moonbit_array_length(_M0L4dataS1041)
    ) {
      #line 126 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      moonbit_panic();
    }
    _M0L4dataS1041[_M0L3lenS1042] = _M0L6_2atmpS1043;
    moonbit_decref(_M0L4dataS1041);
    _M0L3lenS1045 = _M0L4selfS106->$1;
    _M0L6_2atmpS1044 = _M0L3lenS1045 + 1;
    _M0L4selfS106->$1 = _M0L6_2atmpS1044;
  } else if (_M0L4codeS104 <= 1114111u) {
    uint16_t* _M0L4dataS1049 = _M0L4selfS106->$0;
    int32_t _M0L6_2atmpS1047 = Moonbit_array_length(_M0L4dataS1049);
    int32_t _M0L3lenS1048 = _M0L4selfS106->$1;
    int32_t _M0L6_2atmpS1046 = _M0L6_2atmpS1047 - _M0L3lenS1048;
    uint32_t _M0L4codeS107;
    uint16_t* _M0L4dataS1052;
    int32_t _M0L3lenS1053;
    uint32_t _M0L6_2atmpS1056;
    uint32_t _M0L6_2atmpS1055;
    int32_t _M0L6_2atmpS1054;
    uint16_t* _M0L4dataS1057;
    int32_t _M0L3lenS1062;
    int32_t _M0L6_2atmpS1058;
    uint32_t _M0L6_2atmpS1061;
    uint32_t _M0L6_2atmpS1060;
    int32_t _M0L6_2atmpS1059;
    int32_t _M0L3lenS1064;
    int32_t _M0L6_2atmpS1063;
    if (_M0L6_2atmpS1046 < 2) {
      int32_t _M0L3lenS1051 = _M0L4selfS106->$1;
      int32_t _M0L6_2atmpS1050 = _M0L3lenS1051 + 2;
      #line 130 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0MPB13StringBuilder4grow(_M0L4selfS106, _M0L6_2atmpS1050);
    }
    _M0L4codeS107 = _M0L4codeS104 - 65536u;
    _M0L4dataS1052 = _M0L4selfS106->$0;
    _M0L3lenS1053 = _M0L4selfS106->$1;
    _M0L6_2atmpS1056 = _M0L4codeS107 >> 10;
    _M0L6_2atmpS1055 = 55296u + _M0L6_2atmpS1056;
    moonbit_incref(_M0L4dataS1052);
    #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0L6_2atmpS1054 = _M0MPC14uint4UInt10to__uint16(_M0L6_2atmpS1055);
    if (
      _M0L3lenS1053 < 0
      || _M0L3lenS1053 >= Moonbit_array_length(_M0L4dataS1052)
    ) {
      #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      moonbit_panic();
    }
    _M0L4dataS1052[_M0L3lenS1053] = _M0L6_2atmpS1054;
    moonbit_decref(_M0L4dataS1052);
    _M0L4dataS1057 = _M0L4selfS106->$0;
    _M0L3lenS1062 = _M0L4selfS106->$1;
    _M0L6_2atmpS1058 = _M0L3lenS1062 + 1;
    _M0L6_2atmpS1061 = _M0L4codeS107 & 1023u;
    _M0L6_2atmpS1060 = 56320u + _M0L6_2atmpS1061;
    moonbit_incref(_M0L4dataS1057);
    #line 134 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0L6_2atmpS1059 = _M0MPC14uint4UInt10to__uint16(_M0L6_2atmpS1060);
    if (
      _M0L6_2atmpS1058 < 0
      || _M0L6_2atmpS1058 >= Moonbit_array_length(_M0L4dataS1057)
    ) {
      #line 134 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      moonbit_panic();
    }
    _M0L4dataS1057[_M0L6_2atmpS1058] = _M0L6_2atmpS1059;
    moonbit_decref(_M0L4dataS1057);
    _M0L3lenS1064 = _M0L4selfS106->$1;
    _M0L6_2atmpS1063 = _M0L3lenS1064 + 2;
    _M0L4selfS106->$1 = _M0L6_2atmpS1063;
  } else {
    #line 137 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_14.data);
  }
  return 0;
}

int32_t _M0MPB13StringBuilder4grow(
  struct _M0TPB13StringBuilder* _M0L4selfS101,
  int32_t _M0L8requiredS102
) {
  uint16_t* _M0L4dataS1035;
  int32_t _M0L6_2atmpS1033;
  int32_t _M0L3lenS1034;
  int32_t _M0L13new__capacityS100;
  uint16_t* _M0L4dataS1030;
  int32_t _M0L6_2atmpS1031;
  int32_t _M0L3lenS1032;
  uint16_t* _M0L9new__dataS103;
  uint16_t* _M0L6_2aoldS2049;
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L4dataS1035 = _M0L4selfS101->$0;
  _M0L6_2atmpS1033 = Moonbit_array_length(_M0L4dataS1035);
  _M0L3lenS1034 = _M0L4selfS101->$1;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L13new__capacityS100
  = _M0FPB31stringbuilder__growth__capacity(_M0L6_2atmpS1033, _M0L3lenS1034, _M0L8requiredS102);
  _M0L4dataS1030 = _M0L4selfS101->$0;
  moonbit_incref(_M0L4dataS1030);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L6_2atmpS1031 = _M0IPC16uint166UInt16PB7Default7default();
  _M0L3lenS1032 = _M0L4selfS101->$1;
  #line 80 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L9new__dataS103
  = _M0MPC15array10FixedArray23make__and__blit_2einnerGkE(_M0L4dataS1030, _M0L13new__capacityS100, _M0L6_2atmpS1031, _M0L3lenS1032, 0, 0);
  _M0L6_2aoldS2049 = _M0L4selfS101->$0;
  moonbit_decref(_M0L6_2aoldS2049);
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
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_15.data);
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
  int32_t _M0L6_2atmpS1029;
  #line 2785 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS1029 = *(int32_t*)&_M0L4selfS93;
  return (uint16_t)_M0L6_2atmpS1029;
}

uint32_t _M0MPC14char4Char8to__uint(int32_t _M0L4selfS92) {
  int32_t _M0L6_2atmpS1028;
  #line 1335 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS1028 = _M0L4selfS92;
  return *(uint32_t*)&_M0L6_2atmpS1028;
}

moonbit_string_t _M0MPB13StringBuilder10to__string(
  struct _M0TPB13StringBuilder* _M0L4selfS90
) {
  int32_t _M0L3lenS1019;
  #line 181 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L3lenS1019 = _M0L4selfS90->$1;
  if (_M0L3lenS1019 == 0) {
    return (moonbit_string_t)moonbit_string_literal_8.data;
  } else {
    int32_t _M0L3lenS1020 = _M0L4selfS90->$1;
    uint16_t* _M0L4dataS1022 = _M0L4selfS90->$0;
    int32_t _M0L6_2atmpS1021 = Moonbit_array_length(_M0L4dataS1022);
    if (_M0L3lenS1020 == _M0L6_2atmpS1021) {
      uint16_t* _M0L4dataS1023 = _M0L4selfS90->$0;
      moonbit_incref(_M0L4dataS1023);
      return _M0L4dataS1023;
    } else {
      uint16_t* _M0L4dataS1024 = _M0L4selfS90->$0;
      int32_t _M0L3lenS1025 = _M0L4selfS90->$1;
      int32_t _M0L6_2atmpS1026;
      int32_t _M0L3lenS1027;
      uint16_t* _M0L4dataS91;
      moonbit_incref(_M0L4dataS1024);
      #line 190 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0L6_2atmpS1026 = _M0IPC16uint166UInt16PB7Default7default();
      _M0L3lenS1027 = _M0L4selfS90->$1;
      #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0L4dataS91
      = _M0MPC15array10FixedArray23make__and__blit_2einnerGkE(_M0L4dataS1024, _M0L3lenS1025, _M0L6_2atmpS1026, _M0L3lenS1027, 0, 0);
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
  int32_t _if__result_2144;
  #line 97 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
  if (_M0L13allocate__lenS83 >= 0) {
    if (_M0L3lenS84 >= 0) {
      if (_M0L11src__offsetS85 >= 0) {
        if (_M0L11dst__offsetS86 >= 0) {
          int32_t _M0L6_2atmpS1015 = _M0L11src__offsetS85 + _M0L3lenS84;
          int32_t _M0L6_2atmpS1016 = Moonbit_array_length(_M0L3srcS87);
          if (_M0L6_2atmpS1015 <= _M0L6_2atmpS1016) {
            int32_t _M0L6_2atmpS1014 = _M0L11dst__offsetS86 + _M0L3lenS84;
            _if__result_2144 = _M0L6_2atmpS1014 <= _M0L13allocate__lenS83;
          } else {
            _if__result_2144 = 0;
          }
        } else {
          _if__result_2144 = 0;
        }
      } else {
        _if__result_2144 = 0;
      }
    } else {
      _if__result_2144 = 0;
    }
  } else {
    _if__result_2144 = 0;
  }
  if (_if__result_2144) {
    #line 116 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    return _M0MPC15array10FixedArray23unsafe__make__and__blitGkE(_M0L3srcS87, _M0L13allocate__lenS83, _M0L4initS88, _M0L11src__offsetS85, _M0L11dst__offsetS86, _M0L3lenS84);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS89;
    int32_t _M0L6_2atmpS1018;
    moonbit_string_t _M0L6_2atmpS1017;
    uint16_t* _result_2145;
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0L18_2astring__builderS89
    = _M0MPB13StringBuilder21StringBuilder_2einner(89);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS89, (moonbit_string_t)moonbit_string_literal_16.data);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS89, _M0L13allocate__lenS83);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS89, (moonbit_string_t)moonbit_string_literal_17.data);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS89, _M0L11src__offsetS85);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS89, (moonbit_string_t)moonbit_string_literal_18.data);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS89, _M0L11dst__offsetS86);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS89, (moonbit_string_t)moonbit_string_literal_19.data);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS89, _M0L3lenS84);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS89, (moonbit_string_t)moonbit_string_literal_20.data);
    _M0L6_2atmpS1018 = Moonbit_array_length(_M0L3srcS87);
    moonbit_decref(_M0L3srcS87);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS89, _M0L6_2atmpS1018);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0L6_2atmpS1017
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS89);
    moonbit_decref(_M0L18_2astring__builderS89);
    #line 112 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _result_2145 = _M0FPC15abort5abortGAkE(_M0L6_2atmpS1017);
    moonbit_decref(_M0L6_2atmpS1017);
    return _result_2145;
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
  struct _M0TPB13StringBuilder* _block_2146;
  #line 32 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  if (_M0L10size__hintS74 < 1) {
    _M0L7initialS73 = 1;
  } else {
    int32_t _M0L6_2atmpS1013 = _M0L10size__hintS74 + 1;
    _M0L7initialS73 = _M0L6_2atmpS1013 / 2;
  }
  _M0L4dataS75 = (uint16_t*)moonbit_make_string(_M0L7initialS73, 0);
  _block_2146
  = (struct _M0TPB13StringBuilder*)moonbit_malloc(sizeof(struct _M0TPB13StringBuilder));
  Moonbit_object_header(_block_2146)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 59, 0);
  _block_2146->$0 = _M0L4dataS75;
  _block_2146->$1 = 0;
  return _block_2146;
}

int32_t* _M0MPB18UninitializedArray23make__and__blit_2einnerGiE(
  int32_t* _M0L3srcS65,
  int32_t _M0L13allocate__lenS61,
  int32_t _M0L3lenS62,
  int32_t _M0L11src__offsetS63,
  int32_t _M0L11dst__offsetS64
) {
  int32_t _if__result_2147;
  #line 167 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  if (_M0L13allocate__lenS61 >= 0) {
    if (_M0L3lenS62 >= 0) {
      if (_M0L11src__offsetS63 >= 0) {
        if (_M0L11dst__offsetS64 >= 0) {
          int32_t _M0L6_2atmpS1004 = _M0L11src__offsetS63 + _M0L3lenS62;
          int32_t _M0L6_2atmpS1005;
          #line 179 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
          _M0L6_2atmpS1005
          = _M0MPB18UninitializedArray6lengthGiE(_M0L3srcS65);
          if (_M0L6_2atmpS1004 <= _M0L6_2atmpS1005) {
            int32_t _M0L6_2atmpS1003 = _M0L11dst__offsetS64 + _M0L3lenS62;
            _if__result_2147 = _M0L6_2atmpS1003 <= _M0L13allocate__lenS61;
          } else {
            _if__result_2147 = 0;
          }
        } else {
          _if__result_2147 = 0;
        }
      } else {
        _if__result_2147 = 0;
      }
    } else {
      _if__result_2147 = 0;
    }
  } else {
    _if__result_2147 = 0;
  }
  if (_if__result_2147) {
    #line 185 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    return _M0MPB18UninitializedArray23unsafe__make__and__blitGiE(_M0L3srcS65, _M0L13allocate__lenS61, _M0L11src__offsetS63, _M0L11dst__offsetS64, _M0L3lenS62);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS66;
    int32_t _M0L6_2atmpS1007;
    moonbit_string_t _M0L6_2atmpS1006;
    int32_t* _result_2148;
    #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L18_2astring__builderS66
    = _M0MPB13StringBuilder21StringBuilder_2einner(89);
    #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS66, (moonbit_string_t)moonbit_string_literal_16.data);
    #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS66, _M0L13allocate__lenS61);
    #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS66, (moonbit_string_t)moonbit_string_literal_17.data);
    #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS66, _M0L11src__offsetS63);
    #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS66, (moonbit_string_t)moonbit_string_literal_18.data);
    #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS66, _M0L11dst__offsetS64);
    #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS66, (moonbit_string_t)moonbit_string_literal_19.data);
    #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS66, _M0L3lenS62);
    #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS66, (moonbit_string_t)moonbit_string_literal_20.data);
    #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS1007 = _M0MPB18UninitializedArray6lengthGiE(_M0L3srcS65);
    moonbit_decref(_M0L3srcS65);
    #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS66, _M0L6_2atmpS1007);
    #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS1006
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS66);
    moonbit_decref(_M0L18_2astring__builderS66);
    #line 181 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _result_2148
    = _M0FPC15abort5abortGRPB18UninitializedArrayGiEE(_M0L6_2atmpS1006);
    moonbit_decref(_M0L6_2atmpS1006);
    return _result_2148;
  }
}

float* _M0MPB18UninitializedArray23make__and__blit_2einnerGfE(
  float* _M0L3srcS71,
  int32_t _M0L13allocate__lenS67,
  int32_t _M0L3lenS68,
  int32_t _M0L11src__offsetS69,
  int32_t _M0L11dst__offsetS70
) {
  int32_t _if__result_2149;
  #line 167 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  if (_M0L13allocate__lenS67 >= 0) {
    if (_M0L3lenS68 >= 0) {
      if (_M0L11src__offsetS69 >= 0) {
        if (_M0L11dst__offsetS70 >= 0) {
          int32_t _M0L6_2atmpS1009 = _M0L11src__offsetS69 + _M0L3lenS68;
          int32_t _M0L6_2atmpS1010;
          #line 179 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
          _M0L6_2atmpS1010
          = _M0MPB18UninitializedArray6lengthGfE(_M0L3srcS71);
          if (_M0L6_2atmpS1009 <= _M0L6_2atmpS1010) {
            int32_t _M0L6_2atmpS1008 = _M0L11dst__offsetS70 + _M0L3lenS68;
            _if__result_2149 = _M0L6_2atmpS1008 <= _M0L13allocate__lenS67;
          } else {
            _if__result_2149 = 0;
          }
        } else {
          _if__result_2149 = 0;
        }
      } else {
        _if__result_2149 = 0;
      }
    } else {
      _if__result_2149 = 0;
    }
  } else {
    _if__result_2149 = 0;
  }
  if (_if__result_2149) {
    #line 185 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    return _M0MPB18UninitializedArray23unsafe__make__and__blitGfE(_M0L3srcS71, _M0L13allocate__lenS67, _M0L11src__offsetS69, _M0L11dst__offsetS70, _M0L3lenS68);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS72;
    int32_t _M0L6_2atmpS1012;
    moonbit_string_t _M0L6_2atmpS1011;
    float* _result_2150;
    #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L18_2astring__builderS72
    = _M0MPB13StringBuilder21StringBuilder_2einner(89);
    #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS72, (moonbit_string_t)moonbit_string_literal_16.data);
    #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS72, _M0L13allocate__lenS67);
    #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS72, (moonbit_string_t)moonbit_string_literal_17.data);
    #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS72, _M0L11src__offsetS69);
    #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS72, (moonbit_string_t)moonbit_string_literal_18.data);
    #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS72, _M0L11dst__offsetS70);
    #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS72, (moonbit_string_t)moonbit_string_literal_19.data);
    #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS72, _M0L3lenS68);
    #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS72, (moonbit_string_t)moonbit_string_literal_20.data);
    #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS1012 = _M0MPB18UninitializedArray6lengthGfE(_M0L3srcS71);
    moonbit_decref(_M0L3srcS71);
    #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS72, _M0L6_2atmpS1012);
    #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS1011
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS72);
    moonbit_decref(_M0L18_2astring__builderS72);
    #line 181 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _result_2150
    = _M0FPC15abort5abortGRPB18UninitializedArrayGfEE(_M0L6_2atmpS1011);
    moonbit_decref(_M0L6_2atmpS1011);
    return _result_2150;
  }
}

int32_t _M0MPB13StringBuilder13write__objectGiE(
  struct _M0TPB13StringBuilder* _M0L4selfS58,
  int32_t _M0L3objS57
) {
  struct _M0TPB6Logger _M0L6_2atmpS1001;
  #line 17 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  moonbit_incref(_M0L4selfS58);
  _M0L6_2atmpS1001
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS58
  };
  #line 23 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  _M0IP016_24default__implPB4Show6outputGiE(_M0L3objS57, _M0L6_2atmpS1001);
  if (_M0L6_2atmpS1001.$1) {
    moonbit_decref(_M0L6_2atmpS1001.$1);
  }
  return 0;
}

int32_t _M0MPB13StringBuilder13write__objectGmE(
  struct _M0TPB13StringBuilder* _M0L4selfS60,
  uint64_t _M0L3objS59
) {
  struct _M0TPB6Logger _M0L6_2atmpS1002;
  #line 17 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  moonbit_incref(_M0L4selfS60);
  _M0L6_2atmpS1002
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS60
  };
  #line 23 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  _M0IP016_24default__implPB4Show6outputGmE(_M0L3objS59, _M0L6_2atmpS1002);
  if (_M0L6_2atmpS1002.$1) {
    moonbit_decref(_M0L6_2atmpS1002.$1);
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
        int32_t _M0L6_2atmpS974 = _M0L11dst__offsetS10 + _M0L1iS12;
        int32_t _M0L6_2atmpS976 = _M0L11src__offsetS11 + _M0L1iS12;
        int32_t _M0L6_2atmpS975;
        int32_t _M0L6_2atmpS977;
        if (
          _M0L6_2atmpS976 < 0
          || _M0L6_2atmpS976 >= Moonbit_array_length(_M0L3srcS9)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS975 = (int32_t)_M0L3srcS9[_M0L6_2atmpS976];
        if (
          _M0L6_2atmpS974 < 0
          || _M0L6_2atmpS974 >= Moonbit_array_length(_M0L3dstS8)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS8[_M0L6_2atmpS974] = _M0L6_2atmpS975;
        _M0L6_2atmpS977 = _M0L1iS12 + 1;
        _M0L1iS12 = _M0L6_2atmpS977;
        continue;
      } else {
        moonbit_decref(_M0L3srcS9);
        moonbit_decref(_M0L3dstS8);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS982 = _M0L3lenS13 - 1;
    int32_t _M0L1iS15 = _M0L6_2atmpS982;
    while (1) {
      if (_M0L1iS15 >= 0) {
        int32_t _M0L6_2atmpS978 = _M0L11dst__offsetS10 + _M0L1iS15;
        int32_t _M0L6_2atmpS980 = _M0L11src__offsetS11 + _M0L1iS15;
        int32_t _M0L6_2atmpS979;
        int32_t _M0L6_2atmpS981;
        if (
          _M0L6_2atmpS980 < 0
          || _M0L6_2atmpS980 >= Moonbit_array_length(_M0L3srcS9)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS979 = (int32_t)_M0L3srcS9[_M0L6_2atmpS980];
        if (
          _M0L6_2atmpS978 < 0
          || _M0L6_2atmpS978 >= Moonbit_array_length(_M0L3dstS8)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS8[_M0L6_2atmpS978] = _M0L6_2atmpS979;
        _M0L6_2atmpS981 = _M0L1iS15 - 1;
        _M0L1iS15 = _M0L6_2atmpS981;
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
        int32_t _M0L6_2atmpS983 = _M0L11dst__offsetS19 + _M0L1iS21;
        int32_t _M0L6_2atmpS985 = _M0L11src__offsetS20 + _M0L1iS21;
        float _M0L6_2atmpS984;
        int32_t _M0L6_2atmpS986;
        if (
          _M0L6_2atmpS985 < 0
          || _M0L6_2atmpS985 >= Moonbit_array_length(_M0L3srcS18)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS984 = (float)_M0L3srcS18[_M0L6_2atmpS985];
        if (
          _M0L6_2atmpS983 < 0
          || _M0L6_2atmpS983 >= Moonbit_array_length(_M0L3dstS17)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS17[_M0L6_2atmpS983] = _M0L6_2atmpS984;
        _M0L6_2atmpS986 = _M0L1iS21 + 1;
        _M0L1iS21 = _M0L6_2atmpS986;
        continue;
      } else {
        moonbit_decref(_M0L3srcS18);
        moonbit_decref(_M0L3dstS17);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS991 = _M0L3lenS22 - 1;
    int32_t _M0L1iS24 = _M0L6_2atmpS991;
    while (1) {
      if (_M0L1iS24 >= 0) {
        int32_t _M0L6_2atmpS987 = _M0L11dst__offsetS19 + _M0L1iS24;
        int32_t _M0L6_2atmpS989 = _M0L11src__offsetS20 + _M0L1iS24;
        float _M0L6_2atmpS988;
        int32_t _M0L6_2atmpS990;
        if (
          _M0L6_2atmpS989 < 0
          || _M0L6_2atmpS989 >= Moonbit_array_length(_M0L3srcS18)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS988 = (float)_M0L3srcS18[_M0L6_2atmpS989];
        if (
          _M0L6_2atmpS987 < 0
          || _M0L6_2atmpS987 >= Moonbit_array_length(_M0L3dstS17)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS17[_M0L6_2atmpS987] = _M0L6_2atmpS988;
        _M0L6_2atmpS990 = _M0L1iS24 - 1;
        _M0L1iS24 = _M0L6_2atmpS990;
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
        int32_t _M0L6_2atmpS992 = _M0L11dst__offsetS28 + _M0L1iS30;
        int32_t _M0L6_2atmpS994 = _M0L11src__offsetS29 + _M0L1iS30;
        int32_t _M0L6_2atmpS993;
        int32_t _M0L6_2atmpS995;
        if (
          _M0L6_2atmpS994 < 0
          || _M0L6_2atmpS994 >= Moonbit_array_length(_M0L3srcS27)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS993 = (int32_t)_M0L3srcS27[_M0L6_2atmpS994];
        if (
          _M0L6_2atmpS992 < 0
          || _M0L6_2atmpS992 >= Moonbit_array_length(_M0L3dstS26)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS26[_M0L6_2atmpS992] = _M0L6_2atmpS993;
        _M0L6_2atmpS995 = _M0L1iS30 + 1;
        _M0L1iS30 = _M0L6_2atmpS995;
        continue;
      } else {
        moonbit_decref(_M0L3srcS27);
        moonbit_decref(_M0L3dstS26);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1000 = _M0L3lenS31 - 1;
    int32_t _M0L1iS33 = _M0L6_2atmpS1000;
    while (1) {
      if (_M0L1iS33 >= 0) {
        int32_t _M0L6_2atmpS996 = _M0L11dst__offsetS28 + _M0L1iS33;
        int32_t _M0L6_2atmpS998 = _M0L11src__offsetS29 + _M0L1iS33;
        int32_t _M0L6_2atmpS997;
        int32_t _M0L6_2atmpS999;
        if (
          _M0L6_2atmpS998 < 0
          || _M0L6_2atmpS998 >= Moonbit_array_length(_M0L3srcS27)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS997 = (int32_t)_M0L3srcS27[_M0L6_2atmpS998];
        if (
          _M0L6_2atmpS996 < 0
          || _M0L6_2atmpS996 >= Moonbit_array_length(_M0L3dstS26)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS26[_M0L6_2atmpS996] = _M0L6_2atmpS997;
        _M0L6_2atmpS999 = _M0L1iS33 - 1;
        _M0L1iS33 = _M0L6_2atmpS999;
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
  void* _M0L11_2aobj__ptrS919,
  struct _M0TPB4Show _M0L8_2aparamS918
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS917 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS919;
  _M0IP016_24default__implPB6Logger5writeGRPB13StringBuilderE(_M0L7_2aselfS917, _M0L8_2aparamS918);
  return 0;
}

int32_t _M0IP016_24default__implPB6Logger84write__string__interpolation_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLoggerGRPB13StringBuilderE(
  void* _M0L11_2aobj__ptrS916,
  struct _M0TPB4Show _M0L8_2aparamS915
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS914 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS916;
  _M0IP016_24default__implPB6Logger28write__string__interpolationGRPB13StringBuilderE(_M0L7_2aselfS914, _M0L8_2aparamS915);
  return 0;
}

int32_t _M0IPB13StringBuilderPB6Logger67write__char_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLogger(
  void* _M0L11_2aobj__ptrS913,
  int32_t _M0L8_2aparamS912
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS911 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS913;
  _M0IPB13StringBuilderPB6Logger11write__char(_M0L7_2aselfS911, _M0L8_2aparamS912);
  return 0;
}

int32_t _M0IPB13StringBuilderPB6Logger67write__view_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLogger(
  void* _M0L11_2aobj__ptrS910,
  struct _M0TPC16string10StringView _M0L8_2aparamS909
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS908 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS910;
  _M0IPB13StringBuilderPB6Logger11write__view(_M0L7_2aselfS908, _M0L8_2aparamS909);
  return 0;
}

int32_t _M0IP016_24default__implPB6Logger72write__substring_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLoggerGRPB13StringBuilderE(
  void* _M0L11_2aobj__ptrS907,
  moonbit_string_t _M0L8_2aparamS904,
  int32_t _M0L8_2aparamS905,
  int32_t _M0L8_2aparamS906
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS903 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS907;
  _M0IP016_24default__implPB6Logger16write__substringGRPB13StringBuilderE(_M0L7_2aselfS903, _M0L8_2aparamS904, _M0L8_2aparamS905, _M0L8_2aparamS906);
  return 0;
}

int32_t _M0IPB13StringBuilderPB6Logger69write__string_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLogger(
  void* _M0L11_2aobj__ptrS902,
  moonbit_string_t _M0L8_2aparamS901
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS900 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS902;
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L7_2aselfS900, _M0L8_2aparamS901);
  return 0;
}

void moonbit_init() {
  moonbit_layout_table = moonbit_layout_table_data;
}

int main(int argc, char** argv) {
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS861;
  int32_t _M0L2neS862;
  int32_t _M0L2niS863;
  float _M0L2dtS864;
  struct _M0TP26RiantR8snn__mbt19AdExSinExpParameter* _M0L8e__paramS865;
  struct _M0TP26RiantR8snn__mbt10AdExSinExp* _M0L6e__popS866;
  int32_t _M0L7_2abindS867;
  int32_t _M0L1kS868;
  struct _M0TP26RiantR8snn__mbt19AdExSinExpParameter* _M0L7_2abindS963;
  float _M0L11_2afield__0S964;
  float _M0L11_2afield__1S965;
  float _M0L11_2afield__2S966;
  float _M0L11_2afield__3S967;
  float _M0L11_2afield__5S968;
  float _M0L11_2afield__6S969;
  float _M0L11_2afield__7S970;
  float _M0L11_2afield__8S971;
  float _M0L11_2afield__9S972;
  float _M0L12_2afield__10S973;
  struct _M0TP26RiantR8snn__mbt19AdExSinExpParameter* _M0L8i__paramS870;
  struct _M0TP26RiantR8snn__mbt10AdExSinExp* _M0L6i__popS871;
  int32_t _M0L7_2abindS872;
  int32_t _M0L1kS873;
  struct _M0TP26RiantR8snn__mbt11IFParameter* _M0L6_2atmpS962;
  struct _M0TP26RiantR8snn__mbt2IF* _M0L6e__preS875;
  struct _M0TP26RiantR8snn__mbt11IFParameter* _M0L6_2atmpS961;
  struct _M0TP26RiantR8snn__mbt2IF* _M0L6i__preS876;
  struct _M0TPB5ArrayGfE* _M0L5ee__wS877;
  struct _M0TPB5ArrayGfE* _M0L5ei__wS878;
  struct _M0TPB5ArrayGfE* _M0L5ie__wS879;
  struct _M0TPB5ArrayGfE* _M0L5ii__wS880;
  struct _M0TP26RiantR8snn__mbt17PoissonStimulusIF* _M0L7e__stimS881;
  struct _M0TP26RiantR8snn__mbt17PoissonStimulusIF* _M0L7i__stimS882;
  struct _M0TP26RiantR8snn__mbt17MonitorAdExSinExp* _M0L5fm__eS883;
  struct _M0TP26RiantR8snn__mbt17MonitorAdExSinExp* _M0L5fm__iS884;
  float _M0L12duration__msS885;
  float _M0L6_2atmpS960;
  int32_t _M0L5stepsS886;
  int32_t _M0L7_2abindS887;
  int32_t _M0L4stepS888;
  struct _M0TPB5ArrayGfE* _M0L4dataS959;
  float _M0L5e__hzS891;
  struct _M0TPB5ArrayGfE* _M0L4dataS958;
  float _M0L5i__hzS892;
  struct _M0TPB5ArrayGfE* _M0L4dataS957;
  int32_t _M0L6_2acntS2050;
  int32_t _M0L8e__countS893;
  struct _M0TPB5ArrayGfE* _M0L4dataS956;
  int32_t _M0L6_2acntS2055;
  int32_t _M0L8i__countS894;
  int32_t _M0L9ee__countS895;
  int32_t _M0L9ei__countS896;
  int32_t _M0L9ie__countS897;
  int32_t _M0L9ii__countS898;
  moonbit_string_t _M0L6_2atmpS928;
  moonbit_string_t _M0L6_2atmpS927;
  moonbit_string_t _M0L6_2atmpS926;
  moonbit_string_t _M0L6_2atmpS931;
  moonbit_string_t _M0L6_2atmpS930;
  moonbit_string_t _M0L6_2atmpS929;
  moonbit_string_t _M0L6_2atmpS934;
  moonbit_string_t _M0L6_2atmpS933;
  moonbit_string_t _M0L6_2atmpS932;
  moonbit_string_t _M0L6_2atmpS937;
  moonbit_string_t _M0L6_2atmpS936;
  moonbit_string_t _M0L6_2atmpS935;
  moonbit_string_t _M0L6_2atmpS940;
  moonbit_string_t _M0L6_2atmpS939;
  moonbit_string_t _M0L6_2atmpS938;
  moonbit_string_t _M0L6_2atmpS943;
  moonbit_string_t _M0L6_2atmpS942;
  moonbit_string_t _M0L6_2atmpS941;
  moonbit_string_t _M0L6_2atmpS949;
  moonbit_string_t _M0L6_2atmpS948;
  moonbit_string_t _M0L6_2atmpS946;
  moonbit_string_t _M0L6_2atmpS947;
  moonbit_string_t _M0L6_2atmpS945;
  moonbit_string_t _M0L6_2atmpS944;
  moonbit_string_t _M0L6_2atmpS955;
  moonbit_string_t _M0L6_2atmpS954;
  moonbit_string_t _M0L6_2atmpS952;
  moonbit_string_t _M0L6_2atmpS953;
  moonbit_string_t _M0L6_2atmpS951;
  moonbit_string_t _M0L6_2atmpS950;
  moonbit_runtime_init(argc, argv);
  moonbit_init();
  #line 20 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_adex\\main.mbt"
  _M0L3rngS861 = _M0MP26RiantR8snn__mbt7Xoshiro7default();
  _M0L2neS862 = 40;
  _M0L2niS863 = 10;
  _M0L2dtS864 = 0x1p-3f;
  #line 26 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_adex\\main.mbt"
  _M0L8e__paramS865 = _M0MP26RiantR8snn__mbt19AdExSinExpParameter9lkd__adex();
  #line 27 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_adex\\main.mbt"
  _M0L6e__popS866
  = _M0MP26RiantR8snn__mbt10AdExSinExp3new(_M0L2neS862, _M0L8e__paramS865, _M0L3rngS861);
  moonbit_decref(_M0L8e__paramS865);
  _M0L7_2abindS867 = 0;
  _M0L1kS868 = _M0L7_2abindS867;
  while (1) {
    if (_M0L1kS868 < _M0L2neS862) {
      struct _M0TPB5ArrayGfE* _M0L1iS920 = _M0L6e__popS866->$8;
      int32_t _M0L6_2atmpS921;
      moonbit_incref(_M0L1iS920);
      #line 28 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_adex\\main.mbt"
      _M0MPC15array5Array3setGfE(_M0L1iS920, _M0L1kS868, 0x1.5ep+8f);
      moonbit_decref(_M0L1iS920);
      _M0L6_2atmpS921 = _M0L1kS868 + 1;
      _M0L1kS868 = _M0L6_2atmpS921;
      continue;
    }
    break;
  }
  #line 30 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_adex\\main.mbt"
  _M0L7_2abindS963 = _M0MP26RiantR8snn__mbt19AdExSinExpParameter9lkd__adex();
  _M0L11_2afield__0S964 = _M0L7_2abindS963->$0;
  _M0L11_2afield__1S965 = _M0L7_2abindS963->$1;
  _M0L11_2afield__2S966 = _M0L7_2abindS963->$2;
  _M0L11_2afield__3S967 = _M0L7_2abindS963->$3;
  _M0L11_2afield__5S968 = _M0L7_2abindS963->$5;
  _M0L11_2afield__6S969 = _M0L7_2abindS963->$6;
  _M0L11_2afield__7S970 = _M0L7_2abindS963->$7;
  _M0L11_2afield__8S971 = _M0L7_2abindS963->$8;
  _M0L11_2afield__9S972 = _M0L7_2abindS963->$9;
  _M0L12_2afield__10S973 = _M0L7_2abindS963->$10;
  moonbit_decref(_M0L7_2abindS963);
  _M0L8i__paramS870
  = (struct _M0TP26RiantR8snn__mbt19AdExSinExpParameter*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt19AdExSinExpParameter));
  Moonbit_object_header(_M0L8i__paramS870)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L8i__paramS870->$0 = _M0L11_2afield__0S964;
  _M0L8i__paramS870->$1 = _M0L11_2afield__1S965;
  _M0L8i__paramS870->$2 = _M0L11_2afield__2S966;
  _M0L8i__paramS870->$3 = _M0L11_2afield__3S967;
  _M0L8i__paramS870->$4 = -0x1.fp+5f;
  _M0L8i__paramS870->$5 = _M0L11_2afield__5S968;
  _M0L8i__paramS870->$6 = _M0L11_2afield__6S969;
  _M0L8i__paramS870->$7 = _M0L11_2afield__7S970;
  _M0L8i__paramS870->$8 = _M0L11_2afield__8S971;
  _M0L8i__paramS870->$9 = _M0L11_2afield__9S972;
  _M0L8i__paramS870->$10 = _M0L12_2afield__10S973;
  #line 31 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_adex\\main.mbt"
  _M0L6i__popS871
  = _M0MP26RiantR8snn__mbt10AdExSinExp3new(_M0L2niS863, _M0L8i__paramS870, _M0L3rngS861);
  moonbit_decref(_M0L8i__paramS870);
  _M0L7_2abindS872 = 0;
  _M0L1kS873 = _M0L7_2abindS872;
  while (1) {
    if (_M0L1kS873 < _M0L2niS863) {
      struct _M0TPB5ArrayGfE* _M0L1iS922 = _M0L6i__popS871->$8;
      int32_t _M0L6_2atmpS923;
      moonbit_incref(_M0L1iS922);
      #line 32 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_adex\\main.mbt"
      _M0MPC15array5Array3setGfE(_M0L1iS922, _M0L1kS873, 0x1.f4p+7f);
      moonbit_decref(_M0L1iS922);
      _M0L6_2atmpS923 = _M0L1kS873 + 1;
      _M0L1kS873 = _M0L6_2atmpS923;
      continue;
    }
    break;
  }
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_adex\\main.mbt"
  _M0L6_2atmpS962 = _M0MP26RiantR8snn__mbt11IFParameter3new();
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_adex\\main.mbt"
  _M0L6e__preS875
  = _M0MP26RiantR8snn__mbt2IF3new(_M0L2neS862, _M0L6_2atmpS962, _M0L3rngS861);
  moonbit_decref(_M0L6_2atmpS962);
  #line 36 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_adex\\main.mbt"
  _M0L6_2atmpS961 = _M0MP26RiantR8snn__mbt11IFParameter3new();
  #line 36 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_adex\\main.mbt"
  _M0L6i__preS876
  = _M0MP26RiantR8snn__mbt2IF3new(_M0L2niS863, _M0L6_2atmpS961, _M0L3rngS861);
  moonbit_decref(_M0L6_2atmpS961);
  #line 40 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_adex\\main.mbt"
  _M0L5ee__wS877
  = _M0FP26RiantR8snn__mbt21make__random__weights(_M0L2neS862, _M0L2neS862, 0x1.6147ae147ae14p+1f, 0x1.999999999999ap-3f, _M0L3rngS861);
  #line 42 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_adex\\main.mbt"
  _M0L5ei__wS878
  = _M0FP26RiantR8snn__mbt21make__random__weights(_M0L2niS863, _M0L2neS862, 0x1.451eb851eb852p+0f, 0x1.999999999999ap-3f, _M0L3rngS861);
  #line 44 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_adex\\main.mbt"
  _M0L5ie__wS879
  = _M0FP26RiantR8snn__mbt21make__random__weights(_M0L2neS862, _M0L2niS863, 0x1.859999999999ap+5f, 0x1.999999999999ap-3f, _M0L3rngS861);
  #line 46 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_adex\\main.mbt"
  _M0L5ii__wS880
  = _M0FP26RiantR8snn__mbt21make__random__weights(_M0L2niS863, _M0L2niS863, 0x1.0333333333333p+4f, 0x1.999999999999ap-3f, _M0L3rngS861);
  #line 49 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_adex\\main.mbt"
  _M0L7e__stimS881
  = _M0MP26RiantR8snn__mbt17PoissonStimulusIF3new(_M0L6e__preS875, (moonbit_string_t)moonbit_string_literal_3.data, 0x1.26e978d4fdf3bp-8f, _M0L3rngS861);
  #line 50 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_adex\\main.mbt"
  _M0L7i__stimS882
  = _M0MP26RiantR8snn__mbt17PoissonStimulusIF3new(_M0L6i__preS876, (moonbit_string_t)moonbit_string_literal_3.data, 0x1.47ae147ae147bp-9f, _M0L3rngS861);
  moonbit_decref(_M0L3rngS861);
  #line 53 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_adex\\main.mbt"
  _M0L5fm__eS883
  = _M0MP26RiantR8snn__mbt17MonitorAdExSinExp9new__fire(_M0L6e__popS866, 0);
  #line 54 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_adex\\main.mbt"
  _M0L5fm__iS884
  = _M0MP26RiantR8snn__mbt17MonitorAdExSinExp9new__fire(_M0L6i__popS871, 0);
  _M0L12duration__msS885 = 0x1.f4p+9f;
  _M0L6_2atmpS960 = _M0L12duration__msS885 / _M0L2dtS864;
  #line 58 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_adex\\main.mbt"
  _M0L5stepsS886 = _M0MPC15float5Float7to__int(_M0L6_2atmpS960);
  _M0L7_2abindS887 = 0;
  _M0L4stepS888 = _M0L7_2abindS887;
  while (1) {
    if (_M0L4stepS888 < _M0L5stepsS886) {
      float _M0L6_2atmpS924 = (float)_M0L4stepS888;
      float _M0L6t__nowS889 = _M0L6_2atmpS924 * _M0L2dtS864;
      int32_t _M0L6_2atmpS925;
      #line 62 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_adex\\main.mbt"
      _M0FP26RiantR8snn__mbt13stimulate__if(_M0L7e__stimS881, _M0L6t__nowS889, _M0L2dtS864);
      #line 63 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_adex\\main.mbt"
      _M0FP26RiantR8snn__mbt13stimulate__if(_M0L7i__stimS882, _M0L6t__nowS889, _M0L2dtS864);
      #line 64 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_adex\\main.mbt"
      _M0FP26RiantR8snn__mbt18simulate__step__if(_M0L6e__preS875, _M0L2dtS864);
      #line 65 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_adex\\main.mbt"
      _M0FP26RiantR8snn__mbt18simulate__step__if(_M0L6i__preS876, _M0L2dtS864);
      #line 69 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_adex\\main.mbt"
      _M0FP26RiantR8snn__mbt20route__pre__to__post(_M0L6e__preS875, _M0L6e__popS866, _M0L5ee__wS877, 1, 0);
      #line 70 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_adex\\main.mbt"
      _M0FP26RiantR8snn__mbt20route__pre__to__post(_M0L6i__preS876, _M0L6e__popS866, _M0L5ei__wS878, 0, 1);
      #line 71 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_adex\\main.mbt"
      _M0FP26RiantR8snn__mbt20route__pre__to__post(_M0L6e__preS875, _M0L6i__popS871, _M0L5ie__wS879, 1, 0);
      #line 72 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_adex\\main.mbt"
      _M0FP26RiantR8snn__mbt20route__pre__to__post(_M0L6i__preS876, _M0L6i__popS871, _M0L5ii__wS880, 0, 1);
      #line 74 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_adex\\main.mbt"
      _M0FP26RiantR8snn__mbt28adex__sinexp__step__synapses(_M0L6e__popS866, _M0L2dtS864);
      #line 75 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_adex\\main.mbt"
      _M0FP26RiantR8snn__mbt31adex__sinexp__synaptic__current(_M0L6e__popS866);
      #line 76 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_adex\\main.mbt"
      _M0FP26RiantR8snn__mbt18step__adex__sinexp(_M0L6e__popS866, _M0L2dtS864);
      #line 77 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_adex\\main.mbt"
      _M0FP26RiantR8snn__mbt28adex__sinexp__step__synapses(_M0L6i__popS871, _M0L2dtS864);
      #line 78 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_adex\\main.mbt"
      _M0FP26RiantR8snn__mbt31adex__sinexp__synaptic__current(_M0L6i__popS871);
      #line 79 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_adex\\main.mbt"
      _M0FP26RiantR8snn__mbt18step__adex__sinexp(_M0L6i__popS871, _M0L2dtS864);
      #line 81 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_adex\\main.mbt"
      _M0FP26RiantR8snn__mbt19record__one__sinexp(_M0L5fm__eS883, _M0L6t__nowS889);
      #line 82 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_adex\\main.mbt"
      _M0FP26RiantR8snn__mbt19record__one__sinexp(_M0L5fm__iS884, _M0L6t__nowS889);
      _M0L6_2atmpS925 = _M0L4stepS888 + 1;
      _M0L4stepS888 = _M0L6_2atmpS925;
      continue;
    } else {
      moonbit_decref(_M0L7i__stimS882);
      moonbit_decref(_M0L7e__stimS881);
      moonbit_decref(_M0L6i__preS876);
      moonbit_decref(_M0L6e__preS875);
      moonbit_decref(_M0L6i__popS871);
      moonbit_decref(_M0L6e__popS866);
    }
    break;
  }
  _M0L4dataS959 = _M0L5fm__eS883->$2;
  moonbit_incref(_M0L4dataS959);
  #line 85 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_adex\\main.mbt"
  _M0L5e__hzS891
  = _M0FP26RiantR8snn__mbt21monitor__firing__rate(_M0L4dataS959);
  moonbit_decref(_M0L4dataS959);
  _M0L4dataS958 = _M0L5fm__iS884->$2;
  moonbit_incref(_M0L4dataS958);
  #line 86 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_adex\\main.mbt"
  _M0L5i__hzS892
  = _M0FP26RiantR8snn__mbt21monitor__firing__rate(_M0L4dataS958);
  moonbit_decref(_M0L4dataS958);
  _M0L4dataS957 = _M0L5fm__eS883->$2;
  _M0L6_2acntS2050 = Moonbit_rc_count(Moonbit_object_header(_M0L5fm__eS883));
  if (_M0L6_2acntS2050 > 1) {
    int32_t _M0L11_2anew__cntS2054 = _M0L6_2acntS2050 - 1;
    Moonbit_set_rc_count(Moonbit_object_header(_M0L5fm__eS883), _M0L11_2anew__cntS2054);
    moonbit_incref(_M0L4dataS957);
  } else if (_M0L6_2acntS2050 == 1) {
    struct _M0TPB5ArrayGfE* _M0L8_2afieldS2053 = _M0L5fm__eS883->$3;
    moonbit_string_t _M0L8_2afieldS2052;
    struct _M0TP26RiantR8snn__mbt10AdExSinExp* _M0L8_2afieldS2051;
    moonbit_decref(_M0L8_2afieldS2053);
    _M0L8_2afieldS2052 = _M0L5fm__eS883->$1;
    moonbit_decref(_M0L8_2afieldS2052);
    _M0L8_2afieldS2051 = _M0L5fm__eS883->$0;
    moonbit_decref(_M0L8_2afieldS2051);
    #line 86 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_adex\\main.mbt"
    moonbit_free(_M0L5fm__eS883);
  }
  #line 87 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_adex\\main.mbt"
  _M0L8e__countS893
  = _M0FP26RiantR8snn__mbt22monitor__count__spikes(_M0L4dataS957);
  moonbit_decref(_M0L4dataS957);
  _M0L4dataS956 = _M0L5fm__iS884->$2;
  _M0L6_2acntS2055 = Moonbit_rc_count(Moonbit_object_header(_M0L5fm__iS884));
  if (_M0L6_2acntS2055 > 1) {
    int32_t _M0L11_2anew__cntS2059 = _M0L6_2acntS2055 - 1;
    Moonbit_set_rc_count(Moonbit_object_header(_M0L5fm__iS884), _M0L11_2anew__cntS2059);
    moonbit_incref(_M0L4dataS956);
  } else if (_M0L6_2acntS2055 == 1) {
    struct _M0TPB5ArrayGfE* _M0L8_2afieldS2058 = _M0L5fm__iS884->$3;
    moonbit_string_t _M0L8_2afieldS2057;
    struct _M0TP26RiantR8snn__mbt10AdExSinExp* _M0L8_2afieldS2056;
    moonbit_decref(_M0L8_2afieldS2058);
    _M0L8_2afieldS2057 = _M0L5fm__iS884->$1;
    moonbit_decref(_M0L8_2afieldS2057);
    _M0L8_2afieldS2056 = _M0L5fm__iS884->$0;
    moonbit_decref(_M0L8_2afieldS2056);
    #line 87 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_adex\\main.mbt"
    moonbit_free(_M0L5fm__iS884);
  }
  #line 88 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_adex\\main.mbt"
  _M0L8i__countS894
  = _M0FP26RiantR8snn__mbt22monitor__count__spikes(_M0L4dataS956);
  moonbit_decref(_M0L4dataS956);
  #line 89 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_adex\\main.mbt"
  _M0L9ee__countS895 = _M0FP26RiantR8snn__mbt10count__nnz(_M0L5ee__wS877);
  moonbit_decref(_M0L5ee__wS877);
  #line 90 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_adex\\main.mbt"
  _M0L9ei__countS896 = _M0FP26RiantR8snn__mbt10count__nnz(_M0L5ei__wS878);
  moonbit_decref(_M0L5ei__wS878);
  #line 91 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_adex\\main.mbt"
  _M0L9ie__countS897 = _M0FP26RiantR8snn__mbt10count__nnz(_M0L5ie__wS879);
  moonbit_decref(_M0L5ie__wS879);
  #line 92 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_adex\\main.mbt"
  _M0L9ii__countS898 = _M0FP26RiantR8snn__mbt10count__nnz(_M0L5ii__wS880);
  moonbit_decref(_M0L5ii__wS880);
  #line 93 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_adex\\main.mbt"
  _M0FPB7printlnGsE((moonbit_string_t)moonbit_string_literal_21.data);
  #line 94 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_adex\\main.mbt"
  _M0L6_2atmpS928 = _M0MPC13int3Int18to__string_2einner(_M0L2neS862, 10);
  #line 94 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_adex\\main.mbt"
  _M0L6_2atmpS927
  = moonbit_add_string((moonbit_string_t)moonbit_string_literal_22.data, _M0L6_2atmpS928);
  moonbit_decref(_M0L6_2atmpS928);
  #line 94 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_adex\\main.mbt"
  _M0L6_2atmpS926
  = moonbit_add_string(_M0L6_2atmpS927, (moonbit_string_t)moonbit_string_literal_23.data);
  moonbit_decref(_M0L6_2atmpS927);
  #line 94 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_adex\\main.mbt"
  _M0FPB7printlnGsE(_M0L6_2atmpS926);
  moonbit_decref(_M0L6_2atmpS926);
  #line 95 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_adex\\main.mbt"
  _M0L6_2atmpS931 = _M0MPC13int3Int18to__string_2einner(_M0L2niS863, 10);
  #line 95 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_adex\\main.mbt"
  _M0L6_2atmpS930
  = moonbit_add_string((moonbit_string_t)moonbit_string_literal_24.data, _M0L6_2atmpS931);
  moonbit_decref(_M0L6_2atmpS931);
  #line 95 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_adex\\main.mbt"
  _M0L6_2atmpS929
  = moonbit_add_string(_M0L6_2atmpS930, (moonbit_string_t)moonbit_string_literal_25.data);
  moonbit_decref(_M0L6_2atmpS930);
  #line 95 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_adex\\main.mbt"
  _M0FPB7printlnGsE(_M0L6_2atmpS929);
  moonbit_decref(_M0L6_2atmpS929);
  #line 96 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_adex\\main.mbt"
  _M0L6_2atmpS934
  = _M0MPC13int3Int18to__string_2einner(_M0L9ee__countS895, 10);
  #line 96 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_adex\\main.mbt"
  _M0L6_2atmpS933
  = moonbit_add_string((moonbit_string_t)moonbit_string_literal_26.data, _M0L6_2atmpS934);
  moonbit_decref(_M0L6_2atmpS934);
  #line 96 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_adex\\main.mbt"
  _M0L6_2atmpS932
  = moonbit_add_string(_M0L6_2atmpS933, (moonbit_string_t)moonbit_string_literal_27.data);
  moonbit_decref(_M0L6_2atmpS933);
  #line 96 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_adex\\main.mbt"
  _M0FPB7printlnGsE(_M0L6_2atmpS932);
  moonbit_decref(_M0L6_2atmpS932);
  #line 97 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_adex\\main.mbt"
  _M0L6_2atmpS937
  = _M0MPC13int3Int18to__string_2einner(_M0L9ei__countS896, 10);
  #line 97 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_adex\\main.mbt"
  _M0L6_2atmpS936
  = moonbit_add_string((moonbit_string_t)moonbit_string_literal_28.data, _M0L6_2atmpS937);
  moonbit_decref(_M0L6_2atmpS937);
  #line 97 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_adex\\main.mbt"
  _M0L6_2atmpS935
  = moonbit_add_string(_M0L6_2atmpS936, (moonbit_string_t)moonbit_string_literal_29.data);
  moonbit_decref(_M0L6_2atmpS936);
  #line 97 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_adex\\main.mbt"
  _M0FPB7printlnGsE(_M0L6_2atmpS935);
  moonbit_decref(_M0L6_2atmpS935);
  #line 98 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_adex\\main.mbt"
  _M0L6_2atmpS940
  = _M0MPC13int3Int18to__string_2einner(_M0L9ie__countS897, 10);
  #line 98 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_adex\\main.mbt"
  _M0L6_2atmpS939
  = moonbit_add_string((moonbit_string_t)moonbit_string_literal_30.data, _M0L6_2atmpS940);
  moonbit_decref(_M0L6_2atmpS940);
  #line 98 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_adex\\main.mbt"
  _M0L6_2atmpS938
  = moonbit_add_string(_M0L6_2atmpS939, (moonbit_string_t)moonbit_string_literal_31.data);
  moonbit_decref(_M0L6_2atmpS939);
  #line 98 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_adex\\main.mbt"
  _M0FPB7printlnGsE(_M0L6_2atmpS938);
  moonbit_decref(_M0L6_2atmpS938);
  #line 99 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_adex\\main.mbt"
  _M0L6_2atmpS943
  = _M0MPC13int3Int18to__string_2einner(_M0L9ii__countS898, 10);
  #line 99 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_adex\\main.mbt"
  _M0L6_2atmpS942
  = moonbit_add_string((moonbit_string_t)moonbit_string_literal_32.data, _M0L6_2atmpS943);
  moonbit_decref(_M0L6_2atmpS943);
  #line 99 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_adex\\main.mbt"
  _M0L6_2atmpS941
  = moonbit_add_string(_M0L6_2atmpS942, (moonbit_string_t)moonbit_string_literal_33.data);
  moonbit_decref(_M0L6_2atmpS942);
  #line 99 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_adex\\main.mbt"
  _M0FPB7printlnGsE(_M0L6_2atmpS941);
  moonbit_decref(_M0L6_2atmpS941);
  #line 100 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_adex\\main.mbt"
  _M0FPB7printlnGsE((moonbit_string_t)moonbit_string_literal_34.data);
  #line 101 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_adex\\main.mbt"
  _M0FPB7printlnGsE((moonbit_string_t)moonbit_string_literal_35.data);
  #line 102 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_adex\\main.mbt"
  _M0L6_2atmpS949
  = _M0MPC13int3Int18to__string_2einner(_M0L8e__countS893, 10);
  #line 102 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_adex\\main.mbt"
  _M0L6_2atmpS948
  = moonbit_add_string((moonbit_string_t)moonbit_string_literal_36.data, _M0L6_2atmpS949);
  moonbit_decref(_M0L6_2atmpS949);
  #line 102 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_adex\\main.mbt"
  _M0L6_2atmpS946
  = moonbit_add_string(_M0L6_2atmpS948, (moonbit_string_t)moonbit_string_literal_37.data);
  moonbit_decref(_M0L6_2atmpS948);
  #line 102 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_adex\\main.mbt"
  _M0L6_2atmpS947 = _M0IPC15float5FloatPB4Show10to__string(_M0L5e__hzS891);
  #line 102 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_adex\\main.mbt"
  _M0L6_2atmpS945 = moonbit_add_string(_M0L6_2atmpS946, _M0L6_2atmpS947);
  moonbit_decref(_M0L6_2atmpS947);
  moonbit_decref(_M0L6_2atmpS946);
  #line 102 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_adex\\main.mbt"
  _M0L6_2atmpS944
  = moonbit_add_string(_M0L6_2atmpS945, (moonbit_string_t)moonbit_string_literal_38.data);
  moonbit_decref(_M0L6_2atmpS945);
  #line 102 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_adex\\main.mbt"
  _M0FPB7printlnGsE(_M0L6_2atmpS944);
  moonbit_decref(_M0L6_2atmpS944);
  #line 103 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_adex\\main.mbt"
  _M0L6_2atmpS955
  = _M0MPC13int3Int18to__string_2einner(_M0L8i__countS894, 10);
  #line 103 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_adex\\main.mbt"
  _M0L6_2atmpS954
  = moonbit_add_string((moonbit_string_t)moonbit_string_literal_39.data, _M0L6_2atmpS955);
  moonbit_decref(_M0L6_2atmpS955);
  #line 103 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_adex\\main.mbt"
  _M0L6_2atmpS952
  = moonbit_add_string(_M0L6_2atmpS954, (moonbit_string_t)moonbit_string_literal_37.data);
  moonbit_decref(_M0L6_2atmpS954);
  #line 103 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_adex\\main.mbt"
  _M0L6_2atmpS953 = _M0IPC15float5FloatPB4Show10to__string(_M0L5i__hzS892);
  #line 103 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_adex\\main.mbt"
  _M0L6_2atmpS951 = moonbit_add_string(_M0L6_2atmpS952, _M0L6_2atmpS953);
  moonbit_decref(_M0L6_2atmpS953);
  moonbit_decref(_M0L6_2atmpS952);
  #line 103 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_adex\\main.mbt"
  _M0L6_2atmpS950
  = moonbit_add_string(_M0L6_2atmpS951, (moonbit_string_t)moonbit_string_literal_38.data);
  moonbit_decref(_M0L6_2atmpS951);
  #line 103 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_adex\\main.mbt"
  _M0FPB7printlnGsE(_M0L6_2atmpS950);
  moonbit_decref(_M0L6_2atmpS950);
  return 0;
}