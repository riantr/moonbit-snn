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

struct _M0TP26RiantR8snn__mbt2IF;

struct _M0KTPB6LoggerTPB13StringBuilder;

struct _M0TPB6Logger;

struct _M0TPB4Show;

struct _M0TP26RiantR8snn__mbt7Xoshiro;

struct _M0TPB13StringBuilder;

struct _M0TPB5ArrayGfE;

struct _M0TPB17FloatingDecimal64;

struct _M0TP26RiantR8snn__mbt9PostSpike;

struct _M0TUmmmmE;

struct _M0TPB5ArrayGiE;

struct _M0TPB19MulShiftAll64Result;

struct _M0TP26RiantR8snn__mbt11IFParameter;

struct _M0TP26RiantR8snn__mbt18SpikeTimeParameter;

struct _M0TP26RiantR8snn__mbt17SpikeTimeStimulus;

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

struct _M0TP26RiantR8snn__mbt9PostSpike {
  float $0;
  
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

struct _M0TP26RiantR8snn__mbt18SpikeTimeParameter {
  struct _M0TPB5ArrayGfE* $0;
  struct _M0TPB5ArrayGiE* $1;
  
};

struct _M0TP26RiantR8snn__mbt17SpikeTimeStimulus {
  int32_t $0;
  struct _M0TP26RiantR8snn__mbt18SpikeTimeParameter* $1;
  struct _M0TPB5ArrayGfE* $2;
  struct _M0TPB5ArrayGiE* $3;
  struct _M0TPB5ArrayGbE* $4;
  struct _M0TPB5ArrayGfE* $5;
  
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

struct _M0TP26RiantR8snn__mbt11IFParameter* _M0MP26RiantR8snn__mbt11IFParameter6custom(
  float,
  float,
  float,
  float,
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

struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0MP26RiantR8snn__mbt7Xoshiro3new(
  uint64_t
);

struct _M0TUmmmmE* _M0FP26RiantR8snn__mbt10splitmix64(uint64_t);

uint64_t _M0FP26RiantR8snn__mbt5mix64(uint64_t);

int32_t _M0FP26RiantR8snn__mbt20stimulate__spiketime(
  struct _M0TP26RiantR8snn__mbt17SpikeTimeStimulus*,
  float,
  float
);

struct _M0TP26RiantR8snn__mbt17SpikeTimeStimulus* _M0MP26RiantR8snn__mbt17SpikeTimeStimulus3new(
  struct _M0TP26RiantR8snn__mbt2IF*,
  moonbit_string_t,
  struct _M0TPB5ArrayGfE*,
  struct _M0TPB5ArrayGiE*
);

struct _M0TP26RiantR8snn__mbt18SpikeTimeParameter* _M0MP26RiantR8snn__mbt18SpikeTimeParameter3new(
  struct _M0TPB5ArrayGfE*,
  struct _M0TPB5ArrayGiE*
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

moonbit_string_t _M0IPC15float5FloatPB4Show10to__string(float);

int32_t _M0MPC15float5Float7to__int(float);

struct _M0TPB5ArrayGfE* _M0MPC15array5Array4makeGfE(int32_t, float);

struct _M0TPB5ArrayGbE* _M0MPC15array5Array4makeGbE(int32_t, int32_t);

struct _M0TPB5ArrayGiE* _M0MPC15array5Array4makeGiE(int32_t, int32_t);

int32_t _M0MPC15array5Array3setGfE(struct _M0TPB5ArrayGfE*, int32_t, float);

int32_t _M0MPC15array5Array3setGbE(struct _M0TPB5ArrayGbE*, int32_t, int32_t);

int32_t _M0MPC15array5Array3setGiE(struct _M0TPB5ArrayGiE*, int32_t, int32_t);

struct _M0TPB5ArrayGfE* _M0MPC15array5Array4copyGfE(struct _M0TPB5ArrayGfE*);

struct _M0TPB5ArrayGiE* _M0MPC15array5Array4copyGiE(struct _M0TPB5ArrayGiE*);

int32_t _M0MPC15array5Array12unsafe__blitGfE(
  struct _M0TPB5ArrayGfE*,
  int32_t,
  struct _M0TPB5ArrayGfE*,
  int32_t,
  int32_t
);

int32_t _M0MPC15array5Array12unsafe__blitGiE(
  struct _M0TPB5ArrayGiE*,
  int32_t,
  struct _M0TPB5ArrayGiE*,
  int32_t,
  int32_t
);

float _M0MPC15array5Array2atGfE(struct _M0TPB5ArrayGfE*, int32_t);

int32_t _M0MPC15array5Array2atGbE(struct _M0TPB5ArrayGbE*, int32_t);

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

moonbit_string_t _M0IPC14bool4BoolPB4Show10to__string(int32_t);

uint64_t _M0MPC14uint4UInt10to__uint64(uint32_t);

int32_t _M0MPC15array5Array6lengthGfE(struct _M0TPB5ArrayGfE*);

float* _M0MPC15array5Array6bufferGfE(struct _M0TPB5ArrayGfE*);

uint8_t* _M0MPC15array5Array6bufferGbE(struct _M0TPB5ArrayGbE*);

int32_t* _M0MPC15array5Array6bufferGiE(struct _M0TPB5ArrayGiE*);

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

struct { int32_t rc; uint32_t meta; uint16_t const data[1]; 
} const moonbit_string_literal_5 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 0, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[16]; 
} const moonbit_string_literal_16 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 15, 44, 32, 
    100, 115, 116, 95, 111, 102, 102, 115, 101, 116, 32, 61, 32, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[19]; 
} const moonbit_string_literal_12 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 18, 105, 110, 
    118, 97, 108, 105, 100, 32, 99, 111, 100, 101, 32, 112, 111, 105, 
    110, 116, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[2]; 
} const moonbit_string_literal_4 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 1, 45, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[31]; 
} const moonbit_string_literal_10 =
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

struct { int32_t rc; uint32_t meta; uint16_t const data[22]; 
} const moonbit_string_literal_23 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 21, 32, 32, 
    80, 86, 46, 102, 105, 114, 101, 91, 48, 93, 32, 102, 105, 110, 97, 
    108, 32, 61, 32, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[2]; 
} const moonbit_string_literal_1 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 1, 48, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[9]; 
} const moonbit_string_literal_17 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 8, 44, 32, 
    108, 101, 110, 32, 61, 32, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[6]; 
} const moonbit_string_literal_9 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 5, 102, 97, 
    108, 115, 101, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[37]; 
} const moonbit_string_literal_14 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 36, 98, 111, 
    117, 110, 100, 115, 32, 99, 104, 101, 99, 107, 32, 102, 97, 105, 
    108, 101, 100, 58, 32, 97, 108, 108, 111, 99, 97, 116, 101, 95, 108, 
    101, 110, 32, 61, 32, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[3]; 
} const moonbit_string_literal_0 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 2, 103, 101, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[19]; 
} const moonbit_string_literal_20 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 18, 32, 32, 
    80, 86, 46, 118, 91, 48, 93, 32, 102, 105, 110, 97, 108, 32, 61, 
    32, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[5]; 
} const moonbit_string_literal_8 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 4, 116, 114, 
    117, 101, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[37]; 
} const moonbit_string_literal_11 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 36, 48, 49, 
    50, 51, 52, 53, 54, 55, 56, 57, 97, 98, 99, 100, 101, 102, 103, 104, 
    105, 106, 107, 108, 109, 110, 111, 112, 113, 114, 115, 116, 117, 
    118, 119, 120, 121, 122, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[21]; 
} const moonbit_string_literal_22 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 20, 32, 32, 
    80, 86, 46, 103, 108, 117, 91, 48, 93, 32, 102, 105, 110, 97, 108, 
    32, 61, 32, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[16]; 
} const moonbit_string_literal_18 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 15, 44, 32, 
    115, 114, 99, 46, 108, 101, 110, 103, 116, 104, 32, 61, 32, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[62]; 
} const moonbit_string_literal_19 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 61, 108, 107, 
    100, 50, 48, 49, 52, 95, 100, 101, 109, 111, 46, 109, 98, 116, 58, 
    32, 76, 75, 68, 50, 48, 49, 52, 46, 80, 86, 32, 73, 70, 32, 119, 
    105, 116, 104, 32, 115, 105, 110, 103, 108, 101, 32, 115, 112, 105, 
    107, 101, 32, 97, 116, 32, 116, 61, 49, 48, 48, 48, 109, 115, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[18]; 
} const moonbit_string_literal_21 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 17, 32, 40, 
    105, 110, 105, 116, 105, 97, 108, 32, 69, 108, 61, 45, 54, 50, 41, 
    0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[16]; 
} const moonbit_string_literal_15 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 15, 44, 32, 
    115, 114, 99, 95, 111, 102, 102, 115, 101, 116, 32, 61, 32, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[26]; 
} const moonbit_string_literal_2 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 25, 73, 108, 
    108, 101, 103, 97, 108, 65, 114, 103, 117, 109, 101, 110, 116, 69, 
    120, 99, 101, 112, 116, 105, 111, 110, 32, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[32]; 
} const moonbit_string_literal_13 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 31, 83, 116, 
    114, 105, 110, 103, 66, 117, 105, 108, 100, 101, 114, 32, 99, 97, 
    112, 97, 99, 105, 116, 121, 32, 111, 118, 101, 114, 102, 108, 111, 
    119, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[4]; 
} const moonbit_string_literal_7 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 3, 48, 46, 48, 0};

uint32_t const moonbit_layout_table_data[41] =
  {
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
    sizeof(struct _M0TPB5ArrayGfE) / 4, 1,
    offsetof(struct _M0TPB5ArrayGfE, $0) / 4 * 2,
    sizeof(struct _M0TPB5ArrayGiE) / 4, 1,
    offsetof(struct _M0TPB5ArrayGiE, $0) / 4 * 2,
    sizeof(struct _M0TP26RiantR8snn__mbt17SpikeTimeStimulus) / 4, 5,
    offsetof(struct _M0TP26RiantR8snn__mbt17SpikeTimeStimulus, $1) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt17SpikeTimeStimulus, $2) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt17SpikeTimeStimulus, $3) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt17SpikeTimeStimulus, $4) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt17SpikeTimeStimulus, $5) / 4 * 2,
    sizeof(struct _M0TP26RiantR8snn__mbt18SpikeTimeParameter) / 4, 2,
    offsetof(struct _M0TP26RiantR8snn__mbt18SpikeTimeParameter, $0) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt18SpikeTimeParameter, $1) / 4 * 2,
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

struct _M0TP26RiantR8snn__mbt11IFParameter* _M0MP26RiantR8snn__mbt11IFParameter6custom(
  float _M0L2tmS688,
  float _M0L2vtS689,
  float _M0L2vrS690,
  float _M0L2elS691,
  float _M0L1rS692
) {
  float _M0L1cS686;
  float _M0L2glS687;
  struct _M0TP26RiantR8snn__mbt11IFParameter* _block_1649;
  #line 62 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L1cS686 = -0x1p+0f;
  _M0L2glS687 = -0x1p+0f;
  _block_1649
  = (struct _M0TP26RiantR8snn__mbt11IFParameter*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt11IFParameter));
  Moonbit_object_header(_block_1649)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_1649->$0 = _M0L1cS686;
  _block_1649->$1 = _M0L2glS687;
  _block_1649->$2 = _M0L2tmS688;
  _block_1649->$3 = _M0L2vtS689;
  _block_1649->$4 = _M0L2vrS690;
  _block_1649->$5 = _M0L2elS691;
  _block_1649->$6 = _M0L1rS692;
  _block_1649->$7 = 0x1p+1f;
  _block_1649->$8 = 0x0p+0f;
  _block_1649->$9 = 0x0p+0f;
  _block_1649->$10 = 0x0p+0f;
  return _block_1649;
}

struct _M0TP26RiantR8snn__mbt2IF* _M0MP26RiantR8snn__mbt2IF3new(
  int32_t _M0L1nS660,
  struct _M0TP26RiantR8snn__mbt11IFParameter* _M0L5paramS662,
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS665
) {
  struct _M0TPB5ArrayGfE* _M0L1vS659;
  float _M0L2vtS1623;
  float _M0L2vrS1624;
  float _M0L6spreadS661;
  int32_t _M0L7_2abindS663;
  int32_t _M0L1kS664;
  struct _M0TPB5ArrayGfE* _M0L1wS667;
  struct _M0TPB5ArrayGbE* _M0L4fireS668;
  struct _M0TPB5ArrayGiE* _M0L4tabsS669;
  struct _M0TPB5ArrayGfE* _M0L1iS670;
  struct _M0TPB5ArrayGfE* _M0L9syn__currS671;
  struct _M0TPB5ArrayGfE* _M0L2geS672;
  struct _M0TPB5ArrayGfE* _M0L2giS673;
  struct _M0TPB5ArrayGfE* _M0L2heS674;
  struct _M0TPB5ArrayGfE* _M0L2hiS675;
  struct _M0TPB5ArrayGfE* _M0L3gluS676;
  struct _M0TPB5ArrayGfE* _M0L4gabaS677;
  struct _M0TPB5ArrayGfE* _M0L7gsyn__eS678;
  struct _M0TPB5ArrayGfE* _M0L7gsyn__iS679;
  float _M0L4e__eS680;
  float _M0L4e__iS681;
  float _M0L3treS682;
  float _M0L3tdeS683;
  float _M0L3triS684;
  float _M0L3tdiS685;
  struct _M0TP26RiantR8snn__mbt9PostSpike* _M0L6_2atmpS1622;
  struct _M0TP26RiantR8snn__mbt2IF* _block_1651;
  #line 113 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  #line 114 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L1vS659 = _M0MPC15array5Array4makeGfE(_M0L1nS660, 0x0p+0f);
  _M0L2vtS1623 = _M0L5paramS662->$3;
  _M0L2vrS1624 = _M0L5paramS662->$4;
  _M0L6spreadS661 = _M0L2vtS1623 - _M0L2vrS1624;
  _M0L7_2abindS663 = 0;
  _M0L1kS664 = _M0L7_2abindS663;
  while (1) {
    if (_M0L1kS664 < _M0L1nS660) {
      float _M0L2vrS1618 = _M0L5paramS662->$4;
      float _M0L6_2atmpS1620;
      float _M0L6_2atmpS1619;
      float _M0L6_2atmpS1617;
      int32_t _M0L6_2atmpS1621;
      #line 117 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1620 = _M0FP26RiantR8snn__mbt9next__f32(_M0L3rngS665);
      _M0L6_2atmpS1619 = _M0L6_2atmpS1620 * _M0L6spreadS661;
      _M0L6_2atmpS1617 = _M0L2vrS1618 + _M0L6_2atmpS1619;
      #line 117 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS659, _M0L1kS664, _M0L6_2atmpS1617);
      _M0L6_2atmpS1621 = _M0L1kS664 + 1;
      _M0L1kS664 = _M0L6_2atmpS1621;
      continue;
    }
    break;
  }
  #line 119 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L1wS667 = _M0MPC15array5Array4makeGfE(_M0L1nS660, 0x0p+0f);
  #line 120 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L4fireS668 = _M0MPC15array5Array4makeGbE(_M0L1nS660, 0);
  #line 121 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L4tabsS669 = _M0MPC15array5Array4makeGiE(_M0L1nS660, 0);
  #line 122 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L1iS670 = _M0MPC15array5Array4makeGfE(_M0L1nS660, 0x0p+0f);
  #line 123 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L9syn__currS671 = _M0MPC15array5Array4makeGfE(_M0L1nS660, 0x0p+0f);
  #line 124 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L2geS672 = _M0MPC15array5Array4makeGfE(_M0L1nS660, 0x0p+0f);
  #line 125 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L2giS673 = _M0MPC15array5Array4makeGfE(_M0L1nS660, 0x0p+0f);
  #line 126 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L2heS674 = _M0MPC15array5Array4makeGfE(_M0L1nS660, 0x0p+0f);
  #line 127 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L2hiS675 = _M0MPC15array5Array4makeGfE(_M0L1nS660, 0x0p+0f);
  #line 128 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L3gluS676 = _M0MPC15array5Array4makeGfE(_M0L1nS660, 0x0p+0f);
  #line 129 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L4gabaS677 = _M0MPC15array5Array4makeGfE(_M0L1nS660, 0x0p+0f);
  #line 130 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L7gsyn__eS678 = _M0MPC15array5Array4makeGfE(_M0L1nS660, 0x1p+0f);
  #line 131 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L7gsyn__iS679 = _M0MPC15array5Array4makeGfE(_M0L1nS660, 0x1p+0f);
  _M0L4e__eS680 = 0x0p+0f;
  _M0L4e__iS681 = -0x1.2cp+6f;
  _M0L3treS682 = 0x1p+0f;
  _M0L3tdeS683 = 0x1.8p+2f;
  _M0L3triS684 = 0x1p-1f;
  _M0L3tdiS685 = 0x1p+1f;
  #line 138 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L6_2atmpS1622 = _M0MP26RiantR8snn__mbt9PostSpike3new();
  moonbit_incref(_M0L5paramS662);
  _block_1651
  = (struct _M0TP26RiantR8snn__mbt2IF*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt2IF));
  Moonbit_object_header(_block_1651)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 0, 0);
  _block_1651->$0 = _M0L5paramS662;
  _block_1651->$1 = _M0L6_2atmpS1622;
  _block_1651->$2 = _M0L1nS660;
  _block_1651->$3 = _M0L1vS659;
  _block_1651->$4 = _M0L1wS667;
  _block_1651->$5 = _M0L4fireS668;
  _block_1651->$6 = _M0L4tabsS669;
  _block_1651->$7 = _M0L1iS670;
  _block_1651->$8 = _M0L9syn__currS671;
  _block_1651->$9 = _M0L2geS672;
  _block_1651->$10 = _M0L2giS673;
  _block_1651->$11 = _M0L2heS674;
  _block_1651->$12 = _M0L2hiS675;
  _block_1651->$13 = _M0L3gluS676;
  _block_1651->$14 = _M0L4gabaS677;
  _block_1651->$15 = _M0L7gsyn__eS678;
  _block_1651->$16 = _M0L7gsyn__iS679;
  _block_1651->$17 = _M0L4e__eS680;
  _block_1651->$18 = _M0L4e__iS681;
  _block_1651->$19 = _M0L3treS682;
  _block_1651->$20 = _M0L3tdeS683;
  _block_1651->$21 = _M0L3triS684;
  _block_1651->$22 = _M0L3tdiS685;
  return _block_1651;
}

struct _M0TP26RiantR8snn__mbt9PostSpike* _M0MP26RiantR8snn__mbt9PostSpike3new(
  
) {
  struct _M0TP26RiantR8snn__mbt9PostSpike* _block_1652;
  #line 75 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _block_1652
  = (struct _M0TP26RiantR8snn__mbt9PostSpike*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt9PostSpike));
  Moonbit_object_header(_block_1652)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_1652->$0 = 0x1p+1f;
  return _block_1652;
}

struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0MP26RiantR8snn__mbt7Xoshiro7default(
  
) {
  #line 56 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  #line 57 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  return _M0MP26RiantR8snn__mbt7Xoshiro3new(0ull);
}

