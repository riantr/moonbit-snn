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

struct _M0TP26RiantR8snn__mbt21NMDAVoltageDependency;

struct _M0TPB8MutLocalGiE;

struct _M0TP26RiantR8snn__mbt17ReceptorsByTarget;

struct _M0TPC16string10StringView;

struct _M0TPB5ArrayGRP26RiantR8snn__mbt8ReceptorE;

struct _M0KTPB6LoggerTPB13StringBuilder;

struct _M0TPB6Logger;

struct _M0TP26RiantR8snn__mbt9Receptors;

struct _M0TPB4Show;

struct _M0TP26RiantR8snn__mbt9GABAergic;

struct _M0TPB13StringBuilder;

struct _M0TPB5ArrayGfE;

struct _M0TPB17FloatingDecimal64;

struct _M0TPB5ArrayGiE;

struct _M0TPB19MulShiftAll64Result;

struct _M0TP26RiantR8snn__mbt13Glutamatergic;

struct _M0TP26RiantR8snn__mbt8Receptor;

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

struct _M0TP26RiantR8snn__mbt21NMDAVoltageDependency {
  float $0;
  float $1;
  float $2;
  
};

struct _M0TPB8MutLocalGiE {
  int32_t $0;
  
};

struct _M0TP26RiantR8snn__mbt17ReceptorsByTarget {
  struct _M0TPB5ArrayGiE* $0;
  struct _M0TPB5ArrayGiE* $1;
  
};

struct _M0TPC16string10StringView {
  moonbit_string_t $0;
  int32_t $1;
  int32_t $2;
  
};

struct _M0TPB5ArrayGRP26RiantR8snn__mbt8ReceptorE {
  struct _M0TP26RiantR8snn__mbt8Receptor** $0;
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

struct _M0TP26RiantR8snn__mbt9Receptors {
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt8ReceptorE* $0;
  
};

struct _M0TPB4Show {
  struct _M0BTPB4Show* $0;
  void* $1;
  
};

struct _M0TP26RiantR8snn__mbt9GABAergic {
  struct _M0TP26RiantR8snn__mbt8Receptor* $0;
  struct _M0TP26RiantR8snn__mbt8Receptor* $1;
  
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

struct _M0TPB5ArrayGiE {
  int32_t* $0;
  int32_t $1;
  
};

struct _M0TPB19MulShiftAll64Result {
  uint64_t $0;
  uint64_t $1;
  uint64_t $2;
  
};

struct _M0TP26RiantR8snn__mbt13Glutamatergic {
  struct _M0TP26RiantR8snn__mbt8Receptor* $0;
  struct _M0TP26RiantR8snn__mbt8Receptor* $1;
  
};

struct _M0TP26RiantR8snn__mbt8Receptor {
  float $0;
  float $1;
  float $2;
  float $3;
  float $4;
  float $5;
  float $6;
  float $7;
  int32_t $8;
  moonbit_string_t $9;
  
};

struct _M0TPB7Umul128 {
  uint64_t $0;
  uint64_t $1;
  
};

struct _M0TPB8Pow5Pair {
  uint64_t $0;
  uint64_t $1;
  
};

struct _M0TP26RiantR8snn__mbt17ReceptorsByTarget* _M0FP26RiantR8snn__mbt16infer__receptors(
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt8ReceptorE*
);

int32_t _M0FP26RiantR8snn__mbt18receptors__current(
  struct _M0TPB5ArrayGfE*,
  struct _M0TPB5ArrayGfE*,
  struct _M0TP26RiantR8snn__mbt9Receptors*,
  struct _M0TP26RiantR8snn__mbt21NMDAVoltageDependency*,
  struct _M0TPB5ArrayGfE*
);

int32_t _M0FP26RiantR8snn__mbt17receptor__current(
  struct _M0TPB5ArrayGfE*,
  struct _M0TPB5ArrayGfE*,
  struct _M0TP26RiantR8snn__mbt8Receptor*,
  struct _M0TP26RiantR8snn__mbt21NMDAVoltageDependency*,
  struct _M0TPB5ArrayGfE*
);

struct _M0TP26RiantR8snn__mbt9Receptors* _M0MP26RiantR8snn__mbt9Receptors10from__pair(
  struct _M0TP26RiantR8snn__mbt13Glutamatergic*,
  struct _M0TP26RiantR8snn__mbt9GABAergic*
);

struct _M0TP26RiantR8snn__mbt9GABAergic* _M0MP26RiantR8snn__mbt9GABAergic6custom(
  struct _M0TP26RiantR8snn__mbt8Receptor*,
  struct _M0TP26RiantR8snn__mbt8Receptor*
);

struct _M0TP26RiantR8snn__mbt13Glutamatergic* _M0MP26RiantR8snn__mbt13Glutamatergic6custom(
  struct _M0TP26RiantR8snn__mbt8Receptor*,
  struct _M0TP26RiantR8snn__mbt8Receptor*
);

float _M0FP26RiantR8snn__mbt14alpha__synapse(float, float);

struct _M0TP26RiantR8snn__mbt8Receptor* _M0MP26RiantR8snn__mbt8Receptor4nmda(
  float,
  float,
  float,
  float
);

struct _M0TP26RiantR8snn__mbt8Receptor* _M0MP26RiantR8snn__mbt8Receptor6simple(
  float,
  float,
  float,
  float,
  moonbit_string_t
);

float _M0FP26RiantR8snn__mbt13norm__synapse(float, float);

float _M0FP26RiantR8snn__mbt12nmda__gating(
  float,
  struct _M0TP26RiantR8snn__mbt21NMDAVoltageDependency*
);

struct _M0TP26RiantR8snn__mbt21NMDAVoltageDependency* _M0MP26RiantR8snn__mbt21NMDAVoltageDependency4eyal(
  
);

#define _M0FP26RiantR8snn__mbt4logf logf

#define _M0FP26RiantR8snn__mbt4expf expf

moonbit_string_t _M0IPC15float5FloatPB4Show10to__string(float);

struct _M0TPB5ArrayGfE* _M0MPC15array5Array4makeGfE(int32_t, float);

int32_t _M0MPC15array5Array3setGfE(struct _M0TPB5ArrayGfE*, int32_t, float);

int32_t _M0MPC15array5Array2atGiE(struct _M0TPB5ArrayGiE*, int32_t);

float _M0MPC15array5Array2atGfE(struct _M0TPB5ArrayGfE*, int32_t);

struct _M0TP26RiantR8snn__mbt8Receptor* _M0MPC15array5Array2atGRP26RiantR8snn__mbt8ReceptorE(
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt8ReceptorE*,
  int32_t
);

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

uint64_t _M0MPC15array13ReadOnlyArray2atGmE(uint64_t*, int32_t);

uint32_t _M0MPC15array13ReadOnlyArray2atGjE(uint32_t*, int32_t);

moonbit_string_t _M0IPC16uint646UInt64PB4Show10to__string(uint64_t);

moonbit_string_t _M0IPC13int3IntPB4Show10to__string(int32_t);

moonbit_string_t _M0IPC14bool4BoolPB4Show10to__string(int32_t);

uint64_t _M0MPC14uint4UInt10to__uint64(uint32_t);

int32_t _M0MPC15array5Array4pushGiE(struct _M0TPB5ArrayGiE*, int32_t);

int32_t _M0MPC15array5Array7reallocGiE(struct _M0TPB5ArrayGiE*, int32_t);

int32_t _M0MPC15array5Array14resize__bufferGiE(
  struct _M0TPB5ArrayGiE*,
  int32_t
);

int32_t _M0MPC15array5Array8capacityGiE(struct _M0TPB5ArrayGiE*);

int32_t _M0FPB23array__growth__capacity(int32_t, int32_t, int32_t);

int32_t _M0MPC15array5Array6lengthGiE(struct _M0TPB5ArrayGiE*);

int32_t _M0MPC15array5Array6lengthGRP26RiantR8snn__mbt8ReceptorE(
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt8ReceptorE*
);

int32_t _M0MPC15array5Array6lengthGfE(struct _M0TPB5ArrayGfE*);

int32_t* _M0MPC15array5Array6bufferGiE(struct _M0TPB5ArrayGiE*);

float* _M0MPC15array5Array6bufferGfE(struct _M0TPB5ArrayGfE*);

struct _M0TP26RiantR8snn__mbt8Receptor** _M0MPC15array5Array6bufferGRP26RiantR8snn__mbt8ReceptorE(
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt8ReceptorE*
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

int32_t _M0MPB18UninitializedArray12unsafe__blitGiE(
  int32_t*,
  int32_t,
  int32_t*,
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

int32_t _M0MPB18UninitializedArray6lengthGiE(int32_t*);

int32_t _M0FPC15abort5abortGuE(moonbit_string_t);

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

float logf(float);

float expf(float);

struct { int32_t rc; uint32_t meta; uint16_t const data[83]; 
} const moonbit_string_literal_39 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 82, 114, 101, 
    99, 101, 112, 116, 111, 114, 115, 95, 100, 101, 109, 111, 46, 109, 
    98, 116, 58, 32, 52, 45, 114, 101, 99, 101, 112, 116, 111, 114, 32, 
    99, 117, 114, 114, 101, 110, 116, 32, 115, 117, 109, 32, 112, 114, 
    105, 110, 116, 101, 100, 32, 40, 65, 77, 80, 65, 32, 43, 32, 71, 
    65, 66, 65, 97, 59, 32, 78, 77, 68, 65, 61, 48, 59, 32, 71, 65, 66, 
    65, 98, 61, 48, 41, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[43]; 
} const moonbit_string_literal_23 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 42, 32, 32, 
    78, 77, 68, 65, 32, 32, 58, 32, 101, 95, 114, 101, 118, 61, 48, 32, 
    32, 32, 964, 114, 61, 49, 32, 32, 964, 100, 61, 49, 48, 48, 32, 32, 
    105, 115, 95, 110, 109, 100, 97, 61, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[1]; 
} const moonbit_string_literal_6 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 0, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[11]; 
} const moonbit_string_literal_30 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 10, 32, 32, 
    103, 108, 117, 61, 91, 48, 93, 61, 0
  };

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
} const moonbit_string_literal_5 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 1, 45, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[7]; 
} const moonbit_string_literal_24 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 6, 32, 103, 
    115, 121, 110, 61, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[28]; 
} const moonbit_string_literal_34 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 27, 114, 101, 
    99, 101, 112, 116, 111, 114, 115, 95, 100, 101, 109, 111, 46, 109, 
    98, 116, 58, 32, 65, 77, 80, 65, 32, 945, 61, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[31]; 
} const moonbit_string_literal_12 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 30, 114, 97, 
    100, 105, 120, 32, 109, 117, 115, 116, 32, 98, 101, 32, 98, 101, 
    116, 119, 101, 101, 110, 32, 50, 32, 97, 110, 100, 32, 51, 54, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[9]; 
} const moonbit_string_literal_7 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 8, 73, 110, 
    102, 105, 110, 105, 116, 121, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[4]; 
} const moonbit_string_literal_4 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 3, 78, 97, 78, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[10]; 
} const moonbit_string_literal_38 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 9, 32, 8594, 
    32, 73, 95, 115, 121, 110, 61, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[2]; 
} const moonbit_string_literal_2 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 1, 48, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[11]; 
} const moonbit_string_literal_33 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 10, 32, 103, 
    97, 98, 97, 61, 91, 49, 93, 61, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[10]; 
} const moonbit_string_literal_31 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 9, 32, 103, 
    108, 117, 61, 91, 49, 93, 61, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[9]; 
} const moonbit_string_literal_19 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 8, 44, 32, 
    108, 101, 110, 32, 61, 32, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[6]; 
} const moonbit_string_literal_10 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 5, 102, 97, 
    108, 115, 101, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[4]; 
} const moonbit_string_literal_37 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 3, 32, 118, 61, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[37]; 
} const moonbit_string_literal_16 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 36, 98, 111, 
    117, 110, 100, 115, 32, 99, 104, 101, 99, 107, 32, 102, 97, 105, 
    108, 101, 100, 58, 32, 97, 108, 108, 111, 99, 97, 116, 101, 95, 108, 
    101, 110, 32, 61, 32, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[7]; 
} const moonbit_string_literal_35 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 6, 32, 110, 
    111, 114, 109, 61, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[39]; 
} const moonbit_string_literal_26 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 38, 32, 32, 
    71, 65, 66, 65, 98, 32, 58, 32, 101, 95, 114, 101, 118, 61, 45, 55, 
    53, 32, 964, 114, 61, 48, 46, 53, 32, 964, 100, 61, 50, 48, 32, 103, 
    115, 121, 110, 61, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[5]; 
} const moonbit_string_literal_1 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 4, 103, 97, 
    98, 97, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[2]; 
} const moonbit_string_literal_29 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 1, 93, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[5]; 
} const moonbit_string_literal_9 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 4, 116, 114, 
    117, 101, 0
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
} const moonbit_string_literal_20 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 15, 44, 32, 
    115, 114, 99, 46, 108, 101, 110, 103, 116, 104, 32, 61, 32, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[4]; 
} const moonbit_string_literal_0 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 3, 103, 108, 117, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[11]; 
} const moonbit_string_literal_32 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 10, 32, 103, 
    97, 98, 97, 61, 91, 48, 93, 61, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[39]; 
} const moonbit_string_literal_25 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 38, 32, 32, 
    71, 65, 66, 65, 97, 32, 58, 32, 101, 95, 114, 101, 118, 61, 45, 55, 
    53, 32, 964, 114, 61, 48, 46, 53, 32, 964, 100, 61, 50, 32, 32, 103, 
    115, 121, 110, 61, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[16]; 
} const moonbit_string_literal_17 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 15, 44, 32, 
    115, 114, 99, 95, 111, 102, 102, 115, 101, 116, 32, 61, 32, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[44]; 
} const moonbit_string_literal_27 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 43, 114, 101, 
    99, 101, 112, 116, 111, 114, 115, 95, 100, 101, 109, 111, 46, 109, 
    98, 116, 58, 32, 105, 110, 102, 101, 114, 95, 114, 101, 99, 101, 
    112, 116, 111, 114, 115, 32, 8594, 32, 103, 108, 117, 61, 91, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[9]; 
} const moonbit_string_literal_28 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 8, 93, 32, 
    103, 97, 98, 97, 61, 91, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[48]; 
} const moonbit_string_literal_21 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 47, 114, 101, 
    99, 101, 112, 116, 111, 114, 115, 95, 100, 101, 109, 111, 46, 109, 
    98, 116, 58, 32, 52, 45, 114, 101, 99, 101, 112, 116, 111, 114, 32, 
    99, 111, 108, 108, 101, 99, 116, 105, 111, 110, 32, 98, 117, 105, 
    108, 116, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[24]; 
} const moonbit_string_literal_11 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 23, 65, 114, 
    114, 97, 121, 32, 99, 97, 112, 97, 99, 105, 116, 121, 32, 111, 118, 
    101, 114, 102, 108, 111, 119, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[26]; 
} const moonbit_string_literal_3 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 25, 73, 108, 
    108, 101, 103, 97, 108, 65, 114, 103, 117, 109, 101, 110, 116, 69, 
    120, 99, 101, 112, 116, 105, 111, 110, 32, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[40]; 
} const moonbit_string_literal_22 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 39, 32, 32, 
    65, 77, 80, 65, 32, 32, 58, 32, 101, 95, 114, 101, 118, 61, 48, 32, 
    32, 32, 964, 114, 61, 49, 32, 32, 964, 100, 61, 54, 32, 32, 32, 32, 
    103, 115, 121, 110, 61, 0
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
} const moonbit_string_literal_8 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 3, 48, 46, 48, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[10]; 
} const moonbit_string_literal_36 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 9, 32, 32, 
    110, 101, 117, 114, 111, 110, 32, 0
  };

uint32_t const moonbit_layout_table_data[30] =
  {
    sizeof(struct _M0TPB5ArrayGiE) / 4, 1,
    offsetof(struct _M0TPB5ArrayGiE, $0) / 4 * 2,
    sizeof(struct _M0TP26RiantR8snn__mbt17ReceptorsByTarget) / 4, 2,
    offsetof(struct _M0TP26RiantR8snn__mbt17ReceptorsByTarget, $0) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt17ReceptorsByTarget, $1) / 4 * 2,
    sizeof(struct _M0TPB5ArrayGRP26RiantR8snn__mbt8ReceptorE) / 4, 1,
    offsetof(struct _M0TPB5ArrayGRP26RiantR8snn__mbt8ReceptorE, $0) / 4 * 2,
    sizeof(struct _M0TP26RiantR8snn__mbt9Receptors) / 4, 1,
    offsetof(struct _M0TP26RiantR8snn__mbt9Receptors, $0) / 4 * 2,
    sizeof(struct _M0TP26RiantR8snn__mbt9GABAergic) / 4, 2,
    offsetof(struct _M0TP26RiantR8snn__mbt9GABAergic, $0) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt9GABAergic, $1) / 4 * 2,
    sizeof(struct _M0TP26RiantR8snn__mbt13Glutamatergic) / 4, 2,
    offsetof(struct _M0TP26RiantR8snn__mbt13Glutamatergic, $0) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt13Glutamatergic, $1) / 4 * 2,
    sizeof(struct _M0TP26RiantR8snn__mbt8Receptor) / 4, 1,
    offsetof(struct _M0TP26RiantR8snn__mbt8Receptor, $9) / 4 * 2,
    sizeof(struct _M0TPB5ArrayGfE) / 4, 1,
    offsetof(struct _M0TPB5ArrayGfE, $0) / 4 * 2,
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

struct _M0TP26RiantR8snn__mbt17ReceptorsByTarget* _M0FP26RiantR8snn__mbt16infer__receptors(
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt8ReceptorE* _M0L9receptorsS631
) {
  int32_t* _M0L6_2atmpS1477;
  struct _M0TPB5ArrayGiE* _M0L3gluS628;
  int32_t* _M0L6_2atmpS1476;
  struct _M0TPB5ArrayGiE* _M0L4gabaS629;
  struct _M0TPB8MutLocalGiE* _M0L1iS630;
  struct _M0TP26RiantR8snn__mbt17ReceptorsByTarget* _block_1494;
  #line 17 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\infer_receptors.mbt"
  _M0L6_2atmpS1477 = (int32_t*)moonbit_empty_int32_array;
  _M0L3gluS628
  = (struct _M0TPB5ArrayGiE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGiE));
  Moonbit_object_header(_M0L3gluS628)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 0, 0);
  _M0L3gluS628->$0 = _M0L6_2atmpS1477;
  _M0L3gluS628->$1 = 0;
  _M0L6_2atmpS1476 = (int32_t*)moonbit_empty_int32_array;
  _M0L4gabaS629
  = (struct _M0TPB5ArrayGiE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGiE));
  Moonbit_object_header(_M0L4gabaS629)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 0, 0);
  _M0L4gabaS629->$0 = _M0L6_2atmpS1476;
  _M0L4gabaS629->$1 = 0;
  _M0L1iS630
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1iS630)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1iS630->$0 = 0;
  while (1) {
    int32_t _M0L3valS1465 = _M0L1iS630->$0;
    int32_t _M0L6_2atmpS1466;
    #line 21 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\infer_receptors.mbt"
    _M0L6_2atmpS1466
    = _M0MPC15array5Array6lengthGRP26RiantR8snn__mbt8ReceptorE(_M0L9receptorsS631);
    if (_M0L3valS1465 < _M0L6_2atmpS1466) {
      int32_t _M0L3valS1475 = _M0L1iS630->$0;
      struct _M0TP26RiantR8snn__mbt8Receptor* _M0L1rS632;
      moonbit_string_t _M0L6targetS1467;
      int32_t _M0L3valS1474;
      int32_t _M0L6_2atmpS1473;
      #line 22 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\infer_receptors.mbt"
      _M0L1rS632
      = _M0MPC15array5Array2atGRP26RiantR8snn__mbt8ReceptorE(_M0L9receptorsS631, _M0L3valS1475);
      _M0L6targetS1467 = _M0L1rS632->$9;
      #line 23 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\infer_receptors.mbt"
      if (
        _M0L6targetS1467 == (moonbit_string_t)moonbit_string_literal_0.data
        || Moonbit_array_length(_M0L6targetS1467)
           == Moonbit_array_length((moonbit_string_t)moonbit_string_literal_0.data)
           && 0
              == memcmp(_M0L6targetS1467, (moonbit_string_t)moonbit_string_literal_0.data, Moonbit_array_length(_M0L6targetS1467) * 2)
      ) {
        int32_t _M0L3valS1469;
        int32_t _M0L6_2atmpS1468;
        moonbit_decref(_M0L1rS632);
        _M0L3valS1469 = _M0L1iS630->$0;
        #line 24 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\infer_receptors.mbt"
        _M0L6_2atmpS1468
        = _M0MPC15array5Array4pushGiE(_M0L3gluS628, _M0L3valS1469);
      } else {
        moonbit_string_t _M0L6targetS1470 = _M0L1rS632->$9;
        int32_t _M0L6_2acntS1485 =
          Moonbit_rc_count(Moonbit_object_header(_M0L1rS632));
        int32_t _result_1493;
        if (_M0L6_2acntS1485 > 1) {
          int32_t _M0L11_2anew__cntS1486 = _M0L6_2acntS1485 - 1;
          Moonbit_set_rc_count(Moonbit_object_header(_M0L1rS632), _M0L11_2anew__cntS1486);
          moonbit_incref(_M0L6targetS1470);
        } else if (_M0L6_2acntS1485 == 1) {
          #line 25 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\infer_receptors.mbt"
          moonbit_free(_M0L1rS632);
        }
        #line 25 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\infer_receptors.mbt"
        _result_1493
        = _M0L6targetS1470 == (moonbit_string_t)moonbit_string_literal_1.data
          || Moonbit_array_length(_M0L6targetS1470)
             == Moonbit_array_length((moonbit_string_t)moonbit_string_literal_1.data)
             && 0
                == memcmp(_M0L6targetS1470, (moonbit_string_t)moonbit_string_literal_1.data, Moonbit_array_length(_M0L6targetS1470) * 2);
        moonbit_decref(_M0L6targetS1470);
        if (_result_1493) {
          int32_t _M0L3valS1472 = _M0L1iS630->$0;
          int32_t _M0L6_2atmpS1471;
          #line 26 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\infer_receptors.mbt"
          _M0L6_2atmpS1471
          = _M0MPC15array5Array4pushGiE(_M0L4gabaS629, _M0L3valS1472);
        }
      }
      _M0L3valS1474 = _M0L1iS630->$0;
      _M0L6_2atmpS1473 = _M0L3valS1474 + 1;
      _M0L1iS630->$0 = _M0L6_2atmpS1473;
      continue;
    } else {
      moonbit_decref(_M0L1iS630);
    }
    break;
  }
  _block_1494
  = (struct _M0TP26RiantR8snn__mbt17ReceptorsByTarget*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt17ReceptorsByTarget));
  Moonbit_object_header(_block_1494)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 3, 0);
  _block_1494->$0 = _M0L3gluS628;
  _block_1494->$1 = _M0L4gabaS629;
  return _block_1494;
}

int32_t _M0FP26RiantR8snn__mbt18receptors__current(
  struct _M0TPB5ArrayGfE* _M0L9g__matrixS623,
  struct _M0TPB5ArrayGfE* _M0L1vS615,
  struct _M0TP26RiantR8snn__mbt9Receptors* _M0L2rsS617,
  struct _M0TP26RiantR8snn__mbt21NMDAVoltageDependency* _M0L9nmda__depS625,
  struct _M0TPB5ArrayGfE* _M0L3outS626
) {
  int32_t _M0L1nS614;
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt8ReceptorE* _M0L3recS1464;
  int32_t _M0L6n__recS616;
  int32_t _M0L7_2abindS618;
  int32_t _M0L1kS619;
  #line 266 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\receptor.mbt"
  #line 273 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\receptor.mbt"
  _M0L1nS614 = _M0MPC15array5Array6lengthGfE(_M0L1vS615);
  _M0L3recS1464 = _M0L2rsS617->$0;
  #line 274 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\receptor.mbt"
  _M0L6n__recS616
  = _M0MPC15array5Array6lengthGRP26RiantR8snn__mbt8ReceptorE(_M0L3recS1464);
  _M0L7_2abindS618 = 0;
  _M0L1kS619 = _M0L7_2abindS618;
  while (1) {
    if (_M0L1kS619 < _M0L6n__recS616) {
      struct _M0TPB5ArrayGfE* _M0L6g__colS620;
      int32_t _M0L7_2abindS621;
      int32_t _M0L1iS622;
      struct _M0TPB5ArrayGRP26RiantR8snn__mbt8ReceptorE* _M0L3recS1462;
      struct _M0TP26RiantR8snn__mbt8Receptor* _M0L6_2atmpS1461;
      int32_t _M0L6_2atmpS1463;
      #line 277 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\receptor.mbt"
      _M0L6g__colS620 = _M0MPC15array5Array4makeGfE(_M0L1nS614, 0x0p+0f);
      _M0L7_2abindS621 = 0;
      _M0L1iS622 = _M0L7_2abindS621;
      while (1) {
        if (_M0L1iS622 < _M0L1nS614) {
          int32_t _M0L6_2atmpS1459 = _M0L1iS622 * _M0L6n__recS616;
          int32_t _M0L6_2atmpS1458 = _M0L6_2atmpS1459 + _M0L1kS619;
          float _M0L6_2atmpS1457;
          int32_t _M0L6_2atmpS1460;
          #line 279 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\receptor.mbt"
          _M0L6_2atmpS1457
          = _M0MPC15array5Array2atGfE(_M0L9g__matrixS623, _M0L6_2atmpS1458);
          #line 279 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\receptor.mbt"
          _M0MPC15array5Array3setGfE(_M0L6g__colS620, _M0L1iS622, _M0L6_2atmpS1457);
          _M0L6_2atmpS1460 = _M0L1iS622 + 1;
          _M0L1iS622 = _M0L6_2atmpS1460;
          continue;
        }
        break;
      }
      _M0L3recS1462 = _M0L2rsS617->$0;
      #line 281 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\receptor.mbt"
      _M0L6_2atmpS1461
      = _M0MPC15array5Array2atGRP26RiantR8snn__mbt8ReceptorE(_M0L3recS1462, _M0L1kS619);
      #line 281 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\receptor.mbt"
      _M0FP26RiantR8snn__mbt17receptor__current(_M0L6g__colS620, _M0L1vS615, _M0L6_2atmpS1461, _M0L9nmda__depS625, _M0L3outS626);
      moonbit_decref(_M0L6g__colS620);
      moonbit_decref(_M0L6_2atmpS1461);
      _M0L6_2atmpS1463 = _M0L1kS619 + 1;
      _M0L1kS619 = _M0L6_2atmpS1463;
      continue;
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt17receptor__current(
  struct _M0TPB5ArrayGfE* _M0L1gS600,
  struct _M0TPB5ArrayGfE* _M0L1vS607,
  struct _M0TP26RiantR8snn__mbt8Receptor* _M0L1rS602,
  struct _M0TP26RiantR8snn__mbt21NMDAVoltageDependency* _M0L9nmda__depS608,
  struct _M0TPB5ArrayGfE* _M0L3outS609
) {
  int32_t _M0L1nS599;
  float _M0L4gsynS601;
  float _M0L6e__revS603;
  #line 241 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\receptor.mbt"
  #line 248 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\receptor.mbt"
  _M0L1nS599 = _M0MPC15array5Array6lengthGfE(_M0L1gS600);
  _M0L4gsynS601 = _M0L1rS602->$4;
  _M0L6e__revS603 = _M0L1rS602->$0;
  if (_M0L1rS602->$8) {
    int32_t _M0L7_2abindS604 = 0;
    int32_t _M0L1iS605 = _M0L7_2abindS604;
    while (1) {
      if (_M0L1iS605 < _M0L1nS599) {
        float _M0L6_2atmpS1447;
        float _M0L1bS606;
        float _M0L6_2atmpS1440;
        float _M0L6_2atmpS1446;
        float _M0L6_2atmpS1443;
        float _M0L6_2atmpS1445;
        float _M0L6_2atmpS1444;
        float _M0L6_2atmpS1442;
        float _M0L6_2atmpS1441;
        float _M0L6_2atmpS1439;
        int32_t _M0L6_2atmpS1448;
        #line 253 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\receptor.mbt"
        _M0L6_2atmpS1447 = _M0MPC15array5Array2atGfE(_M0L1vS607, _M0L1iS605);
        #line 253 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\receptor.mbt"
        _M0L1bS606
        = _M0FP26RiantR8snn__mbt12nmda__gating(_M0L6_2atmpS1447, _M0L9nmda__depS608);
        #line 254 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\receptor.mbt"
        _M0L6_2atmpS1440
        = _M0MPC15array5Array2atGfE(_M0L3outS609, _M0L1iS605);
        #line 254 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\receptor.mbt"
        _M0L6_2atmpS1446 = _M0MPC15array5Array2atGfE(_M0L1gS600, _M0L1iS605);
        _M0L6_2atmpS1443 = _M0L4gsynS601 * _M0L6_2atmpS1446;
        #line 254 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\receptor.mbt"
        _M0L6_2atmpS1445 = _M0MPC15array5Array2atGfE(_M0L1vS607, _M0L1iS605);
        _M0L6_2atmpS1444 = _M0L6_2atmpS1445 - _M0L6e__revS603;
        _M0L6_2atmpS1442 = _M0L6_2atmpS1443 * _M0L6_2atmpS1444;
        _M0L6_2atmpS1441 = _M0L6_2atmpS1442 * _M0L1bS606;
        _M0L6_2atmpS1439 = _M0L6_2atmpS1440 + _M0L6_2atmpS1441;
        #line 254 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\receptor.mbt"
        _M0MPC15array5Array3setGfE(_M0L3outS609, _M0L1iS605, _M0L6_2atmpS1439);
        _M0L6_2atmpS1448 = _M0L1iS605 + 1;
        _M0L1iS605 = _M0L6_2atmpS1448;
        continue;
      }
      break;
    }
  } else {
    int32_t _M0L7_2abindS611 = 0;
    int32_t _M0L1iS612 = _M0L7_2abindS611;
    while (1) {
      if (_M0L1iS612 < _M0L1nS599) {
        float _M0L6_2atmpS1450;
        float _M0L6_2atmpS1455;
        float _M0L6_2atmpS1452;
        float _M0L6_2atmpS1454;
        float _M0L6_2atmpS1453;
        float _M0L6_2atmpS1451;
        float _M0L6_2atmpS1449;
        int32_t _M0L6_2atmpS1456;
        #line 258 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\receptor.mbt"
        _M0L6_2atmpS1450
        = _M0MPC15array5Array2atGfE(_M0L3outS609, _M0L1iS612);
        #line 258 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\receptor.mbt"
        _M0L6_2atmpS1455 = _M0MPC15array5Array2atGfE(_M0L1gS600, _M0L1iS612);
        _M0L6_2atmpS1452 = _M0L4gsynS601 * _M0L6_2atmpS1455;
        #line 258 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\receptor.mbt"
        _M0L6_2atmpS1454 = _M0MPC15array5Array2atGfE(_M0L1vS607, _M0L1iS612);
        _M0L6_2atmpS1453 = _M0L6_2atmpS1454 - _M0L6e__revS603;
        _M0L6_2atmpS1451 = _M0L6_2atmpS1452 * _M0L6_2atmpS1453;
        _M0L6_2atmpS1449 = _M0L6_2atmpS1450 + _M0L6_2atmpS1451;
        #line 258 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\receptor.mbt"
        _M0MPC15array5Array3setGfE(_M0L3outS609, _M0L1iS612, _M0L6_2atmpS1449);
        _M0L6_2atmpS1456 = _M0L1iS612 + 1;
        _M0L1iS612 = _M0L6_2atmpS1456;
        continue;
      }
      break;
    }
  }
  return 0;
}

struct _M0TP26RiantR8snn__mbt9Receptors* _M0MP26RiantR8snn__mbt9Receptors10from__pair(
  struct _M0TP26RiantR8snn__mbt13Glutamatergic* _M0L3gluS597,
  struct _M0TP26RiantR8snn__mbt9GABAergic* _M0L4gabaS598
) {
  struct _M0TP26RiantR8snn__mbt8Receptor* _M0L4ampaS1435;
  struct _M0TP26RiantR8snn__mbt8Receptor* _M0L4nmdaS1436;
  struct _M0TP26RiantR8snn__mbt8Receptor* _M0L5gabaaS1437;
  struct _M0TP26RiantR8snn__mbt8Receptor* _M0L5gababS1438;
  struct _M0TP26RiantR8snn__mbt8Receptor** _M0L6_2atmpS1434;
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt8ReceptorE* _M0L6_2atmpS1433;
  struct _M0TP26RiantR8snn__mbt9Receptors* _block_1499;
  #line 208 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\receptor.mbt"
  _M0L4ampaS1435 = _M0L3gluS597->$0;
  _M0L4nmdaS1436 = _M0L3gluS597->$1;
  _M0L5gabaaS1437 = _M0L4gabaS598->$0;
  _M0L5gababS1438 = _M0L4gabaS598->$1;
  moonbit_incref(_M0L4ampaS1435);
  moonbit_incref(_M0L4nmdaS1436);
  moonbit_incref(_M0L5gabaaS1437);
  moonbit_incref(_M0L5gababS1438);
  _M0L6_2atmpS1434
  = (struct _M0TP26RiantR8snn__mbt8Receptor**)moonbit_make_ref_array_raw(4);
  _M0L6_2atmpS1434[0] = _M0L4ampaS1435;
  _M0L6_2atmpS1434[1] = _M0L4nmdaS1436;
  _M0L6_2atmpS1434[2] = _M0L5gabaaS1437;
  _M0L6_2atmpS1434[3] = _M0L5gababS1438;
  _M0L6_2atmpS1433
  = (struct _M0TPB5ArrayGRP26RiantR8snn__mbt8ReceptorE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGRP26RiantR8snn__mbt8ReceptorE));
  Moonbit_object_header(_M0L6_2atmpS1433)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 7, 0);
  _M0L6_2atmpS1433->$0 = _M0L6_2atmpS1434;
  _M0L6_2atmpS1433->$1 = 4;
  _block_1499
  = (struct _M0TP26RiantR8snn__mbt9Receptors*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt9Receptors));
  Moonbit_object_header(_block_1499)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 10, 0);
  _block_1499->$0 = _M0L6_2atmpS1433;
  return _block_1499;
}