int32_t _M0FP26RiantR8snn__mbt17synaptic__current(
  struct _M0TP26RiantR8snn__mbt2IF* _M0L1pS655
) {
  int32_t _M0L1nS654;
  int32_t _M0L7_2abindS656;
  int32_t _M0L1iS657;
  #line 165 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L1nS654 = _M0L1pS655->$2;
  _M0L7_2abindS656 = 0;
  _M0L1iS657 = _M0L7_2abindS656;
  while (1) {
    if (_M0L1iS657 < _M0L1nS654) {
      struct _M0TPB5ArrayGfE* _M0L9syn__currS1594 = _M0L1pS655->$8;
      struct _M0TPB5ArrayGfE* _M0L2geS1615 = _M0L1pS655->$9;
      float _M0L6_2atmpS1610;
      struct _M0TPB5ArrayGfE* _M0L1vS1614;
      float _M0L6_2atmpS1612;
      float _M0L4e__eS1613;
      float _M0L6_2atmpS1611;
      float _M0L6_2atmpS1607;
      struct _M0TPB5ArrayGfE* _M0L7gsyn__eS1609;
      float _M0L6_2atmpS1608;
      float _M0L6_2atmpS1596;
      struct _M0TPB5ArrayGfE* _M0L2giS1606;
      float _M0L6_2atmpS1601;
      struct _M0TPB5ArrayGfE* _M0L1vS1605;
      float _M0L6_2atmpS1603;
      float _M0L4e__iS1604;
      float _M0L6_2atmpS1602;
      float _M0L6_2atmpS1598;
      struct _M0TPB5ArrayGfE* _M0L7gsyn__iS1600;
      float _M0L6_2atmpS1599;
      float _M0L6_2atmpS1597;
      float _M0L6_2atmpS1595;
      int32_t _M0L6_2atmpS1616;
      #line 168 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1610 = _M0MPC15array5Array2atGfE(_M0L2geS1615, _M0L1iS657);
      _M0L1vS1614 = _M0L1pS655->$3;
      #line 168 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1612 = _M0MPC15array5Array2atGfE(_M0L1vS1614, _M0L1iS657);
      _M0L4e__eS1613 = _M0L1pS655->$17;
      _M0L6_2atmpS1611 = _M0L6_2atmpS1612 - _M0L4e__eS1613;
      _M0L6_2atmpS1607 = _M0L6_2atmpS1610 * _M0L6_2atmpS1611;
      _M0L7gsyn__eS1609 = _M0L1pS655->$15;
      #line 168 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1608
      = _M0MPC15array5Array2atGfE(_M0L7gsyn__eS1609, _M0L1iS657);
      _M0L6_2atmpS1596 = _M0L6_2atmpS1607 * _M0L6_2atmpS1608;
      _M0L2giS1606 = _M0L1pS655->$10;
      #line 169 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1601 = _M0MPC15array5Array2atGfE(_M0L2giS1606, _M0L1iS657);
      _M0L1vS1605 = _M0L1pS655->$3;
      #line 169 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1603 = _M0MPC15array5Array2atGfE(_M0L1vS1605, _M0L1iS657);
      _M0L4e__iS1604 = _M0L1pS655->$18;
      _M0L6_2atmpS1602 = _M0L6_2atmpS1603 - _M0L4e__iS1604;
      _M0L6_2atmpS1598 = _M0L6_2atmpS1601 * _M0L6_2atmpS1602;
      _M0L7gsyn__iS1600 = _M0L1pS655->$16;
      #line 169 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1599
      = _M0MPC15array5Array2atGfE(_M0L7gsyn__iS1600, _M0L1iS657);
      _M0L6_2atmpS1597 = _M0L6_2atmpS1598 * _M0L6_2atmpS1599;
      _M0L6_2atmpS1595 = _M0L6_2atmpS1596 + _M0L6_2atmpS1597;
      #line 168 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L9syn__currS1594, _M0L1iS657, _M0L6_2atmpS1595);
      _M0L6_2atmpS1616 = _M0L1iS657 + 1;
      _M0L1iS657 = _M0L6_2atmpS1616;
      continue;
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt14step__synapses(
  struct _M0TP26RiantR8snn__mbt2IF* _M0L1pS646,
  float _M0L2dtS649
) {
  int32_t _M0L1nS645;
  int32_t _M0L7_2abindS647;
  int32_t _M0L1iS648;
  int32_t _M0L7_2abindS651;
  int32_t _M0L1iS652;
  #line 146 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L1nS645 = _M0L1pS646->$2;
  _M0L7_2abindS647 = 0;
  _M0L1iS648 = _M0L7_2abindS647;
  while (1) {
    if (_M0L1iS648 < _M0L1nS645) {
      struct _M0TPB5ArrayGfE* _M0L2heS1532 = _M0L1pS646->$11;
      struct _M0TPB5ArrayGfE* _M0L2heS1537 = _M0L1pS646->$11;
      float _M0L6_2atmpS1534;
      struct _M0TPB5ArrayGfE* _M0L3gluS1536;
      float _M0L6_2atmpS1535;
      float _M0L6_2atmpS1533;
      struct _M0TPB5ArrayGfE* _M0L2hiS1538;
      struct _M0TPB5ArrayGfE* _M0L2hiS1543;
      float _M0L6_2atmpS1540;
      struct _M0TPB5ArrayGfE* _M0L4gabaS1542;
      float _M0L6_2atmpS1541;
      float _M0L6_2atmpS1539;
      struct _M0TPB5ArrayGfE* _M0L2geS1544;
      struct _M0TPB5ArrayGfE* _M0L2geS1556;
      float _M0L6_2atmpS1546;
      struct _M0TPB5ArrayGfE* _M0L2geS1555;
      float _M0L6_2atmpS1554;
      float _M0L6_2atmpS1552;
      float _M0L3tdeS1553;
      float _M0L6_2atmpS1549;
      struct _M0TPB5ArrayGfE* _M0L2heS1551;
      float _M0L6_2atmpS1550;
      float _M0L6_2atmpS1548;
      float _M0L6_2atmpS1547;
      float _M0L6_2atmpS1545;
      struct _M0TPB5ArrayGfE* _M0L2heS1557;
      struct _M0TPB5ArrayGfE* _M0L2heS1566;
      float _M0L6_2atmpS1559;
      struct _M0TPB5ArrayGfE* _M0L2heS1565;
      float _M0L6_2atmpS1564;
      float _M0L6_2atmpS1562;
      float _M0L3treS1563;
      float _M0L6_2atmpS1561;
      float _M0L6_2atmpS1560;
      float _M0L6_2atmpS1558;
      struct _M0TPB5ArrayGfE* _M0L2giS1567;
      struct _M0TPB5ArrayGfE* _M0L2giS1579;
      float _M0L6_2atmpS1569;
      struct _M0TPB5ArrayGfE* _M0L2giS1578;
      float _M0L6_2atmpS1577;
      float _M0L6_2atmpS1575;
      float _M0L3tdiS1576;
      float _M0L6_2atmpS1572;
      struct _M0TPB5ArrayGfE* _M0L2hiS1574;
      float _M0L6_2atmpS1573;
      float _M0L6_2atmpS1571;
      float _M0L6_2atmpS1570;
      float _M0L6_2atmpS1568;
      struct _M0TPB5ArrayGfE* _M0L2hiS1580;
      struct _M0TPB5ArrayGfE* _M0L2hiS1589;
      float _M0L6_2atmpS1582;
      struct _M0TPB5ArrayGfE* _M0L2hiS1588;
      float _M0L6_2atmpS1587;
      float _M0L6_2atmpS1585;
      float _M0L3triS1586;
      float _M0L6_2atmpS1584;
      float _M0L6_2atmpS1583;
      float _M0L6_2atmpS1581;
      int32_t _M0L6_2atmpS1590;
      #line 149 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1534 = _M0MPC15array5Array2atGfE(_M0L2heS1537, _M0L1iS648);
      _M0L3gluS1536 = _M0L1pS646->$13;
      #line 149 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1535 = _M0MPC15array5Array2atGfE(_M0L3gluS1536, _M0L1iS648);
      _M0L6_2atmpS1533 = _M0L6_2atmpS1534 + _M0L6_2atmpS1535;
      #line 149 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L2heS1532, _M0L1iS648, _M0L6_2atmpS1533);
      _M0L2hiS1538 = _M0L1pS646->$12;
      _M0L2hiS1543 = _M0L1pS646->$12;
      #line 150 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1540 = _M0MPC15array5Array2atGfE(_M0L2hiS1543, _M0L1iS648);
      _M0L4gabaS1542 = _M0L1pS646->$14;
      #line 150 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1541
      = _M0MPC15array5Array2atGfE(_M0L4gabaS1542, _M0L1iS648);
      _M0L6_2atmpS1539 = _M0L6_2atmpS1540 + _M0L6_2atmpS1541;
      #line 150 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L2hiS1538, _M0L1iS648, _M0L6_2atmpS1539);
      _M0L2geS1544 = _M0L1pS646->$9;
      _M0L2geS1556 = _M0L1pS646->$9;
      #line 151 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1546 = _M0MPC15array5Array2atGfE(_M0L2geS1556, _M0L1iS648);
      _M0L2geS1555 = _M0L1pS646->$9;
      #line 151 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1554 = _M0MPC15array5Array2atGfE(_M0L2geS1555, _M0L1iS648);
      _M0L6_2atmpS1552 = -_M0L6_2atmpS1554;
      _M0L3tdeS1553 = _M0L1pS646->$20;
      _M0L6_2atmpS1549 = _M0L6_2atmpS1552 / _M0L3tdeS1553;
      _M0L2heS1551 = _M0L1pS646->$11;
      #line 151 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1550 = _M0MPC15array5Array2atGfE(_M0L2heS1551, _M0L1iS648);
      _M0L6_2atmpS1548 = _M0L6_2atmpS1549 + _M0L6_2atmpS1550;
      _M0L6_2atmpS1547 = _M0L2dtS649 * _M0L6_2atmpS1548;
      _M0L6_2atmpS1545 = _M0L6_2atmpS1546 + _M0L6_2atmpS1547;
      #line 151 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L2geS1544, _M0L1iS648, _M0L6_2atmpS1545);
      _M0L2heS1557 = _M0L1pS646->$11;
      _M0L2heS1566 = _M0L1pS646->$11;
      #line 152 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1559 = _M0MPC15array5Array2atGfE(_M0L2heS1566, _M0L1iS648);
      _M0L2heS1565 = _M0L1pS646->$11;
      #line 152 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1564 = _M0MPC15array5Array2atGfE(_M0L2heS1565, _M0L1iS648);
      _M0L6_2atmpS1562 = -_M0L6_2atmpS1564;
      _M0L3treS1563 = _M0L1pS646->$19;
      _M0L6_2atmpS1561 = _M0L6_2atmpS1562 / _M0L3treS1563;
      _M0L6_2atmpS1560 = _M0L2dtS649 * _M0L6_2atmpS1561;
      _M0L6_2atmpS1558 = _M0L6_2atmpS1559 + _M0L6_2atmpS1560;
      #line 152 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L2heS1557, _M0L1iS648, _M0L6_2atmpS1558);
      _M0L2giS1567 = _M0L1pS646->$10;
      _M0L2giS1579 = _M0L1pS646->$10;
      #line 153 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1569 = _M0MPC15array5Array2atGfE(_M0L2giS1579, _M0L1iS648);
      _M0L2giS1578 = _M0L1pS646->$10;
      #line 153 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1577 = _M0MPC15array5Array2atGfE(_M0L2giS1578, _M0L1iS648);
      _M0L6_2atmpS1575 = -_M0L6_2atmpS1577;
      _M0L3tdiS1576 = _M0L1pS646->$22;
      _M0L6_2atmpS1572 = _M0L6_2atmpS1575 / _M0L3tdiS1576;
      _M0L2hiS1574 = _M0L1pS646->$12;
      #line 153 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1573 = _M0MPC15array5Array2atGfE(_M0L2hiS1574, _M0L1iS648);
      _M0L6_2atmpS1571 = _M0L6_2atmpS1572 + _M0L6_2atmpS1573;
      _M0L6_2atmpS1570 = _M0L2dtS649 * _M0L6_2atmpS1571;
      _M0L6_2atmpS1568 = _M0L6_2atmpS1569 + _M0L6_2atmpS1570;
      #line 153 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L2giS1567, _M0L1iS648, _M0L6_2atmpS1568);
      _M0L2hiS1580 = _M0L1pS646->$12;
      _M0L2hiS1589 = _M0L1pS646->$12;
      #line 154 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1582 = _M0MPC15array5Array2atGfE(_M0L2hiS1589, _M0L1iS648);
      _M0L2hiS1588 = _M0L1pS646->$12;
      #line 154 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1587 = _M0MPC15array5Array2atGfE(_M0L2hiS1588, _M0L1iS648);
      _M0L6_2atmpS1585 = -_M0L6_2atmpS1587;
      _M0L3triS1586 = _M0L1pS646->$21;
      _M0L6_2atmpS1584 = _M0L6_2atmpS1585 / _M0L3triS1586;
      _M0L6_2atmpS1583 = _M0L2dtS649 * _M0L6_2atmpS1584;
      _M0L6_2atmpS1581 = _M0L6_2atmpS1582 + _M0L6_2atmpS1583;
      #line 154 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L2hiS1580, _M0L1iS648, _M0L6_2atmpS1581);
      _M0L6_2atmpS1590 = _M0L1iS648 + 1;
      _M0L1iS648 = _M0L6_2atmpS1590;
      continue;
    }
    break;
  }
  _M0L7_2abindS651 = 0;
  _M0L1iS652 = _M0L7_2abindS651;
  while (1) {
    if (_M0L1iS652 < _M0L1nS645) {
      struct _M0TPB5ArrayGfE* _M0L3gluS1591 = _M0L1pS646->$13;
      struct _M0TPB5ArrayGfE* _M0L4gabaS1592;
      int32_t _M0L6_2atmpS1593;
      #line 157 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L3gluS1591, _M0L1iS652, 0x0p+0f);
      _M0L4gabaS1592 = _M0L1pS646->$14;
      #line 158 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L4gabaS1592, _M0L1iS652, 0x0p+0f);
      _M0L6_2atmpS1593 = _M0L1iS652 + 1;
      _M0L1iS652 = _M0L6_2atmpS1593;
      continue;
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt12step__neuron(
  struct _M0TP26RiantR8snn__mbt2IF* _M0L1pS631,
  float _M0L2dtS640
) {
  int32_t _M0L1nS630;
  struct _M0TP26RiantR8snn__mbt11IFParameter* _M0L3p__S632;
  float _M0L2tmS633;
  float _M0L2elS634;
  float _M0L1rS635;
  float _M0L2vtS636;
  float _M0L2vrS637;
  struct _M0TP26RiantR8snn__mbt9PostSpike* _M0L5spikeS1531;
  float _M0L11tabs__constS638;
  float _M0L6_2atmpS1530;
  int32_t _M0L11tabs__stepsS639;
  int32_t _M0L7_2abindS641;
  int32_t _M0L1iS642;
  #line 176 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L1nS630 = _M0L1pS631->$2;
  _M0L3p__S632 = _M0L1pS631->$0;
  _M0L2tmS633 = _M0L3p__S632->$2;
  _M0L2elS634 = _M0L3p__S632->$5;
  _M0L1rS635 = _M0L3p__S632->$6;
  _M0L2vtS636 = _M0L3p__S632->$3;
  _M0L2vrS637 = _M0L3p__S632->$4;
  _M0L5spikeS1531 = _M0L1pS631->$1;
  _M0L11tabs__constS638 = _M0L5spikeS1531->$0;
  _M0L6_2atmpS1530 = _M0L11tabs__constS638 / _M0L2dtS640;
  #line 185 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L11tabs__stepsS639 = _M0MPC15float5Float7to__int(_M0L6_2atmpS1530);
  _M0L7_2abindS641 = 0;
  _M0L1iS642 = _M0L7_2abindS641;
  while (1) {
    if (_M0L1iS642 < _M0L1nS630) {
      struct _M0TPB5ArrayGiE* _M0L4tabsS1490 = _M0L1pS631->$6;
      int32_t _M0L6_2atmpS1489;
      struct _M0TPB5ArrayGfE* _M0L1vS1496;
      struct _M0TPB5ArrayGfE* _M0L1vS1517;
      float _M0L6_2atmpS1498;
      float _M0L6_2atmpS1500;
      struct _M0TPB5ArrayGfE* _M0L1vS1516;
      float _M0L6_2atmpS1515;
      float _M0L6_2atmpS1514;
      float _M0L6_2atmpS1506;
      struct _M0TPB5ArrayGfE* _M0L1wS1513;
      float _M0L6_2atmpS1512;
      float _M0L6_2atmpS1509;
      struct _M0TPB5ArrayGfE* _M0L1iS1511;
      float _M0L6_2atmpS1510;
      float _M0L6_2atmpS1508;
      float _M0L6_2atmpS1507;
      float _M0L6_2atmpS1502;
      struct _M0TPB5ArrayGfE* _M0L9syn__currS1505;
      float _M0L6_2atmpS1504;
      float _M0L6_2atmpS1503;
      float _M0L6_2atmpS1501;
      float _M0L6_2atmpS1499;
      float _M0L6_2atmpS1497;
      struct _M0TPB5ArrayGbE* _M0L4fireS1518;
      struct _M0TPB5ArrayGfE* _M0L1vS1521;
      float _M0L6_2atmpS1520;
      int32_t _M0L6_2atmpS1519;
      struct _M0TPB5ArrayGfE* _M0L1vS1522;
      struct _M0TPB5ArrayGbE* _M0L4fireS1524;
      float _M0L6_2atmpS1523;
      struct _M0TPB5ArrayGiE* _M0L4tabsS1526;
      struct _M0TPB5ArrayGbE* _M0L4fireS1528;
      int32_t _M0L6_2atmpS1527;
      int32_t _M0L6_2atmpS1488;
      #line 188 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1489
      = _M0MPC15array5Array2atGiE(_M0L4tabsS1490, _M0L1iS642);
      if (_M0L6_2atmpS1489 > 0) {
        struct _M0TPB5ArrayGbE* _M0L4fireS1491 = _M0L1pS631->$5;
        struct _M0TPB5ArrayGiE* _M0L4tabsS1492;
        struct _M0TPB5ArrayGiE* _M0L4tabsS1495;
        int32_t _M0L6_2atmpS1494;
        int32_t _M0L6_2atmpS1493;
        #line 189 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
        _M0MPC15array5Array3setGbE(_M0L4fireS1491, _M0L1iS642, 0);
        _M0L4tabsS1492 = _M0L1pS631->$6;
        _M0L4tabsS1495 = _M0L1pS631->$6;
        #line 190 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
        _M0L6_2atmpS1494
        = _M0MPC15array5Array2atGiE(_M0L4tabsS1495, _M0L1iS642);
        _M0L6_2atmpS1493 = _M0L6_2atmpS1494 - 1;
        #line 190 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
        _M0MPC15array5Array3setGiE(_M0L4tabsS1492, _M0L1iS642, _M0L6_2atmpS1493);
        goto join_643;
      }
      _M0L1vS1496 = _M0L1pS631->$3;
      _M0L1vS1517 = _M0L1pS631->$3;
      #line 193 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1498 = _M0MPC15array5Array2atGfE(_M0L1vS1517, _M0L1iS642);
      _M0L6_2atmpS1500 = _M0L2dtS640 / _M0L2tmS633;
      _M0L1vS1516 = _M0L1pS631->$3;
      #line 194 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1515 = _M0MPC15array5Array2atGfE(_M0L1vS1516, _M0L1iS642);
      _M0L6_2atmpS1514 = _M0L6_2atmpS1515 - _M0L2elS634;
      _M0L6_2atmpS1506 = -_M0L6_2atmpS1514;
      _M0L1wS1513 = _M0L1pS631->$4;
      #line 194 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1512 = _M0MPC15array5Array2atGfE(_M0L1wS1513, _M0L1iS642);
      _M0L6_2atmpS1509 = -_M0L6_2atmpS1512;
      _M0L1iS1511 = _M0L1pS631->$7;
      #line 194 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1510 = _M0MPC15array5Array2atGfE(_M0L1iS1511, _M0L1iS642);
      _M0L6_2atmpS1508 = _M0L6_2atmpS1509 + _M0L6_2atmpS1510;
      _M0L6_2atmpS1507 = _M0L1rS635 * _M0L6_2atmpS1508;
      _M0L6_2atmpS1502 = _M0L6_2atmpS1506 + _M0L6_2atmpS1507;
      _M0L9syn__currS1505 = _M0L1pS631->$8;
      #line 194 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1504
      = _M0MPC15array5Array2atGfE(_M0L9syn__currS1505, _M0L1iS642);
      _M0L6_2atmpS1503 = _M0L1rS635 * _M0L6_2atmpS1504;
      _M0L6_2atmpS1501 = _M0L6_2atmpS1502 - _M0L6_2atmpS1503;
      _M0L6_2atmpS1499 = _M0L6_2atmpS1500 * _M0L6_2atmpS1501;
      _M0L6_2atmpS1497 = _M0L6_2atmpS1498 + _M0L6_2atmpS1499;
      #line 193 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS1496, _M0L1iS642, _M0L6_2atmpS1497);
      _M0L4fireS1518 = _M0L1pS631->$5;
      _M0L1vS1521 = _M0L1pS631->$3;
      #line 195 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1520 = _M0MPC15array5Array2atGfE(_M0L1vS1521, _M0L1iS642);
      _M0L6_2atmpS1519 = _M0L6_2atmpS1520 > _M0L2vtS636;
      #line 195 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGbE(_M0L4fireS1518, _M0L1iS642, _M0L6_2atmpS1519);
      _M0L1vS1522 = _M0L1pS631->$3;
      _M0L4fireS1524 = _M0L1pS631->$5;
      #line 196 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS1524, _M0L1iS642)) {
        _M0L6_2atmpS1523 = _M0L2vrS637;
      } else {
        struct _M0TPB5ArrayGfE* _M0L1vS1525 = _M0L1pS631->$3;
        #line 196 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
        _M0L6_2atmpS1523 = _M0MPC15array5Array2atGfE(_M0L1vS1525, _M0L1iS642);
      }
      #line 196 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS1522, _M0L1iS642, _M0L6_2atmpS1523);
      _M0L4tabsS1526 = _M0L1pS631->$6;
      _M0L4fireS1528 = _M0L1pS631->$5;
      #line 197 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS1528, _M0L1iS642)) {
        _M0L6_2atmpS1527 = _M0L11tabs__stepsS639;
      } else {
        struct _M0TPB5ArrayGiE* _M0L4tabsS1529 = _M0L1pS631->$6;
        #line 197 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
        _M0L6_2atmpS1527
        = _M0MPC15array5Array2atGiE(_M0L4tabsS1529, _M0L1iS642);
      }
      #line 197 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGiE(_M0L4tabsS1526, _M0L1iS642, _M0L6_2atmpS1527);
      goto join_643;
      goto joinlet_1657;
      join_643:;
      _M0L6_2atmpS1488 = _M0L1iS642 + 1;
      _M0L1iS642 = _M0L6_2atmpS1488;
      continue;
      joinlet_1657:;
    }
    break;
  }
  return 0;
}

struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0MP26RiantR8snn__mbt7Xoshiro3new(
  uint64_t _M0L4seedS628
) {
  struct _M0TUmmmmE* _M0L1sS627;
  uint64_t _M0L6_2atmpS1487;
  struct _M0TUmmmmE* _M0L1tS629;
  uint64_t _M0L6_2atmpS1483;
  uint64_t _M0L6_2atmpS1484;
  uint64_t _M0L6_2atmpS1485;
  uint64_t _M0L6_2atmpS1486;
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _block_1658;
  #line 48 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  #line 49 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L1sS627 = _M0FP26RiantR8snn__mbt10splitmix64(_M0L4seedS628);
  _M0L6_2atmpS1487 = _M0L1sS627->$3;
  #line 50 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L1tS629 = _M0FP26RiantR8snn__mbt10splitmix64(_M0L6_2atmpS1487);
  _M0L6_2atmpS1483 = _M0L1sS627->$0;
  _M0L6_2atmpS1484 = _M0L1sS627->$1;
  _M0L6_2atmpS1485 = _M0L1sS627->$2;
  moonbit_decref(_M0L1sS627);
  _M0L6_2atmpS1486 = _M0L1tS629->$0;
  moonbit_decref(_M0L1tS629);
  _block_1658
  = (struct _M0TP26RiantR8snn__mbt7Xoshiro*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt7Xoshiro));
  Moonbit_object_header(_block_1658)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_1658->$0 = _M0L6_2atmpS1483;
  _block_1658->$1 = _M0L6_2atmpS1484;
  _block_1658->$2 = _M0L6_2atmpS1485;
  _block_1658->$3 = _M0L6_2atmpS1486;
  return _block_1658;
}

struct _M0TUmmmmE* _M0FP26RiantR8snn__mbt10splitmix64(uint64_t _M0L4seedS619) {
  uint64_t _M0L2s1S618;
  uint64_t _M0L2z1S620;
  uint64_t _M0L2s2S621;
  uint64_t _M0L2z2S622;
  uint64_t _M0L2s3S623;
  uint64_t _M0L2z3S624;
  uint64_t _M0L2s4S625;
  uint64_t _M0L2z4S626;
  struct _M0TUmmmmE* _block_1659;
  #line 62 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2s1S618 = _M0L4seedS619 + 11400714819323198485ull;
  #line 64 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2z1S620 = _M0FP26RiantR8snn__mbt5mix64(_M0L2s1S618);
  _M0L2s2S621 = _M0L2s1S618 + 11400714819323198485ull;
  #line 66 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2z2S622 = _M0FP26RiantR8snn__mbt5mix64(_M0L2s2S621);
  _M0L2s3S623 = _M0L2s2S621 + 11400714819323198485ull;
  #line 68 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2z3S624 = _M0FP26RiantR8snn__mbt5mix64(_M0L2s3S623);
  _M0L2s4S625 = _M0L2s3S623 + 11400714819323198485ull;
  #line 70 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2z4S626 = _M0FP26RiantR8snn__mbt5mix64(_M0L2s4S625);
  _block_1659 = (struct _M0TUmmmmE*)moonbit_malloc(sizeof(struct _M0TUmmmmE));
  Moonbit_object_header(_block_1659)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_1659->$0 = _M0L2z1S620;
  _block_1659->$1 = _M0L2z2S622;
  _block_1659->$2 = _M0L2z3S624;
  _block_1659->$3 = _M0L2z4S626;
  return _block_1659;
}

uint64_t _M0FP26RiantR8snn__mbt5mix64(uint64_t _M0L1zS616) {
  uint64_t _M0L6_2atmpS1482;
  uint64_t _M0L6_2atmpS1481;
  uint64_t _M0L1zS615;
  uint64_t _M0L6_2atmpS1480;
  uint64_t _M0L6_2atmpS1479;
  uint64_t _M0L1zS617;
  uint64_t _M0L6_2atmpS1478;
  #line 75 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L6_2atmpS1482 = _M0L1zS616 >> 30;
  _M0L6_2atmpS1481 = _M0L1zS616 ^ _M0L6_2atmpS1482;
  _M0L1zS615 = _M0L6_2atmpS1481 * 13787848793156543929ull;
  _M0L6_2atmpS1480 = _M0L1zS615 >> 27;
  _M0L6_2atmpS1479 = _M0L1zS615 ^ _M0L6_2atmpS1480;
  _M0L1zS617 = _M0L6_2atmpS1479 * 10723151780598845931ull;
  _M0L6_2atmpS1478 = _M0L1zS617 >> 31;
  return _M0L1zS617 ^ _M0L6_2atmpS1478;
}

int32_t _M0FP26RiantR8snn__mbt20stimulate__spiketime(
  struct _M0TP26RiantR8snn__mbt17SpikeTimeStimulus* _M0L1sS609,
  float _M0L1tS611,
  float _M0L1wS613
) {
  struct _M0TPB8MutLocalGiE* _M0L1iS608;
  #line 107 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
  _M0L1iS608
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1iS608)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1iS608->$0 = 0;
  while (1) {
    int32_t _M0L3valS1440 = _M0L1iS608->$0;
    int32_t _M0L1nS1441 = _M0L1sS609->$0;
    if (_M0L3valS1440 < _M0L1nS1441) {
      struct _M0TPB5ArrayGbE* _M0L4fireS1442 = _M0L1sS609->$4;
      int32_t _M0L3valS1443 = _M0L1iS608->$0;
      int32_t _M0L3valS1445;
      int32_t _M0L6_2atmpS1444;
      #line 115 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
      _M0MPC15array5Array3setGbE(_M0L4fireS1442, _M0L3valS1443, 0);
      _M0L3valS1445 = _M0L1iS608->$0;
      _M0L6_2atmpS1444 = _M0L3valS1445 + 1;
      _M0L1iS608->$0 = _M0L6_2atmpS1444;
      continue;
    } else {
      moonbit_decref(_M0L1iS608);
    }
    break;
  }
  while (1) {
    struct _M0TPB5ArrayGiE* _M0L11next__indexS1449 = _M0L1sS609->$3;
    int32_t _M0L6_2atmpS1448;
    int32_t _if__result_1662;
    #line 119 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
    _M0L6_2atmpS1448 = _M0MPC15array5Array2atGiE(_M0L11next__indexS1449, 0);
    if (_M0L6_2atmpS1448 >= 0) {
      struct _M0TPB5ArrayGfE* _M0L11next__spikeS1447 = _M0L1sS609->$2;
      float _M0L6_2atmpS1446;
      #line 119 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
      _M0L6_2atmpS1446 = _M0MPC15array5Array2atGfE(_M0L11next__spikeS1447, 0);
      _if__result_1662 = _M0L6_2atmpS1446 <= _M0L1tS611;
    } else {
      _if__result_1662 = 0;
    }
    if (_if__result_1662) {
      struct _M0TP26RiantR8snn__mbt18SpikeTimeParameter* _M0L5paramS1477 =
        _M0L1sS609->$1;
      struct _M0TPB5ArrayGiE* _M0L7neuronsS1474 = _M0L5paramS1477->$1;
      struct _M0TPB5ArrayGiE* _M0L11next__indexS1476 = _M0L1sS609->$3;
      int32_t _M0L6_2atmpS1475;
      int32_t _M0L1jS612;
      struct _M0TPB5ArrayGbE* _M0L4fireS1450;
      struct _M0TPB5ArrayGfE* _M0L1gS1451;
      struct _M0TPB5ArrayGfE* _M0L1gS1454;
      float _M0L6_2atmpS1453;
      float _M0L6_2atmpS1452;
      struct _M0TPB5ArrayGiE* _M0L11next__indexS1460;
      int32_t _M0L6_2atmpS1459;
      int32_t _M0L6_2atmpS1455;
      struct _M0TP26RiantR8snn__mbt18SpikeTimeParameter* _M0L5paramS1458;
      struct _M0TPB5ArrayGfE* _M0L10spiketimesS1457;
      int32_t _M0L6_2atmpS1456;
      #line 120 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
      _M0L6_2atmpS1475 = _M0MPC15array5Array2atGiE(_M0L11next__indexS1476, 0);
      #line 120 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
      _M0L1jS612
      = _M0MPC15array5Array2atGiE(_M0L7neuronsS1474, _M0L6_2atmpS1475);
      _M0L4fireS1450 = _M0L1sS609->$4;
      #line 121 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
      _M0MPC15array5Array3setGbE(_M0L4fireS1450, _M0L1jS612, 1);
      _M0L1gS1451 = _M0L1sS609->$5;
      _M0L1gS1454 = _M0L1sS609->$5;
      #line 122 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
      _M0L6_2atmpS1453 = _M0MPC15array5Array2atGfE(_M0L1gS1454, _M0L1jS612);
      _M0L6_2atmpS1452 = _M0L6_2atmpS1453 + _M0L1wS613;
      #line 122 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
      _M0MPC15array5Array3setGfE(_M0L1gS1451, _M0L1jS612, _M0L6_2atmpS1452);
      _M0L11next__indexS1460 = _M0L1sS609->$3;
      #line 124 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
      _M0L6_2atmpS1459 = _M0MPC15array5Array2atGiE(_M0L11next__indexS1460, 0);
      _M0L6_2atmpS1455 = _M0L6_2atmpS1459 + 1;
      _M0L5paramS1458 = _M0L1sS609->$1;
      _M0L10spiketimesS1457 = _M0L5paramS1458->$0;
      #line 124 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
      _M0L6_2atmpS1456 = _M0MPC15array5Array6lengthGfE(_M0L10spiketimesS1457);
      if (_M0L6_2atmpS1455 < _M0L6_2atmpS1456) {
        struct _M0TPB5ArrayGiE* _M0L11next__indexS1461 = _M0L1sS609->$3;
        struct _M0TPB5ArrayGiE* _M0L11next__indexS1464 = _M0L1sS609->$3;
        int32_t _M0L6_2atmpS1463;
        int32_t _M0L6_2atmpS1462;
        struct _M0TPB5ArrayGfE* _M0L11next__spikeS1465;
        struct _M0TP26RiantR8snn__mbt18SpikeTimeParameter* _M0L5paramS1470;
        struct _M0TPB5ArrayGfE* _M0L10spiketimesS1467;
        struct _M0TPB5ArrayGiE* _M0L11next__indexS1469;
        int32_t _M0L6_2atmpS1468;
        float _M0L6_2atmpS1466;
        #line 125 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
        _M0L6_2atmpS1463
        = _M0MPC15array5Array2atGiE(_M0L11next__indexS1464, 0);
        _M0L6_2atmpS1462 = _M0L6_2atmpS1463 + 1;
        #line 125 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
        _M0MPC15array5Array3setGiE(_M0L11next__indexS1461, 0, _M0L6_2atmpS1462);
        _M0L11next__spikeS1465 = _M0L1sS609->$2;
        _M0L5paramS1470 = _M0L1sS609->$1;
        _M0L10spiketimesS1467 = _M0L5paramS1470->$0;
        _M0L11next__indexS1469 = _M0L1sS609->$3;
        #line 126 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
        _M0L6_2atmpS1468
        = _M0MPC15array5Array2atGiE(_M0L11next__indexS1469, 0);
        #line 126 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
        _M0L6_2atmpS1466
        = _M0MPC15array5Array2atGfE(_M0L10spiketimesS1467, _M0L6_2atmpS1468);
        #line 126 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
        _M0MPC15array5Array3setGfE(_M0L11next__spikeS1465, 0, _M0L6_2atmpS1466);
      } else {
        struct _M0TPB5ArrayGfE* _M0L11next__spikeS1471 = _M0L1sS609->$2;
        float _M0L6_2atmpS1472 = 0x0p+0f / (float)MOONBIT_ZERO;
        struct _M0TPB5ArrayGiE* _M0L11next__indexS1473;
        #line 128 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
        _M0MPC15array5Array3setGfE(_M0L11next__spikeS1471, 0, _M0L6_2atmpS1472);
        _M0L11next__indexS1473 = _M0L1sS609->$3;
        #line 129 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
        _M0MPC15array5Array3setGiE(_M0L11next__indexS1473, 0, -1);
      }
      continue;
    }
    break;
  }
  return 0;
}

struct _M0TP26RiantR8snn__mbt17SpikeTimeStimulus* _M0MP26RiantR8snn__mbt17SpikeTimeStimulus3new(
  struct _M0TP26RiantR8snn__mbt2IF* _M0L6e__popS599,
  moonbit_string_t _M0L3symS605,
  struct _M0TPB5ArrayGfE* _M0L10spiketimesS601,
  struct _M0TPB5ArrayGiE* _M0L7neuronsS602
) {
  int32_t _M0L1nS598;
  struct _M0TP26RiantR8snn__mbt18SpikeTimeParameter* _M0L5paramS600;
  struct _M0TPB5ArrayGbE* _M0L4fireS603;
  struct _M0TPB5ArrayGfE* _M0L1gS604;
  struct _M0TPB5ArrayGfE* _M0L10spiketimesS1434;
  int32_t _M0L6_2atmpS1433;
  struct _M0TPB5ArrayGfE* _M0L11next__spikeS606;
  struct _M0TPB5ArrayGfE* _M0L10spiketimesS1430;
  int32_t _M0L6_2atmpS1429;
  struct _M0TPB5ArrayGiE* _M0L11next__indexS607;
  struct _M0TP26RiantR8snn__mbt17SpikeTimeStimulus* _block_1663;
  #line 79 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
  _M0L1nS598 = _M0L6e__popS599->$2;
  #line 86 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
  _M0L5paramS600
  = _M0MP26RiantR8snn__mbt18SpikeTimeParameter3new(_M0L10spiketimesS601, _M0L7neuronsS602);
  #line 87 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
  _M0L4fireS603 = _M0MPC15array5Array4makeGbE(_M0L1nS598, 0);
  #line 88 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
  if (
    _M0L3symS605 == (moonbit_string_t)moonbit_string_literal_0.data
    || Moonbit_array_length(_M0L3symS605)
       == Moonbit_array_length((moonbit_string_t)moonbit_string_literal_0.data)
       && 0
          == memcmp(_M0L3symS605, (moonbit_string_t)moonbit_string_literal_0.data, Moonbit_array_length(_M0L3symS605) * 2)
  ) {
    struct _M0TPB5ArrayGfE* _M0L8_2afieldS1625 = _M0L6e__popS599->$13;
    moonbit_incref(_M0L8_2afieldS1625);
    _M0L1gS604 = _M0L8_2afieldS1625;
  } else {
    struct _M0TPB5ArrayGfE* _M0L8_2afieldS1626 = _M0L6e__popS599->$14;
    moonbit_incref(_M0L8_2afieldS1626);
    _M0L1gS604 = _M0L8_2afieldS1626;
  }
  _M0L10spiketimesS1434 = _M0L5paramS600->$0;
  moonbit_incref(_M0L10spiketimesS1434);
  #line 89 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
  _M0L6_2atmpS1433 = _M0MPC15array5Array6lengthGfE(_M0L10spiketimesS1434);
  moonbit_decref(_M0L10spiketimesS1434);
  if (_M0L6_2atmpS1433 > 0) {
    struct _M0TPB5ArrayGfE* _M0L10spiketimesS1437 = _M0L5paramS600->$0;
    float _M0L6_2atmpS1436;
    float* _M0L6_2atmpS1435;
    moonbit_incref(_M0L10spiketimesS1437);
    #line 90 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
    _M0L6_2atmpS1436 = _M0MPC15array5Array2atGfE(_M0L10spiketimesS1437, 0);
    moonbit_decref(_M0L10spiketimesS1437);
    _M0L6_2atmpS1435 = (float*)moonbit_make_float_array_raw(1);
    _M0L6_2atmpS1435[0] = _M0L6_2atmpS1436;
    _M0L11next__spikeS606
    = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
    Moonbit_object_header(_M0L11next__spikeS606)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 18, 0);
    _M0L11next__spikeS606->$0 = _M0L6_2atmpS1435;
    _M0L11next__spikeS606->$1 = 1;
  } else {
    float _M0L6_2atmpS1439 = 0x0p+0f / (float)MOONBIT_ZERO;
    float* _M0L6_2atmpS1438 = (float*)moonbit_make_float_array_raw(1);
    _M0L6_2atmpS1438[0] = _M0L6_2atmpS1439;
    _M0L11next__spikeS606
    = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
    Moonbit_object_header(_M0L11next__spikeS606)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 18, 0);
    _M0L11next__spikeS606->$0 = _M0L6_2atmpS1438;
    _M0L11next__spikeS606->$1 = 1;
  }
  _M0L10spiketimesS1430 = _M0L5paramS600->$0;
  moonbit_incref(_M0L10spiketimesS1430);
  #line 94 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
  _M0L6_2atmpS1429 = _M0MPC15array5Array6lengthGfE(_M0L10spiketimesS1430);
  moonbit_decref(_M0L10spiketimesS1430);
  if (_M0L6_2atmpS1429 > 0) {
    int32_t* _M0L6_2atmpS1431 = (int32_t*)moonbit_make_int32_array_raw(1);
    _M0L6_2atmpS1431[0] = 0;
    _M0L11next__indexS607
    = (struct _M0TPB5ArrayGiE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGiE));
    Moonbit_object_header(_M0L11next__indexS607)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 21, 0);
    _M0L11next__indexS607->$0 = _M0L6_2atmpS1431;
    _M0L11next__indexS607->$1 = 1;
  } else {
    int32_t* _M0L6_2atmpS1432 = (int32_t*)moonbit_make_int32_array_raw(1);
    _M0L6_2atmpS1432[0] = -1;
    _M0L11next__indexS607
    = (struct _M0TPB5ArrayGiE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGiE));
    Moonbit_object_header(_M0L11next__indexS607)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 21, 0);
    _M0L11next__indexS607->$0 = _M0L6_2atmpS1432;
    _M0L11next__indexS607->$1 = 1;
  }
  _block_1663
  = (struct _M0TP26RiantR8snn__mbt17SpikeTimeStimulus*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt17SpikeTimeStimulus));
  Moonbit_object_header(_block_1663)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 24, 0);
  _block_1663->$0 = _M0L1nS598;
  _block_1663->$1 = _M0L5paramS600;
  _block_1663->$2 = _M0L11next__spikeS606;
  _block_1663->$3 = _M0L11next__indexS607;
  _block_1663->$4 = _M0L4fireS603;
  _block_1663->$5 = _M0L1gS604;
  return _block_1663;
}

struct _M0TP26RiantR8snn__mbt18SpikeTimeParameter* _M0MP26RiantR8snn__mbt18SpikeTimeParameter3new(
  struct _M0TPB5ArrayGfE* _M0L10spiketimesS588,
  struct _M0TPB5ArrayGiE* _M0L7neuronsS591
) {
  int32_t _M0L1nS587;
  struct _M0TPB5ArrayGfE* _M0L9sorted__tS589;
  struct _M0TPB5ArrayGiE* _M0L9sorted__nS590;
  struct _M0TPB8MutLocalGiE* _M0L1iS592;
  struct _M0TP26RiantR8snn__mbt18SpikeTimeParameter* _block_1667;
  #line 33 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
  #line 38 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
  _M0L1nS587 = _M0MPC15array5Array6lengthGfE(_M0L10spiketimesS588);
  #line 39 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
  _M0L9sorted__tS589 = _M0MPC15array5Array4copyGfE(_M0L10spiketimesS588);
  #line 40 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
  _M0L9sorted__nS590 = _M0MPC15array5Array4copyGiE(_M0L7neuronsS591);
  _M0L1iS592
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1iS592)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1iS592->$0 = 1;
  while (1) {
    int32_t _M0L3valS1407 = _M0L1iS592->$0;
    if (_M0L3valS1407 < _M0L1nS587) {
      int32_t _M0L3valS1428 = _M0L1iS592->$0;
      float _M0L6key__tS593;
      int32_t _M0L3valS1427;
      int32_t _M0L6key__nS594;
      int32_t _M0L3valS1426;
      struct _M0TPB8MutLocalGiE* _M0L1jS595;
      int32_t _M0L3valS1422;
      int32_t _M0L3valS1423;
      int32_t _M0L3valS1425;
      int32_t _M0L6_2atmpS1424;
      #line 43 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
      _M0L6key__tS593
      = _M0MPC15array5Array2atGfE(_M0L9sorted__tS589, _M0L3valS1428);
      _M0L3valS1427 = _M0L1iS592->$0;
      #line 44 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
      _M0L6key__nS594
      = _M0MPC15array5Array2atGiE(_M0L9sorted__nS590, _M0L3valS1427);
      _M0L3valS1426 = _M0L1iS592->$0;
      _M0L1jS595
      = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
      Moonbit_object_header(_M0L1jS595)->meta
      = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
      _M0L1jS595->$0 = _M0L3valS1426;
      while (1) {
        int32_t _M0L3valS1411 = _M0L1jS595->$0;
        int32_t _if__result_1666;
        if (_M0L3valS1411 > 0) {
          int32_t _M0L3valS1410 = _M0L1jS595->$0;
          int32_t _M0L6_2atmpS1409 = _M0L3valS1410 - 1;
          float _M0L6_2atmpS1408;
          #line 46 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
          _M0L6_2atmpS1408
          = _M0MPC15array5Array2atGfE(_M0L9sorted__tS589, _M0L6_2atmpS1409);
          _if__result_1666 = _M0L6_2atmpS1408 > _M0L6key__tS593;
        } else {
          _if__result_1666 = 0;
        }
        if (_if__result_1666) {
          int32_t _M0L3valS1412 = _M0L1jS595->$0;
          int32_t _M0L3valS1415 = _M0L1jS595->$0;
          int32_t _M0L6_2atmpS1414 = _M0L3valS1415 - 1;
          float _M0L6_2atmpS1413;
          int32_t _M0L3valS1416;
          int32_t _M0L3valS1419;
          int32_t _M0L6_2atmpS1418;
          int32_t _M0L6_2atmpS1417;
          int32_t _M0L3valS1421;
          int32_t _M0L6_2atmpS1420;
          #line 47 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
          _M0L6_2atmpS1413
          = _M0MPC15array5Array2atGfE(_M0L9sorted__tS589, _M0L6_2atmpS1414);
          #line 47 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
          _M0MPC15array5Array3setGfE(_M0L9sorted__tS589, _M0L3valS1412, _M0L6_2atmpS1413);
          _M0L3valS1416 = _M0L1jS595->$0;
          _M0L3valS1419 = _M0L1jS595->$0;
          _M0L6_2atmpS1418 = _M0L3valS1419 - 1;
          #line 48 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
          _M0L6_2atmpS1417
          = _M0MPC15array5Array2atGiE(_M0L9sorted__nS590, _M0L6_2atmpS1418);
          #line 48 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
          _M0MPC15array5Array3setGiE(_M0L9sorted__nS590, _M0L3valS1416, _M0L6_2atmpS1417);
          _M0L3valS1421 = _M0L1jS595->$0;
          _M0L6_2atmpS1420 = _M0L3valS1421 - 1;
          _M0L1jS595->$0 = _M0L6_2atmpS1420;
          continue;
        }
        break;
      }
      _M0L3valS1422 = _M0L1jS595->$0;
      #line 51 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
      _M0MPC15array5Array3setGfE(_M0L9sorted__tS589, _M0L3valS1422, _M0L6key__tS593);
      _M0L3valS1423 = _M0L1jS595->$0;
      moonbit_decref(_M0L1jS595);
      #line 52 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
      _M0MPC15array5Array3setGiE(_M0L9sorted__nS590, _M0L3valS1423, _M0L6key__nS594);
      _M0L3valS1425 = _M0L1iS592->$0;
      _M0L6_2atmpS1424 = _M0L3valS1425 + 1;
      _M0L1iS592->$0 = _M0L6_2atmpS1424;
      continue;
    } else {
      moonbit_decref(_M0L1iS592);
    }
    break;
  }
  _block_1667
  = (struct _M0TP26RiantR8snn__mbt18SpikeTimeParameter*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt18SpikeTimeParameter));
  Moonbit_object_header(_block_1667)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 31, 0);
  _block_1667->$0 = _M0L9sorted__tS589;
  _block_1667->$1 = _M0L9sorted__nS590;
  return _block_1667;
}

float _M0FP26RiantR8snn__mbt9next__f32(
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L1rS585
) {
  uint32_t _M0L1uS584;
  uint32_t _M0L4bitsS586;
  double _M0L6_2atmpS1406;
  double _M0L6_2atmpS1405;
  #line 128 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  #line 129 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L1uS584 = _M0FP26RiantR8snn__mbt9next__u32(_M0L1rS585);
  _M0L4bitsS586 = _M0L1uS584 >> 8;
  _M0L6_2atmpS1406 = (double)_M0L4bitsS586;
  _M0L6_2atmpS1405 = _M0L6_2atmpS1406 * 0x1p-24;
  return (float)_M0L6_2atmpS1405;
}

uint32_t _M0FP26RiantR8snn__mbt9next__u32(
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L1rS583
) {
  uint64_t _M0L1uS582;
  uint64_t _M0L6_2atmpS1404;
  #line 110 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  #line 111 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L1uS582 = _M0FP26RiantR8snn__mbt9next__u64(_M0L1rS583);
  _M0L6_2atmpS1404 = _M0L1uS582 >> 32;
  return (uint32_t)_M0L6_2atmpS1404;
}

uint64_t _M0FP26RiantR8snn__mbt9next__u64(
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L1rS575
) {
  uint64_t _M0L2s0S574;
  uint64_t _M0L2s1S576;
  uint64_t _M0L2s2S577;
  uint64_t _M0L2s3S578;
  uint64_t _M0L3tmpS579;
  uint64_t _M0L6_2atmpS1403;
  uint64_t _M0L3resS580;
  uint64_t _M0L1tS581;
  uint64_t _M0L6_2atmpS1393;
  uint64_t _M0L6_2atmpS1394;
  uint64_t _M0L2s2S1396;
  uint64_t _M0L6_2atmpS1395;
  uint64_t _M0L2s3S1398;
  uint64_t _M0L6_2atmpS1397;
  uint64_t _M0L2s2S1400;
  uint64_t _M0L6_2atmpS1399;
  uint64_t _M0L2s3S1402;
  uint64_t _M0L6_2atmpS1401;
  #line 90 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2s0S574 = _M0L1rS575->$0;
  _M0L2s1S576 = _M0L1rS575->$1;
  _M0L2s2S577 = _M0L1rS575->$2;
  _M0L2s3S578 = _M0L1rS575->$3;
  _M0L3tmpS579 = _M0L2s0S574 + _M0L2s3S578;
  #line 96 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L6_2atmpS1403 = _M0FP26RiantR8snn__mbt4rotl(_M0L3tmpS579, 23);
  _M0L3resS580 = _M0L6_2atmpS1403 + _M0L2s0S574;
  _M0L1tS581 = _M0L2s1S576 << 17;
  _M0L6_2atmpS1393 = _M0L2s2S577 ^ _M0L2s0S574;
  _M0L1rS575->$2 = _M0L6_2atmpS1393;
  _M0L6_2atmpS1394 = _M0L2s3S578 ^ _M0L2s1S576;
  _M0L1rS575->$3 = _M0L6_2atmpS1394;
  _M0L2s2S1396 = _M0L1rS575->$2;
  _M0L6_2atmpS1395 = _M0L2s1S576 ^ _M0L2s2S1396;
  _M0L1rS575->$1 = _M0L6_2atmpS1395;
  _M0L2s3S1398 = _M0L1rS575->$3;
  _M0L6_2atmpS1397 = _M0L2s0S574 ^ _M0L2s3S1398;
  _M0L1rS575->$0 = _M0L6_2atmpS1397;
  _M0L2s2S1400 = _M0L1rS575->$2;
  _M0L6_2atmpS1399 = _M0L2s2S1400 ^ _M0L1tS581;
  _M0L1rS575->$2 = _M0L6_2atmpS1399;
  _M0L2s3S1402 = _M0L1rS575->$3;
  #line 103 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L6_2atmpS1401 = _M0FP26RiantR8snn__mbt4rotl(_M0L2s3S1402, 45);
  _M0L1rS575->$3 = _M0L6_2atmpS1401;
  return _M0L3resS580;
}