struct _M0TP26RiantR8snn__mbt9GABAergic* _M0MP26RiantR8snn__mbt9GABAergic6custom(
  struct _M0TP26RiantR8snn__mbt8Receptor* _M0L5gabaaS595,
  struct _M0TP26RiantR8snn__mbt8Receptor* _M0L5gababS596
) {
  struct _M0TP26RiantR8snn__mbt9GABAergic* _block_1500;
  #line 182 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\receptor.mbt"
  moonbit_incref(_M0L5gabaaS595);
  moonbit_incref(_M0L5gababS596);
  _block_1500
  = (struct _M0TP26RiantR8snn__mbt9GABAergic*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt9GABAergic));
  Moonbit_object_header(_block_1500)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 13, 0);
  _block_1500->$0 = _M0L5gabaaS595;
  _block_1500->$1 = _M0L5gababS596;
  return _block_1500;
}

struct _M0TP26RiantR8snn__mbt13Glutamatergic* _M0MP26RiantR8snn__mbt13Glutamatergic6custom(
  struct _M0TP26RiantR8snn__mbt8Receptor* _M0L4ampaS593,
  struct _M0TP26RiantR8snn__mbt8Receptor* _M0L4nmdaS594
) {
  struct _M0TP26RiantR8snn__mbt13Glutamatergic* _block_1501;
  #line 160 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\receptor.mbt"
  moonbit_incref(_M0L4ampaS593);
  moonbit_incref(_M0L4nmdaS594);
  _block_1501
  = (struct _M0TP26RiantR8snn__mbt13Glutamatergic*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt13Glutamatergic));
  Moonbit_object_header(_block_1501)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 17, 0);
  _block_1501->$0 = _M0L4ampaS593;
  _block_1501->$1 = _M0L4nmdaS594;
  return _block_1501;
}

float _M0FP26RiantR8snn__mbt14alpha__synapse(
  float _M0L6tau__rS592,
  float _M0L6tau__dS591
) {
  float _M0L6_2atmpS1431;
  float _M0L6_2atmpS1432;
  #line 138 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\receptor.mbt"
  _M0L6_2atmpS1431 = _M0L6tau__dS591 - _M0L6tau__rS592;
  _M0L6_2atmpS1432 = _M0L6tau__dS591 * _M0L6tau__rS592;
  return _M0L6_2atmpS1431 / _M0L6_2atmpS1432;
}

struct _M0TP26RiantR8snn__mbt8Receptor* _M0MP26RiantR8snn__mbt8Receptor4nmda(
  float _M0L6e__revS587,
  float _M0L6tau__rS588,
  float _M0L6tau__dS589,
  float _M0L2g0S590
) {
  struct _M0TP26RiantR8snn__mbt8Receptor* _M0L1rS586;
  float _M0L11_2afield__0S1422;
  float _M0L11_2afield__1S1423;
  float _M0L11_2afield__2S1424;
  float _M0L11_2afield__3S1425;
  float _M0L11_2afield__4S1426;
  float _M0L11_2afield__5S1427;
  float _M0L11_2afield__6S1428;
  float _M0L11_2afield__7S1429;
  moonbit_string_t _M0L11_2afield__9S1430;
  int32_t _M0L6_2acntS1487;
  struct _M0TP26RiantR8snn__mbt8Receptor* _block_1502;
  #line 116 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\receptor.mbt"
  #line 122 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\receptor.mbt"
  _M0L1rS586
  = _M0MP26RiantR8snn__mbt8Receptor6simple(_M0L6e__revS587, _M0L6tau__rS588, _M0L6tau__dS589, _M0L2g0S590, (moonbit_string_t)moonbit_string_literal_0.data);
  _M0L11_2afield__0S1422 = _M0L1rS586->$0;
  _M0L11_2afield__1S1423 = _M0L1rS586->$1;
  _M0L11_2afield__2S1424 = _M0L1rS586->$2;
  _M0L11_2afield__3S1425 = _M0L1rS586->$3;
  _M0L11_2afield__4S1426 = _M0L1rS586->$4;
  _M0L11_2afield__5S1427 = _M0L1rS586->$5;
  _M0L11_2afield__6S1428 = _M0L1rS586->$6;
  _M0L11_2afield__7S1429 = _M0L1rS586->$7;
  _M0L11_2afield__9S1430 = _M0L1rS586->$9;
  _M0L6_2acntS1487 = Moonbit_rc_count(Moonbit_object_header(_M0L1rS586));
  if (_M0L6_2acntS1487 > 1) {
    int32_t _M0L11_2anew__cntS1488 = _M0L6_2acntS1487 - 1;
    Moonbit_set_rc_count(Moonbit_object_header(_M0L1rS586), _M0L11_2anew__cntS1488);
    moonbit_incref(_M0L11_2afield__9S1430);
  } else if (_M0L6_2acntS1487 == 1) {
    #line 122 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\receptor.mbt"
    moonbit_free(_M0L1rS586);
  }
  _block_1502
  = (struct _M0TP26RiantR8snn__mbt8Receptor*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt8Receptor));
  Moonbit_object_header(_block_1502)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 21, 0);
  _block_1502->$0 = _M0L11_2afield__0S1422;
  _block_1502->$1 = _M0L11_2afield__1S1423;
  _block_1502->$2 = _M0L11_2afield__2S1424;
  _block_1502->$3 = _M0L11_2afield__3S1425;
  _block_1502->$4 = _M0L11_2afield__4S1426;
  _block_1502->$5 = _M0L11_2afield__5S1427;
  _block_1502->$6 = _M0L11_2afield__6S1428;
  _block_1502->$7 = _M0L11_2afield__7S1429;
  _block_1502->$8 = 1;
  _block_1502->$9 = _M0L11_2afield__9S1430;
  return _block_1502;
}

struct _M0TP26RiantR8snn__mbt8Receptor* _M0MP26RiantR8snn__mbt8Receptor6simple(
  float _M0L6e__revS584,
  float _M0L6tau__rS578,
  float _M0L6tau__dS577,
  float _M0L2g0S583,
  moonbit_string_t _M0L6targetS585
) {
  float _M0L6_2atmpS1420;
  float _M0L6_2atmpS1421;
  float _M0L5alphaS576;
  float _M0L11tau__r__invS579;
  float _M0L11tau__d__invS580;
  float _M0L4normS581;
  float _M0L4gsynS582;
  struct _M0TP26RiantR8snn__mbt8Receptor* _block_1503;
  #line 96 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\receptor.mbt"
  _M0L6_2atmpS1420 = _M0L6tau__dS577 - _M0L6tau__rS578;
  _M0L6_2atmpS1421 = _M0L6tau__dS577 * _M0L6tau__rS578;
  _M0L5alphaS576 = _M0L6_2atmpS1420 / _M0L6_2atmpS1421;
  if (_M0L6tau__rS578 > 0x0p+0f) {
    _M0L11tau__r__invS579 = 0x1p+0f / _M0L6tau__rS578;
  } else {
    _M0L11tau__r__invS579 = 0x0p+0f;
  }
  if (_M0L6tau__dS577 > 0x0p+0f) {
    _M0L11tau__d__invS580 = 0x1p+0f / _M0L6tau__dS577;
  } else {
    _M0L11tau__d__invS580 = 0x0p+0f;
  }
  #line 108 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\receptor.mbt"
  _M0L4normS581
  = _M0FP26RiantR8snn__mbt13norm__synapse(_M0L6tau__rS578, _M0L6tau__dS577);
  if (_M0L2g0S583 > 0x0p+0f) {
    _M0L4gsynS582 = _M0L2g0S583 * _M0L4normS581;
  } else {
    _M0L4gsynS582 = 0x0p+0f;
  }
  moonbit_incref(_M0L6targetS585);
  _block_1503
  = (struct _M0TP26RiantR8snn__mbt8Receptor*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt8Receptor));
  Moonbit_object_header(_block_1503)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 21, 0);
  _block_1503->$0 = _M0L6e__revS584;
  _block_1503->$1 = _M0L6tau__rS578;
  _block_1503->$2 = _M0L6tau__dS577;
  _block_1503->$3 = _M0L2g0S583;
  _block_1503->$4 = _M0L4gsynS582;
  _block_1503->$5 = _M0L5alphaS576;
  _block_1503->$6 = _M0L11tau__r__invS579;
  _block_1503->$7 = _M0L11tau__d__invS580;
  _block_1503->$8 = 0;
  _block_1503->$9 = _M0L6targetS585;
  return _block_1503;
}

float _M0FP26RiantR8snn__mbt13norm__synapse(
  float _M0L6tau__rS574,
  float _M0L6tau__dS575
) {
  float _M0L6_2atmpS1418;
  float _M0L6_2atmpS1419;
  float _M0L6_2atmpS1415;
  float _M0L6_2atmpS1417;
  float _M0L6_2atmpS1416;
  float _M0L4t__pS573;
  float _M0L6_2atmpS1414;
  float _M0L6_2atmpS1413;
  float _M0L6_2atmpS1412;
  float _M0L6_2atmpS1408;
  float _M0L6_2atmpS1411;
  float _M0L6_2atmpS1410;
  float _M0L6_2atmpS1409;
  float _M0L6_2atmpS1407;
  #line 129 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\receptor.mbt"
  _M0L6_2atmpS1418 = _M0L6tau__rS574 * _M0L6tau__dS575;
  _M0L6_2atmpS1419 = _M0L6tau__dS575 - _M0L6tau__rS574;
  _M0L6_2atmpS1415 = _M0L6_2atmpS1418 / _M0L6_2atmpS1419;
  _M0L6_2atmpS1417 = _M0L6tau__dS575 / _M0L6tau__rS574;
  #line 130 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\receptor.mbt"
  _M0L6_2atmpS1416 = _M0FP26RiantR8snn__mbt4logf(_M0L6_2atmpS1417);
  _M0L4t__pS573 = _M0L6_2atmpS1415 * _M0L6_2atmpS1416;
  _M0L6_2atmpS1414 = -_M0L4t__pS573;
  _M0L6_2atmpS1413 = _M0L6_2atmpS1414 / _M0L6tau__rS574;
  #line 131 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\receptor.mbt"
  _M0L6_2atmpS1412 = _M0FP26RiantR8snn__mbt4expf(_M0L6_2atmpS1413);
  _M0L6_2atmpS1408 = -_M0L6_2atmpS1412;
  _M0L6_2atmpS1411 = -_M0L4t__pS573;
  _M0L6_2atmpS1410 = _M0L6_2atmpS1411 / _M0L6tau__dS575;
  #line 131 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\receptor.mbt"
  _M0L6_2atmpS1409 = _M0FP26RiantR8snn__mbt4expf(_M0L6_2atmpS1410);
  _M0L6_2atmpS1407 = _M0L6_2atmpS1408 + _M0L6_2atmpS1409;
  return 0x1p+0f / _M0L6_2atmpS1407;
}

float _M0FP26RiantR8snn__mbt12nmda__gating(
  float _M0L1vS570,
  struct _M0TP26RiantR8snn__mbt21NMDAVoltageDependency* _M0L4nmdaS569
) {
  float _M0L1kS1406;
  float _M0L3argS568;
  float _M0L8exp__argS571;
  float _M0L2mgS1404;
  float _M0L1bS1405;
  float _M0L6_2atmpS1403;
  float _M0L6_2atmpS1402;
  float _M0L5denomS572;
  #line 60 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\receptor.mbt"
  _M0L1kS1406 = _M0L4nmdaS569->$1;
  _M0L3argS568 = _M0L1kS1406 * _M0L1vS570;
  if (_M0L3argS568 < -0x1.5cp+6f) {
    _M0L8exp__argS571 = 0x0p+0f;
  } else if (_M0L3argS568 > 0x1.6p+6f) {
    _M0L8exp__argS571 = 0x1.2ced32a16a1b1p+126f;
  } else {
    #line 70 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\receptor.mbt"
    _M0L8exp__argS571 = _M0FP26RiantR8snn__mbt4expf(_M0L3argS568);
  }
  _M0L2mgS1404 = _M0L4nmdaS569->$2;
  _M0L1bS1405 = _M0L4nmdaS569->$0;
  _M0L6_2atmpS1403 = _M0L2mgS1404 / _M0L1bS1405;
  _M0L6_2atmpS1402 = _M0L6_2atmpS1403 * _M0L8exp__argS571;
  _M0L5denomS572 = 0x1p+0f + _M0L6_2atmpS1402;
  return 0x1p+0f / _M0L5denomS572;
}

struct _M0TP26RiantR8snn__mbt21NMDAVoltageDependency* _M0MP26RiantR8snn__mbt21NMDAVoltageDependency4eyal(
  
) {
  struct _M0TP26RiantR8snn__mbt21NMDAVoltageDependency* _block_1504;
  #line 36 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\receptor.mbt"
  _block_1504
  = (struct _M0TP26RiantR8snn__mbt21NMDAVoltageDependency*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt21NMDAVoltageDependency));
  Moonbit_object_header(_block_1504)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_1504->$0 = 0x1.ae147ae147ae1p+1f;
  _block_1504->$1 = -0x1.3b645a1cac083p-4f;
  _block_1504->$2 = 0x1p+0f;
  return _block_1504;
}

moonbit_string_t _M0IPC15float5FloatPB4Show10to__string(float _M0L4selfS567) {
  double _M0L6_2atmpS1401;
  #line 16 "C:\\Users\\31379\\.moon\\lib\\core\\float\\methods.mbt"
  _M0L6_2atmpS1401 = (double)_M0L4selfS567;
  #line 17 "C:\\Users\\31379\\.moon\\lib\\core\\float\\methods.mbt"
  return _M0MPC16double6Double10to__string(_M0L6_2atmpS1401);
}

struct _M0TPB5ArrayGfE* _M0MPC15array5Array4makeGfE(
  int32_t _M0L3lenS563,
  float _M0L4elemS565
) {
  struct _M0TPB5ArrayGfE* _M0L3arrS562;
  int32_t _M0L1iS564;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3arrS562 = _M0MPC15array5Array20unsafe__make__uninitGfE(_M0L3lenS563);
  _M0L1iS564 = 0;
  while (1) {
    if (_M0L1iS564 < _M0L3lenS563) {
      float* _M0L3bufS1399 = _M0L3arrS562->$0;
      int32_t _M0L6_2atmpS1400;
      _M0L3bufS1399[_M0L1iS564] = _M0L4elemS565;
      _M0L6_2atmpS1400 = _M0L1iS564 + 1;
      _M0L1iS564 = _M0L6_2atmpS1400;
      continue;
    }
    break;
  }
  return _M0L3arrS562;
}

int32_t _M0MPC15array5Array3setGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS559,
  int32_t _M0L5indexS560,
  float _M0L5valueS561
) {
  int32_t _M0L3lenS558;
  #line 262 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS558 = _M0L4selfS559->$1;
  if (_M0L5indexS560 >= 0 && _M0L5indexS560 < _M0L3lenS558) {
    float* _M0L6_2atmpS1398;
    #line 268 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS1398 = _M0MPC15array5Array6bufferGfE(_M0L4selfS559);
    _M0L6_2atmpS1398[_M0L5indexS560] = _M0L5valueS561;
    moonbit_decref(_M0L6_2atmpS1398);
  } else {
    #line 267 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
  return 0;
}

int32_t _M0MPC15array5Array2atGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS550,
  int32_t _M0L5indexS551
) {
  int32_t _M0L3lenS549;
  #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS549 = _M0L4selfS550->$1;
  if (_M0L5indexS551 >= 0 && _M0L5indexS551 < _M0L3lenS549) {
    int32_t* _M0L6_2atmpS1395;
    int32_t _result_1506;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS1395 = _M0MPC15array5Array6bufferGiE(_M0L4selfS550);
    _result_1506 = (int32_t)_M0L6_2atmpS1395[_M0L5indexS551];
    moonbit_decref(_M0L6_2atmpS1395);
    return _result_1506;
  } else {
    #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
}

float _M0MPC15array5Array2atGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS553,
  int32_t _M0L5indexS554
) {
  int32_t _M0L3lenS552;
  #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS552 = _M0L4selfS553->$1;
  if (_M0L5indexS554 >= 0 && _M0L5indexS554 < _M0L3lenS552) {
    float* _M0L6_2atmpS1396;
    float _result_1507;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS1396 = _M0MPC15array5Array6bufferGfE(_M0L4selfS553);
    _result_1507 = (float)_M0L6_2atmpS1396[_M0L5indexS554];
    moonbit_decref(_M0L6_2atmpS1396);
    return _result_1507;
  } else {
    #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
}

struct _M0TP26RiantR8snn__mbt8Receptor* _M0MPC15array5Array2atGRP26RiantR8snn__mbt8ReceptorE(
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt8ReceptorE* _M0L4selfS556,
  int32_t _M0L5indexS557
) {
  int32_t _M0L3lenS555;
  #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS555 = _M0L4selfS556->$1;
  if (_M0L5indexS557 >= 0 && _M0L5indexS557 < _M0L3lenS555) {
    struct _M0TP26RiantR8snn__mbt8Receptor** _M0L6_2atmpS1397;
    struct _M0TP26RiantR8snn__mbt8Receptor* _M0L6_2atmpS1478;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS1397
    = _M0MPC15array5Array6bufferGRP26RiantR8snn__mbt8ReceptorE(_M0L4selfS556);
    _M0L6_2atmpS1478
    = (struct _M0TP26RiantR8snn__mbt8Receptor*)_M0L6_2atmpS1397[
        _M0L5indexS557
      ];
    if (_M0L6_2atmpS1478) {
      moonbit_incref(_M0L6_2atmpS1478);
    }
    moonbit_decref(_M0L6_2atmpS1397);
    return _M0L6_2atmpS1478;
  } else {
    #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
}

int32_t _M0FPB7printlnGsE(moonbit_string_t _M0L5inputS548) {
  moonbit_string_t _M0L6_2atmpS1394;
  #line 36 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\console.mbt"
  #line 37 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\console.mbt"
  _M0L6_2atmpS1394 = _M0IPC16string6StringPB4Show10to__string(_M0L5inputS548);
  #line 37 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\console.mbt"
  moonbit_println(_M0L6_2atmpS1394);
  moonbit_decref(_M0L6_2atmpS1394);
  return 0;
}

moonbit_string_t _M0MPC16double6Double10to__string(double _M0L4selfS547) {
  #line 296 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double.mbt"
  #line 298 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double.mbt"
  return _M0FPB15ryu__to__string(_M0L4selfS547);
}

moonbit_string_t _M0FPB15ryu__to__string(double _M0L3valS532) {
  uint64_t _M0L4bitsS535;
  uint64_t _M0L6_2atmpS1393;
  uint64_t _M0L6_2atmpS1392;
  int32_t _M0L8ieeeSignS536;
  uint64_t _M0L12ieeeMantissaS537;
  uint64_t _M0L6_2atmpS1391;
  uint64_t _M0L6_2atmpS1390;
  int32_t _M0L12ieeeExponentS538;
  struct _M0TPB17FloatingDecimal64* _M0L7_2abindS539;
  struct _M0TPB17FloatingDecimal64* _M0L1vS540;
  moonbit_string_t _result_1509;
  #line 668 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  if (_M0L3valS532 == 0x0p+0) {
    return (moonbit_string_t)moonbit_string_literal_2.data;
  }
  if (_M0L3valS532 >= -0x1p+53 && _M0L3valS532 <= 0x1p+53) {
    if (_M0L3valS532 >= -0x1p+31 && _M0L3valS532 <= 0x1.fffffffcp+30) {
      int32_t _M0L1iS533;
      double _M0L6_2atmpS1379;
      #line 683 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0L1iS533 = _M0MPC16double6Double7to__int(_M0L3valS532);
      _M0L6_2atmpS1379 = (double)_M0L1iS533;
      if (_M0L6_2atmpS1379 == _M0L3valS532) {
        #line 685 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        return _M0MPC13int3Int18to__string_2einner(_M0L1iS533, 10);
      }
    } else {
      int64_t _M0L1iS534;
      double _M0L6_2atmpS1380;
      #line 688 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0L1iS534 = _M0MPC16double6Double9to__int64(_M0L3valS532);
      _M0L6_2atmpS1380 = (double)_M0L1iS534;
      if (_M0L6_2atmpS1380 == _M0L3valS532) {
        #line 690 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        return _M0MPC15int645Int6418to__string_2einner(_M0L1iS534, 10);
      }
    }
  }
  _M0L4bitsS535 = *(int64_t*)&_M0L3valS532;
  _M0L6_2atmpS1393 = _M0L4bitsS535 >> 63;
  _M0L6_2atmpS1392 = _M0L6_2atmpS1393 & 1ull;
  _M0L8ieeeSignS536 = _M0L6_2atmpS1392 != 0ull;
  _M0L12ieeeMantissaS537 = _M0L4bitsS535 & 4503599627370495ull;
  _M0L6_2atmpS1391 = _M0L4bitsS535 >> 52;
  _M0L6_2atmpS1390 = _M0L6_2atmpS1391 & 2047ull;
  _M0L12ieeeExponentS538 = (int32_t)_M0L6_2atmpS1390;
  if (
    _M0L12ieeeExponentS538 == 2047
    || _M0L12ieeeExponentS538 == 0 && _M0L12ieeeMantissaS537 == 0ull
  ) {
    int32_t _M0L6_2atmpS1381 = _M0L12ieeeExponentS538 != 0;
    int32_t _M0L6_2atmpS1382 = _M0L12ieeeMantissaS537 != 0ull;
    #line 707 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    return _M0FPB18copy__special__str(_M0L8ieeeSignS536, _M0L6_2atmpS1381, _M0L6_2atmpS1382);
  }
  #line 709 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS539
  = _M0FPB15d2d__small__int(_M0L12ieeeMantissaS537, _M0L12ieeeExponentS538);
  if (_M0L7_2abindS539 == 0) {
    uint32_t _M0L6_2atmpS1383;
    if (_M0L7_2abindS539) {
      moonbit_decref(_M0L7_2abindS539);
    }
    _M0L6_2atmpS1383 = *(uint32_t*)&_M0L12ieeeExponentS538;
    #line 719 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L1vS540 = _M0FPB3d2d(_M0L12ieeeMantissaS537, _M0L6_2atmpS1383);
  } else {
    struct _M0TPB17FloatingDecimal64* _M0L7_2aSomeS541 = _M0L7_2abindS539;
    struct _M0TPB17FloatingDecimal64* _M0L4_2afS542 = _M0L7_2aSomeS541;
    struct _M0TPB17FloatingDecimal64* _M0L1xS543 = _M0L4_2afS542;
    while (1) {
      uint64_t _M0L8mantissaS1389 = _M0L1xS543->$0;
      uint64_t _M0L1qS544 = _M0L8mantissaS1389 / 10ull;
      uint64_t _M0L8mantissaS1387 = _M0L1xS543->$0;
      uint64_t _M0L6_2atmpS1388 = 10ull * _M0L1qS544;
      uint64_t _M0L1rS545 = _M0L8mantissaS1387 - _M0L6_2atmpS1388;
      int32_t _M0L8exponentS1386;
      int32_t _M0L6_2atmpS1385;
      struct _M0TPB17FloatingDecimal64* _M0L6_2atmpS1384;
      if (_M0L1rS545 != 0ull) {
        _M0L1vS540 = _M0L1xS543;
        break;
      }
      _M0L8exponentS1386 = _M0L1xS543->$1;
      moonbit_decref(_M0L1xS543);
      _M0L6_2atmpS1385 = _M0L8exponentS1386 + 1;
      _M0L6_2atmpS1384
      = (struct _M0TPB17FloatingDecimal64*)moonbit_malloc(sizeof(struct _M0TPB17FloatingDecimal64));
      Moonbit_object_header(_M0L6_2atmpS1384)->meta
      = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
      _M0L6_2atmpS1384->$0 = _M0L1qS544;
      _M0L6_2atmpS1384->$1 = _M0L6_2atmpS1385;
      _M0L1xS543 = _M0L6_2atmpS1384;
      continue;
      break;
    }
  }
  #line 721 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _result_1509 = _M0FPB9to__chars(_M0L1vS540, _M0L8ieeeSignS536);
  moonbit_decref(_M0L1vS540);
  return _result_1509;
}

struct _M0TPB17FloatingDecimal64* _M0FPB15d2d__small__int(
  uint64_t _M0L12ieeeMantissaS527,
  int32_t _M0L12ieeeExponentS529
) {
  uint64_t _M0L2m2S526;
  int32_t _M0L6_2atmpS1378;
  int32_t _M0L2e2S528;
  int32_t _M0L6_2atmpS1377;
  uint64_t _M0L6_2atmpS1376;
  uint64_t _M0L4maskS530;
  uint64_t _M0L8fractionS531;
  int32_t _M0L6_2atmpS1375;
  uint64_t _M0L6_2atmpS1374;
  struct _M0TPB17FloatingDecimal64* _M0L6_2atmpS1373;
  #line 637 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L2m2S526 = 4503599627370496ull | _M0L12ieeeMantissaS527;
  _M0L6_2atmpS1378 = _M0L12ieeeExponentS529 - 1023;
  _M0L2e2S528 = _M0L6_2atmpS1378 - 52;
  if (_M0L2e2S528 > 0) {
    return 0;
  }
  if (_M0L2e2S528 < -52) {
    return 0;
  }
  _M0L6_2atmpS1377 = -_M0L2e2S528;
  _M0L6_2atmpS1376 = 1ull << (_M0L6_2atmpS1377 & 63);
  _M0L4maskS530 = _M0L6_2atmpS1376 - 1ull;
  _M0L8fractionS531 = _M0L2m2S526 & _M0L4maskS530;
  if (_M0L8fractionS531 != 0ull) {
    return 0;
  }
  _M0L6_2atmpS1375 = -_M0L2e2S528;
  _M0L6_2atmpS1374 = _M0L2m2S526 >> (_M0L6_2atmpS1375 & 63);
  _M0L6_2atmpS1373
  = (struct _M0TPB17FloatingDecimal64*)moonbit_malloc(sizeof(struct _M0TPB17FloatingDecimal64));
  Moonbit_object_header(_M0L6_2atmpS1373)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L6_2atmpS1373->$0 = _M0L6_2atmpS1374;
  _M0L6_2atmpS1373->$1 = 0;
  return _M0L6_2atmpS1373;
}

moonbit_string_t _M0FPB9to__chars(
  struct _M0TPB17FloatingDecimal64* _M0L1vS494,
  int32_t _M0L4signS492
) {
  moonbit_bytes_t _M0L6resultS490;
  int32_t _M0Lm5indexS491;
  uint64_t _M0L6outputS493;
  int32_t _M0L7olengthS495;
  int32_t _M0L8exponentS1372;
  int32_t _M0L6_2atmpS1371;
  int32_t _M0Lm3expS496;
  int32_t _M0L6_2atmpS1370;
  int32_t _M0L6_2atmpS1368;
  int32_t _M0L18scientificNotationS497;
  #line 530 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6resultS490 = (moonbit_bytes_t)moonbit_make_bytes(25, 0);
  _M0Lm5indexS491 = 0;
  if (_M0L4signS492) {
    int32_t _M0L6_2atmpS1242 = _M0Lm5indexS491;
    int32_t _M0L6_2atmpS1243;
    if (
      _M0L6_2atmpS1242 < 0
      || _M0L6_2atmpS1242 >= Moonbit_array_length(_M0L6resultS490)
    ) {
      #line 535 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      moonbit_panic();
    }
    _M0L6resultS490[_M0L6_2atmpS1242] = 45;
    _M0L6_2atmpS1243 = _M0Lm5indexS491;
    _M0Lm5indexS491 = _M0L6_2atmpS1243 + 1;
  }
  _M0L6outputS493 = _M0L1vS494->$0;
  #line 539 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7olengthS495 = _M0FPB17decimal__length17(_M0L6outputS493);
  _M0L8exponentS1372 = _M0L1vS494->$1;
  _M0L6_2atmpS1371 = _M0L8exponentS1372 + _M0L7olengthS495;
  _M0Lm3expS496 = _M0L6_2atmpS1371 - 1;
  _M0L6_2atmpS1370 = _M0Lm3expS496;
  if (_M0L6_2atmpS1370 >= -6) {
    int32_t _M0L6_2atmpS1369 = _M0Lm3expS496;
    _M0L6_2atmpS1368 = _M0L6_2atmpS1369 < 21;
  } else {
    _M0L6_2atmpS1368 = 0;
  }
  _M0L18scientificNotationS497 = !_M0L6_2atmpS1368;
  if (_M0L18scientificNotationS497) {
    int32_t _M0L7_2abindS498 = _M0L7olengthS495 - 1;
    uint64_t _M0L6outputS499;
    int32_t _M0L1iS500 = 0;
    uint64_t _M0L6outputS501 = _M0L6outputS493;
    int32_t _M0L6_2atmpS1244;
    int32_t _M0L6_2atmpS1248;
    int32_t _M0L6_2atmpS1247;
    int32_t _M0L6_2atmpS1246;
    int32_t _M0L6_2atmpS1245;
    int32_t _M0L6_2atmpS1252;
    int32_t _M0L6_2atmpS1253;
    int32_t _M0L6_2atmpS1254;
    int32_t _M0L6_2atmpS1255;
    int32_t _M0L6_2atmpS1256;
    int32_t _M0L6_2atmpS1262;
    int32_t _M0L6_2atmpS1295;
    moonbit_string_t _result_1511;
    while (1) {
      if (_M0L1iS500 < _M0L7_2abindS498) {
        uint64_t _M0L1cS502 = _M0L6outputS501 % 10ull;
        int32_t _M0L6_2atmpS1301 = _M0Lm5indexS491;
        int32_t _M0L6_2atmpS1300 = _M0L6_2atmpS1301 + _M0L7olengthS495;
        int32_t _M0L6_2atmpS1296 = _M0L6_2atmpS1300 - _M0L1iS500;
        int32_t _M0L6_2atmpS1299 = (int32_t)_M0L1cS502;
        int32_t _M0L6_2atmpS1298 = 48 + _M0L6_2atmpS1299;
        int32_t _M0L6_2atmpS1297 = _M0L6_2atmpS1298 & 0xff;
        int32_t _M0L6_2atmpS1302;
        uint64_t _M0L6_2atmpS1303;
        if (
          _M0L6_2atmpS1296 < 0
          || _M0L6_2atmpS1296 >= Moonbit_array_length(_M0L6resultS490)
        ) {
          #line 547 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
          moonbit_panic();
        }
        _M0L6resultS490[_M0L6_2atmpS1296] = _M0L6_2atmpS1297;
        _M0L6_2atmpS1302 = _M0L1iS500 + 1;
        _M0L6_2atmpS1303 = _M0L6outputS501 / 10ull;
        _M0L1iS500 = _M0L6_2atmpS1302;
        _M0L6outputS501 = _M0L6_2atmpS1303;
        continue;
      } else {
        _M0L6outputS499 = _M0L6outputS501;
      }
      break;
    }
    _M0L6_2atmpS1244 = _M0Lm5indexS491;
    _M0L6_2atmpS1248 = (int32_t)_M0L6outputS499;
    _M0L6_2atmpS1247 = _M0L6_2atmpS1248 % 10;
    _M0L6_2atmpS1246 = 48 + _M0L6_2atmpS1247;
    _M0L6_2atmpS1245 = _M0L6_2atmpS1246 & 0xff;
    if (
      _M0L6_2atmpS1244 < 0
      || _M0L6_2atmpS1244 >= Moonbit_array_length(_M0L6resultS490)
    ) {
      #line 552 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      moonbit_panic();
    }
    _M0L6resultS490[_M0L6_2atmpS1244] = _M0L6_2atmpS1245;
    if (_M0L7olengthS495 > 1) {
      int32_t _M0L6_2atmpS1250 = _M0Lm5indexS491;
      int32_t _M0L6_2atmpS1249 = _M0L6_2atmpS1250 + 1;
      if (
        _M0L6_2atmpS1249 < 0
        || _M0L6_2atmpS1249 >= Moonbit_array_length(_M0L6resultS490)
      ) {
        #line 554 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS490[_M0L6_2atmpS1249] = 46;
    } else {
      int32_t _M0L6_2atmpS1251 = _M0Lm5indexS491;
      _M0Lm5indexS491 = _M0L6_2atmpS1251 - 1;
    }
    _M0L6_2atmpS1252 = _M0Lm5indexS491;
    _M0L6_2atmpS1253 = _M0L7olengthS495 + 1;
    _M0Lm5indexS491 = _M0L6_2atmpS1252 + _M0L6_2atmpS1253;
    _M0L6_2atmpS1254 = _M0Lm5indexS491;
    if (
      _M0L6_2atmpS1254 < 0
      || _M0L6_2atmpS1254 >= Moonbit_array_length(_M0L6resultS490)
    ) {
      #line 562 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      moonbit_panic();
    }
    _M0L6resultS490[_M0L6_2atmpS1254] = 101;
    _M0L6_2atmpS1255 = _M0Lm5indexS491;
    _M0Lm5indexS491 = _M0L6_2atmpS1255 + 1;
    _M0L6_2atmpS1256 = _M0Lm3expS496;
    if (_M0L6_2atmpS1256 < 0) {
      int32_t _M0L6_2atmpS1257 = _M0Lm5indexS491;
      int32_t _M0L6_2atmpS1258;
      int32_t _M0L6_2atmpS1259;
      if (
        _M0L6_2atmpS1257 < 0
        || _M0L6_2atmpS1257 >= Moonbit_array_length(_M0L6resultS490)
      ) {
        #line 565 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS490[_M0L6_2atmpS1257] = 45;
      _M0L6_2atmpS1258 = _M0Lm5indexS491;
      _M0Lm5indexS491 = _M0L6_2atmpS1258 + 1;
      _M0L6_2atmpS1259 = _M0Lm3expS496;
      _M0Lm3expS496 = -_M0L6_2atmpS1259;
    } else {
      int32_t _M0L6_2atmpS1260 = _M0Lm5indexS491;
      int32_t _M0L6_2atmpS1261;
      if (
        _M0L6_2atmpS1260 < 0
        || _M0L6_2atmpS1260 >= Moonbit_array_length(_M0L6resultS490)
      ) {
        #line 569 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS490[_M0L6_2atmpS1260] = 43;
      _M0L6_2atmpS1261 = _M0Lm5indexS491;
      _M0Lm5indexS491 = _M0L6_2atmpS1261 + 1;
    }
    _M0L6_2atmpS1262 = _M0Lm3expS496;
    if (_M0L6_2atmpS1262 >= 100) {
      int32_t _M0L6_2atmpS1278 = _M0Lm3expS496;
      int32_t _M0L1aS504 = _M0L6_2atmpS1278 / 100;
      int32_t _M0L6_2atmpS1277 = _M0Lm3expS496;
      int32_t _M0L6_2atmpS1276 = _M0L6_2atmpS1277 / 10;
      int32_t _M0L1bS505 = _M0L6_2atmpS1276 % 10;
      int32_t _M0L6_2atmpS1275 = _M0Lm3expS496;
      int32_t _M0L1cS506 = _M0L6_2atmpS1275 % 10;
      int32_t _M0L6_2atmpS1263 = _M0Lm5indexS491;
      int32_t _M0L6_2atmpS1265 = 48 + _M0L1aS504;
      int32_t _M0L6_2atmpS1264 = _M0L6_2atmpS1265 & 0xff;
      int32_t _M0L6_2atmpS1269;
      int32_t _M0L6_2atmpS1266;
      int32_t _M0L6_2atmpS1268;
      int32_t _M0L6_2atmpS1267;
      int32_t _M0L6_2atmpS1273;
      int32_t _M0L6_2atmpS1270;
      int32_t _M0L6_2atmpS1272;
      int32_t _M0L6_2atmpS1271;
      int32_t _M0L6_2atmpS1274;
      if (
        _M0L6_2atmpS1263 < 0
        || _M0L6_2atmpS1263 >= Moonbit_array_length(_M0L6resultS490)
      ) {
        #line 576 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS490[_M0L6_2atmpS1263] = _M0L6_2atmpS1264;
      _M0L6_2atmpS1269 = _M0Lm5indexS491;
      _M0L6_2atmpS1266 = _M0L6_2atmpS1269 + 1;
      _M0L6_2atmpS1268 = 48 + _M0L1bS505;
      _M0L6_2atmpS1267 = _M0L6_2atmpS1268 & 0xff;
      if (
        _M0L6_2atmpS1266 < 0
        || _M0L6_2atmpS1266 >= Moonbit_array_length(_M0L6resultS490)
      ) {
        #line 577 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS490[_M0L6_2atmpS1266] = _M0L6_2atmpS1267;
      _M0L6_2atmpS1273 = _M0Lm5indexS491;
      _M0L6_2atmpS1270 = _M0L6_2atmpS1273 + 2;
      _M0L6_2atmpS1272 = 48 + _M0L1cS506;
      _M0L6_2atmpS1271 = _M0L6_2atmpS1272 & 0xff;
      if (
        _M0L6_2atmpS1270 < 0
        || _M0L6_2atmpS1270 >= Moonbit_array_length(_M0L6resultS490)
      ) {
        #line 578 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS490[_M0L6_2atmpS1270] = _M0L6_2atmpS1271;
      _M0L6_2atmpS1274 = _M0Lm5indexS491;
      _M0Lm5indexS491 = _M0L6_2atmpS1274 + 3;
    } else {
      int32_t _M0L6_2atmpS1279 = _M0Lm3expS496;
      if (_M0L6_2atmpS1279 >= 10) {
        int32_t _M0L6_2atmpS1289 = _M0Lm3expS496;
        int32_t _M0L1aS507 = _M0L6_2atmpS1289 / 10;
        int32_t _M0L6_2atmpS1288 = _M0Lm3expS496;
        int32_t _M0L1bS508 = _M0L6_2atmpS1288 % 10;
        int32_t _M0L6_2atmpS1280 = _M0Lm5indexS491;
        int32_t _M0L6_2atmpS1282 = 48 + _M0L1aS507;
        int32_t _M0L6_2atmpS1281 = _M0L6_2atmpS1282 & 0xff;
        int32_t _M0L6_2atmpS1286;
        int32_t _M0L6_2atmpS1283;
        int32_t _M0L6_2atmpS1285;
        int32_t _M0L6_2atmpS1284;
        int32_t _M0L6_2atmpS1287;
        if (
          _M0L6_2atmpS1280 < 0
          || _M0L6_2atmpS1280 >= Moonbit_array_length(_M0L6resultS490)
        ) {
          #line 583 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
          moonbit_panic();
        }
        _M0L6resultS490[_M0L6_2atmpS1280] = _M0L6_2atmpS1281;
        _M0L6_2atmpS1286 = _M0Lm5indexS491;
        _M0L6_2atmpS1283 = _M0L6_2atmpS1286 + 1;
        _M0L6_2atmpS1285 = 48 + _M0L1bS508;
        _M0L6_2atmpS1284 = _M0L6_2atmpS1285 & 0xff;
        if (
          _M0L6_2atmpS1283 < 0
          || _M0L6_2atmpS1283 >= Moonbit_array_length(_M0L6resultS490)
        ) {
          #line 584 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
          moonbit_panic();
        }
        _M0L6resultS490[_M0L6_2atmpS1283] = _M0L6_2atmpS1284;
        _M0L6_2atmpS1287 = _M0Lm5indexS491;
        _M0Lm5indexS491 = _M0L6_2atmpS1287 + 2;
      } else {
        int32_t _M0L6_2atmpS1290 = _M0Lm5indexS491;
        int32_t _M0L6_2atmpS1293 = _M0Lm3expS496;
        int32_t _M0L6_2atmpS1292 = 48 + _M0L6_2atmpS1293;
        int32_t _M0L6_2atmpS1291 = _M0L6_2atmpS1292 & 0xff;
        int32_t _M0L6_2atmpS1294;
        if (
          _M0L6_2atmpS1290 < 0
          || _M0L6_2atmpS1290 >= Moonbit_array_length(_M0L6resultS490)
        ) {
          #line 587 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
          moonbit_panic();
        }
        _M0L6resultS490[_M0L6_2atmpS1290] = _M0L6_2atmpS1291;
        _M0L6_2atmpS1294 = _M0Lm5indexS491;
        _M0Lm5indexS491 = _M0L6_2atmpS1294 + 1;
      }
    }
    _M0L6_2atmpS1295 = _M0Lm5indexS491;
    #line 590 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _result_1511
    = _M0FPB19string__from__bytes(_M0L6resultS490, 0, _M0L6_2atmpS1295);
    moonbit_decref(_M0L6resultS490);
    return _result_1511;
  } else {
    int32_t _M0L6_2atmpS1304 = _M0Lm3expS496;
    int32_t _M0L6_2atmpS1367;
    moonbit_string_t _result_1517;
    if (_M0L6_2atmpS1304 < 0) {
      int32_t _M0L6_2atmpS1305 = _M0Lm5indexS491;
      int32_t _M0L6_2atmpS1307;
      int32_t _M0L6_2atmpS1306;
      int32_t _M0L6_2atmpS1308;
      int32_t _M0L1iS509;
      int32_t _M0L6_2atmpS1323;
      int32_t _M0L6_2atmpS1325;
      int32_t _M0L6_2atmpS1324;
      int32_t _M0L7currentS511;
      int32_t _M0L1iS512;
      uint64_t _M0L6outputS513;
      if (
        _M0L6_2atmpS1305 < 0
        || _M0L6_2atmpS1305 >= Moonbit_array_length(_M0L6resultS490)
      ) {
        #line 595 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS490[_M0L6_2atmpS1305] = 48;
      _M0L6_2atmpS1307 = _M0Lm5indexS491;
      _M0L6_2atmpS1306 = _M0L6_2atmpS1307 + 1;
      if (
        _M0L6_2atmpS1306 < 0
        || _M0L6_2atmpS1306 >= Moonbit_array_length(_M0L6resultS490)
      ) {
        #line 596 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS490[_M0L6_2atmpS1306] = 46;
      _M0L6_2atmpS1308 = _M0Lm5indexS491;
      _M0Lm5indexS491 = _M0L6_2atmpS1308 + 2;
      _M0L1iS509 = -1;
      while (1) {
        int32_t _M0L6_2atmpS1309 = _M0Lm3expS496;
        if (_M0L1iS509 > _M0L6_2atmpS1309) {
          int32_t _M0L6_2atmpS1312 = _M0Lm5indexS491;
          int32_t _M0L6_2atmpS1311 = _M0L6_2atmpS1312 - _M0L1iS509;
          int32_t _M0L6_2atmpS1310 = _M0L6_2atmpS1311 - 1;
          int32_t _M0L6_2atmpS1313;
          if (
            _M0L6_2atmpS1310 < 0
            || _M0L6_2atmpS1310 >= Moonbit_array_length(_M0L6resultS490)
          ) {
            #line 599 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
            moonbit_panic();
          }
          _M0L6resultS490[_M0L6_2atmpS1310] = 48;
          _M0L6_2atmpS1313 = _M0L1iS509 - 1;
          _M0L1iS509 = _M0L6_2atmpS1313;
          continue;
        }
        break;
      }
      _M0L6_2atmpS1323 = _M0Lm5indexS491;
      _M0L6_2atmpS1325 = _M0Lm3expS496;
      _M0L6_2atmpS1324 = -1 - _M0L6_2atmpS1325;
      _M0L7currentS511 = _M0L6_2atmpS1323 + _M0L6_2atmpS1324;
      _M0L1iS512 = 0;
      _M0L6outputS513 = _M0L6outputS493;
      while (1) {
        if (_M0L1iS512 < _M0L7olengthS495) {
          int32_t _M0L6_2atmpS1320 = _M0L7currentS511 + _M0L7olengthS495;
          int32_t _M0L6_2atmpS1319 = _M0L6_2atmpS1320 - _M0L1iS512;
          int32_t _M0L6_2atmpS1314 = _M0L6_2atmpS1319 - 1;
          uint64_t _M0L6_2atmpS1318 = _M0L6outputS513 % 10ull;
          int32_t _M0L6_2atmpS1317 = (int32_t)_M0L6_2atmpS1318;
          int32_t _M0L6_2atmpS1316 = 48 + _M0L6_2atmpS1317;
          int32_t _M0L6_2atmpS1315 = _M0L6_2atmpS1316 & 0xff;
          int32_t _M0L6_2atmpS1321;
          uint64_t _M0L6_2atmpS1322;
          if (
            _M0L6_2atmpS1314 < 0
            || _M0L6_2atmpS1314 >= Moonbit_array_length(_M0L6resultS490)
          ) {
            #line 603 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
            moonbit_panic();
          }
          _M0L6resultS490[_M0L6_2atmpS1314] = _M0L6_2atmpS1315;
          _M0L6_2atmpS1321 = _M0L1iS512 + 1;
          _M0L6_2atmpS1322 = _M0L6outputS513 / 10ull;
          _M0L1iS512 = _M0L6_2atmpS1321;
          _M0L6outputS513 = _M0L6_2atmpS1322;
          continue;
        }
        break;
      }
      _M0Lm5indexS491 = _M0L7currentS511 + _M0L7olengthS495;
    } else {
      int32_t _M0L6_2atmpS1327 = _M0Lm3expS496;
      int32_t _M0L6_2atmpS1326 = _M0L6_2atmpS1327 + 1;
      if (_M0L6_2atmpS1326 >= _M0L7olengthS495) {
        int32_t _M0L1iS515 = 0;
        uint64_t _M0L6outputS516 = _M0L6outputS493;
        int32_t _M0L6_2atmpS1338;
        int32_t _M0L6_2atmpS1343;
        int32_t _M0L7_2abindS518;
        int32_t _M0L1iS519;
        int32_t _M0L6_2atmpS1344;
        int32_t _M0L6_2atmpS1347;
        int32_t _M0L6_2atmpS1346;
        int32_t _M0L6_2atmpS1345;
        while (1) {
          if (_M0L1iS515 < _M0L7olengthS495) {
            int32_t _M0L6_2atmpS1335 = _M0Lm5indexS491;
            int32_t _M0L6_2atmpS1334 = _M0L6_2atmpS1335 + _M0L7olengthS495;
            int32_t _M0L6_2atmpS1333 = _M0L6_2atmpS1334 - _M0L1iS515;
            int32_t _M0L6_2atmpS1328 = _M0L6_2atmpS1333 - 1;
            uint64_t _M0L6_2atmpS1332 = _M0L6outputS516 % 10ull;
            int32_t _M0L6_2atmpS1331 = (int32_t)_M0L6_2atmpS1332;
            int32_t _M0L6_2atmpS1330 = 48 + _M0L6_2atmpS1331;
            int32_t _M0L6_2atmpS1329 = _M0L6_2atmpS1330 & 0xff;
            int32_t _M0L6_2atmpS1336;
            uint64_t _M0L6_2atmpS1337;
            if (
              _M0L6_2atmpS1328 < 0
              || _M0L6_2atmpS1328 >= Moonbit_array_length(_M0L6resultS490)
            ) {
              #line 610 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
              moonbit_panic();
            }
            _M0L6resultS490[_M0L6_2atmpS1328] = _M0L6_2atmpS1329;
            _M0L6_2atmpS1336 = _M0L1iS515 + 1;
            _M0L6_2atmpS1337 = _M0L6outputS516 / 10ull;
            _M0L1iS515 = _M0L6_2atmpS1336;
            _M0L6outputS516 = _M0L6_2atmpS1337;
            continue;
          }
          break;
        }
        _M0L6_2atmpS1338 = _M0Lm5indexS491;
        _M0Lm5indexS491 = _M0L6_2atmpS1338 + _M0L7olengthS495;
        _M0L6_2atmpS1343 = _M0Lm3expS496;
        _M0L7_2abindS518 = _M0L6_2atmpS1343 + 1;
        _M0L1iS519 = _M0L7olengthS495;
        while (1) {
          if (_M0L1iS519 < _M0L7_2abindS518) {
            int32_t _M0L6_2atmpS1341 = _M0Lm5indexS491;
            int32_t _M0L6_2atmpS1340 = _M0L6_2atmpS1341 + _M0L1iS519;
            int32_t _M0L6_2atmpS1339 = _M0L6_2atmpS1340 - _M0L7olengthS495;
            int32_t _M0L6_2atmpS1342;
            if (
              _M0L6_2atmpS1339 < 0
              || _M0L6_2atmpS1339 >= Moonbit_array_length(_M0L6resultS490)
            ) {
              #line 615 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
              moonbit_panic();
            }
            _M0L6resultS490[_M0L6_2atmpS1339] = 48;
            _M0L6_2atmpS1342 = _M0L1iS519 + 1;
            _M0L1iS519 = _M0L6_2atmpS1342;
            continue;
          }
          break;
        }
        _M0L6_2atmpS1344 = _M0Lm5indexS491;
        _M0L6_2atmpS1347 = _M0Lm3expS496;
        _M0L6_2atmpS1346 = _M0L6_2atmpS1347 + 1;
        _M0L6_2atmpS1345 = _M0L6_2atmpS1346 - _M0L7olengthS495;
        _M0Lm5indexS491 = _M0L6_2atmpS1344 + _M0L6_2atmpS1345;
      } else {
        int32_t _M0L6_2atmpS1364 = _M0Lm5indexS491;
        int32_t _M0L6_2atmpS1363 = _M0L6_2atmpS1364 + 1;
        int32_t _M0L1iS521 = 0;
        int32_t _M0L7currentS522 = _M0L6_2atmpS1363;
        uint64_t _M0L6outputS523 = _M0L6outputS493;
        int32_t _M0L6_2atmpS1365;
        int32_t _M0L6_2atmpS1366;
        while (1) {
          if (_M0L1iS521 < _M0L7olengthS495) {
            int32_t _M0L6_2atmpS1359 = _M0L7olengthS495 - _M0L1iS521;
            int32_t _M0L6_2atmpS1357 = _M0L6_2atmpS1359 - 1;
            int32_t _M0L6_2atmpS1358 = _M0Lm3expS496;
            int32_t _M0L7currentS524;
            int32_t _M0L6_2atmpS1354;
            int32_t _M0L6_2atmpS1353;
            int32_t _M0L6_2atmpS1348;
            uint64_t _M0L6_2atmpS1352;
            int32_t _M0L6_2atmpS1351;
            int32_t _M0L6_2atmpS1350;
            int32_t _M0L6_2atmpS1349;
            int32_t _M0L6_2atmpS1355;
            uint64_t _M0L6_2atmpS1356;
            if (_M0L6_2atmpS1357 == _M0L6_2atmpS1358) {
              int32_t _M0L6_2atmpS1362 = _M0L7currentS522 + _M0L7olengthS495;
              int32_t _M0L6_2atmpS1361 = _M0L6_2atmpS1362 - _M0L1iS521;
              int32_t _M0L6_2atmpS1360 = _M0L6_2atmpS1361 - 1;
              if (
                _M0L6_2atmpS1360 < 0
                || _M0L6_2atmpS1360 >= Moonbit_array_length(_M0L6resultS490)
              ) {
                #line 622 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
                moonbit_panic();
              }
              _M0L6resultS490[_M0L6_2atmpS1360] = 46;
              _M0L7currentS524 = _M0L7currentS522 - 1;
            } else {
              _M0L7currentS524 = _M0L7currentS522;
            }
            _M0L6_2atmpS1354 = _M0L7currentS524 + _M0L7olengthS495;
            _M0L6_2atmpS1353 = _M0L6_2atmpS1354 - _M0L1iS521;
            _M0L6_2atmpS1348 = _M0L6_2atmpS1353 - 1;
            _M0L6_2atmpS1352 = _M0L6outputS523 % 10ull;
            _M0L6_2atmpS1351 = (int32_t)_M0L6_2atmpS1352;
            _M0L6_2atmpS1350 = 48 + _M0L6_2atmpS1351;
            _M0L6_2atmpS1349 = _M0L6_2atmpS1350 & 0xff;
            if (
              _M0L6_2atmpS1348 < 0
              || _M0L6_2atmpS1348 >= Moonbit_array_length(_M0L6resultS490)
            ) {
              #line 627 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
              moonbit_panic();
            }
            _M0L6resultS490[_M0L6_2atmpS1348] = _M0L6_2atmpS1349;
            _M0L6_2atmpS1355 = _M0L1iS521 + 1;
            _M0L6_2atmpS1356 = _M0L6outputS523 / 10ull;
            _M0L1iS521 = _M0L6_2atmpS1355;
            _M0L7currentS522 = _M0L7currentS524;
            _M0L6outputS523 = _M0L6_2atmpS1356;
            continue;
          }
          break;
        }
        _M0L6_2atmpS1365 = _M0Lm5indexS491;
        _M0L6_2atmpS1366 = _M0L7olengthS495 + 1;
        _M0Lm5indexS491 = _M0L6_2atmpS1365 + _M0L6_2atmpS1366;
      }
    }
    _M0L6_2atmpS1367 = _M0Lm5indexS491;
    #line 632 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _result_1517
    = _M0FPB19string__from__bytes(_M0L6resultS490, 0, _M0L6_2atmpS1367);
    moonbit_decref(_M0L6resultS490);
    return _result_1517;
  }
}

struct _M0TPB17FloatingDecimal64* _M0FPB3d2d(
  uint64_t _M0L12ieeeMantissaS436,
  uint32_t _M0L12ieeeExponentS435
) {
  int32_t _M0Lm2e2S433;
  uint64_t _M0Lm2m2S434;
  uint64_t _M0L6_2atmpS1241;
  uint64_t _M0L6_2atmpS1240;
  int32_t _M0L4evenS437;
  uint64_t _M0L6_2atmpS1239;
  uint64_t _M0L2mvS438;
  int32_t _M0L7mmShiftS439;
  uint64_t _M0Lm2vrS440;
  uint64_t _M0Lm2vpS441;
  uint64_t _M0Lm2vmS442;
  int32_t _M0Lm3e10S443;
  int32_t _M0Lm17vmIsTrailingZerosS444;
  int32_t _M0Lm17vrIsTrailingZerosS445;
  int32_t _M0L6_2atmpS1141;
  int32_t _M0Lm7removedS464;
  int32_t _M0Lm16lastRemovedDigitS465;
  uint64_t _M0Lm6outputS466;
  int32_t _M0L6_2atmpS1237;
  int32_t _M0L6_2atmpS1238;
  int32_t _M0L3expS489;
  uint64_t _M0L6_2atmpS1236;
  struct _M0TPB17FloatingDecimal64* _block_1523;
  #line 347 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0Lm2e2S433 = 0;
  _M0Lm2m2S434 = 0ull;
  if (_M0L12ieeeExponentS435 == 0u) {
    _M0Lm2e2S433 = -1076;
    _M0Lm2m2S434 = _M0L12ieeeMantissaS436;
  } else {
    int32_t _M0L6_2atmpS1140 = *(int32_t*)&_M0L12ieeeExponentS435;
    int32_t _M0L6_2atmpS1139 = _M0L6_2atmpS1140 - 1023;
    int32_t _M0L6_2atmpS1138 = _M0L6_2atmpS1139 - 52;
    _M0Lm2e2S433 = _M0L6_2atmpS1138 - 2;
    _M0Lm2m2S434 = 4503599627370496ull | _M0L12ieeeMantissaS436;
  }
  _M0L6_2atmpS1241 = _M0Lm2m2S434;
  _M0L6_2atmpS1240 = _M0L6_2atmpS1241 & 1ull;
  _M0L4evenS437 = _M0L6_2atmpS1240 == 0ull;
  _M0L6_2atmpS1239 = _M0Lm2m2S434;
  _M0L2mvS438 = 4ull * _M0L6_2atmpS1239;
  _M0L7mmShiftS439
  = _M0L12ieeeMantissaS436 != 0ull || _M0L12ieeeExponentS435 <= 1u;
  _M0Lm2vrS440 = 0ull;
  _M0Lm2vpS441 = 0ull;
  _M0Lm2vmS442 = 0ull;
  _M0Lm3e10S443 = 0;
  _M0Lm17vmIsTrailingZerosS444 = 0;
  _M0Lm17vrIsTrailingZerosS445 = 0;
  _M0L6_2atmpS1141 = _M0Lm2e2S433;
  if (_M0L6_2atmpS1141 >= 0) {
    int32_t _M0L6_2atmpS1163 = _M0Lm2e2S433;
    int32_t _M0L6_2atmpS1159;
    int32_t _M0L6_2atmpS1162;
    int32_t _M0L6_2atmpS1161;
    int32_t _M0L6_2atmpS1160;
    int32_t _M0L1qS446;
    int32_t _M0L6_2atmpS1158;
    int32_t _M0L6_2atmpS1157;
    int32_t _M0L1kS447;
    int32_t _M0L6_2atmpS1156;
    int32_t _M0L6_2atmpS1155;
    int32_t _M0L6_2atmpS1154;
    int32_t _M0L1iS448;
    struct _M0TPB8Pow5Pair _M0L4pow5S449;
    uint64_t _M0L6_2atmpS1153;
    struct _M0TPB19MulShiftAll64Result _M0L7_2abindS450;
    uint64_t _M0L8_2avrOutS451;
    uint64_t _M0L8_2avpOutS452;
    uint64_t _M0L8_2avmOutS453;
    #line 383 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1159 = _M0FPB9log10Pow2(_M0L6_2atmpS1163);
    _M0L6_2atmpS1162 = _M0Lm2e2S433;
    _M0L6_2atmpS1161 = _M0L6_2atmpS1162 > 3;
    #line 383 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1160 = _M0MPC14bool4Bool7to__int(_M0L6_2atmpS1161);
    _M0L1qS446 = _M0L6_2atmpS1159 - _M0L6_2atmpS1160;
    _M0Lm3e10S443 = _M0L1qS446;
    #line 385 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1158 = _M0FPB8pow5bits(_M0L1qS446);
    _M0L6_2atmpS1157 = 125 + _M0L6_2atmpS1158;
    _M0L1kS447 = _M0L6_2atmpS1157 - 1;
    _M0L6_2atmpS1156 = _M0Lm2e2S433;
    _M0L6_2atmpS1155 = -_M0L6_2atmpS1156;
    _M0L6_2atmpS1154 = _M0L6_2atmpS1155 + _M0L1qS446;
    _M0L1iS448 = _M0L6_2atmpS1154 + _M0L1kS447;
    #line 387 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L4pow5S449 = _M0FPB22double__computeInvPow5(_M0L1qS446);
    _M0L6_2atmpS1153 = _M0Lm2m2S434;
    #line 388 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L7_2abindS450
    = _M0FPB13mulShiftAll64(_M0L6_2atmpS1153, _M0L4pow5S449, _M0L1iS448, _M0L7mmShiftS439);
    _M0L8_2avrOutS451 = _M0L7_2abindS450.$0;
    _M0L8_2avpOutS452 = _M0L7_2abindS450.$1;
    _M0L8_2avmOutS453 = _M0L7_2abindS450.$2;
    _M0Lm2vrS440 = _M0L8_2avrOutS451;
    _M0Lm2vpS441 = _M0L8_2avpOutS452;
    _M0Lm2vmS442 = _M0L8_2avmOutS453;
    if (_M0L1qS446 <= 21) {
      int32_t _M0L6_2atmpS1149 = (int32_t)_M0L2mvS438;
      uint64_t _M0L6_2atmpS1152 = _M0L2mvS438 / 5ull;
      int32_t _M0L6_2atmpS1151 = (int32_t)_M0L6_2atmpS1152;
      int32_t _M0L6_2atmpS1150 = 5 * _M0L6_2atmpS1151;
      int32_t _M0L6mvMod5S454 = _M0L6_2atmpS1149 - _M0L6_2atmpS1150;
      if (_M0L6mvMod5S454 == 0) {
        #line 400 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0Lm17vrIsTrailingZerosS445
        = _M0FPB18multipleOfPowerOf5(_M0L2mvS438, _M0L1qS446);
      } else if (_M0L4evenS437) {
        uint64_t _M0L6_2atmpS1143 = _M0L2mvS438 - 1ull;
        uint64_t _M0L6_2atmpS1144;
        uint64_t _M0L6_2atmpS1142;
        #line 406 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0L6_2atmpS1144 = _M0MPC14bool4Bool10to__uint64(_M0L7mmShiftS439);
        _M0L6_2atmpS1142 = _M0L6_2atmpS1143 - _M0L6_2atmpS1144;
        #line 405 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0Lm17vmIsTrailingZerosS444
        = _M0FPB18multipleOfPowerOf5(_M0L6_2atmpS1142, _M0L1qS446);
      } else {
        uint64_t _M0L6_2atmpS1145 = _M0Lm2vpS441;
        uint64_t _M0L6_2atmpS1148 = _M0L2mvS438 + 2ull;
        int32_t _M0L6_2atmpS1147;
        uint64_t _M0L6_2atmpS1146;
        #line 410 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0L6_2atmpS1147
        = _M0FPB18multipleOfPowerOf5(_M0L6_2atmpS1148, _M0L1qS446);
        #line 410 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0L6_2atmpS1146 = _M0MPC14bool4Bool10to__uint64(_M0L6_2atmpS1147);
        _M0Lm2vpS441 = _M0L6_2atmpS1145 - _M0L6_2atmpS1146;
      }
    }
  } else {
    int32_t _M0L6_2atmpS1177 = _M0Lm2e2S433;
    int32_t _M0L6_2atmpS1176 = -_M0L6_2atmpS1177;
    int32_t _M0L6_2atmpS1171;
    int32_t _M0L6_2atmpS1175;
    int32_t _M0L6_2atmpS1174;
    int32_t _M0L6_2atmpS1173;
    int32_t _M0L6_2atmpS1172;
    int32_t _M0L1qS455;
    int32_t _M0L6_2atmpS1164;
    int32_t _M0L6_2atmpS1170;
    int32_t _M0L6_2atmpS1169;
    int32_t _M0L1iS456;
    int32_t _M0L6_2atmpS1168;
    int32_t _M0L1kS457;
    int32_t _M0L1jS458;
    struct _M0TPB8Pow5Pair _M0L4pow5S459;
    uint64_t _M0L6_2atmpS1167;
    struct _M0TPB19MulShiftAll64Result _M0L7_2abindS460;
    uint64_t _M0L8_2avrOutS461;
    uint64_t _M0L8_2avpOutS462;
    uint64_t _M0L8_2avmOutS463;
    #line 415 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1171 = _M0FPB9log10Pow5(_M0L6_2atmpS1176);
    _M0L6_2atmpS1175 = _M0Lm2e2S433;
    _M0L6_2atmpS1174 = -_M0L6_2atmpS1175;
    _M0L6_2atmpS1173 = _M0L6_2atmpS1174 > 1;
    #line 415 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1172 = _M0MPC14bool4Bool7to__int(_M0L6_2atmpS1173);
    _M0L1qS455 = _M0L6_2atmpS1171 - _M0L6_2atmpS1172;
    _M0L6_2atmpS1164 = _M0Lm2e2S433;
    _M0Lm3e10S443 = _M0L1qS455 + _M0L6_2atmpS1164;
    _M0L6_2atmpS1170 = _M0Lm2e2S433;
    _M0L6_2atmpS1169 = -_M0L6_2atmpS1170;
    _M0L1iS456 = _M0L6_2atmpS1169 - _M0L1qS455;
    #line 418 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1168 = _M0FPB8pow5bits(_M0L1iS456);
    _M0L1kS457 = _M0L6_2atmpS1168 - 125;
    _M0L1jS458 = _M0L1qS455 - _M0L1kS457;
    #line 420 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L4pow5S459 = _M0FPB19double__computePow5(_M0L1iS456);
    _M0L6_2atmpS1167 = _M0Lm2m2S434;
    #line 421 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L7_2abindS460
    = _M0FPB13mulShiftAll64(_M0L6_2atmpS1167, _M0L4pow5S459, _M0L1jS458, _M0L7mmShiftS439);
    _M0L8_2avrOutS461 = _M0L7_2abindS460.$0;
    _M0L8_2avpOutS462 = _M0L7_2abindS460.$1;
    _M0L8_2avmOutS463 = _M0L7_2abindS460.$2;
    _M0Lm2vrS440 = _M0L8_2avrOutS461;
    _M0Lm2vpS441 = _M0L8_2avpOutS462;
    _M0Lm2vmS442 = _M0L8_2avmOutS463;
    if (_M0L1qS455 <= 1) {
      _M0Lm17vrIsTrailingZerosS445 = 1;
      if (_M0L4evenS437) {
        int32_t _M0L6_2atmpS1165;
        #line 432 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0L6_2atmpS1165 = _M0MPC14bool4Bool7to__int(_M0L7mmShiftS439);
        _M0Lm17vmIsTrailingZerosS444 = _M0L6_2atmpS1165 == 1;
      } else {
        uint64_t _M0L6_2atmpS1166 = _M0Lm2vpS441;
        _M0Lm2vpS441 = _M0L6_2atmpS1166 - 1ull;
      }
    } else if (_M0L1qS455 < 63) {
      #line 437 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0Lm17vrIsTrailingZerosS445
      = _M0FPB18multipleOfPowerOf2(_M0L2mvS438, _M0L1qS455);
    }
  }
  _M0Lm7removedS464 = 0;
  _M0Lm16lastRemovedDigitS465 = 0;
  _M0Lm6outputS466 = 0ull;
  if (_M0Lm17vmIsTrailingZerosS444 || _M0Lm17vrIsTrailingZerosS445) {
    int32_t _if__result_1520;
    uint64_t _M0L6_2atmpS1207;
    uint64_t _M0L6_2atmpS1213;
    uint64_t _M0L6_2atmpS1214;
    int32_t _if__result_1521;
    int32_t _M0L6_2atmpS1210;
    int64_t _M0L6_2atmpS1209;
    uint64_t _M0L6_2atmpS1208;
    while (1) {
      uint64_t _M0L6_2atmpS1190 = _M0Lm2vpS441;
      uint64_t _M0L7vpDiv10S467 = _M0L6_2atmpS1190 / 10ull;
      uint64_t _M0L6_2atmpS1189 = _M0Lm2vmS442;
      uint64_t _M0L7vmDiv10S468 = _M0L6_2atmpS1189 / 10ull;
      uint64_t _M0L6_2atmpS1188;
      int32_t _M0L6_2atmpS1185;
      int32_t _M0L6_2atmpS1187;
      int32_t _M0L6_2atmpS1186;
      int32_t _M0L7vmMod10S470;
      uint64_t _M0L6_2atmpS1184;
      uint64_t _M0L7vrDiv10S471;
      uint64_t _M0L6_2atmpS1183;
      int32_t _M0L6_2atmpS1180;
      int32_t _M0L6_2atmpS1182;
      int32_t _M0L6_2atmpS1181;
      int32_t _M0L7vrMod10S472;
      int32_t _M0L6_2atmpS1179;
      if (_M0L7vpDiv10S467 <= _M0L7vmDiv10S468) {
        break;
      }
      _M0L6_2atmpS1188 = _M0Lm2vmS442;
      _M0L6_2atmpS1185 = (int32_t)_M0L6_2atmpS1188;
      _M0L6_2atmpS1187 = (int32_t)_M0L7vmDiv10S468;
      _M0L6_2atmpS1186 = 10 * _M0L6_2atmpS1187;
      _M0L7vmMod10S470 = _M0L6_2atmpS1185 - _M0L6_2atmpS1186;
      _M0L6_2atmpS1184 = _M0Lm2vrS440;
      _M0L7vrDiv10S471 = _M0L6_2atmpS1184 / 10ull;
      _M0L6_2atmpS1183 = _M0Lm2vrS440;
      _M0L6_2atmpS1180 = (int32_t)_M0L6_2atmpS1183;
      _M0L6_2atmpS1182 = (int32_t)_M0L7vrDiv10S471;
      _M0L6_2atmpS1181 = 10 * _M0L6_2atmpS1182;
      _M0L7vrMod10S472 = _M0L6_2atmpS1180 - _M0L6_2atmpS1181;
      _M0Lm17vmIsTrailingZerosS444
      = _M0Lm17vmIsTrailingZerosS444 && _M0L7vmMod10S470 == 0;
      if (_M0Lm17vrIsTrailingZerosS445) {
        int32_t _M0L6_2atmpS1178 = _M0Lm16lastRemovedDigitS465;
        _M0Lm17vrIsTrailingZerosS445 = _M0L6_2atmpS1178 == 0;
      } else {
        _M0Lm17vrIsTrailingZerosS445 = 0;
      }
      _M0Lm16lastRemovedDigitS465 = _M0L7vrMod10S472;
      _M0Lm2vrS440 = _M0L7vrDiv10S471;
      _M0Lm2vpS441 = _M0L7vpDiv10S467;
      _M0Lm2vmS442 = _M0L7vmDiv10S468;
      _M0L6_2atmpS1179 = _M0Lm7removedS464;
      _M0Lm7removedS464 = _M0L6_2atmpS1179 + 1;
      continue;
      break;
    }
    if (_M0Lm17vmIsTrailingZerosS444) {
      while (1) {
        uint64_t _M0L6_2atmpS1203 = _M0Lm2vmS442;
        uint64_t _M0L7vmDiv10S473 = _M0L6_2atmpS1203 / 10ull;
        uint64_t _M0L6_2atmpS1202 = _M0Lm2vmS442;
        int32_t _M0L6_2atmpS1199 = (int32_t)_M0L6_2atmpS1202;
        int32_t _M0L6_2atmpS1201 = (int32_t)_M0L7vmDiv10S473;
        int32_t _M0L6_2atmpS1200 = 10 * _M0L6_2atmpS1201;
        int32_t _M0L7vmMod10S474 = _M0L6_2atmpS1199 - _M0L6_2atmpS1200;
        uint64_t _M0L6_2atmpS1198;
        uint64_t _M0L7vpDiv10S476;
        uint64_t _M0L6_2atmpS1197;
        uint64_t _M0L7vrDiv10S477;
        uint64_t _M0L6_2atmpS1196;
        int32_t _M0L6_2atmpS1193;
        int32_t _M0L6_2atmpS1195;
        int32_t _M0L6_2atmpS1194;
        int32_t _M0L7vrMod10S478;
        int32_t _M0L6_2atmpS1192;
        if (_M0L7vmMod10S474 != 0) {
          break;
        }
        _M0L6_2atmpS1198 = _M0Lm2vpS441;
        _M0L7vpDiv10S476 = _M0L6_2atmpS1198 / 10ull;
        _M0L6_2atmpS1197 = _M0Lm2vrS440;
        _M0L7vrDiv10S477 = _M0L6_2atmpS1197 / 10ull;
        _M0L6_2atmpS1196 = _M0Lm2vrS440;
        _M0L6_2atmpS1193 = (int32_t)_M0L6_2atmpS1196;
        _M0L6_2atmpS1195 = (int32_t)_M0L7vrDiv10S477;
        _M0L6_2atmpS1194 = 10 * _M0L6_2atmpS1195;
        _M0L7vrMod10S478 = _M0L6_2atmpS1193 - _M0L6_2atmpS1194;
        if (_M0Lm17vrIsTrailingZerosS445) {
          int32_t _M0L6_2atmpS1191 = _M0Lm16lastRemovedDigitS465;
          _M0Lm17vrIsTrailingZerosS445 = _M0L6_2atmpS1191 == 0;
        } else {
          _M0Lm17vrIsTrailingZerosS445 = 0;
        }
        _M0Lm16lastRemovedDigitS465 = _M0L7vrMod10S478;
        _M0Lm2vrS440 = _M0L7vrDiv10S477;
        _M0Lm2vpS441 = _M0L7vpDiv10S476;
        _M0Lm2vmS442 = _M0L7vmDiv10S473;
        _M0L6_2atmpS1192 = _M0Lm7removedS464;
        _M0Lm7removedS464 = _M0L6_2atmpS1192 + 1;
        continue;
        break;
      }
    }
    if (_M0Lm17vrIsTrailingZerosS445) {
      int32_t _M0L6_2atmpS1206 = _M0Lm16lastRemovedDigitS465;
      if (_M0L6_2atmpS1206 == 5) {
        uint64_t _M0L6_2atmpS1205 = _M0Lm2vrS440;
        uint64_t _M0L6_2atmpS1204 = _M0L6_2atmpS1205 % 2ull;
        _if__result_1520 = _M0L6_2atmpS1204 == 0ull;
      } else {
        _if__result_1520 = 0;
      }
    } else {
      _if__result_1520 = 0;
    }
    if (_if__result_1520) {
      _M0Lm16lastRemovedDigitS465 = 4;
    }
    _M0L6_2atmpS1207 = _M0Lm2vrS440;
    _M0L6_2atmpS1213 = _M0Lm2vrS440;
    _M0L6_2atmpS1214 = _M0Lm2vmS442;
    if (_M0L6_2atmpS1213 == _M0L6_2atmpS1214) {
      if (!_M0L4evenS437) {
        _if__result_1521 = 1;
      } else {
        int32_t _M0L6_2atmpS1212 = _M0Lm17vmIsTrailingZerosS444;
        _if__result_1521 = !_M0L6_2atmpS1212;
      }
    } else {
      _if__result_1521 = 0;
    }
    if (_if__result_1521) {
      _M0L6_2atmpS1210 = 1;
    } else {
      int32_t _M0L6_2atmpS1211 = _M0Lm16lastRemovedDigitS465;
      _M0L6_2atmpS1210 = _M0L6_2atmpS1211 >= 5;
    }
    #line 487 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1209 = _M0MPC14bool4Bool9to__int64(_M0L6_2atmpS1210);
    _M0L6_2atmpS1208 = *(uint64_t*)&_M0L6_2atmpS1209;
    _M0Lm6outputS466 = _M0L6_2atmpS1207 + _M0L6_2atmpS1208;
  } else {
    int32_t _M0Lm7roundUpS479 = 0;
    uint64_t _M0L6_2atmpS1235 = _M0Lm2vpS441;
    uint64_t _M0L8vpDiv100S480 = _M0L6_2atmpS1235 / 100ull;
    uint64_t _M0L6_2atmpS1234 = _M0Lm2vmS442;
    uint64_t _M0L8vmDiv100S481 = _M0L6_2atmpS1234 / 100ull;
    uint64_t _M0L6_2atmpS1229;
    uint64_t _M0L6_2atmpS1232;
    uint64_t _M0L6_2atmpS1233;
    int32_t _M0L6_2atmpS1231;
    uint64_t _M0L6_2atmpS1230;
    if (_M0L8vpDiv100S480 > _M0L8vmDiv100S481) {
      uint64_t _M0L6_2atmpS1220 = _M0Lm2vrS440;
      uint64_t _M0L8vrDiv100S482 = _M0L6_2atmpS1220 / 100ull;
      uint64_t _M0L6_2atmpS1219 = _M0Lm2vrS440;
      int32_t _M0L6_2atmpS1216 = (int32_t)_M0L6_2atmpS1219;
      int32_t _M0L6_2atmpS1218 = (int32_t)_M0L8vrDiv100S482;
      int32_t _M0L6_2atmpS1217 = 100 * _M0L6_2atmpS1218;
      int32_t _M0L8vrMod100S483 = _M0L6_2atmpS1216 - _M0L6_2atmpS1217;
      int32_t _M0L6_2atmpS1215;
      _M0Lm7roundUpS479 = _M0L8vrMod100S483 >= 50;
      _M0Lm2vrS440 = _M0L8vrDiv100S482;
      _M0Lm2vpS441 = _M0L8vpDiv100S480;
      _M0Lm2vmS442 = _M0L8vmDiv100S481;
      _M0L6_2atmpS1215 = _M0Lm7removedS464;
      _M0Lm7removedS464 = _M0L6_2atmpS1215 + 2;
    }
    while (1) {
      uint64_t _M0L6_2atmpS1228 = _M0Lm2vpS441;
      uint64_t _M0L7vpDiv10S484 = _M0L6_2atmpS1228 / 10ull;
      uint64_t _M0L6_2atmpS1227 = _M0Lm2vmS442;
      uint64_t _M0L7vmDiv10S485 = _M0L6_2atmpS1227 / 10ull;
      uint64_t _M0L6_2atmpS1226;
      uint64_t _M0L7vrDiv10S487;
      uint64_t _M0L6_2atmpS1225;
      int32_t _M0L6_2atmpS1222;
      int32_t _M0L6_2atmpS1224;
      int32_t _M0L6_2atmpS1223;
      int32_t _M0L7vrMod10S488;
      int32_t _M0L6_2atmpS1221;
      if (_M0L7vpDiv10S484 <= _M0L7vmDiv10S485) {
        break;
      }
      _M0L6_2atmpS1226 = _M0Lm2vrS440;
      _M0L7vrDiv10S487 = _M0L6_2atmpS1226 / 10ull;
      _M0L6_2atmpS1225 = _M0Lm2vrS440;
      _M0L6_2atmpS1222 = (int32_t)_M0L6_2atmpS1225;
      _M0L6_2atmpS1224 = (int32_t)_M0L7vrDiv10S487;
      _M0L6_2atmpS1223 = 10 * _M0L6_2atmpS1224;
      _M0L7vrMod10S488 = _M0L6_2atmpS1222 - _M0L6_2atmpS1223;
      _M0Lm7roundUpS479 = _M0L7vrMod10S488 >= 5;
      _M0Lm2vrS440 = _M0L7vrDiv10S487;
      _M0Lm2vpS441 = _M0L7vpDiv10S484;
      _M0Lm2vmS442 = _M0L7vmDiv10S485;
      _M0L6_2atmpS1221 = _M0Lm7removedS464;
      _M0Lm7removedS464 = _M0L6_2atmpS1221 + 1;
      continue;
      break;
    }
    _M0L6_2atmpS1229 = _M0Lm2vrS440;
    _M0L6_2atmpS1232 = _M0Lm2vrS440;
    _M0L6_2atmpS1233 = _M0Lm2vmS442;
    _M0L6_2atmpS1231
    = _M0L6_2atmpS1232 == _M0L6_2atmpS1233 || _M0Lm7roundUpS479;
    #line 522 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1230 = _M0MPC14bool4Bool10to__uint64(_M0L6_2atmpS1231);
    _M0Lm6outputS466 = _M0L6_2atmpS1229 + _M0L6_2atmpS1230;
  }
  _M0L6_2atmpS1237 = _M0Lm3e10S443;
  _M0L6_2atmpS1238 = _M0Lm7removedS464;
  _M0L3expS489 = _M0L6_2atmpS1237 + _M0L6_2atmpS1238;
  _M0L6_2atmpS1236 = _M0Lm6outputS466;
  _block_1523
  = (struct _M0TPB17FloatingDecimal64*)moonbit_malloc(sizeof(struct _M0TPB17FloatingDecimal64));
  Moonbit_object_header(_block_1523)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_1523->$0 = _M0L6_2atmpS1236;
  _block_1523->$1 = _M0L3expS489;
  return _block_1523;
}

uint64_t _M0MPC14bool4Bool10to__uint64(int32_t _M0L4selfS432) {
  #line 110 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bool.mbt"
  if (_M0L4selfS432) {
    return 1ull;
  } else {
    return 0ull;
  }
}

int64_t _M0MPC14bool4Bool9to__int64(int32_t _M0L4selfS431) {
  #line 58 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bool.mbt"
  if (_M0L4selfS431) {
    return 1ll;
  } else {
    return 0ll;
  }
}

int32_t _M0MPC14bool4Bool7to__int(int32_t _M0L4selfS430) {
  #line 32 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bool.mbt"
  if (_M0L4selfS430) {
    return 1;
  } else {
    return 0;
  }
}

int32_t _M0FPB17decimal__length17(uint64_t _M0L1vS429) {
  #line 280 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  if (_M0L1vS429 >= 10000000000000000ull) {
    return 17;
  }
  if (_M0L1vS429 >= 1000000000000000ull) {
    return 16;
  }
  if (_M0L1vS429 >= 100000000000000ull) {
    return 15;
  }
  if (_M0L1vS429 >= 10000000000000ull) {
    return 14;
  }
  if (_M0L1vS429 >= 1000000000000ull) {
    return 13;
  }
  if (_M0L1vS429 >= 100000000000ull) {
    return 12;
  }
  if (_M0L1vS429 >= 10000000000ull) {
    return 11;
  }
  if (_M0L1vS429 >= 1000000000ull) {
    return 10;
  }
  if (_M0L1vS429 >= 100000000ull) {
    return 9;
  }
  if (_M0L1vS429 >= 10000000ull) {
    return 8;
  }
  if (_M0L1vS429 >= 1000000ull) {
    return 7;
  }
  if (_M0L1vS429 >= 100000ull) {
    return 6;
  }
  if (_M0L1vS429 >= 10000ull) {
    return 5;
  }
  if (_M0L1vS429 >= 1000ull) {
    return 4;
  }
  if (_M0L1vS429 >= 100ull) {
    return 3;
  }
  if (_M0L1vS429 >= 10ull) {
    return 2;
  }
  return 1;
}

struct _M0TPB8Pow5Pair _M0FPB22double__computeInvPow5(int32_t _M0L1iS412) {
  int32_t _M0L6_2atmpS1137;
  int32_t _M0L6_2atmpS1136;
  int32_t _M0L4baseS411;
  int32_t _M0L5base2S413;
  int32_t _M0L6offsetS414;
  int32_t _M0L6_2atmpS1135;
  uint64_t _M0L4mul0S415;
  int32_t _M0L6_2atmpS1134;
  int32_t _M0L6_2atmpS1133;
  uint64_t _M0L4mul1S416;
  uint64_t _M0L1mS417;
  struct _M0TPB7Umul128 _M0L7_2abindS418;
  uint64_t _M0L7_2alow1S419;
  uint64_t _M0L8_2ahigh1S420;
  struct _M0TPB7Umul128 _M0L7_2abindS421;
  uint64_t _M0L7_2alow0S422;
  uint64_t _M0L8_2ahigh0S423;
  uint64_t _M0L3sumS424;
  uint64_t _M0Lm5high1S425;
  int32_t _M0L6_2atmpS1131;
  int32_t _M0L6_2atmpS1132;
  int32_t _M0L5deltaS426;
  uint64_t _M0L6_2atmpS1130;
  uint64_t _M0L6_2atmpS1122;
  int32_t _M0L6_2atmpS1129;
  uint32_t _M0L6_2atmpS1126;
  int32_t _M0L6_2atmpS1128;
  int32_t _M0L6_2atmpS1127;
  uint32_t _M0L6_2atmpS1125;
  uint32_t _M0L6_2atmpS1124;
  uint64_t _M0L6_2atmpS1123;
  uint64_t _M0L1aS427;
  uint64_t _M0L6_2atmpS1121;
  uint64_t _M0L1bS428;
  #line 239 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1137 = _M0L1iS412 + 26;
  _M0L6_2atmpS1136 = _M0L6_2atmpS1137 - 1;
  _M0L4baseS411 = _M0L6_2atmpS1136 / 26;
  _M0L5base2S413 = _M0L4baseS411 * 26;
  _M0L6offsetS414 = _M0L5base2S413 - _M0L1iS412;
  _M0L6_2atmpS1135 = _M0L4baseS411 * 2;
  #line 243 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4mul0S415
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB26gDOUBLE__POW5__INV__SPLIT2, _M0L6_2atmpS1135);
  _M0L6_2atmpS1134 = _M0L4baseS411 * 2;
  _M0L6_2atmpS1133 = _M0L6_2atmpS1134 + 1;
  #line 244 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4mul1S416
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB26gDOUBLE__POW5__INV__SPLIT2, _M0L6_2atmpS1133);
  if (_M0L6offsetS414 == 0) {
    return (struct _M0TPB8Pow5Pair){.$0 = _M0L4mul0S415, .$1 = _M0L4mul1S416};
  }
  #line 248 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L1mS417
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB20gDOUBLE__POW5__TABLE, _M0L6offsetS414);
  #line 249 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS418 = _M0FPB7umul128(_M0L1mS417, _M0L4mul1S416);
  _M0L7_2alow1S419 = _M0L7_2abindS418.$0;
  _M0L8_2ahigh1S420 = _M0L7_2abindS418.$1;
  #line 250 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS421 = _M0FPB7umul128(_M0L1mS417, _M0L4mul0S415);
  _M0L7_2alow0S422 = _M0L7_2abindS421.$0;
  _M0L8_2ahigh0S423 = _M0L7_2abindS421.$1;
  _M0L3sumS424 = _M0L8_2ahigh0S423 + _M0L7_2alow1S419;
  _M0Lm5high1S425 = _M0L8_2ahigh1S420;
  if (_M0L3sumS424 < _M0L8_2ahigh0S423) {
    uint64_t _M0L6_2atmpS1120 = _M0Lm5high1S425;
    _M0Lm5high1S425 = _M0L6_2atmpS1120 + 1ull;
  }
  #line 256 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1131 = _M0FPB8pow5bits(_M0L5base2S413);
  #line 256 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1132 = _M0FPB8pow5bits(_M0L1iS412);
  _M0L5deltaS426 = _M0L6_2atmpS1131 - _M0L6_2atmpS1132;
  #line 257 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1130
  = _M0FPB13shiftright128(_M0L7_2alow0S422, _M0L3sumS424, _M0L5deltaS426);
  _M0L6_2atmpS1122 = _M0L6_2atmpS1130 + 1ull;
  _M0L6_2atmpS1129 = _M0L1iS412 / 16;
  #line 259 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1126
  = _M0MPC15array13ReadOnlyArray2atGjE(_M0FPB19gPOW5__INV__OFFSETS, _M0L6_2atmpS1129);
  _M0L6_2atmpS1128 = _M0L1iS412 % 16;
  _M0L6_2atmpS1127 = _M0L6_2atmpS1128 << 1;
  _M0L6_2atmpS1125 = _M0L6_2atmpS1126 >> (_M0L6_2atmpS1127 & 31);
  _M0L6_2atmpS1124 = _M0L6_2atmpS1125 & 3u;
  #line 259 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1123 = _M0MPC14uint4UInt10to__uint64(_M0L6_2atmpS1124);
  _M0L1aS427 = _M0L6_2atmpS1122 + _M0L6_2atmpS1123;
  _M0L6_2atmpS1121 = _M0Lm5high1S425;
  #line 260 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L1bS428
  = _M0FPB13shiftright128(_M0L3sumS424, _M0L6_2atmpS1121, _M0L5deltaS426);
  return (struct _M0TPB8Pow5Pair){.$0 = _M0L1aS427, .$1 = _M0L1bS428};
}

struct _M0TPB8Pow5Pair _M0FPB19double__computePow5(int32_t _M0L1iS394) {
  int32_t _M0L4baseS393;
  int32_t _M0L5base2S395;
  int32_t _M0L6offsetS396;
  int32_t _M0L6_2atmpS1119;
  uint64_t _M0L4mul0S397;
  int32_t _M0L6_2atmpS1118;
  int32_t _M0L6_2atmpS1117;
  uint64_t _M0L4mul1S398;
  uint64_t _M0L1mS399;
  struct _M0TPB7Umul128 _M0L7_2abindS400;
  uint64_t _M0L7_2alow1S401;
  uint64_t _M0L8_2ahigh1S402;
  struct _M0TPB7Umul128 _M0L7_2abindS403;
  uint64_t _M0L7_2alow0S404;
  uint64_t _M0L8_2ahigh0S405;
  uint64_t _M0L3sumS406;
  uint64_t _M0Lm5high1S407;
  int32_t _M0L6_2atmpS1115;
  int32_t _M0L6_2atmpS1116;
  int32_t _M0L5deltaS408;
  uint64_t _M0L6_2atmpS1107;
  int32_t _M0L6_2atmpS1114;
  uint32_t _M0L6_2atmpS1111;
  int32_t _M0L6_2atmpS1113;
  int32_t _M0L6_2atmpS1112;
  uint32_t _M0L6_2atmpS1110;
  uint32_t _M0L6_2atmpS1109;
  uint64_t _M0L6_2atmpS1108;
  uint64_t _M0L1aS409;
  uint64_t _M0L6_2atmpS1106;
  uint64_t _M0L1bS410;
  #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4baseS393 = _M0L1iS394 / 26;
  _M0L5base2S395 = _M0L4baseS393 * 26;
  _M0L6offsetS396 = _M0L1iS394 - _M0L5base2S395;
  _M0L6_2atmpS1119 = _M0L4baseS393 * 2;
  #line 217 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4mul0S397
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB21gDOUBLE__POW5__SPLIT2, _M0L6_2atmpS1119);
  _M0L6_2atmpS1118 = _M0L4baseS393 * 2;
  _M0L6_2atmpS1117 = _M0L6_2atmpS1118 + 1;
  #line 218 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4mul1S398
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB21gDOUBLE__POW5__SPLIT2, _M0L6_2atmpS1117);
  if (_M0L6offsetS396 == 0) {
    return (struct _M0TPB8Pow5Pair){.$0 = _M0L4mul0S397, .$1 = _M0L4mul1S398};
  }
  #line 222 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L1mS399
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB20gDOUBLE__POW5__TABLE, _M0L6offsetS396);
  #line 223 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS400 = _M0FPB7umul128(_M0L1mS399, _M0L4mul1S398);
  _M0L7_2alow1S401 = _M0L7_2abindS400.$0;
  _M0L8_2ahigh1S402 = _M0L7_2abindS400.$1;
  #line 224 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS403 = _M0FPB7umul128(_M0L1mS399, _M0L4mul0S397);
  _M0L7_2alow0S404 = _M0L7_2abindS403.$0;
  _M0L8_2ahigh0S405 = _M0L7_2abindS403.$1;
  _M0L3sumS406 = _M0L8_2ahigh0S405 + _M0L7_2alow1S401;
  _M0Lm5high1S407 = _M0L8_2ahigh1S402;
  if (_M0L3sumS406 < _M0L8_2ahigh0S405) {
    uint64_t _M0L6_2atmpS1105 = _M0Lm5high1S407;
    _M0Lm5high1S407 = _M0L6_2atmpS1105 + 1ull;
  }
  #line 230 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1115 = _M0FPB8pow5bits(_M0L1iS394);
  #line 230 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1116 = _M0FPB8pow5bits(_M0L5base2S395);
  _M0L5deltaS408 = _M0L6_2atmpS1115 - _M0L6_2atmpS1116;
  #line 231 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1107
  = _M0FPB13shiftright128(_M0L7_2alow0S404, _M0L3sumS406, _M0L5deltaS408);
  _M0L6_2atmpS1114 = _M0L1iS394 / 16;
  #line 232 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1111
  = _M0MPC15array13ReadOnlyArray2atGjE(_M0FPB14gPOW5__OFFSETS, _M0L6_2atmpS1114);
  _M0L6_2atmpS1113 = _M0L1iS394 % 16;
  _M0L6_2atmpS1112 = _M0L6_2atmpS1113 << 1;
  _M0L6_2atmpS1110 = _M0L6_2atmpS1111 >> (_M0L6_2atmpS1112 & 31);
  _M0L6_2atmpS1109 = _M0L6_2atmpS1110 & 3u;
  #line 232 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1108 = _M0MPC14uint4UInt10to__uint64(_M0L6_2atmpS1109);
  _M0L1aS409 = _M0L6_2atmpS1107 + _M0L6_2atmpS1108;
  _M0L6_2atmpS1106 = _M0Lm5high1S407;
  #line 233 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L1bS410
  = _M0FPB13shiftright128(_M0L3sumS406, _M0L6_2atmpS1106, _M0L5deltaS408);
  return (struct _M0TPB8Pow5Pair){.$0 = _M0L1aS409, .$1 = _M0L1bS410};
}

struct _M0TPB19MulShiftAll64Result _M0FPB13mulShiftAll64(
  uint64_t _M0L1mS367,
  struct _M0TPB8Pow5Pair _M0L3mulS364,
  int32_t _M0L1jS380,
  int32_t _M0L7mmShiftS382
) {
  uint64_t _M0L7_2amul0S363;
  uint64_t _M0L7_2amul1S365;
  uint64_t _M0L1mS366;
  struct _M0TPB7Umul128 _M0L7_2abindS368;
  uint64_t _M0L5_2aloS369;
  uint64_t _M0L6_2atmpS370;
  struct _M0TPB7Umul128 _M0L7_2abindS371;
  uint64_t _M0L6_2alo2S372;
  uint64_t _M0L6_2ahi2S373;
  uint64_t _M0L3midS374;
  uint64_t _M0L6_2atmpS1104;
  uint64_t _M0L2hiS375;
  uint64_t _M0L3lo2S376;
  uint64_t _M0L6_2atmpS1102;
  uint64_t _M0L6_2atmpS1103;
  uint64_t _M0L4mid2S377;
  uint64_t _M0L6_2atmpS1101;
  uint64_t _M0L3hi2S378;
  int32_t _M0L6_2atmpS1100;
  int32_t _M0L6_2atmpS1099;
  uint64_t _M0L2vpS379;
  uint64_t _M0Lm2vmS381;
  int32_t _M0L6_2atmpS1098;
  int32_t _M0L6_2atmpS1097;
  uint64_t _M0L2vrS392;
  uint64_t _M0L6_2atmpS1096;
  #line 129 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2amul0S363 = _M0L3mulS364.$0;
  _M0L7_2amul1S365 = _M0L3mulS364.$1;
  _M0L1mS366 = _M0L1mS367 << 1;
  #line 137 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS368 = _M0FPB7umul128(_M0L1mS366, _M0L7_2amul0S363);
  _M0L5_2aloS369 = _M0L7_2abindS368.$0;
  _M0L6_2atmpS370 = _M0L7_2abindS368.$1;
  #line 138 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS371 = _M0FPB7umul128(_M0L1mS366, _M0L7_2amul1S365);
  _M0L6_2alo2S372 = _M0L7_2abindS371.$0;
  _M0L6_2ahi2S373 = _M0L7_2abindS371.$1;
  _M0L3midS374 = _M0L6_2atmpS370 + _M0L6_2alo2S372;
  if (_M0L3midS374 < _M0L6_2atmpS370) {
    _M0L6_2atmpS1104 = 1ull;
  } else {
    _M0L6_2atmpS1104 = 0ull;
  }
  _M0L2hiS375 = _M0L6_2ahi2S373 + _M0L6_2atmpS1104;
  _M0L3lo2S376 = _M0L5_2aloS369 + _M0L7_2amul0S363;
  _M0L6_2atmpS1102 = _M0L3midS374 + _M0L7_2amul1S365;
  if (_M0L3lo2S376 < _M0L5_2aloS369) {
    _M0L6_2atmpS1103 = 1ull;
  } else {
    _M0L6_2atmpS1103 = 0ull;
  }
  _M0L4mid2S377 = _M0L6_2atmpS1102 + _M0L6_2atmpS1103;
  if (_M0L4mid2S377 < _M0L3midS374) {
    _M0L6_2atmpS1101 = 1ull;
  } else {
    _M0L6_2atmpS1101 = 0ull;
  }
  _M0L3hi2S378 = _M0L2hiS375 + _M0L6_2atmpS1101;
  _M0L6_2atmpS1100 = _M0L1jS380 - 64;
  _M0L6_2atmpS1099 = _M0L6_2atmpS1100 - 1;
  #line 144 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L2vpS379
  = _M0FPB13shiftright128(_M0L4mid2S377, _M0L3hi2S378, _M0L6_2atmpS1099);
  _M0Lm2vmS381 = 0ull;
  if (_M0L7mmShiftS382) {
    uint64_t _M0L3lo3S383 = _M0L5_2aloS369 - _M0L7_2amul0S363;
    uint64_t _M0L6_2atmpS1086 = _M0L3midS374 - _M0L7_2amul1S365;
    uint64_t _M0L6_2atmpS1087;
    uint64_t _M0L4mid3S384;
    uint64_t _M0L6_2atmpS1085;
    uint64_t _M0L3hi3S385;
    int32_t _M0L6_2atmpS1084;
    int32_t _M0L6_2atmpS1083;
    if (_M0L5_2aloS369 < _M0L3lo3S383) {
      _M0L6_2atmpS1087 = 1ull;
    } else {
      _M0L6_2atmpS1087 = 0ull;
    }
    _M0L4mid3S384 = _M0L6_2atmpS1086 - _M0L6_2atmpS1087;
    if (_M0L3midS374 < _M0L4mid3S384) {
      _M0L6_2atmpS1085 = 1ull;
    } else {
      _M0L6_2atmpS1085 = 0ull;
    }
    _M0L3hi3S385 = _M0L2hiS375 - _M0L6_2atmpS1085;
    _M0L6_2atmpS1084 = _M0L1jS380 - 64;
    _M0L6_2atmpS1083 = _M0L6_2atmpS1084 - 1;
    #line 150 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0Lm2vmS381
    = _M0FPB13shiftright128(_M0L4mid3S384, _M0L3hi3S385, _M0L6_2atmpS1083);
  } else {
    uint64_t _M0L3lo3S386 = _M0L5_2aloS369 + _M0L5_2aloS369;
    uint64_t _M0L6_2atmpS1094 = _M0L3midS374 + _M0L3midS374;
    uint64_t _M0L6_2atmpS1095;
    uint64_t _M0L4mid3S387;
    uint64_t _M0L6_2atmpS1092;
    uint64_t _M0L6_2atmpS1093;
    uint64_t _M0L3hi3S388;
    uint64_t _M0L3lo4S389;
    uint64_t _M0L6_2atmpS1090;
    uint64_t _M0L6_2atmpS1091;
    uint64_t _M0L4mid4S390;
    uint64_t _M0L6_2atmpS1089;
    uint64_t _M0L3hi4S391;
    int32_t _M0L6_2atmpS1088;
    if (_M0L3lo3S386 < _M0L5_2aloS369) {
      _M0L6_2atmpS1095 = 1ull;
    } else {
      _M0L6_2atmpS1095 = 0ull;
    }
    _M0L4mid3S387 = _M0L6_2atmpS1094 + _M0L6_2atmpS1095;
    _M0L6_2atmpS1092 = _M0L2hiS375 + _M0L2hiS375;
    if (_M0L4mid3S387 < _M0L3midS374) {
      _M0L6_2atmpS1093 = 1ull;
    } else {
      _M0L6_2atmpS1093 = 0ull;
    }
    _M0L3hi3S388 = _M0L6_2atmpS1092 + _M0L6_2atmpS1093;
    _M0L3lo4S389 = _M0L3lo3S386 - _M0L7_2amul0S363;
    _M0L6_2atmpS1090 = _M0L4mid3S387 - _M0L7_2amul1S365;
    if (_M0L3lo3S386 < _M0L3lo4S389) {
      _M0L6_2atmpS1091 = 1ull;
    } else {
      _M0L6_2atmpS1091 = 0ull;
    }
    _M0L4mid4S390 = _M0L6_2atmpS1090 - _M0L6_2atmpS1091;
    if (_M0L4mid3S387 < _M0L4mid4S390) {
      _M0L6_2atmpS1089 = 1ull;
    } else {
      _M0L6_2atmpS1089 = 0ull;
    }
    _M0L3hi4S391 = _M0L3hi3S388 - _M0L6_2atmpS1089;
    _M0L6_2atmpS1088 = _M0L1jS380 - 64;
    #line 158 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0Lm2vmS381
    = _M0FPB13shiftright128(_M0L4mid4S390, _M0L3hi4S391, _M0L6_2atmpS1088);
  }
  _M0L6_2atmpS1098 = _M0L1jS380 - 64;
  _M0L6_2atmpS1097 = _M0L6_2atmpS1098 - 1;
  #line 160 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L2vrS392
  = _M0FPB13shiftright128(_M0L3midS374, _M0L2hiS375, _M0L6_2atmpS1097);
  _M0L6_2atmpS1096 = _M0Lm2vmS381;
  return (struct _M0TPB19MulShiftAll64Result){.$0 = _M0L2vrS392,
                                                .$1 = _M0L2vpS379,
                                                .$2 = _M0L6_2atmpS1096};
}

int32_t _M0FPB18multipleOfPowerOf2(
  uint64_t _M0L5valueS361,
  int32_t _M0L1pS362
) {
  uint64_t _M0L6_2atmpS1082;
  uint64_t _M0L6_2atmpS1081;
  uint64_t _M0L6_2atmpS1080;
  #line 124 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1082 = 1ull << (_M0L1pS362 & 63);
  _M0L6_2atmpS1081 = _M0L6_2atmpS1082 - 1ull;
  _M0L6_2atmpS1080 = _M0L5valueS361 & _M0L6_2atmpS1081;
  return _M0L6_2atmpS1080 == 0ull;
}

int32_t _M0FPB18multipleOfPowerOf5(
  uint64_t _M0L5valueS359,
  int32_t _M0L1pS360
) {
  int32_t _M0L6_2atmpS1079;
  #line 119 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  #line 120 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1079 = _M0FPB10pow5Factor(_M0L5valueS359);
  return _M0L6_2atmpS1079 >= _M0L1pS360;
}

int32_t _M0FPB10pow5Factor(uint64_t _M0L5valueS354) {
  uint64_t _M0L6_2atmpS1070;
  uint64_t _M0L6_2atmpS1071;
  uint64_t _M0L6_2atmpS1072;
  uint64_t _M0L6_2atmpS1073;
  uint64_t _M0L6_2atmpS1078;
  int32_t _M0L5countS355;
  uint64_t _M0L1vS356;
  #line 94 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1070 = _M0L5valueS354 % 5ull;
  if (_M0L6_2atmpS1070 != 0ull) {
    return 0;
  }
  _M0L6_2atmpS1071 = _M0L5valueS354 % 25ull;
  if (_M0L6_2atmpS1071 != 0ull) {
    return 1;
  }
  _M0L6_2atmpS1072 = _M0L5valueS354 % 125ull;
  if (_M0L6_2atmpS1072 != 0ull) {
    return 2;
  }
  _M0L6_2atmpS1073 = _M0L5valueS354 % 625ull;
  if (_M0L6_2atmpS1073 != 0ull) {
    return 3;
  }
  _M0L6_2atmpS1078 = _M0L5valueS354 / 625ull;
  _M0L5countS355 = 4;
  _M0L1vS356 = _M0L6_2atmpS1078;
  while (1) {
    if (_M0L1vS356 > 0ull) {
      uint64_t _M0L6_2atmpS1074 = _M0L1vS356 % 5ull;
      int32_t _M0L6_2atmpS1075;
      uint64_t _M0L6_2atmpS1076;
      if (_M0L6_2atmpS1074 != 0ull) {
        return _M0L5countS355;
      }
      _M0L6_2atmpS1075 = _M0L5countS355 + 1;
      _M0L6_2atmpS1076 = _M0L1vS356 / 5ull;
      _M0L5countS355 = _M0L6_2atmpS1075;
      _M0L1vS356 = _M0L6_2atmpS1076;
      continue;
    } else {
      struct _M0TPB13StringBuilder* _M0L18_2astring__builderS358;
      moonbit_string_t _M0L6_2atmpS1077;
      int32_t _result_1525;
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0L18_2astring__builderS358
      = _M0MPB13StringBuilder21StringBuilder_2einner(25);
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS358, (moonbit_string_t)moonbit_string_literal_3.data);
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0MPB13StringBuilder13write__objectGmE(_M0L18_2astring__builderS358, _M0L5valueS354);
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0L6_2atmpS1077
      = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS358);
      moonbit_decref(_M0L18_2astring__builderS358);
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _result_1525 = _M0FPC15abort5abortGiE(_M0L6_2atmpS1077);
      moonbit_decref(_M0L6_2atmpS1077);
      return _result_1525;
    }
    break;
  }
}