uint64_t _M0FP26RiantR8snn__mbt4rotl(uint64_t _M0L1xS572, int32_t _M0L1kS573) {
  uint64_t _M0L6_2atmpS1390;
  int32_t _M0L6_2atmpS1392;
  uint64_t _M0L6_2atmpS1391;
  #line 83 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L6_2atmpS1390 = _M0L1xS572 << (_M0L1kS573 & 63);
  _M0L6_2atmpS1392 = 64 - _M0L1kS573;
  _M0L6_2atmpS1391 = _M0L1xS572 >> (_M0L6_2atmpS1392 & 63);
  return _M0L6_2atmpS1390 | _M0L6_2atmpS1391;
}

moonbit_string_t _M0IPC15float5FloatPB4Show10to__string(float _M0L4selfS571) {
  double _M0L6_2atmpS1389;
  #line 16 "C:\\Users\\31379\\.moon\\lib\\core\\float\\methods.mbt"
  _M0L6_2atmpS1389 = (double)_M0L4selfS571;
  #line 17 "C:\\Users\\31379\\.moon\\lib\\core\\float\\methods.mbt"
  return _M0MPC16double6Double10to__string(_M0L6_2atmpS1389);
}

int32_t _M0MPC15float5Float7to__int(float _M0L4selfS570) {
  #line 43 "C:\\Users\\31379\\.moon\\lib\\core\\float\\to_int.mbt"
  if (_M0L4selfS570 != _M0L4selfS570) {
    return 0;
  } else if (_M0L4selfS570 >= 0x1.fffffffcp+30f) {
    return 2147483647;
  } else if (_M0L4selfS570 <= -0x1p+31f) {
    return (int32_t)0x80000000;
  } else {
    return (int32_t)_M0L4selfS570;
  }
}

struct _M0TPB5ArrayGfE* _M0MPC15array5Array4makeGfE(
  int32_t _M0L3lenS556,
  float _M0L4elemS558
) {
  struct _M0TPB5ArrayGfE* _M0L3arrS555;
  int32_t _M0L1iS557;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3arrS555 = _M0MPC15array5Array20unsafe__make__uninitGfE(_M0L3lenS556);
  _M0L1iS557 = 0;
  while (1) {
    if (_M0L1iS557 < _M0L3lenS556) {
      float* _M0L3bufS1383 = _M0L3arrS555->$0;
      int32_t _M0L6_2atmpS1384;
      _M0L3bufS1383[_M0L1iS557] = _M0L4elemS558;
      _M0L6_2atmpS1384 = _M0L1iS557 + 1;
      _M0L1iS557 = _M0L6_2atmpS1384;
      continue;
    }
    break;
  }
  return _M0L3arrS555;
}

struct _M0TPB5ArrayGbE* _M0MPC15array5Array4makeGbE(
  int32_t _M0L3lenS561,
  int32_t _M0L4elemS563
) {
  struct _M0TPB5ArrayGbE* _M0L3arrS560;
  int32_t _M0L1iS562;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3arrS560 = _M0MPC15array5Array20unsafe__make__uninitGbE(_M0L3lenS561);
  _M0L1iS562 = 0;
  while (1) {
    if (_M0L1iS562 < _M0L3lenS561) {
      uint8_t* _M0L3bufS1385 = _M0L3arrS560->$0;
      int32_t _M0L6_2atmpS1386;
      _M0L3bufS1385[_M0L1iS562] = _M0L4elemS563;
      _M0L6_2atmpS1386 = _M0L1iS562 + 1;
      _M0L1iS562 = _M0L6_2atmpS1386;
      continue;
    }
    break;
  }
  return _M0L3arrS560;
}

struct _M0TPB5ArrayGiE* _M0MPC15array5Array4makeGiE(
  int32_t _M0L3lenS566,
  int32_t _M0L4elemS568
) {
  struct _M0TPB5ArrayGiE* _M0L3arrS565;
  int32_t _M0L1iS567;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3arrS565 = _M0MPC15array5Array20unsafe__make__uninitGiE(_M0L3lenS566);
  _M0L1iS567 = 0;
  while (1) {
    if (_M0L1iS567 < _M0L3lenS566) {
      int32_t* _M0L3bufS1387 = _M0L3arrS565->$0;
      int32_t _M0L6_2atmpS1388;
      _M0L3bufS1387[_M0L1iS567] = _M0L4elemS568;
      _M0L6_2atmpS1388 = _M0L1iS567 + 1;
      _M0L1iS567 = _M0L6_2atmpS1388;
      continue;
    }
    break;
  }
  return _M0L3arrS565;
}

int32_t _M0MPC15array5Array3setGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS544,
  int32_t _M0L5indexS545,
  float _M0L5valueS546
) {
  int32_t _M0L3lenS543;
  #line 262 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS543 = _M0L4selfS544->$1;
  if (_M0L5indexS545 >= 0 && _M0L5indexS545 < _M0L3lenS543) {
    float* _M0L6_2atmpS1380;
    #line 268 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS1380 = _M0MPC15array5Array6bufferGfE(_M0L4selfS544);
    _M0L6_2atmpS1380[_M0L5indexS545] = _M0L5valueS546;
    moonbit_decref(_M0L6_2atmpS1380);
  } else {
    #line 267 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
  return 0;
}

int32_t _M0MPC15array5Array3setGbE(
  struct _M0TPB5ArrayGbE* _M0L4selfS548,
  int32_t _M0L5indexS549,
  int32_t _M0L5valueS550
) {
  int32_t _M0L3lenS547;
  #line 262 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS547 = _M0L4selfS548->$1;
  if (_M0L5indexS549 >= 0 && _M0L5indexS549 < _M0L3lenS547) {
    uint8_t* _M0L6_2atmpS1381;
    #line 268 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS1381 = _M0MPC15array5Array6bufferGbE(_M0L4selfS548);
    _M0L6_2atmpS1381[_M0L5indexS549] = _M0L5valueS550;
    moonbit_decref(_M0L6_2atmpS1381);
  } else {
    #line 267 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
  return 0;
}

int32_t _M0MPC15array5Array3setGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS552,
  int32_t _M0L5indexS553,
  int32_t _M0L5valueS554
) {
  int32_t _M0L3lenS551;
  #line 262 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS551 = _M0L4selfS552->$1;
  if (_M0L5indexS553 >= 0 && _M0L5indexS553 < _M0L3lenS551) {
    int32_t* _M0L6_2atmpS1382;
    #line 268 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS1382 = _M0MPC15array5Array6bufferGiE(_M0L4selfS552);
    _M0L6_2atmpS1382[_M0L5indexS553] = _M0L5valueS554;
    moonbit_decref(_M0L6_2atmpS1382);
  } else {
    #line 267 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
  return 0;
}

struct _M0TPB5ArrayGfE* _M0MPC15array5Array4copyGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS538
) {
  int32_t _M0L3lenS537;
  struct _M0TPB5ArrayGfE* _M0L3arrS539;
  #line 842 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L3lenS537 = _M0L4selfS538->$1;
  if (_M0L3lenS537 == 0) {
    float* _M0L6_2atmpS1378 = moonbit_empty_float_array;
    struct _M0TPB5ArrayGfE* _block_1671 =
      (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
    Moonbit_object_header(_block_1671)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 18, 0);
    _block_1671->$0 = _M0L6_2atmpS1378;
    _block_1671->$1 = 0;
    return _block_1671;
  }
  #line 848 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L3arrS539 = _M0MPC15array5Array20unsafe__make__uninitGfE(_M0L3lenS537);
  #line 849 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0MPC15array5Array12unsafe__blitGfE(_M0L3arrS539, 0, _M0L4selfS538, 0, _M0L3lenS537);
  return _M0L3arrS539;
}

struct _M0TPB5ArrayGiE* _M0MPC15array5Array4copyGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS541
) {
  int32_t _M0L3lenS540;
  struct _M0TPB5ArrayGiE* _M0L3arrS542;
  #line 842 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L3lenS540 = _M0L4selfS541->$1;
  if (_M0L3lenS540 == 0) {
    int32_t* _M0L6_2atmpS1379 = (int32_t*)moonbit_empty_int32_array;
    struct _M0TPB5ArrayGiE* _block_1672 =
      (struct _M0TPB5ArrayGiE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGiE));
    Moonbit_object_header(_block_1672)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 21, 0);
    _block_1672->$0 = _M0L6_2atmpS1379;
    _block_1672->$1 = 0;
    return _block_1672;
  }
  #line 848 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L3arrS542 = _M0MPC15array5Array20unsafe__make__uninitGiE(_M0L3lenS540);
  #line 849 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0MPC15array5Array12unsafe__blitGiE(_M0L3arrS542, 0, _M0L4selfS541, 0, _M0L3lenS540);
  return _M0L3arrS542;
}

int32_t _M0MPC15array5Array12unsafe__blitGfE(
  struct _M0TPB5ArrayGfE* _M0L3dstS527,
  int32_t _M0L11dst__offsetS528,
  struct _M0TPB5ArrayGfE* _M0L3srcS529,
  int32_t _M0L11src__offsetS530,
  int32_t _M0L3lenS531
) {
  float* _M0L6_2atmpS1374;
  float* _M0L6_2atmpS1375;
  #line 48 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array_block.mbt"
  #line 58 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array_block.mbt"
  _M0L6_2atmpS1374 = _M0MPC15array5Array6bufferGfE(_M0L3dstS527);
  #line 60 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array_block.mbt"
  _M0L6_2atmpS1375 = _M0MPC15array5Array6bufferGfE(_M0L3srcS529);
  #line 57 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array_block.mbt"
  moonbit_unsafe_val_array_blit(_M0L6_2atmpS1374, _M0L11dst__offsetS528, _M0L6_2atmpS1375, _M0L11src__offsetS530, _M0L3lenS531, sizeof(float));
  return 0;
}

int32_t _M0MPC15array5Array12unsafe__blitGiE(
  struct _M0TPB5ArrayGiE* _M0L3dstS532,
  int32_t _M0L11dst__offsetS533,
  struct _M0TPB5ArrayGiE* _M0L3srcS534,
  int32_t _M0L11src__offsetS535,
  int32_t _M0L3lenS536
) {
  int32_t* _M0L6_2atmpS1376;
  int32_t* _M0L6_2atmpS1377;
  #line 48 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array_block.mbt"
  #line 58 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array_block.mbt"
  _M0L6_2atmpS1376 = _M0MPC15array5Array6bufferGiE(_M0L3dstS532);
  #line 60 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array_block.mbt"
  _M0L6_2atmpS1377 = _M0MPC15array5Array6bufferGiE(_M0L3srcS534);
  #line 57 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array_block.mbt"
  moonbit_unsafe_val_array_blit(_M0L6_2atmpS1376, _M0L11dst__offsetS533, _M0L6_2atmpS1377, _M0L11src__offsetS535, _M0L3lenS536, sizeof(int32_t));
  return 0;
}