uint64_t _M0FPB13shiftright128(
  uint64_t _M0L2loS353,
  uint64_t _M0L2hiS351,
  int32_t _M0L4distS352
) {
  int32_t _M0L6_2atmpS1069;
  uint64_t _M0L6_2atmpS1067;
  uint64_t _M0L6_2atmpS1068;
  #line 89 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1069 = 64 - _M0L4distS352;
  _M0L6_2atmpS1067 = _M0L2hiS351 << (_M0L6_2atmpS1069 & 63);
  _M0L6_2atmpS1068 = _M0L2loS353 >> (_M0L4distS352 & 63);
  return _M0L6_2atmpS1067 | _M0L6_2atmpS1068;
}

struct _M0TPB7Umul128 _M0FPB7umul128(
  uint64_t _M0L1aS341,
  uint64_t _M0L1bS344
) {
  uint64_t _M0L3aLoS340;
  uint64_t _M0L3aHiS342;
  uint64_t _M0L3bLoS343;
  uint64_t _M0L3bHiS345;
  uint64_t _M0L1xS346;
  uint64_t _M0L6_2atmpS1065;
  uint64_t _M0L6_2atmpS1066;
  uint64_t _M0L1yS347;
  uint64_t _M0L6_2atmpS1063;
  uint64_t _M0L6_2atmpS1064;
  uint64_t _M0L1zS348;
  uint64_t _M0L6_2atmpS1061;
  uint64_t _M0L6_2atmpS1062;
  uint64_t _M0L6_2atmpS1059;
  uint64_t _M0L6_2atmpS1060;
  uint64_t _M0L1wS349;
  uint64_t _M0L2loS350;
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L3aLoS340 = _M0L1aS341 & 4294967295ull;
  _M0L3aHiS342 = _M0L1aS341 >> 32;
  _M0L3bLoS343 = _M0L1bS344 & 4294967295ull;
  _M0L3bHiS345 = _M0L1bS344 >> 32;
  _M0L1xS346 = _M0L3aLoS340 * _M0L3bLoS343;
  _M0L6_2atmpS1065 = _M0L3aHiS342 * _M0L3bLoS343;
  _M0L6_2atmpS1066 = _M0L1xS346 >> 32;
  _M0L1yS347 = _M0L6_2atmpS1065 + _M0L6_2atmpS1066;
  _M0L6_2atmpS1063 = _M0L3aLoS340 * _M0L3bHiS345;
  _M0L6_2atmpS1064 = _M0L1yS347 & 4294967295ull;
  _M0L1zS348 = _M0L6_2atmpS1063 + _M0L6_2atmpS1064;
  _M0L6_2atmpS1061 = _M0L3aHiS342 * _M0L3bHiS345;
  _M0L6_2atmpS1062 = _M0L1yS347 >> 32;
  _M0L6_2atmpS1059 = _M0L6_2atmpS1061 + _M0L6_2atmpS1062;
  _M0L6_2atmpS1060 = _M0L1zS348 >> 32;
  _M0L1wS349 = _M0L6_2atmpS1059 + _M0L6_2atmpS1060;
  _M0L2loS350 = _M0L1aS341 * _M0L1bS344;
  return (struct _M0TPB7Umul128){.$0 = _M0L2loS350, .$1 = _M0L1wS349};
}