float _M0MPC15array5Array2atGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS519,
  int32_t _M0L5indexS520
) {
  int32_t _M0L3lenS518;
  #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS518 = _M0L4selfS519->$1;
  if (_M0L5indexS520 >= 0 && _M0L5indexS520 < _M0L3lenS518) {
    float* _M0L6_2atmpS1371;
    float _result_1673;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS1371 = _M0MPC15array5Array6bufferGfE(_M0L4selfS519);
    _result_1673 = (float)_M0L6_2atmpS1371[_M0L5indexS520];
    moonbit_decref(_M0L6_2atmpS1371);
    return _result_1673;
  } else {
    #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
}

int32_t _M0MPC15array5Array2atGbE(
  struct _M0TPB5ArrayGbE* _M0L4selfS522,
  int32_t _M0L5indexS523
) {
  int32_t _M0L3lenS521;
  #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS521 = _M0L4selfS522->$1;
  if (_M0L5indexS523 >= 0 && _M0L5indexS523 < _M0L3lenS521) {
    uint8_t* _M0L6_2atmpS1372;
    int32_t _result_1674;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS1372 = _M0MPC15array5Array6bufferGbE(_M0L4selfS522);
    _result_1674 = (int32_t)_M0L6_2atmpS1372[_M0L5indexS523];
    moonbit_decref(_M0L6_2atmpS1372);
    return _result_1674;
  } else {
    #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
}

int32_t _M0MPC15array5Array2atGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS525,
  int32_t _M0L5indexS526
) {
  int32_t _M0L3lenS524;
  #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS524 = _M0L4selfS525->$1;
  if (_M0L5indexS526 >= 0 && _M0L5indexS526 < _M0L3lenS524) {
    int32_t* _M0L6_2atmpS1373;
    int32_t _result_1675;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS1373 = _M0MPC15array5Array6bufferGiE(_M0L4selfS525);
    _result_1675 = (int32_t)_M0L6_2atmpS1373[_M0L5indexS526];
    moonbit_decref(_M0L6_2atmpS1373);
    return _result_1675;
  } else {
    #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
}

int32_t _M0FPB7printlnGsE(moonbit_string_t _M0L5inputS517) {
  moonbit_string_t _M0L6_2atmpS1370;
  #line 36 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\console.mbt"
  #line 37 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\console.mbt"
  _M0L6_2atmpS1370 = _M0IPC16string6StringPB4Show10to__string(_M0L5inputS517);
  #line 37 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\console.mbt"
  moonbit_println(_M0L6_2atmpS1370);
  moonbit_decref(_M0L6_2atmpS1370);
  return 0;
}

moonbit_string_t _M0MPC16double6Double10to__string(double _M0L4selfS516) {
  #line 296 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double.mbt"
  #line 298 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double.mbt"
  return _M0FPB15ryu__to__string(_M0L4selfS516);
}

moonbit_string_t _M0FPB15ryu__to__string(double _M0L3valS501) {
  uint64_t _M0L4bitsS504;
  uint64_t _M0L6_2atmpS1369;
  uint64_t _M0L6_2atmpS1368;
  int32_t _M0L8ieeeSignS505;
  uint64_t _M0L12ieeeMantissaS506;
  uint64_t _M0L6_2atmpS1367;
  uint64_t _M0L6_2atmpS1366;
  int32_t _M0L12ieeeExponentS507;
  struct _M0TPB17FloatingDecimal64* _M0L7_2abindS508;
  struct _M0TPB17FloatingDecimal64* _M0L1vS509;
  moonbit_string_t _result_1677;
  #line 668 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  if (_M0L3valS501 == 0x0p+0) {
    return (moonbit_string_t)moonbit_string_literal_1.data;
  }
  if (_M0L3valS501 >= -0x1p+53 && _M0L3valS501 <= 0x1p+53) {
    if (_M0L3valS501 >= -0x1p+31 && _M0L3valS501 <= 0x1.fffffffcp+30) {
      int32_t _M0L1iS502;
      double _M0L6_2atmpS1355;
      #line 683 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0L1iS502 = _M0MPC16double6Double7to__int(_M0L3valS501);
      _M0L6_2atmpS1355 = (double)_M0L1iS502;
      if (_M0L6_2atmpS1355 == _M0L3valS501) {
        #line 685 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        return _M0MPC13int3Int18to__string_2einner(_M0L1iS502, 10);
      }
    } else {
      int64_t _M0L1iS503;
      double _M0L6_2atmpS1356;
      #line 688 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0L1iS503 = _M0MPC16double6Double9to__int64(_M0L3valS501);
      _M0L6_2atmpS1356 = (double)_M0L1iS503;
      if (_M0L6_2atmpS1356 == _M0L3valS501) {
        #line 690 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        return _M0MPC15int645Int6418to__string_2einner(_M0L1iS503, 10);
      }
    }
  }
  _M0L4bitsS504 = *(int64_t*)&_M0L3valS501;
  _M0L6_2atmpS1369 = _M0L4bitsS504 >> 63;
  _M0L6_2atmpS1368 = _M0L6_2atmpS1369 & 1ull;
  _M0L8ieeeSignS505 = _M0L6_2atmpS1368 != 0ull;
  _M0L12ieeeMantissaS506 = _M0L4bitsS504 & 4503599627370495ull;
  _M0L6_2atmpS1367 = _M0L4bitsS504 >> 52;
  _M0L6_2atmpS1366 = _M0L6_2atmpS1367 & 2047ull;
  _M0L12ieeeExponentS507 = (int32_t)_M0L6_2atmpS1366;
  if (
    _M0L12ieeeExponentS507 == 2047
    || _M0L12ieeeExponentS507 == 0 && _M0L12ieeeMantissaS506 == 0ull
  ) {
    int32_t _M0L6_2atmpS1357 = _M0L12ieeeExponentS507 != 0;
    int32_t _M0L6_2atmpS1358 = _M0L12ieeeMantissaS506 != 0ull;
    #line 707 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    return _M0FPB18copy__special__str(_M0L8ieeeSignS505, _M0L6_2atmpS1357, _M0L6_2atmpS1358);
  }
  #line 709 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS508
  = _M0FPB15d2d__small__int(_M0L12ieeeMantissaS506, _M0L12ieeeExponentS507);
  if (_M0L7_2abindS508 == 0) {
    uint32_t _M0L6_2atmpS1359;
    if (_M0L7_2abindS508) {
      moonbit_decref(_M0L7_2abindS508);
    }
    _M0L6_2atmpS1359 = *(uint32_t*)&_M0L12ieeeExponentS507;
    #line 719 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L1vS509 = _M0FPB3d2d(_M0L12ieeeMantissaS506, _M0L6_2atmpS1359);
  } else {
    struct _M0TPB17FloatingDecimal64* _M0L7_2aSomeS510 = _M0L7_2abindS508;
    struct _M0TPB17FloatingDecimal64* _M0L4_2afS511 = _M0L7_2aSomeS510;
    struct _M0TPB17FloatingDecimal64* _M0L1xS512 = _M0L4_2afS511;
    while (1) {
      uint64_t _M0L8mantissaS1365 = _M0L1xS512->$0;
      uint64_t _M0L1qS513 = _M0L8mantissaS1365 / 10ull;
      uint64_t _M0L8mantissaS1363 = _M0L1xS512->$0;
      uint64_t _M0L6_2atmpS1364 = 10ull * _M0L1qS513;
      uint64_t _M0L1rS514 = _M0L8mantissaS1363 - _M0L6_2atmpS1364;
      int32_t _M0L8exponentS1362;
      int32_t _M0L6_2atmpS1361;
      struct _M0TPB17FloatingDecimal64* _M0L6_2atmpS1360;
      if (_M0L1rS514 != 0ull) {
        _M0L1vS509 = _M0L1xS512;
        break;
      }
      _M0L8exponentS1362 = _M0L1xS512->$1;
      moonbit_decref(_M0L1xS512);
      _M0L6_2atmpS1361 = _M0L8exponentS1362 + 1;
      _M0L6_2atmpS1360
      = (struct _M0TPB17FloatingDecimal64*)moonbit_malloc(sizeof(struct _M0TPB17FloatingDecimal64));
      Moonbit_object_header(_M0L6_2atmpS1360)->meta
      = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
      _M0L6_2atmpS1360->$0 = _M0L1qS513;
      _M0L6_2atmpS1360->$1 = _M0L6_2atmpS1361;
      _M0L1xS512 = _M0L6_2atmpS1360;
      continue;
      break;
    }
  }
  #line 721 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _result_1677 = _M0FPB9to__chars(_M0L1vS509, _M0L8ieeeSignS505);
  moonbit_decref(_M0L1vS509);
  return _result_1677;
}

struct _M0TPB17FloatingDecimal64* _M0FPB15d2d__small__int(
  uint64_t _M0L12ieeeMantissaS496,
  int32_t _M0L12ieeeExponentS498
) {
  uint64_t _M0L2m2S495;
  int32_t _M0L6_2atmpS1354;
  int32_t _M0L2e2S497;
  int32_t _M0L6_2atmpS1353;
  uint64_t _M0L6_2atmpS1352;
  uint64_t _M0L4maskS499;
  uint64_t _M0L8fractionS500;
  int32_t _M0L6_2atmpS1351;
  uint64_t _M0L6_2atmpS1350;
  struct _M0TPB17FloatingDecimal64* _M0L6_2atmpS1349;
  #line 637 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L2m2S495 = 4503599627370496ull | _M0L12ieeeMantissaS496;
  _M0L6_2atmpS1354 = _M0L12ieeeExponentS498 - 1023;
  _M0L2e2S497 = _M0L6_2atmpS1354 - 52;
  if (_M0L2e2S497 > 0) {
    return 0;
  }
  if (_M0L2e2S497 < -52) {
    return 0;
  }
  _M0L6_2atmpS1353 = -_M0L2e2S497;
  _M0L6_2atmpS1352 = 1ull << (_M0L6_2atmpS1353 & 63);
  _M0L4maskS499 = _M0L6_2atmpS1352 - 1ull;
  _M0L8fractionS500 = _M0L2m2S495 & _M0L4maskS499;
  if (_M0L8fractionS500 != 0ull) {
    return 0;
  }
  _M0L6_2atmpS1351 = -_M0L2e2S497;
  _M0L6_2atmpS1350 = _M0L2m2S495 >> (_M0L6_2atmpS1351 & 63);
  _M0L6_2atmpS1349
  = (struct _M0TPB17FloatingDecimal64*)moonbit_malloc(sizeof(struct _M0TPB17FloatingDecimal64));
  Moonbit_object_header(_M0L6_2atmpS1349)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L6_2atmpS1349->$0 = _M0L6_2atmpS1350;
  _M0L6_2atmpS1349->$1 = 0;
  return _M0L6_2atmpS1349;
}

moonbit_string_t _M0FPB9to__chars(
  struct _M0TPB17FloatingDecimal64* _M0L1vS463,
  int32_t _M0L4signS461
) {
  moonbit_bytes_t _M0L6resultS459;
  int32_t _M0Lm5indexS460;
  uint64_t _M0L6outputS462;
  int32_t _M0L7olengthS464;
  int32_t _M0L8exponentS1348;
  int32_t _M0L6_2atmpS1347;
  int32_t _M0Lm3expS465;
  int32_t _M0L6_2atmpS1346;
  int32_t _M0L6_2atmpS1344;
  int32_t _M0L18scientificNotationS466;
  #line 530 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6resultS459 = (moonbit_bytes_t)moonbit_make_bytes(25, 0);
  _M0Lm5indexS460 = 0;
  if (_M0L4signS461) {
    int32_t _M0L6_2atmpS1218 = _M0Lm5indexS460;
    int32_t _M0L6_2atmpS1219;
    if (
      _M0L6_2atmpS1218 < 0
      || _M0L6_2atmpS1218 >= Moonbit_array_length(_M0L6resultS459)
    ) {
      #line 535 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      moonbit_panic();
    }
    _M0L6resultS459[_M0L6_2atmpS1218] = 45;
    _M0L6_2atmpS1219 = _M0Lm5indexS460;
    _M0Lm5indexS460 = _M0L6_2atmpS1219 + 1;
  }
  _M0L6outputS462 = _M0L1vS463->$0;
  #line 539 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7olengthS464 = _M0FPB17decimal__length17(_M0L6outputS462);
  _M0L8exponentS1348 = _M0L1vS463->$1;
  _M0L6_2atmpS1347 = _M0L8exponentS1348 + _M0L7olengthS464;
  _M0Lm3expS465 = _M0L6_2atmpS1347 - 1;
  _M0L6_2atmpS1346 = _M0Lm3expS465;
  if (_M0L6_2atmpS1346 >= -6) {
    int32_t _M0L6_2atmpS1345 = _M0Lm3expS465;
    _M0L6_2atmpS1344 = _M0L6_2atmpS1345 < 21;
  } else {
    _M0L6_2atmpS1344 = 0;
  }
  _M0L18scientificNotationS466 = !_M0L6_2atmpS1344;
  if (_M0L18scientificNotationS466) {
    int32_t _M0L7_2abindS467 = _M0L7olengthS464 - 1;
    uint64_t _M0L6outputS468;
    int32_t _M0L1iS469 = 0;
    uint64_t _M0L6outputS470 = _M0L6outputS462;
    int32_t _M0L6_2atmpS1220;
    int32_t _M0L6_2atmpS1224;
    int32_t _M0L6_2atmpS1223;
    int32_t _M0L6_2atmpS1222;
    int32_t _M0L6_2atmpS1221;
    int32_t _M0L6_2atmpS1228;
    int32_t _M0L6_2atmpS1229;
    int32_t _M0L6_2atmpS1230;
    int32_t _M0L6_2atmpS1231;
    int32_t _M0L6_2atmpS1232;
    int32_t _M0L6_2atmpS1238;
    int32_t _M0L6_2atmpS1271;
    moonbit_string_t _result_1679;
    while (1) {
      if (_M0L1iS469 < _M0L7_2abindS467) {
        uint64_t _M0L1cS471 = _M0L6outputS470 % 10ull;
        int32_t _M0L6_2atmpS1277 = _M0Lm5indexS460;
        int32_t _M0L6_2atmpS1276 = _M0L6_2atmpS1277 + _M0L7olengthS464;
        int32_t _M0L6_2atmpS1272 = _M0L6_2atmpS1276 - _M0L1iS469;
        int32_t _M0L6_2atmpS1275 = (int32_t)_M0L1cS471;
        int32_t _M0L6_2atmpS1274 = 48 + _M0L6_2atmpS1275;
        int32_t _M0L6_2atmpS1273 = _M0L6_2atmpS1274 & 0xff;
        int32_t _M0L6_2atmpS1278;
        uint64_t _M0L6_2atmpS1279;
        if (
          _M0L6_2atmpS1272 < 0
          || _M0L6_2atmpS1272 >= Moonbit_array_length(_M0L6resultS459)
        ) {
          #line 547 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
          moonbit_panic();
        }
        _M0L6resultS459[_M0L6_2atmpS1272] = _M0L6_2atmpS1273;
        _M0L6_2atmpS1278 = _M0L1iS469 + 1;
        _M0L6_2atmpS1279 = _M0L6outputS470 / 10ull;
        _M0L1iS469 = _M0L6_2atmpS1278;
        _M0L6outputS470 = _M0L6_2atmpS1279;
        continue;
      } else {
        _M0L6outputS468 = _M0L6outputS470;
      }
      break;
    }
    _M0L6_2atmpS1220 = _M0Lm5indexS460;
    _M0L6_2atmpS1224 = (int32_t)_M0L6outputS468;
    _M0L6_2atmpS1223 = _M0L6_2atmpS1224 % 10;
    _M0L6_2atmpS1222 = 48 + _M0L6_2atmpS1223;
    _M0L6_2atmpS1221 = _M0L6_2atmpS1222 & 0xff;
    if (
      _M0L6_2atmpS1220 < 0
      || _M0L6_2atmpS1220 >= Moonbit_array_length(_M0L6resultS459)
    ) {
      #line 552 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      moonbit_panic();
    }
    _M0L6resultS459[_M0L6_2atmpS1220] = _M0L6_2atmpS1221;
    if (_M0L7olengthS464 > 1) {
      int32_t _M0L6_2atmpS1226 = _M0Lm5indexS460;
      int32_t _M0L6_2atmpS1225 = _M0L6_2atmpS1226 + 1;
      if (
        _M0L6_2atmpS1225 < 0
        || _M0L6_2atmpS1225 >= Moonbit_array_length(_M0L6resultS459)
      ) {
        #line 554 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS459[_M0L6_2atmpS1225] = 46;
    } else {
      int32_t _M0L6_2atmpS1227 = _M0Lm5indexS460;
      _M0Lm5indexS460 = _M0L6_2atmpS1227 - 1;
    }
    _M0L6_2atmpS1228 = _M0Lm5indexS460;
    _M0L6_2atmpS1229 = _M0L7olengthS464 + 1;
    _M0Lm5indexS460 = _M0L6_2atmpS1228 + _M0L6_2atmpS1229;
    _M0L6_2atmpS1230 = _M0Lm5indexS460;
    if (
      _M0L6_2atmpS1230 < 0
      || _M0L6_2atmpS1230 >= Moonbit_array_length(_M0L6resultS459)
    ) {
      #line 562 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      moonbit_panic();
    }
    _M0L6resultS459[_M0L6_2atmpS1230] = 101;
    _M0L6_2atmpS1231 = _M0Lm5indexS460;
    _M0Lm5indexS460 = _M0L6_2atmpS1231 + 1;
    _M0L6_2atmpS1232 = _M0Lm3expS465;
    if (_M0L6_2atmpS1232 < 0) {
      int32_t _M0L6_2atmpS1233 = _M0Lm5indexS460;
      int32_t _M0L6_2atmpS1234;
      int32_t _M0L6_2atmpS1235;
      if (
        _M0L6_2atmpS1233 < 0
        || _M0L6_2atmpS1233 >= Moonbit_array_length(_M0L6resultS459)
      ) {
        #line 565 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS459[_M0L6_2atmpS1233] = 45;
      _M0L6_2atmpS1234 = _M0Lm5indexS460;
      _M0Lm5indexS460 = _M0L6_2atmpS1234 + 1;
      _M0L6_2atmpS1235 = _M0Lm3expS465;
      _M0Lm3expS465 = -_M0L6_2atmpS1235;
    } else {
      int32_t _M0L6_2atmpS1236 = _M0Lm5indexS460;
      int32_t _M0L6_2atmpS1237;
      if (
        _M0L6_2atmpS1236 < 0
        || _M0L6_2atmpS1236 >= Moonbit_array_length(_M0L6resultS459)
      ) {
        #line 569 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS459[_M0L6_2atmpS1236] = 43;
      _M0L6_2atmpS1237 = _M0Lm5indexS460;
      _M0Lm5indexS460 = _M0L6_2atmpS1237 + 1;
    }
    _M0L6_2atmpS1238 = _M0Lm3expS465;
    if (_M0L6_2atmpS1238 >= 100) {
      int32_t _M0L6_2atmpS1254 = _M0Lm3expS465;
      int32_t _M0L1aS473 = _M0L6_2atmpS1254 / 100;
      int32_t _M0L6_2atmpS1253 = _M0Lm3expS465;
      int32_t _M0L6_2atmpS1252 = _M0L6_2atmpS1253 / 10;
      int32_t _M0L1bS474 = _M0L6_2atmpS1252 % 10;
      int32_t _M0L6_2atmpS1251 = _M0Lm3expS465;
      int32_t _M0L1cS475 = _M0L6_2atmpS1251 % 10;
      int32_t _M0L6_2atmpS1239 = _M0Lm5indexS460;
      int32_t _M0L6_2atmpS1241 = 48 + _M0L1aS473;
      int32_t _M0L6_2atmpS1240 = _M0L6_2atmpS1241 & 0xff;
      int32_t _M0L6_2atmpS1245;
      int32_t _M0L6_2atmpS1242;
      int32_t _M0L6_2atmpS1244;
      int32_t _M0L6_2atmpS1243;
      int32_t _M0L6_2atmpS1249;
      int32_t _M0L6_2atmpS1246;
      int32_t _M0L6_2atmpS1248;
      int32_t _M0L6_2atmpS1247;
      int32_t _M0L6_2atmpS1250;
      if (
        _M0L6_2atmpS1239 < 0
        || _M0L6_2atmpS1239 >= Moonbit_array_length(_M0L6resultS459)
      ) {
        #line 576 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS459[_M0L6_2atmpS1239] = _M0L6_2atmpS1240;
      _M0L6_2atmpS1245 = _M0Lm5indexS460;
      _M0L6_2atmpS1242 = _M0L6_2atmpS1245 + 1;
      _M0L6_2atmpS1244 = 48 + _M0L1bS474;
      _M0L6_2atmpS1243 = _M0L6_2atmpS1244 & 0xff;
      if (
        _M0L6_2atmpS1242 < 0
        || _M0L6_2atmpS1242 >= Moonbit_array_length(_M0L6resultS459)
      ) {
        #line 577 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS459[_M0L6_2atmpS1242] = _M0L6_2atmpS1243;
      _M0L6_2atmpS1249 = _M0Lm5indexS460;
      _M0L6_2atmpS1246 = _M0L6_2atmpS1249 + 2;
      _M0L6_2atmpS1248 = 48 + _M0L1cS475;
      _M0L6_2atmpS1247 = _M0L6_2atmpS1248 & 0xff;
      if (
        _M0L6_2atmpS1246 < 0
        || _M0L6_2atmpS1246 >= Moonbit_array_length(_M0L6resultS459)
      ) {
        #line 578 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS459[_M0L6_2atmpS1246] = _M0L6_2atmpS1247;
      _M0L6_2atmpS1250 = _M0Lm5indexS460;
      _M0Lm5indexS460 = _M0L6_2atmpS1250 + 3;
    } else {
      int32_t _M0L6_2atmpS1255 = _M0Lm3expS465;
      if (_M0L6_2atmpS1255 >= 10) {
        int32_t _M0L6_2atmpS1265 = _M0Lm3expS465;
        int32_t _M0L1aS476 = _M0L6_2atmpS1265 / 10;
        int32_t _M0L6_2atmpS1264 = _M0Lm3expS465;
        int32_t _M0L1bS477 = _M0L6_2atmpS1264 % 10;
        int32_t _M0L6_2atmpS1256 = _M0Lm5indexS460;
        int32_t _M0L6_2atmpS1258 = 48 + _M0L1aS476;
        int32_t _M0L6_2atmpS1257 = _M0L6_2atmpS1258 & 0xff;
        int32_t _M0L6_2atmpS1262;
        int32_t _M0L6_2atmpS1259;
        int32_t _M0L6_2atmpS1261;
        int32_t _M0L6_2atmpS1260;
        int32_t _M0L6_2atmpS1263;
        if (
          _M0L6_2atmpS1256 < 0
          || _M0L6_2atmpS1256 >= Moonbit_array_length(_M0L6resultS459)
        ) {
          #line 583 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
          moonbit_panic();
        }
        _M0L6resultS459[_M0L6_2atmpS1256] = _M0L6_2atmpS1257;
        _M0L6_2atmpS1262 = _M0Lm5indexS460;
        _M0L6_2atmpS1259 = _M0L6_2atmpS1262 + 1;
        _M0L6_2atmpS1261 = 48 + _M0L1bS477;
        _M0L6_2atmpS1260 = _M0L6_2atmpS1261 & 0xff;
        if (
          _M0L6_2atmpS1259 < 0
          || _M0L6_2atmpS1259 >= Moonbit_array_length(_M0L6resultS459)
        ) {
          #line 584 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
          moonbit_panic();
        }
        _M0L6resultS459[_M0L6_2atmpS1259] = _M0L6_2atmpS1260;
        _M0L6_2atmpS1263 = _M0Lm5indexS460;
        _M0Lm5indexS460 = _M0L6_2atmpS1263 + 2;
      } else {
        int32_t _M0L6_2atmpS1266 = _M0Lm5indexS460;
        int32_t _M0L6_2atmpS1269 = _M0Lm3expS465;
        int32_t _M0L6_2atmpS1268 = 48 + _M0L6_2atmpS1269;
        int32_t _M0L6_2atmpS1267 = _M0L6_2atmpS1268 & 0xff;
        int32_t _M0L6_2atmpS1270;
        if (
          _M0L6_2atmpS1266 < 0
          || _M0L6_2atmpS1266 >= Moonbit_array_length(_M0L6resultS459)
        ) {
          #line 587 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
          moonbit_panic();
        }
        _M0L6resultS459[_M0L6_2atmpS1266] = _M0L6_2atmpS1267;
        _M0L6_2atmpS1270 = _M0Lm5indexS460;
        _M0Lm5indexS460 = _M0L6_2atmpS1270 + 1;
      }
    }
    _M0L6_2atmpS1271 = _M0Lm5indexS460;
    #line 590 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _result_1679
    = _M0FPB19string__from__bytes(_M0L6resultS459, 0, _M0L6_2atmpS1271);
    moonbit_decref(_M0L6resultS459);
    return _result_1679;
  } else {
    int32_t _M0L6_2atmpS1280 = _M0Lm3expS465;
    int32_t _M0L6_2atmpS1343;
    moonbit_string_t _result_1685;
    if (_M0L6_2atmpS1280 < 0) {
      int32_t _M0L6_2atmpS1281 = _M0Lm5indexS460;
      int32_t _M0L6_2atmpS1283;
      int32_t _M0L6_2atmpS1282;
      int32_t _M0L6_2atmpS1284;
      int32_t _M0L1iS478;
      int32_t _M0L6_2atmpS1299;
      int32_t _M0L6_2atmpS1301;
      int32_t _M0L6_2atmpS1300;
      int32_t _M0L7currentS480;
      int32_t _M0L1iS481;
      uint64_t _M0L6outputS482;
      if (
        _M0L6_2atmpS1281 < 0
        || _M0L6_2atmpS1281 >= Moonbit_array_length(_M0L6resultS459)
      ) {
        #line 595 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS459[_M0L6_2atmpS1281] = 48;
      _M0L6_2atmpS1283 = _M0Lm5indexS460;
      _M0L6_2atmpS1282 = _M0L6_2atmpS1283 + 1;
      if (
        _M0L6_2atmpS1282 < 0
        || _M0L6_2atmpS1282 >= Moonbit_array_length(_M0L6resultS459)
      ) {
        #line 596 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS459[_M0L6_2atmpS1282] = 46;
      _M0L6_2atmpS1284 = _M0Lm5indexS460;
      _M0Lm5indexS460 = _M0L6_2atmpS1284 + 2;
      _M0L1iS478 = -1;
      while (1) {
        int32_t _M0L6_2atmpS1285 = _M0Lm3expS465;
        if (_M0L1iS478 > _M0L6_2atmpS1285) {
          int32_t _M0L6_2atmpS1288 = _M0Lm5indexS460;
          int32_t _M0L6_2atmpS1287 = _M0L6_2atmpS1288 - _M0L1iS478;
          int32_t _M0L6_2atmpS1286 = _M0L6_2atmpS1287 - 1;
          int32_t _M0L6_2atmpS1289;
          if (
            _M0L6_2atmpS1286 < 0
            || _M0L6_2atmpS1286 >= Moonbit_array_length(_M0L6resultS459)
          ) {
            #line 599 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
            moonbit_panic();
          }
          _M0L6resultS459[_M0L6_2atmpS1286] = 48;
          _M0L6_2atmpS1289 = _M0L1iS478 - 1;
          _M0L1iS478 = _M0L6_2atmpS1289;
          continue;
        }
        break;
      }
      _M0L6_2atmpS1299 = _M0Lm5indexS460;
      _M0L6_2atmpS1301 = _M0Lm3expS465;
      _M0L6_2atmpS1300 = -1 - _M0L6_2atmpS1301;
      _M0L7currentS480 = _M0L6_2atmpS1299 + _M0L6_2atmpS1300;
      _M0L1iS481 = 0;
      _M0L6outputS482 = _M0L6outputS462;
      while (1) {
        if (_M0L1iS481 < _M0L7olengthS464) {
          int32_t _M0L6_2atmpS1296 = _M0L7currentS480 + _M0L7olengthS464;
          int32_t _M0L6_2atmpS1295 = _M0L6_2atmpS1296 - _M0L1iS481;
          int32_t _M0L6_2atmpS1290 = _M0L6_2atmpS1295 - 1;
          uint64_t _M0L6_2atmpS1294 = _M0L6outputS482 % 10ull;
          int32_t _M0L6_2atmpS1293 = (int32_t)_M0L6_2atmpS1294;
          int32_t _M0L6_2atmpS1292 = 48 + _M0L6_2atmpS1293;
          int32_t _M0L6_2atmpS1291 = _M0L6_2atmpS1292 & 0xff;
          int32_t _M0L6_2atmpS1297;
          uint64_t _M0L6_2atmpS1298;
          if (
            _M0L6_2atmpS1290 < 0
            || _M0L6_2atmpS1290 >= Moonbit_array_length(_M0L6resultS459)
          ) {
            #line 603 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
            moonbit_panic();
          }
          _M0L6resultS459[_M0L6_2atmpS1290] = _M0L6_2atmpS1291;
          _M0L6_2atmpS1297 = _M0L1iS481 + 1;
          _M0L6_2atmpS1298 = _M0L6outputS482 / 10ull;
          _M0L1iS481 = _M0L6_2atmpS1297;
          _M0L6outputS482 = _M0L6_2atmpS1298;
          continue;
        }
        break;
      }
      _M0Lm5indexS460 = _M0L7currentS480 + _M0L7olengthS464;
    } else {
      int32_t _M0L6_2atmpS1303 = _M0Lm3expS465;
      int32_t _M0L6_2atmpS1302 = _M0L6_2atmpS1303 + 1;
      if (_M0L6_2atmpS1302 >= _M0L7olengthS464) {
        int32_t _M0L1iS484 = 0;
        uint64_t _M0L6outputS485 = _M0L6outputS462;
        int32_t _M0L6_2atmpS1314;
        int32_t _M0L6_2atmpS1319;
        int32_t _M0L7_2abindS487;
        int32_t _M0L1iS488;
        int32_t _M0L6_2atmpS1320;
        int32_t _M0L6_2atmpS1323;
        int32_t _M0L6_2atmpS1322;
        int32_t _M0L6_2atmpS1321;
        while (1) {
          if (_M0L1iS484 < _M0L7olengthS464) {
            int32_t _M0L6_2atmpS1311 = _M0Lm5indexS460;
            int32_t _M0L6_2atmpS1310 = _M0L6_2atmpS1311 + _M0L7olengthS464;
            int32_t _M0L6_2atmpS1309 = _M0L6_2atmpS1310 - _M0L1iS484;
            int32_t _M0L6_2atmpS1304 = _M0L6_2atmpS1309 - 1;
            uint64_t _M0L6_2atmpS1308 = _M0L6outputS485 % 10ull;
            int32_t _M0L6_2atmpS1307 = (int32_t)_M0L6_2atmpS1308;
            int32_t _M0L6_2atmpS1306 = 48 + _M0L6_2atmpS1307;
            int32_t _M0L6_2atmpS1305 = _M0L6_2atmpS1306 & 0xff;
            int32_t _M0L6_2atmpS1312;
            uint64_t _M0L6_2atmpS1313;
            if (
              _M0L6_2atmpS1304 < 0
              || _M0L6_2atmpS1304 >= Moonbit_array_length(_M0L6resultS459)
            ) {
              #line 610 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
              moonbit_panic();
            }
            _M0L6resultS459[_M0L6_2atmpS1304] = _M0L6_2atmpS1305;
            _M0L6_2atmpS1312 = _M0L1iS484 + 1;
            _M0L6_2atmpS1313 = _M0L6outputS485 / 10ull;
            _M0L1iS484 = _M0L6_2atmpS1312;
            _M0L6outputS485 = _M0L6_2atmpS1313;
            continue;
          }
          break;
        }
        _M0L6_2atmpS1314 = _M0Lm5indexS460;
        _M0Lm5indexS460 = _M0L6_2atmpS1314 + _M0L7olengthS464;
        _M0L6_2atmpS1319 = _M0Lm3expS465;
        _M0L7_2abindS487 = _M0L6_2atmpS1319 + 1;
        _M0L1iS488 = _M0L7olengthS464;
        while (1) {
          if (_M0L1iS488 < _M0L7_2abindS487) {
            int32_t _M0L6_2atmpS1317 = _M0Lm5indexS460;
            int32_t _M0L6_2atmpS1316 = _M0L6_2atmpS1317 + _M0L1iS488;
            int32_t _M0L6_2atmpS1315 = _M0L6_2atmpS1316 - _M0L7olengthS464;
            int32_t _M0L6_2atmpS1318;
            if (
              _M0L6_2atmpS1315 < 0
              || _M0L6_2atmpS1315 >= Moonbit_array_length(_M0L6resultS459)
            ) {
              #line 615 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
              moonbit_panic();
            }
            _M0L6resultS459[_M0L6_2atmpS1315] = 48;
            _M0L6_2atmpS1318 = _M0L1iS488 + 1;
            _M0L1iS488 = _M0L6_2atmpS1318;
            continue;
          }
          break;
        }
        _M0L6_2atmpS1320 = _M0Lm5indexS460;
        _M0L6_2atmpS1323 = _M0Lm3expS465;
        _M0L6_2atmpS1322 = _M0L6_2atmpS1323 + 1;
        _M0L6_2atmpS1321 = _M0L6_2atmpS1322 - _M0L7olengthS464;
        _M0Lm5indexS460 = _M0L6_2atmpS1320 + _M0L6_2atmpS1321;
      } else {
        int32_t _M0L6_2atmpS1340 = _M0Lm5indexS460;
        int32_t _M0L6_2atmpS1339 = _M0L6_2atmpS1340 + 1;
        int32_t _M0L1iS490 = 0;
        int32_t _M0L7currentS491 = _M0L6_2atmpS1339;
        uint64_t _M0L6outputS492 = _M0L6outputS462;
        int32_t _M0L6_2atmpS1341;
        int32_t _M0L6_2atmpS1342;
        while (1) {
          if (_M0L1iS490 < _M0L7olengthS464) {
            int32_t _M0L6_2atmpS1335 = _M0L7olengthS464 - _M0L1iS490;
            int32_t _M0L6_2atmpS1333 = _M0L6_2atmpS1335 - 1;
            int32_t _M0L6_2atmpS1334 = _M0Lm3expS465;
            int32_t _M0L7currentS493;
            int32_t _M0L6_2atmpS1330;
            int32_t _M0L6_2atmpS1329;
            int32_t _M0L6_2atmpS1324;
            uint64_t _M0L6_2atmpS1328;
            int32_t _M0L6_2atmpS1327;
            int32_t _M0L6_2atmpS1326;
            int32_t _M0L6_2atmpS1325;
            int32_t _M0L6_2atmpS1331;
            uint64_t _M0L6_2atmpS1332;
            if (_M0L6_2atmpS1333 == _M0L6_2atmpS1334) {
              int32_t _M0L6_2atmpS1338 = _M0L7currentS491 + _M0L7olengthS464;
              int32_t _M0L6_2atmpS1337 = _M0L6_2atmpS1338 - _M0L1iS490;
              int32_t _M0L6_2atmpS1336 = _M0L6_2atmpS1337 - 1;
              if (
                _M0L6_2atmpS1336 < 0
                || _M0L6_2atmpS1336 >= Moonbit_array_length(_M0L6resultS459)
              ) {
                #line 622 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
                moonbit_panic();
              }
              _M0L6resultS459[_M0L6_2atmpS1336] = 46;
              _M0L7currentS493 = _M0L7currentS491 - 1;
            } else {
              _M0L7currentS493 = _M0L7currentS491;
            }
            _M0L6_2atmpS1330 = _M0L7currentS493 + _M0L7olengthS464;
            _M0L6_2atmpS1329 = _M0L6_2atmpS1330 - _M0L1iS490;
            _M0L6_2atmpS1324 = _M0L6_2atmpS1329 - 1;
            _M0L6_2atmpS1328 = _M0L6outputS492 % 10ull;
            _M0L6_2atmpS1327 = (int32_t)_M0L6_2atmpS1328;
            _M0L6_2atmpS1326 = 48 + _M0L6_2atmpS1327;
            _M0L6_2atmpS1325 = _M0L6_2atmpS1326 & 0xff;
            if (
              _M0L6_2atmpS1324 < 0
              || _M0L6_2atmpS1324 >= Moonbit_array_length(_M0L6resultS459)
            ) {
              #line 627 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
              moonbit_panic();
            }
            _M0L6resultS459[_M0L6_2atmpS1324] = _M0L6_2atmpS1325;
            _M0L6_2atmpS1331 = _M0L1iS490 + 1;
            _M0L6_2atmpS1332 = _M0L6outputS492 / 10ull;
            _M0L1iS490 = _M0L6_2atmpS1331;
            _M0L7currentS491 = _M0L7currentS493;
            _M0L6outputS492 = _M0L6_2atmpS1332;
            continue;
          }
          break;
        }
        _M0L6_2atmpS1341 = _M0Lm5indexS460;
        _M0L6_2atmpS1342 = _M0L7olengthS464 + 1;
        _M0Lm5indexS460 = _M0L6_2atmpS1341 + _M0L6_2atmpS1342;
      }
    }
    _M0L6_2atmpS1343 = _M0Lm5indexS460;
    #line 632 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _result_1685
    = _M0FPB19string__from__bytes(_M0L6resultS459, 0, _M0L6_2atmpS1343);
    moonbit_decref(_M0L6resultS459);
    return _result_1685;
  }
}

struct _M0TPB17FloatingDecimal64* _M0FPB3d2d(
  uint64_t _M0L12ieeeMantissaS405,
  uint32_t _M0L12ieeeExponentS404
) {
  int32_t _M0Lm2e2S402;
  uint64_t _M0Lm2m2S403;
  uint64_t _M0L6_2atmpS1217;
  uint64_t _M0L6_2atmpS1216;
  int32_t _M0L4evenS406;
  uint64_t _M0L6_2atmpS1215;
  uint64_t _M0L2mvS407;
  int32_t _M0L7mmShiftS408;
  uint64_t _M0Lm2vrS409;
  uint64_t _M0Lm2vpS410;
  uint64_t _M0Lm2vmS411;
  int32_t _M0Lm3e10S412;
  int32_t _M0Lm17vmIsTrailingZerosS413;
  int32_t _M0Lm17vrIsTrailingZerosS414;
  int32_t _M0L6_2atmpS1117;
  int32_t _M0Lm7removedS433;
  int32_t _M0Lm16lastRemovedDigitS434;
  uint64_t _M0Lm6outputS435;
  int32_t _M0L6_2atmpS1213;
  int32_t _M0L6_2atmpS1214;
  int32_t _M0L3expS458;
  uint64_t _M0L6_2atmpS1212;
  struct _M0TPB17FloatingDecimal64* _block_1691;
  #line 347 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0Lm2e2S402 = 0;
  _M0Lm2m2S403 = 0ull;
  if (_M0L12ieeeExponentS404 == 0u) {
    _M0Lm2e2S402 = -1076;
    _M0Lm2m2S403 = _M0L12ieeeMantissaS405;
  } else {
    int32_t _M0L6_2atmpS1116 = *(int32_t*)&_M0L12ieeeExponentS404;
    int32_t _M0L6_2atmpS1115 = _M0L6_2atmpS1116 - 1023;
    int32_t _M0L6_2atmpS1114 = _M0L6_2atmpS1115 - 52;
    _M0Lm2e2S402 = _M0L6_2atmpS1114 - 2;
    _M0Lm2m2S403 = 4503599627370496ull | _M0L12ieeeMantissaS405;
  }
  _M0L6_2atmpS1217 = _M0Lm2m2S403;
  _M0L6_2atmpS1216 = _M0L6_2atmpS1217 & 1ull;
  _M0L4evenS406 = _M0L6_2atmpS1216 == 0ull;
  _M0L6_2atmpS1215 = _M0Lm2m2S403;
  _M0L2mvS407 = 4ull * _M0L6_2atmpS1215;
  _M0L7mmShiftS408
  = _M0L12ieeeMantissaS405 != 0ull || _M0L12ieeeExponentS404 <= 1u;
  _M0Lm2vrS409 = 0ull;
  _M0Lm2vpS410 = 0ull;
  _M0Lm2vmS411 = 0ull;
  _M0Lm3e10S412 = 0;
  _M0Lm17vmIsTrailingZerosS413 = 0;
  _M0Lm17vrIsTrailingZerosS414 = 0;
  _M0L6_2atmpS1117 = _M0Lm2e2S402;
  if (_M0L6_2atmpS1117 >= 0) {
    int32_t _M0L6_2atmpS1139 = _M0Lm2e2S402;
    int32_t _M0L6_2atmpS1135;
    int32_t _M0L6_2atmpS1138;
    int32_t _M0L6_2atmpS1137;
    int32_t _M0L6_2atmpS1136;
    int32_t _M0L1qS415;
    int32_t _M0L6_2atmpS1134;
    int32_t _M0L6_2atmpS1133;
    int32_t _M0L1kS416;
    int32_t _M0L6_2atmpS1132;
    int32_t _M0L6_2atmpS1131;
    int32_t _M0L6_2atmpS1130;
    int32_t _M0L1iS417;
    struct _M0TPB8Pow5Pair _M0L4pow5S418;
    uint64_t _M0L6_2atmpS1129;
    struct _M0TPB19MulShiftAll64Result _M0L7_2abindS419;
    uint64_t _M0L8_2avrOutS420;
    uint64_t _M0L8_2avpOutS421;
    uint64_t _M0L8_2avmOutS422;
    #line 383 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1135 = _M0FPB9log10Pow2(_M0L6_2atmpS1139);
    _M0L6_2atmpS1138 = _M0Lm2e2S402;
    _M0L6_2atmpS1137 = _M0L6_2atmpS1138 > 3;
    #line 383 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1136 = _M0MPC14bool4Bool7to__int(_M0L6_2atmpS1137);
    _M0L1qS415 = _M0L6_2atmpS1135 - _M0L6_2atmpS1136;
    _M0Lm3e10S412 = _M0L1qS415;
    #line 385 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1134 = _M0FPB8pow5bits(_M0L1qS415);
    _M0L6_2atmpS1133 = 125 + _M0L6_2atmpS1134;
    _M0L1kS416 = _M0L6_2atmpS1133 - 1;
    _M0L6_2atmpS1132 = _M0Lm2e2S402;
    _M0L6_2atmpS1131 = -_M0L6_2atmpS1132;
    _M0L6_2atmpS1130 = _M0L6_2atmpS1131 + _M0L1qS415;
    _M0L1iS417 = _M0L6_2atmpS1130 + _M0L1kS416;
    #line 387 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L4pow5S418 = _M0FPB22double__computeInvPow5(_M0L1qS415);
    _M0L6_2atmpS1129 = _M0Lm2m2S403;
    #line 388 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L7_2abindS419
    = _M0FPB13mulShiftAll64(_M0L6_2atmpS1129, _M0L4pow5S418, _M0L1iS417, _M0L7mmShiftS408);
    _M0L8_2avrOutS420 = _M0L7_2abindS419.$0;
    _M0L8_2avpOutS421 = _M0L7_2abindS419.$1;
    _M0L8_2avmOutS422 = _M0L7_2abindS419.$2;
    _M0Lm2vrS409 = _M0L8_2avrOutS420;
    _M0Lm2vpS410 = _M0L8_2avpOutS421;
    _M0Lm2vmS411 = _M0L8_2avmOutS422;
    if (_M0L1qS415 <= 21) {
      int32_t _M0L6_2atmpS1125 = (int32_t)_M0L2mvS407;
      uint64_t _M0L6_2atmpS1128 = _M0L2mvS407 / 5ull;
      int32_t _M0L6_2atmpS1127 = (int32_t)_M0L6_2atmpS1128;
      int32_t _M0L6_2atmpS1126 = 5 * _M0L6_2atmpS1127;
      int32_t _M0L6mvMod5S423 = _M0L6_2atmpS1125 - _M0L6_2atmpS1126;
      if (_M0L6mvMod5S423 == 0) {
        #line 400 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0Lm17vrIsTrailingZerosS414
        = _M0FPB18multipleOfPowerOf5(_M0L2mvS407, _M0L1qS415);
      } else if (_M0L4evenS406) {
        uint64_t _M0L6_2atmpS1119 = _M0L2mvS407 - 1ull;
        uint64_t _M0L6_2atmpS1120;
        uint64_t _M0L6_2atmpS1118;
        #line 406 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0L6_2atmpS1120 = _M0MPC14bool4Bool10to__uint64(_M0L7mmShiftS408);
        _M0L6_2atmpS1118 = _M0L6_2atmpS1119 - _M0L6_2atmpS1120;
        #line 405 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0Lm17vmIsTrailingZerosS413
        = _M0FPB18multipleOfPowerOf5(_M0L6_2atmpS1118, _M0L1qS415);
      } else {
        uint64_t _M0L6_2atmpS1121 = _M0Lm2vpS410;
        uint64_t _M0L6_2atmpS1124 = _M0L2mvS407 + 2ull;
        int32_t _M0L6_2atmpS1123;
        uint64_t _M0L6_2atmpS1122;
        #line 410 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0L6_2atmpS1123
        = _M0FPB18multipleOfPowerOf5(_M0L6_2atmpS1124, _M0L1qS415);
        #line 410 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0L6_2atmpS1122 = _M0MPC14bool4Bool10to__uint64(_M0L6_2atmpS1123);
        _M0Lm2vpS410 = _M0L6_2atmpS1121 - _M0L6_2atmpS1122;
      }
    }
  } else {
    int32_t _M0L6_2atmpS1153 = _M0Lm2e2S402;
    int32_t _M0L6_2atmpS1152 = -_M0L6_2atmpS1153;
    int32_t _M0L6_2atmpS1147;
    int32_t _M0L6_2atmpS1151;
    int32_t _M0L6_2atmpS1150;
    int32_t _M0L6_2atmpS1149;
    int32_t _M0L6_2atmpS1148;
    int32_t _M0L1qS424;
    int32_t _M0L6_2atmpS1140;
    int32_t _M0L6_2atmpS1146;
    int32_t _M0L6_2atmpS1145;
    int32_t _M0L1iS425;
    int32_t _M0L6_2atmpS1144;
    int32_t _M0L1kS426;
    int32_t _M0L1jS427;
    struct _M0TPB8Pow5Pair _M0L4pow5S428;
    uint64_t _M0L6_2atmpS1143;
    struct _M0TPB19MulShiftAll64Result _M0L7_2abindS429;
    uint64_t _M0L8_2avrOutS430;
    uint64_t _M0L8_2avpOutS431;
    uint64_t _M0L8_2avmOutS432;
    #line 415 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1147 = _M0FPB9log10Pow5(_M0L6_2atmpS1152);
    _M0L6_2atmpS1151 = _M0Lm2e2S402;
    _M0L6_2atmpS1150 = -_M0L6_2atmpS1151;
    _M0L6_2atmpS1149 = _M0L6_2atmpS1150 > 1;
    #line 415 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1148 = _M0MPC14bool4Bool7to__int(_M0L6_2atmpS1149);
    _M0L1qS424 = _M0L6_2atmpS1147 - _M0L6_2atmpS1148;
    _M0L6_2atmpS1140 = _M0Lm2e2S402;
    _M0Lm3e10S412 = _M0L1qS424 + _M0L6_2atmpS1140;
    _M0L6_2atmpS1146 = _M0Lm2e2S402;
    _M0L6_2atmpS1145 = -_M0L6_2atmpS1146;
    _M0L1iS425 = _M0L6_2atmpS1145 - _M0L1qS424;
    #line 418 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1144 = _M0FPB8pow5bits(_M0L1iS425);
    _M0L1kS426 = _M0L6_2atmpS1144 - 125;
    _M0L1jS427 = _M0L1qS424 - _M0L1kS426;
    #line 420 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L4pow5S428 = _M0FPB19double__computePow5(_M0L1iS425);
    _M0L6_2atmpS1143 = _M0Lm2m2S403;
    #line 421 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L7_2abindS429
    = _M0FPB13mulShiftAll64(_M0L6_2atmpS1143, _M0L4pow5S428, _M0L1jS427, _M0L7mmShiftS408);
    _M0L8_2avrOutS430 = _M0L7_2abindS429.$0;
    _M0L8_2avpOutS431 = _M0L7_2abindS429.$1;
    _M0L8_2avmOutS432 = _M0L7_2abindS429.$2;
    _M0Lm2vrS409 = _M0L8_2avrOutS430;
    _M0Lm2vpS410 = _M0L8_2avpOutS431;
    _M0Lm2vmS411 = _M0L8_2avmOutS432;
    if (_M0L1qS424 <= 1) {
      _M0Lm17vrIsTrailingZerosS414 = 1;
      if (_M0L4evenS406) {
        int32_t _M0L6_2atmpS1141;
        #line 432 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0L6_2atmpS1141 = _M0MPC14bool4Bool7to__int(_M0L7mmShiftS408);
        _M0Lm17vmIsTrailingZerosS413 = _M0L6_2atmpS1141 == 1;
      } else {
        uint64_t _M0L6_2atmpS1142 = _M0Lm2vpS410;
        _M0Lm2vpS410 = _M0L6_2atmpS1142 - 1ull;
      }
    } else if (_M0L1qS424 < 63) {
      #line 437 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0Lm17vrIsTrailingZerosS414
      = _M0FPB18multipleOfPowerOf2(_M0L2mvS407, _M0L1qS424);
    }
  }
  _M0Lm7removedS433 = 0;
  _M0Lm16lastRemovedDigitS434 = 0;
  _M0Lm6outputS435 = 0ull;
  if (_M0Lm17vmIsTrailingZerosS413 || _M0Lm17vrIsTrailingZerosS414) {
    int32_t _if__result_1688;
    uint64_t _M0L6_2atmpS1183;
    uint64_t _M0L6_2atmpS1189;
    uint64_t _M0L6_2atmpS1190;
    int32_t _if__result_1689;
    int32_t _M0L6_2atmpS1186;
    int64_t _M0L6_2atmpS1185;
    uint64_t _M0L6_2atmpS1184;
    while (1) {
      uint64_t _M0L6_2atmpS1166 = _M0Lm2vpS410;
      uint64_t _M0L7vpDiv10S436 = _M0L6_2atmpS1166 / 10ull;
      uint64_t _M0L6_2atmpS1165 = _M0Lm2vmS411;
      uint64_t _M0L7vmDiv10S437 = _M0L6_2atmpS1165 / 10ull;
      uint64_t _M0L6_2atmpS1164;
      int32_t _M0L6_2atmpS1161;
      int32_t _M0L6_2atmpS1163;
      int32_t _M0L6_2atmpS1162;
      int32_t _M0L7vmMod10S439;
      uint64_t _M0L6_2atmpS1160;
      uint64_t _M0L7vrDiv10S440;
      uint64_t _M0L6_2atmpS1159;
      int32_t _M0L6_2atmpS1156;
      int32_t _M0L6_2atmpS1158;
      int32_t _M0L6_2atmpS1157;
      int32_t _M0L7vrMod10S441;
      int32_t _M0L6_2atmpS1155;
      if (_M0L7vpDiv10S436 <= _M0L7vmDiv10S437) {
        break;
      }
      _M0L6_2atmpS1164 = _M0Lm2vmS411;
      _M0L6_2atmpS1161 = (int32_t)_M0L6_2atmpS1164;
      _M0L6_2atmpS1163 = (int32_t)_M0L7vmDiv10S437;
      _M0L6_2atmpS1162 = 10 * _M0L6_2atmpS1163;
      _M0L7vmMod10S439 = _M0L6_2atmpS1161 - _M0L6_2atmpS1162;
      _M0L6_2atmpS1160 = _M0Lm2vrS409;
      _M0L7vrDiv10S440 = _M0L6_2atmpS1160 / 10ull;
      _M0L6_2atmpS1159 = _M0Lm2vrS409;
      _M0L6_2atmpS1156 = (int32_t)_M0L6_2atmpS1159;
      _M0L6_2atmpS1158 = (int32_t)_M0L7vrDiv10S440;
      _M0L6_2atmpS1157 = 10 * _M0L6_2atmpS1158;
      _M0L7vrMod10S441 = _M0L6_2atmpS1156 - _M0L6_2atmpS1157;
      _M0Lm17vmIsTrailingZerosS413
      = _M0Lm17vmIsTrailingZerosS413 && _M0L7vmMod10S439 == 0;
      if (_M0Lm17vrIsTrailingZerosS414) {
        int32_t _M0L6_2atmpS1154 = _M0Lm16lastRemovedDigitS434;
        _M0Lm17vrIsTrailingZerosS414 = _M0L6_2atmpS1154 == 0;
      } else {
        _M0Lm17vrIsTrailingZerosS414 = 0;
      }
      _M0Lm16lastRemovedDigitS434 = _M0L7vrMod10S441;
      _M0Lm2vrS409 = _M0L7vrDiv10S440;
      _M0Lm2vpS410 = _M0L7vpDiv10S436;
      _M0Lm2vmS411 = _M0L7vmDiv10S437;
      _M0L6_2atmpS1155 = _M0Lm7removedS433;
      _M0Lm7removedS433 = _M0L6_2atmpS1155 + 1;
      continue;
      break;
    }
    if (_M0Lm17vmIsTrailingZerosS413) {
      while (1) {
        uint64_t _M0L6_2atmpS1179 = _M0Lm2vmS411;
        uint64_t _M0L7vmDiv10S442 = _M0L6_2atmpS1179 / 10ull;
        uint64_t _M0L6_2atmpS1178 = _M0Lm2vmS411;
        int32_t _M0L6_2atmpS1175 = (int32_t)_M0L6_2atmpS1178;
        int32_t _M0L6_2atmpS1177 = (int32_t)_M0L7vmDiv10S442;
        int32_t _M0L6_2atmpS1176 = 10 * _M0L6_2atmpS1177;
        int32_t _M0L7vmMod10S443 = _M0L6_2atmpS1175 - _M0L6_2atmpS1176;
        uint64_t _M0L6_2atmpS1174;
        uint64_t _M0L7vpDiv10S445;
        uint64_t _M0L6_2atmpS1173;
        uint64_t _M0L7vrDiv10S446;
        uint64_t _M0L6_2atmpS1172;
        int32_t _M0L6_2atmpS1169;
        int32_t _M0L6_2atmpS1171;
        int32_t _M0L6_2atmpS1170;
        int32_t _M0L7vrMod10S447;
        int32_t _M0L6_2atmpS1168;
        if (_M0L7vmMod10S443 != 0) {
          break;
        }
        _M0L6_2atmpS1174 = _M0Lm2vpS410;
        _M0L7vpDiv10S445 = _M0L6_2atmpS1174 / 10ull;
        _M0L6_2atmpS1173 = _M0Lm2vrS409;
        _M0L7vrDiv10S446 = _M0L6_2atmpS1173 / 10ull;
        _M0L6_2atmpS1172 = _M0Lm2vrS409;
        _M0L6_2atmpS1169 = (int32_t)_M0L6_2atmpS1172;
        _M0L6_2atmpS1171 = (int32_t)_M0L7vrDiv10S446;
        _M0L6_2atmpS1170 = 10 * _M0L6_2atmpS1171;
        _M0L7vrMod10S447 = _M0L6_2atmpS1169 - _M0L6_2atmpS1170;
        if (_M0Lm17vrIsTrailingZerosS414) {
          int32_t _M0L6_2atmpS1167 = _M0Lm16lastRemovedDigitS434;
          _M0Lm17vrIsTrailingZerosS414 = _M0L6_2atmpS1167 == 0;
        } else {
          _M0Lm17vrIsTrailingZerosS414 = 0;
        }
        _M0Lm16lastRemovedDigitS434 = _M0L7vrMod10S447;
        _M0Lm2vrS409 = _M0L7vrDiv10S446;
        _M0Lm2vpS410 = _M0L7vpDiv10S445;
        _M0Lm2vmS411 = _M0L7vmDiv10S442;
        _M0L6_2atmpS1168 = _M0Lm7removedS433;
        _M0Lm7removedS433 = _M0L6_2atmpS1168 + 1;
        continue;
        break;
      }
    }
    if (_M0Lm17vrIsTrailingZerosS414) {
      int32_t _M0L6_2atmpS1182 = _M0Lm16lastRemovedDigitS434;
      if (_M0L6_2atmpS1182 == 5) {
        uint64_t _M0L6_2atmpS1181 = _M0Lm2vrS409;
        uint64_t _M0L6_2atmpS1180 = _M0L6_2atmpS1181 % 2ull;
        _if__result_1688 = _M0L6_2atmpS1180 == 0ull;
      } else {
        _if__result_1688 = 0;
      }
    } else {
      _if__result_1688 = 0;
    }
    if (_if__result_1688) {
      _M0Lm16lastRemovedDigitS434 = 4;
    }
    _M0L6_2atmpS1183 = _M0Lm2vrS409;
    _M0L6_2atmpS1189 = _M0Lm2vrS409;
    _M0L6_2atmpS1190 = _M0Lm2vmS411;
    if (_M0L6_2atmpS1189 == _M0L6_2atmpS1190) {
      if (!_M0L4evenS406) {
        _if__result_1689 = 1;
      } else {
        int32_t _M0L6_2atmpS1188 = _M0Lm17vmIsTrailingZerosS413;
        _if__result_1689 = !_M0L6_2atmpS1188;
      }
    } else {
      _if__result_1689 = 0;
    }
    if (_if__result_1689) {
      _M0L6_2atmpS1186 = 1;
    } else {
      int32_t _M0L6_2atmpS1187 = _M0Lm16lastRemovedDigitS434;
      _M0L6_2atmpS1186 = _M0L6_2atmpS1187 >= 5;
    }
    #line 487 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1185 = _M0MPC14bool4Bool9to__int64(_M0L6_2atmpS1186);
    _M0L6_2atmpS1184 = *(uint64_t*)&_M0L6_2atmpS1185;
    _M0Lm6outputS435 = _M0L6_2atmpS1183 + _M0L6_2atmpS1184;
  } else {
    int32_t _M0Lm7roundUpS448 = 0;
    uint64_t _M0L6_2atmpS1211 = _M0Lm2vpS410;
    uint64_t _M0L8vpDiv100S449 = _M0L6_2atmpS1211 / 100ull;
    uint64_t _M0L6_2atmpS1210 = _M0Lm2vmS411;
    uint64_t _M0L8vmDiv100S450 = _M0L6_2atmpS1210 / 100ull;
    uint64_t _M0L6_2atmpS1205;
    uint64_t _M0L6_2atmpS1208;
    uint64_t _M0L6_2atmpS1209;
    int32_t _M0L6_2atmpS1207;
    uint64_t _M0L6_2atmpS1206;
    if (_M0L8vpDiv100S449 > _M0L8vmDiv100S450) {
      uint64_t _M0L6_2atmpS1196 = _M0Lm2vrS409;
      uint64_t _M0L8vrDiv100S451 = _M0L6_2atmpS1196 / 100ull;
      uint64_t _M0L6_2atmpS1195 = _M0Lm2vrS409;
      int32_t _M0L6_2atmpS1192 = (int32_t)_M0L6_2atmpS1195;
      int32_t _M0L6_2atmpS1194 = (int32_t)_M0L8vrDiv100S451;
      int32_t _M0L6_2atmpS1193 = 100 * _M0L6_2atmpS1194;
      int32_t _M0L8vrMod100S452 = _M0L6_2atmpS1192 - _M0L6_2atmpS1193;
      int32_t _M0L6_2atmpS1191;
      _M0Lm7roundUpS448 = _M0L8vrMod100S452 >= 50;
      _M0Lm2vrS409 = _M0L8vrDiv100S451;
      _M0Lm2vpS410 = _M0L8vpDiv100S449;
      _M0Lm2vmS411 = _M0L8vmDiv100S450;
      _M0L6_2atmpS1191 = _M0Lm7removedS433;
      _M0Lm7removedS433 = _M0L6_2atmpS1191 + 2;
    }
    while (1) {
      uint64_t _M0L6_2atmpS1204 = _M0Lm2vpS410;
      uint64_t _M0L7vpDiv10S453 = _M0L6_2atmpS1204 / 10ull;
      uint64_t _M0L6_2atmpS1203 = _M0Lm2vmS411;
      uint64_t _M0L7vmDiv10S454 = _M0L6_2atmpS1203 / 10ull;
      uint64_t _M0L6_2atmpS1202;
      uint64_t _M0L7vrDiv10S456;
      uint64_t _M0L6_2atmpS1201;
      int32_t _M0L6_2atmpS1198;
      int32_t _M0L6_2atmpS1200;
      int32_t _M0L6_2atmpS1199;
      int32_t _M0L7vrMod10S457;
      int32_t _M0L6_2atmpS1197;
      if (_M0L7vpDiv10S453 <= _M0L7vmDiv10S454) {
        break;
      }
      _M0L6_2atmpS1202 = _M0Lm2vrS409;
      _M0L7vrDiv10S456 = _M0L6_2atmpS1202 / 10ull;
      _M0L6_2atmpS1201 = _M0Lm2vrS409;
      _M0L6_2atmpS1198 = (int32_t)_M0L6_2atmpS1201;
      _M0L6_2atmpS1200 = (int32_t)_M0L7vrDiv10S456;
      _M0L6_2atmpS1199 = 10 * _M0L6_2atmpS1200;
      _M0L7vrMod10S457 = _M0L6_2atmpS1198 - _M0L6_2atmpS1199;
      _M0Lm7roundUpS448 = _M0L7vrMod10S457 >= 5;
      _M0Lm2vrS409 = _M0L7vrDiv10S456;
      _M0Lm2vpS410 = _M0L7vpDiv10S453;
      _M0Lm2vmS411 = _M0L7vmDiv10S454;
      _M0L6_2atmpS1197 = _M0Lm7removedS433;
      _M0Lm7removedS433 = _M0L6_2atmpS1197 + 1;
      continue;
      break;
    }
    _M0L6_2atmpS1205 = _M0Lm2vrS409;
    _M0L6_2atmpS1208 = _M0Lm2vrS409;
    _M0L6_2atmpS1209 = _M0Lm2vmS411;
    _M0L6_2atmpS1207
    = _M0L6_2atmpS1208 == _M0L6_2atmpS1209 || _M0Lm7roundUpS448;
    #line 522 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1206 = _M0MPC14bool4Bool10to__uint64(_M0L6_2atmpS1207);
    _M0Lm6outputS435 = _M0L6_2atmpS1205 + _M0L6_2atmpS1206;
  }
  _M0L6_2atmpS1213 = _M0Lm3e10S412;
  _M0L6_2atmpS1214 = _M0Lm7removedS433;
  _M0L3expS458 = _M0L6_2atmpS1213 + _M0L6_2atmpS1214;
  _M0L6_2atmpS1212 = _M0Lm6outputS435;
  _block_1691
  = (struct _M0TPB17FloatingDecimal64*)moonbit_malloc(sizeof(struct _M0TPB17FloatingDecimal64));
  Moonbit_object_header(_block_1691)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_1691->$0 = _M0L6_2atmpS1212;
  _block_1691->$1 = _M0L3expS458;
  return _block_1691;
}

uint64_t _M0MPC14bool4Bool10to__uint64(int32_t _M0L4selfS401) {
  #line 110 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bool.mbt"
  if (_M0L4selfS401) {
    return 1ull;
  } else {
    return 0ull;
  }
}

int64_t _M0MPC14bool4Bool9to__int64(int32_t _M0L4selfS400) {
  #line 58 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bool.mbt"
  if (_M0L4selfS400) {
    return 1ll;
  } else {
    return 0ll;
  }
}

int32_t _M0MPC14bool4Bool7to__int(int32_t _M0L4selfS399) {
  #line 32 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bool.mbt"
  if (_M0L4selfS399) {
    return 1;
  } else {
    return 0;
  }
}

int32_t _M0FPB17decimal__length17(uint64_t _M0L1vS398) {
  #line 280 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  if (_M0L1vS398 >= 10000000000000000ull) {
    return 17;
  }
  if (_M0L1vS398 >= 1000000000000000ull) {
    return 16;
  }
  if (_M0L1vS398 >= 100000000000000ull) {
    return 15;
  }
  if (_M0L1vS398 >= 10000000000000ull) {
    return 14;
  }
  if (_M0L1vS398 >= 1000000000000ull) {
    return 13;
  }
  if (_M0L1vS398 >= 100000000000ull) {
    return 12;
  }
  if (_M0L1vS398 >= 10000000000ull) {
    return 11;
  }
  if (_M0L1vS398 >= 1000000000ull) {
    return 10;
  }
  if (_M0L1vS398 >= 100000000ull) {
    return 9;
  }
  if (_M0L1vS398 >= 10000000ull) {
    return 8;
  }
  if (_M0L1vS398 >= 1000000ull) {
    return 7;
  }
  if (_M0L1vS398 >= 100000ull) {
    return 6;
  }
  if (_M0L1vS398 >= 10000ull) {
    return 5;
  }
  if (_M0L1vS398 >= 1000ull) {
    return 4;
  }
  if (_M0L1vS398 >= 100ull) {
    return 3;
  }
  if (_M0L1vS398 >= 10ull) {
    return 2;
  }
  return 1;
}

struct _M0TPB8Pow5Pair _M0FPB22double__computeInvPow5(int32_t _M0L1iS381) {
  int32_t _M0L6_2atmpS1113;
  int32_t _M0L6_2atmpS1112;
  int32_t _M0L4baseS380;
  int32_t _M0L5base2S382;
  int32_t _M0L6offsetS383;
  int32_t _M0L6_2atmpS1111;
  uint64_t _M0L4mul0S384;
  int32_t _M0L6_2atmpS1110;
  int32_t _M0L6_2atmpS1109;
  uint64_t _M0L4mul1S385;
  uint64_t _M0L1mS386;
  struct _M0TPB7Umul128 _M0L7_2abindS387;
  uint64_t _M0L7_2alow1S388;
  uint64_t _M0L8_2ahigh1S389;
  struct _M0TPB7Umul128 _M0L7_2abindS390;
  uint64_t _M0L7_2alow0S391;
  uint64_t _M0L8_2ahigh0S392;
  uint64_t _M0L3sumS393;
  uint64_t _M0Lm5high1S394;
  int32_t _M0L6_2atmpS1107;
  int32_t _M0L6_2atmpS1108;
  int32_t _M0L5deltaS395;
  uint64_t _M0L6_2atmpS1106;
  uint64_t _M0L6_2atmpS1098;
  int32_t _M0L6_2atmpS1105;
  uint32_t _M0L6_2atmpS1102;
  int32_t _M0L6_2atmpS1104;
  int32_t _M0L6_2atmpS1103;
  uint32_t _M0L6_2atmpS1101;
  uint32_t _M0L6_2atmpS1100;
  uint64_t _M0L6_2atmpS1099;
  uint64_t _M0L1aS396;
  uint64_t _M0L6_2atmpS1097;
  uint64_t _M0L1bS397;
  #line 239 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1113 = _M0L1iS381 + 26;
  _M0L6_2atmpS1112 = _M0L6_2atmpS1113 - 1;
  _M0L4baseS380 = _M0L6_2atmpS1112 / 26;
  _M0L5base2S382 = _M0L4baseS380 * 26;
  _M0L6offsetS383 = _M0L5base2S382 - _M0L1iS381;
  _M0L6_2atmpS1111 = _M0L4baseS380 * 2;
  #line 243 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4mul0S384
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB26gDOUBLE__POW5__INV__SPLIT2, _M0L6_2atmpS1111);
  _M0L6_2atmpS1110 = _M0L4baseS380 * 2;
  _M0L6_2atmpS1109 = _M0L6_2atmpS1110 + 1;
  #line 244 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4mul1S385
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB26gDOUBLE__POW5__INV__SPLIT2, _M0L6_2atmpS1109);
  if (_M0L6offsetS383 == 0) {
    return (struct _M0TPB8Pow5Pair){.$0 = _M0L4mul0S384, .$1 = _M0L4mul1S385};
  }
  #line 248 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L1mS386
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB20gDOUBLE__POW5__TABLE, _M0L6offsetS383);
  #line 249 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS387 = _M0FPB7umul128(_M0L1mS386, _M0L4mul1S385);
  _M0L7_2alow1S388 = _M0L7_2abindS387.$0;
  _M0L8_2ahigh1S389 = _M0L7_2abindS387.$1;
  #line 250 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS390 = _M0FPB7umul128(_M0L1mS386, _M0L4mul0S384);
  _M0L7_2alow0S391 = _M0L7_2abindS390.$0;
  _M0L8_2ahigh0S392 = _M0L7_2abindS390.$1;
  _M0L3sumS393 = _M0L8_2ahigh0S392 + _M0L7_2alow1S388;
  _M0Lm5high1S394 = _M0L8_2ahigh1S389;
  if (_M0L3sumS393 < _M0L8_2ahigh0S392) {
    uint64_t _M0L6_2atmpS1096 = _M0Lm5high1S394;
    _M0Lm5high1S394 = _M0L6_2atmpS1096 + 1ull;
  }
  #line 256 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1107 = _M0FPB8pow5bits(_M0L5base2S382);
  #line 256 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1108 = _M0FPB8pow5bits(_M0L1iS381);
  _M0L5deltaS395 = _M0L6_2atmpS1107 - _M0L6_2atmpS1108;
  #line 257 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1106
  = _M0FPB13shiftright128(_M0L7_2alow0S391, _M0L3sumS393, _M0L5deltaS395);
  _M0L6_2atmpS1098 = _M0L6_2atmpS1106 + 1ull;
  _M0L6_2atmpS1105 = _M0L1iS381 / 16;
  #line 259 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1102
  = _M0MPC15array13ReadOnlyArray2atGjE(_M0FPB19gPOW5__INV__OFFSETS, _M0L6_2atmpS1105);
  _M0L6_2atmpS1104 = _M0L1iS381 % 16;
  _M0L6_2atmpS1103 = _M0L6_2atmpS1104 << 1;
  _M0L6_2atmpS1101 = _M0L6_2atmpS1102 >> (_M0L6_2atmpS1103 & 31);
  _M0L6_2atmpS1100 = _M0L6_2atmpS1101 & 3u;
  #line 259 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1099 = _M0MPC14uint4UInt10to__uint64(_M0L6_2atmpS1100);
  _M0L1aS396 = _M0L6_2atmpS1098 + _M0L6_2atmpS1099;
  _M0L6_2atmpS1097 = _M0Lm5high1S394;
  #line 260 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L1bS397
  = _M0FPB13shiftright128(_M0L3sumS393, _M0L6_2atmpS1097, _M0L5deltaS395);
  return (struct _M0TPB8Pow5Pair){.$0 = _M0L1aS396, .$1 = _M0L1bS397};
}

struct _M0TPB8Pow5Pair _M0FPB19double__computePow5(int32_t _M0L1iS363) {
  int32_t _M0L4baseS362;
  int32_t _M0L5base2S364;
  int32_t _M0L6offsetS365;
  int32_t _M0L6_2atmpS1095;
  uint64_t _M0L4mul0S366;
  int32_t _M0L6_2atmpS1094;
  int32_t _M0L6_2atmpS1093;
  uint64_t _M0L4mul1S367;
  uint64_t _M0L1mS368;
  struct _M0TPB7Umul128 _M0L7_2abindS369;
  uint64_t _M0L7_2alow1S370;
  uint64_t _M0L8_2ahigh1S371;
  struct _M0TPB7Umul128 _M0L7_2abindS372;
  uint64_t _M0L7_2alow0S373;
  uint64_t _M0L8_2ahigh0S374;
  uint64_t _M0L3sumS375;
  uint64_t _M0Lm5high1S376;
  int32_t _M0L6_2atmpS1091;
  int32_t _M0L6_2atmpS1092;
  int32_t _M0L5deltaS377;
  uint64_t _M0L6_2atmpS1083;
  int32_t _M0L6_2atmpS1090;
  uint32_t _M0L6_2atmpS1087;
  int32_t _M0L6_2atmpS1089;
  int32_t _M0L6_2atmpS1088;
  uint32_t _M0L6_2atmpS1086;
  uint32_t _M0L6_2atmpS1085;
  uint64_t _M0L6_2atmpS1084;
  uint64_t _M0L1aS378;
  uint64_t _M0L6_2atmpS1082;
  uint64_t _M0L1bS379;
  #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4baseS362 = _M0L1iS363 / 26;
  _M0L5base2S364 = _M0L4baseS362 * 26;
  _M0L6offsetS365 = _M0L1iS363 - _M0L5base2S364;
  _M0L6_2atmpS1095 = _M0L4baseS362 * 2;
  #line 217 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4mul0S366
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB21gDOUBLE__POW5__SPLIT2, _M0L6_2atmpS1095);
  _M0L6_2atmpS1094 = _M0L4baseS362 * 2;
  _M0L6_2atmpS1093 = _M0L6_2atmpS1094 + 1;
  #line 218 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4mul1S367
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB21gDOUBLE__POW5__SPLIT2, _M0L6_2atmpS1093);
  if (_M0L6offsetS365 == 0) {
    return (struct _M0TPB8Pow5Pair){.$0 = _M0L4mul0S366, .$1 = _M0L4mul1S367};
  }
  #line 222 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L1mS368
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB20gDOUBLE__POW5__TABLE, _M0L6offsetS365);
  #line 223 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS369 = _M0FPB7umul128(_M0L1mS368, _M0L4mul1S367);
  _M0L7_2alow1S370 = _M0L7_2abindS369.$0;
  _M0L8_2ahigh1S371 = _M0L7_2abindS369.$1;
  #line 224 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS372 = _M0FPB7umul128(_M0L1mS368, _M0L4mul0S366);
  _M0L7_2alow0S373 = _M0L7_2abindS372.$0;
  _M0L8_2ahigh0S374 = _M0L7_2abindS372.$1;
  _M0L3sumS375 = _M0L8_2ahigh0S374 + _M0L7_2alow1S370;
  _M0Lm5high1S376 = _M0L8_2ahigh1S371;
  if (_M0L3sumS375 < _M0L8_2ahigh0S374) {
    uint64_t _M0L6_2atmpS1081 = _M0Lm5high1S376;
    _M0Lm5high1S376 = _M0L6_2atmpS1081 + 1ull;
  }
  #line 230 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1091 = _M0FPB8pow5bits(_M0L1iS363);
  #line 230 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1092 = _M0FPB8pow5bits(_M0L5base2S364);
  _M0L5deltaS377 = _M0L6_2atmpS1091 - _M0L6_2atmpS1092;
  #line 231 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1083
  = _M0FPB13shiftright128(_M0L7_2alow0S373, _M0L3sumS375, _M0L5deltaS377);
  _M0L6_2atmpS1090 = _M0L1iS363 / 16;
  #line 232 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1087
  = _M0MPC15array13ReadOnlyArray2atGjE(_M0FPB14gPOW5__OFFSETS, _M0L6_2atmpS1090);
  _M0L6_2atmpS1089 = _M0L1iS363 % 16;
  _M0L6_2atmpS1088 = _M0L6_2atmpS1089 << 1;
  _M0L6_2atmpS1086 = _M0L6_2atmpS1087 >> (_M0L6_2atmpS1088 & 31);
  _M0L6_2atmpS1085 = _M0L6_2atmpS1086 & 3u;
  #line 232 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1084 = _M0MPC14uint4UInt10to__uint64(_M0L6_2atmpS1085);
  _M0L1aS378 = _M0L6_2atmpS1083 + _M0L6_2atmpS1084;
  _M0L6_2atmpS1082 = _M0Lm5high1S376;
  #line 233 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L1bS379
  = _M0FPB13shiftright128(_M0L3sumS375, _M0L6_2atmpS1082, _M0L5deltaS377);
  return (struct _M0TPB8Pow5Pair){.$0 = _M0L1aS378, .$1 = _M0L1bS379};
}

struct _M0TPB19MulShiftAll64Result _M0FPB13mulShiftAll64(
  uint64_t _M0L1mS336,
  struct _M0TPB8Pow5Pair _M0L3mulS333,
  int32_t _M0L1jS349,
  int32_t _M0L7mmShiftS351
) {
  uint64_t _M0L7_2amul0S332;
  uint64_t _M0L7_2amul1S334;
  uint64_t _M0L1mS335;
  struct _M0TPB7Umul128 _M0L7_2abindS337;
  uint64_t _M0L5_2aloS338;
  uint64_t _M0L6_2atmpS339;
  struct _M0TPB7Umul128 _M0L7_2abindS340;
  uint64_t _M0L6_2alo2S341;
  uint64_t _M0L6_2ahi2S342;
  uint64_t _M0L3midS343;
  uint64_t _M0L6_2atmpS1080;
  uint64_t _M0L2hiS344;
  uint64_t _M0L3lo2S345;
  uint64_t _M0L6_2atmpS1078;
  uint64_t _M0L6_2atmpS1079;
  uint64_t _M0L4mid2S346;
  uint64_t _M0L6_2atmpS1077;
  uint64_t _M0L3hi2S347;
  int32_t _M0L6_2atmpS1076;
  int32_t _M0L6_2atmpS1075;
  uint64_t _M0L2vpS348;
  uint64_t _M0Lm2vmS350;
  int32_t _M0L6_2atmpS1074;
  int32_t _M0L6_2atmpS1073;
  uint64_t _M0L2vrS361;
  uint64_t _M0L6_2atmpS1072;
  #line 129 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2amul0S332 = _M0L3mulS333.$0;
  _M0L7_2amul1S334 = _M0L3mulS333.$1;
  _M0L1mS335 = _M0L1mS336 << 1;
  #line 137 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS337 = _M0FPB7umul128(_M0L1mS335, _M0L7_2amul0S332);
  _M0L5_2aloS338 = _M0L7_2abindS337.$0;
  _M0L6_2atmpS339 = _M0L7_2abindS337.$1;
  #line 138 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS340 = _M0FPB7umul128(_M0L1mS335, _M0L7_2amul1S334);
  _M0L6_2alo2S341 = _M0L7_2abindS340.$0;
  _M0L6_2ahi2S342 = _M0L7_2abindS340.$1;
  _M0L3midS343 = _M0L6_2atmpS339 + _M0L6_2alo2S341;
  if (_M0L3midS343 < _M0L6_2atmpS339) {
    _M0L6_2atmpS1080 = 1ull;
  } else {
    _M0L6_2atmpS1080 = 0ull;
  }
  _M0L2hiS344 = _M0L6_2ahi2S342 + _M0L6_2atmpS1080;
  _M0L3lo2S345 = _M0L5_2aloS338 + _M0L7_2amul0S332;
  _M0L6_2atmpS1078 = _M0L3midS343 + _M0L7_2amul1S334;
  if (_M0L3lo2S345 < _M0L5_2aloS338) {
    _M0L6_2atmpS1079 = 1ull;
  } else {
    _M0L6_2atmpS1079 = 0ull;
  }
  _M0L4mid2S346 = _M0L6_2atmpS1078 + _M0L6_2atmpS1079;
  if (_M0L4mid2S346 < _M0L3midS343) {
    _M0L6_2atmpS1077 = 1ull;
  } else {
    _M0L6_2atmpS1077 = 0ull;
  }
  _M0L3hi2S347 = _M0L2hiS344 + _M0L6_2atmpS1077;
  _M0L6_2atmpS1076 = _M0L1jS349 - 64;
  _M0L6_2atmpS1075 = _M0L6_2atmpS1076 - 1;
  #line 144 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L2vpS348
  = _M0FPB13shiftright128(_M0L4mid2S346, _M0L3hi2S347, _M0L6_2atmpS1075);
  _M0Lm2vmS350 = 0ull;
  if (_M0L7mmShiftS351) {
    uint64_t _M0L3lo3S352 = _M0L5_2aloS338 - _M0L7_2amul0S332;
    uint64_t _M0L6_2atmpS1062 = _M0L3midS343 - _M0L7_2amul1S334;
    uint64_t _M0L6_2atmpS1063;
    uint64_t _M0L4mid3S353;
    uint64_t _M0L6_2atmpS1061;
    uint64_t _M0L3hi3S354;
    int32_t _M0L6_2atmpS1060;
    int32_t _M0L6_2atmpS1059;
    if (_M0L5_2aloS338 < _M0L3lo3S352) {
      _M0L6_2atmpS1063 = 1ull;
    } else {
      _M0L6_2atmpS1063 = 0ull;
    }
    _M0L4mid3S353 = _M0L6_2atmpS1062 - _M0L6_2atmpS1063;
    if (_M0L3midS343 < _M0L4mid3S353) {
      _M0L6_2atmpS1061 = 1ull;
    } else {
      _M0L6_2atmpS1061 = 0ull;
    }
    _M0L3hi3S354 = _M0L2hiS344 - _M0L6_2atmpS1061;
    _M0L6_2atmpS1060 = _M0L1jS349 - 64;
    _M0L6_2atmpS1059 = _M0L6_2atmpS1060 - 1;
    #line 150 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0Lm2vmS350
    = _M0FPB13shiftright128(_M0L4mid3S353, _M0L3hi3S354, _M0L6_2atmpS1059);
  } else {
    uint64_t _M0L3lo3S355 = _M0L5_2aloS338 + _M0L5_2aloS338;
    uint64_t _M0L6_2atmpS1070 = _M0L3midS343 + _M0L3midS343;
    uint64_t _M0L6_2atmpS1071;
    uint64_t _M0L4mid3S356;
    uint64_t _M0L6_2atmpS1068;
    uint64_t _M0L6_2atmpS1069;
    uint64_t _M0L3hi3S357;
    uint64_t _M0L3lo4S358;
    uint64_t _M0L6_2atmpS1066;
    uint64_t _M0L6_2atmpS1067;
    uint64_t _M0L4mid4S359;
    uint64_t _M0L6_2atmpS1065;
    uint64_t _M0L3hi4S360;
    int32_t _M0L6_2atmpS1064;
    if (_M0L3lo3S355 < _M0L5_2aloS338) {
      _M0L6_2atmpS1071 = 1ull;
    } else {
      _M0L6_2atmpS1071 = 0ull;
    }
    _M0L4mid3S356 = _M0L6_2atmpS1070 + _M0L6_2atmpS1071;
    _M0L6_2atmpS1068 = _M0L2hiS344 + _M0L2hiS344;
    if (_M0L4mid3S356 < _M0L3midS343) {
      _M0L6_2atmpS1069 = 1ull;
    } else {
      _M0L6_2atmpS1069 = 0ull;
    }
    _M0L3hi3S357 = _M0L6_2atmpS1068 + _M0L6_2atmpS1069;
    _M0L3lo4S358 = _M0L3lo3S355 - _M0L7_2amul0S332;
    _M0L6_2atmpS1066 = _M0L4mid3S356 - _M0L7_2amul1S334;
    if (_M0L3lo3S355 < _M0L3lo4S358) {
      _M0L6_2atmpS1067 = 1ull;
    } else {
      _M0L6_2atmpS1067 = 0ull;
    }
    _M0L4mid4S359 = _M0L6_2atmpS1066 - _M0L6_2atmpS1067;
    if (_M0L4mid3S356 < _M0L4mid4S359) {
      _M0L6_2atmpS1065 = 1ull;
    } else {
      _M0L6_2atmpS1065 = 0ull;
    }
    _M0L3hi4S360 = _M0L3hi3S357 - _M0L6_2atmpS1065;
    _M0L6_2atmpS1064 = _M0L1jS349 - 64;
    #line 158 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0Lm2vmS350
    = _M0FPB13shiftright128(_M0L4mid4S359, _M0L3hi4S360, _M0L6_2atmpS1064);
  }
  _M0L6_2atmpS1074 = _M0L1jS349 - 64;
  _M0L6_2atmpS1073 = _M0L6_2atmpS1074 - 1;
  #line 160 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L2vrS361
  = _M0FPB13shiftright128(_M0L3midS343, _M0L2hiS344, _M0L6_2atmpS1073);
  _M0L6_2atmpS1072 = _M0Lm2vmS350;
  return (struct _M0TPB19MulShiftAll64Result){.$0 = _M0L2vrS361,
                                                .$1 = _M0L2vpS348,
                                                .$2 = _M0L6_2atmpS1072};
}

int32_t _M0FPB18multipleOfPowerOf2(
  uint64_t _M0L5valueS330,
  int32_t _M0L1pS331
) {
  uint64_t _M0L6_2atmpS1058;
  uint64_t _M0L6_2atmpS1057;
  uint64_t _M0L6_2atmpS1056;
  #line 124 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1058 = 1ull << (_M0L1pS331 & 63);
  _M0L6_2atmpS1057 = _M0L6_2atmpS1058 - 1ull;
  _M0L6_2atmpS1056 = _M0L5valueS330 & _M0L6_2atmpS1057;
  return _M0L6_2atmpS1056 == 0ull;
}

int32_t _M0FPB18multipleOfPowerOf5(
  uint64_t _M0L5valueS328,
  int32_t _M0L1pS329
) {
  int32_t _M0L6_2atmpS1055;
  #line 119 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  #line 120 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1055 = _M0FPB10pow5Factor(_M0L5valueS328);
  return _M0L6_2atmpS1055 >= _M0L1pS329;
}

int32_t _M0FPB10pow5Factor(uint64_t _M0L5valueS323) {
  uint64_t _M0L6_2atmpS1046;
  uint64_t _M0L6_2atmpS1047;
  uint64_t _M0L6_2atmpS1048;
  uint64_t _M0L6_2atmpS1049;
  uint64_t _M0L6_2atmpS1054;
  int32_t _M0L5countS324;
  uint64_t _M0L1vS325;
  #line 94 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1046 = _M0L5valueS323 % 5ull;
  if (_M0L6_2atmpS1046 != 0ull) {
    return 0;
  }
  _M0L6_2atmpS1047 = _M0L5valueS323 % 25ull;
  if (_M0L6_2atmpS1047 != 0ull) {
    return 1;
  }
  _M0L6_2atmpS1048 = _M0L5valueS323 % 125ull;
  if (_M0L6_2atmpS1048 != 0ull) {
    return 2;
  }
  _M0L6_2atmpS1049 = _M0L5valueS323 % 625ull;
  if (_M0L6_2atmpS1049 != 0ull) {
    return 3;
  }
  _M0L6_2atmpS1054 = _M0L5valueS323 / 625ull;
  _M0L5countS324 = 4;
  _M0L1vS325 = _M0L6_2atmpS1054;
  while (1) {
    if (_M0L1vS325 > 0ull) {
      uint64_t _M0L6_2atmpS1050 = _M0L1vS325 % 5ull;
      int32_t _M0L6_2atmpS1051;
      uint64_t _M0L6_2atmpS1052;
      if (_M0L6_2atmpS1050 != 0ull) {
        return _M0L5countS324;
      }
      _M0L6_2atmpS1051 = _M0L5countS324 + 1;
      _M0L6_2atmpS1052 = _M0L1vS325 / 5ull;
      _M0L5countS324 = _M0L6_2atmpS1051;
      _M0L1vS325 = _M0L6_2atmpS1052;
      continue;
    } else {
      struct _M0TPB13StringBuilder* _M0L18_2astring__builderS327;
      moonbit_string_t _M0L6_2atmpS1053;
      int32_t _result_1693;
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0L18_2astring__builderS327
      = _M0MPB13StringBuilder21StringBuilder_2einner(25);
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS327, (moonbit_string_t)moonbit_string_literal_2.data);
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0MPB13StringBuilder13write__objectGmE(_M0L18_2astring__builderS327, _M0L5valueS323);
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0L6_2atmpS1053
      = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS327);
      moonbit_decref(_M0L18_2astring__builderS327);
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _result_1693 = _M0FPC15abort5abortGiE(_M0L6_2atmpS1053);
      moonbit_decref(_M0L6_2atmpS1053);
      return _result_1693;
    }
    break;
  }
}