moonbit_string_t _M0FPB19string__from__bytes(
  moonbit_bytes_t _M0L5bytesS338,
  int32_t _M0L4fromS335,
  int32_t _M0L2toS334
) {
  int32_t _M0L3lenS333;
  int32_t _M0L6_2atmpS1058;
  uint16_t* _M0L6bufferS336;
  int32_t _M0L1iS337;
  #line 52 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L3lenS333 = _M0L2toS334 - _M0L4fromS335;
  #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1058 = _M0IPC16uint166UInt16PB7Default7default();
  _M0L6bufferS336
  = (uint16_t*)moonbit_make_string(_M0L3lenS333, _M0L6_2atmpS1058);
  _M0L1iS337 = 0;
  while (1) {
    if (_M0L1iS337 < _M0L3lenS333) {
      int32_t _M0L6_2atmpS1056 = _M0L4fromS335 + _M0L1iS337;
      int32_t _M0L6_2atmpS1055;
      int32_t _M0L6_2atmpS1054;
      int32_t _M0L6_2atmpS1057;
      if (
        _M0L6_2atmpS1056 < 0
        || _M0L6_2atmpS1056 >= Moonbit_array_length(_M0L5bytesS338)
      ) {
        #line 56 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6_2atmpS1055 = (int32_t)_M0L5bytesS338[_M0L6_2atmpS1056];
      _M0L6_2atmpS1054 = (uint16_t)_M0L6_2atmpS1055;
      if (
        _M0L1iS337 < 0 || _M0L1iS337 >= Moonbit_array_length(_M0L6bufferS336)
      ) {
        #line 56 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6bufferS336[_M0L1iS337] = _M0L6_2atmpS1054;
      _M0L6_2atmpS1057 = _M0L1iS337 + 1;
      _M0L1iS337 = _M0L6_2atmpS1057;
      continue;
    }
    break;
  }
  return _M0L6bufferS336;
}

int32_t _M0FPB9log10Pow2(int32_t _M0L1eS332) {
  int32_t _M0L6_2atmpS1053;
  uint32_t _M0L6_2atmpS1052;
  uint32_t _M0L6_2atmpS1051;
  #line 44 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1053 = _M0L1eS332 * 78913;
  _M0L6_2atmpS1052 = *(uint32_t*)&_M0L6_2atmpS1053;
  _M0L6_2atmpS1051 = _M0L6_2atmpS1052 >> 18;
  return *(int32_t*)&_M0L6_2atmpS1051;
}

int32_t _M0FPB9log10Pow5(int32_t _M0L1eS331) {
  int32_t _M0L6_2atmpS1050;
  uint32_t _M0L6_2atmpS1049;
  uint32_t _M0L6_2atmpS1048;
  #line 37 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1050 = _M0L1eS331 * 732923;
  _M0L6_2atmpS1049 = *(uint32_t*)&_M0L6_2atmpS1050;
  _M0L6_2atmpS1048 = _M0L6_2atmpS1049 >> 20;
  return *(int32_t*)&_M0L6_2atmpS1048;
}

moonbit_string_t _M0FPB18copy__special__str(
  int32_t _M0L4signS329,
  int32_t _M0L8exponentS330,
  int32_t _M0L8mantissaS327
) {
  moonbit_string_t _M0L1sS328;
  moonbit_string_t _result_1528;
  #line 23 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  if (_M0L8mantissaS327) {
    return (moonbit_string_t)moonbit_string_literal_4.data;
  }
  if (_M0L4signS329) {
    _M0L1sS328 = (moonbit_string_t)moonbit_string_literal_5.data;
  } else {
    _M0L1sS328 = (moonbit_string_t)moonbit_string_literal_6.data;
  }
  if (_M0L8exponentS330) {
    moonbit_string_t _result_1527;
    #line 29 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _result_1527
    = moonbit_add_string(_M0L1sS328, (moonbit_string_t)moonbit_string_literal_7.data);
    moonbit_decref(_M0L1sS328);
    return _result_1527;
  }
  #line 31 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _result_1528
  = moonbit_add_string(_M0L1sS328, (moonbit_string_t)moonbit_string_literal_8.data);
  moonbit_decref(_M0L1sS328);
  return _result_1528;
}

int32_t _M0FPB8pow5bits(int32_t _M0L1eS326) {
  int32_t _M0L6_2atmpS1047;
  uint32_t _M0L6_2atmpS1046;
  uint32_t _M0L6_2atmpS1045;
  int32_t _M0L6_2atmpS1044;
  #line 18 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1047 = _M0L1eS326 * 1217359;
  _M0L6_2atmpS1046 = *(uint32_t*)&_M0L6_2atmpS1047;
  _M0L6_2atmpS1045 = _M0L6_2atmpS1046 >> 19;
  _M0L6_2atmpS1044 = *(int32_t*)&_M0L6_2atmpS1045;
  return _M0L6_2atmpS1044 + 1;
}

int32_t _M0MPC16double6Double7to__int(double _M0L4selfS325) {
  #line 43 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_to_int.mbt"
  if (_M0L4selfS325 != _M0L4selfS325) {
    return 0;
  } else if (_M0L4selfS325 >= 0x1.fffffffcp+30) {
    return 2147483647;
  } else if (_M0L4selfS325 <= -0x1p+31) {
    return (int32_t)0x80000000;
  } else {
    return (int32_t)_M0L4selfS325;
  }
}

int64_t _M0MPC16double6Double9to__int64(double _M0L4selfS324) {
  #line 46 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_to_int64_native.mbt"
  if (_M0L4selfS324 != _M0L4selfS324) {
    return 0ll;
  } else if (_M0L4selfS324 >= 0x1p+63) {
    return 9223372036854775807ll;
  } else if (_M0L4selfS324 <= -0x1p+63) {
    return (int64_t)0x8000000000000000ll;
  } else {
    return (int64_t)_M0L4selfS324;
  }
}

struct _M0TPB5ArrayGfE* _M0MPC15array5Array20unsafe__make__uninitGfE(
  int32_t _M0L3lenS323
) {
  float* _M0L6_2atmpS1043;
  struct _M0TPB5ArrayGfE* _block_1529;
  #line 30 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1043 = (float*)moonbit_make_float_array_raw(_M0L3lenS323);
  _block_1529
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_block_1529)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 24, 0);
  _block_1529->$0 = _M0L6_2atmpS1043;
  _block_1529->$1 = _M0L3lenS323;
  return _block_1529;
}

uint64_t _M0MPC15array13ReadOnlyArray2atGmE(
  uint64_t* _M0L4selfS319,
  int32_t _M0L5indexS320
) {
  uint64_t* _M0L6_2atmpS1041;
  #line 38 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\readonlyarray.mbt"
  _M0L6_2atmpS1041 = _M0L4selfS319;
  if (
    _M0L5indexS320 < 0
    || _M0L5indexS320 >= Moonbit_array_length(_M0L6_2atmpS1041)
  ) {
    #line 40 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\readonlyarray.mbt"
    moonbit_panic();
  }
  return (uint64_t)_M0L6_2atmpS1041[_M0L5indexS320];
}

uint32_t _M0MPC15array13ReadOnlyArray2atGjE(
  uint32_t* _M0L4selfS321,
  int32_t _M0L5indexS322
) {
  uint32_t* _M0L6_2atmpS1042;
  #line 38 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\readonlyarray.mbt"
  _M0L6_2atmpS1042 = _M0L4selfS321;
  if (
    _M0L5indexS322 < 0
    || _M0L5indexS322 >= Moonbit_array_length(_M0L6_2atmpS1042)
  ) {
    #line 40 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\readonlyarray.mbt"
    moonbit_panic();
  }
  return (uint32_t)_M0L6_2atmpS1042[_M0L5indexS322];
}

moonbit_string_t _M0IPC16uint646UInt64PB4Show10to__string(
  uint64_t _M0L4selfS318
) {
  #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  #line 51 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  return _M0MPC16uint646UInt6418to__string_2einner(_M0L4selfS318, 10);
}

moonbit_string_t _M0IPC13int3IntPB4Show10to__string(int32_t _M0L4selfS317) {
  #line 35 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  #line 36 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  return _M0MPC13int3Int18to__string_2einner(_M0L4selfS317, 10);
}

moonbit_string_t _M0IPC14bool4BoolPB4Show10to__string(int32_t _M0L4selfS316) {
  #line 26 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  if (_M0L4selfS316) {
    return (moonbit_string_t)moonbit_string_literal_9.data;
  } else {
    return (moonbit_string_t)moonbit_string_literal_10.data;
  }
}

uint64_t _M0MPC14uint4UInt10to__uint64(uint32_t _M0L4selfS315) {
  #line 2576 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  return (uint64_t)_M0L4selfS315;
}

int32_t _M0MPC15array5Array4pushGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS312,
  int32_t _M0L5valueS314
) {
  int32_t _M0L3lenS1034;
  int32_t* _M0L6_2atmpS1036;
  int32_t _M0L6_2atmpS1035;
  int32_t _M0L6lengthS313;
  int32_t* _M0L3bufS1039;
  int32_t _M0L6_2atmpS1040;
  #line 406 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L3lenS1034 = _M0L4selfS312->$1;
  #line 408 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1036 = _M0MPC15array5Array6bufferGiE(_M0L4selfS312);
  _M0L6_2atmpS1035 = Moonbit_array_length(_M0L6_2atmpS1036);
  moonbit_decref(_M0L6_2atmpS1036);
  if (_M0L3lenS1034 == _M0L6_2atmpS1035) {
    int32_t _M0L3lenS1038 = _M0L4selfS312->$1;
    int32_t _M0L6_2atmpS1037 = _M0L3lenS1038 + 1;
    #line 409 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0MPC15array5Array7reallocGiE(_M0L4selfS312, _M0L6_2atmpS1037);
  }
  _M0L6lengthS313 = _M0L4selfS312->$1;
  _M0L3bufS1039 = _M0L4selfS312->$0;
  _M0L3bufS1039[_M0L6lengthS313] = _M0L5valueS314;
  _M0L6_2atmpS1040 = _M0L6lengthS313 + 1;
  _M0L4selfS312->$1 = _M0L6_2atmpS1040;
  return 0;
}

int32_t _M0MPC15array5Array7reallocGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS309,
  int32_t _M0L8requiredS311
) {
  int32_t _M0L8old__capS308;
  int32_t _M0L3lenS1033;
  int32_t _M0L8new__capS310;
  #line 302 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  #line 304 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__capS308 = _M0MPC15array5Array8capacityGiE(_M0L4selfS309);
  _M0L3lenS1033 = _M0L4selfS309->$1;
  #line 305 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__capS310
  = _M0FPB23array__growth__capacity(_M0L8old__capS308, _M0L3lenS1033, _M0L8requiredS311);
  #line 306 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0MPC15array5Array14resize__bufferGiE(_M0L4selfS309, _M0L8new__capS310);
  return 0;
}

int32_t _M0MPC15array5Array14resize__bufferGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS303,
  int32_t _M0L13new__capacityS306
) {
  int32_t* _M0L8old__bufS302;
  int32_t _M0L3lenS304;
  int32_t _M0L9copy__lenS305;
  int32_t* _M0L8new__bufS307;
  int32_t* _M0L6_2aoldS1479;
  #line 242 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__bufS302 = _M0L4selfS303->$0;
  _M0L3lenS304 = _M0L4selfS303->$1;
  if (_M0L3lenS304 < _M0L13new__capacityS306) {
    _M0L9copy__lenS305 = _M0L3lenS304;
  } else {
    _M0L9copy__lenS305 = _M0L13new__capacityS306;
  }
  moonbit_incref(_M0L8old__bufS302);
  #line 249 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__bufS307
  = _M0MPB18UninitializedArray23make__and__blit_2einnerGiE(_M0L8old__bufS302, _M0L13new__capacityS306, _M0L9copy__lenS305, 0, 0);
  _M0L6_2aoldS1479 = _M0L4selfS303->$0;
  moonbit_decref(_M0L6_2aoldS1479);
  _M0L4selfS303->$0 = _M0L8new__bufS307;
  return 0;
}

int32_t _M0MPC15array5Array8capacityGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS301
) {
  int32_t* _M0L6_2atmpS1032;
  int32_t _result_1530;
  #line 132 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L6_2atmpS1032 = _M0MPC15array5Array6bufferGiE(_M0L4selfS301);
  _result_1530 = Moonbit_array_length(_M0L6_2atmpS1032);
  moonbit_decref(_M0L6_2atmpS1032);
  return _result_1530;
}

int32_t _M0FPB23array__growth__capacity(
  int32_t _M0L7currentS297,
  int32_t _M0L3lenS295,
  int32_t _M0L8requiredS294
) {
  int32_t _M0L5startS296;
  int32_t _M0L5spaceS298;
  #line 200 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  if (_M0L8requiredS294 < _M0L3lenS295) {
    #line 203 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_11.data);
  }
  if (_M0L7currentS297 == 0) {
    _M0L5startS296 = 8;
  } else {
    _M0L5startS296 = _M0L7currentS297;
  }
  _M0L5spaceS298 = _M0L5startS296;
  while (1) {
    if (_M0L5spaceS298 < _M0L8requiredS294) {
      int32_t _M0L4nextS299 = _M0L5spaceS298 * 2;
      if (_M0L4nextS299 <= _M0L5spaceS298) {
        return _M0L8requiredS294;
      }
      _M0L5spaceS298 = _M0L4nextS299;
      continue;
    } else {
      return _M0L5spaceS298;
    }
    break;
  }
}

int32_t _M0MPC15array5Array6lengthGiE(struct _M0TPB5ArrayGiE* _M0L4selfS291) {
  #line 147 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  return _M0L4selfS291->$1;
}

int32_t _M0MPC15array5Array6lengthGRP26RiantR8snn__mbt8ReceptorE(
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt8ReceptorE* _M0L4selfS292
) {
  #line 147 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  return _M0L4selfS292->$1;
}

int32_t _M0MPC15array5Array6lengthGfE(struct _M0TPB5ArrayGfE* _M0L4selfS293) {
  #line 147 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  return _M0L4selfS293->$1;
}

int32_t* _M0MPC15array5Array6bufferGiE(struct _M0TPB5ArrayGiE* _M0L4selfS288) {
  int32_t* _M0L8_2afieldS1480;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS1480 = _M0L4selfS288->$0;
  moonbit_incref(_M0L8_2afieldS1480);
  return _M0L8_2afieldS1480;
}

float* _M0MPC15array5Array6bufferGfE(struct _M0TPB5ArrayGfE* _M0L4selfS289) {
  float* _M0L8_2afieldS1481;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS1481 = _M0L4selfS289->$0;
  moonbit_incref(_M0L8_2afieldS1481);
  return _M0L8_2afieldS1481;
}

struct _M0TP26RiantR8snn__mbt8Receptor** _M0MPC15array5Array6bufferGRP26RiantR8snn__mbt8ReceptorE(
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt8ReceptorE* _M0L4selfS290
) {
  struct _M0TP26RiantR8snn__mbt8Receptor** _M0L8_2afieldS1482;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS1482 = _M0L4selfS290->$0;
  moonbit_incref(_M0L8_2afieldS1482);
  return _M0L8_2afieldS1482;
}

moonbit_string_t _M0IPC16string6StringPB4Show10to__string(
  moonbit_string_t _M0L4selfS287
) {
  #line 222 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  moonbit_incref(_M0L4selfS287);
  return _M0L4selfS287;
}

int32_t _M0IPB13StringBuilderPB6Logger11write__view(
  struct _M0TPB13StringBuilder* _M0L4selfS286,
  struct _M0TPC16string10StringView _M0L3strS284
) {
  int32_t _M0L3endS1030;
  int32_t _M0L5startS1031;
  int32_t _M0L8str__lenS283;
  int32_t _M0L3lenS1029;
  int32_t _M0L8requiredS285;
  uint16_t* _M0L4dataS1022;
  int32_t _M0L6_2atmpS1021;
  int32_t _if__result_1532;
  uint16_t* _M0L4dataS1023;
  int32_t _M0L3lenS1024;
  moonbit_string_t _M0L6_2atmpS1025;
  int32_t _M0L6_2atmpS1026;
  int32_t _M0L3lenS1028;
  int32_t _M0L6_2atmpS1027;
  #line 158 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L3endS1030 = _M0L3strS284.$2;
  _M0L5startS1031 = _M0L3strS284.$1;
  _M0L8str__lenS283 = _M0L3endS1030 - _M0L5startS1031;
  if (_M0L8str__lenS283 == 0) {
    return 0;
  }
  _M0L3lenS1029 = _M0L4selfS286->$1;
  _M0L8requiredS285 = _M0L3lenS1029 + _M0L8str__lenS283;
  _M0L4dataS1022 = _M0L4selfS286->$0;
  _M0L6_2atmpS1021 = Moonbit_array_length(_M0L4dataS1022);
  if (_M0L8requiredS285 > _M0L6_2atmpS1021) {
    _if__result_1532 = 1;
  } else {
    int32_t _M0L3lenS1020 = _M0L4selfS286->$1;
    _if__result_1532 = _M0L8requiredS285 < _M0L3lenS1020;
  }
  if (_if__result_1532) {
    #line 168 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0MPB13StringBuilder4grow(_M0L4selfS286, _M0L8requiredS285);
  }
  _M0L4dataS1023 = _M0L4selfS286->$0;
  _M0L3lenS1024 = _M0L4selfS286->$1;
  moonbit_incref(_M0L4dataS1023);
  #line 172 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L6_2atmpS1025 = _M0MPC16string10StringView4data(_M0L3strS284);
  #line 173 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L6_2atmpS1026 = _M0MPC16string10StringView13start__offset(_M0L3strS284);
  #line 170 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0MPC15array10FixedArray26unsafe__blit__from__string(_M0L4dataS1023, _M0L3lenS1024, _M0L6_2atmpS1025, _M0L6_2atmpS1026, _M0L8str__lenS283);
  moonbit_decref(_M0L4dataS1023);
  moonbit_decref(_M0L6_2atmpS1025);
  _M0L3lenS1028 = _M0L4selfS286->$1;
  _M0L6_2atmpS1027 = _M0L3lenS1028 + _M0L8str__lenS283;
  _M0L4selfS286->$1 = _M0L6_2atmpS1027;
  return 0;
}

moonbit_string_t _M0MPC16uint646UInt6418to__string_2einner(
  uint64_t _M0L4selfS275,
  int32_t _M0L5radixS274
) {
  uint16_t* _M0L6bufferS276;
  #line 607 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  if (_M0L5radixS274 < 2 || _M0L5radixS274 > 36) {
    #line 611 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_12.data);
  }
  if (_M0L4selfS275 == 0ull) {
    return (moonbit_string_t)moonbit_string_literal_2.data;
  }
  switch (_M0L5radixS274) {
    case 10: {
      int32_t _M0L3lenS277;
      uint16_t* _M0L6bufferS278;
      #line 622 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L3lenS277 = _M0FPB12dec__count64(_M0L4selfS275);
      _M0L6bufferS278 = (uint16_t*)moonbit_make_string(_M0L3lenS277, 0);
      #line 624 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0FPB22int64__to__string__dec(_M0L6bufferS278, _M0L4selfS275, 0, _M0L3lenS277);
      _M0L6bufferS276 = _M0L6bufferS278;
      break;
    }
    
    case 16: {
      int32_t _M0L3lenS279;
      uint16_t* _M0L6bufferS280;
      #line 628 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L3lenS279 = _M0FPB12hex__count64(_M0L4selfS275);
      _M0L6bufferS280 = (uint16_t*)moonbit_make_string(_M0L3lenS279, 0);
      #line 630 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0FPB22int64__to__string__hex(_M0L6bufferS280, _M0L4selfS275, 0, _M0L3lenS279);
      _M0L6bufferS276 = _M0L6bufferS280;
      break;
    }
    default: {
      int32_t _M0L3lenS281;
      uint16_t* _M0L6bufferS282;
      #line 634 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L3lenS281 = _M0FPB14radix__count64(_M0L4selfS275, _M0L5radixS274);
      _M0L6bufferS282 = (uint16_t*)moonbit_make_string(_M0L3lenS281, 0);
      #line 636 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0FPB26int64__to__string__generic(_M0L6bufferS282, _M0L4selfS275, 0, _M0L3lenS281, _M0L5radixS274);
      _M0L6bufferS276 = _M0L6bufferS282;
      break;
    }
  }
  return _M0L6bufferS276;
}

moonbit_string_t _M0MPC15int645Int6418to__string_2einner(
  int64_t _M0L4selfS258,
  int32_t _M0L5radixS257
) {
  int32_t _M0L12is__negativeS259;
  uint64_t _M0L3numS260;
  uint16_t* _M0L6bufferS261;
  #line 548 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  if (_M0L5radixS257 < 2 || _M0L5radixS257 > 36) {
    #line 552 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_12.data);
  }
  if (_M0L4selfS258 == 0ll) {
    return (moonbit_string_t)moonbit_string_literal_2.data;
  }
  _M0L12is__negativeS259 = _M0L4selfS258 < 0ll;
  if (_M0L12is__negativeS259) {
    int64_t _M0L6_2atmpS1019 = -_M0L4selfS258;
    _M0L3numS260 = *(uint64_t*)&_M0L6_2atmpS1019;
  } else {
    _M0L3numS260 = *(uint64_t*)&_M0L4selfS258;
  }
  switch (_M0L5radixS257) {
    case 10: {
      int32_t _M0L10digit__lenS262;
      int32_t _M0L6_2atmpS1016;
      int32_t _M0L10total__lenS263;
      uint16_t* _M0L6bufferS264;
      int32_t _M0L12digit__startS265;
      #line 573 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS262 = _M0FPB12dec__count64(_M0L3numS260);
      if (_M0L12is__negativeS259) {
        _M0L6_2atmpS1016 = 1;
      } else {
        _M0L6_2atmpS1016 = 0;
      }
      _M0L10total__lenS263 = _M0L10digit__lenS262 + _M0L6_2atmpS1016;
      _M0L6bufferS264
      = (uint16_t*)moonbit_make_string(_M0L10total__lenS263, 0);
      if (_M0L12is__negativeS259) {
        _M0L12digit__startS265 = 1;
      } else {
        _M0L12digit__startS265 = 0;
      }
      #line 577 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0FPB22int64__to__string__dec(_M0L6bufferS264, _M0L3numS260, _M0L12digit__startS265, _M0L10total__lenS263);
      _M0L6bufferS261 = _M0L6bufferS264;
      break;
    }
    
    case 16: {
      int32_t _M0L10digit__lenS266;
      int32_t _M0L6_2atmpS1017;
      int32_t _M0L10total__lenS267;
      uint16_t* _M0L6bufferS268;
      int32_t _M0L12digit__startS269;
      #line 581 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS266 = _M0FPB12hex__count64(_M0L3numS260);
      if (_M0L12is__negativeS259) {
        _M0L6_2atmpS1017 = 1;
      } else {
        _M0L6_2atmpS1017 = 0;
      }
      _M0L10total__lenS267 = _M0L10digit__lenS266 + _M0L6_2atmpS1017;
      _M0L6bufferS268
      = (uint16_t*)moonbit_make_string(_M0L10total__lenS267, 0);
      if (_M0L12is__negativeS259) {
        _M0L12digit__startS269 = 1;
      } else {
        _M0L12digit__startS269 = 0;
      }
      #line 585 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0FPB22int64__to__string__hex(_M0L6bufferS268, _M0L3numS260, _M0L12digit__startS269, _M0L10total__lenS267);
      _M0L6bufferS261 = _M0L6bufferS268;
      break;
    }
    default: {
      int32_t _M0L10digit__lenS270;
      int32_t _M0L6_2atmpS1018;
      int32_t _M0L10total__lenS271;
      uint16_t* _M0L6bufferS272;
      int32_t _M0L12digit__startS273;
      #line 589 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS270
      = _M0FPB14radix__count64(_M0L3numS260, _M0L5radixS257);
      if (_M0L12is__negativeS259) {
        _M0L6_2atmpS1018 = 1;
      } else {
        _M0L6_2atmpS1018 = 0;
      }
      _M0L10total__lenS271 = _M0L10digit__lenS270 + _M0L6_2atmpS1018;
      _M0L6bufferS272
      = (uint16_t*)moonbit_make_string(_M0L10total__lenS271, 0);
      if (_M0L12is__negativeS259) {
        _M0L12digit__startS273 = 1;
      } else {
        _M0L12digit__startS273 = 0;
      }
      #line 593 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0FPB26int64__to__string__generic(_M0L6bufferS272, _M0L3numS260, _M0L12digit__startS273, _M0L10total__lenS271, _M0L5radixS257);
      _M0L6bufferS261 = _M0L6bufferS272;
      break;
    }
  }
  if (_M0L12is__negativeS259) {
    _M0L6bufferS261[0] = 45;
  }
  return _M0L6bufferS261;
}

int32_t _M0FPB22int64__to__string__dec(
  uint16_t* _M0L6bufferS243,
  uint64_t _M0L3numS255,
  int32_t _M0L12digit__startS244,
  int32_t _M0L10total__lenS256
) {
  int32_t _M0L6_2atmpS1015;
  uint64_t _M0L3numS233;
  int32_t _M0L6offsetS234;
  #line 493 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L6_2atmpS1015 = _M0L10total__lenS256 - _M0L12digit__startS244;
  _M0L3numS233 = _M0L3numS255;
  _M0L6offsetS234 = _M0L6_2atmpS1015;
  while (1) {
    if (_M0L3numS233 >= 10000ull) {
      uint64_t _M0L1tS235 = _M0L3numS233 / 10000ull;
      uint64_t _M0L6_2atmpS992 = _M0L3numS233 % 10000ull;
      int32_t _M0L1rS236 = (int32_t)_M0L6_2atmpS992;
      int32_t _M0L2d1S237 = _M0L1rS236 / 100;
      int32_t _M0L2d2S238 = _M0L1rS236 % 100;
      int32_t _M0L6_2atmpS991 = _M0L2d1S237 / 10;
      int32_t _M0L6_2atmpS990 = 48 + _M0L6_2atmpS991;
      int32_t _M0L6d1__hiS239 = (uint16_t)_M0L6_2atmpS990;
      int32_t _M0L6_2atmpS989 = _M0L2d1S237 % 10;
      int32_t _M0L6_2atmpS988 = 48 + _M0L6_2atmpS989;
      int32_t _M0L6d1__loS240 = (uint16_t)_M0L6_2atmpS988;
      int32_t _M0L6_2atmpS987 = _M0L2d2S238 / 10;
      int32_t _M0L6_2atmpS986 = 48 + _M0L6_2atmpS987;
      int32_t _M0L6d2__hiS241 = (uint16_t)_M0L6_2atmpS986;
      int32_t _M0L6_2atmpS985 = _M0L2d2S238 % 10;
      int32_t _M0L6_2atmpS984 = 48 + _M0L6_2atmpS985;
      int32_t _M0L6d2__loS242 = (uint16_t)_M0L6_2atmpS984;
      int32_t _M0L6_2atmpS976 = _M0L12digit__startS244 + _M0L6offsetS234;
      int32_t _M0L6_2atmpS975 = _M0L6_2atmpS976 - 4;
      int32_t _M0L6_2atmpS978;
      int32_t _M0L6_2atmpS977;
      int32_t _M0L6_2atmpS980;
      int32_t _M0L6_2atmpS979;
      int32_t _M0L6_2atmpS982;
      int32_t _M0L6_2atmpS981;
      int32_t _M0L6_2atmpS983;
      _M0L6bufferS243[_M0L6_2atmpS975] = _M0L6d1__hiS239;
      _M0L6_2atmpS978 = _M0L12digit__startS244 + _M0L6offsetS234;
      _M0L6_2atmpS977 = _M0L6_2atmpS978 - 3;
      _M0L6bufferS243[_M0L6_2atmpS977] = _M0L6d1__loS240;
      _M0L6_2atmpS980 = _M0L12digit__startS244 + _M0L6offsetS234;
      _M0L6_2atmpS979 = _M0L6_2atmpS980 - 2;
      _M0L6bufferS243[_M0L6_2atmpS979] = _M0L6d2__hiS241;
      _M0L6_2atmpS982 = _M0L12digit__startS244 + _M0L6offsetS234;
      _M0L6_2atmpS981 = _M0L6_2atmpS982 - 1;
      _M0L6bufferS243[_M0L6_2atmpS981] = _M0L6d2__loS242;
      _M0L6_2atmpS983 = _M0L6offsetS234 - 4;
      _M0L3numS233 = _M0L1tS235;
      _M0L6offsetS234 = _M0L6_2atmpS983;
      continue;
    } else {
      int32_t _M0L6_2atmpS1014 = (int32_t)_M0L3numS233;
      int32_t _M0L9remainingS246 = _M0L6_2atmpS1014;
      int32_t _M0L6offsetS247 = _M0L6offsetS234;
      while (1) {
        if (_M0L9remainingS246 >= 100) {
          int32_t _M0L1tS248 = _M0L9remainingS246 / 100;
          int32_t _M0L1dS249 = _M0L9remainingS246 % 100;
          int32_t _M0L6_2atmpS1001 = _M0L1dS249 / 10;
          int32_t _M0L6_2atmpS1000 = 48 + _M0L6_2atmpS1001;
          int32_t _M0L5d__hiS250 = (uint16_t)_M0L6_2atmpS1000;
          int32_t _M0L6_2atmpS999 = _M0L1dS249 % 10;
          int32_t _M0L6_2atmpS998 = 48 + _M0L6_2atmpS999;
          int32_t _M0L5d__loS251 = (uint16_t)_M0L6_2atmpS998;
          int32_t _M0L6_2atmpS994 = _M0L12digit__startS244 + _M0L6offsetS247;
          int32_t _M0L6_2atmpS993 = _M0L6_2atmpS994 - 2;
          int32_t _M0L6_2atmpS996;
          int32_t _M0L6_2atmpS995;
          int32_t _M0L6_2atmpS997;
          _M0L6bufferS243[_M0L6_2atmpS993] = _M0L5d__hiS250;
          _M0L6_2atmpS996 = _M0L12digit__startS244 + _M0L6offsetS247;
          _M0L6_2atmpS995 = _M0L6_2atmpS996 - 1;
          _M0L6bufferS243[_M0L6_2atmpS995] = _M0L5d__loS251;
          _M0L6_2atmpS997 = _M0L6offsetS247 - 2;
          _M0L9remainingS246 = _M0L1tS248;
          _M0L6offsetS247 = _M0L6_2atmpS997;
          continue;
        } else if (_M0L9remainingS246 >= 10) {
          int32_t _M0L6_2atmpS1009 = _M0L9remainingS246 / 10;
          int32_t _M0L6_2atmpS1008 = 48 + _M0L6_2atmpS1009;
          int32_t _M0L5d__hiS253 = (uint16_t)_M0L6_2atmpS1008;
          int32_t _M0L6_2atmpS1007 = _M0L9remainingS246 % 10;
          int32_t _M0L6_2atmpS1006 = 48 + _M0L6_2atmpS1007;
          int32_t _M0L5d__loS254 = (uint16_t)_M0L6_2atmpS1006;
          int32_t _M0L6_2atmpS1003 = _M0L12digit__startS244 + _M0L6offsetS247;
          int32_t _M0L6_2atmpS1002 = _M0L6_2atmpS1003 - 2;
          int32_t _M0L6_2atmpS1005;
          int32_t _M0L6_2atmpS1004;
          _M0L6bufferS243[_M0L6_2atmpS1002] = _M0L5d__hiS253;
          _M0L6_2atmpS1005 = _M0L12digit__startS244 + _M0L6offsetS247;
          _M0L6_2atmpS1004 = _M0L6_2atmpS1005 - 1;
          _M0L6bufferS243[_M0L6_2atmpS1004] = _M0L5d__loS254;
        } else {
          int32_t _M0L6_2atmpS1013 = _M0L12digit__startS244 + _M0L6offsetS247;
          int32_t _M0L6_2atmpS1010 = _M0L6_2atmpS1013 - 1;
          int32_t _M0L6_2atmpS1012 = 48 + _M0L9remainingS246;
          int32_t _M0L6_2atmpS1011 = (uint16_t)_M0L6_2atmpS1012;
          _M0L6bufferS243[_M0L6_2atmpS1010] = _M0L6_2atmpS1011;
        }
        break;
      }
    }
    break;
  }
  return 0;
}