uint64_t _M0FPB13shiftright128(
  uint64_t _M0L2loS322,
  uint64_t _M0L2hiS320,
  int32_t _M0L4distS321
) {
  int32_t _M0L6_2atmpS1045;
  uint64_t _M0L6_2atmpS1043;
  uint64_t _M0L6_2atmpS1044;
  #line 89 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1045 = 64 - _M0L4distS321;
  _M0L6_2atmpS1043 = _M0L2hiS320 << (_M0L6_2atmpS1045 & 63);
  _M0L6_2atmpS1044 = _M0L2loS322 >> (_M0L4distS321 & 63);
  return _M0L6_2atmpS1043 | _M0L6_2atmpS1044;
}

struct _M0TPB7Umul128 _M0FPB7umul128(
  uint64_t _M0L1aS310,
  uint64_t _M0L1bS313
) {
  uint64_t _M0L3aLoS309;
  uint64_t _M0L3aHiS311;
  uint64_t _M0L3bLoS312;
  uint64_t _M0L3bHiS314;
  uint64_t _M0L1xS315;
  uint64_t _M0L6_2atmpS1041;
  uint64_t _M0L6_2atmpS1042;
  uint64_t _M0L1yS316;
  uint64_t _M0L6_2atmpS1039;
  uint64_t _M0L6_2atmpS1040;
  uint64_t _M0L1zS317;
  uint64_t _M0L6_2atmpS1037;
  uint64_t _M0L6_2atmpS1038;
  uint64_t _M0L6_2atmpS1035;
  uint64_t _M0L6_2atmpS1036;
  uint64_t _M0L1wS318;
  uint64_t _M0L2loS319;
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L3aLoS309 = _M0L1aS310 & 4294967295ull;
  _M0L3aHiS311 = _M0L1aS310 >> 32;
  _M0L3bLoS312 = _M0L1bS313 & 4294967295ull;
  _M0L3bHiS314 = _M0L1bS313 >> 32;
  _M0L1xS315 = _M0L3aLoS309 * _M0L3bLoS312;
  _M0L6_2atmpS1041 = _M0L3aHiS311 * _M0L3bLoS312;
  _M0L6_2atmpS1042 = _M0L1xS315 >> 32;
  _M0L1yS316 = _M0L6_2atmpS1041 + _M0L6_2atmpS1042;
  _M0L6_2atmpS1039 = _M0L3aLoS309 * _M0L3bHiS314;
  _M0L6_2atmpS1040 = _M0L1yS316 & 4294967295ull;
  _M0L1zS317 = _M0L6_2atmpS1039 + _M0L6_2atmpS1040;
  _M0L6_2atmpS1037 = _M0L3aHiS311 * _M0L3bHiS314;
  _M0L6_2atmpS1038 = _M0L1yS316 >> 32;
  _M0L6_2atmpS1035 = _M0L6_2atmpS1037 + _M0L6_2atmpS1038;
  _M0L6_2atmpS1036 = _M0L1zS317 >> 32;
  _M0L1wS318 = _M0L6_2atmpS1035 + _M0L6_2atmpS1036;
  _M0L2loS319 = _M0L1aS310 * _M0L1bS313;
  return (struct _M0TPB7Umul128){.$0 = _M0L2loS319, .$1 = _M0L1wS318};
}

moonbit_string_t _M0FPB19string__from__bytes(
  moonbit_bytes_t _M0L5bytesS307,
  int32_t _M0L4fromS304,
  int32_t _M0L2toS303
) {
  int32_t _M0L3lenS302;
  int32_t _M0L6_2atmpS1034;
  uint16_t* _M0L6bufferS305;
  int32_t _M0L1iS306;
  #line 52 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L3lenS302 = _M0L2toS303 - _M0L4fromS304;
  #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1034 = _M0IPC16uint166UInt16PB7Default7default();
  _M0L6bufferS305
  = (uint16_t*)moonbit_make_string(_M0L3lenS302, _M0L6_2atmpS1034);
  _M0L1iS306 = 0;
  while (1) {
    if (_M0L1iS306 < _M0L3lenS302) {
      int32_t _M0L6_2atmpS1032 = _M0L4fromS304 + _M0L1iS306;
      int32_t _M0L6_2atmpS1031;
      int32_t _M0L6_2atmpS1030;
      int32_t _M0L6_2atmpS1033;
      if (
        _M0L6_2atmpS1032 < 0
        || _M0L6_2atmpS1032 >= Moonbit_array_length(_M0L5bytesS307)
      ) {
        #line 56 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6_2atmpS1031 = (int32_t)_M0L5bytesS307[_M0L6_2atmpS1032];
      _M0L6_2atmpS1030 = (uint16_t)_M0L6_2atmpS1031;
      if (
        _M0L1iS306 < 0 || _M0L1iS306 >= Moonbit_array_length(_M0L6bufferS305)
      ) {
        #line 56 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6bufferS305[_M0L1iS306] = _M0L6_2atmpS1030;
      _M0L6_2atmpS1033 = _M0L1iS306 + 1;
      _M0L1iS306 = _M0L6_2atmpS1033;
      continue;
    }
    break;
  }
  return _M0L6bufferS305;
}

int32_t _M0FPB9log10Pow2(int32_t _M0L1eS301) {
  int32_t _M0L6_2atmpS1029;
  uint32_t _M0L6_2atmpS1028;
  uint32_t _M0L6_2atmpS1027;
  #line 44 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1029 = _M0L1eS301 * 78913;
  _M0L6_2atmpS1028 = *(uint32_t*)&_M0L6_2atmpS1029;
  _M0L6_2atmpS1027 = _M0L6_2atmpS1028 >> 18;
  return *(int32_t*)&_M0L6_2atmpS1027;
}

int32_t _M0FPB9log10Pow5(int32_t _M0L1eS300) {
  int32_t _M0L6_2atmpS1026;
  uint32_t _M0L6_2atmpS1025;
  uint32_t _M0L6_2atmpS1024;
  #line 37 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1026 = _M0L1eS300 * 732923;
  _M0L6_2atmpS1025 = *(uint32_t*)&_M0L6_2atmpS1026;
  _M0L6_2atmpS1024 = _M0L6_2atmpS1025 >> 20;
  return *(int32_t*)&_M0L6_2atmpS1024;
}

moonbit_string_t _M0FPB18copy__special__str(
  int32_t _M0L4signS298,
  int32_t _M0L8exponentS299,
  int32_t _M0L8mantissaS296
) {
  moonbit_string_t _M0L1sS297;
  moonbit_string_t _result_1696;
  #line 23 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  if (_M0L8mantissaS296) {
    return (moonbit_string_t)moonbit_string_literal_3.data;
  }
  if (_M0L4signS298) {
    _M0L1sS297 = (moonbit_string_t)moonbit_string_literal_4.data;
  } else {
    _M0L1sS297 = (moonbit_string_t)moonbit_string_literal_5.data;
  }
  if (_M0L8exponentS299) {
    moonbit_string_t _result_1695;
    #line 29 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _result_1695
    = moonbit_add_string(_M0L1sS297, (moonbit_string_t)moonbit_string_literal_6.data);
    moonbit_decref(_M0L1sS297);
    return _result_1695;
  }
  #line 31 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _result_1696
  = moonbit_add_string(_M0L1sS297, (moonbit_string_t)moonbit_string_literal_7.data);
  moonbit_decref(_M0L1sS297);
  return _result_1696;
}

int32_t _M0FPB8pow5bits(int32_t _M0L1eS295) {
  int32_t _M0L6_2atmpS1023;
  uint32_t _M0L6_2atmpS1022;
  uint32_t _M0L6_2atmpS1021;
  int32_t _M0L6_2atmpS1020;
  #line 18 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1023 = _M0L1eS295 * 1217359;
  _M0L6_2atmpS1022 = *(uint32_t*)&_M0L6_2atmpS1023;
  _M0L6_2atmpS1021 = _M0L6_2atmpS1022 >> 19;
  _M0L6_2atmpS1020 = *(int32_t*)&_M0L6_2atmpS1021;
  return _M0L6_2atmpS1020 + 1;
}

int32_t _M0MPC16double6Double7to__int(double _M0L4selfS294) {
  #line 43 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_to_int.mbt"
  if (_M0L4selfS294 != _M0L4selfS294) {
    return 0;
  } else if (_M0L4selfS294 >= 0x1.fffffffcp+30) {
    return 2147483647;
  } else if (_M0L4selfS294 <= -0x1p+31) {
    return (int32_t)0x80000000;
  } else {
    return (int32_t)_M0L4selfS294;
  }
}

int64_t _M0MPC16double6Double9to__int64(double _M0L4selfS293) {
  #line 46 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_to_int64_native.mbt"
  if (_M0L4selfS293 != _M0L4selfS293) {
    return 0ll;
  } else if (_M0L4selfS293 >= 0x1p+63) {
    return 9223372036854775807ll;
  } else if (_M0L4selfS293 <= -0x1p+63) {
    return (int64_t)0x8000000000000000ll;
  } else {
    return (int64_t)_M0L4selfS293;
  }
}

struct _M0TPB5ArrayGfE* _M0MPC15array5Array20unsafe__make__uninitGfE(
  int32_t _M0L3lenS290
) {
  float* _M0L6_2atmpS1017;
  struct _M0TPB5ArrayGfE* _block_1697;
  #line 30 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1017 = (float*)moonbit_make_float_array_raw(_M0L3lenS290);
  _block_1697
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_block_1697)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 18, 0);
  _block_1697->$0 = _M0L6_2atmpS1017;
  _block_1697->$1 = _M0L3lenS290;
  return _block_1697;
}

struct _M0TPB5ArrayGbE* _M0MPC15array5Array20unsafe__make__uninitGbE(
  int32_t _M0L3lenS291
) {
  uint8_t* _M0L6_2atmpS1018;
  struct _M0TPB5ArrayGbE* _block_1698;
  #line 30 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1018 = (uint8_t*)moonbit_make_bytes_raw(_M0L3lenS291);
  _block_1698
  = (struct _M0TPB5ArrayGbE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGbE));
  Moonbit_object_header(_block_1698)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 35, 0);
  _block_1698->$0 = _M0L6_2atmpS1018;
  _block_1698->$1 = _M0L3lenS291;
  return _block_1698;
}

struct _M0TPB5ArrayGiE* _M0MPC15array5Array20unsafe__make__uninitGiE(
  int32_t _M0L3lenS292
) {
  int32_t* _M0L6_2atmpS1019;
  struct _M0TPB5ArrayGiE* _block_1699;
  #line 30 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1019 = (int32_t*)moonbit_make_int32_array_raw(_M0L3lenS292);
  _block_1699
  = (struct _M0TPB5ArrayGiE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGiE));
  Moonbit_object_header(_block_1699)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 21, 0);
  _block_1699->$0 = _M0L6_2atmpS1019;
  _block_1699->$1 = _M0L3lenS292;
  return _block_1699;
}

uint64_t _M0MPC15array13ReadOnlyArray2atGmE(
  uint64_t* _M0L4selfS286,
  int32_t _M0L5indexS287
) {
  uint64_t* _M0L6_2atmpS1015;
  #line 38 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\readonlyarray.mbt"
  _M0L6_2atmpS1015 = _M0L4selfS286;
  if (
    _M0L5indexS287 < 0
    || _M0L5indexS287 >= Moonbit_array_length(_M0L6_2atmpS1015)
  ) {
    #line 40 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\readonlyarray.mbt"
    moonbit_panic();
  }
  return (uint64_t)_M0L6_2atmpS1015[_M0L5indexS287];
}

uint32_t _M0MPC15array13ReadOnlyArray2atGjE(
  uint32_t* _M0L4selfS288,
  int32_t _M0L5indexS289
) {
  uint32_t* _M0L6_2atmpS1016;
  #line 38 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\readonlyarray.mbt"
  _M0L6_2atmpS1016 = _M0L4selfS288;
  if (
    _M0L5indexS289 < 0
    || _M0L5indexS289 >= Moonbit_array_length(_M0L6_2atmpS1016)
  ) {
    #line 40 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\readonlyarray.mbt"
    moonbit_panic();
  }
  return (uint32_t)_M0L6_2atmpS1016[_M0L5indexS289];
}

moonbit_string_t _M0IPC16uint646UInt64PB4Show10to__string(
  uint64_t _M0L4selfS285
) {
  #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  #line 51 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  return _M0MPC16uint646UInt6418to__string_2einner(_M0L4selfS285, 10);
}

moonbit_string_t _M0IPC13int3IntPB4Show10to__string(int32_t _M0L4selfS284) {
  #line 35 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  #line 36 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  return _M0MPC13int3Int18to__string_2einner(_M0L4selfS284, 10);
}

moonbit_string_t _M0IPC14bool4BoolPB4Show10to__string(int32_t _M0L4selfS283) {
  #line 26 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  if (_M0L4selfS283) {
    return (moonbit_string_t)moonbit_string_literal_8.data;
  } else {
    return (moonbit_string_t)moonbit_string_literal_9.data;
  }
}

uint64_t _M0MPC14uint4UInt10to__uint64(uint32_t _M0L4selfS282) {
  #line 2576 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  return (uint64_t)_M0L4selfS282;
}

int32_t _M0MPC15array5Array6lengthGfE(struct _M0TPB5ArrayGfE* _M0L4selfS281) {
  #line 147 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  return _M0L4selfS281->$1;
}

float* _M0MPC15array5Array6bufferGfE(struct _M0TPB5ArrayGfE* _M0L4selfS278) {
  float* _M0L8_2afieldS1627;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS1627 = _M0L4selfS278->$0;
  moonbit_incref(_M0L8_2afieldS1627);
  return _M0L8_2afieldS1627;
}

uint8_t* _M0MPC15array5Array6bufferGbE(struct _M0TPB5ArrayGbE* _M0L4selfS279) {
  uint8_t* _M0L8_2afieldS1628;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS1628 = _M0L4selfS279->$0;
  moonbit_incref(_M0L8_2afieldS1628);
  return _M0L8_2afieldS1628;
}

int32_t* _M0MPC15array5Array6bufferGiE(struct _M0TPB5ArrayGiE* _M0L4selfS280) {
  int32_t* _M0L8_2afieldS1629;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS1629 = _M0L4selfS280->$0;
  moonbit_incref(_M0L8_2afieldS1629);
  return _M0L8_2afieldS1629;
}

moonbit_string_t _M0IPC16string6StringPB4Show10to__string(
  moonbit_string_t _M0L4selfS277
) {
  #line 222 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  moonbit_incref(_M0L4selfS277);
  return _M0L4selfS277;
}

int32_t _M0IPB13StringBuilderPB6Logger11write__view(
  struct _M0TPB13StringBuilder* _M0L4selfS276,
  struct _M0TPC16string10StringView _M0L3strS274
) {
  int32_t _M0L3endS1013;
  int32_t _M0L5startS1014;
  int32_t _M0L8str__lenS273;
  int32_t _M0L3lenS1012;
  int32_t _M0L8requiredS275;
  uint16_t* _M0L4dataS1005;
  int32_t _M0L6_2atmpS1004;
  int32_t _if__result_1700;
  uint16_t* _M0L4dataS1006;
  int32_t _M0L3lenS1007;
  moonbit_string_t _M0L6_2atmpS1008;
  int32_t _M0L6_2atmpS1009;
  int32_t _M0L3lenS1011;
  int32_t _M0L6_2atmpS1010;
  #line 158 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L3endS1013 = _M0L3strS274.$2;
  _M0L5startS1014 = _M0L3strS274.$1;
  _M0L8str__lenS273 = _M0L3endS1013 - _M0L5startS1014;
  if (_M0L8str__lenS273 == 0) {
    return 0;
  }
  _M0L3lenS1012 = _M0L4selfS276->$1;
  _M0L8requiredS275 = _M0L3lenS1012 + _M0L8str__lenS273;
  _M0L4dataS1005 = _M0L4selfS276->$0;
  _M0L6_2atmpS1004 = Moonbit_array_length(_M0L4dataS1005);
  if (_M0L8requiredS275 > _M0L6_2atmpS1004) {
    _if__result_1700 = 1;
  } else {
    int32_t _M0L3lenS1003 = _M0L4selfS276->$1;
    _if__result_1700 = _M0L8requiredS275 < _M0L3lenS1003;
  }
  if (_if__result_1700) {
    #line 168 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0MPB13StringBuilder4grow(_M0L4selfS276, _M0L8requiredS275);
  }
  _M0L4dataS1006 = _M0L4selfS276->$0;
  _M0L3lenS1007 = _M0L4selfS276->$1;
  moonbit_incref(_M0L4dataS1006);
  #line 172 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L6_2atmpS1008 = _M0MPC16string10StringView4data(_M0L3strS274);
  #line 173 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L6_2atmpS1009 = _M0MPC16string10StringView13start__offset(_M0L3strS274);
  #line 170 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0MPC15array10FixedArray26unsafe__blit__from__string(_M0L4dataS1006, _M0L3lenS1007, _M0L6_2atmpS1008, _M0L6_2atmpS1009, _M0L8str__lenS273);
  moonbit_decref(_M0L4dataS1006);
  moonbit_decref(_M0L6_2atmpS1008);
  _M0L3lenS1011 = _M0L4selfS276->$1;
  _M0L6_2atmpS1010 = _M0L3lenS1011 + _M0L8str__lenS273;
  _M0L4selfS276->$1 = _M0L6_2atmpS1010;
  return 0;
}

moonbit_string_t _M0MPC16uint646UInt6418to__string_2einner(
  uint64_t _M0L4selfS265,
  int32_t _M0L5radixS264
) {
  uint16_t* _M0L6bufferS266;
  #line 607 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  if (_M0L5radixS264 < 2 || _M0L5radixS264 > 36) {
    #line 611 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_10.data);
  }
  if (_M0L4selfS265 == 0ull) {
    return (moonbit_string_t)moonbit_string_literal_1.data;
  }
  switch (_M0L5radixS264) {
    case 10: {
      int32_t _M0L3lenS267;
      uint16_t* _M0L6bufferS268;
      #line 622 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L3lenS267 = _M0FPB12dec__count64(_M0L4selfS265);
      _M0L6bufferS268 = (uint16_t*)moonbit_make_string(_M0L3lenS267, 0);
      #line 624 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0FPB22int64__to__string__dec(_M0L6bufferS268, _M0L4selfS265, 0, _M0L3lenS267);
      _M0L6bufferS266 = _M0L6bufferS268;
      break;
    }
    
    case 16: {
      int32_t _M0L3lenS269;
      uint16_t* _M0L6bufferS270;
      #line 628 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L3lenS269 = _M0FPB12hex__count64(_M0L4selfS265);
      _M0L6bufferS270 = (uint16_t*)moonbit_make_string(_M0L3lenS269, 0);
      #line 630 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0FPB22int64__to__string__hex(_M0L6bufferS270, _M0L4selfS265, 0, _M0L3lenS269);
      _M0L6bufferS266 = _M0L6bufferS270;
      break;
    }
    default: {
      int32_t _M0L3lenS271;
      uint16_t* _M0L6bufferS272;
      #line 634 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L3lenS271 = _M0FPB14radix__count64(_M0L4selfS265, _M0L5radixS264);
      _M0L6bufferS272 = (uint16_t*)moonbit_make_string(_M0L3lenS271, 0);
      #line 636 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0FPB26int64__to__string__generic(_M0L6bufferS272, _M0L4selfS265, 0, _M0L3lenS271, _M0L5radixS264);
      _M0L6bufferS266 = _M0L6bufferS272;
      break;
    }
  }
  return _M0L6bufferS266;
}

moonbit_string_t _M0MPC15int645Int6418to__string_2einner(
  int64_t _M0L4selfS248,
  int32_t _M0L5radixS247
) {
  int32_t _M0L12is__negativeS249;
  uint64_t _M0L3numS250;
  uint16_t* _M0L6bufferS251;
  #line 548 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  if (_M0L5radixS247 < 2 || _M0L5radixS247 > 36) {
    #line 552 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_10.data);
  }
  if (_M0L4selfS248 == 0ll) {
    return (moonbit_string_t)moonbit_string_literal_1.data;
  }
  _M0L12is__negativeS249 = _M0L4selfS248 < 0ll;
  if (_M0L12is__negativeS249) {
    int64_t _M0L6_2atmpS1002 = -_M0L4selfS248;
    _M0L3numS250 = *(uint64_t*)&_M0L6_2atmpS1002;
  } else {
    _M0L3numS250 = *(uint64_t*)&_M0L4selfS248;
  }
  switch (_M0L5radixS247) {
    case 10: {
      int32_t _M0L10digit__lenS252;
      int32_t _M0L6_2atmpS999;
      int32_t _M0L10total__lenS253;
      uint16_t* _M0L6bufferS254;
      int32_t _M0L12digit__startS255;
      #line 573 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS252 = _M0FPB12dec__count64(_M0L3numS250);
      if (_M0L12is__negativeS249) {
        _M0L6_2atmpS999 = 1;
      } else {
        _M0L6_2atmpS999 = 0;
      }
      _M0L10total__lenS253 = _M0L10digit__lenS252 + _M0L6_2atmpS999;
      _M0L6bufferS254
      = (uint16_t*)moonbit_make_string(_M0L10total__lenS253, 0);
      if (_M0L12is__negativeS249) {
        _M0L12digit__startS255 = 1;
      } else {
        _M0L12digit__startS255 = 0;
      }
      #line 577 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0FPB22int64__to__string__dec(_M0L6bufferS254, _M0L3numS250, _M0L12digit__startS255, _M0L10total__lenS253);
      _M0L6bufferS251 = _M0L6bufferS254;
      break;
    }
    
    case 16: {
      int32_t _M0L10digit__lenS256;
      int32_t _M0L6_2atmpS1000;
      int32_t _M0L10total__lenS257;
      uint16_t* _M0L6bufferS258;
      int32_t _M0L12digit__startS259;
      #line 581 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS256 = _M0FPB12hex__count64(_M0L3numS250);
      if (_M0L12is__negativeS249) {
        _M0L6_2atmpS1000 = 1;
      } else {
        _M0L6_2atmpS1000 = 0;
      }
      _M0L10total__lenS257 = _M0L10digit__lenS256 + _M0L6_2atmpS1000;
      _M0L6bufferS258
      = (uint16_t*)moonbit_make_string(_M0L10total__lenS257, 0);
      if (_M0L12is__negativeS249) {
        _M0L12digit__startS259 = 1;
      } else {
        _M0L12digit__startS259 = 0;
      }
      #line 585 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0FPB22int64__to__string__hex(_M0L6bufferS258, _M0L3numS250, _M0L12digit__startS259, _M0L10total__lenS257);
      _M0L6bufferS251 = _M0L6bufferS258;
      break;
    }
    default: {
      int32_t _M0L10digit__lenS260;
      int32_t _M0L6_2atmpS1001;
      int32_t _M0L10total__lenS261;
      uint16_t* _M0L6bufferS262;
      int32_t _M0L12digit__startS263;
      #line 589 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS260
      = _M0FPB14radix__count64(_M0L3numS250, _M0L5radixS247);
      if (_M0L12is__negativeS249) {
        _M0L6_2atmpS1001 = 1;
      } else {
        _M0L6_2atmpS1001 = 0;
      }
      _M0L10total__lenS261 = _M0L10digit__lenS260 + _M0L6_2atmpS1001;
      _M0L6bufferS262
      = (uint16_t*)moonbit_make_string(_M0L10total__lenS261, 0);
      if (_M0L12is__negativeS249) {
        _M0L12digit__startS263 = 1;
      } else {
        _M0L12digit__startS263 = 0;
      }
      #line 593 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0FPB26int64__to__string__generic(_M0L6bufferS262, _M0L3numS250, _M0L12digit__startS263, _M0L10total__lenS261, _M0L5radixS247);
      _M0L6bufferS251 = _M0L6bufferS262;
      break;
    }
  }
  if (_M0L12is__negativeS249) {
    _M0L6bufferS251[0] = 45;
  }
  return _M0L6bufferS251;
}

int32_t _M0FPB22int64__to__string__dec(
  uint16_t* _M0L6bufferS233,
  uint64_t _M0L3numS245,
  int32_t _M0L12digit__startS234,
  int32_t _M0L10total__lenS246
) {
  int32_t _M0L6_2atmpS998;
  uint64_t _M0L3numS223;
  int32_t _M0L6offsetS224;
  #line 493 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L6_2atmpS998 = _M0L10total__lenS246 - _M0L12digit__startS234;
  _M0L3numS223 = _M0L3numS245;
  _M0L6offsetS224 = _M0L6_2atmpS998;
  while (1) {
    if (_M0L3numS223 >= 10000ull) {
      uint64_t _M0L1tS225 = _M0L3numS223 / 10000ull;
      uint64_t _M0L6_2atmpS975 = _M0L3numS223 % 10000ull;
      int32_t _M0L1rS226 = (int32_t)_M0L6_2atmpS975;
      int32_t _M0L2d1S227 = _M0L1rS226 / 100;
      int32_t _M0L2d2S228 = _M0L1rS226 % 100;
      int32_t _M0L6_2atmpS974 = _M0L2d1S227 / 10;
      int32_t _M0L6_2atmpS973 = 48 + _M0L6_2atmpS974;
      int32_t _M0L6d1__hiS229 = (uint16_t)_M0L6_2atmpS973;
      int32_t _M0L6_2atmpS972 = _M0L2d1S227 % 10;
      int32_t _M0L6_2atmpS971 = 48 + _M0L6_2atmpS972;
      int32_t _M0L6d1__loS230 = (uint16_t)_M0L6_2atmpS971;
      int32_t _M0L6_2atmpS970 = _M0L2d2S228 / 10;
      int32_t _M0L6_2atmpS969 = 48 + _M0L6_2atmpS970;
      int32_t _M0L6d2__hiS231 = (uint16_t)_M0L6_2atmpS969;
      int32_t _M0L6_2atmpS968 = _M0L2d2S228 % 10;
      int32_t _M0L6_2atmpS967 = 48 + _M0L6_2atmpS968;
      int32_t _M0L6d2__loS232 = (uint16_t)_M0L6_2atmpS967;
      int32_t _M0L6_2atmpS959 = _M0L12digit__startS234 + _M0L6offsetS224;
      int32_t _M0L6_2atmpS958 = _M0L6_2atmpS959 - 4;
      int32_t _M0L6_2atmpS961;
      int32_t _M0L6_2atmpS960;
      int32_t _M0L6_2atmpS963;
      int32_t _M0L6_2atmpS962;
      int32_t _M0L6_2atmpS965;
      int32_t _M0L6_2atmpS964;
      int32_t _M0L6_2atmpS966;
      _M0L6bufferS233[_M0L6_2atmpS958] = _M0L6d1__hiS229;
      _M0L6_2atmpS961 = _M0L12digit__startS234 + _M0L6offsetS224;
      _M0L6_2atmpS960 = _M0L6_2atmpS961 - 3;
      _M0L6bufferS233[_M0L6_2atmpS960] = _M0L6d1__loS230;
      _M0L6_2atmpS963 = _M0L12digit__startS234 + _M0L6offsetS224;
      _M0L6_2atmpS962 = _M0L6_2atmpS963 - 2;
      _M0L6bufferS233[_M0L6_2atmpS962] = _M0L6d2__hiS231;
      _M0L6_2atmpS965 = _M0L12digit__startS234 + _M0L6offsetS224;
      _M0L6_2atmpS964 = _M0L6_2atmpS965 - 1;
      _M0L6bufferS233[_M0L6_2atmpS964] = _M0L6d2__loS232;
      _M0L6_2atmpS966 = _M0L6offsetS224 - 4;
      _M0L3numS223 = _M0L1tS225;
      _M0L6offsetS224 = _M0L6_2atmpS966;
      continue;
    } else {
      int32_t _M0L6_2atmpS997 = (int32_t)_M0L3numS223;
      int32_t _M0L9remainingS236 = _M0L6_2atmpS997;
      int32_t _M0L6offsetS237 = _M0L6offsetS224;
      while (1) {
        if (_M0L9remainingS236 >= 100) {
          int32_t _M0L1tS238 = _M0L9remainingS236 / 100;
          int32_t _M0L1dS239 = _M0L9remainingS236 % 100;
          int32_t _M0L6_2atmpS984 = _M0L1dS239 / 10;
          int32_t _M0L6_2atmpS983 = 48 + _M0L6_2atmpS984;
          int32_t _M0L5d__hiS240 = (uint16_t)_M0L6_2atmpS983;
          int32_t _M0L6_2atmpS982 = _M0L1dS239 % 10;
          int32_t _M0L6_2atmpS981 = 48 + _M0L6_2atmpS982;
          int32_t _M0L5d__loS241 = (uint16_t)_M0L6_2atmpS981;
          int32_t _M0L6_2atmpS977 = _M0L12digit__startS234 + _M0L6offsetS237;
          int32_t _M0L6_2atmpS976 = _M0L6_2atmpS977 - 2;
          int32_t _M0L6_2atmpS979;
          int32_t _M0L6_2atmpS978;
          int32_t _M0L6_2atmpS980;
          _M0L6bufferS233[_M0L6_2atmpS976] = _M0L5d__hiS240;
          _M0L6_2atmpS979 = _M0L12digit__startS234 + _M0L6offsetS237;
          _M0L6_2atmpS978 = _M0L6_2atmpS979 - 1;
          _M0L6bufferS233[_M0L6_2atmpS978] = _M0L5d__loS241;
          _M0L6_2atmpS980 = _M0L6offsetS237 - 2;
          _M0L9remainingS236 = _M0L1tS238;
          _M0L6offsetS237 = _M0L6_2atmpS980;
          continue;
        } else if (_M0L9remainingS236 >= 10) {
          int32_t _M0L6_2atmpS992 = _M0L9remainingS236 / 10;
          int32_t _M0L6_2atmpS991 = 48 + _M0L6_2atmpS992;
          int32_t _M0L5d__hiS243 = (uint16_t)_M0L6_2atmpS991;
          int32_t _M0L6_2atmpS990 = _M0L9remainingS236 % 10;
          int32_t _M0L6_2atmpS989 = 48 + _M0L6_2atmpS990;
          int32_t _M0L5d__loS244 = (uint16_t)_M0L6_2atmpS989;
          int32_t _M0L6_2atmpS986 = _M0L12digit__startS234 + _M0L6offsetS237;
          int32_t _M0L6_2atmpS985 = _M0L6_2atmpS986 - 2;
          int32_t _M0L6_2atmpS988;
          int32_t _M0L6_2atmpS987;
          _M0L6bufferS233[_M0L6_2atmpS985] = _M0L5d__hiS243;
          _M0L6_2atmpS988 = _M0L12digit__startS234 + _M0L6offsetS237;
          _M0L6_2atmpS987 = _M0L6_2atmpS988 - 1;
          _M0L6bufferS233[_M0L6_2atmpS987] = _M0L5d__loS244;
        } else {
          int32_t _M0L6_2atmpS996 = _M0L12digit__startS234 + _M0L6offsetS237;
          int32_t _M0L6_2atmpS993 = _M0L6_2atmpS996 - 1;
          int32_t _M0L6_2atmpS995 = 48 + _M0L9remainingS236;
          int32_t _M0L6_2atmpS994 = (uint16_t)_M0L6_2atmpS995;
          _M0L6bufferS233[_M0L6_2atmpS993] = _M0L6_2atmpS994;
        }
        break;
      }
    }
    break;
  }
  return 0;
}

int32_t _M0FPB26int64__to__string__generic(
  uint16_t* _M0L6bufferS213,
  uint64_t _M0L3numS217,
  int32_t _M0L12digit__startS214,
  int32_t _M0L10total__lenS216,
  int32_t _M0L5radixS207
) {
  uint64_t _M0L4baseS206;
  int32_t _M0L6_2atmpS943;
  int32_t _M0L6_2atmpS942;
  #line 462 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  #line 470 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L4baseS206 = _M0MPC13int3Int10to__uint64(_M0L5radixS207);
  _M0L6_2atmpS943 = _M0L5radixS207 - 1;
  _M0L6_2atmpS942 = _M0L5radixS207 & _M0L6_2atmpS943;
  if (_M0L6_2atmpS942 == 0) {
    int32_t _M0L5shiftS208;
    uint64_t _M0L4maskS209;
    int32_t _M0L6_2atmpS950;
    int32_t _M0L6offsetS210;
    uint64_t _M0L1nS211;
    #line 473 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0L5shiftS208 = moonbit_ctz32(_M0L5radixS207);
    _M0L4maskS209 = _M0L4baseS206 - 1ull;
    _M0L6_2atmpS950 = _M0L10total__lenS216 - _M0L12digit__startS214;
    _M0L6offsetS210 = _M0L6_2atmpS950;
    _M0L1nS211 = _M0L3numS217;
    while (1) {
      if (_M0L1nS211 > 0ull) {
        uint64_t _M0L6_2atmpS949 = _M0L1nS211 & _M0L4maskS209;
        int32_t _M0L5digitS212 = (int32_t)_M0L6_2atmpS949;
        int32_t _M0L6_2atmpS946 = _M0L12digit__startS214 + _M0L6offsetS210;
        int32_t _M0L6_2atmpS944 = _M0L6_2atmpS946 - 1;
        int32_t _M0L6_2atmpS945 =
          ((moonbit_string_t)moonbit_string_literal_11.data)[_M0L5digitS212];
        int32_t _M0L6_2atmpS947;
        uint64_t _M0L6_2atmpS948;
        _M0L6bufferS213[_M0L6_2atmpS944] = _M0L6_2atmpS945;
        _M0L6_2atmpS947 = _M0L6offsetS210 - 1;
        _M0L6_2atmpS948 = _M0L1nS211 >> (_M0L5shiftS208 & 63);
        _M0L6offsetS210 = _M0L6_2atmpS947;
        _M0L1nS211 = _M0L6_2atmpS948;
        continue;
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS957 = _M0L10total__lenS216 - _M0L12digit__startS214;
    int32_t _M0L6offsetS218 = _M0L6_2atmpS957;
    uint64_t _M0L1nS219 = _M0L3numS217;
    while (1) {
      if (_M0L1nS219 > 0ull) {
        uint64_t _M0L1qS220 = _M0L1nS219 / _M0L4baseS206;
        uint64_t _M0L6_2atmpS956 = _M0L1qS220 * _M0L4baseS206;
        uint64_t _M0L6_2atmpS955 = _M0L1nS219 - _M0L6_2atmpS956;
        int32_t _M0L5digitS221 = (int32_t)_M0L6_2atmpS955;
        int32_t _M0L6_2atmpS953 = _M0L12digit__startS214 + _M0L6offsetS218;
        int32_t _M0L6_2atmpS951 = _M0L6_2atmpS953 - 1;
        int32_t _M0L6_2atmpS952 =
          ((moonbit_string_t)moonbit_string_literal_11.data)[_M0L5digitS221];
        int32_t _M0L6_2atmpS954;
        _M0L6bufferS213[_M0L6_2atmpS951] = _M0L6_2atmpS952;
        _M0L6_2atmpS954 = _M0L6offsetS218 - 1;
        _M0L6offsetS218 = _M0L6_2atmpS954;
        _M0L1nS219 = _M0L1qS220;
        continue;
      }
      break;
    }
  }
  return 0;
}

int32_t _M0FPB22int64__to__string__hex(
  uint16_t* _M0L6bufferS200,
  uint64_t _M0L3numS205,
  int32_t _M0L12digit__startS201,
  int32_t _M0L10total__lenS204
) {
  int32_t _M0L6_2atmpS941;
  int32_t _M0L6offsetS195;
  uint64_t _M0L1nS196;
  #line 434 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L6_2atmpS941 = _M0L10total__lenS204 - _M0L12digit__startS201;
  _M0L6offsetS195 = _M0L6_2atmpS941;
  _M0L1nS196 = _M0L3numS205;
  while (1) {
    if (_M0L6offsetS195 >= 2) {
      uint64_t _M0L6_2atmpS938 = _M0L1nS196 & 255ull;
      int32_t _M0L9byte__valS197 = (int32_t)_M0L6_2atmpS938;
      int32_t _M0L2hiS198 = _M0L9byte__valS197 / 16;
      int32_t _M0L2loS199 = _M0L9byte__valS197 % 16;
      int32_t _M0L6_2atmpS932 = _M0L12digit__startS201 + _M0L6offsetS195;
      int32_t _M0L6_2atmpS930 = _M0L6_2atmpS932 - 2;
      int32_t _M0L6_2atmpS931 =
        ((moonbit_string_t)moonbit_string_literal_11.data)[_M0L2hiS198];
      int32_t _M0L6_2atmpS935;
      int32_t _M0L6_2atmpS933;
      int32_t _M0L6_2atmpS934;
      int32_t _M0L6_2atmpS936;
      uint64_t _M0L6_2atmpS937;
      _M0L6bufferS200[_M0L6_2atmpS930] = _M0L6_2atmpS931;
      _M0L6_2atmpS935 = _M0L12digit__startS201 + _M0L6offsetS195;
      _M0L6_2atmpS933 = _M0L6_2atmpS935 - 1;
      _M0L6_2atmpS934
      = ((moonbit_string_t)moonbit_string_literal_11.data)[
        _M0L2loS199
      ];
      _M0L6bufferS200[_M0L6_2atmpS933] = _M0L6_2atmpS934;
      _M0L6_2atmpS936 = _M0L6offsetS195 - 2;
      _M0L6_2atmpS937 = _M0L1nS196 >> 8;
      _M0L6offsetS195 = _M0L6_2atmpS936;
      _M0L1nS196 = _M0L6_2atmpS937;
      continue;
    } else if (_M0L6offsetS195 == 1) {
      uint64_t _M0L6_2atmpS940 = _M0L1nS196 & 15ull;
      int32_t _M0L6nibbleS203 = (int32_t)_M0L6_2atmpS940;
      int32_t _M0L6_2atmpS939 =
        ((moonbit_string_t)moonbit_string_literal_11.data)[_M0L6nibbleS203];
      _M0L6bufferS200[_M0L12digit__startS201] = _M0L6_2atmpS939;
    }
    break;
  }
  return 0;
}

int32_t _M0FPB14radix__count64(
  uint64_t _M0L5valueS189,
  int32_t _M0L5radixS191
) {
  uint64_t _M0L4baseS190;
  uint64_t _M0L3numS192;
  int32_t _M0L5countS193;
  #line 419 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  if (_M0L5valueS189 == 0ull) {
    return 1;
  }
  #line 424 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L4baseS190 = _M0MPC13int3Int10to__uint64(_M0L5radixS191);
  _M0L3numS192 = _M0L5valueS189;
  _M0L5countS193 = 0;
  while (1) {
    if (_M0L3numS192 > 0ull) {
      uint64_t _M0L6_2atmpS928 = _M0L3numS192 / _M0L4baseS190;
      int32_t _M0L6_2atmpS929 = _M0L5countS193 + 1;
      _M0L3numS192 = _M0L6_2atmpS928;
      _M0L5countS193 = _M0L6_2atmpS929;
      continue;
    } else {
      return _M0L5countS193;
    }
    break;
  }
}

int32_t _M0FPB12hex__count64(uint64_t _M0L5valueS187) {
  #line 407 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  if (_M0L5valueS187 == 0ull) {
    return 1;
  } else {
    int32_t _M0L14leading__zerosS188;
    int32_t _M0L6_2atmpS927;
    int32_t _M0L6_2atmpS926;
    #line 412 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0L14leading__zerosS188 = moonbit_clz64(_M0L5valueS187);
    _M0L6_2atmpS927 = 63 - _M0L14leading__zerosS188;
    _M0L6_2atmpS926 = _M0L6_2atmpS927 / 4;
    return _M0L6_2atmpS926 + 1;
  }
}

int32_t _M0FPB12dec__count64(uint64_t _M0L5valueS186) {
  #line 343 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  if (_M0L5valueS186 >= 10000000000ull) {
    if (_M0L5valueS186 >= 100000000000000ull) {
      if (_M0L5valueS186 >= 10000000000000000ull) {
        if (_M0L5valueS186 >= 1000000000000000000ull) {
          if (_M0L5valueS186 >= 10000000000000000000ull) {
            return 20;
          } else {
            return 19;
          }
        } else if (_M0L5valueS186 >= 100000000000000000ull) {
          return 18;
        } else {
          return 17;
        }
      } else if (_M0L5valueS186 >= 1000000000000000ull) {
        return 16;
      } else {
        return 15;
      }
    } else if (_M0L5valueS186 >= 1000000000000ull) {
      if (_M0L5valueS186 >= 10000000000000ull) {
        return 14;
      } else {
        return 13;
      }
    } else if (_M0L5valueS186 >= 100000000000ull) {
      return 12;
    } else {
      return 11;
    }
  } else if (_M0L5valueS186 >= 100000ull) {
    if (_M0L5valueS186 >= 10000000ull) {
      if (_M0L5valueS186 >= 1000000000ull) {
        return 10;
      } else if (_M0L5valueS186 >= 100000000ull) {
        return 9;
      } else {
        return 8;
      }
    } else if (_M0L5valueS186 >= 1000000ull) {
      return 7;
    } else {
      return 6;
    }
  } else if (_M0L5valueS186 >= 1000ull) {
    if (_M0L5valueS186 >= 10000ull) {
      return 5;
    } else {
      return 4;
    }
  } else if (_M0L5valueS186 >= 100ull) {
    return 3;
  } else if (_M0L5valueS186 >= 10ull) {
    return 2;
  } else {
    return 1;
  }
}