int32_t _M0FPB26int64__to__string__generic(
  uint16_t* _M0L6bufferS223,
  uint64_t _M0L3numS227,
  int32_t _M0L12digit__startS224,
  int32_t _M0L10total__lenS226,
  int32_t _M0L5radixS217
) {
  uint64_t _M0L4baseS216;
  int32_t _M0L6_2atmpS960;
  int32_t _M0L6_2atmpS959;
  #line 462 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  #line 470 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L4baseS216 = _M0MPC13int3Int10to__uint64(_M0L5radixS217);
  _M0L6_2atmpS960 = _M0L5radixS217 - 1;
  _M0L6_2atmpS959 = _M0L5radixS217 & _M0L6_2atmpS960;
  if (_M0L6_2atmpS959 == 0) {
    int32_t _M0L5shiftS218;
    uint64_t _M0L4maskS219;
    int32_t _M0L6_2atmpS967;
    int32_t _M0L6offsetS220;
    uint64_t _M0L1nS221;
    #line 473 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0L5shiftS218 = moonbit_ctz32(_M0L5radixS217);
    _M0L4maskS219 = _M0L4baseS216 - 1ull;
    _M0L6_2atmpS967 = _M0L10total__lenS226 - _M0L12digit__startS224;
    _M0L6offsetS220 = _M0L6_2atmpS967;
    _M0L1nS221 = _M0L3numS227;
    while (1) {
      if (_M0L1nS221 > 0ull) {
        uint64_t _M0L6_2atmpS966 = _M0L1nS221 & _M0L4maskS219;
        int32_t _M0L5digitS222 = (int32_t)_M0L6_2atmpS966;
        int32_t _M0L6_2atmpS963 = _M0L12digit__startS224 + _M0L6offsetS220;
        int32_t _M0L6_2atmpS961 = _M0L6_2atmpS963 - 1;
        int32_t _M0L6_2atmpS962 =
          ((moonbit_string_t)moonbit_string_literal_13.data)[_M0L5digitS222];
        int32_t _M0L6_2atmpS964;
        uint64_t _M0L6_2atmpS965;
        _M0L6bufferS223[_M0L6_2atmpS961] = _M0L6_2atmpS962;
        _M0L6_2atmpS964 = _M0L6offsetS220 - 1;
        _M0L6_2atmpS965 = _M0L1nS221 >> (_M0L5shiftS218 & 63);
        _M0L6offsetS220 = _M0L6_2atmpS964;
        _M0L1nS221 = _M0L6_2atmpS965;
        continue;
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS974 = _M0L10total__lenS226 - _M0L12digit__startS224;
    int32_t _M0L6offsetS228 = _M0L6_2atmpS974;
    uint64_t _M0L1nS229 = _M0L3numS227;
    while (1) {
      if (_M0L1nS229 > 0ull) {
        uint64_t _M0L1qS230 = _M0L1nS229 / _M0L4baseS216;
        uint64_t _M0L6_2atmpS973 = _M0L1qS230 * _M0L4baseS216;
        uint64_t _M0L6_2atmpS972 = _M0L1nS229 - _M0L6_2atmpS973;
        int32_t _M0L5digitS231 = (int32_t)_M0L6_2atmpS972;
        int32_t _M0L6_2atmpS970 = _M0L12digit__startS224 + _M0L6offsetS228;
        int32_t _M0L6_2atmpS968 = _M0L6_2atmpS970 - 1;
        int32_t _M0L6_2atmpS969 =
          ((moonbit_string_t)moonbit_string_literal_13.data)[_M0L5digitS231];
        int32_t _M0L6_2atmpS971;
        _M0L6bufferS223[_M0L6_2atmpS968] = _M0L6_2atmpS969;
        _M0L6_2atmpS971 = _M0L6offsetS228 - 1;
        _M0L6offsetS228 = _M0L6_2atmpS971;
        _M0L1nS229 = _M0L1qS230;
        continue;
      }
      break;
    }
  }
  return 0;
}

int32_t _M0FPB22int64__to__string__hex(
  uint16_t* _M0L6bufferS210,
  uint64_t _M0L3numS215,
  int32_t _M0L12digit__startS211,
  int32_t _M0L10total__lenS214
) {
  int32_t _M0L6_2atmpS958;
  int32_t _M0L6offsetS205;
  uint64_t _M0L1nS206;
  #line 434 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L6_2atmpS958 = _M0L10total__lenS214 - _M0L12digit__startS211;
  _M0L6offsetS205 = _M0L6_2atmpS958;
  _M0L1nS206 = _M0L3numS215;
  while (1) {
    if (_M0L6offsetS205 >= 2) {
      uint64_t _M0L6_2atmpS955 = _M0L1nS206 & 255ull;
      int32_t _M0L9byte__valS207 = (int32_t)_M0L6_2atmpS955;
      int32_t _M0L2hiS208 = _M0L9byte__valS207 / 16;
      int32_t _M0L2loS209 = _M0L9byte__valS207 % 16;
      int32_t _M0L6_2atmpS949 = _M0L12digit__startS211 + _M0L6offsetS205;
      int32_t _M0L6_2atmpS947 = _M0L6_2atmpS949 - 2;
      int32_t _M0L6_2atmpS948 =
        ((moonbit_string_t)moonbit_string_literal_13.data)[_M0L2hiS208];
      int32_t _M0L6_2atmpS952;
      int32_t _M0L6_2atmpS950;
      int32_t _M0L6_2atmpS951;
      int32_t _M0L6_2atmpS953;
      uint64_t _M0L6_2atmpS954;
      _M0L6bufferS210[_M0L6_2atmpS947] = _M0L6_2atmpS948;
      _M0L6_2atmpS952 = _M0L12digit__startS211 + _M0L6offsetS205;
      _M0L6_2atmpS950 = _M0L6_2atmpS952 - 1;
      _M0L6_2atmpS951
      = ((moonbit_string_t)moonbit_string_literal_13.data)[
        _M0L2loS209
      ];
      _M0L6bufferS210[_M0L6_2atmpS950] = _M0L6_2atmpS951;
      _M0L6_2atmpS953 = _M0L6offsetS205 - 2;
      _M0L6_2atmpS954 = _M0L1nS206 >> 8;
      _M0L6offsetS205 = _M0L6_2atmpS953;
      _M0L1nS206 = _M0L6_2atmpS954;
      continue;
    } else if (_M0L6offsetS205 == 1) {
      uint64_t _M0L6_2atmpS957 = _M0L1nS206 & 15ull;
      int32_t _M0L6nibbleS213 = (int32_t)_M0L6_2atmpS957;
      int32_t _M0L6_2atmpS956 =
        ((moonbit_string_t)moonbit_string_literal_13.data)[_M0L6nibbleS213];
      _M0L6bufferS210[_M0L12digit__startS211] = _M0L6_2atmpS956;
    }
    break;
  }
  return 0;
}

int32_t _M0FPB14radix__count64(
  uint64_t _M0L5valueS199,
  int32_t _M0L5radixS201
) {
  uint64_t _M0L4baseS200;
  uint64_t _M0L3numS202;
  int32_t _M0L5countS203;
  #line 419 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  if (_M0L5valueS199 == 0ull) {
    return 1;
  }
  #line 424 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L4baseS200 = _M0MPC13int3Int10to__uint64(_M0L5radixS201);
  _M0L3numS202 = _M0L5valueS199;
  _M0L5countS203 = 0;
  while (1) {
    if (_M0L3numS202 > 0ull) {
      uint64_t _M0L6_2atmpS945 = _M0L3numS202 / _M0L4baseS200;
      int32_t _M0L6_2atmpS946 = _M0L5countS203 + 1;
      _M0L3numS202 = _M0L6_2atmpS945;
      _M0L5countS203 = _M0L6_2atmpS946;
      continue;
    } else {
      return _M0L5countS203;
    }
    break;
  }
}

int32_t _M0FPB12hex__count64(uint64_t _M0L5valueS197) {
  #line 407 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  if (_M0L5valueS197 == 0ull) {
    return 1;
  } else {
    int32_t _M0L14leading__zerosS198;
    int32_t _M0L6_2atmpS944;
    int32_t _M0L6_2atmpS943;
    #line 412 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0L14leading__zerosS198 = moonbit_clz64(_M0L5valueS197);
    _M0L6_2atmpS944 = 63 - _M0L14leading__zerosS198;
    _M0L6_2atmpS943 = _M0L6_2atmpS944 / 4;
    return _M0L6_2atmpS943 + 1;
  }
}

int32_t _M0FPB12dec__count64(uint64_t _M0L5valueS196) {
  #line 343 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  if (_M0L5valueS196 >= 10000000000ull) {
    if (_M0L5valueS196 >= 100000000000000ull) {
      if (_M0L5valueS196 >= 10000000000000000ull) {
        if (_M0L5valueS196 >= 1000000000000000000ull) {
          if (_M0L5valueS196 >= 10000000000000000000ull) {
            return 20;
          } else {
            return 19;
          }
        } else if (_M0L5valueS196 >= 100000000000000000ull) {
          return 18;
        } else {
          return 17;
        }
      } else if (_M0L5valueS196 >= 1000000000000000ull) {
        return 16;
      } else {
        return 15;
      }
    } else if (_M0L5valueS196 >= 1000000000000ull) {
      if (_M0L5valueS196 >= 10000000000000ull) {
        return 14;
      } else {
        return 13;
      }
    } else if (_M0L5valueS196 >= 100000000000ull) {
      return 12;
    } else {
      return 11;
    }
  } else if (_M0L5valueS196 >= 100000ull) {
    if (_M0L5valueS196 >= 10000000ull) {
      if (_M0L5valueS196 >= 1000000000ull) {
        return 10;
      } else if (_M0L5valueS196 >= 100000000ull) {
        return 9;
      } else {
        return 8;
      }
    } else if (_M0L5valueS196 >= 1000000ull) {
      return 7;
    } else {
      return 6;
    }
  } else if (_M0L5valueS196 >= 1000ull) {
    if (_M0L5valueS196 >= 10000ull) {
      return 5;
    } else {
      return 4;
    }
  } else if (_M0L5valueS196 >= 100ull) {
    return 3;
  } else if (_M0L5valueS196 >= 10ull) {
    return 2;
  } else {
    return 1;
  }
}

moonbit_string_t _M0MPC13int3Int18to__string_2einner(
  int32_t _M0L4selfS180,
  int32_t _M0L5radixS179
) {
  int32_t _M0L12is__negativeS181;
  uint32_t _M0L3numS182;
  uint16_t* _M0L6bufferS183;
  #line 209 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  if (_M0L5radixS179 < 2 || _M0L5radixS179 > 36) {
    #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_12.data);
  }
  if (_M0L4selfS180 == 0) {
    return (moonbit_string_t)moonbit_string_literal_2.data;
  }
  _M0L12is__negativeS181 = _M0L4selfS180 < 0;
  if (_M0L12is__negativeS181) {
    int32_t _M0L6_2atmpS942 = -_M0L4selfS180;
    _M0L3numS182 = *(uint32_t*)&_M0L6_2atmpS942;
  } else {
    _M0L3numS182 = *(uint32_t*)&_M0L4selfS180;
  }
  switch (_M0L5radixS179) {
    case 10: {
      int32_t _M0L10digit__lenS184;
      int32_t _M0L6_2atmpS939;
      int32_t _M0L10total__lenS185;
      uint16_t* _M0L6bufferS186;
      int32_t _M0L12digit__startS187;
      #line 235 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS184 = _M0FPB12dec__count32(_M0L3numS182);
      if (_M0L12is__negativeS181) {
        _M0L6_2atmpS939 = 1;
      } else {
        _M0L6_2atmpS939 = 0;
      }
      _M0L10total__lenS185 = _M0L10digit__lenS184 + _M0L6_2atmpS939;
      _M0L6bufferS186
      = (uint16_t*)moonbit_make_string(_M0L10total__lenS185, 0);
      if (_M0L12is__negativeS181) {
        _M0L12digit__startS187 = 1;
      } else {
        _M0L12digit__startS187 = 0;
      }
      #line 239 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0FPB20int__to__string__dec(_M0L6bufferS186, _M0L3numS182, _M0L12digit__startS187, _M0L10total__lenS185);
      _M0L6bufferS183 = _M0L6bufferS186;
      break;
    }
    
    case 16: {
      int32_t _M0L10digit__lenS188;
      int32_t _M0L6_2atmpS940;
      int32_t _M0L10total__lenS189;
      uint16_t* _M0L6bufferS190;
      int32_t _M0L12digit__startS191;
      #line 243 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS188 = _M0FPB12hex__count32(_M0L3numS182);
      if (_M0L12is__negativeS181) {
        _M0L6_2atmpS940 = 1;
      } else {
        _M0L6_2atmpS940 = 0;
      }
      _M0L10total__lenS189 = _M0L10digit__lenS188 + _M0L6_2atmpS940;
      _M0L6bufferS190
      = (uint16_t*)moonbit_make_string(_M0L10total__lenS189, 0);
      if (_M0L12is__negativeS181) {
        _M0L12digit__startS191 = 1;
      } else {
        _M0L12digit__startS191 = 0;
      }
      #line 247 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0FPB20int__to__string__hex(_M0L6bufferS190, _M0L3numS182, _M0L12digit__startS191, _M0L10total__lenS189);
      _M0L6bufferS183 = _M0L6bufferS190;
      break;
    }
    default: {
      int32_t _M0L10digit__lenS192;
      int32_t _M0L6_2atmpS941;
      int32_t _M0L10total__lenS193;
      uint16_t* _M0L6bufferS194;
      int32_t _M0L12digit__startS195;
      #line 251 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS192
      = _M0FPB14radix__count32(_M0L3numS182, _M0L5radixS179);
      if (_M0L12is__negativeS181) {
        _M0L6_2atmpS941 = 1;
      } else {
        _M0L6_2atmpS941 = 0;
      }
      _M0L10total__lenS193 = _M0L10digit__lenS192 + _M0L6_2atmpS941;
      _M0L6bufferS194
      = (uint16_t*)moonbit_make_string(_M0L10total__lenS193, 0);
      if (_M0L12is__negativeS181) {
        _M0L12digit__startS195 = 1;
      } else {
        _M0L12digit__startS195 = 0;
      }
      #line 255 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0FPB24int__to__string__generic(_M0L6bufferS194, _M0L3numS182, _M0L12digit__startS195, _M0L10total__lenS193, _M0L5radixS179);
      _M0L6bufferS183 = _M0L6bufferS194;
      break;
    }
  }
  if (_M0L12is__negativeS181) {
    _M0L6bufferS183[0] = 45;
  }
  return _M0L6bufferS183;
}

int32_t _M0FPB14radix__count32(
  uint32_t _M0L5valueS173,
  int32_t _M0L5radixS175
) {
  uint32_t _M0L4baseS174;
  uint32_t _M0L3numS176;
  int32_t _M0L5countS177;
  #line 189 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  if (_M0L5valueS173 == 0u) {
    return 1;
  }
  _M0L4baseS174 = *(uint32_t*)&_M0L5radixS175;
  _M0L3numS176 = _M0L5valueS173;
  _M0L5countS177 = 0;
  while (1) {
    if (_M0L3numS176 > 0u) {
      uint32_t _M0L6_2atmpS937 = _M0L3numS176 / _M0L4baseS174;
      int32_t _M0L6_2atmpS938 = _M0L5countS177 + 1;
      _M0L3numS176 = _M0L6_2atmpS937;
      _M0L5countS177 = _M0L6_2atmpS938;
      continue;
    } else {
      return _M0L5countS177;
    }
    break;
  }
}

int32_t _M0FPB12hex__count32(uint32_t _M0L5valueS171) {
  #line 177 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  if (_M0L5valueS171 == 0u) {
    return 1;
  } else {
    int32_t _M0L14leading__zerosS172;
    int32_t _M0L6_2atmpS936;
    int32_t _M0L6_2atmpS935;
    #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0L14leading__zerosS172 = moonbit_clz32(_M0L5valueS171);
    _M0L6_2atmpS936 = 31 - _M0L14leading__zerosS172;
    _M0L6_2atmpS935 = _M0L6_2atmpS936 / 4;
    return _M0L6_2atmpS935 + 1;
  }
}

int32_t _M0FPB12dec__count32(uint32_t _M0L5valueS170) {
  #line 143 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  if (_M0L5valueS170 >= 100000u) {
    if (_M0L5valueS170 >= 10000000u) {
      if (_M0L5valueS170 >= 1000000000u) {
        return 10;
      } else if (_M0L5valueS170 >= 100000000u) {
        return 9;
      } else {
        return 8;
      }
    } else if (_M0L5valueS170 >= 1000000u) {
      return 7;
    } else {
      return 6;
    }
  } else if (_M0L5valueS170 >= 1000u) {
    if (_M0L5valueS170 >= 10000u) {
      return 5;
    } else {
      return 4;
    }
  } else if (_M0L5valueS170 >= 100u) {
    return 3;
  } else if (_M0L5valueS170 >= 10u) {
    return 2;
  } else {
    return 1;
  }
}

int32_t _M0FPB20int__to__string__dec(
  uint16_t* _M0L6bufferS156,
  uint32_t _M0L3numS168,
  int32_t _M0L12digit__startS157,
  int32_t _M0L10total__lenS169
) {
  int32_t _M0L6_2atmpS934;
  uint32_t _M0L3numS146;
  int32_t _M0L6offsetS147;
  #line 88 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L6_2atmpS934 = _M0L10total__lenS169 - _M0L12digit__startS157;
  _M0L3numS146 = _M0L3numS168;
  _M0L6offsetS147 = _M0L6_2atmpS934;
  while (1) {
    if (_M0L3numS146 >= 10000u) {
      uint32_t _M0L1tS148 = _M0L3numS146 / 10000u;
      uint32_t _M0L6_2atmpS911 = _M0L3numS146 % 10000u;
      int32_t _M0L1rS149 = *(int32_t*)&_M0L6_2atmpS911;
      int32_t _M0L2d1S150 = _M0L1rS149 / 100;
      int32_t _M0L2d2S151 = _M0L1rS149 % 100;
      int32_t _M0L6_2atmpS910 = _M0L2d1S150 / 10;
      int32_t _M0L6_2atmpS909 = 48 + _M0L6_2atmpS910;
      int32_t _M0L6d1__hiS152 = (uint16_t)_M0L6_2atmpS909;
      int32_t _M0L6_2atmpS908 = _M0L2d1S150 % 10;
      int32_t _M0L6_2atmpS907 = 48 + _M0L6_2atmpS908;
      int32_t _M0L6d1__loS153 = (uint16_t)_M0L6_2atmpS907;
      int32_t _M0L6_2atmpS906 = _M0L2d2S151 / 10;
      int32_t _M0L6_2atmpS905 = 48 + _M0L6_2atmpS906;
      int32_t _M0L6d2__hiS154 = (uint16_t)_M0L6_2atmpS905;
      int32_t _M0L6_2atmpS904 = _M0L2d2S151 % 10;
      int32_t _M0L6_2atmpS903 = 48 + _M0L6_2atmpS904;
      int32_t _M0L6d2__loS155 = (uint16_t)_M0L6_2atmpS903;
      int32_t _M0L6_2atmpS895 = _M0L12digit__startS157 + _M0L6offsetS147;
      int32_t _M0L6_2atmpS894 = _M0L6_2atmpS895 - 4;
      int32_t _M0L6_2atmpS897;
      int32_t _M0L6_2atmpS896;
      int32_t _M0L6_2atmpS899;
      int32_t _M0L6_2atmpS898;
      int32_t _M0L6_2atmpS901;
      int32_t _M0L6_2atmpS900;
      int32_t _M0L6_2atmpS902;
      _M0L6bufferS156[_M0L6_2atmpS894] = _M0L6d1__hiS152;
      _M0L6_2atmpS897 = _M0L12digit__startS157 + _M0L6offsetS147;
      _M0L6_2atmpS896 = _M0L6_2atmpS897 - 3;
      _M0L6bufferS156[_M0L6_2atmpS896] = _M0L6d1__loS153;
      _M0L6_2atmpS899 = _M0L12digit__startS157 + _M0L6offsetS147;
      _M0L6_2atmpS898 = _M0L6_2atmpS899 - 2;
      _M0L6bufferS156[_M0L6_2atmpS898] = _M0L6d2__hiS154;
      _M0L6_2atmpS901 = _M0L12digit__startS157 + _M0L6offsetS147;
      _M0L6_2atmpS900 = _M0L6_2atmpS901 - 1;
      _M0L6bufferS156[_M0L6_2atmpS900] = _M0L6d2__loS155;
      _M0L6_2atmpS902 = _M0L6offsetS147 - 4;
      _M0L3numS146 = _M0L1tS148;
      _M0L6offsetS147 = _M0L6_2atmpS902;
      continue;
    } else {
      int32_t _M0L6_2atmpS933 = *(int32_t*)&_M0L3numS146;
      int32_t _M0L9remainingS159 = _M0L6_2atmpS933;
      int32_t _M0L6offsetS160 = _M0L6offsetS147;
      while (1) {
        if (_M0L9remainingS159 >= 100) {
          int32_t _M0L1tS161 = _M0L9remainingS159 / 100;
          int32_t _M0L1dS162 = _M0L9remainingS159 % 100;
          int32_t _M0L6_2atmpS920 = _M0L1dS162 / 10;
          int32_t _M0L6_2atmpS919 = 48 + _M0L6_2atmpS920;
          int32_t _M0L5d__hiS163 = (uint16_t)_M0L6_2atmpS919;
          int32_t _M0L6_2atmpS918 = _M0L1dS162 % 10;
          int32_t _M0L6_2atmpS917 = 48 + _M0L6_2atmpS918;
          int32_t _M0L5d__loS164 = (uint16_t)_M0L6_2atmpS917;
          int32_t _M0L6_2atmpS913 = _M0L12digit__startS157 + _M0L6offsetS160;
          int32_t _M0L6_2atmpS912 = _M0L6_2atmpS913 - 2;
          int32_t _M0L6_2atmpS915;
          int32_t _M0L6_2atmpS914;
          int32_t _M0L6_2atmpS916;
          _M0L6bufferS156[_M0L6_2atmpS912] = _M0L5d__hiS163;
          _M0L6_2atmpS915 = _M0L12digit__startS157 + _M0L6offsetS160;
          _M0L6_2atmpS914 = _M0L6_2atmpS915 - 1;
          _M0L6bufferS156[_M0L6_2atmpS914] = _M0L5d__loS164;
          _M0L6_2atmpS916 = _M0L6offsetS160 - 2;
          _M0L9remainingS159 = _M0L1tS161;
          _M0L6offsetS160 = _M0L6_2atmpS916;
          continue;
        } else if (_M0L9remainingS159 >= 10) {
          int32_t _M0L6_2atmpS928 = _M0L9remainingS159 / 10;
          int32_t _M0L6_2atmpS927 = 48 + _M0L6_2atmpS928;
          int32_t _M0L5d__hiS166 = (uint16_t)_M0L6_2atmpS927;
          int32_t _M0L6_2atmpS926 = _M0L9remainingS159 % 10;
          int32_t _M0L6_2atmpS925 = 48 + _M0L6_2atmpS926;
          int32_t _M0L5d__loS167 = (uint16_t)_M0L6_2atmpS925;
          int32_t _M0L6_2atmpS922 = _M0L12digit__startS157 + _M0L6offsetS160;
          int32_t _M0L6_2atmpS921 = _M0L6_2atmpS922 - 2;
          int32_t _M0L6_2atmpS924;
          int32_t _M0L6_2atmpS923;
          _M0L6bufferS156[_M0L6_2atmpS921] = _M0L5d__hiS166;
          _M0L6_2atmpS924 = _M0L12digit__startS157 + _M0L6offsetS160;
          _M0L6_2atmpS923 = _M0L6_2atmpS924 - 1;
          _M0L6bufferS156[_M0L6_2atmpS923] = _M0L5d__loS167;
        } else {
          int32_t _M0L6_2atmpS932 = _M0L12digit__startS157 + _M0L6offsetS160;
          int32_t _M0L6_2atmpS929 = _M0L6_2atmpS932 - 1;
          int32_t _M0L6_2atmpS931 = 48 + _M0L9remainingS159;
          int32_t _M0L6_2atmpS930 = (uint16_t)_M0L6_2atmpS931;
          _M0L6bufferS156[_M0L6_2atmpS929] = _M0L6_2atmpS930;
        }
        break;
      }
    }
    break;
  }
  return 0;
}

int32_t _M0FPB24int__to__string__generic(
  uint16_t* _M0L6bufferS136,
  uint32_t _M0L3numS140,
  int32_t _M0L12digit__startS137,
  int32_t _M0L10total__lenS139,
  int32_t _M0L5radixS130
) {
  uint32_t _M0L4baseS129;
  int32_t _M0L6_2atmpS879;
  int32_t _M0L6_2atmpS878;
  #line 57 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L4baseS129 = *(uint32_t*)&_M0L5radixS130;
  _M0L6_2atmpS879 = _M0L5radixS130 - 1;
  _M0L6_2atmpS878 = _M0L5radixS130 & _M0L6_2atmpS879;
  if (_M0L6_2atmpS878 == 0) {
    int32_t _M0L5shiftS131;
    uint32_t _M0L4maskS132;
    int32_t _M0L6_2atmpS886;
    int32_t _M0L6offsetS133;
    uint32_t _M0L1nS134;
    #line 68 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0L5shiftS131 = moonbit_ctz32(_M0L5radixS130);
    _M0L4maskS132 = _M0L4baseS129 - 1u;
    _M0L6_2atmpS886 = _M0L10total__lenS139 - _M0L12digit__startS137;
    _M0L6offsetS133 = _M0L6_2atmpS886;
    _M0L1nS134 = _M0L3numS140;
    while (1) {
      if (_M0L1nS134 > 0u) {
        uint32_t _M0L6_2atmpS885 = _M0L1nS134 & _M0L4maskS132;
        int32_t _M0L5digitS135 = *(int32_t*)&_M0L6_2atmpS885;
        int32_t _M0L6_2atmpS882 = _M0L12digit__startS137 + _M0L6offsetS133;
        int32_t _M0L6_2atmpS880 = _M0L6_2atmpS882 - 1;
        int32_t _M0L6_2atmpS881 =
          ((moonbit_string_t)moonbit_string_literal_13.data)[_M0L5digitS135];
        int32_t _M0L6_2atmpS883;
        uint32_t _M0L6_2atmpS884;
        _M0L6bufferS136[_M0L6_2atmpS880] = _M0L6_2atmpS881;
        _M0L6_2atmpS883 = _M0L6offsetS133 - 1;
        _M0L6_2atmpS884 = _M0L1nS134 >> (_M0L5shiftS131 & 31);
        _M0L6offsetS133 = _M0L6_2atmpS883;
        _M0L1nS134 = _M0L6_2atmpS884;
        continue;
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS893 = _M0L10total__lenS139 - _M0L12digit__startS137;
    int32_t _M0L6offsetS141 = _M0L6_2atmpS893;
    uint32_t _M0L1nS142 = _M0L3numS140;
    while (1) {
      if (_M0L1nS142 > 0u) {
        uint32_t _M0L1qS143 = _M0L1nS142 / _M0L4baseS129;
        uint32_t _M0L6_2atmpS892 = _M0L1qS143 * _M0L4baseS129;
        uint32_t _M0L6_2atmpS891 = _M0L1nS142 - _M0L6_2atmpS892;
        int32_t _M0L5digitS144 = *(int32_t*)&_M0L6_2atmpS891;
        int32_t _M0L6_2atmpS889 = _M0L12digit__startS137 + _M0L6offsetS141;
        int32_t _M0L6_2atmpS887 = _M0L6_2atmpS889 - 1;
        int32_t _M0L6_2atmpS888 =
          ((moonbit_string_t)moonbit_string_literal_13.data)[_M0L5digitS144];
        int32_t _M0L6_2atmpS890;
        _M0L6bufferS136[_M0L6_2atmpS887] = _M0L6_2atmpS888;
        _M0L6_2atmpS890 = _M0L6offsetS141 - 1;
        _M0L6offsetS141 = _M0L6_2atmpS890;
        _M0L1nS142 = _M0L1qS143;
        continue;
      }
      break;
    }
  }
  return 0;
}

int32_t _M0FPB20int__to__string__hex(
  uint16_t* _M0L6bufferS123,
  uint32_t _M0L3numS128,
  int32_t _M0L12digit__startS124,
  int32_t _M0L10total__lenS127
) {
  int32_t _M0L6_2atmpS877;
  int32_t _M0L6offsetS118;
  uint32_t _M0L1nS119;
  #line 29 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L6_2atmpS877 = _M0L10total__lenS127 - _M0L12digit__startS124;
  _M0L6offsetS118 = _M0L6_2atmpS877;
  _M0L1nS119 = _M0L3numS128;
  while (1) {
    if (_M0L6offsetS118 >= 2) {
      uint32_t _M0L6_2atmpS874 = _M0L1nS119 & 255u;
      int32_t _M0L9byte__valS120 = *(int32_t*)&_M0L6_2atmpS874;
      int32_t _M0L2hiS121 = _M0L9byte__valS120 / 16;
      int32_t _M0L2loS122 = _M0L9byte__valS120 % 16;
      int32_t _M0L6_2atmpS868 = _M0L12digit__startS124 + _M0L6offsetS118;
      int32_t _M0L6_2atmpS866 = _M0L6_2atmpS868 - 2;
      int32_t _M0L6_2atmpS867 =
        ((moonbit_string_t)moonbit_string_literal_13.data)[_M0L2hiS121];
      int32_t _M0L6_2atmpS871;
      int32_t _M0L6_2atmpS869;
      int32_t _M0L6_2atmpS870;
      int32_t _M0L6_2atmpS872;
      uint32_t _M0L6_2atmpS873;
      _M0L6bufferS123[_M0L6_2atmpS866] = _M0L6_2atmpS867;
      _M0L6_2atmpS871 = _M0L12digit__startS124 + _M0L6offsetS118;
      _M0L6_2atmpS869 = _M0L6_2atmpS871 - 1;
      _M0L6_2atmpS870
      = ((moonbit_string_t)moonbit_string_literal_13.data)[
        _M0L2loS122
      ];
      _M0L6bufferS123[_M0L6_2atmpS869] = _M0L6_2atmpS870;
      _M0L6_2atmpS872 = _M0L6offsetS118 - 2;
      _M0L6_2atmpS873 = _M0L1nS119 >> 8;
      _M0L6offsetS118 = _M0L6_2atmpS872;
      _M0L1nS119 = _M0L6_2atmpS873;
      continue;
    } else if (_M0L6offsetS118 == 1) {
      uint32_t _M0L6_2atmpS876 = _M0L1nS119 & 15u;
      int32_t _M0L6nibbleS126 = *(int32_t*)&_M0L6_2atmpS876;
      int32_t _M0L6_2atmpS875 =
        ((moonbit_string_t)moonbit_string_literal_13.data)[_M0L6nibbleS126];
      _M0L6bufferS123[_M0L12digit__startS124] = _M0L6_2atmpS875;
    }
    break;
  }
  return 0;
}

int32_t _M0IP016_24default__implPB4Show6outputGiE(
  int32_t _M0L4selfS115,
  struct _M0TPB6Logger _M0L6loggerS114
) {
  moonbit_string_t _M0L6_2atmpS864;
  #line 159 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  #line 160 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS864 = _M0IPC13int3IntPB4Show10to__string(_M0L4selfS115);
  #line 160 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6loggerS114.$0->$method_0(_M0L6loggerS114.$1, _M0L6_2atmpS864);
  moonbit_decref(_M0L6_2atmpS864);
  return 0;
}

int32_t _M0IP016_24default__implPB4Show6outputGmE(
  uint64_t _M0L4selfS117,
  struct _M0TPB6Logger _M0L6loggerS116
) {
  moonbit_string_t _M0L6_2atmpS865;
  #line 159 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  #line 160 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS865 = _M0IPC16uint646UInt64PB4Show10to__string(_M0L4selfS117);
  #line 160 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6loggerS116.$0->$method_0(_M0L6loggerS116.$1, _M0L6_2atmpS865);
  moonbit_decref(_M0L6_2atmpS865);
  return 0;
}

int32_t _M0MPC16string10StringView13start__offset(
  struct _M0TPC16string10StringView _M0L4selfS113
) {
  #line 99 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
  return _M0L4selfS113.$1;
}

moonbit_string_t _M0MPC16string10StringView4data(
  struct _M0TPC16string10StringView _M0L4selfS112
) {
  moonbit_string_t _M0L8_2afieldS1483;
  #line 92 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
  _M0L8_2afieldS1483 = _M0L4selfS112.$0;
  moonbit_incref(_M0L8_2afieldS1483);
  return _M0L8_2afieldS1483;
}

int32_t _M0IP016_24default__implPB6Logger16write__substringGRPB13StringBuilderE(
  struct _M0TPB13StringBuilder* _M0L4selfS108,
  moonbit_string_t _M0L5valueS109,
  int32_t _M0L5startS110,
  int32_t _M0L3lenS111
) {
  int32_t _M0L6_2atmpS863;
  int64_t _M0L6_2atmpS862;
  struct _M0TPC16string10StringView _M0L6_2atmpS861;
  #line 122 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS863 = _M0L5startS110 + _M0L3lenS111;
  _M0L6_2atmpS862 = (int64_t)_M0L6_2atmpS863;
  #line 123 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS861
  = _M0MPC16string6String11sub_2einner(_M0L5valueS109, _M0L5startS110, _M0L6_2atmpS862);
  #line 123 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0IPB13StringBuilderPB6Logger11write__view(_M0L4selfS108, _M0L6_2atmpS861);
  moonbit_decref(_M0L6_2atmpS861.$0);
  return 0;
}

struct _M0TPC16string10StringView _M0MPC16string6String11sub_2einner(
  moonbit_string_t _M0L4selfS100,
  int32_t _M0L5startS107,
  int64_t _M0L3endS104
) {
  int32_t _M0L3lenS99;
  int32_t _M0L3endS103;
  int32_t _M0L3endS101;
  #line 923 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
  _M0L3lenS99 = Moonbit_array_length(_M0L4selfS100);
  if (_M0L3endS104 == 4294967296ll) {
    _M0L3endS103 = _M0L3lenS99;
    goto join_102;
  } else {
    int64_t _M0L7_2aSomeS105 = _M0L3endS104;
    int32_t _M0L6_2aendS106 = (int32_t)_M0L7_2aSomeS105;
    _M0L3endS103 = _M0L6_2aendS106;
    goto join_102;
  }
  goto joinlet_1545;
  join_102:;
  _M0L3endS101 = _M0L3endS103;
  joinlet_1545:;
  if (
    _M0L5startS107 >= 0
    && _M0L5startS107 <= _M0L3endS101
    && _M0L3endS101 <= _M0L3lenS99
  ) {
    if (_M0L5startS107 < _M0L3lenS99) {
      int32_t _M0L6_2atmpS858 = _M0L4selfS100[_M0L5startS107];
      int32_t _M0L6_2atmpS857;
      #line 931 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
      _M0L6_2atmpS857
      = _M0MPC16uint166UInt1623is__trailing__surrogate(_M0L6_2atmpS858);
      if (!_M0L6_2atmpS857) {
        
      } else {
        #line 931 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
        moonbit_panic();
      }
    }
    if (_M0L3endS101 < _M0L3lenS99) {
      int32_t _M0L6_2atmpS860 = _M0L4selfS100[_M0L3endS101];
      int32_t _M0L6_2atmpS859;
      #line 934 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
      _M0L6_2atmpS859
      = _M0MPC16uint166UInt1623is__trailing__surrogate(_M0L6_2atmpS860);
      if (!_M0L6_2atmpS859) {
        
      } else {
        #line 934 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
        moonbit_panic();
      }
    }
    moonbit_incref(_M0L4selfS100);
    return (struct _M0TPC16string10StringView){.$0 = _M0L4selfS100,
                                                 .$1 = _M0L5startS107,
                                                 .$2 = _M0L3endS101};
  } else {
    #line 929 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
    moonbit_panic();
  }
}

int32_t _M0IP016_24default__implPB6Logger5writeGRPB13StringBuilderE(
  struct _M0TPB13StringBuilder* _M0L4selfS98,
  struct _M0TPB4Show _M0L4showS97
) {
  struct _M0TPB6Logger _M0L6_2atmpS856;
  #line 116 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  moonbit_incref(_M0L4selfS98);
  _M0L6_2atmpS856
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS98
  };
  #line 117 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L4showS97.$0->$method_0(_M0L4showS97.$1, _M0L6_2atmpS856);
  if (_M0L6_2atmpS856.$1) {
    moonbit_decref(_M0L6_2atmpS856.$1);
  }
  return 0;
}

int32_t _M0IP016_24default__implPB6Logger28write__string__interpolationGRPB13StringBuilderE(
  struct _M0TPB13StringBuilder* _M0L4selfS96,
  struct _M0TPB4Show _M0L4showS95
) {
  struct _M0TPB6Logger _M0L6_2atmpS855;
  #line 111 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  moonbit_incref(_M0L4selfS96);
  _M0L6_2atmpS855
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS96
  };
  #line 112 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L4showS95.$0->$method_0(_M0L4showS95.$1, _M0L6_2atmpS855);
  if (_M0L6_2atmpS855.$1) {
    moonbit_decref(_M0L6_2atmpS855.$1);
  }
  return 0;
}