moonbit_string_t _M0MPC13int3Int18to__string_2einner(
  int32_t _M0L4selfS170,
  int32_t _M0L5radixS169
) {
  int32_t _M0L12is__negativeS171;
  uint32_t _M0L3numS172;
  uint16_t* _M0L6bufferS173;
  #line 209 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  if (_M0L5radixS169 < 2 || _M0L5radixS169 > 36) {
    #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_10.data);
  }
  if (_M0L4selfS170 == 0) {
    return (moonbit_string_t)moonbit_string_literal_1.data;
  }
  _M0L12is__negativeS171 = _M0L4selfS170 < 0;
  if (_M0L12is__negativeS171) {
    int32_t _M0L6_2atmpS925 = -_M0L4selfS170;
    _M0L3numS172 = *(uint32_t*)&_M0L6_2atmpS925;
  } else {
    _M0L3numS172 = *(uint32_t*)&_M0L4selfS170;
  }
  switch (_M0L5radixS169) {
    case 10: {
      int32_t _M0L10digit__lenS174;
      int32_t _M0L6_2atmpS922;
      int32_t _M0L10total__lenS175;
      uint16_t* _M0L6bufferS176;
      int32_t _M0L12digit__startS177;
      #line 235 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS174 = _M0FPB12dec__count32(_M0L3numS172);
      if (_M0L12is__negativeS171) {
        _M0L6_2atmpS922 = 1;
      } else {
        _M0L6_2atmpS922 = 0;
      }
      _M0L10total__lenS175 = _M0L10digit__lenS174 + _M0L6_2atmpS922;
      _M0L6bufferS176
      = (uint16_t*)moonbit_make_string(_M0L10total__lenS175, 0);
      if (_M0L12is__negativeS171) {
        _M0L12digit__startS177 = 1;
      } else {
        _M0L12digit__startS177 = 0;
      }
      #line 239 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0FPB20int__to__string__dec(_M0L6bufferS176, _M0L3numS172, _M0L12digit__startS177, _M0L10total__lenS175);
      _M0L6bufferS173 = _M0L6bufferS176;
      break;
    }
    
    case 16: {
      int32_t _M0L10digit__lenS178;
      int32_t _M0L6_2atmpS923;
      int32_t _M0L10total__lenS179;
      uint16_t* _M0L6bufferS180;
      int32_t _M0L12digit__startS181;
      #line 243 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS178 = _M0FPB12hex__count32(_M0L3numS172);
      if (_M0L12is__negativeS171) {
        _M0L6_2atmpS923 = 1;
      } else {
        _M0L6_2atmpS923 = 0;
      }
      _M0L10total__lenS179 = _M0L10digit__lenS178 + _M0L6_2atmpS923;
      _M0L6bufferS180
      = (uint16_t*)moonbit_make_string(_M0L10total__lenS179, 0);
      if (_M0L12is__negativeS171) {
        _M0L12digit__startS181 = 1;
      } else {
        _M0L12digit__startS181 = 0;
      }
      #line 247 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0FPB20int__to__string__hex(_M0L6bufferS180, _M0L3numS172, _M0L12digit__startS181, _M0L10total__lenS179);
      _M0L6bufferS173 = _M0L6bufferS180;
      break;
    }
    default: {
      int32_t _M0L10digit__lenS182;
      int32_t _M0L6_2atmpS924;
      int32_t _M0L10total__lenS183;
      uint16_t* _M0L6bufferS184;
      int32_t _M0L12digit__startS185;
      #line 251 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS182
      = _M0FPB14radix__count32(_M0L3numS172, _M0L5radixS169);
      if (_M0L12is__negativeS171) {
        _M0L6_2atmpS924 = 1;
      } else {
        _M0L6_2atmpS924 = 0;
      }
      _M0L10total__lenS183 = _M0L10digit__lenS182 + _M0L6_2atmpS924;
      _M0L6bufferS184
      = (uint16_t*)moonbit_make_string(_M0L10total__lenS183, 0);
      if (_M0L12is__negativeS171) {
        _M0L12digit__startS185 = 1;
      } else {
        _M0L12digit__startS185 = 0;
      }
      #line 255 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0FPB24int__to__string__generic(_M0L6bufferS184, _M0L3numS172, _M0L12digit__startS185, _M0L10total__lenS183, _M0L5radixS169);
      _M0L6bufferS173 = _M0L6bufferS184;
      break;
    }
  }
  if (_M0L12is__negativeS171) {
    _M0L6bufferS173[0] = 45;
  }
  return _M0L6bufferS173;
}

int32_t _M0FPB14radix__count32(
  uint32_t _M0L5valueS163,
  int32_t _M0L5radixS165
) {
  uint32_t _M0L4baseS164;
  uint32_t _M0L3numS166;
  int32_t _M0L5countS167;
  #line 189 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  if (_M0L5valueS163 == 0u) {
    return 1;
  }
  _M0L4baseS164 = *(uint32_t*)&_M0L5radixS165;
  _M0L3numS166 = _M0L5valueS163;
  _M0L5countS167 = 0;
  while (1) {
    if (_M0L3numS166 > 0u) {
      uint32_t _M0L6_2atmpS920 = _M0L3numS166 / _M0L4baseS164;
      int32_t _M0L6_2atmpS921 = _M0L5countS167 + 1;
      _M0L3numS166 = _M0L6_2atmpS920;
      _M0L5countS167 = _M0L6_2atmpS921;
      continue;
    } else {
      return _M0L5countS167;
    }
    break;
  }
}

int32_t _M0FPB12hex__count32(uint32_t _M0L5valueS161) {
  #line 177 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  if (_M0L5valueS161 == 0u) {
    return 1;
  } else {
    int32_t _M0L14leading__zerosS162;
    int32_t _M0L6_2atmpS919;
    int32_t _M0L6_2atmpS918;
    #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0L14leading__zerosS162 = moonbit_clz32(_M0L5valueS161);
    _M0L6_2atmpS919 = 31 - _M0L14leading__zerosS162;
    _M0L6_2atmpS918 = _M0L6_2atmpS919 / 4;
    return _M0L6_2atmpS918 + 1;
  }
}

int32_t _M0FPB12dec__count32(uint32_t _M0L5valueS160) {
  #line 143 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  if (_M0L5valueS160 >= 100000u) {
    if (_M0L5valueS160 >= 10000000u) {
      if (_M0L5valueS160 >= 1000000000u) {
        return 10;
      } else if (_M0L5valueS160 >= 100000000u) {
        return 9;
      } else {
        return 8;
      }
    } else if (_M0L5valueS160 >= 1000000u) {
      return 7;
    } else {
      return 6;
    }
  } else if (_M0L5valueS160 >= 1000u) {
    if (_M0L5valueS160 >= 10000u) {
      return 5;
    } else {
      return 4;
    }
  } else if (_M0L5valueS160 >= 100u) {
    return 3;
  } else if (_M0L5valueS160 >= 10u) {
    return 2;
  } else {
    return 1;
  }
}

int32_t _M0FPB20int__to__string__dec(
  uint16_t* _M0L6bufferS146,
  uint32_t _M0L3numS158,
  int32_t _M0L12digit__startS147,
  int32_t _M0L10total__lenS159
) {
  int32_t _M0L6_2atmpS917;
  uint32_t _M0L3numS136;
  int32_t _M0L6offsetS137;
  #line 88 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L6_2atmpS917 = _M0L10total__lenS159 - _M0L12digit__startS147;
  _M0L3numS136 = _M0L3numS158;
  _M0L6offsetS137 = _M0L6_2atmpS917;
  while (1) {
    if (_M0L3numS136 >= 10000u) {
      uint32_t _M0L1tS138 = _M0L3numS136 / 10000u;
      uint32_t _M0L6_2atmpS894 = _M0L3numS136 % 10000u;
      int32_t _M0L1rS139 = *(int32_t*)&_M0L6_2atmpS894;
      int32_t _M0L2d1S140 = _M0L1rS139 / 100;
      int32_t _M0L2d2S141 = _M0L1rS139 % 100;
      int32_t _M0L6_2atmpS893 = _M0L2d1S140 / 10;
      int32_t _M0L6_2atmpS892 = 48 + _M0L6_2atmpS893;
      int32_t _M0L6d1__hiS142 = (uint16_t)_M0L6_2atmpS892;
      int32_t _M0L6_2atmpS891 = _M0L2d1S140 % 10;
      int32_t _M0L6_2atmpS890 = 48 + _M0L6_2atmpS891;
      int32_t _M0L6d1__loS143 = (uint16_t)_M0L6_2atmpS890;
      int32_t _M0L6_2atmpS889 = _M0L2d2S141 / 10;
      int32_t _M0L6_2atmpS888 = 48 + _M0L6_2atmpS889;
      int32_t _M0L6d2__hiS144 = (uint16_t)_M0L6_2atmpS888;
      int32_t _M0L6_2atmpS887 = _M0L2d2S141 % 10;
      int32_t _M0L6_2atmpS886 = 48 + _M0L6_2atmpS887;
      int32_t _M0L6d2__loS145 = (uint16_t)_M0L6_2atmpS886;
      int32_t _M0L6_2atmpS878 = _M0L12digit__startS147 + _M0L6offsetS137;
      int32_t _M0L6_2atmpS877 = _M0L6_2atmpS878 - 4;
      int32_t _M0L6_2atmpS880;
      int32_t _M0L6_2atmpS879;
      int32_t _M0L6_2atmpS882;
      int32_t _M0L6_2atmpS881;
      int32_t _M0L6_2atmpS884;
      int32_t _M0L6_2atmpS883;
      int32_t _M0L6_2atmpS885;
      _M0L6bufferS146[_M0L6_2atmpS877] = _M0L6d1__hiS142;
      _M0L6_2atmpS880 = _M0L12digit__startS147 + _M0L6offsetS137;
      _M0L6_2atmpS879 = _M0L6_2atmpS880 - 3;
      _M0L6bufferS146[_M0L6_2atmpS879] = _M0L6d1__loS143;
      _M0L6_2atmpS882 = _M0L12digit__startS147 + _M0L6offsetS137;
      _M0L6_2atmpS881 = _M0L6_2atmpS882 - 2;
      _M0L6bufferS146[_M0L6_2atmpS881] = _M0L6d2__hiS144;
      _M0L6_2atmpS884 = _M0L12digit__startS147 + _M0L6offsetS137;
      _M0L6_2atmpS883 = _M0L6_2atmpS884 - 1;
      _M0L6bufferS146[_M0L6_2atmpS883] = _M0L6d2__loS145;
      _M0L6_2atmpS885 = _M0L6offsetS137 - 4;
      _M0L3numS136 = _M0L1tS138;
      _M0L6offsetS137 = _M0L6_2atmpS885;
      continue;
    } else {
      int32_t _M0L6_2atmpS916 = *(int32_t*)&_M0L3numS136;
      int32_t _M0L9remainingS149 = _M0L6_2atmpS916;
      int32_t _M0L6offsetS150 = _M0L6offsetS137;
      while (1) {
        if (_M0L9remainingS149 >= 100) {
          int32_t _M0L1tS151 = _M0L9remainingS149 / 100;
          int32_t _M0L1dS152 = _M0L9remainingS149 % 100;
          int32_t _M0L6_2atmpS903 = _M0L1dS152 / 10;
          int32_t _M0L6_2atmpS902 = 48 + _M0L6_2atmpS903;
          int32_t _M0L5d__hiS153 = (uint16_t)_M0L6_2atmpS902;
          int32_t _M0L6_2atmpS901 = _M0L1dS152 % 10;
          int32_t _M0L6_2atmpS900 = 48 + _M0L6_2atmpS901;
          int32_t _M0L5d__loS154 = (uint16_t)_M0L6_2atmpS900;
          int32_t _M0L6_2atmpS896 = _M0L12digit__startS147 + _M0L6offsetS150;
          int32_t _M0L6_2atmpS895 = _M0L6_2atmpS896 - 2;
          int32_t _M0L6_2atmpS898;
          int32_t _M0L6_2atmpS897;
          int32_t _M0L6_2atmpS899;
          _M0L6bufferS146[_M0L6_2atmpS895] = _M0L5d__hiS153;
          _M0L6_2atmpS898 = _M0L12digit__startS147 + _M0L6offsetS150;
          _M0L6_2atmpS897 = _M0L6_2atmpS898 - 1;
          _M0L6bufferS146[_M0L6_2atmpS897] = _M0L5d__loS154;
          _M0L6_2atmpS899 = _M0L6offsetS150 - 2;
          _M0L9remainingS149 = _M0L1tS151;
          _M0L6offsetS150 = _M0L6_2atmpS899;
          continue;
        } else if (_M0L9remainingS149 >= 10) {
          int32_t _M0L6_2atmpS911 = _M0L9remainingS149 / 10;
          int32_t _M0L6_2atmpS910 = 48 + _M0L6_2atmpS911;
          int32_t _M0L5d__hiS156 = (uint16_t)_M0L6_2atmpS910;
          int32_t _M0L6_2atmpS909 = _M0L9remainingS149 % 10;
          int32_t _M0L6_2atmpS908 = 48 + _M0L6_2atmpS909;
          int32_t _M0L5d__loS157 = (uint16_t)_M0L6_2atmpS908;
          int32_t _M0L6_2atmpS905 = _M0L12digit__startS147 + _M0L6offsetS150;
          int32_t _M0L6_2atmpS904 = _M0L6_2atmpS905 - 2;
          int32_t _M0L6_2atmpS907;
          int32_t _M0L6_2atmpS906;
          _M0L6bufferS146[_M0L6_2atmpS904] = _M0L5d__hiS156;
          _M0L6_2atmpS907 = _M0L12digit__startS147 + _M0L6offsetS150;
          _M0L6_2atmpS906 = _M0L6_2atmpS907 - 1;
          _M0L6bufferS146[_M0L6_2atmpS906] = _M0L5d__loS157;
        } else {
          int32_t _M0L6_2atmpS915 = _M0L12digit__startS147 + _M0L6offsetS150;
          int32_t _M0L6_2atmpS912 = _M0L6_2atmpS915 - 1;
          int32_t _M0L6_2atmpS914 = 48 + _M0L9remainingS149;
          int32_t _M0L6_2atmpS913 = (uint16_t)_M0L6_2atmpS914;
          _M0L6bufferS146[_M0L6_2atmpS912] = _M0L6_2atmpS913;
        }
        break;
      }
    }
    break;
  }
  return 0;
}

int32_t _M0FPB24int__to__string__generic(
  uint16_t* _M0L6bufferS126,
  uint32_t _M0L3numS130,
  int32_t _M0L12digit__startS127,
  int32_t _M0L10total__lenS129,
  int32_t _M0L5radixS120
) {
  uint32_t _M0L4baseS119;
  int32_t _M0L6_2atmpS862;
  int32_t _M0L6_2atmpS861;
  #line 57 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L4baseS119 = *(uint32_t*)&_M0L5radixS120;
  _M0L6_2atmpS862 = _M0L5radixS120 - 1;
  _M0L6_2atmpS861 = _M0L5radixS120 & _M0L6_2atmpS862;
  if (_M0L6_2atmpS861 == 0) {
    int32_t _M0L5shiftS121;
    uint32_t _M0L4maskS122;
    int32_t _M0L6_2atmpS869;
    int32_t _M0L6offsetS123;
    uint32_t _M0L1nS124;
    #line 68 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0L5shiftS121 = moonbit_ctz32(_M0L5radixS120);
    _M0L4maskS122 = _M0L4baseS119 - 1u;
    _M0L6_2atmpS869 = _M0L10total__lenS129 - _M0L12digit__startS127;
    _M0L6offsetS123 = _M0L6_2atmpS869;
    _M0L1nS124 = _M0L3numS130;
    while (1) {
      if (_M0L1nS124 > 0u) {
        uint32_t _M0L6_2atmpS868 = _M0L1nS124 & _M0L4maskS122;
        int32_t _M0L5digitS125 = *(int32_t*)&_M0L6_2atmpS868;
        int32_t _M0L6_2atmpS865 = _M0L12digit__startS127 + _M0L6offsetS123;
        int32_t _M0L6_2atmpS863 = _M0L6_2atmpS865 - 1;
        int32_t _M0L6_2atmpS864 =
          ((moonbit_string_t)moonbit_string_literal_11.data)[_M0L5digitS125];
        int32_t _M0L6_2atmpS866;
        uint32_t _M0L6_2atmpS867;
        _M0L6bufferS126[_M0L6_2atmpS863] = _M0L6_2atmpS864;
        _M0L6_2atmpS866 = _M0L6offsetS123 - 1;
        _M0L6_2atmpS867 = _M0L1nS124 >> (_M0L5shiftS121 & 31);
        _M0L6offsetS123 = _M0L6_2atmpS866;
        _M0L1nS124 = _M0L6_2atmpS867;
        continue;
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS876 = _M0L10total__lenS129 - _M0L12digit__startS127;
    int32_t _M0L6offsetS131 = _M0L6_2atmpS876;
    uint32_t _M0L1nS132 = _M0L3numS130;
    while (1) {
      if (_M0L1nS132 > 0u) {
        uint32_t _M0L1qS133 = _M0L1nS132 / _M0L4baseS119;
        uint32_t _M0L6_2atmpS875 = _M0L1qS133 * _M0L4baseS119;
        uint32_t _M0L6_2atmpS874 = _M0L1nS132 - _M0L6_2atmpS875;
        int32_t _M0L5digitS134 = *(int32_t*)&_M0L6_2atmpS874;
        int32_t _M0L6_2atmpS872 = _M0L12digit__startS127 + _M0L6offsetS131;
        int32_t _M0L6_2atmpS870 = _M0L6_2atmpS872 - 1;
        int32_t _M0L6_2atmpS871 =
          ((moonbit_string_t)moonbit_string_literal_11.data)[_M0L5digitS134];
        int32_t _M0L6_2atmpS873;
        _M0L6bufferS126[_M0L6_2atmpS870] = _M0L6_2atmpS871;
        _M0L6_2atmpS873 = _M0L6offsetS131 - 1;
        _M0L6offsetS131 = _M0L6_2atmpS873;
        _M0L1nS132 = _M0L1qS133;
        continue;
      }
      break;
    }
  }
  return 0;
}

int32_t _M0FPB20int__to__string__hex(
  uint16_t* _M0L6bufferS113,
  uint32_t _M0L3numS118,
  int32_t _M0L12digit__startS114,
  int32_t _M0L10total__lenS117
) {
  int32_t _M0L6_2atmpS860;
  int32_t _M0L6offsetS108;
  uint32_t _M0L1nS109;
  #line 29 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L6_2atmpS860 = _M0L10total__lenS117 - _M0L12digit__startS114;
  _M0L6offsetS108 = _M0L6_2atmpS860;
  _M0L1nS109 = _M0L3numS118;
  while (1) {
    if (_M0L6offsetS108 >= 2) {
      uint32_t _M0L6_2atmpS857 = _M0L1nS109 & 255u;
      int32_t _M0L9byte__valS110 = *(int32_t*)&_M0L6_2atmpS857;
      int32_t _M0L2hiS111 = _M0L9byte__valS110 / 16;
      int32_t _M0L2loS112 = _M0L9byte__valS110 % 16;
      int32_t _M0L6_2atmpS851 = _M0L12digit__startS114 + _M0L6offsetS108;
      int32_t _M0L6_2atmpS849 = _M0L6_2atmpS851 - 2;
      int32_t _M0L6_2atmpS850 =
        ((moonbit_string_t)moonbit_string_literal_11.data)[_M0L2hiS111];
      int32_t _M0L6_2atmpS854;
      int32_t _M0L6_2atmpS852;
      int32_t _M0L6_2atmpS853;
      int32_t _M0L6_2atmpS855;
      uint32_t _M0L6_2atmpS856;
      _M0L6bufferS113[_M0L6_2atmpS849] = _M0L6_2atmpS850;
      _M0L6_2atmpS854 = _M0L12digit__startS114 + _M0L6offsetS108;
      _M0L6_2atmpS852 = _M0L6_2atmpS854 - 1;
      _M0L6_2atmpS853
      = ((moonbit_string_t)moonbit_string_literal_11.data)[
        _M0L2loS112
      ];
      _M0L6bufferS113[_M0L6_2atmpS852] = _M0L6_2atmpS853;
      _M0L6_2atmpS855 = _M0L6offsetS108 - 2;
      _M0L6_2atmpS856 = _M0L1nS109 >> 8;
      _M0L6offsetS108 = _M0L6_2atmpS855;
      _M0L1nS109 = _M0L6_2atmpS856;
      continue;
    } else if (_M0L6offsetS108 == 1) {
      uint32_t _M0L6_2atmpS859 = _M0L1nS109 & 15u;
      int32_t _M0L6nibbleS116 = *(int32_t*)&_M0L6_2atmpS859;
      int32_t _M0L6_2atmpS858 =
        ((moonbit_string_t)moonbit_string_literal_11.data)[_M0L6nibbleS116];
      _M0L6bufferS113[_M0L12digit__startS114] = _M0L6_2atmpS858;
    }
    break;
  }
  return 0;
}

int32_t _M0IP016_24default__implPB4Show6outputGmE(
  uint64_t _M0L4selfS105,
  struct _M0TPB6Logger _M0L6loggerS104
) {
  moonbit_string_t _M0L6_2atmpS847;
  #line 159 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  #line 160 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS847 = _M0IPC16uint646UInt64PB4Show10to__string(_M0L4selfS105);
  #line 160 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6loggerS104.$0->$method_0(_M0L6loggerS104.$1, _M0L6_2atmpS847);
  moonbit_decref(_M0L6_2atmpS847);
  return 0;
}

int32_t _M0IP016_24default__implPB4Show6outputGiE(
  int32_t _M0L4selfS107,
  struct _M0TPB6Logger _M0L6loggerS106
) {
  moonbit_string_t _M0L6_2atmpS848;
  #line 159 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  #line 160 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS848 = _M0IPC13int3IntPB4Show10to__string(_M0L4selfS107);
  #line 160 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6loggerS106.$0->$method_0(_M0L6loggerS106.$1, _M0L6_2atmpS848);
  moonbit_decref(_M0L6_2atmpS848);
  return 0;
}

int32_t _M0MPC16string10StringView13start__offset(
  struct _M0TPC16string10StringView _M0L4selfS103
) {
  #line 99 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
  return _M0L4selfS103.$1;
}

moonbit_string_t _M0MPC16string10StringView4data(
  struct _M0TPC16string10StringView _M0L4selfS102
) {
  moonbit_string_t _M0L8_2afieldS1630;
  #line 92 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
  _M0L8_2afieldS1630 = _M0L4selfS102.$0;
  moonbit_incref(_M0L8_2afieldS1630);
  return _M0L8_2afieldS1630;
}

int32_t _M0IP016_24default__implPB6Logger16write__substringGRPB13StringBuilderE(
  struct _M0TPB13StringBuilder* _M0L4selfS98,
  moonbit_string_t _M0L5valueS99,
  int32_t _M0L5startS100,
  int32_t _M0L3lenS101
) {
  int32_t _M0L6_2atmpS846;
  int64_t _M0L6_2atmpS845;
  struct _M0TPC16string10StringView _M0L6_2atmpS844;
  #line 122 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS846 = _M0L5startS100 + _M0L3lenS101;
  _M0L6_2atmpS845 = (int64_t)_M0L6_2atmpS846;
  #line 123 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS844
  = _M0MPC16string6String11sub_2einner(_M0L5valueS99, _M0L5startS100, _M0L6_2atmpS845);
  #line 123 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0IPB13StringBuilderPB6Logger11write__view(_M0L4selfS98, _M0L6_2atmpS844);
  moonbit_decref(_M0L6_2atmpS844.$0);
  return 0;
}

struct _M0TPC16string10StringView _M0MPC16string6String11sub_2einner(
  moonbit_string_t _M0L4selfS90,
  int32_t _M0L5startS97,
  int64_t _M0L3endS94
) {
  int32_t _M0L3lenS89;
  int32_t _M0L3endS93;
  int32_t _M0L3endS91;
  #line 923 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
  _M0L3lenS89 = Moonbit_array_length(_M0L4selfS90);
  if (_M0L3endS94 == 4294967296ll) {
    _M0L3endS93 = _M0L3lenS89;
    goto join_92;
  } else {
    int64_t _M0L7_2aSomeS95 = _M0L3endS94;
    int32_t _M0L6_2aendS96 = (int32_t)_M0L7_2aSomeS95;
    _M0L3endS93 = _M0L6_2aendS96;
    goto join_92;
  }
  goto joinlet_1713;
  join_92:;
  _M0L3endS91 = _M0L3endS93;
  joinlet_1713:;
  if (
    _M0L5startS97 >= 0
    && _M0L5startS97 <= _M0L3endS91
    && _M0L3endS91 <= _M0L3lenS89
  ) {
    if (_M0L5startS97 < _M0L3lenS89) {
      int32_t _M0L6_2atmpS841 = _M0L4selfS90[_M0L5startS97];
      int32_t _M0L6_2atmpS840;
      #line 931 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
      _M0L6_2atmpS840
      = _M0MPC16uint166UInt1623is__trailing__surrogate(_M0L6_2atmpS841);
      if (!_M0L6_2atmpS840) {
        
      } else {
        #line 931 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
        moonbit_panic();
      }
    }
    if (_M0L3endS91 < _M0L3lenS89) {
      int32_t _M0L6_2atmpS843 = _M0L4selfS90[_M0L3endS91];
      int32_t _M0L6_2atmpS842;
      #line 934 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
      _M0L6_2atmpS842
      = _M0MPC16uint166UInt1623is__trailing__surrogate(_M0L6_2atmpS843);
      if (!_M0L6_2atmpS842) {
        
      } else {
        #line 934 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
        moonbit_panic();
      }
    }
    moonbit_incref(_M0L4selfS90);
    return (struct _M0TPC16string10StringView){.$0 = _M0L4selfS90,
                                                 .$1 = _M0L5startS97,
                                                 .$2 = _M0L3endS91};
  } else {
    #line 929 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
    moonbit_panic();
  }
}

int32_t _M0IP016_24default__implPB6Logger5writeGRPB13StringBuilderE(
  struct _M0TPB13StringBuilder* _M0L4selfS88,
  struct _M0TPB4Show _M0L4showS87
) {
  struct _M0TPB6Logger _M0L6_2atmpS839;
  #line 116 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  moonbit_incref(_M0L4selfS88);
  _M0L6_2atmpS839
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS88
  };
  #line 117 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L4showS87.$0->$method_0(_M0L4showS87.$1, _M0L6_2atmpS839);
  if (_M0L6_2atmpS839.$1) {
    moonbit_decref(_M0L6_2atmpS839.$1);
  }
  return 0;
}

int32_t _M0IP016_24default__implPB6Logger28write__string__interpolationGRPB13StringBuilderE(
  struct _M0TPB13StringBuilder* _M0L4selfS86,
  struct _M0TPB4Show _M0L4showS85
) {
  struct _M0TPB6Logger _M0L6_2atmpS838;
  #line 111 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  moonbit_incref(_M0L4selfS86);
  _M0L6_2atmpS838
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS86
  };
  #line 112 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L4showS85.$0->$method_0(_M0L4showS85.$1, _M0L6_2atmpS838);
  if (_M0L6_2atmpS838.$1) {
    moonbit_decref(_M0L6_2atmpS838.$1);
  }
  return 0;
}

uint64_t _M0MPC13int3Int10to__uint64(int32_t _M0L4selfS84) {
  int64_t _M0L6_2atmpS837;
  #line 989 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS837 = (int64_t)_M0L4selfS84;
  return *(uint64_t*)&_M0L6_2atmpS837;
}

int32_t _M0IPC16uint166UInt16PB7Default7default() {
  #line 201 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uint16_char.mbt"
  return 0;
}

int32_t _M0MPC16uint166UInt1623is__trailing__surrogate(int32_t _M0L4selfS83) {
  #line 58 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uint16_char.mbt"
  return _M0L4selfS83 >= 56320 && _M0L4selfS83 <= 57343;
}

int32_t _M0IPB13StringBuilderPB6Logger13write__string(
  struct _M0TPB13StringBuilder* _M0L4selfS82,
  moonbit_string_t _M0L3strS80
) {
  int32_t _M0L8str__lenS79;
  int32_t _M0L3lenS836;
  int32_t _M0L8requiredS81;
  uint16_t* _M0L4dataS831;
  int32_t _M0L6_2atmpS830;
  int32_t _if__result_1714;
  uint16_t* _M0L4dataS832;
  int32_t _M0L3lenS833;
  int32_t _M0L3lenS835;
  int32_t _M0L6_2atmpS834;
  #line 105 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L8str__lenS79 = Moonbit_array_length(_M0L3strS80);
  if (_M0L8str__lenS79 == 0) {
    return 0;
  }
  _M0L3lenS836 = _M0L4selfS82->$1;
  _M0L8requiredS81 = _M0L3lenS836 + _M0L8str__lenS79;
  _M0L4dataS831 = _M0L4selfS82->$0;
  _M0L6_2atmpS830 = Moonbit_array_length(_M0L4dataS831);
  if (_M0L8requiredS81 > _M0L6_2atmpS830) {
    _if__result_1714 = 1;
  } else {
    int32_t _M0L3lenS829 = _M0L4selfS82->$1;
    _if__result_1714 = _M0L8requiredS81 < _M0L3lenS829;
  }
  if (_if__result_1714) {
    #line 112 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0MPB13StringBuilder4grow(_M0L4selfS82, _M0L8requiredS81);
  }
  _M0L4dataS832 = _M0L4selfS82->$0;
  _M0L3lenS833 = _M0L4selfS82->$1;
  moonbit_incref(_M0L4dataS832);
  #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0MPC15array10FixedArray26unsafe__blit__from__string(_M0L4dataS832, _M0L3lenS833, _M0L3strS80, 0, _M0L8str__lenS79);
  moonbit_decref(_M0L4dataS832);
  _M0L3lenS835 = _M0L4selfS82->$1;
  _M0L6_2atmpS834 = _M0L3lenS835 + _M0L8str__lenS79;
  _M0L4selfS82->$1 = _M0L6_2atmpS834;
  return 0;
}

int32_t _M0MPC15array10FixedArray26unsafe__blit__from__string(
  uint16_t* _M0L4selfS75,
  int32_t _M0L11dst__offsetS78,
  moonbit_string_t _M0L3strS76,
  int32_t _M0L11str__offsetS71,
  int32_t _M0L3lenS72
) {
  int32_t _M0L16end__str__offsetS70;
  int32_t _M0L1iS73;
  int32_t _M0L1jS74;
  #line 90 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L16end__str__offsetS70 = _M0L11str__offsetS71 + _M0L3lenS72;
  _M0L1iS73 = _M0L11str__offsetS71;
  _M0L1jS74 = _M0L11dst__offsetS78;
  while (1) {
    if (_M0L1iS73 < _M0L16end__str__offsetS70) {
      int32_t _M0L6_2atmpS826 = _M0L3strS76[_M0L1iS73];
      int32_t _M0L6_2atmpS827;
      int32_t _M0L6_2atmpS828;
      _M0L4selfS75[_M0L1jS74] = _M0L6_2atmpS826;
      _M0L6_2atmpS827 = _M0L1iS73 + 1;
      _M0L6_2atmpS828 = _M0L1jS74 + 1;
      _M0L1iS73 = _M0L6_2atmpS827;
      _M0L1jS74 = _M0L6_2atmpS828;
      continue;
    }
    break;
  }
  return 0;
}

int32_t _M0IPB13StringBuilderPB6Logger11write__char(
  struct _M0TPB13StringBuilder* _M0L4selfS68,
  int32_t _M0L2chS67
) {
  uint32_t _M0L4codeS66;
  #line 120 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  #line 121 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L4codeS66 = _M0MPC14char4Char8to__uint(_M0L2chS67);
  if (_M0L4codeS66 <= 65535u) {
    int32_t _M0L3lenS797 = _M0L4selfS68->$1;
    uint16_t* _M0L4dataS799 = _M0L4selfS68->$0;
    int32_t _M0L6_2atmpS798 = Moonbit_array_length(_M0L4dataS799);
    uint16_t* _M0L4dataS802;
    int32_t _M0L3lenS803;
    int32_t _M0L6_2atmpS804;
    int32_t _M0L3lenS806;
    int32_t _M0L6_2atmpS805;
    if (_M0L3lenS797 >= _M0L6_2atmpS798) {
      int32_t _M0L3lenS801 = _M0L4selfS68->$1;
      int32_t _M0L6_2atmpS800 = _M0L3lenS801 + 1;
      #line 124 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0MPB13StringBuilder4grow(_M0L4selfS68, _M0L6_2atmpS800);
    }
    _M0L4dataS802 = _M0L4selfS68->$0;
    _M0L3lenS803 = _M0L4selfS68->$1;
    moonbit_incref(_M0L4dataS802);
    #line 126 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0L6_2atmpS804 = _M0MPC14uint4UInt10to__uint16(_M0L4codeS66);
    if (
      _M0L3lenS803 < 0 || _M0L3lenS803 >= Moonbit_array_length(_M0L4dataS802)
    ) {
      #line 126 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      moonbit_panic();
    }
    _M0L4dataS802[_M0L3lenS803] = _M0L6_2atmpS804;
    moonbit_decref(_M0L4dataS802);
    _M0L3lenS806 = _M0L4selfS68->$1;
    _M0L6_2atmpS805 = _M0L3lenS806 + 1;
    _M0L4selfS68->$1 = _M0L6_2atmpS805;
  } else if (_M0L4codeS66 <= 1114111u) {
    uint16_t* _M0L4dataS810 = _M0L4selfS68->$0;
    int32_t _M0L6_2atmpS808 = Moonbit_array_length(_M0L4dataS810);
    int32_t _M0L3lenS809 = _M0L4selfS68->$1;
    int32_t _M0L6_2atmpS807 = _M0L6_2atmpS808 - _M0L3lenS809;
    uint32_t _M0L4codeS69;
    uint16_t* _M0L4dataS813;
    int32_t _M0L3lenS814;
    uint32_t _M0L6_2atmpS817;
    uint32_t _M0L6_2atmpS816;
    int32_t _M0L6_2atmpS815;
    uint16_t* _M0L4dataS818;
    int32_t _M0L3lenS823;
    int32_t _M0L6_2atmpS819;
    uint32_t _M0L6_2atmpS822;
    uint32_t _M0L6_2atmpS821;
    int32_t _M0L6_2atmpS820;
    int32_t _M0L3lenS825;
    int32_t _M0L6_2atmpS824;
    if (_M0L6_2atmpS807 < 2) {
      int32_t _M0L3lenS812 = _M0L4selfS68->$1;
      int32_t _M0L6_2atmpS811 = _M0L3lenS812 + 2;
      #line 130 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0MPB13StringBuilder4grow(_M0L4selfS68, _M0L6_2atmpS811);
    }
    _M0L4codeS69 = _M0L4codeS66 - 65536u;
    _M0L4dataS813 = _M0L4selfS68->$0;
    _M0L3lenS814 = _M0L4selfS68->$1;
    _M0L6_2atmpS817 = _M0L4codeS69 >> 10;
    _M0L6_2atmpS816 = 55296u + _M0L6_2atmpS817;
    moonbit_incref(_M0L4dataS813);
    #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0L6_2atmpS815 = _M0MPC14uint4UInt10to__uint16(_M0L6_2atmpS816);
    if (
      _M0L3lenS814 < 0 || _M0L3lenS814 >= Moonbit_array_length(_M0L4dataS813)
    ) {
      #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      moonbit_panic();
    }
    _M0L4dataS813[_M0L3lenS814] = _M0L6_2atmpS815;
    moonbit_decref(_M0L4dataS813);
    _M0L4dataS818 = _M0L4selfS68->$0;
    _M0L3lenS823 = _M0L4selfS68->$1;
    _M0L6_2atmpS819 = _M0L3lenS823 + 1;
    _M0L6_2atmpS822 = _M0L4codeS69 & 1023u;
    _M0L6_2atmpS821 = 56320u + _M0L6_2atmpS822;
    moonbit_incref(_M0L4dataS818);
    #line 134 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0L6_2atmpS820 = _M0MPC14uint4UInt10to__uint16(_M0L6_2atmpS821);
    if (
      _M0L6_2atmpS819 < 0
      || _M0L6_2atmpS819 >= Moonbit_array_length(_M0L4dataS818)
    ) {
      #line 134 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      moonbit_panic();
    }
    _M0L4dataS818[_M0L6_2atmpS819] = _M0L6_2atmpS820;
    moonbit_decref(_M0L4dataS818);
    _M0L3lenS825 = _M0L4selfS68->$1;
    _M0L6_2atmpS824 = _M0L3lenS825 + 2;
    _M0L4selfS68->$1 = _M0L6_2atmpS824;
  } else {
    #line 137 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_12.data);
  }
  return 0;
}

int32_t _M0MPB13StringBuilder4grow(
  struct _M0TPB13StringBuilder* _M0L4selfS63,
  int32_t _M0L8requiredS64
) {
  uint16_t* _M0L4dataS796;
  int32_t _M0L6_2atmpS794;
  int32_t _M0L3lenS795;
  int32_t _M0L13new__capacityS62;
  uint16_t* _M0L4dataS791;
  int32_t _M0L6_2atmpS792;
  int32_t _M0L3lenS793;
  uint16_t* _M0L9new__dataS65;
  uint16_t* _M0L6_2aoldS1631;
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L4dataS796 = _M0L4selfS63->$0;
  _M0L6_2atmpS794 = Moonbit_array_length(_M0L4dataS796);
  _M0L3lenS795 = _M0L4selfS63->$1;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L13new__capacityS62
  = _M0FPB31stringbuilder__growth__capacity(_M0L6_2atmpS794, _M0L3lenS795, _M0L8requiredS64);
  _M0L4dataS791 = _M0L4selfS63->$0;
  moonbit_incref(_M0L4dataS791);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L6_2atmpS792 = _M0IPC16uint166UInt16PB7Default7default();
  _M0L3lenS793 = _M0L4selfS63->$1;
  #line 80 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L9new__dataS65
  = _M0MPC15array10FixedArray23make__and__blit_2einnerGkE(_M0L4dataS791, _M0L13new__capacityS62, _M0L6_2atmpS792, _M0L3lenS793, 0, 0);
  _M0L6_2aoldS1631 = _M0L4selfS63->$0;
  moonbit_decref(_M0L6_2aoldS1631);
  _M0L4selfS63->$0 = _M0L9new__dataS65;
  return 0;
}

int32_t _M0FPB31stringbuilder__growth__capacity(
  int32_t _M0L7currentS61,
  int32_t _M0L3lenS57,
  int32_t _M0L8requiredS56
) {
  int32_t _M0L5spaceS58;
  #line 48 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  if (_M0L8requiredS56 < _M0L3lenS57) {
    #line 55 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_13.data);
  }
  _M0L5spaceS58 = _M0L7currentS61;
  while (1) {
    if (_M0L5spaceS58 < _M0L8requiredS56) {
      int32_t _M0L4nextS59 = _M0L5spaceS58 * 2;
      if (_M0L4nextS59 <= _M0L5spaceS58) {
        return _M0L8requiredS56;
      }
      _M0L5spaceS58 = _M0L4nextS59;
      continue;
    } else {
      return _M0L5spaceS58;
    }
    break;
  }
}

int32_t _M0MPC14uint4UInt10to__uint16(uint32_t _M0L4selfS55) {
  int32_t _M0L6_2atmpS790;
  #line 2785 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS790 = *(int32_t*)&_M0L4selfS55;
  return (uint16_t)_M0L6_2atmpS790;
}

uint32_t _M0MPC14char4Char8to__uint(int32_t _M0L4selfS54) {
  int32_t _M0L6_2atmpS789;
  #line 1335 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS789 = _M0L4selfS54;
  return *(uint32_t*)&_M0L6_2atmpS789;
}

moonbit_string_t _M0MPB13StringBuilder10to__string(
  struct _M0TPB13StringBuilder* _M0L4selfS52
) {
  int32_t _M0L3lenS780;
  #line 181 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L3lenS780 = _M0L4selfS52->$1;
  if (_M0L3lenS780 == 0) {
    return (moonbit_string_t)moonbit_string_literal_5.data;
  } else {
    int32_t _M0L3lenS781 = _M0L4selfS52->$1;
    uint16_t* _M0L4dataS783 = _M0L4selfS52->$0;
    int32_t _M0L6_2atmpS782 = Moonbit_array_length(_M0L4dataS783);
    if (_M0L3lenS781 == _M0L6_2atmpS782) {
      uint16_t* _M0L4dataS784 = _M0L4selfS52->$0;
      moonbit_incref(_M0L4dataS784);
      return _M0L4dataS784;
    } else {
      uint16_t* _M0L4dataS785 = _M0L4selfS52->$0;
      int32_t _M0L3lenS786 = _M0L4selfS52->$1;
      int32_t _M0L6_2atmpS787;
      int32_t _M0L3lenS788;
      uint16_t* _M0L4dataS53;
      moonbit_incref(_M0L4dataS785);
      #line 190 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0L6_2atmpS787 = _M0IPC16uint166UInt16PB7Default7default();
      _M0L3lenS788 = _M0L4selfS52->$1;
      #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0L4dataS53
      = _M0MPC15array10FixedArray23make__and__blit_2einnerGkE(_M0L4dataS785, _M0L3lenS786, _M0L6_2atmpS787, _M0L3lenS788, 0, 0);
      return _M0L4dataS53;
    }
  }
}

uint16_t* _M0MPC15array10FixedArray23make__and__blit_2einnerGkE(
  uint16_t* _M0L3srcS49,
  int32_t _M0L13allocate__lenS45,
  int32_t _M0L4initS50,
  int32_t _M0L3lenS46,
  int32_t _M0L11src__offsetS47,
  int32_t _M0L11dst__offsetS48
) {
  int32_t _if__result_1717;
  #line 97 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
  if (_M0L13allocate__lenS45 >= 0) {
    if (_M0L3lenS46 >= 0) {
      if (_M0L11src__offsetS47 >= 0) {
        if (_M0L11dst__offsetS48 >= 0) {
          int32_t _M0L6_2atmpS776 = _M0L11src__offsetS47 + _M0L3lenS46;
          int32_t _M0L6_2atmpS777 = Moonbit_array_length(_M0L3srcS49);
          if (_M0L6_2atmpS776 <= _M0L6_2atmpS777) {
            int32_t _M0L6_2atmpS775 = _M0L11dst__offsetS48 + _M0L3lenS46;
            _if__result_1717 = _M0L6_2atmpS775 <= _M0L13allocate__lenS45;
          } else {
            _if__result_1717 = 0;
          }
        } else {
          _if__result_1717 = 0;
        }
      } else {
        _if__result_1717 = 0;
      }
    } else {
      _if__result_1717 = 0;
    }
  } else {
    _if__result_1717 = 0;
  }
  if (_if__result_1717) {
    #line 116 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    return _M0MPC15array10FixedArray23unsafe__make__and__blitGkE(_M0L3srcS49, _M0L13allocate__lenS45, _M0L4initS50, _M0L11src__offsetS47, _M0L11dst__offsetS48, _M0L3lenS46);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS51;
    int32_t _M0L6_2atmpS779;
    moonbit_string_t _M0L6_2atmpS778;
    uint16_t* _result_1718;
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0L18_2astring__builderS51
    = _M0MPB13StringBuilder21StringBuilder_2einner(89);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS51, (moonbit_string_t)moonbit_string_literal_14.data);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS51, _M0L13allocate__lenS45);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS51, (moonbit_string_t)moonbit_string_literal_15.data);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS51, _M0L11src__offsetS47);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS51, (moonbit_string_t)moonbit_string_literal_16.data);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS51, _M0L11dst__offsetS48);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS51, (moonbit_string_t)moonbit_string_literal_17.data);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS51, _M0L3lenS46);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS51, (moonbit_string_t)moonbit_string_literal_18.data);
    _M0L6_2atmpS779 = Moonbit_array_length(_M0L3srcS49);
    moonbit_decref(_M0L3srcS49);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS51, _M0L6_2atmpS779);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0L6_2atmpS778
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS51);
    moonbit_decref(_M0L18_2astring__builderS51);
    #line 112 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _result_1718 = _M0FPC15abort5abortGAkE(_M0L6_2atmpS778);
    moonbit_decref(_M0L6_2atmpS778);
    return _result_1718;
  }
}

uint16_t* _M0MPC15array10FixedArray23unsafe__make__and__blitGkE(
  uint16_t* _M0L3srcS42,
  int32_t _M0L13allocate__lenS39,
  int32_t _M0L4initS40,
  int32_t _M0L11src__offsetS43,
  int32_t _M0L11dst__offsetS41,
  int32_t _M0L9blit__lenS44
) {
  uint16_t* _M0L3dstS38;
  #line 79 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
  _M0L3dstS38
  = (uint16_t*)moonbit_make_string(_M0L13allocate__lenS39, _M0L4initS40);
  moonbit_incref(_M0L3dstS38);
  #line 90 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
  moonbit_unsafe_val_array_blit(_M0L3dstS38, _M0L11dst__offsetS41, _M0L3srcS42, _M0L11src__offsetS43, _M0L9blit__lenS44, sizeof(uint16_t));
  return _M0L3dstS38;
}

struct _M0TPB13StringBuilder* _M0MPB13StringBuilder21StringBuilder_2einner(
  int32_t _M0L10size__hintS36
) {
  int32_t _M0L7initialS35;
  uint16_t* _M0L4dataS37;
  struct _M0TPB13StringBuilder* _block_1719;
  #line 32 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  if (_M0L10size__hintS36 < 1) {
    _M0L7initialS35 = 1;
  } else {
    int32_t _M0L6_2atmpS774 = _M0L10size__hintS36 + 1;
    _M0L7initialS35 = _M0L6_2atmpS774 / 2;
  }
  _M0L4dataS37 = (uint16_t*)moonbit_make_string(_M0L7initialS35, 0);
  _block_1719
  = (struct _M0TPB13StringBuilder*)moonbit_malloc(sizeof(struct _M0TPB13StringBuilder));
  Moonbit_object_header(_block_1719)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 38, 0);
  _block_1719->$0 = _M0L4dataS37;
  _block_1719->$1 = 0;
  return _block_1719;
}

int32_t _M0MPB13StringBuilder13write__objectGmE(
  struct _M0TPB13StringBuilder* _M0L4selfS32,
  uint64_t _M0L3objS31
) {
  struct _M0TPB6Logger _M0L6_2atmpS772;
  #line 17 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  moonbit_incref(_M0L4selfS32);
  _M0L6_2atmpS772
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS32
  };
  #line 23 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  _M0IP016_24default__implPB4Show6outputGmE(_M0L3objS31, _M0L6_2atmpS772);
  if (_M0L6_2atmpS772.$1) {
    moonbit_decref(_M0L6_2atmpS772.$1);
  }
  return 0;
}

int32_t _M0MPB13StringBuilder13write__objectGiE(
  struct _M0TPB13StringBuilder* _M0L4selfS34,
  int32_t _M0L3objS33
) {
  struct _M0TPB6Logger _M0L6_2atmpS773;
  #line 17 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  moonbit_incref(_M0L4selfS34);
  _M0L6_2atmpS773
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS34
  };
  #line 23 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  _M0IP016_24default__implPB4Show6outputGiE(_M0L3objS33, _M0L6_2atmpS773);
  if (_M0L6_2atmpS773.$1) {
    moonbit_decref(_M0L6_2atmpS773.$1);
  }
  return 0;
}

int32_t _M0MPC15array10FixedArray12unsafe__blitGRPB17UnsafeMaybeUninitGfEE(
  float* _M0L3dstS4,
  int32_t _M0L11dst__offsetS6,
  float* _M0L3srcS5,
  int32_t _M0L11src__offsetS7,
  int32_t _M0L3lenS9
) {
  #line 38 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
  if (_M0L3dstS4 == _M0L3srcS5 && _M0L11dst__offsetS6 < _M0L11src__offsetS7) {
    int32_t _M0L1iS8 = 0;
    while (1) {
      if (_M0L1iS8 < _M0L3lenS9) {
        int32_t _M0L6_2atmpS745 = _M0L11dst__offsetS6 + _M0L1iS8;
        int32_t _M0L6_2atmpS747 = _M0L11src__offsetS7 + _M0L1iS8;
        float _M0L6_2atmpS746;
        int32_t _M0L6_2atmpS748;
        if (
          _M0L6_2atmpS747 < 0
          || _M0L6_2atmpS747 >= Moonbit_array_length(_M0L3srcS5)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS746 = (float)_M0L3srcS5[_M0L6_2atmpS747];
        if (
          _M0L6_2atmpS745 < 0
          || _M0L6_2atmpS745 >= Moonbit_array_length(_M0L3dstS4)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS4[_M0L6_2atmpS745] = _M0L6_2atmpS746;
        _M0L6_2atmpS748 = _M0L1iS8 + 1;
        _M0L1iS8 = _M0L6_2atmpS748;
        continue;
      } else {
        moonbit_decref(_M0L3srcS5);
        moonbit_decref(_M0L3dstS4);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS753 = _M0L3lenS9 - 1;
    int32_t _M0L1iS11 = _M0L6_2atmpS753;
    while (1) {
      if (_M0L1iS11 >= 0) {
        int32_t _M0L6_2atmpS749 = _M0L11dst__offsetS6 + _M0L1iS11;
        int32_t _M0L6_2atmpS751 = _M0L11src__offsetS7 + _M0L1iS11;
        float _M0L6_2atmpS750;
        int32_t _M0L6_2atmpS752;
        if (
          _M0L6_2atmpS751 < 0
          || _M0L6_2atmpS751 >= Moonbit_array_length(_M0L3srcS5)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS750 = (float)_M0L3srcS5[_M0L6_2atmpS751];
        if (
          _M0L6_2atmpS749 < 0
          || _M0L6_2atmpS749 >= Moonbit_array_length(_M0L3dstS4)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS4[_M0L6_2atmpS749] = _M0L6_2atmpS750;
        _M0L6_2atmpS752 = _M0L1iS11 - 1;
        _M0L1iS11 = _M0L6_2atmpS752;
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

int32_t _M0MPC15array10FixedArray12unsafe__blitGRPB17UnsafeMaybeUninitGiEE(
  int32_t* _M0L3dstS13,
  int32_t _M0L11dst__offsetS15,
  int32_t* _M0L3srcS14,
  int32_t _M0L11src__offsetS16,
  int32_t _M0L3lenS18
) {
  #line 38 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
  if (
    _M0L3dstS13 == _M0L3srcS14 && _M0L11dst__offsetS15 < _M0L11src__offsetS16
  ) {
    int32_t _M0L1iS17 = 0;
    while (1) {
      if (_M0L1iS17 < _M0L3lenS18) {
        int32_t _M0L6_2atmpS754 = _M0L11dst__offsetS15 + _M0L1iS17;
        int32_t _M0L6_2atmpS756 = _M0L11src__offsetS16 + _M0L1iS17;
        int32_t _M0L6_2atmpS755;
        int32_t _M0L6_2atmpS757;
        if (
          _M0L6_2atmpS756 < 0
          || _M0L6_2atmpS756 >= Moonbit_array_length(_M0L3srcS14)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS755 = (int32_t)_M0L3srcS14[_M0L6_2atmpS756];
        if (
          _M0L6_2atmpS754 < 0
          || _M0L6_2atmpS754 >= Moonbit_array_length(_M0L3dstS13)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS13[_M0L6_2atmpS754] = _M0L6_2atmpS755;
        _M0L6_2atmpS757 = _M0L1iS17 + 1;
        _M0L1iS17 = _M0L6_2atmpS757;
        continue;
      } else {
        moonbit_decref(_M0L3srcS14);
        moonbit_decref(_M0L3dstS13);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS762 = _M0L3lenS18 - 1;
    int32_t _M0L1iS20 = _M0L6_2atmpS762;
    while (1) {
      if (_M0L1iS20 >= 0) {
        int32_t _M0L6_2atmpS758 = _M0L11dst__offsetS15 + _M0L1iS20;
        int32_t _M0L6_2atmpS760 = _M0L11src__offsetS16 + _M0L1iS20;
        int32_t _M0L6_2atmpS759;
        int32_t _M0L6_2atmpS761;
        if (
          _M0L6_2atmpS760 < 0
          || _M0L6_2atmpS760 >= Moonbit_array_length(_M0L3srcS14)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS759 = (int32_t)_M0L3srcS14[_M0L6_2atmpS760];
        if (
          _M0L6_2atmpS758 < 0
          || _M0L6_2atmpS758 >= Moonbit_array_length(_M0L3dstS13)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS13[_M0L6_2atmpS758] = _M0L6_2atmpS759;
        _M0L6_2atmpS761 = _M0L1iS20 - 1;
        _M0L1iS20 = _M0L6_2atmpS761;
        continue;
      } else {
        moonbit_decref(_M0L3srcS14);
        moonbit_decref(_M0L3dstS13);
      }
      break;
    }
  }
  return 0;
}

int32_t _M0MPC15array10FixedArray12unsafe__blitGkE(
  uint16_t* _M0L3dstS22,
  int32_t _M0L11dst__offsetS24,
  uint16_t* _M0L3srcS23,
  int32_t _M0L11src__offsetS25,
  int32_t _M0L3lenS27
) {
  #line 38 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
  if (
    _M0L3dstS22 == _M0L3srcS23 && _M0L11dst__offsetS24 < _M0L11src__offsetS25
  ) {
    int32_t _M0L1iS26 = 0;
    while (1) {
      if (_M0L1iS26 < _M0L3lenS27) {
        int32_t _M0L6_2atmpS763 = _M0L11dst__offsetS24 + _M0L1iS26;
        int32_t _M0L6_2atmpS765 = _M0L11src__offsetS25 + _M0L1iS26;
        int32_t _M0L6_2atmpS764;
        int32_t _M0L6_2atmpS766;
        if (
          _M0L6_2atmpS765 < 0
          || _M0L6_2atmpS765 >= Moonbit_array_length(_M0L3srcS23)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS764 = (int32_t)_M0L3srcS23[_M0L6_2atmpS765];
        if (
          _M0L6_2atmpS763 < 0
          || _M0L6_2atmpS763 >= Moonbit_array_length(_M0L3dstS22)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS22[_M0L6_2atmpS763] = _M0L6_2atmpS764;
        _M0L6_2atmpS766 = _M0L1iS26 + 1;
        _M0L1iS26 = _M0L6_2atmpS766;
        continue;
      } else {
        moonbit_decref(_M0L3srcS23);
        moonbit_decref(_M0L3dstS22);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS771 = _M0L3lenS27 - 1;
    int32_t _M0L1iS29 = _M0L6_2atmpS771;
    while (1) {
      if (_M0L1iS29 >= 0) {
        int32_t _M0L6_2atmpS767 = _M0L11dst__offsetS24 + _M0L1iS29;
        int32_t _M0L6_2atmpS769 = _M0L11src__offsetS25 + _M0L1iS29;
        int32_t _M0L6_2atmpS768;
        int32_t _M0L6_2atmpS770;
        if (
          _M0L6_2atmpS769 < 0
          || _M0L6_2atmpS769 >= Moonbit_array_length(_M0L3srcS23)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS768 = (int32_t)_M0L3srcS23[_M0L6_2atmpS769];
        if (
          _M0L6_2atmpS767 < 0
          || _M0L6_2atmpS767 >= Moonbit_array_length(_M0L3dstS22)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS22[_M0L6_2atmpS767] = _M0L6_2atmpS768;
        _M0L6_2atmpS770 = _M0L1iS29 - 1;
        _M0L1iS29 = _M0L6_2atmpS770;
        continue;
      } else {
        moonbit_decref(_M0L3srcS23);
        moonbit_decref(_M0L3dstS22);
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
  void* _M0L11_2aobj__ptrS721,
  struct _M0TPB4Show _M0L8_2aparamS720
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS719 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS721;
  _M0IP016_24default__implPB6Logger5writeGRPB13StringBuilderE(_M0L7_2aselfS719, _M0L8_2aparamS720);
  return 0;
}

int32_t _M0IP016_24default__implPB6Logger84write__string__interpolation_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLoggerGRPB13StringBuilderE(
  void* _M0L11_2aobj__ptrS718,
  struct _M0TPB4Show _M0L8_2aparamS717
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS716 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS718;
  _M0IP016_24default__implPB6Logger28write__string__interpolationGRPB13StringBuilderE(_M0L7_2aselfS716, _M0L8_2aparamS717);
  return 0;
}

int32_t _M0IPB13StringBuilderPB6Logger67write__char_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLogger(
  void* _M0L11_2aobj__ptrS715,
  int32_t _M0L8_2aparamS714
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS713 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS715;
  _M0IPB13StringBuilderPB6Logger11write__char(_M0L7_2aselfS713, _M0L8_2aparamS714);
  return 0;
}

int32_t _M0IPB13StringBuilderPB6Logger67write__view_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLogger(
  void* _M0L11_2aobj__ptrS712,
  struct _M0TPC16string10StringView _M0L8_2aparamS711
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS710 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS712;
  _M0IPB13StringBuilderPB6Logger11write__view(_M0L7_2aselfS710, _M0L8_2aparamS711);
  return 0;
}

int32_t _M0IP016_24default__implPB6Logger72write__substring_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLoggerGRPB13StringBuilderE(
  void* _M0L11_2aobj__ptrS709,
  moonbit_string_t _M0L8_2aparamS706,
  int32_t _M0L8_2aparamS707,
  int32_t _M0L8_2aparamS708
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS705 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS709;
  _M0IP016_24default__implPB6Logger16write__substringGRPB13StringBuilderE(_M0L7_2aselfS705, _M0L8_2aparamS706, _M0L8_2aparamS707, _M0L8_2aparamS708);
  return 0;
}

int32_t _M0IPB13StringBuilderPB6Logger69write__string_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLogger(
  void* _M0L11_2aobj__ptrS704,
  moonbit_string_t _M0L8_2aparamS703
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS702 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS704;
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L7_2aselfS702, _M0L8_2aparamS703);
  return 0;
}

void moonbit_init() {
  moonbit_layout_table = moonbit_layout_table_data;
}

int main(int argc, char** argv) {
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS693;
  float _M0L2dtS694;
  struct _M0TP26RiantR8snn__mbt11IFParameter* _M0L9pv__paramS695;
  struct _M0TP26RiantR8snn__mbt2IF* _M0L7pv__popS696;
  float* _M0L6_2atmpS744;
  struct _M0TPB5ArrayGfE* _M0L6_2atmpS741;
  int32_t* _M0L6_2atmpS743;
  struct _M0TPB5ArrayGiE* _M0L6_2atmpS742;
  struct _M0TP26RiantR8snn__mbt17SpikeTimeStimulus* _M0L8pv__stimS697;
  float _M0L6_2atmpS740;
  int32_t _M0L8n__stepsS698;
  struct _M0TPB8MutLocalGiE* _M0L1sS699;
  struct _M0TPB5ArrayGfE* _M0L1vS731;
  float _M0L6_2atmpS730;
  moonbit_string_t _M0L6_2atmpS729;
  moonbit_string_t _M0L6_2atmpS728;
  moonbit_string_t _M0L6_2atmpS727;
  struct _M0TPB5ArrayGfE* _M0L3gluS735;
  float _M0L6_2atmpS734;
  moonbit_string_t _M0L6_2atmpS733;
  moonbit_string_t _M0L6_2atmpS732;
  struct _M0TPB5ArrayGbE* _M0L4fireS739;
  int32_t _M0L6_2acntS1632;
  int32_t _M0L6_2atmpS738;
  moonbit_string_t _M0L6_2atmpS737;
  moonbit_string_t _M0L6_2atmpS736;
  moonbit_runtime_init(argc, argv);
  moonbit_init();
  #line 16 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_demo\\main.mbt"
  _M0L3rngS693 = _M0MP26RiantR8snn__mbt7Xoshiro7default();
  _M0L2dtS694 = 0x1p-3f;
  #line 21 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_demo\\main.mbt"
  _M0L9pv__paramS695
  = _M0MP26RiantR8snn__mbt11IFParameter6custom(0x1.4p+4f, -0x1.ap+5f, -0x1.cbc28f5c28f5cp+5f, -0x1.fp+5f, 0x1.113404ea4a8c1p-4f);
  #line 28 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_demo\\main.mbt"
  _M0L7pv__popS696
  = _M0MP26RiantR8snn__mbt2IF3new(1, _M0L9pv__paramS695, _M0L3rngS693);
  moonbit_decref(_M0L9pv__paramS695);
  moonbit_decref(_M0L3rngS693);
  _M0L6_2atmpS744 = (float*)moonbit_make_float_array_raw(1);
  _M0L6_2atmpS744[0] = 0x1.f4p+9f;
  _M0L6_2atmpS741
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_M0L6_2atmpS741)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 18, 0);
  _M0L6_2atmpS741->$0 = _M0L6_2atmpS744;
  _M0L6_2atmpS741->$1 = 1;
  _M0L6_2atmpS743 = (int32_t*)moonbit_make_int32_array_raw(1);
  _M0L6_2atmpS743[0] = 0;
  _M0L6_2atmpS742
  = (struct _M0TPB5ArrayGiE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGiE));
  Moonbit_object_header(_M0L6_2atmpS742)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 21, 0);
  _M0L6_2atmpS742->$0 = _M0L6_2atmpS743;
  _M0L6_2atmpS742->$1 = 1;
  #line 31 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_demo\\main.mbt"
  _M0L8pv__stimS697
  = _M0MP26RiantR8snn__mbt17SpikeTimeStimulus3new(_M0L7pv__popS696, (moonbit_string_t)moonbit_string_literal_0.data, _M0L6_2atmpS741, _M0L6_2atmpS742);
  moonbit_decref(_M0L6_2atmpS741);
  moonbit_decref(_M0L6_2atmpS742);
  _M0L6_2atmpS740 = 0x1.2cp+10f / _M0L2dtS694;
  #line 37 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_demo\\main.mbt"
  _M0L8n__stepsS698 = _M0MPC15float5Float7to__int(_M0L6_2atmpS740);
  _M0L1sS699
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1sS699)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1sS699->$0 = 0;
  while (1) {
    int32_t _M0L3valS722 = _M0L1sS699->$0;
    if (_M0L3valS722 < _M0L8n__stepsS698) {
      int32_t _M0L3valS726 = _M0L1sS699->$0;
      float _M0L6_2atmpS725 = (float)_M0L3valS726;
      float _M0L1tS700 = _M0L6_2atmpS725 * _M0L2dtS694;
      int32_t _M0L3valS724;
      int32_t _M0L6_2atmpS723;
      #line 42 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_demo\\main.mbt"
      _M0FP26RiantR8snn__mbt20stimulate__spiketime(_M0L8pv__stimS697, _M0L1tS700, 0x1p+0f);
      #line 44 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_demo\\main.mbt"
      _M0FP26RiantR8snn__mbt14step__synapses(_M0L7pv__popS696, _M0L2dtS694);
      #line 45 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_demo\\main.mbt"
      _M0FP26RiantR8snn__mbt17synaptic__current(_M0L7pv__popS696);
      #line 46 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_demo\\main.mbt"
      _M0FP26RiantR8snn__mbt12step__neuron(_M0L7pv__popS696, _M0L2dtS694);
      _M0L3valS724 = _M0L1sS699->$0;
      _M0L6_2atmpS723 = _M0L3valS724 + 1;
      _M0L1sS699->$0 = _M0L6_2atmpS723;
      continue;
    } else {
      moonbit_decref(_M0L1sS699);
      moonbit_decref(_M0L8pv__stimS697);
    }
    break;
  }
  #line 50 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_demo\\main.mbt"
  _M0FPB7printlnGsE((moonbit_string_t)moonbit_string_literal_19.data);
  _M0L1vS731 = _M0L7pv__popS696->$3;
  moonbit_incref(_M0L1vS731);
  #line 51 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_demo\\main.mbt"
  _M0L6_2atmpS730 = _M0MPC15array5Array2atGfE(_M0L1vS731, 0);
  moonbit_decref(_M0L1vS731);
  #line 51 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_demo\\main.mbt"
  _M0L6_2atmpS729 = _M0IPC15float5FloatPB4Show10to__string(_M0L6_2atmpS730);
  #line 51 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_demo\\main.mbt"
  _M0L6_2atmpS728
  = moonbit_add_string((moonbit_string_t)moonbit_string_literal_20.data, _M0L6_2atmpS729);
  moonbit_decref(_M0L6_2atmpS729);
  #line 51 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_demo\\main.mbt"
  _M0L6_2atmpS727
  = moonbit_add_string(_M0L6_2atmpS728, (moonbit_string_t)moonbit_string_literal_21.data);
  moonbit_decref(_M0L6_2atmpS728);
  #line 51 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_demo\\main.mbt"
  _M0FPB7printlnGsE(_M0L6_2atmpS727);
  moonbit_decref(_M0L6_2atmpS727);
  _M0L3gluS735 = _M0L7pv__popS696->$13;
  moonbit_incref(_M0L3gluS735);
  #line 52 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_demo\\main.mbt"
  _M0L6_2atmpS734 = _M0MPC15array5Array2atGfE(_M0L3gluS735, 0);
  moonbit_decref(_M0L3gluS735);
  #line 52 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_demo\\main.mbt"
  _M0L6_2atmpS733 = _M0IPC15float5FloatPB4Show10to__string(_M0L6_2atmpS734);
  #line 52 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_demo\\main.mbt"
  _M0L6_2atmpS732
  = moonbit_add_string((moonbit_string_t)moonbit_string_literal_22.data, _M0L6_2atmpS733);
  moonbit_decref(_M0L6_2atmpS733);
  #line 52 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_demo\\main.mbt"
  _M0FPB7printlnGsE(_M0L6_2atmpS732);
  moonbit_decref(_M0L6_2atmpS732);
  _M0L4fireS739 = _M0L7pv__popS696->$5;
  _M0L6_2acntS1632
  = Moonbit_rc_count(Moonbit_object_header(_M0L7pv__popS696));
  if (_M0L6_2acntS1632 > 1) {
    int32_t _M0L11_2anew__cntS1648 = _M0L6_2acntS1632 - 1;
    Moonbit_set_rc_count(Moonbit_object_header(_M0L7pv__popS696), _M0L11_2anew__cntS1648);
    moonbit_incref(_M0L4fireS739);
  } else if (_M0L6_2acntS1632 == 1) {
    struct _M0TPB5ArrayGfE* _M0L8_2afieldS1647 = _M0L7pv__popS696->$16;
    struct _M0TPB5ArrayGfE* _M0L8_2afieldS1646;
    struct _M0TPB5ArrayGfE* _M0L8_2afieldS1645;
    struct _M0TPB5ArrayGfE* _M0L8_2afieldS1644;
    struct _M0TPB5ArrayGfE* _M0L8_2afieldS1643;
    struct _M0TPB5ArrayGfE* _M0L8_2afieldS1642;
    struct _M0TPB5ArrayGfE* _M0L8_2afieldS1641;
    struct _M0TPB5ArrayGfE* _M0L8_2afieldS1640;
    struct _M0TPB5ArrayGfE* _M0L8_2afieldS1639;
    struct _M0TPB5ArrayGfE* _M0L8_2afieldS1638;
    struct _M0TPB5ArrayGiE* _M0L8_2afieldS1637;
    struct _M0TPB5ArrayGfE* _M0L8_2afieldS1636;
    struct _M0TPB5ArrayGfE* _M0L8_2afieldS1635;
    struct _M0TP26RiantR8snn__mbt9PostSpike* _M0L8_2afieldS1634;
    struct _M0TP26RiantR8snn__mbt11IFParameter* _M0L8_2afieldS1633;
    moonbit_decref(_M0L8_2afieldS1647);
    _M0L8_2afieldS1646 = _M0L7pv__popS696->$15;
    moonbit_decref(_M0L8_2afieldS1646);
    _M0L8_2afieldS1645 = _M0L7pv__popS696->$14;
    moonbit_decref(_M0L8_2afieldS1645);
    _M0L8_2afieldS1644 = _M0L7pv__popS696->$13;
    moonbit_decref(_M0L8_2afieldS1644);
    _M0L8_2afieldS1643 = _M0L7pv__popS696->$12;
    moonbit_decref(_M0L8_2afieldS1643);
    _M0L8_2afieldS1642 = _M0L7pv__popS696->$11;
    moonbit_decref(_M0L8_2afieldS1642);
    _M0L8_2afieldS1641 = _M0L7pv__popS696->$10;
    moonbit_decref(_M0L8_2afieldS1641);
    _M0L8_2afieldS1640 = _M0L7pv__popS696->$9;
    moonbit_decref(_M0L8_2afieldS1640);
    _M0L8_2afieldS1639 = _M0L7pv__popS696->$8;
    moonbit_decref(_M0L8_2afieldS1639);
    _M0L8_2afieldS1638 = _M0L7pv__popS696->$7;
    moonbit_decref(_M0L8_2afieldS1638);
    _M0L8_2afieldS1637 = _M0L7pv__popS696->$6;
    moonbit_decref(_M0L8_2afieldS1637);
    _M0L8_2afieldS1636 = _M0L7pv__popS696->$4;
    moonbit_decref(_M0L8_2afieldS1636);
    _M0L8_2afieldS1635 = _M0L7pv__popS696->$3;
    moonbit_decref(_M0L8_2afieldS1635);
    _M0L8_2afieldS1634 = _M0L7pv__popS696->$1;
    moonbit_decref(_M0L8_2afieldS1634);
    _M0L8_2afieldS1633 = _M0L7pv__popS696->$0;
    moonbit_decref(_M0L8_2afieldS1633);
    #line 38 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_demo\\main.mbt"
    moonbit_free(_M0L7pv__popS696);
  }
  #line 53 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_demo\\main.mbt"
  _M0L6_2atmpS738 = _M0MPC15array5Array2atGbE(_M0L4fireS739, 0);
  moonbit_decref(_M0L4fireS739);
  #line 53 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_demo\\main.mbt"
  _M0L6_2atmpS737 = _M0IPC14bool4BoolPB4Show10to__string(_M0L6_2atmpS738);
  #line 53 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_demo\\main.mbt"
  _M0L6_2atmpS736
  = moonbit_add_string((moonbit_string_t)moonbit_string_literal_23.data, _M0L6_2atmpS737);
  moonbit_decref(_M0L6_2atmpS737);
  #line 53 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_demo\\main.mbt"
  _M0FPB7printlnGsE(_M0L6_2atmpS736);
  moonbit_decref(_M0L6_2atmpS736);
  return 0;
}