uint64_t _M0MPC13int3Int10to__uint64(int32_t _M0L4selfS94) {
  int64_t _M0L6_2atmpS854;
  #line 989 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS854 = (int64_t)_M0L4selfS94;
  return *(uint64_t*)&_M0L6_2atmpS854;
}

int32_t _M0IPC16uint166UInt16PB7Default7default() {
  #line 201 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uint16_char.mbt"
  return 0;
}

int32_t _M0MPC16uint166UInt1623is__trailing__surrogate(int32_t _M0L4selfS93) {
  #line 58 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uint16_char.mbt"
  return _M0L4selfS93 >= 56320 && _M0L4selfS93 <= 57343;
}

int32_t _M0IPB13StringBuilderPB6Logger13write__string(
  struct _M0TPB13StringBuilder* _M0L4selfS92,
  moonbit_string_t _M0L3strS90
) {
  int32_t _M0L8str__lenS89;
  int32_t _M0L3lenS853;
  int32_t _M0L8requiredS91;
  uint16_t* _M0L4dataS848;
  int32_t _M0L6_2atmpS847;
  int32_t _if__result_1546;
  uint16_t* _M0L4dataS849;
  int32_t _M0L3lenS850;
  int32_t _M0L3lenS852;
  int32_t _M0L6_2atmpS851;
  #line 105 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L8str__lenS89 = Moonbit_array_length(_M0L3strS90);
  if (_M0L8str__lenS89 == 0) {
    return 0;
  }
  _M0L3lenS853 = _M0L4selfS92->$1;
  _M0L8requiredS91 = _M0L3lenS853 + _M0L8str__lenS89;
  _M0L4dataS848 = _M0L4selfS92->$0;
  _M0L6_2atmpS847 = Moonbit_array_length(_M0L4dataS848);
  if (_M0L8requiredS91 > _M0L6_2atmpS847) {
    _if__result_1546 = 1;
  } else {
    int32_t _M0L3lenS846 = _M0L4selfS92->$1;
    _if__result_1546 = _M0L8requiredS91 < _M0L3lenS846;
  }
  if (_if__result_1546) {
    #line 112 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0MPB13StringBuilder4grow(_M0L4selfS92, _M0L8requiredS91);
  }
  _M0L4dataS849 = _M0L4selfS92->$0;
  _M0L3lenS850 = _M0L4selfS92->$1;
  moonbit_incref(_M0L4dataS849);
  #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0MPC15array10FixedArray26unsafe__blit__from__string(_M0L4dataS849, _M0L3lenS850, _M0L3strS90, 0, _M0L8str__lenS89);
  moonbit_decref(_M0L4dataS849);
  _M0L3lenS852 = _M0L4selfS92->$1;
  _M0L6_2atmpS851 = _M0L3lenS852 + _M0L8str__lenS89;
  _M0L4selfS92->$1 = _M0L6_2atmpS851;
  return 0;
}

int32_t _M0MPC15array10FixedArray26unsafe__blit__from__string(
  uint16_t* _M0L4selfS85,
  int32_t _M0L11dst__offsetS88,
  moonbit_string_t _M0L3strS86,
  int32_t _M0L11str__offsetS81,
  int32_t _M0L3lenS82
) {
  int32_t _M0L16end__str__offsetS80;
  int32_t _M0L1iS83;
  int32_t _M0L1jS84;
  #line 90 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L16end__str__offsetS80 = _M0L11str__offsetS81 + _M0L3lenS82;
  _M0L1iS83 = _M0L11str__offsetS81;
  _M0L1jS84 = _M0L11dst__offsetS88;
  while (1) {
    if (_M0L1iS83 < _M0L16end__str__offsetS80) {
      int32_t _M0L6_2atmpS843 = _M0L3strS86[_M0L1iS83];
      int32_t _M0L6_2atmpS844;
      int32_t _M0L6_2atmpS845;
      _M0L4selfS85[_M0L1jS84] = _M0L6_2atmpS843;
      _M0L6_2atmpS844 = _M0L1iS83 + 1;
      _M0L6_2atmpS845 = _M0L1jS84 + 1;
      _M0L1iS83 = _M0L6_2atmpS844;
      _M0L1jS84 = _M0L6_2atmpS845;
      continue;
    }
    break;
  }
  return 0;
}

int32_t _M0IPB13StringBuilderPB6Logger11write__char(
  struct _M0TPB13StringBuilder* _M0L4selfS78,
  int32_t _M0L2chS77
) {
  uint32_t _M0L4codeS76;
  #line 120 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  #line 121 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L4codeS76 = _M0MPC14char4Char8to__uint(_M0L2chS77);
  if (_M0L4codeS76 <= 65535u) {
    int32_t _M0L3lenS814 = _M0L4selfS78->$1;
    uint16_t* _M0L4dataS816 = _M0L4selfS78->$0;
    int32_t _M0L6_2atmpS815 = Moonbit_array_length(_M0L4dataS816);
    uint16_t* _M0L4dataS819;
    int32_t _M0L3lenS820;
    int32_t _M0L6_2atmpS821;
    int32_t _M0L3lenS823;
    int32_t _M0L6_2atmpS822;
    if (_M0L3lenS814 >= _M0L6_2atmpS815) {
      int32_t _M0L3lenS818 = _M0L4selfS78->$1;
      int32_t _M0L6_2atmpS817 = _M0L3lenS818 + 1;
      #line 124 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0MPB13StringBuilder4grow(_M0L4selfS78, _M0L6_2atmpS817);
    }
    _M0L4dataS819 = _M0L4selfS78->$0;
    _M0L3lenS820 = _M0L4selfS78->$1;
    moonbit_incref(_M0L4dataS819);
    #line 126 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0L6_2atmpS821 = _M0MPC14uint4UInt10to__uint16(_M0L4codeS76);
    if (
      _M0L3lenS820 < 0 || _M0L3lenS820 >= Moonbit_array_length(_M0L4dataS819)
    ) {
      #line 126 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      moonbit_panic();
    }
    _M0L4dataS819[_M0L3lenS820] = _M0L6_2atmpS821;
    moonbit_decref(_M0L4dataS819);
    _M0L3lenS823 = _M0L4selfS78->$1;
    _M0L6_2atmpS822 = _M0L3lenS823 + 1;
    _M0L4selfS78->$1 = _M0L6_2atmpS822;
  } else if (_M0L4codeS76 <= 1114111u) {
    uint16_t* _M0L4dataS827 = _M0L4selfS78->$0;
    int32_t _M0L6_2atmpS825 = Moonbit_array_length(_M0L4dataS827);
    int32_t _M0L3lenS826 = _M0L4selfS78->$1;
    int32_t _M0L6_2atmpS824 = _M0L6_2atmpS825 - _M0L3lenS826;
    uint32_t _M0L4codeS79;
    uint16_t* _M0L4dataS830;
    int32_t _M0L3lenS831;
    uint32_t _M0L6_2atmpS834;
    uint32_t _M0L6_2atmpS833;
    int32_t _M0L6_2atmpS832;
    uint16_t* _M0L4dataS835;
    int32_t _M0L3lenS840;
    int32_t _M0L6_2atmpS836;
    uint32_t _M0L6_2atmpS839;
    uint32_t _M0L6_2atmpS838;
    int32_t _M0L6_2atmpS837;
    int32_t _M0L3lenS842;
    int32_t _M0L6_2atmpS841;
    if (_M0L6_2atmpS824 < 2) {
      int32_t _M0L3lenS829 = _M0L4selfS78->$1;
      int32_t _M0L6_2atmpS828 = _M0L3lenS829 + 2;
      #line 130 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0MPB13StringBuilder4grow(_M0L4selfS78, _M0L6_2atmpS828);
    }
    _M0L4codeS79 = _M0L4codeS76 - 65536u;
    _M0L4dataS830 = _M0L4selfS78->$0;
    _M0L3lenS831 = _M0L4selfS78->$1;
    _M0L6_2atmpS834 = _M0L4codeS79 >> 10;
    _M0L6_2atmpS833 = 55296u + _M0L6_2atmpS834;
    moonbit_incref(_M0L4dataS830);
    #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0L6_2atmpS832 = _M0MPC14uint4UInt10to__uint16(_M0L6_2atmpS833);
    if (
      _M0L3lenS831 < 0 || _M0L3lenS831 >= Moonbit_array_length(_M0L4dataS830)
    ) {
      #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      moonbit_panic();
    }
    _M0L4dataS830[_M0L3lenS831] = _M0L6_2atmpS832;
    moonbit_decref(_M0L4dataS830);
    _M0L4dataS835 = _M0L4selfS78->$0;
    _M0L3lenS840 = _M0L4selfS78->$1;
    _M0L6_2atmpS836 = _M0L3lenS840 + 1;
    _M0L6_2atmpS839 = _M0L4codeS79 & 1023u;
    _M0L6_2atmpS838 = 56320u + _M0L6_2atmpS839;
    moonbit_incref(_M0L4dataS835);
    #line 134 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0L6_2atmpS837 = _M0MPC14uint4UInt10to__uint16(_M0L6_2atmpS838);
    if (
      _M0L6_2atmpS836 < 0
      || _M0L6_2atmpS836 >= Moonbit_array_length(_M0L4dataS835)
    ) {
      #line 134 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      moonbit_panic();
    }
    _M0L4dataS835[_M0L6_2atmpS836] = _M0L6_2atmpS837;
    moonbit_decref(_M0L4dataS835);
    _M0L3lenS842 = _M0L4selfS78->$1;
    _M0L6_2atmpS841 = _M0L3lenS842 + 2;
    _M0L4selfS78->$1 = _M0L6_2atmpS841;
  } else {
    #line 137 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_14.data);
  }
  return 0;
}

int32_t _M0MPB13StringBuilder4grow(
  struct _M0TPB13StringBuilder* _M0L4selfS73,
  int32_t _M0L8requiredS74
) {
  uint16_t* _M0L4dataS813;
  int32_t _M0L6_2atmpS811;
  int32_t _M0L3lenS812;
  int32_t _M0L13new__capacityS72;
  uint16_t* _M0L4dataS808;
  int32_t _M0L6_2atmpS809;
  int32_t _M0L3lenS810;
  uint16_t* _M0L9new__dataS75;
  uint16_t* _M0L6_2aoldS1484;
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L4dataS813 = _M0L4selfS73->$0;
  _M0L6_2atmpS811 = Moonbit_array_length(_M0L4dataS813);
  _M0L3lenS812 = _M0L4selfS73->$1;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L13new__capacityS72
  = _M0FPB31stringbuilder__growth__capacity(_M0L6_2atmpS811, _M0L3lenS812, _M0L8requiredS74);
  _M0L4dataS808 = _M0L4selfS73->$0;
  moonbit_incref(_M0L4dataS808);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L6_2atmpS809 = _M0IPC16uint166UInt16PB7Default7default();
  _M0L3lenS810 = _M0L4selfS73->$1;
  #line 80 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L9new__dataS75
  = _M0MPC15array10FixedArray23make__and__blit_2einnerGkE(_M0L4dataS808, _M0L13new__capacityS72, _M0L6_2atmpS809, _M0L3lenS810, 0, 0);
  _M0L6_2aoldS1484 = _M0L4selfS73->$0;
  moonbit_decref(_M0L6_2aoldS1484);
  _M0L4selfS73->$0 = _M0L9new__dataS75;
  return 0;
}

int32_t _M0FPB31stringbuilder__growth__capacity(
  int32_t _M0L7currentS71,
  int32_t _M0L3lenS67,
  int32_t _M0L8requiredS66
) {
  int32_t _M0L5spaceS68;
  #line 48 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  if (_M0L8requiredS66 < _M0L3lenS67) {
    #line 55 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_15.data);
  }
  _M0L5spaceS68 = _M0L7currentS71;
  while (1) {
    if (_M0L5spaceS68 < _M0L8requiredS66) {
      int32_t _M0L4nextS69 = _M0L5spaceS68 * 2;
      if (_M0L4nextS69 <= _M0L5spaceS68) {
        return _M0L8requiredS66;
      }
      _M0L5spaceS68 = _M0L4nextS69;
      continue;
    } else {
      return _M0L5spaceS68;
    }
    break;
  }
}

int32_t _M0MPC14uint4UInt10to__uint16(uint32_t _M0L4selfS65) {
  int32_t _M0L6_2atmpS807;
  #line 2785 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS807 = *(int32_t*)&_M0L4selfS65;
  return (uint16_t)_M0L6_2atmpS807;
}

uint32_t _M0MPC14char4Char8to__uint(int32_t _M0L4selfS64) {
  int32_t _M0L6_2atmpS806;
  #line 1335 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS806 = _M0L4selfS64;
  return *(uint32_t*)&_M0L6_2atmpS806;
}

moonbit_string_t _M0MPB13StringBuilder10to__string(
  struct _M0TPB13StringBuilder* _M0L4selfS62
) {
  int32_t _M0L3lenS797;
  #line 181 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L3lenS797 = _M0L4selfS62->$1;
  if (_M0L3lenS797 == 0) {
    return (moonbit_string_t)moonbit_string_literal_6.data;
  } else {
    int32_t _M0L3lenS798 = _M0L4selfS62->$1;
    uint16_t* _M0L4dataS800 = _M0L4selfS62->$0;
    int32_t _M0L6_2atmpS799 = Moonbit_array_length(_M0L4dataS800);
    if (_M0L3lenS798 == _M0L6_2atmpS799) {
      uint16_t* _M0L4dataS801 = _M0L4selfS62->$0;
      moonbit_incref(_M0L4dataS801);
      return _M0L4dataS801;
    } else {
      uint16_t* _M0L4dataS802 = _M0L4selfS62->$0;
      int32_t _M0L3lenS803 = _M0L4selfS62->$1;
      int32_t _M0L6_2atmpS804;
      int32_t _M0L3lenS805;
      uint16_t* _M0L4dataS63;
      moonbit_incref(_M0L4dataS802);
      #line 190 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0L6_2atmpS804 = _M0IPC16uint166UInt16PB7Default7default();
      _M0L3lenS805 = _M0L4selfS62->$1;
      #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0L4dataS63
      = _M0MPC15array10FixedArray23make__and__blit_2einnerGkE(_M0L4dataS802, _M0L3lenS803, _M0L6_2atmpS804, _M0L3lenS805, 0, 0);
      return _M0L4dataS63;
    }
  }
}

uint16_t* _M0MPC15array10FixedArray23make__and__blit_2einnerGkE(
  uint16_t* _M0L3srcS59,
  int32_t _M0L13allocate__lenS55,
  int32_t _M0L4initS60,
  int32_t _M0L3lenS56,
  int32_t _M0L11src__offsetS57,
  int32_t _M0L11dst__offsetS58
) {
  int32_t _if__result_1549;
  #line 97 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
  if (_M0L13allocate__lenS55 >= 0) {
    if (_M0L3lenS56 >= 0) {
      if (_M0L11src__offsetS57 >= 0) {
        if (_M0L11dst__offsetS58 >= 0) {
          int32_t _M0L6_2atmpS793 = _M0L11src__offsetS57 + _M0L3lenS56;
          int32_t _M0L6_2atmpS794 = Moonbit_array_length(_M0L3srcS59);
          if (_M0L6_2atmpS793 <= _M0L6_2atmpS794) {
            int32_t _M0L6_2atmpS792 = _M0L11dst__offsetS58 + _M0L3lenS56;
            _if__result_1549 = _M0L6_2atmpS792 <= _M0L13allocate__lenS55;
          } else {
            _if__result_1549 = 0;
          }
        } else {
          _if__result_1549 = 0;
        }
      } else {
        _if__result_1549 = 0;
      }
    } else {
      _if__result_1549 = 0;
    }
  } else {
    _if__result_1549 = 0;
  }
  if (_if__result_1549) {
    #line 116 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    return _M0MPC15array10FixedArray23unsafe__make__and__blitGkE(_M0L3srcS59, _M0L13allocate__lenS55, _M0L4initS60, _M0L11src__offsetS57, _M0L11dst__offsetS58, _M0L3lenS56);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS61;
    int32_t _M0L6_2atmpS796;
    moonbit_string_t _M0L6_2atmpS795;
    uint16_t* _result_1550;
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0L18_2astring__builderS61
    = _M0MPB13StringBuilder21StringBuilder_2einner(89);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS61, (moonbit_string_t)moonbit_string_literal_16.data);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS61, _M0L13allocate__lenS55);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS61, (moonbit_string_t)moonbit_string_literal_17.data);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS61, _M0L11src__offsetS57);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS61, (moonbit_string_t)moonbit_string_literal_18.data);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS61, _M0L11dst__offsetS58);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS61, (moonbit_string_t)moonbit_string_literal_19.data);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS61, _M0L3lenS56);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS61, (moonbit_string_t)moonbit_string_literal_20.data);
    _M0L6_2atmpS796 = Moonbit_array_length(_M0L3srcS59);
    moonbit_decref(_M0L3srcS59);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS61, _M0L6_2atmpS796);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0L6_2atmpS795
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS61);
    moonbit_decref(_M0L18_2astring__builderS61);
    #line 112 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _result_1550 = _M0FPC15abort5abortGAkE(_M0L6_2atmpS795);
    moonbit_decref(_M0L6_2atmpS795);
    return _result_1550;
  }
}

uint16_t* _M0MPC15array10FixedArray23unsafe__make__and__blitGkE(
  uint16_t* _M0L3srcS52,
  int32_t _M0L13allocate__lenS49,
  int32_t _M0L4initS50,
  int32_t _M0L11src__offsetS53,
  int32_t _M0L11dst__offsetS51,
  int32_t _M0L9blit__lenS54
) {
  uint16_t* _M0L3dstS48;
  #line 79 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
  _M0L3dstS48
  = (uint16_t*)moonbit_make_string(_M0L13allocate__lenS49, _M0L4initS50);
  moonbit_incref(_M0L3dstS48);
  #line 90 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
  moonbit_unsafe_val_array_blit(_M0L3dstS48, _M0L11dst__offsetS51, _M0L3srcS52, _M0L11src__offsetS53, _M0L9blit__lenS54, sizeof(uint16_t));
  return _M0L3dstS48;
}

struct _M0TPB13StringBuilder* _M0MPB13StringBuilder21StringBuilder_2einner(
  int32_t _M0L10size__hintS46
) {
  int32_t _M0L7initialS45;
  uint16_t* _M0L4dataS47;
  struct _M0TPB13StringBuilder* _block_1551;
  #line 32 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  if (_M0L10size__hintS46 < 1) {
    _M0L7initialS45 = 1;
  } else {
    int32_t _M0L6_2atmpS791 = _M0L10size__hintS46 + 1;
    _M0L7initialS45 = _M0L6_2atmpS791 / 2;
  }
  _M0L4dataS47 = (uint16_t*)moonbit_make_string(_M0L7initialS45, 0);
  _block_1551
  = (struct _M0TPB13StringBuilder*)moonbit_malloc(sizeof(struct _M0TPB13StringBuilder));
  Moonbit_object_header(_block_1551)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 27, 0);
  _block_1551->$0 = _M0L4dataS47;
  _block_1551->$1 = 0;
  return _block_1551;
}

int32_t* _M0MPB18UninitializedArray23make__and__blit_2einnerGiE(
  int32_t* _M0L3srcS43,
  int32_t _M0L13allocate__lenS39,
  int32_t _M0L3lenS40,
  int32_t _M0L11src__offsetS41,
  int32_t _M0L11dst__offsetS42
) {
  int32_t _if__result_1552;
  #line 167 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  if (_M0L13allocate__lenS39 >= 0) {
    if (_M0L3lenS40 >= 0) {
      if (_M0L11src__offsetS41 >= 0) {
        if (_M0L11dst__offsetS42 >= 0) {
          int32_t _M0L6_2atmpS787 = _M0L11src__offsetS41 + _M0L3lenS40;
          int32_t _M0L6_2atmpS788;
          #line 179 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
          _M0L6_2atmpS788 = _M0MPB18UninitializedArray6lengthGiE(_M0L3srcS43);
          if (_M0L6_2atmpS787 <= _M0L6_2atmpS788) {
            int32_t _M0L6_2atmpS786 = _M0L11dst__offsetS42 + _M0L3lenS40;
            _if__result_1552 = _M0L6_2atmpS786 <= _M0L13allocate__lenS39;
          } else {
            _if__result_1552 = 0;
          }
        } else {
          _if__result_1552 = 0;
        }
      } else {
        _if__result_1552 = 0;
      }
    } else {
      _if__result_1552 = 0;
    }
  } else {
    _if__result_1552 = 0;
  }
  if (_if__result_1552) {
    #line 185 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    return _M0MPB18UninitializedArray23unsafe__make__and__blitGiE(_M0L3srcS43, _M0L13allocate__lenS39, _M0L11src__offsetS41, _M0L11dst__offsetS42, _M0L3lenS40);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS44;
    int32_t _M0L6_2atmpS790;
    moonbit_string_t _M0L6_2atmpS789;
    int32_t* _result_1553;
    #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L18_2astring__builderS44
    = _M0MPB13StringBuilder21StringBuilder_2einner(89);
    #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS44, (moonbit_string_t)moonbit_string_literal_16.data);
    #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS44, _M0L13allocate__lenS39);
    #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS44, (moonbit_string_t)moonbit_string_literal_17.data);
    #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS44, _M0L11src__offsetS41);
    #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS44, (moonbit_string_t)moonbit_string_literal_18.data);
    #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS44, _M0L11dst__offsetS42);
    #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS44, (moonbit_string_t)moonbit_string_literal_19.data);
    #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS44, _M0L3lenS40);
    #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS44, (moonbit_string_t)moonbit_string_literal_20.data);
    #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS790 = _M0MPB18UninitializedArray6lengthGiE(_M0L3srcS43);
    moonbit_decref(_M0L3srcS43);
    #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS44, _M0L6_2atmpS790);
    #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS789
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS44);
    moonbit_decref(_M0L18_2astring__builderS44);
    #line 181 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _result_1553
    = _M0FPC15abort5abortGRPB18UninitializedArrayGiEE(_M0L6_2atmpS789);
    moonbit_decref(_M0L6_2atmpS789);
    return _result_1553;
  }
}

int32_t _M0MPB13StringBuilder13write__objectGiE(
  struct _M0TPB13StringBuilder* _M0L4selfS36,
  int32_t _M0L3objS35
) {
  struct _M0TPB6Logger _M0L6_2atmpS784;
  #line 17 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  moonbit_incref(_M0L4selfS36);
  _M0L6_2atmpS784
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS36
  };
  #line 23 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  _M0IP016_24default__implPB4Show6outputGiE(_M0L3objS35, _M0L6_2atmpS784);
  if (_M0L6_2atmpS784.$1) {
    moonbit_decref(_M0L6_2atmpS784.$1);
  }
  return 0;
}

int32_t _M0MPB13StringBuilder13write__objectGmE(
  struct _M0TPB13StringBuilder* _M0L4selfS38,
  uint64_t _M0L3objS37
) {
  struct _M0TPB6Logger _M0L6_2atmpS785;
  #line 17 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  moonbit_incref(_M0L4selfS38);
  _M0L6_2atmpS785
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS38
  };
  #line 23 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  _M0IP016_24default__implPB4Show6outputGmE(_M0L3objS37, _M0L6_2atmpS785);
  if (_M0L6_2atmpS785.$1) {
    moonbit_decref(_M0L6_2atmpS785.$1);
  }
  return 0;
}

int32_t* _M0MPB18UninitializedArray23unsafe__make__and__blitGiE(
  int32_t* _M0L3srcS32,
  int32_t _M0L13allocate__lenS30,
  int32_t _M0L11src__offsetS33,
  int32_t _M0L11dst__offsetS31,
  int32_t _M0L9blit__lenS34
) {
  int32_t* _M0L3dstS29;
  #line 132 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  _M0L3dstS29
  = (int32_t*)moonbit_make_int32_array_raw(_M0L13allocate__lenS30);
  #line 142 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  _M0MPB18UninitializedArray12unsafe__blitGiE(_M0L3dstS29, _M0L11dst__offsetS31, _M0L3srcS32, _M0L11src__offsetS33, _M0L9blit__lenS34);
  moonbit_decref(_M0L3srcS32);
  return _M0L3dstS29;
}

int32_t _M0MPB18UninitializedArray12unsafe__blitGiE(
  int32_t* _M0L3dstS24,
  int32_t _M0L11dst__offsetS25,
  int32_t* _M0L3srcS26,
  int32_t _M0L11src__offsetS27,
  int32_t _M0L3lenS28
) {
  #line 118 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  moonbit_incref(_M0L3srcS26);
  moonbit_incref(_M0L3dstS24);
  #line 127 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  moonbit_unsafe_val_array_blit(_M0L3dstS24, _M0L11dst__offsetS25, _M0L3srcS26, _M0L11src__offsetS27, _M0L3lenS28, sizeof(int32_t));
  return 0;
}

int32_t _M0MPC15array10FixedArray12unsafe__blitGRPB17UnsafeMaybeUninitGiEE(
  int32_t* _M0L3dstS6,
  int32_t _M0L11dst__offsetS8,
  int32_t* _M0L3srcS7,
  int32_t _M0L11src__offsetS9,
  int32_t _M0L3lenS11
) {
  #line 38 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
  if (_M0L3dstS6 == _M0L3srcS7 && _M0L11dst__offsetS8 < _M0L11src__offsetS9) {
    int32_t _M0L1iS10 = 0;
    while (1) {
      if (_M0L1iS10 < _M0L3lenS11) {
        int32_t _M0L6_2atmpS766 = _M0L11dst__offsetS8 + _M0L1iS10;
        int32_t _M0L6_2atmpS768 = _M0L11src__offsetS9 + _M0L1iS10;
        int32_t _M0L6_2atmpS767;
        int32_t _M0L6_2atmpS769;
        if (
          _M0L6_2atmpS768 < 0
          || _M0L6_2atmpS768 >= Moonbit_array_length(_M0L3srcS7)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS767 = (int32_t)_M0L3srcS7[_M0L6_2atmpS768];
        if (
          _M0L6_2atmpS766 < 0
          || _M0L6_2atmpS766 >= Moonbit_array_length(_M0L3dstS6)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS6[_M0L6_2atmpS766] = _M0L6_2atmpS767;
        _M0L6_2atmpS769 = _M0L1iS10 + 1;
        _M0L1iS10 = _M0L6_2atmpS769;
        continue;
      } else {
        moonbit_decref(_M0L3srcS7);
        moonbit_decref(_M0L3dstS6);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS774 = _M0L3lenS11 - 1;
    int32_t _M0L1iS13 = _M0L6_2atmpS774;
    while (1) {
      if (_M0L1iS13 >= 0) {
        int32_t _M0L6_2atmpS770 = _M0L11dst__offsetS8 + _M0L1iS13;
        int32_t _M0L6_2atmpS772 = _M0L11src__offsetS9 + _M0L1iS13;
        int32_t _M0L6_2atmpS771;
        int32_t _M0L6_2atmpS773;
        if (
          _M0L6_2atmpS772 < 0
          || _M0L6_2atmpS772 >= Moonbit_array_length(_M0L3srcS7)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS771 = (int32_t)_M0L3srcS7[_M0L6_2atmpS772];
        if (
          _M0L6_2atmpS770 < 0
          || _M0L6_2atmpS770 >= Moonbit_array_length(_M0L3dstS6)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS6[_M0L6_2atmpS770] = _M0L6_2atmpS771;
        _M0L6_2atmpS773 = _M0L1iS13 - 1;
        _M0L1iS13 = _M0L6_2atmpS773;
        continue;
      } else {
        moonbit_decref(_M0L3srcS7);
        moonbit_decref(_M0L3dstS6);
      }
      break;
    }
  }
  return 0;
}

int32_t _M0MPC15array10FixedArray12unsafe__blitGkE(
  uint16_t* _M0L3dstS15,
  int32_t _M0L11dst__offsetS17,
  uint16_t* _M0L3srcS16,
  int32_t _M0L11src__offsetS18,
  int32_t _M0L3lenS20
) {
  #line 38 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
  if (
    _M0L3dstS15 == _M0L3srcS16 && _M0L11dst__offsetS17 < _M0L11src__offsetS18
  ) {
    int32_t _M0L1iS19 = 0;
    while (1) {
      if (_M0L1iS19 < _M0L3lenS20) {
        int32_t _M0L6_2atmpS775 = _M0L11dst__offsetS17 + _M0L1iS19;
        int32_t _M0L6_2atmpS777 = _M0L11src__offsetS18 + _M0L1iS19;
        int32_t _M0L6_2atmpS776;
        int32_t _M0L6_2atmpS778;
        if (
          _M0L6_2atmpS777 < 0
          || _M0L6_2atmpS777 >= Moonbit_array_length(_M0L3srcS16)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS776 = (int32_t)_M0L3srcS16[_M0L6_2atmpS777];
        if (
          _M0L6_2atmpS775 < 0
          || _M0L6_2atmpS775 >= Moonbit_array_length(_M0L3dstS15)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS15[_M0L6_2atmpS775] = _M0L6_2atmpS776;
        _M0L6_2atmpS778 = _M0L1iS19 + 1;
        _M0L1iS19 = _M0L6_2atmpS778;
        continue;
      } else {
        moonbit_decref(_M0L3srcS16);
        moonbit_decref(_M0L3dstS15);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS783 = _M0L3lenS20 - 1;
    int32_t _M0L1iS22 = _M0L6_2atmpS783;
    while (1) {
      if (_M0L1iS22 >= 0) {
        int32_t _M0L6_2atmpS779 = _M0L11dst__offsetS17 + _M0L1iS22;
        int32_t _M0L6_2atmpS781 = _M0L11src__offsetS18 + _M0L1iS22;
        int32_t _M0L6_2atmpS780;
        int32_t _M0L6_2atmpS782;
        if (
          _M0L6_2atmpS781 < 0
          || _M0L6_2atmpS781 >= Moonbit_array_length(_M0L3srcS16)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS780 = (int32_t)_M0L3srcS16[_M0L6_2atmpS781];
        if (
          _M0L6_2atmpS779 < 0
          || _M0L6_2atmpS779 >= Moonbit_array_length(_M0L3dstS15)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS15[_M0L6_2atmpS779] = _M0L6_2atmpS780;
        _M0L6_2atmpS782 = _M0L1iS22 - 1;
        _M0L1iS22 = _M0L6_2atmpS782;
        continue;
      } else {
        moonbit_decref(_M0L3srcS16);
        moonbit_decref(_M0L3dstS15);
      }
      break;
    }
  }
  return 0;
}

int32_t _M0MPB18UninitializedArray6lengthGiE(int32_t* _M0L4selfS5) {
  #line 112 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  return Moonbit_array_length(_M0L4selfS5);
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

int32_t _M0FPC15abort5abortGiE(moonbit_string_t _M0L3msgS3) {
  #line 47 "C:\\Users\\31379\\.moon\\lib\\core\\abort\\abort.mbt"
  #line 49 "C:\\Users\\31379\\.moon\\lib\\core\\abort\\abort.mbt"
  moonbit_println(_M0L3msgS3);
  #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\abort\\abort.mbt"
  moonbit_panic();
}

uint16_t* _M0FPC15abort5abortGAkE(moonbit_string_t _M0L3msgS4) {
  #line 47 "C:\\Users\\31379\\.moon\\lib\\core\\abort\\abort.mbt"
  #line 49 "C:\\Users\\31379\\.moon\\lib\\core\\abort\\abort.mbt"
  moonbit_println(_M0L3msgS4);
  #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\abort\\abort.mbt"
  moonbit_panic();
}

int32_t _M0IP016_24default__implPB6Logger61write_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLoggerGRPB13StringBuilderE(
  void* _M0L11_2aobj__ptrS674,
  struct _M0TPB4Show _M0L8_2aparamS673
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS672 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS674;
  _M0IP016_24default__implPB6Logger5writeGRPB13StringBuilderE(_M0L7_2aselfS672, _M0L8_2aparamS673);
  return 0;
}

int32_t _M0IP016_24default__implPB6Logger84write__string__interpolation_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLoggerGRPB13StringBuilderE(
  void* _M0L11_2aobj__ptrS671,
  struct _M0TPB4Show _M0L8_2aparamS670
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS669 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS671;
  _M0IP016_24default__implPB6Logger28write__string__interpolationGRPB13StringBuilderE(_M0L7_2aselfS669, _M0L8_2aparamS670);
  return 0;
}

int32_t _M0IPB13StringBuilderPB6Logger67write__char_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLogger(
  void* _M0L11_2aobj__ptrS668,
  int32_t _M0L8_2aparamS667
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS666 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS668;
  _M0IPB13StringBuilderPB6Logger11write__char(_M0L7_2aselfS666, _M0L8_2aparamS667);
  return 0;
}

int32_t _M0IPB13StringBuilderPB6Logger67write__view_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLogger(
  void* _M0L11_2aobj__ptrS665,
  struct _M0TPC16string10StringView _M0L8_2aparamS664
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS663 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS665;
  _M0IPB13StringBuilderPB6Logger11write__view(_M0L7_2aselfS663, _M0L8_2aparamS664);
  return 0;
}

int32_t _M0IP016_24default__implPB6Logger72write__substring_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLoggerGRPB13StringBuilderE(
  void* _M0L11_2aobj__ptrS662,
  moonbit_string_t _M0L8_2aparamS659,
  int32_t _M0L8_2aparamS660,
  int32_t _M0L8_2aparamS661
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS658 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS662;
  _M0IP016_24default__implPB6Logger16write__substringGRPB13StringBuilderE(_M0L7_2aselfS658, _M0L8_2aparamS659, _M0L8_2aparamS660, _M0L8_2aparamS661);
  return 0;
}

int32_t _M0IPB13StringBuilderPB6Logger69write__string_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLogger(
  void* _M0L11_2aobj__ptrS657,
  moonbit_string_t _M0L8_2aparamS656
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS655 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS657;
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L7_2aselfS655, _M0L8_2aparamS656);
  return 0;
}

void moonbit_init() {
  moonbit_layout_table = moonbit_layout_table_data;
}

int main(int argc, char** argv) {
  struct _M0TP26RiantR8snn__mbt8Receptor* _M0L4ampaS634;
  struct _M0TP26RiantR8snn__mbt8Receptor* _M0L4nmdaS635;
  struct _M0TP26RiantR8snn__mbt8Receptor* _M0L5gabaaS636;
  struct _M0TP26RiantR8snn__mbt8Receptor* _M0L5gababS637;
  float _M0L4gsynS677;
  moonbit_string_t _M0L6_2atmpS676;
  moonbit_string_t _M0L6_2atmpS675;
  int32_t _M0L8is__nmdaS684;
  moonbit_string_t _M0L6_2atmpS683;
  moonbit_string_t _M0L6_2atmpS682;
  moonbit_string_t _M0L6_2atmpS679;
  float _M0L4gsynS681;
  moonbit_string_t _M0L6_2atmpS680;
  moonbit_string_t _M0L6_2atmpS678;
  float _M0L4gsynS687;
  moonbit_string_t _M0L6_2atmpS686;
  moonbit_string_t _M0L6_2atmpS685;
  float _M0L4gsynS690;
  moonbit_string_t _M0L6_2atmpS689;
  moonbit_string_t _M0L6_2atmpS688;
  struct _M0TP26RiantR8snn__mbt13Glutamatergic* _M0L3gluS638;
  struct _M0TP26RiantR8snn__mbt9GABAergic* _M0L4gabaS639;
  struct _M0TP26RiantR8snn__mbt9Receptors* _M0L2rsS640;
  struct _M0TP26RiantR8snn__mbt8Receptor** _M0L6_2atmpS765;
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt8ReceptorE* _M0L9recs__arrS641;
  struct _M0TP26RiantR8snn__mbt17ReceptorsByTarget* _M0L1rS642;
  struct _M0TPB5ArrayGiE* _M0L3gluS700;
  int32_t _M0L6_2atmpS699;
  moonbit_string_t _M0L6_2atmpS698;
  moonbit_string_t _M0L6_2atmpS697;
  moonbit_string_t _M0L6_2atmpS693;
  struct _M0TPB5ArrayGiE* _M0L4gabaS696;
  int32_t _M0L6_2atmpS695;
  moonbit_string_t _M0L6_2atmpS694;
  moonbit_string_t _M0L6_2atmpS692;
  moonbit_string_t _M0L6_2atmpS691;
  struct _M0TPB5ArrayGiE* _M0L3gluS719;
  int32_t _M0L6_2atmpS718;
  moonbit_string_t _M0L6_2atmpS717;
  moonbit_string_t _M0L6_2atmpS716;
  moonbit_string_t _M0L6_2atmpS712;
  struct _M0TPB5ArrayGiE* _M0L3gluS715;
  int32_t _M0L6_2atmpS714;
  moonbit_string_t _M0L6_2atmpS713;
  moonbit_string_t _M0L6_2atmpS711;
  moonbit_string_t _M0L6_2atmpS707;
  struct _M0TPB5ArrayGiE* _M0L4gabaS710;
  int32_t _M0L6_2atmpS709;
  moonbit_string_t _M0L6_2atmpS708;
  moonbit_string_t _M0L6_2atmpS706;
  moonbit_string_t _M0L6_2atmpS702;
  struct _M0TPB5ArrayGiE* _M0L4gabaS705;
  int32_t _M0L6_2acntS1489;
  int32_t _M0L6_2atmpS704;
  moonbit_string_t _M0L6_2atmpS703;
  moonbit_string_t _M0L6_2atmpS701;
  float _M0L11alpha__ampaS643;
  float _M0L10norm__ampaS644;
  moonbit_string_t _M0L6_2atmpS724;
  moonbit_string_t _M0L6_2atmpS723;
  moonbit_string_t _M0L6_2atmpS721;
  moonbit_string_t _M0L6_2atmpS722;
  moonbit_string_t _M0L6_2atmpS720;
  int32_t _M0L1nS645;
  int32_t _M0L6_2atmpS764;
  struct _M0TPB5ArrayGfE* _M0L6g__matS646;
  struct _M0TPB8MutLocalGiE* _M0L1kS647;
  struct _M0TPB5ArrayGfE* _M0L1vS649;
  int32_t _M0L6_2atmpS743;
  int32_t _M0L6_2atmpS744;
  int32_t _M0L6_2atmpS745;
  int32_t _M0L6_2atmpS746;
  struct _M0TPB5ArrayGfE* _M0L3outS650;
  struct _M0TP26RiantR8snn__mbt21NMDAVoltageDependency* _M0L6_2atmpS747;
  struct _M0TPB8MutLocalGiE* _M0L1jS651;
  moonbit_runtime_init(argc, argv);
  moonbit_init();
  #line 17 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\receptors_demo\\main.mbt"
  _M0L4ampaS634
  = _M0MP26RiantR8snn__mbt8Receptor6simple(0x0p+0f, 0x1p+0f, 0x1.8p+2f, 0x1p+0f, (moonbit_string_t)moonbit_string_literal_0.data);
  #line 20 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\receptors_demo\\main.mbt"
  _M0L4nmdaS635
  = _M0MP26RiantR8snn__mbt8Receptor4nmda(0x0p+0f, 0x1p+0f, 0x1.9p+6f, 0x1p+0f);
  #line 21 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\receptors_demo\\main.mbt"
  _M0L5gabaaS636
  = _M0MP26RiantR8snn__mbt8Receptor6simple(-0x1.2cp+6f, 0x1p-1f, 0x1p+1f, 0x1p+0f, (moonbit_string_t)moonbit_string_literal_1.data);
  #line 24 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\receptors_demo\\main.mbt"
  _M0L5gababS637
  = _M0MP26RiantR8snn__mbt8Receptor6simple(-0x1.2cp+6f, 0x1p-1f, 0x1.4p+4f, 0x1p+0f, (moonbit_string_t)moonbit_string_literal_1.data);
  #line 27 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\receptors_demo\\main.mbt"
  _M0FPB7printlnGsE((moonbit_string_t)moonbit_string_literal_21.data);
  _M0L4gsynS677 = _M0L4ampaS634->$4;
  #line 30 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\receptors_demo\\main.mbt"
  _M0L6_2atmpS676 = _M0IPC15float5FloatPB4Show10to__string(_M0L4gsynS677);
  #line 29 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\receptors_demo\\main.mbt"
  _M0L6_2atmpS675
  = moonbit_add_string((moonbit_string_t)moonbit_string_literal_22.data, _M0L6_2atmpS676);
  moonbit_decref(_M0L6_2atmpS676);
  #line 28 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\receptors_demo\\main.mbt"
  _M0FPB7printlnGsE(_M0L6_2atmpS675);
  moonbit_decref(_M0L6_2atmpS675);
  _M0L8is__nmdaS684 = _M0L4nmdaS635->$8;
  #line 34 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\receptors_demo\\main.mbt"
  _M0L6_2atmpS683 = _M0IPC14bool4BoolPB4Show10to__string(_M0L8is__nmdaS684);
  #line 33 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\receptors_demo\\main.mbt"
  _M0L6_2atmpS682
  = moonbit_add_string((moonbit_string_t)moonbit_string_literal_23.data, _M0L6_2atmpS683);
  moonbit_decref(_M0L6_2atmpS683);
  #line 33 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\receptors_demo\\main.mbt"
  _M0L6_2atmpS679
  = moonbit_add_string(_M0L6_2atmpS682, (moonbit_string_t)moonbit_string_literal_24.data);
  moonbit_decref(_M0L6_2atmpS682);
  _M0L4gsynS681 = _M0L4nmdaS635->$4;
  #line 34 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\receptors_demo\\main.mbt"
  _M0L6_2atmpS680 = _M0IPC15float5FloatPB4Show10to__string(_M0L4gsynS681);
  #line 33 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\receptors_demo\\main.mbt"
  _M0L6_2atmpS678 = moonbit_add_string(_M0L6_2atmpS679, _M0L6_2atmpS680);
  moonbit_decref(_M0L6_2atmpS680);
  moonbit_decref(_M0L6_2atmpS679);
  #line 32 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\receptors_demo\\main.mbt"
  _M0FPB7printlnGsE(_M0L6_2atmpS678);
  moonbit_decref(_M0L6_2atmpS678);
  _M0L4gsynS687 = _M0L5gabaaS636->$4;
  #line 38 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\receptors_demo\\main.mbt"
  _M0L6_2atmpS686 = _M0IPC15float5FloatPB4Show10to__string(_M0L4gsynS687);
  #line 37 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\receptors_demo\\main.mbt"
  _M0L6_2atmpS685
  = moonbit_add_string((moonbit_string_t)moonbit_string_literal_25.data, _M0L6_2atmpS686);
  moonbit_decref(_M0L6_2atmpS686);
  #line 36 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\receptors_demo\\main.mbt"
  _M0FPB7printlnGsE(_M0L6_2atmpS685);
  moonbit_decref(_M0L6_2atmpS685);
  _M0L4gsynS690 = _M0L5gababS637->$4;
  #line 42 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\receptors_demo\\main.mbt"
  _M0L6_2atmpS689 = _M0IPC15float5FloatPB4Show10to__string(_M0L4gsynS690);
  #line 41 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\receptors_demo\\main.mbt"
  _M0L6_2atmpS688
  = moonbit_add_string((moonbit_string_t)moonbit_string_literal_26.data, _M0L6_2atmpS689);
  moonbit_decref(_M0L6_2atmpS689);
  #line 40 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\receptors_demo\\main.mbt"
  _M0FPB7printlnGsE(_M0L6_2atmpS688);
  moonbit_decref(_M0L6_2atmpS688);
  #line 45 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\receptors_demo\\main.mbt"
  _M0L3gluS638
  = _M0MP26RiantR8snn__mbt13Glutamatergic6custom(_M0L4ampaS634, _M0L4nmdaS635);
  #line 46 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\receptors_demo\\main.mbt"
  _M0L4gabaS639
  = _M0MP26RiantR8snn__mbt9GABAergic6custom(_M0L5gabaaS636, _M0L5gababS637);
  #line 47 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\receptors_demo\\main.mbt"
  _M0L2rsS640
  = _M0MP26RiantR8snn__mbt9Receptors10from__pair(_M0L3gluS638, _M0L4gabaS639);
  moonbit_decref(_M0L3gluS638);
  moonbit_decref(_M0L4gabaS639);
  _M0L6_2atmpS765
  = (struct _M0TP26RiantR8snn__mbt8Receptor**)moonbit_make_ref_array_raw(4);
  _M0L6_2atmpS765[0] = _M0L4ampaS634;
  _M0L6_2atmpS765[1] = _M0L4nmdaS635;
  _M0L6_2atmpS765[2] = _M0L5gabaaS636;
  _M0L6_2atmpS765[3] = _M0L5gababS637;
  _M0L9recs__arrS641
  = (struct _M0TPB5ArrayGRP26RiantR8snn__mbt8ReceptorE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGRP26RiantR8snn__mbt8ReceptorE));
  Moonbit_object_header(_M0L9recs__arrS641)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 7, 0);
  _M0L9recs__arrS641->$0 = _M0L6_2atmpS765;
  _M0L9recs__arrS641->$1 = 4;
  #line 50 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\receptors_demo\\main.mbt"
  _M0L1rS642 = _M0FP26RiantR8snn__mbt16infer__receptors(_M0L9recs__arrS641);
  moonbit_decref(_M0L9recs__arrS641);
  _M0L3gluS700 = _M0L1rS642->$0;
  moonbit_incref(_M0L3gluS700);
  #line 53 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\receptors_demo\\main.mbt"
  _M0L6_2atmpS699 = _M0MPC15array5Array6lengthGiE(_M0L3gluS700);
  moonbit_decref(_M0L3gluS700);
  #line 53 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\receptors_demo\\main.mbt"
  _M0L6_2atmpS698 = _M0MPC13int3Int18to__string_2einner(_M0L6_2atmpS699, 10);
  #line 52 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\receptors_demo\\main.mbt"
  _M0L6_2atmpS697
  = moonbit_add_string((moonbit_string_t)moonbit_string_literal_27.data, _M0L6_2atmpS698);
  moonbit_decref(_M0L6_2atmpS698);
  #line 52 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\receptors_demo\\main.mbt"
  _M0L6_2atmpS693
  = moonbit_add_string(_M0L6_2atmpS697, (moonbit_string_t)moonbit_string_literal_28.data);
  moonbit_decref(_M0L6_2atmpS697);
  _M0L4gabaS696 = _M0L1rS642->$1;
  moonbit_incref(_M0L4gabaS696);
  #line 54 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\receptors_demo\\main.mbt"
  _M0L6_2atmpS695 = _M0MPC15array5Array6lengthGiE(_M0L4gabaS696);
  moonbit_decref(_M0L4gabaS696);
  #line 54 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\receptors_demo\\main.mbt"
  _M0L6_2atmpS694 = _M0MPC13int3Int18to__string_2einner(_M0L6_2atmpS695, 10);
  #line 52 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\receptors_demo\\main.mbt"
  _M0L6_2atmpS692 = moonbit_add_string(_M0L6_2atmpS693, _M0L6_2atmpS694);
  moonbit_decref(_M0L6_2atmpS694);
  moonbit_decref(_M0L6_2atmpS693);
  #line 52 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\receptors_demo\\main.mbt"
  _M0L6_2atmpS691
  = moonbit_add_string(_M0L6_2atmpS692, (moonbit_string_t)moonbit_string_literal_29.data);
  moonbit_decref(_M0L6_2atmpS692);
  #line 51 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\receptors_demo\\main.mbt"
  _M0FPB7printlnGsE(_M0L6_2atmpS691);
  moonbit_decref(_M0L6_2atmpS691);
  _M0L3gluS719 = _M0L1rS642->$0;
  moonbit_incref(_M0L3gluS719);
  #line 57 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\receptors_demo\\main.mbt"
  _M0L6_2atmpS718 = _M0MPC15array5Array2atGiE(_M0L3gluS719, 0);
  moonbit_decref(_M0L3gluS719);
  #line 57 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\receptors_demo\\main.mbt"
  _M0L6_2atmpS717 = _M0MPC13int3Int18to__string_2einner(_M0L6_2atmpS718, 10);
  #line 57 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\receptors_demo\\main.mbt"
  _M0L6_2atmpS716
  = moonbit_add_string((moonbit_string_t)moonbit_string_literal_30.data, _M0L6_2atmpS717);
  moonbit_decref(_M0L6_2atmpS717);
  #line 57 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\receptors_demo\\main.mbt"
  _M0L6_2atmpS712
  = moonbit_add_string(_M0L6_2atmpS716, (moonbit_string_t)moonbit_string_literal_31.data);
  moonbit_decref(_M0L6_2atmpS716);
  _M0L3gluS715 = _M0L1rS642->$0;
  moonbit_incref(_M0L3gluS715);
  #line 57 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\receptors_demo\\main.mbt"
  _M0L6_2atmpS714 = _M0MPC15array5Array2atGiE(_M0L3gluS715, 1);
  moonbit_decref(_M0L3gluS715);
  #line 57 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\receptors_demo\\main.mbt"
  _M0L6_2atmpS713 = _M0MPC13int3Int18to__string_2einner(_M0L6_2atmpS714, 10);
  #line 57 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\receptors_demo\\main.mbt"
  _M0L6_2atmpS711 = moonbit_add_string(_M0L6_2atmpS712, _M0L6_2atmpS713);
  moonbit_decref(_M0L6_2atmpS713);
  moonbit_decref(_M0L6_2atmpS712);
  #line 57 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\receptors_demo\\main.mbt"
  _M0L6_2atmpS707
  = moonbit_add_string(_M0L6_2atmpS711, (moonbit_string_t)moonbit_string_literal_32.data);
  moonbit_decref(_M0L6_2atmpS711);
  _M0L4gabaS710 = _M0L1rS642->$1;
  moonbit_incref(_M0L4gabaS710);
  #line 58 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\receptors_demo\\main.mbt"
  _M0L6_2atmpS709 = _M0MPC15array5Array2atGiE(_M0L4gabaS710, 0);
  moonbit_decref(_M0L4gabaS710);
  #line 58 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\receptors_demo\\main.mbt"
  _M0L6_2atmpS708 = _M0MPC13int3Int18to__string_2einner(_M0L6_2atmpS709, 10);
  #line 57 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\receptors_demo\\main.mbt"
  _M0L6_2atmpS706 = moonbit_add_string(_M0L6_2atmpS707, _M0L6_2atmpS708);
  moonbit_decref(_M0L6_2atmpS708);
  moonbit_decref(_M0L6_2atmpS707);
  #line 57 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\receptors_demo\\main.mbt"
  _M0L6_2atmpS702
  = moonbit_add_string(_M0L6_2atmpS706, (moonbit_string_t)moonbit_string_literal_33.data);
  moonbit_decref(_M0L6_2atmpS706);
  _M0L4gabaS705 = _M0L1rS642->$1;
  _M0L6_2acntS1489 = Moonbit_rc_count(Moonbit_object_header(_M0L1rS642));
  if (_M0L6_2acntS1489 > 1) {
    int32_t _M0L11_2anew__cntS1491 = _M0L6_2acntS1489 - 1;
    Moonbit_set_rc_count(Moonbit_object_header(_M0L1rS642), _M0L11_2anew__cntS1491);
    moonbit_incref(_M0L4gabaS705);
  } else if (_M0L6_2acntS1489 == 1) {
    struct _M0TPB5ArrayGiE* _M0L8_2afieldS1490 = _M0L1rS642->$0;
    moonbit_decref(_M0L8_2afieldS1490);
    #line 50 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\receptors_demo\\main.mbt"
    moonbit_free(_M0L1rS642);
  }
  #line 58 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\receptors_demo\\main.mbt"
  _M0L6_2atmpS704 = _M0MPC15array5Array2atGiE(_M0L4gabaS705, 1);
  moonbit_decref(_M0L4gabaS705);
  #line 58 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\receptors_demo\\main.mbt"
  _M0L6_2atmpS703 = _M0MPC13int3Int18to__string_2einner(_M0L6_2atmpS704, 10);
  #line 57 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\receptors_demo\\main.mbt"
  _M0L6_2atmpS701 = moonbit_add_string(_M0L6_2atmpS702, _M0L6_2atmpS703);
  moonbit_decref(_M0L6_2atmpS703);
  moonbit_decref(_M0L6_2atmpS702);
  #line 56 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\receptors_demo\\main.mbt"
  _M0FPB7printlnGsE(_M0L6_2atmpS701);
  moonbit_decref(_M0L6_2atmpS701);
  #line 61 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\receptors_demo\\main.mbt"
  _M0L11alpha__ampaS643
  = _M0FP26RiantR8snn__mbt14alpha__synapse(0x1p+0f, 0x1.8p+2f);
  #line 62 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\receptors_demo\\main.mbt"
  _M0L10norm__ampaS644
  = _M0FP26RiantR8snn__mbt13norm__synapse(0x1p+0f, 0x1.8p+2f);
  #line 65 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\receptors_demo\\main.mbt"
  _M0L6_2atmpS724
  = _M0IPC15float5FloatPB4Show10to__string(_M0L11alpha__ampaS643);
  #line 64 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\receptors_demo\\main.mbt"
  _M0L6_2atmpS723
  = moonbit_add_string((moonbit_string_t)moonbit_string_literal_34.data, _M0L6_2atmpS724);
  moonbit_decref(_M0L6_2atmpS724);
  #line 64 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\receptors_demo\\main.mbt"
  _M0L6_2atmpS721
  = moonbit_add_string(_M0L6_2atmpS723, (moonbit_string_t)moonbit_string_literal_35.data);
  moonbit_decref(_M0L6_2atmpS723);
  #line 65 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\receptors_demo\\main.mbt"
  _M0L6_2atmpS722
  = _M0IPC15float5FloatPB4Show10to__string(_M0L10norm__ampaS644);
  #line 64 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\receptors_demo\\main.mbt"
  _M0L6_2atmpS720 = moonbit_add_string(_M0L6_2atmpS721, _M0L6_2atmpS722);
  moonbit_decref(_M0L6_2atmpS722);
  moonbit_decref(_M0L6_2atmpS721);
  #line 63 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\receptors_demo\\main.mbt"
  _M0FPB7printlnGsE(_M0L6_2atmpS720);
  moonbit_decref(_M0L6_2atmpS720);
  _M0L1nS645 = 4;
  _M0L6_2atmpS764 = _M0L1nS645 * 4;
  #line 69 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\receptors_demo\\main.mbt"
  _M0L6g__matS646 = _M0MPC15array5Array4makeGfE(_M0L6_2atmpS764, 0x0p+0f);
  _M0L1kS647
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1kS647)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1kS647->$0 = 0;
  while (1) {
    int32_t _M0L3valS725 = _M0L1kS647->$0;
    if (_M0L3valS725 < _M0L1nS645) {
      int32_t _M0L3valS728 = _M0L1kS647->$0;
      int32_t _M0L6_2atmpS727 = _M0L3valS728 * 4;
      int32_t _M0L6_2atmpS726;
      int32_t _M0L3valS732;
      int32_t _M0L6_2atmpS731;
      int32_t _M0L6_2atmpS730;
      int32_t _M0L6_2atmpS729;
      int32_t _M0L3valS736;
      int32_t _M0L6_2atmpS735;
      int32_t _M0L6_2atmpS734;
      int32_t _M0L6_2atmpS733;
      int32_t _M0L3valS740;
      int32_t _M0L6_2atmpS739;
      int32_t _M0L6_2atmpS738;
      int32_t _M0L6_2atmpS737;
      int32_t _M0L3valS742;
      int32_t _M0L6_2atmpS741;
      #line 73 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\receptors_demo\\main.mbt"
      _M0L6_2atmpS726
      = _M0MPC15array5Array3setGfE(_M0L6g__matS646, _M0L6_2atmpS727, 0x1p+0f);
      _M0L3valS732 = _M0L1kS647->$0;
      _M0L6_2atmpS731 = _M0L3valS732 * 4;
      _M0L6_2atmpS730 = _M0L6_2atmpS731 + 1;
      #line 74 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\receptors_demo\\main.mbt"
      _M0L6_2atmpS729
      = _M0MPC15array5Array3setGfE(_M0L6g__matS646, _M0L6_2atmpS730, 0x0p+0f);
      _M0L3valS736 = _M0L1kS647->$0;
      _M0L6_2atmpS735 = _M0L3valS736 * 4;
      _M0L6_2atmpS734 = _M0L6_2atmpS735 + 2;
      #line 75 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\receptors_demo\\main.mbt"
      _M0L6_2atmpS733
      = _M0MPC15array5Array3setGfE(_M0L6g__matS646, _M0L6_2atmpS734, 0x1p-1f);
      _M0L3valS740 = _M0L1kS647->$0;
      _M0L6_2atmpS739 = _M0L3valS740 * 4;
      _M0L6_2atmpS738 = _M0L6_2atmpS739 + 3;
      #line 76 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\receptors_demo\\main.mbt"
      _M0L6_2atmpS737
      = _M0MPC15array5Array3setGfE(_M0L6g__matS646, _M0L6_2atmpS738, 0x0p+0f);
      _M0L3valS742 = _M0L1kS647->$0;
      _M0L6_2atmpS741 = _M0L3valS742 + 1;
      _M0L1kS647->$0 = _M0L6_2atmpS741;
      continue;
    } else {
      moonbit_decref(_M0L1kS647);
    }
    break;
  }
  #line 79 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\receptors_demo\\main.mbt"
  _M0L1vS649 = _M0MPC15array5Array4makeGfE(_M0L1nS645, 0x0p+0f);
  #line 80 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\receptors_demo\\main.mbt"
  _M0L6_2atmpS743 = _M0MPC15array5Array3setGfE(_M0L1vS649, 0, -0x1.18p+6f);
  #line 81 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\receptors_demo\\main.mbt"
  _M0L6_2atmpS744 = _M0MPC15array5Array3setGfE(_M0L1vS649, 1, -0x1.9p+5f);
  #line 82 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\receptors_demo\\main.mbt"
  _M0L6_2atmpS745 = _M0MPC15array5Array3setGfE(_M0L1vS649, 2, -0x1.2cp+6f);
  #line 83 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\receptors_demo\\main.mbt"
  _M0L6_2atmpS746 = _M0MPC15array5Array3setGfE(_M0L1vS649, 3, -0x1.04p+6f);
  #line 84 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\receptors_demo\\main.mbt"
  _M0L3outS650 = _M0MPC15array5Array4makeGfE(_M0L1nS645, 0x0p+0f);
  #line 86 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\receptors_demo\\main.mbt"
  _M0L6_2atmpS747 = _M0MP26RiantR8snn__mbt21NMDAVoltageDependency4eyal();
  #line 85 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\receptors_demo\\main.mbt"
  _M0FP26RiantR8snn__mbt18receptors__current(_M0L6g__matS646, _M0L1vS649, _M0L2rsS640, _M0L6_2atmpS747, _M0L3outS650);
  moonbit_decref(_M0L6g__matS646);
  moonbit_decref(_M0L2rsS640);
  moonbit_decref(_M0L6_2atmpS747);
  _M0L1jS651
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1jS651)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1jS651->$0 = 0;
  while (1) {
    int32_t _M0L3valS748 = _M0L1jS651->$0;
    if (_M0L3valS748 < _M0L1nS645) {
      int32_t _M0L3valS761 = _M0L1jS651->$0;
      moonbit_string_t _M0L6_2atmpS760;
      moonbit_string_t _M0L6_2atmpS759;
      moonbit_string_t _M0L6_2atmpS755;
      int32_t _M0L3valS758;
      float _M0L6_2atmpS757;
      moonbit_string_t _M0L6_2atmpS756;
      moonbit_string_t _M0L6_2atmpS754;
      moonbit_string_t _M0L6_2atmpS750;
      int32_t _M0L3valS753;
      float _M0L6_2atmpS752;
      moonbit_string_t _M0L6_2atmpS751;
      moonbit_string_t _M0L6_2atmpS749;
      int32_t _M0L3valS763;
      int32_t _M0L6_2atmpS762;
      #line 91 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\receptors_demo\\main.mbt"
      _M0L6_2atmpS760 = _M0MPC13int3Int18to__string_2einner(_M0L3valS761, 10);
      #line 91 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\receptors_demo\\main.mbt"
      _M0L6_2atmpS759
      = moonbit_add_string((moonbit_string_t)moonbit_string_literal_36.data, _M0L6_2atmpS760);
      moonbit_decref(_M0L6_2atmpS760);
      #line 91 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\receptors_demo\\main.mbt"
      _M0L6_2atmpS755
      = moonbit_add_string(_M0L6_2atmpS759, (moonbit_string_t)moonbit_string_literal_37.data);
      moonbit_decref(_M0L6_2atmpS759);
      _M0L3valS758 = _M0L1jS651->$0;
      #line 91 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\receptors_demo\\main.mbt"
      _M0L6_2atmpS757 = _M0MPC15array5Array2atGfE(_M0L1vS649, _M0L3valS758);
      #line 91 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\receptors_demo\\main.mbt"
      _M0L6_2atmpS756
      = _M0IPC15float5FloatPB4Show10to__string(_M0L6_2atmpS757);
      #line 91 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\receptors_demo\\main.mbt"
      _M0L6_2atmpS754 = moonbit_add_string(_M0L6_2atmpS755, _M0L6_2atmpS756);
      moonbit_decref(_M0L6_2atmpS756);
      moonbit_decref(_M0L6_2atmpS755);
      #line 91 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\receptors_demo\\main.mbt"
      _M0L6_2atmpS750
      = moonbit_add_string(_M0L6_2atmpS754, (moonbit_string_t)moonbit_string_literal_38.data);
      moonbit_decref(_M0L6_2atmpS754);
      _M0L3valS753 = _M0L1jS651->$0;
      #line 92 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\receptors_demo\\main.mbt"
      _M0L6_2atmpS752 = _M0MPC15array5Array2atGfE(_M0L3outS650, _M0L3valS753);
      #line 92 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\receptors_demo\\main.mbt"
      _M0L6_2atmpS751
      = _M0IPC15float5FloatPB4Show10to__string(_M0L6_2atmpS752);
      #line 91 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\receptors_demo\\main.mbt"
      _M0L6_2atmpS749 = moonbit_add_string(_M0L6_2atmpS750, _M0L6_2atmpS751);
      moonbit_decref(_M0L6_2atmpS751);
      moonbit_decref(_M0L6_2atmpS750);
      #line 90 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\receptors_demo\\main.mbt"
      _M0FPB7printlnGsE(_M0L6_2atmpS749);
      moonbit_decref(_M0L6_2atmpS749);
      _M0L3valS763 = _M0L1jS651->$0;
      _M0L6_2atmpS762 = _M0L3valS763 + 1;
      _M0L1jS651->$0 = _M0L6_2atmpS762;
      continue;
    } else {
      moonbit_decref(_M0L1jS651);
      moonbit_decref(_M0L3outS650);
      moonbit_decref(_M0L1vS649);
    }
    break;
  }
  #line 96 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\receptors_demo\\main.mbt"
  _M0FPB7printlnGsE((moonbit_string_t)moonbit_string_literal_39.data);
  return 0;
}