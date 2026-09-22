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

struct _M0TP26RiantR8snn__mbt13AdExPostSpike;

struct _M0TPB4Show;

struct _M0TPB13StringBuilder;

struct _M0TPB5ArrayGfE;

struct _M0TPB17FloatingDecimal64;

struct _M0TP26RiantR8snn__mbt9PostSpike;

struct _M0TP26RiantR8snn__mbt6Tripod;

struct _M0TP26RiantR8snn__mbt13AdExParameter;

struct _M0TUdiE;

struct _M0TPB5ArrayGbE;

struct _M0TPB5ArrayGRPB5ArrayGfEE;

struct _M0TP26RiantR8snn__mbt9IstdpRate;

struct _M0TP26RiantR8snn__mbt18IstdpRateVariables;

struct _M0BTPB6Logger;

struct _M0BTPB4Show;

struct _M0TPC16string10StringView;

struct _M0TP26RiantR8snn__mbt2IF;

struct _M0TP26RiantR8snn__mbt23IstdpPotentialVariables;

struct _M0KTPB6LoggerTPB13StringBuilder;

struct _M0TPB6Logger;

struct _M0TP26RiantR8snn__mbt24CompartmentSynapseTripod;

struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR;

struct _M0TP26RiantR8snn__mbt7Xoshiro;

struct _M0TUmmmmE;

struct _M0TPB5ArrayGiE;

struct _M0TPB19MulShiftAll64Result;

struct _M0TP26RiantR8snn__mbt11IFParameter;

struct _M0TP26RiantR8snn__mbt14IstdpPotential;

struct _M0TP26RiantR8snn__mbt19IstdpPotentialEntry;

struct _M0TP26RiantR8snn__mbt14IstdpRateEntry;

struct _M0TUddE;

struct _M0TP26RiantR8snn__mbt8Dendrite;

struct _M0TPB7Umul128;

struct _M0TPB8Pow5Pair;

struct _M0TPB8MutLocalGiE {
  int32_t $0;
  
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

struct _M0TP26RiantR8snn__mbt6Tripod {
  struct _M0TP26RiantR8snn__mbt13AdExParameter* $0;
  struct _M0TP26RiantR8snn__mbt13AdExPostSpike* $1;
  struct _M0TP26RiantR8snn__mbt8Dendrite* $2;
  struct _M0TP26RiantR8snn__mbt8Dendrite* $3;
  struct _M0TPB5ArrayGfE* $4;
  struct _M0TPB5ArrayGfE* $5;
  struct _M0TPB5ArrayGfE* $6;
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
  struct _M0TPB5ArrayGfE* $17;
  struct _M0TPB5ArrayGfE* $18;
  float $19;
  float $20;
  float $21;
  float $22;
  float $23;
  float $24;
  int32_t $25;
  struct _M0TPB5ArrayGfE* $26;
  struct _M0TPB5ArrayGfE* $27;
  struct _M0TPB5ArrayGfE* $28;
  struct _M0TPB5ArrayGfE* $29;
  struct _M0TPB5ArrayGbE* $30;
  struct _M0TPB5ArrayGfE* $31;
  struct _M0TPB5ArrayGiE* $32;
  struct _M0TPB5ArrayGfE* $33;
  struct _M0TPB5ArrayGfE* $34;
  struct _M0TPB5ArrayGfE* $35;
  struct _M0TPB5ArrayGfE* $36;
  struct _M0TPB5ArrayGfE* $37;
  
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

struct _M0TP26RiantR8snn__mbt23IstdpPotentialVariables {
  struct _M0TPB5ArrayGfE* $0;
  struct _M0TPB5ArrayGfE* $1;
  
};

struct _M0KTPB6LoggerTPB13StringBuilder {
  struct _M0BTPB6Logger* $0;
  void* $1;
  
};

struct _M0TPB6Logger {
  struct _M0BTPB6Logger* $0;
  void* $1;
  
};

struct _M0TP26RiantR8snn__mbt24CompartmentSynapseTripod {
  struct _M0TP26RiantR8snn__mbt2IF* $0;
  struct _M0TP26RiantR8snn__mbt6Tripod* $1;
  moonbit_string_t $2;
  moonbit_string_t $3;
  struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* $4;
  struct _M0TPB5ArrayGfE* $5;
  struct _M0TPB5ArrayGfE* $6;
  struct _M0TPB5ArrayGfE* $7;
  struct _M0TPB5ArrayGiE* $8;
  struct _M0TPB5ArrayGfE* $9;
  
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

struct _M0TP26RiantR8snn__mbt14IstdpPotential {
  float $0;
  float $1;
  float $2;
  float $3;
  float $4;
  
};

struct _M0TP26RiantR8snn__mbt19IstdpPotentialEntry {
  int32_t $0;
  int32_t $1;
  int32_t $2;
  struct _M0TP26RiantR8snn__mbt14IstdpPotential* $3;
  struct _M0TP26RiantR8snn__mbt23IstdpPotentialVariables* $4;
  struct _M0TPB5ArrayGfE* $5;
  
};

struct _M0TP26RiantR8snn__mbt14IstdpRateEntry {
  int32_t $0;
  int32_t $1;
  int32_t $2;
  struct _M0TP26RiantR8snn__mbt9IstdpRate* $3;
  struct _M0TP26RiantR8snn__mbt18IstdpRateVariables* $4;
  struct _M0TPB5ArrayGfE* $5;
  
};

struct _M0TUddE {
  double $0;
  double $1;
  
};

struct _M0TP26RiantR8snn__mbt8Dendrite {
  int32_t $0;
  struct _M0TPB5ArrayGfE* $1;
  struct _M0TPB5ArrayGfE* $2;
  struct _M0TPB5ArrayGfE* $3;
  struct _M0TPB5ArrayGfE* $4;
  struct _M0TPB5ArrayGfE* $5;
  struct _M0TPB5ArrayGfE* $6;
  struct _M0TPB5ArrayGfE* $7;
  
};

struct _M0TPB7Umul128 {
  uint64_t $0;
  uint64_t $1;
  
};

struct _M0TPB8Pow5Pair {
  uint64_t $0;
  uint64_t $1;
  
};

int32_t _M0FP26RiantR8snn__mbt28forward__compartment__tripod(
  struct _M0TP26RiantR8snn__mbt24CompartmentSynapseTripod*,
  float
);

int32_t _M0FP26RiantR8snn__mbt34apply__compartment__weight__tripod(
  struct _M0TP26RiantR8snn__mbt24CompartmentSynapseTripod*,
  int32_t,
  float
);

struct _M0TP26RiantR8snn__mbt24CompartmentSynapseTripod* _M0MP26RiantR8snn__mbt24CompartmentSynapseTripod6random(
  struct _M0TP26RiantR8snn__mbt2IF*,
  struct _M0TP26RiantR8snn__mbt6Tripod*,
  moonbit_string_t,
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

struct _M0TP26RiantR8snn__mbt19IstdpPotentialEntry* _M0MP26RiantR8snn__mbt19IstdpPotentialEntry3new(
  int32_t,
  int32_t,
  int32_t,
  struct _M0TP26RiantR8snn__mbt14IstdpPotential*
);

struct _M0TP26RiantR8snn__mbt19IstdpPotentialEntry* _M0MP26RiantR8snn__mbt19IstdpPotentialEntry11new_2einner(
  int32_t,
  int32_t,
  int32_t,
  struct _M0TP26RiantR8snn__mbt14IstdpPotential*
);

struct _M0TP26RiantR8snn__mbt23IstdpPotentialVariables* _M0MP26RiantR8snn__mbt23IstdpPotentialVariables3new(
  int32_t,
  int32_t
);

struct _M0TP26RiantR8snn__mbt14IstdpPotential* _M0MP26RiantR8snn__mbt14IstdpPotential3new(
  
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

struct _M0TP26RiantR8snn__mbt14IstdpRateEntry* _M0MP26RiantR8snn__mbt14IstdpRateEntry3new(
  int32_t,
  int32_t,
  int32_t,
  struct _M0TP26RiantR8snn__mbt9IstdpRate*
);

struct _M0TP26RiantR8snn__mbt14IstdpRateEntry* _M0MP26RiantR8snn__mbt14IstdpRateEntry11new_2einner(
  int32_t,
  int32_t,
  int32_t,
  struct _M0TP26RiantR8snn__mbt9IstdpRate*
);

struct _M0TP26RiantR8snn__mbt18IstdpRateVariables* _M0MP26RiantR8snn__mbt18IstdpRateVariables3new(
  int32_t,
  int32_t
);

struct _M0TP26RiantR8snn__mbt9IstdpRate* _M0MP26RiantR8snn__mbt9IstdpRate3new(
  
);

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

struct _M0TP26RiantR8snn__mbt13AdExParameter* _M0MP26RiantR8snn__mbt13AdExParameter3new(
  
);

int32_t _M0FP26RiantR8snn__mbt12step__tripod(
  struct _M0TP26RiantR8snn__mbt6Tripod*,
  float
);

int32_t _M0FP26RiantR8snn__mbt18tripod__heun__step(
  struct _M0TP26RiantR8snn__mbt6Tripod*,
  float,
  int32_t
);

int32_t _M0FP26RiantR8snn__mbt24tripod__syn__curr__dends(
  struct _M0TP26RiantR8snn__mbt6Tripod*
);

int32_t _M0FP26RiantR8snn__mbt23tripod__syn__curr__soma(
  struct _M0TP26RiantR8snn__mbt6Tripod*
);

int32_t _M0FP26RiantR8snn__mbt28tripod__dend__step__synapses(
  struct _M0TP26RiantR8snn__mbt6Tripod*,
  float
);

int32_t _M0FP26RiantR8snn__mbt28tripod__soma__step__synapses(
  struct _M0TP26RiantR8snn__mbt6Tripod*,
  float
);

struct _M0TP26RiantR8snn__mbt6Tripod* _M0MP26RiantR8snn__mbt6Tripod3new(
  int32_t,
  struct _M0TP26RiantR8snn__mbt13AdExParameter*,
  struct _M0TP26RiantR8snn__mbt7Xoshiro*
);

struct _M0TP26RiantR8snn__mbt8Dendrite* _M0MP26RiantR8snn__mbt8Dendrite3new(
  int32_t
);

struct _M0TP26RiantR8snn__mbt13AdExPostSpike* _M0MP26RiantR8snn__mbt13AdExPostSpike3new(
  
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

float expf(float);

struct { int32_t rc; uint32_t meta; uint16_t const data[19]; 
} const moonbit_string_literal_37 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 18, 32, 32, 
    69, 32, 32, 115, 111, 109, 97, 32, 115, 112, 105, 107, 101, 115, 
    58, 32, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[1]; 
} const moonbit_string_literal_8 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 0, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[24]; 
} const moonbit_string_literal_26 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 23, 32, 40, 
    964, 109, 32, 61, 32, 55, 109, 115, 44, 32, 69, 108, 32, 61, 32, 
    45, 53, 53, 109, 86, 41, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[49]; 
} const moonbit_string_literal_34 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 48, 32, 32, 
    32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 
    32, 32, 32, 32, 67, 111, 109, 112, 97, 114, 116, 109, 101, 110, 116, 
    83, 121, 110, 97, 112, 115, 101, 84, 114, 105, 112, 111, 100, 41, 
    0
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
} const moonbit_string_literal_7 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 1, 45, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[24]; 
} const moonbit_string_literal_43 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 23, 32, 32, 
    73, 50, 32, 109, 101, 97, 110, 32, 102, 105, 114, 105, 110, 103, 
    32, 114, 97, 116, 101, 58, 32, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[19]; 
} const moonbit_string_literal_38 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 18, 32, 32, 
    73, 49, 32, 115, 112, 105, 107, 101, 115, 58, 32, 32, 32, 32, 32, 
    32, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[22]; 
} const moonbit_string_literal_35 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 21, 61, 61, 
    61, 32, 83, 105, 109, 117, 108, 97, 116, 105, 111, 110, 32, 100, 
    111, 110, 101, 32, 40, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[76]; 
} const moonbit_string_literal_45 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 75, 32, 32, 
    116, 111, 32, 107, 101, 101, 112, 32, 114, 101, 99, 117, 114, 114, 
    101, 110, 116, 32, 119, 101, 105, 103, 104, 116, 115, 32, 105, 110, 
    32, 99, 104, 101, 99, 107, 46, 32, 84, 104, 97, 116, 32, 105, 110, 
    102, 114, 97, 115, 116, 114, 117, 99, 116, 117, 114, 101, 32, 105, 
    115, 110, 39, 116, 32, 112, 111, 114, 116, 101, 100, 32, 121, 101, 
    116, 44, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[60]; 
} const moonbit_string_literal_32 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 59, 32, 32, 
    69, 32, 32, 45, 62, 32, 73, 49, 44, 32, 69, 32, 45, 62, 32, 73, 50, 
    58, 32, 97, 112, 112, 114, 111, 120, 105, 109, 97, 116, 101, 100, 
    32, 118, 105, 97, 32, 116, 111, 110, 105, 99, 32, 99, 117, 114, 114, 
    101, 110, 116, 32, 40, 50, 53, 48, 112, 65, 41, 0
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

struct { int32_t rc; uint32_t meta; uint16_t const data[24]; 
} const moonbit_string_literal_42 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 23, 32, 32, 
    73, 49, 32, 109, 101, 97, 110, 32, 102, 105, 114, 105, 110, 103, 
    32, 114, 97, 116, 101, 58, 32, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[14]; 
} const moonbit_string_literal_25 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 13, 32, 32, 
    73, 49, 32, 32, 61, 32, 73, 70, 32, 120, 32, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[2]; 
} const moonbit_string_literal_4 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 1, 48, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[9]; 
} const moonbit_string_literal_19 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 8, 44, 32, 
    108, 101, 110, 32, 61, 32, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[37]; 
} const moonbit_string_literal_16 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 36, 98, 111, 
    117, 110, 100, 115, 32, 99, 104, 101, 99, 107, 32, 102, 97, 105, 
    108, 101, 100, 58, 32, 97, 108, 108, 111, 99, 97, 116, 101, 95, 108, 
    101, 110, 32, 61, 32, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[3]; 
} const moonbit_string_literal_0 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 2, 103, 101, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[25]; 
} const moonbit_string_literal_24 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 24, 32, 40, 
    104, 111, 109, 111, 103, 101, 110, 101, 111, 117, 115, 32, 65, 100, 
    69, 120, 32, 115, 111, 109, 97, 41, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[57]; 
} const moonbit_string_literal_46 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 56, 32, 32, 
    115, 111, 32, 116, 104, 105, 115, 32, 100, 101, 109, 111, 32, 115, 
    107, 105, 112, 115, 32, 69, 8594, 69, 32, 114, 101, 99, 117, 114, 
    114, 101, 110, 116, 32, 99, 111, 110, 110, 101, 99, 116, 105, 111, 
    110, 115, 32, 101, 110, 116, 105, 114, 101, 108, 121, 46, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[24]; 
} const moonbit_string_literal_40 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 23, 32, 32, 
    69, 32, 32, 109, 101, 97, 110, 32, 102, 105, 114, 105, 110, 103, 
    32, 114, 97, 116, 101, 58, 32, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[50]; 
} const moonbit_string_literal_31 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 49, 32, 32, 
    73, 50, 32, 45, 62, 32, 69, 32, 40, 58, 100, 49, 41, 32, 32, 32, 
    32, 956, 61, 53, 46, 48, 44, 32, 112, 61, 48, 46, 50, 32, 32, 91, 
    105, 83, 84, 68, 80, 80, 111, 116, 101, 110, 116, 105, 97, 108, 93, 
    0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[20]; 
} const moonbit_string_literal_22 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 19, 61, 61, 
    61, 32, 80, 111, 112, 117, 108, 97, 116, 105, 111, 110, 115, 32, 
    61, 61, 61, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[20]; 
} const moonbit_string_literal_29 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 19, 61, 61, 
    61, 32, 67, 111, 110, 110, 101, 99, 116, 105, 111, 110, 115, 32, 
    61, 61, 61, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[14]; 
} const moonbit_string_literal_27 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 13, 32, 32, 
    73, 50, 32, 32, 61, 32, 73, 70, 32, 120, 32, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[71]; 
} const moonbit_string_literal_44 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 70, 78, 111, 
    116, 101, 58, 32, 116, 104, 101, 32, 74, 117, 108, 105, 97, 32, 118, 
    101, 114, 115, 105, 111, 110, 32, 117, 115, 101, 115, 32, 83, 121, 
    110, 97, 112, 115, 101, 78, 111, 114, 109, 97, 108, 105, 122, 97, 
    116, 105, 111, 110, 32, 40, 77, 117, 108, 116, 105, 112, 108, 105, 
    99, 97, 116, 105, 118, 101, 78, 111, 114, 109, 41, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[45]; 
} const moonbit_string_literal_30 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 44, 32, 32, 
    73, 49, 32, 45, 62, 32, 69, 32, 40, 58, 115, 111, 109, 97, 41, 32, 
    32, 956, 61, 53, 46, 48, 44, 32, 112, 61, 48, 46, 50, 32, 32, 91, 
    105, 83, 84, 68, 80, 82, 97, 116, 101, 93, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[25]; 
} const moonbit_string_literal_28 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 24, 32, 40, 
    964, 109, 32, 61, 32, 50, 48, 109, 115, 44, 32, 69, 108, 32, 61, 
    32, 45, 53, 53, 109, 86, 41, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[18]; 
} const moonbit_string_literal_23 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 17, 32, 32, 
    69, 32, 32, 32, 61, 32, 84, 114, 105, 112, 111, 100, 32, 120, 32, 
    0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[37]; 
} const moonbit_string_literal_13 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 36, 48, 49, 
    50, 51, 52, 53, 54, 55, 56, 57, 97, 98, 99, 100, 101, 102, 103, 104, 
    105, 106, 107, 108, 109, 110, 111, 112, 113, 114, 115, 116, 117, 
    118, 119, 120, 121, 122, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[3]; 
} const moonbit_string_literal_1 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 2, 100, 50, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[16]; 
} const moonbit_string_literal_20 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 15, 44, 32, 
    115, 114, 99, 46, 108, 101, 110, 103, 116, 104, 32, 61, 32, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[3]; 
} const moonbit_string_literal_2 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 2, 100, 49, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[16]; 
} const moonbit_string_literal_17 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 15, 44, 32, 
    115, 114, 99, 95, 111, 102, 102, 115, 101, 116, 32, 61, 32, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[53]; 
} const moonbit_string_literal_33 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 52, 32, 32, 
    69, 32, 32, 45, 62, 32, 69, 32, 114, 101, 99, 117, 114, 114, 101, 
    110, 116, 58, 32, 115, 107, 105, 112, 112, 101, 100, 32, 40, 84, 
    114, 105, 112, 111, 100, 32, 99, 97, 110, 39, 116, 32, 98, 101, 32, 
    112, 114, 101, 32, 105, 110, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[5]; 
} const moonbit_string_literal_3 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 4, 115, 111, 
    109, 97, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[24]; 
} const moonbit_string_literal_11 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 23, 65, 114, 
    114, 97, 121, 32, 99, 97, 112, 97, 99, 105, 116, 121, 32, 111, 118, 
    101, 114, 102, 108, 111, 119, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[19]; 
} const moonbit_string_literal_39 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 18, 32, 32, 
    73, 50, 32, 115, 112, 105, 107, 101, 115, 58, 32, 32, 32, 32, 32, 
    32, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[21]; 
} const moonbit_string_literal_36 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 20, 32, 109, 
    115, 44, 32, 100, 116, 61, 48, 46, 49, 50, 53, 109, 115, 41, 32, 
    61, 61, 61, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[26]; 
} const moonbit_string_literal_5 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 25, 73, 108, 
    108, 101, 103, 97, 108, 65, 114, 103, 117, 109, 101, 110, 116, 69, 
    120, 99, 101, 112, 116, 105, 111, 110, 32, 0
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

struct { int32_t rc; uint32_t meta; uint16_t const data[11]; 
} const moonbit_string_literal_41 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 10, 32, 72, 
    122, 47, 110, 101, 117, 114, 111, 110, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[57]; 
} const moonbit_string_literal_21 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 56, 116, 114, 
    105, 112, 111, 100, 95, 110, 101, 116, 119, 111, 114, 107, 46, 109, 
    98, 116, 58, 32, 69, 45, 73, 32, 84, 114, 105, 112, 111, 100, 32, 
    110, 101, 116, 119, 111, 114, 107, 32, 40, 109, 97, 110, 117, 97, 
    108, 32, 115, 116, 101, 112, 112, 105, 110, 103, 41, 0
  };

uint32_t const moonbit_layout_table_data[110] =
  {
    sizeof(struct _M0TPB5ArrayGfE) / 4, 1,
    offsetof(struct _M0TPB5ArrayGfE, $0) / 4 * 2,
    sizeof(struct _M0TPB5ArrayGiE) / 4, 1,
    offsetof(struct _M0TPB5ArrayGiE, $0) / 4 * 2,
    sizeof(struct _M0TP26RiantR8snn__mbt24CompartmentSynapseTripod) / 4, 
    10,
    offsetof(struct _M0TP26RiantR8snn__mbt24CompartmentSynapseTripod, $0)
    / 4
    * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt24CompartmentSynapseTripod, $1)
    / 4
    * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt24CompartmentSynapseTripod, $2)
    / 4
    * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt24CompartmentSynapseTripod, $3)
    / 4
    * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt24CompartmentSynapseTripod, $4)
    / 4
    * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt24CompartmentSynapseTripod, $5)
    / 4
    * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt24CompartmentSynapseTripod, $6)
    / 4
    * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt24CompartmentSynapseTripod, $7)
    / 4
    * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt24CompartmentSynapseTripod, $8)
    / 4
    * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt24CompartmentSynapseTripod, $9)
    / 4
    * 2, sizeof(struct _M0TP26RiantR8snn__mbt19IstdpPotentialEntry) / 4, 
    3,
    offsetof(struct _M0TP26RiantR8snn__mbt19IstdpPotentialEntry, $3) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt19IstdpPotentialEntry, $4) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt19IstdpPotentialEntry, $5) / 4 * 2,
    sizeof(struct _M0TP26RiantR8snn__mbt23IstdpPotentialVariables) / 4, 
    2,
    offsetof(struct _M0TP26RiantR8snn__mbt23IstdpPotentialVariables, $0)
    / 4
    * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt23IstdpPotentialVariables, $1)
    / 4
    * 2, sizeof(struct _M0TP26RiantR8snn__mbt14IstdpRateEntry) / 4, 3,
    offsetof(struct _M0TP26RiantR8snn__mbt14IstdpRateEntry, $3) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt14IstdpRateEntry, $4) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt14IstdpRateEntry, $5) / 4 * 2,
    sizeof(struct _M0TP26RiantR8snn__mbt18IstdpRateVariables) / 4, 2,
    offsetof(struct _M0TP26RiantR8snn__mbt18IstdpRateVariables, $0) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt18IstdpRateVariables, $1) / 4 * 2,
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
    sizeof(struct _M0TP26RiantR8snn__mbt6Tripod) / 4, 31,
    offsetof(struct _M0TP26RiantR8snn__mbt6Tripod, $0) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt6Tripod, $1) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt6Tripod, $2) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt6Tripod, $3) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt6Tripod, $4) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt6Tripod, $5) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt6Tripod, $6) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt6Tripod, $7) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt6Tripod, $8) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt6Tripod, $9) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt6Tripod, $10) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt6Tripod, $11) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt6Tripod, $12) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt6Tripod, $13) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt6Tripod, $14) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt6Tripod, $15) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt6Tripod, $16) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt6Tripod, $17) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt6Tripod, $18) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt6Tripod, $26) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt6Tripod, $27) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt6Tripod, $28) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt6Tripod, $29) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt6Tripod, $30) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt6Tripod, $31) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt6Tripod, $32) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt6Tripod, $33) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt6Tripod, $34) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt6Tripod, $35) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt6Tripod, $36) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt6Tripod, $37) / 4 * 2,
    sizeof(struct _M0TP26RiantR8snn__mbt8Dendrite) / 4, 7,
    offsetof(struct _M0TP26RiantR8snn__mbt8Dendrite, $1) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt8Dendrite, $2) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt8Dendrite, $3) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt8Dendrite, $4) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt8Dendrite, $5) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt8Dendrite, $6) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt8Dendrite, $7) / 4 * 2,
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

float _M0FP26RiantR8snn__mbt2hz = 0x1.0624dd2f1a9fcp-10f;

int32_t _M0FP26RiantR8snn__mbt28forward__compartment__tripod(
  struct _M0TP26RiantR8snn__mbt24CompartmentSynapseTripod* _M0L1cS1104,
  float _M0L6t__nowS1114
) {
  struct _M0TPB5ArrayGfE* _M0L6delaysS3010;
  int32_t _M0L6_2atmpS3009;
  int32_t _M0L10use__delayS1103;
  struct _M0TPB5ArrayGfE* _M0L3rhoS3008;
  int32_t _M0L6_2atmpS3007;
  int32_t _M0L8use__rhoS1105;
  #line 225 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_compartment.mbt"
  _M0L6delaysS3010 = _M0L1cS1104->$6;
  #line 229 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_compartment.mbt"
  _M0L6_2atmpS3009 = _M0MPC15array5Array6lengthGfE(_M0L6delaysS3010);
  _M0L10use__delayS1103 = _M0L6_2atmpS3009 > 0;
  _M0L3rhoS3008 = _M0L1cS1104->$5;
  #line 230 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_compartment.mbt"
  _M0L6_2atmpS3007 = _M0MPC15array5Array6lengthGfE(_M0L3rhoS3008);
  _M0L8use__rhoS1105 = _M0L6_2atmpS3007 > 0;
  if (_M0L10use__delayS1103) {
    struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS2964 = _M0L1cS1104->$0;
    struct _M0TPB5ArrayGbE* _M0L4fireS2963 = _M0L3preS2964->$5;
    int32_t _M0L6n__preS1106;
    struct _M0TPB8MutLocalGiE* _M0L1jS1107;
    #line 232 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_compartment.mbt"
    _M0L6n__preS1106 = _M0MPC15array5Array6lengthGbE(_M0L4fireS2963);
    _M0L1jS1107
    = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
    Moonbit_object_header(_M0L1jS1107)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
    _M0L1jS1107->$0 = 0;
    while (1) {
      int32_t _M0L3valS2928 = _M0L1jS1107->$0;
      if (_M0L3valS2928 < _M0L6n__preS1106) {
        struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS2931 = _M0L1cS1104->$0;
        struct _M0TPB5ArrayGbE* _M0L4fireS2929 = _M0L3preS2931->$5;
        int32_t _M0L3valS2930 = _M0L1jS1107->$0;
        int32_t _M0L3valS2962;
        int32_t _M0L6_2atmpS2961;
        #line 235 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_compartment.mbt"
        if (_M0MPC15array5Array2atGbE(_M0L4fireS2929, _M0L3valS2930)) {
          struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS2960 =
            _M0L1cS1104->$4;
          struct _M0TPB5ArrayGiE* _M0L6rowptrS2958 = _M0L6matrixS2960->$2;
          int32_t _M0L3valS2959 = _M0L1jS1107->$0;
          int32_t _M0L5startS1108;
          struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS2957;
          struct _M0TPB5ArrayGiE* _M0L6rowptrS2954;
          int32_t _M0L3valS2956;
          int32_t _M0L6_2atmpS2955;
          int32_t _M0L3endS1109;
          struct _M0TPB8MutLocalGiE* _M0L1sS1110;
          #line 236 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_compartment.mbt"
          _M0L5startS1108
          = _M0MPC15array5Array2atGiE(_M0L6rowptrS2958, _M0L3valS2959);
          _M0L6matrixS2957 = _M0L1cS1104->$4;
          _M0L6rowptrS2954 = _M0L6matrixS2957->$2;
          _M0L3valS2956 = _M0L1jS1107->$0;
          _M0L6_2atmpS2955 = _M0L3valS2956 + 1;
          #line 237 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_compartment.mbt"
          _M0L3endS1109
          = _M0MPC15array5Array2atGiE(_M0L6rowptrS2954, _M0L6_2atmpS2955);
          _M0L1sS1110
          = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
          Moonbit_object_header(_M0L1sS1110)->meta
          = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
          _M0L1sS1110->$0 = _M0L5startS1108;
          while (1) {
            int32_t _M0L3valS2932 = _M0L1sS1110->$0;
            if (_M0L3valS2932 < _M0L3endS1109) {
              struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS2953 =
                _M0L1cS1104->$4;
              struct _M0TPB5ArrayGiE* _M0L6colptrS2951 = _M0L6matrixS2953->$3;
              int32_t _M0L3valS2952 = _M0L1sS1110->$0;
              int32_t _M0L9post__idxS1111;
              float _M0L1wS1112;
              struct _M0TPB5ArrayGfE* _M0L6delaysS2939;
              int32_t _M0L3valS2940;
              float _M0L1dS1113;
              int32_t _M0L3valS2938;
              int32_t _M0L6_2atmpS2937;
              #line 240 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_compartment.mbt"
              _M0L9post__idxS1111
              = _M0MPC15array5Array2atGiE(_M0L6colptrS2951, _M0L3valS2952);
              if (_M0L8use__rhoS1105) {
                struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS2947 =
                  _M0L1cS1104->$4;
                struct _M0TPB5ArrayGfE* _M0L4valsS2945 = _M0L6matrixS2947->$4;
                int32_t _M0L3valS2946 = _M0L1sS1110->$0;
                float _M0L6_2atmpS2941;
                struct _M0TPB5ArrayGfE* _M0L3rhoS2943;
                int32_t _M0L3valS2944;
                float _M0L6_2atmpS2942;
                #line 242 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_compartment.mbt"
                _M0L6_2atmpS2941
                = _M0MPC15array5Array2atGfE(_M0L4valsS2945, _M0L3valS2946);
                _M0L3rhoS2943 = _M0L1cS1104->$5;
                _M0L3valS2944 = _M0L1sS1110->$0;
                #line 242 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_compartment.mbt"
                _M0L6_2atmpS2942
                = _M0MPC15array5Array2atGfE(_M0L3rhoS2943, _M0L3valS2944);
                _M0L1wS1112 = _M0L6_2atmpS2941 * _M0L6_2atmpS2942;
              } else {
                struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS2950 =
                  _M0L1cS1104->$4;
                struct _M0TPB5ArrayGfE* _M0L4valsS2948 = _M0L6matrixS2950->$4;
                int32_t _M0L3valS2949 = _M0L1sS1110->$0;
                #line 244 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_compartment.mbt"
                _M0L1wS1112
                = _M0MPC15array5Array2atGfE(_M0L4valsS2948, _M0L3valS2949);
              }
              _M0L6delaysS2939 = _M0L1cS1104->$6;
              _M0L3valS2940 = _M0L1sS1110->$0;
              #line 246 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_compartment.mbt"
              _M0L1dS1113
              = _M0MPC15array5Array2atGfE(_M0L6delaysS2939, _M0L3valS2940);
              if (_M0L1dS1113 == 0x0p+0f) {
                #line 248 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_compartment.mbt"
                _M0FP26RiantR8snn__mbt34apply__compartment__weight__tripod(_M0L1cS1104, _M0L9post__idxS1111, _M0L1wS1112);
              } else {
                struct _M0TPB5ArrayGfE* _M0L14pending__timesS2933 =
                  _M0L1cS1104->$7;
                float _M0L6_2atmpS2934 = _M0L6t__nowS1114 + _M0L1dS1113;
                struct _M0TPB5ArrayGiE* _M0L14pending__postsS2935;
                struct _M0TPB5ArrayGfE* _M0L16pending__weightsS2936;
                #line 250 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_compartment.mbt"
                _M0MPC15array5Array4pushGfE(_M0L14pending__timesS2933, _M0L6_2atmpS2934);
                _M0L14pending__postsS2935 = _M0L1cS1104->$8;
                #line 251 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_compartment.mbt"
                _M0MPC15array5Array4pushGiE(_M0L14pending__postsS2935, _M0L9post__idxS1111);
                _M0L16pending__weightsS2936 = _M0L1cS1104->$9;
                #line 252 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_compartment.mbt"
                _M0MPC15array5Array4pushGfE(_M0L16pending__weightsS2936, _M0L1wS1112);
              }
              _M0L3valS2938 = _M0L1sS1110->$0;
              _M0L6_2atmpS2937 = _M0L3valS2938 + 1;
              _M0L1sS1110->$0 = _M0L6_2atmpS2937;
              continue;
            } else {
              moonbit_decref(_M0L1sS1110);
            }
            break;
          }
        }
        _M0L3valS2962 = _M0L1jS1107->$0;
        _M0L6_2atmpS2961 = _M0L3valS2962 + 1;
        _M0L1jS1107->$0 = _M0L6_2atmpS2961;
        continue;
      } else {
        moonbit_decref(_M0L1jS1107);
      }
      break;
    }
  } else {
    moonbit_string_t _M0L3symS2998 = _M0L1cS1104->$2;
    struct _M0TPB5ArrayGfE* _M0L3bufS1117;
    #line 260 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_compartment.mbt"
    if (
      _M0L3symS2998 == (moonbit_string_t)moonbit_string_literal_0.data
      || Moonbit_array_length(_M0L3symS2998)
         == Moonbit_array_length((moonbit_string_t)moonbit_string_literal_0.data)
         && 0
            == memcmp(_M0L3symS2998, (moonbit_string_t)moonbit_string_literal_0.data, Moonbit_array_length(_M0L3symS2998) * 2)
    ) {
      moonbit_string_t _M0L7_2abindS1118 = _M0L1cS1104->$3;
      if (
        _M0L7_2abindS1118 == (moonbit_string_t)moonbit_string_literal_3.data
        || Moonbit_array_length(_M0L7_2abindS1118) == 4
           && 0
              == memcmp(_M0L7_2abindS1118, (moonbit_string_t)moonbit_string_literal_3.data, 8)
      ) {
        struct _M0TP26RiantR8snn__mbt6Tripod* _M0L4postS2999 =
          _M0L1cS1104->$1;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS3011 = _M0L4postS2999->$13;
        moonbit_incref(_M0L8_2afieldS3011);
        _M0L3bufS1117 = _M0L8_2afieldS3011;
      } else if (
               _M0L7_2abindS1118
               == (moonbit_string_t)moonbit_string_literal_2.data
               || Moonbit_array_length(_M0L7_2abindS1118) == 2
                  && 0
                     == memcmp(_M0L7_2abindS1118, (moonbit_string_t)moonbit_string_literal_2.data, 4)
             ) {
        struct _M0TP26RiantR8snn__mbt6Tripod* _M0L4postS3000 =
          _M0L1cS1104->$1;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS3012 = _M0L4postS3000->$15;
        moonbit_incref(_M0L8_2afieldS3012);
        _M0L3bufS1117 = _M0L8_2afieldS3012;
      } else if (
               _M0L7_2abindS1118
               == (moonbit_string_t)moonbit_string_literal_1.data
               || Moonbit_array_length(_M0L7_2abindS1118) == 2
                  && 0
                     == memcmp(_M0L7_2abindS1118, (moonbit_string_t)moonbit_string_literal_1.data, 4)
             ) {
        struct _M0TP26RiantR8snn__mbt6Tripod* _M0L4postS3001 =
          _M0L1cS1104->$1;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS3013 = _M0L4postS3001->$17;
        moonbit_incref(_M0L8_2afieldS3013);
        _M0L3bufS1117 = _M0L8_2afieldS3013;
      } else {
        struct _M0TP26RiantR8snn__mbt6Tripod* _M0L4postS3002 =
          _M0L1cS1104->$1;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS3014 = _M0L4postS3002->$13;
        moonbit_incref(_M0L8_2afieldS3014);
        _M0L3bufS1117 = _M0L8_2afieldS3014;
      }
    } else {
      moonbit_string_t _M0L7_2abindS1119 = _M0L1cS1104->$3;
      if (
        _M0L7_2abindS1119 == (moonbit_string_t)moonbit_string_literal_3.data
        || Moonbit_array_length(_M0L7_2abindS1119) == 4
           && 0
              == memcmp(_M0L7_2abindS1119, (moonbit_string_t)moonbit_string_literal_3.data, 8)
      ) {
        struct _M0TP26RiantR8snn__mbt6Tripod* _M0L4postS3003 =
          _M0L1cS1104->$1;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS3015 = _M0L4postS3003->$14;
        moonbit_incref(_M0L8_2afieldS3015);
        _M0L3bufS1117 = _M0L8_2afieldS3015;
      } else if (
               _M0L7_2abindS1119
               == (moonbit_string_t)moonbit_string_literal_2.data
               || Moonbit_array_length(_M0L7_2abindS1119) == 2
                  && 0
                     == memcmp(_M0L7_2abindS1119, (moonbit_string_t)moonbit_string_literal_2.data, 4)
             ) {
        struct _M0TP26RiantR8snn__mbt6Tripod* _M0L4postS3004 =
          _M0L1cS1104->$1;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS3016 = _M0L4postS3004->$16;
        moonbit_incref(_M0L8_2afieldS3016);
        _M0L3bufS1117 = _M0L8_2afieldS3016;
      } else if (
               _M0L7_2abindS1119
               == (moonbit_string_t)moonbit_string_literal_1.data
               || Moonbit_array_length(_M0L7_2abindS1119) == 2
                  && 0
                     == memcmp(_M0L7_2abindS1119, (moonbit_string_t)moonbit_string_literal_1.data, 4)
             ) {
        struct _M0TP26RiantR8snn__mbt6Tripod* _M0L4postS3005 =
          _M0L1cS1104->$1;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS3017 = _M0L4postS3005->$18;
        moonbit_incref(_M0L8_2afieldS3017);
        _M0L3bufS1117 = _M0L8_2afieldS3017;
      } else {
        struct _M0TP26RiantR8snn__mbt6Tripod* _M0L4postS3006 =
          _M0L1cS1104->$1;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS3018 = _M0L4postS3006->$14;
        moonbit_incref(_M0L8_2afieldS3018);
        _M0L3bufS1117 = _M0L8_2afieldS3018;
      }
    }
    if (_M0L8use__rhoS1105) {
      struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS2994 = _M0L1cS1104->$0;
      struct _M0TPB5ArrayGbE* _M0L4fireS2993 = _M0L3preS2994->$5;
      int32_t _M0L6n__preS1120;
      struct _M0TPB8MutLocalGiE* _M0L1jS1121;
      #line 276 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_compartment.mbt"
      _M0L6n__preS1120 = _M0MPC15array5Array6lengthGbE(_M0L4fireS2993);
      _M0L1jS1121
      = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
      Moonbit_object_header(_M0L1jS1121)->meta
      = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
      _M0L1jS1121->$0 = 0;
      while (1) {
        int32_t _M0L3valS2965 = _M0L1jS1121->$0;
        if (_M0L3valS2965 < _M0L6n__preS1120) {
          struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS2968 = _M0L1cS1104->$0;
          struct _M0TPB5ArrayGbE* _M0L4fireS2966 = _M0L3preS2968->$5;
          int32_t _M0L3valS2967 = _M0L1jS1121->$0;
          int32_t _M0L3valS2992;
          int32_t _M0L6_2atmpS2991;
          #line 279 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_compartment.mbt"
          if (_M0MPC15array5Array2atGbE(_M0L4fireS2966, _M0L3valS2967)) {
            struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS2990 =
              _M0L1cS1104->$4;
            struct _M0TPB5ArrayGiE* _M0L6rowptrS2988 = _M0L6matrixS2990->$2;
            int32_t _M0L3valS2989 = _M0L1jS1121->$0;
            int32_t _M0L5startS1122;
            struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS2987;
            struct _M0TPB5ArrayGiE* _M0L6rowptrS2984;
            int32_t _M0L3valS2986;
            int32_t _M0L6_2atmpS2985;
            int32_t _M0L3endS1123;
            struct _M0TPB8MutLocalGiE* _M0L1sS1124;
            #line 280 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_compartment.mbt"
            _M0L5startS1122
            = _M0MPC15array5Array2atGiE(_M0L6rowptrS2988, _M0L3valS2989);
            _M0L6matrixS2987 = _M0L1cS1104->$4;
            _M0L6rowptrS2984 = _M0L6matrixS2987->$2;
            _M0L3valS2986 = _M0L1jS1121->$0;
            _M0L6_2atmpS2985 = _M0L3valS2986 + 1;
            #line 281 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_compartment.mbt"
            _M0L3endS1123
            = _M0MPC15array5Array2atGiE(_M0L6rowptrS2984, _M0L6_2atmpS2985);
            _M0L1sS1124
            = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
            Moonbit_object_header(_M0L1sS1124)->meta
            = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
            _M0L1sS1124->$0 = _M0L5startS1122;
            while (1) {
              int32_t _M0L3valS2969 = _M0L1sS1124->$0;
              if (_M0L3valS2969 < _M0L3endS1123) {
                struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS2983 =
                  _M0L1cS1104->$4;
                struct _M0TPB5ArrayGiE* _M0L6colptrS2981 =
                  _M0L6matrixS2983->$3;
                int32_t _M0L3valS2982 = _M0L1sS1124->$0;
                int32_t _M0L9post__idxS1125;
                struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS2980;
                struct _M0TPB5ArrayGfE* _M0L4valsS2978;
                int32_t _M0L3valS2979;
                float _M0L6_2atmpS2974;
                struct _M0TPB5ArrayGfE* _M0L3rhoS2976;
                int32_t _M0L3valS2977;
                float _M0L6_2atmpS2975;
                float _M0L9w__scaledS1126;
                float _M0L6_2atmpS2971;
                float _M0L6_2atmpS2970;
                int32_t _M0L3valS2973;
                int32_t _M0L6_2atmpS2972;
                #line 284 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_compartment.mbt"
                _M0L9post__idxS1125
                = _M0MPC15array5Array2atGiE(_M0L6colptrS2981, _M0L3valS2982);
                _M0L6matrixS2980 = _M0L1cS1104->$4;
                _M0L4valsS2978 = _M0L6matrixS2980->$4;
                _M0L3valS2979 = _M0L1sS1124->$0;
                #line 285 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_compartment.mbt"
                _M0L6_2atmpS2974
                = _M0MPC15array5Array2atGfE(_M0L4valsS2978, _M0L3valS2979);
                _M0L3rhoS2976 = _M0L1cS1104->$5;
                _M0L3valS2977 = _M0L1sS1124->$0;
                #line 285 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_compartment.mbt"
                _M0L6_2atmpS2975
                = _M0MPC15array5Array2atGfE(_M0L3rhoS2976, _M0L3valS2977);
                _M0L9w__scaledS1126 = _M0L6_2atmpS2974 * _M0L6_2atmpS2975;
                #line 286 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_compartment.mbt"
                _M0L6_2atmpS2971
                = _M0MPC15array5Array2atGfE(_M0L3bufS1117, _M0L9post__idxS1125);
                _M0L6_2atmpS2970 = _M0L6_2atmpS2971 + _M0L9w__scaledS1126;
                #line 286 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_compartment.mbt"
                _M0MPC15array5Array3setGfE(_M0L3bufS1117, _M0L9post__idxS1125, _M0L6_2atmpS2970);
                _M0L3valS2973 = _M0L1sS1124->$0;
                _M0L6_2atmpS2972 = _M0L3valS2973 + 1;
                _M0L1sS1124->$0 = _M0L6_2atmpS2972;
                continue;
              } else {
                moonbit_decref(_M0L1sS1124);
              }
              break;
            }
          }
          _M0L3valS2992 = _M0L1jS1121->$0;
          _M0L6_2atmpS2991 = _M0L3valS2992 + 1;
          _M0L1jS1121->$0 = _M0L6_2atmpS2991;
          continue;
        } else {
          moonbit_decref(_M0L1jS1121);
          moonbit_decref(_M0L3bufS1117);
        }
        break;
      }
    } else {
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS2995 =
        _M0L1cS1104->$4;
      struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS2997 = _M0L1cS1104->$0;
      struct _M0TPB5ArrayGbE* _M0L4fireS2996 = _M0L3preS2997->$5;
      #line 293 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_compartment.mbt"
      _M0MP26RiantR8snn__mbt15SparseMatrixCSR7forward(_M0L6matrixS2995, _M0L4fireS2996, _M0L3bufS1117);
      moonbit_decref(_M0L3bufS1117);
    }
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt34apply__compartment__weight__tripod(
  struct _M0TP26RiantR8snn__mbt24CompartmentSynapseTripod* _M0L1cS1098,
  int32_t _M0L9post__idxS1101,
  float _M0L1wS1102
) {
  moonbit_string_t _M0L3symS2927;
  int32_t _M0L6is__geS1097;
  moonbit_string_t _M0L7_2abindS1100;
  struct _M0TPB5ArrayGfE* _M0L3bufS1099;
  float _M0L6_2atmpS2919;
  float _M0L6_2atmpS2918;
  #line 125 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_compartment.mbt"
  _M0L3symS2927 = _M0L1cS1098->$2;
  #line 130 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_compartment.mbt"
  _M0L6is__geS1097
  = _M0L3symS2927 == (moonbit_string_t)moonbit_string_literal_0.data
    || Moonbit_array_length(_M0L3symS2927)
       == Moonbit_array_length((moonbit_string_t)moonbit_string_literal_0.data)
       && 0
          == memcmp(_M0L3symS2927, (moonbit_string_t)moonbit_string_literal_0.data, Moonbit_array_length(_M0L3symS2927) * 2);
  _M0L7_2abindS1100 = _M0L1cS1098->$3;
  if (
    _M0L7_2abindS1100 == (moonbit_string_t)moonbit_string_literal_3.data
    || Moonbit_array_length(_M0L7_2abindS1100) == 4
       && 0
          == memcmp(_M0L7_2abindS1100, (moonbit_string_t)moonbit_string_literal_3.data, 8)
  ) {
    if (_M0L6is__geS1097 == 1) {
      struct _M0TP26RiantR8snn__mbt6Tripod* _M0L4postS2921 = _M0L1cS1098->$1;
      struct _M0TPB5ArrayGfE* _M0L8_2afieldS3019 = _M0L4postS2921->$13;
      moonbit_incref(_M0L8_2afieldS3019);
      _M0L3bufS1099 = _M0L8_2afieldS3019;
    } else {
      struct _M0TP26RiantR8snn__mbt6Tripod* _M0L4postS2920 = _M0L1cS1098->$1;
      struct _M0TPB5ArrayGfE* _M0L8_2afieldS3020 = _M0L4postS2920->$14;
      moonbit_incref(_M0L8_2afieldS3020);
      _M0L3bufS1099 = _M0L8_2afieldS3020;
    }
  } else if (
           _M0L7_2abindS1100
           == (moonbit_string_t)moonbit_string_literal_2.data
           || Moonbit_array_length(_M0L7_2abindS1100) == 2
              && 0
                 == memcmp(_M0L7_2abindS1100, (moonbit_string_t)moonbit_string_literal_2.data, 4)
         ) {
    if (_M0L6is__geS1097 == 1) {
      struct _M0TP26RiantR8snn__mbt6Tripod* _M0L4postS2923 = _M0L1cS1098->$1;
      struct _M0TPB5ArrayGfE* _M0L8_2afieldS3021 = _M0L4postS2923->$15;
      moonbit_incref(_M0L8_2afieldS3021);
      _M0L3bufS1099 = _M0L8_2afieldS3021;
    } else {
      struct _M0TP26RiantR8snn__mbt6Tripod* _M0L4postS2922 = _M0L1cS1098->$1;
      struct _M0TPB5ArrayGfE* _M0L8_2afieldS3022 = _M0L4postS2922->$16;
      moonbit_incref(_M0L8_2afieldS3022);
      _M0L3bufS1099 = _M0L8_2afieldS3022;
    }
  } else if (
           _M0L7_2abindS1100
           == (moonbit_string_t)moonbit_string_literal_1.data
           || Moonbit_array_length(_M0L7_2abindS1100) == 2
              && 0
                 == memcmp(_M0L7_2abindS1100, (moonbit_string_t)moonbit_string_literal_1.data, 4)
         ) {
    if (_M0L6is__geS1097 == 1) {
      struct _M0TP26RiantR8snn__mbt6Tripod* _M0L4postS2925 = _M0L1cS1098->$1;
      struct _M0TPB5ArrayGfE* _M0L8_2afieldS3023 = _M0L4postS2925->$17;
      moonbit_incref(_M0L8_2afieldS3023);
      _M0L3bufS1099 = _M0L8_2afieldS3023;
    } else {
      struct _M0TP26RiantR8snn__mbt6Tripod* _M0L4postS2924 = _M0L1cS1098->$1;
      struct _M0TPB5ArrayGfE* _M0L8_2afieldS3024 = _M0L4postS2924->$18;
      moonbit_incref(_M0L8_2afieldS3024);
      _M0L3bufS1099 = _M0L8_2afieldS3024;
    }
  } else {
    struct _M0TP26RiantR8snn__mbt6Tripod* _M0L4postS2926 = _M0L1cS1098->$1;
    struct _M0TPB5ArrayGfE* _M0L8_2afieldS3025 = _M0L4postS2926->$13;
    moonbit_incref(_M0L8_2afieldS3025);
    _M0L3bufS1099 = _M0L8_2afieldS3025;
  }
  #line 140 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_compartment.mbt"
  _M0L6_2atmpS2919
  = _M0MPC15array5Array2atGfE(_M0L3bufS1099, _M0L9post__idxS1101);
  _M0L6_2atmpS2918 = _M0L6_2atmpS2919 + _M0L1wS1102;
  #line 140 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_compartment.mbt"
  _M0MPC15array5Array3setGfE(_M0L3bufS1099, _M0L9post__idxS1101, _M0L6_2atmpS2918);
  moonbit_decref(_M0L3bufS1099);
  return 0;
}

struct _M0TP26RiantR8snn__mbt24CompartmentSynapseTripod* _M0MP26RiantR8snn__mbt24CompartmentSynapseTripod6random(
  struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS1089,
  struct _M0TP26RiantR8snn__mbt6Tripod* _M0L4postS1090,
  moonbit_string_t _M0L3symS1095,
  moonbit_string_t _M0L6targetS1096,
  float _M0L2muS1091,
  float _M0L5sigmaS1092,
  float _M0L1pS1093,
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS1094
) {
  int32_t _M0L1nS2916;
  int32_t _M0L1nS2917;
  struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L1mS1088;
  float* _M0L6_2atmpS2915;
  struct _M0TPB5ArrayGfE* _M0L6_2atmpS2906;
  float* _M0L6_2atmpS2914;
  struct _M0TPB5ArrayGfE* _M0L6_2atmpS2907;
  float* _M0L6_2atmpS2913;
  struct _M0TPB5ArrayGfE* _M0L6_2atmpS2908;
  int32_t* _M0L6_2atmpS2912;
  struct _M0TPB5ArrayGiE* _M0L6_2atmpS2909;
  float* _M0L6_2atmpS2911;
  struct _M0TPB5ArrayGfE* _M0L6_2atmpS2910;
  struct _M0TP26RiantR8snn__mbt24CompartmentSynapseTripod* _block_3041;
  #line 65 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_compartment.mbt"
  _M0L1nS2916 = _M0L3preS1089->$2;
  _M0L1nS2917 = _M0L4postS1090->$25;
  #line 75 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_compartment.mbt"
  _M0L1mS1088
  = _M0MP26RiantR8snn__mbt15SparseMatrixCSR6random(_M0L1nS2916, _M0L1nS2917, _M0L2muS1091, _M0L5sigmaS1092, _M0L1pS1093, _M0L3rngS1094);
  _M0L6_2atmpS2915 = moonbit_empty_float_array;
  _M0L6_2atmpS2906
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_M0L6_2atmpS2906)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 0, 0);
  _M0L6_2atmpS2906->$0 = _M0L6_2atmpS2915;
  _M0L6_2atmpS2906->$1 = 0;
  _M0L6_2atmpS2914 = moonbit_empty_float_array;
  _M0L6_2atmpS2907
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_M0L6_2atmpS2907)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 0, 0);
  _M0L6_2atmpS2907->$0 = _M0L6_2atmpS2914;
  _M0L6_2atmpS2907->$1 = 0;
  _M0L6_2atmpS2913 = moonbit_empty_float_array;
  _M0L6_2atmpS2908
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_M0L6_2atmpS2908)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 0, 0);
  _M0L6_2atmpS2908->$0 = _M0L6_2atmpS2913;
  _M0L6_2atmpS2908->$1 = 0;
  _M0L6_2atmpS2912 = (int32_t*)moonbit_empty_int32_array;
  _M0L6_2atmpS2909
  = (struct _M0TPB5ArrayGiE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGiE));
  Moonbit_object_header(_M0L6_2atmpS2909)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 3, 0);
  _M0L6_2atmpS2909->$0 = _M0L6_2atmpS2912;
  _M0L6_2atmpS2909->$1 = 0;
  _M0L6_2atmpS2911 = moonbit_empty_float_array;
  _M0L6_2atmpS2910
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_M0L6_2atmpS2910)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 0, 0);
  _M0L6_2atmpS2910->$0 = _M0L6_2atmpS2911;
  _M0L6_2atmpS2910->$1 = 0;
  moonbit_incref(_M0L3preS1089);
  moonbit_incref(_M0L4postS1090);
  moonbit_incref(_M0L3symS1095);
  moonbit_incref(_M0L6targetS1096);
  _block_3041
  = (struct _M0TP26RiantR8snn__mbt24CompartmentSynapseTripod*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt24CompartmentSynapseTripod));
  Moonbit_object_header(_block_3041)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 6, 0);
  _block_3041->$0 = _M0L3preS1089;
  _block_3041->$1 = _M0L4postS1090;
  _block_3041->$2 = _M0L3symS1095;
  _block_3041->$3 = _M0L6targetS1096;
  _block_3041->$4 = _M0L1mS1088;
  _block_3041->$5 = _M0L6_2atmpS2906;
  _block_3041->$6 = _M0L6_2atmpS2907;
  _block_3041->$7 = _M0L6_2atmpS2908;
  _block_3041->$8 = _M0L6_2atmpS2909;
  _block_3041->$9 = _M0L6_2atmpS2910;
  return _block_3041;
}

int32_t _M0FP26RiantR8snn__mbt22istdp__potential__step(
  struct _M0TPB5ArrayGfE* _M0L1wS1084,
  struct _M0TPB5ArrayGbE* _M0L9pre__fireS1061,
  struct _M0TPB5ArrayGbE* _M0L10post__fireS1063,
  struct _M0TPB5ArrayGiE* _M0L6colptrS1080,
  struct _M0TPB5ArrayGiE* _M0L6rowptrS1074,
  struct _M0TPB5ArrayGfE* _M0L7v__postS1071,
  struct _M0TP26RiantR8snn__mbt23IstdpPotentialVariables* _M0L4varsS1067,
  struct _M0TP26RiantR8snn__mbt14IstdpPotential* _M0L5paramS1065,
  float _M0L6t__nowS1059,
  float _M0L2dtS1068
) {
  int32_t _M0L6n__preS1060;
  int32_t _M0L7n__postS1062;
  float _M0L6tau__yS2905;
  float _M0L11inv__tau__yS1064;
  struct _M0TPB8MutLocalGiE* _M0L1jS1066;
  struct _M0TPB8MutLocalGiE* _M0L1iS1070;
  #line 316 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
  #line 329 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
  _M0L6n__preS1060 = _M0MPC15array5Array6lengthGbE(_M0L9pre__fireS1061);
  #line 330 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
  _M0L7n__postS1062 = _M0MPC15array5Array6lengthGbE(_M0L10post__fireS1063);
  _M0L6tau__yS2905 = _M0L5paramS1065->$2;
  _M0L11inv__tau__yS1064 = 0x1p+0f / _M0L6tau__yS2905;
  _M0L1jS1066
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1jS1066)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1jS1066->$0 = 0;
  while (1) {
    int32_t _M0L3valS2822 = _M0L1jS1066->$0;
    if (_M0L3valS2822 < _M0L6n__preS1060) {
      struct _M0TPB5ArrayGfE* _M0L4tpreS2823 = _M0L4varsS1067->$0;
      int32_t _M0L3valS2824 = _M0L1jS1066->$0;
      struct _M0TPB5ArrayGfE* _M0L4tpreS2833 = _M0L4varsS1067->$0;
      int32_t _M0L3valS2834 = _M0L1jS1066->$0;
      float _M0L6_2atmpS2826;
      struct _M0TPB5ArrayGfE* _M0L4tpreS2831;
      int32_t _M0L3valS2832;
      float _M0L6_2atmpS2830;
      float _M0L6_2atmpS2829;
      float _M0L6_2atmpS2828;
      float _M0L6_2atmpS2827;
      float _M0L6_2atmpS2825;
      int32_t _M0L3valS2835;
      int32_t _M0L3valS2843;
      int32_t _M0L6_2atmpS2842;
      #line 337 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      _M0L6_2atmpS2826
      = _M0MPC15array5Array2atGfE(_M0L4tpreS2833, _M0L3valS2834);
      _M0L4tpreS2831 = _M0L4varsS1067->$0;
      _M0L3valS2832 = _M0L1jS1066->$0;
      #line 337 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      _M0L6_2atmpS2830
      = _M0MPC15array5Array2atGfE(_M0L4tpreS2831, _M0L3valS2832);
      _M0L6_2atmpS2829 = -_M0L6_2atmpS2830;
      _M0L6_2atmpS2828 = _M0L2dtS1068 * _M0L6_2atmpS2829;
      _M0L6_2atmpS2827 = _M0L6_2atmpS2828 * _M0L11inv__tau__yS1064;
      _M0L6_2atmpS2825 = _M0L6_2atmpS2826 + _M0L6_2atmpS2827;
      #line 337 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      _M0MPC15array5Array3setGfE(_M0L4tpreS2823, _M0L3valS2824, _M0L6_2atmpS2825);
      _M0L3valS2835 = _M0L1jS1066->$0;
      #line 338 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L9pre__fireS1061, _M0L3valS2835)) {
        struct _M0TPB5ArrayGfE* _M0L4tpreS2836 = _M0L4varsS1067->$0;
        int32_t _M0L3valS2837 = _M0L1jS1066->$0;
        struct _M0TPB5ArrayGfE* _M0L4tpreS2840 = _M0L4varsS1067->$0;
        int32_t _M0L3valS2841 = _M0L1jS1066->$0;
        float _M0L6_2atmpS2839;
        float _M0L6_2atmpS2838;
        #line 339 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
        _M0L6_2atmpS2839
        = _M0MPC15array5Array2atGfE(_M0L4tpreS2840, _M0L3valS2841);
        _M0L6_2atmpS2838 = _M0L6_2atmpS2839 + 0x1p+0f;
        #line 339 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L4tpreS2836, _M0L3valS2837, _M0L6_2atmpS2838);
      }
      _M0L3valS2843 = _M0L1jS1066->$0;
      _M0L6_2atmpS2842 = _M0L3valS2843 + 1;
      _M0L1jS1066->$0 = _M0L6_2atmpS2842;
      continue;
    }
    break;
  }
  _M0L1iS1070
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1iS1070)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1iS1070->$0 = 0;
  while (1) {
    int32_t _M0L3valS2844 = _M0L1iS1070->$0;
    if (_M0L3valS2844 < _M0L7n__postS1062) {
      struct _M0TPB5ArrayGfE* _M0L5tpostS2845 = _M0L4varsS1067->$1;
      int32_t _M0L3valS2846 = _M0L1iS1070->$0;
      struct _M0TPB5ArrayGfE* _M0L5tpostS2858 = _M0L4varsS1067->$1;
      int32_t _M0L3valS2859 = _M0L1iS1070->$0;
      float _M0L6_2atmpS2848;
      struct _M0TPB5ArrayGfE* _M0L5tpostS2856;
      int32_t _M0L3valS2857;
      float _M0L6_2atmpS2853;
      int32_t _M0L3valS2855;
      float _M0L6_2atmpS2854;
      float _M0L6_2atmpS2852;
      float _M0L6_2atmpS2851;
      float _M0L6_2atmpS2850;
      float _M0L6_2atmpS2849;
      float _M0L6_2atmpS2847;
      int32_t _M0L3valS2860;
      int32_t _M0L3valS2868;
      int32_t _M0L6_2atmpS2867;
      #line 346 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      _M0L6_2atmpS2848
      = _M0MPC15array5Array2atGfE(_M0L5tpostS2858, _M0L3valS2859);
      _M0L5tpostS2856 = _M0L4varsS1067->$1;
      _M0L3valS2857 = _M0L1iS1070->$0;
      #line 346 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      _M0L6_2atmpS2853
      = _M0MPC15array5Array2atGfE(_M0L5tpostS2856, _M0L3valS2857);
      _M0L3valS2855 = _M0L1iS1070->$0;
      #line 346 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      _M0L6_2atmpS2854
      = _M0MPC15array5Array2atGfE(_M0L7v__postS1071, _M0L3valS2855);
      _M0L6_2atmpS2852 = _M0L6_2atmpS2853 - _M0L6_2atmpS2854;
      _M0L6_2atmpS2851 = -_M0L6_2atmpS2852;
      _M0L6_2atmpS2850 = _M0L2dtS1068 * _M0L6_2atmpS2851;
      _M0L6_2atmpS2849 = _M0L6_2atmpS2850 * _M0L11inv__tau__yS1064;
      _M0L6_2atmpS2847 = _M0L6_2atmpS2848 + _M0L6_2atmpS2849;
      #line 346 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      _M0MPC15array5Array3setGfE(_M0L5tpostS2845, _M0L3valS2846, _M0L6_2atmpS2847);
      _M0L3valS2860 = _M0L1iS1070->$0;
      #line 347 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L10post__fireS1063, _M0L3valS2860)) {
        struct _M0TPB5ArrayGfE* _M0L5tpostS2861 = _M0L4varsS1067->$1;
        int32_t _M0L3valS2862 = _M0L1iS1070->$0;
        struct _M0TPB5ArrayGfE* _M0L5tpostS2865 = _M0L4varsS1067->$1;
        int32_t _M0L3valS2866 = _M0L1iS1070->$0;
        float _M0L6_2atmpS2864;
        float _M0L6_2atmpS2863;
        #line 348 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
        _M0L6_2atmpS2864
        = _M0MPC15array5Array2atGfE(_M0L5tpostS2865, _M0L3valS2866);
        _M0L6_2atmpS2863 = _M0L6_2atmpS2864 + 0x1p+0f;
        #line 348 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L5tpostS2861, _M0L3valS2862, _M0L6_2atmpS2863);
      }
      _M0L3valS2868 = _M0L1iS1070->$0;
      _M0L6_2atmpS2867 = _M0L3valS2868 + 1;
      _M0L1iS1070->$0 = _M0L6_2atmpS2867;
      continue;
    } else {
      moonbit_decref(_M0L1iS1070);
    }
    break;
  }
  _M0L1jS1066->$0 = 0;
  while (1) {
    int32_t _M0L3valS2869 = _M0L1jS1066->$0;
    if (_M0L3valS2869 < _M0L6n__preS1060) {
      int32_t _M0L3valS2904 = _M0L1jS1066->$0;
      int32_t _M0L5startS1073;
      int32_t _M0L3valS2903;
      int32_t _M0L6_2atmpS2902;
      int32_t _M0L3endS1075;
      int32_t _M0L3valS2901;
      int32_t _M0L10pre__firedS1076;
      struct _M0TPB5ArrayGfE* _M0L4tpreS2899;
      int32_t _M0L3valS2900;
      float _M0L7tpre__jS1077;
      struct _M0TPB8MutLocalGiE* _M0L1sS1078;
      int32_t _M0L3valS2898;
      int32_t _M0L6_2atmpS2897;
      #line 358 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      _M0L5startS1073
      = _M0MPC15array5Array2atGiE(_M0L6rowptrS1074, _M0L3valS2904);
      _M0L3valS2903 = _M0L1jS1066->$0;
      _M0L6_2atmpS2902 = _M0L3valS2903 + 1;
      #line 359 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      _M0L3endS1075
      = _M0MPC15array5Array2atGiE(_M0L6rowptrS1074, _M0L6_2atmpS2902);
      _M0L3valS2901 = _M0L1jS1066->$0;
      #line 360 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      _M0L10pre__firedS1076
      = _M0MPC15array5Array2atGbE(_M0L9pre__fireS1061, _M0L3valS2901);
      _M0L4tpreS2899 = _M0L4varsS1067->$0;
      _M0L3valS2900 = _M0L1jS1066->$0;
      #line 361 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      _M0L7tpre__jS1077
      = _M0MPC15array5Array2atGfE(_M0L4tpreS2899, _M0L3valS2900);
      _M0L1sS1078
      = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
      Moonbit_object_header(_M0L1sS1078)->meta
      = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
      _M0L1sS1078->$0 = _M0L5startS1073;
      while (1) {
        int32_t _M0L3valS2870 = _M0L1sS1078->$0;
        if (_M0L3valS2870 < _M0L3endS1075) {
          int32_t _M0L3valS2896 = _M0L1sS1078->$0;
          int32_t _M0L9post__idxS1079;
          int32_t _M0L11post__firedS1081;
          struct _M0TPB5ArrayGfE* _M0L5tpostS2895;
          float _M0L8tpost__iS1082;
          int32_t _M0L3valS2885;
          float _M0L6_2atmpS2883;
          float _M0L6w__minS2884;
          int32_t _M0L3valS2890;
          float _M0L6_2atmpS2888;
          float _M0L6w__maxS2889;
          int32_t _M0L3valS2894;
          int32_t _M0L6_2atmpS2893;
          #line 364 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
          _M0L9post__idxS1079
          = _M0MPC15array5Array2atGiE(_M0L6colptrS1080, _M0L3valS2896);
          #line 365 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
          _M0L11post__firedS1081
          = _M0MPC15array5Array2atGbE(_M0L10post__fireS1063, _M0L9post__idxS1079);
          _M0L5tpostS2895 = _M0L4varsS1067->$1;
          #line 366 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
          _M0L8tpost__iS1082
          = _M0MPC15array5Array2atGfE(_M0L5tpostS2895, _M0L9post__idxS1079);
          if (_M0L10pre__firedS1076) {
            float _M0L3etaS2875 = _M0L5paramS1065->$0;
            float _M0L2v0S2877 = _M0L5paramS1065->$1;
            float _M0L6_2atmpS2876 = _M0L8tpost__iS1082 - _M0L2v0S2877;
            float _M0L2dwS1083 = _M0L3etaS2875 * _M0L6_2atmpS2876;
            int32_t _M0L3valS2871 = _M0L1sS1078->$0;
            int32_t _M0L3valS2874 = _M0L1sS1078->$0;
            float _M0L6_2atmpS2873;
            float _M0L6_2atmpS2872;
            #line 369 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
            _M0L6_2atmpS2873
            = _M0MPC15array5Array2atGfE(_M0L1wS1084, _M0L3valS2874);
            _M0L6_2atmpS2872 = _M0L6_2atmpS2873 + _M0L2dwS1083;
            #line 369 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
            _M0MPC15array5Array3setGfE(_M0L1wS1084, _M0L3valS2871, _M0L6_2atmpS2872);
          }
          if (_M0L11post__firedS1081) {
            float _M0L3etaS2882 = _M0L5paramS1065->$0;
            float _M0L2dwS1085 = _M0L3etaS2882 * _M0L7tpre__jS1077;
            int32_t _M0L3valS2878 = _M0L1sS1078->$0;
            int32_t _M0L3valS2881 = _M0L1sS1078->$0;
            float _M0L6_2atmpS2880;
            float _M0L6_2atmpS2879;
            #line 373 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
            _M0L6_2atmpS2880
            = _M0MPC15array5Array2atGfE(_M0L1wS1084, _M0L3valS2881);
            _M0L6_2atmpS2879 = _M0L6_2atmpS2880 + _M0L2dwS1085;
            #line 373 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
            _M0MPC15array5Array3setGfE(_M0L1wS1084, _M0L3valS2878, _M0L6_2atmpS2879);
          }
          _M0L3valS2885 = _M0L1sS1078->$0;
          #line 376 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
          _M0L6_2atmpS2883
          = _M0MPC15array5Array2atGfE(_M0L1wS1084, _M0L3valS2885);
          _M0L6w__minS2884 = _M0L5paramS1065->$4;
          if (_M0L6_2atmpS2883 < _M0L6w__minS2884) {
            int32_t _M0L3valS2886 = _M0L1sS1078->$0;
            float _M0L6w__minS2887 = _M0L5paramS1065->$4;
            #line 376 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
            _M0MPC15array5Array3setGfE(_M0L1wS1084, _M0L3valS2886, _M0L6w__minS2887);
          }
          _M0L3valS2890 = _M0L1sS1078->$0;
          #line 377 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
          _M0L6_2atmpS2888
          = _M0MPC15array5Array2atGfE(_M0L1wS1084, _M0L3valS2890);
          _M0L6w__maxS2889 = _M0L5paramS1065->$3;
          if (_M0L6_2atmpS2888 > _M0L6w__maxS2889) {
            int32_t _M0L3valS2891 = _M0L1sS1078->$0;
            float _M0L6w__maxS2892 = _M0L5paramS1065->$3;
            #line 377 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
            _M0MPC15array5Array3setGfE(_M0L1wS1084, _M0L3valS2891, _M0L6w__maxS2892);
          }
          _M0L3valS2894 = _M0L1sS1078->$0;
          _M0L6_2atmpS2893 = _M0L3valS2894 + 1;
          _M0L1sS1078->$0 = _M0L6_2atmpS2893;
          continue;
        } else {
          moonbit_decref(_M0L1sS1078);
        }
        break;
      }
      _M0L3valS2898 = _M0L1jS1066->$0;
      _M0L6_2atmpS2897 = _M0L3valS2898 + 1;
      _M0L1jS1066->$0 = _M0L6_2atmpS2897;
      continue;
    } else {
      moonbit_decref(_M0L1jS1066);
    }
    break;
  }
  return 0;
}

struct _M0TP26RiantR8snn__mbt19IstdpPotentialEntry* _M0MP26RiantR8snn__mbt19IstdpPotentialEntry3new(
  int32_t _M0L11conn__indexS1056,
  int32_t _M0L6n__preS1057,
  int32_t _M0L7n__postS1058,
  struct _M0TP26RiantR8snn__mbt14IstdpPotential* _M0L11param_2eoptS1054
) {
  struct _M0TP26RiantR8snn__mbt14IstdpPotential* _M0L5paramS1053;
  struct _M0TP26RiantR8snn__mbt19IstdpPotentialEntry* _result_3046;
  if (_M0L11param_2eoptS1054 == 0) {
    #line 279 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
    _M0L5paramS1053 = _M0MP26RiantR8snn__mbt14IstdpPotential3new();
  } else {
    struct _M0TP26RiantR8snn__mbt14IstdpPotential* _M0L7_2aSomeS1055 =
      _M0L11param_2eoptS1054;
    if (_M0L7_2aSomeS1055) {
      moonbit_incref(_M0L7_2aSomeS1055);
    }
    _M0L5paramS1053 = _M0L7_2aSomeS1055;
  }
  _result_3046
  = _M0MP26RiantR8snn__mbt19IstdpPotentialEntry11new_2einner(_M0L11conn__indexS1056, _M0L6n__preS1057, _M0L7n__postS1058, _M0L5paramS1053);
  moonbit_decref(_M0L5paramS1053);
  return _result_3046;
}

struct _M0TP26RiantR8snn__mbt19IstdpPotentialEntry* _M0MP26RiantR8snn__mbt19IstdpPotentialEntry11new_2einner(
  int32_t _M0L11conn__indexS1049,
  int32_t _M0L6n__preS1050,
  int32_t _M0L7n__postS1051,
  struct _M0TP26RiantR8snn__mbt14IstdpPotential* _M0L5paramS1052
) {
  struct _M0TP26RiantR8snn__mbt23IstdpPotentialVariables* _M0L6_2atmpS2819;
  float* _M0L6_2atmpS2821;
  struct _M0TPB5ArrayGfE* _M0L6_2atmpS2820;
  struct _M0TP26RiantR8snn__mbt19IstdpPotentialEntry* _block_3047;
  #line 275 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
  #line 286 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
  _M0L6_2atmpS2819
  = _M0MP26RiantR8snn__mbt23IstdpPotentialVariables3new(_M0L6n__preS1050, _M0L7n__postS1051);
  _M0L6_2atmpS2821 = (float*)moonbit_make_float_array_raw(1);
  _M0L6_2atmpS2821[0] = 0x0p+0f;
  _M0L6_2atmpS2820
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_M0L6_2atmpS2820)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 0, 0);
  _M0L6_2atmpS2820->$0 = _M0L6_2atmpS2821;
  _M0L6_2atmpS2820->$1 = 1;
  moonbit_incref(_M0L5paramS1052);
  _block_3047
  = (struct _M0TP26RiantR8snn__mbt19IstdpPotentialEntry*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt19IstdpPotentialEntry));
  Moonbit_object_header(_block_3047)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 18, 0);
  _block_3047->$0 = _M0L11conn__indexS1049;
  _block_3047->$1 = _M0L6n__preS1050;
  _block_3047->$2 = _M0L7n__postS1051;
  _block_3047->$3 = _M0L5paramS1052;
  _block_3047->$4 = _M0L6_2atmpS2819;
  _block_3047->$5 = _M0L6_2atmpS2820;
  return _block_3047;
}

struct _M0TP26RiantR8snn__mbt23IstdpPotentialVariables* _M0MP26RiantR8snn__mbt23IstdpPotentialVariables3new(
  int32_t _M0L6n__preS1047,
  int32_t _M0L7n__postS1048
) {
  struct _M0TPB5ArrayGfE* _M0L6_2atmpS2817;
  struct _M0TPB5ArrayGfE* _M0L6_2atmpS2818;
  struct _M0TP26RiantR8snn__mbt23IstdpPotentialVariables* _block_3048;
  #line 252 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
  #line 257 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
  _M0L6_2atmpS2817 = _M0MPC15array5Array4makeGfE(_M0L6n__preS1047, 0x0p+0f);
  #line 258 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
  _M0L6_2atmpS2818 = _M0MPC15array5Array4makeGfE(_M0L7n__postS1048, 0x0p+0f);
  _block_3048
  = (struct _M0TP26RiantR8snn__mbt23IstdpPotentialVariables*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt23IstdpPotentialVariables));
  Moonbit_object_header(_block_3048)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 23, 0);
  _block_3048->$0 = _M0L6_2atmpS2817;
  _block_3048->$1 = _M0L6_2atmpS2818;
  return _block_3048;
}

struct _M0TP26RiantR8snn__mbt14IstdpPotential* _M0MP26RiantR8snn__mbt14IstdpPotential3new(
  
) {
  struct _M0TP26RiantR8snn__mbt14IstdpPotential* _block_3049;
  #line 230 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
  _block_3049
  = (struct _M0TP26RiantR8snn__mbt14IstdpPotential*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt14IstdpPotential));
  Moonbit_object_header(_block_3049)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_3049->$0 = 0x1.0624dd2f1a9fcp-10f;
  _block_3049->$1 = -0x1.9p+5f;
  _block_3049->$2 = 0x1.9p+7f;
  _block_3049->$3 = 0x1.e6p+7f;
  _block_3049->$4 = 0x1.47ae147ae147bp-7f;
  return _block_3049;
}

int32_t _M0FP26RiantR8snn__mbt17istdp__rate__step(
  struct _M0TPB5ArrayGfE* _M0L1wS1043,
  struct _M0TPB5ArrayGbE* _M0L9pre__fireS1021,
  struct _M0TPB5ArrayGbE* _M0L10post__fireS1023,
  struct _M0TPB5ArrayGiE* _M0L6colptrS1039,
  struct _M0TPB5ArrayGiE* _M0L6rowptrS1033,
  struct _M0TP26RiantR8snn__mbt18IstdpRateVariables* _M0L4varsS1027,
  struct _M0TP26RiantR8snn__mbt9IstdpRate* _M0L5paramS1025,
  float _M0L6t__nowS1019,
  float _M0L2dtS1028
) {
  int32_t _M0L6n__preS1020;
  int32_t _M0L7n__postS1022;
  float _M0L6tau__yS2816;
  float _M0L11inv__tau__yS1024;
  struct _M0TPB8MutLocalGiE* _M0L1jS1026;
  struct _M0TPB8MutLocalGiE* _M0L1iS1030;
  #line 141 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
  #line 153 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
  _M0L6n__preS1020 = _M0MPC15array5Array6lengthGbE(_M0L9pre__fireS1021);
  #line 154 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
  _M0L7n__postS1022 = _M0MPC15array5Array6lengthGbE(_M0L10post__fireS1023);
  _M0L6tau__yS2816 = _M0L5paramS1025->$2;
  _M0L11inv__tau__yS1024 = 0x1p+0f / _M0L6tau__yS2816;
  _M0L1jS1026
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1jS1026)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1jS1026->$0 = 0;
  while (1) {
    int32_t _M0L3valS2733 = _M0L1jS1026->$0;
    if (_M0L3valS2733 < _M0L6n__preS1020) {
      struct _M0TPB5ArrayGfE* _M0L4tpreS2734 = _M0L4varsS1027->$0;
      int32_t _M0L3valS2735 = _M0L1jS1026->$0;
      struct _M0TPB5ArrayGfE* _M0L4tpreS2744 = _M0L4varsS1027->$0;
      int32_t _M0L3valS2745 = _M0L1jS1026->$0;
      float _M0L6_2atmpS2737;
      struct _M0TPB5ArrayGfE* _M0L4tpreS2742;
      int32_t _M0L3valS2743;
      float _M0L6_2atmpS2741;
      float _M0L6_2atmpS2740;
      float _M0L6_2atmpS2739;
      float _M0L6_2atmpS2738;
      float _M0L6_2atmpS2736;
      int32_t _M0L3valS2746;
      int32_t _M0L3valS2754;
      int32_t _M0L6_2atmpS2753;
      #line 159 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      _M0L6_2atmpS2737
      = _M0MPC15array5Array2atGfE(_M0L4tpreS2744, _M0L3valS2745);
      _M0L4tpreS2742 = _M0L4varsS1027->$0;
      _M0L3valS2743 = _M0L1jS1026->$0;
      #line 159 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      _M0L6_2atmpS2741
      = _M0MPC15array5Array2atGfE(_M0L4tpreS2742, _M0L3valS2743);
      _M0L6_2atmpS2740 = -_M0L6_2atmpS2741;
      _M0L6_2atmpS2739 = _M0L2dtS1028 * _M0L6_2atmpS2740;
      _M0L6_2atmpS2738 = _M0L6_2atmpS2739 * _M0L11inv__tau__yS1024;
      _M0L6_2atmpS2736 = _M0L6_2atmpS2737 + _M0L6_2atmpS2738;
      #line 159 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      _M0MPC15array5Array3setGfE(_M0L4tpreS2734, _M0L3valS2735, _M0L6_2atmpS2736);
      _M0L3valS2746 = _M0L1jS1026->$0;
      #line 160 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L9pre__fireS1021, _M0L3valS2746)) {
        struct _M0TPB5ArrayGfE* _M0L4tpreS2747 = _M0L4varsS1027->$0;
        int32_t _M0L3valS2748 = _M0L1jS1026->$0;
        struct _M0TPB5ArrayGfE* _M0L4tpreS2751 = _M0L4varsS1027->$0;
        int32_t _M0L3valS2752 = _M0L1jS1026->$0;
        float _M0L6_2atmpS2750;
        float _M0L6_2atmpS2749;
        #line 161 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
        _M0L6_2atmpS2750
        = _M0MPC15array5Array2atGfE(_M0L4tpreS2751, _M0L3valS2752);
        _M0L6_2atmpS2749 = _M0L6_2atmpS2750 + 0x1p+0f;
        #line 161 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L4tpreS2747, _M0L3valS2748, _M0L6_2atmpS2749);
      }
      _M0L3valS2754 = _M0L1jS1026->$0;
      _M0L6_2atmpS2753 = _M0L3valS2754 + 1;
      _M0L1jS1026->$0 = _M0L6_2atmpS2753;
      continue;
    }
    break;
  }
  _M0L1iS1030
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1iS1030)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1iS1030->$0 = 0;
  while (1) {
    int32_t _M0L3valS2755 = _M0L1iS1030->$0;
    if (_M0L3valS2755 < _M0L7n__postS1022) {
      struct _M0TPB5ArrayGfE* _M0L5tpostS2756 = _M0L4varsS1027->$1;
      int32_t _M0L3valS2757 = _M0L1iS1030->$0;
      struct _M0TPB5ArrayGfE* _M0L5tpostS2766 = _M0L4varsS1027->$1;
      int32_t _M0L3valS2767 = _M0L1iS1030->$0;
      float _M0L6_2atmpS2759;
      struct _M0TPB5ArrayGfE* _M0L5tpostS2764;
      int32_t _M0L3valS2765;
      float _M0L6_2atmpS2763;
      float _M0L6_2atmpS2762;
      float _M0L6_2atmpS2761;
      float _M0L6_2atmpS2760;
      float _M0L6_2atmpS2758;
      int32_t _M0L3valS2768;
      int32_t _M0L3valS2776;
      int32_t _M0L6_2atmpS2775;
      #line 167 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      _M0L6_2atmpS2759
      = _M0MPC15array5Array2atGfE(_M0L5tpostS2766, _M0L3valS2767);
      _M0L5tpostS2764 = _M0L4varsS1027->$1;
      _M0L3valS2765 = _M0L1iS1030->$0;
      #line 167 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      _M0L6_2atmpS2763
      = _M0MPC15array5Array2atGfE(_M0L5tpostS2764, _M0L3valS2765);
      _M0L6_2atmpS2762 = -_M0L6_2atmpS2763;
      _M0L6_2atmpS2761 = _M0L2dtS1028 * _M0L6_2atmpS2762;
      _M0L6_2atmpS2760 = _M0L6_2atmpS2761 * _M0L11inv__tau__yS1024;
      _M0L6_2atmpS2758 = _M0L6_2atmpS2759 + _M0L6_2atmpS2760;
      #line 167 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      _M0MPC15array5Array3setGfE(_M0L5tpostS2756, _M0L3valS2757, _M0L6_2atmpS2758);
      _M0L3valS2768 = _M0L1iS1030->$0;
      #line 168 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L10post__fireS1023, _M0L3valS2768)) {
        struct _M0TPB5ArrayGfE* _M0L5tpostS2769 = _M0L4varsS1027->$1;
        int32_t _M0L3valS2770 = _M0L1iS1030->$0;
        struct _M0TPB5ArrayGfE* _M0L5tpostS2773 = _M0L4varsS1027->$1;
        int32_t _M0L3valS2774 = _M0L1iS1030->$0;
        float _M0L6_2atmpS2772;
        float _M0L6_2atmpS2771;
        #line 169 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
        _M0L6_2atmpS2772
        = _M0MPC15array5Array2atGfE(_M0L5tpostS2773, _M0L3valS2774);
        _M0L6_2atmpS2771 = _M0L6_2atmpS2772 + 0x1p+0f;
        #line 169 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L5tpostS2769, _M0L3valS2770, _M0L6_2atmpS2771);
      }
      _M0L3valS2776 = _M0L1iS1030->$0;
      _M0L6_2atmpS2775 = _M0L3valS2776 + 1;
      _M0L1iS1030->$0 = _M0L6_2atmpS2775;
      continue;
    } else {
      moonbit_decref(_M0L1iS1030);
    }
    break;
  }
  _M0L1jS1026->$0 = 0;
  while (1) {
    int32_t _M0L3valS2777 = _M0L1jS1026->$0;
    if (_M0L3valS2777 < _M0L6n__preS1020) {
      int32_t _M0L3valS2815 = _M0L1jS1026->$0;
      int32_t _M0L5startS1032;
      int32_t _M0L3valS2814;
      int32_t _M0L6_2atmpS2813;
      int32_t _M0L3endS1034;
      int32_t _M0L3valS2812;
      int32_t _M0L10pre__firedS1035;
      struct _M0TPB5ArrayGfE* _M0L4tpreS2810;
      int32_t _M0L3valS2811;
      float _M0L7tpre__jS1036;
      struct _M0TPB8MutLocalGiE* _M0L1sS1037;
      int32_t _M0L3valS2809;
      int32_t _M0L6_2atmpS2808;
      #line 179 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      _M0L5startS1032
      = _M0MPC15array5Array2atGiE(_M0L6rowptrS1033, _M0L3valS2815);
      _M0L3valS2814 = _M0L1jS1026->$0;
      _M0L6_2atmpS2813 = _M0L3valS2814 + 1;
      #line 180 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      _M0L3endS1034
      = _M0MPC15array5Array2atGiE(_M0L6rowptrS1033, _M0L6_2atmpS2813);
      _M0L3valS2812 = _M0L1jS1026->$0;
      #line 181 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      _M0L10pre__firedS1035
      = _M0MPC15array5Array2atGbE(_M0L9pre__fireS1021, _M0L3valS2812);
      _M0L4tpreS2810 = _M0L4varsS1027->$0;
      _M0L3valS2811 = _M0L1jS1026->$0;
      #line 182 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      _M0L7tpre__jS1036
      = _M0MPC15array5Array2atGfE(_M0L4tpreS2810, _M0L3valS2811);
      _M0L1sS1037
      = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
      Moonbit_object_header(_M0L1sS1037)->meta
      = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
      _M0L1sS1037->$0 = _M0L5startS1032;
      while (1) {
        int32_t _M0L3valS2778 = _M0L1sS1037->$0;
        if (_M0L3valS2778 < _M0L3endS1034) {
          int32_t _M0L3valS2807 = _M0L1sS1037->$0;
          int32_t _M0L9post__idxS1038;
          int32_t _M0L11post__firedS1040;
          struct _M0TPB5ArrayGfE* _M0L5tpostS2806;
          float _M0L8tpost__iS1041;
          int32_t _M0L3valS2796;
          float _M0L6_2atmpS2794;
          float _M0L6w__minS2795;
          int32_t _M0L3valS2801;
          float _M0L6_2atmpS2799;
          float _M0L6w__maxS2800;
          int32_t _M0L3valS2805;
          int32_t _M0L6_2atmpS2804;
          #line 185 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
          _M0L9post__idxS1038
          = _M0MPC15array5Array2atGiE(_M0L6colptrS1039, _M0L3valS2807);
          #line 186 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
          _M0L11post__firedS1040
          = _M0MPC15array5Array2atGbE(_M0L10post__fireS1023, _M0L9post__idxS1038);
          _M0L5tpostS2806 = _M0L4varsS1027->$1;
          #line 187 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
          _M0L8tpost__iS1041
          = _M0MPC15array5Array2atGfE(_M0L5tpostS2806, _M0L9post__idxS1038);
          if (_M0L10pre__firedS1035) {
            float _M0L3etaS2783 = _M0L5paramS1025->$0;
            float _M0L1rS2788 = _M0L5paramS1025->$1;
            float _M0L6_2atmpS2786 = 0x1p+1f * _M0L1rS2788;
            float _M0L6tau__yS2787 = _M0L5paramS1025->$2;
            float _M0L6_2atmpS2785 = _M0L6_2atmpS2786 * _M0L6tau__yS2787;
            float _M0L6_2atmpS2784 = _M0L8tpost__iS1041 - _M0L6_2atmpS2785;
            float _M0L2dwS1042 = _M0L3etaS2783 * _M0L6_2atmpS2784;
            int32_t _M0L3valS2779 = _M0L1sS1037->$0;
            int32_t _M0L3valS2782 = _M0L1sS1037->$0;
            float _M0L6_2atmpS2781;
            float _M0L6_2atmpS2780;
            #line 190 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
            _M0L6_2atmpS2781
            = _M0MPC15array5Array2atGfE(_M0L1wS1043, _M0L3valS2782);
            _M0L6_2atmpS2780 = _M0L6_2atmpS2781 + _M0L2dwS1042;
            #line 190 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
            _M0MPC15array5Array3setGfE(_M0L1wS1043, _M0L3valS2779, _M0L6_2atmpS2780);
          }
          if (_M0L11post__firedS1040) {
            float _M0L3etaS2793 = _M0L5paramS1025->$0;
            float _M0L2dwS1044 = _M0L3etaS2793 * _M0L7tpre__jS1036;
            int32_t _M0L3valS2789 = _M0L1sS1037->$0;
            int32_t _M0L3valS2792 = _M0L1sS1037->$0;
            float _M0L6_2atmpS2791;
            float _M0L6_2atmpS2790;
            #line 194 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
            _M0L6_2atmpS2791
            = _M0MPC15array5Array2atGfE(_M0L1wS1043, _M0L3valS2792);
            _M0L6_2atmpS2790 = _M0L6_2atmpS2791 + _M0L2dwS1044;
            #line 194 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
            _M0MPC15array5Array3setGfE(_M0L1wS1043, _M0L3valS2789, _M0L6_2atmpS2790);
          }
          _M0L3valS2796 = _M0L1sS1037->$0;
          #line 197 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
          _M0L6_2atmpS2794
          = _M0MPC15array5Array2atGfE(_M0L1wS1043, _M0L3valS2796);
          _M0L6w__minS2795 = _M0L5paramS1025->$4;
          if (_M0L6_2atmpS2794 < _M0L6w__minS2795) {
            int32_t _M0L3valS2797 = _M0L1sS1037->$0;
            float _M0L6w__minS2798 = _M0L5paramS1025->$4;
            #line 197 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
            _M0MPC15array5Array3setGfE(_M0L1wS1043, _M0L3valS2797, _M0L6w__minS2798);
          }
          _M0L3valS2801 = _M0L1sS1037->$0;
          #line 198 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
          _M0L6_2atmpS2799
          = _M0MPC15array5Array2atGfE(_M0L1wS1043, _M0L3valS2801);
          _M0L6w__maxS2800 = _M0L5paramS1025->$3;
          if (_M0L6_2atmpS2799 > _M0L6w__maxS2800) {
            int32_t _M0L3valS2802 = _M0L1sS1037->$0;
            float _M0L6w__maxS2803 = _M0L5paramS1025->$3;
            #line 198 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
            _M0MPC15array5Array3setGfE(_M0L1wS1043, _M0L3valS2802, _M0L6w__maxS2803);
          }
          _M0L3valS2805 = _M0L1sS1037->$0;
          _M0L6_2atmpS2804 = _M0L3valS2805 + 1;
          _M0L1sS1037->$0 = _M0L6_2atmpS2804;
          continue;
        } else {
          moonbit_decref(_M0L1sS1037);
        }
        break;
      }
      _M0L3valS2809 = _M0L1jS1026->$0;
      _M0L6_2atmpS2808 = _M0L3valS2809 + 1;
      _M0L1jS1026->$0 = _M0L6_2atmpS2808;
      continue;
    } else {
      moonbit_decref(_M0L1jS1026);
    }
    break;
  }
  return 0;
}

struct _M0TP26RiantR8snn__mbt14IstdpRateEntry* _M0MP26RiantR8snn__mbt14IstdpRateEntry3new(
  int32_t _M0L11conn__indexS1016,
  int32_t _M0L6n__preS1017,
  int32_t _M0L7n__postS1018,
  struct _M0TP26RiantR8snn__mbt9IstdpRate* _M0L11param_2eoptS1014
) {
  struct _M0TP26RiantR8snn__mbt9IstdpRate* _M0L5paramS1013;
  struct _M0TP26RiantR8snn__mbt14IstdpRateEntry* _result_3054;
  if (_M0L11param_2eoptS1014 == 0) {
    #line 101 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
    _M0L5paramS1013 = _M0MP26RiantR8snn__mbt9IstdpRate3new();
  } else {
    struct _M0TP26RiantR8snn__mbt9IstdpRate* _M0L7_2aSomeS1015 =
      _M0L11param_2eoptS1014;
    if (_M0L7_2aSomeS1015) {
      moonbit_incref(_M0L7_2aSomeS1015);
    }
    _M0L5paramS1013 = _M0L7_2aSomeS1015;
  }
  _result_3054
  = _M0MP26RiantR8snn__mbt14IstdpRateEntry11new_2einner(_M0L11conn__indexS1016, _M0L6n__preS1017, _M0L7n__postS1018, _M0L5paramS1013);
  moonbit_decref(_M0L5paramS1013);
  return _result_3054;
}

struct _M0TP26RiantR8snn__mbt14IstdpRateEntry* _M0MP26RiantR8snn__mbt14IstdpRateEntry11new_2einner(
  int32_t _M0L11conn__indexS1009,
  int32_t _M0L6n__preS1010,
  int32_t _M0L7n__postS1011,
  struct _M0TP26RiantR8snn__mbt9IstdpRate* _M0L5paramS1012
) {
  struct _M0TP26RiantR8snn__mbt18IstdpRateVariables* _M0L6_2atmpS2730;
  float* _M0L6_2atmpS2732;
  struct _M0TPB5ArrayGfE* _M0L6_2atmpS2731;
  struct _M0TP26RiantR8snn__mbt14IstdpRateEntry* _block_3055;
  #line 97 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
  #line 108 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
  _M0L6_2atmpS2730
  = _M0MP26RiantR8snn__mbt18IstdpRateVariables3new(_M0L6n__preS1010, _M0L7n__postS1011);
  _M0L6_2atmpS2732 = (float*)moonbit_make_float_array_raw(1);
  _M0L6_2atmpS2732[0] = 0x0p+0f;
  _M0L6_2atmpS2731
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_M0L6_2atmpS2731)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 0, 0);
  _M0L6_2atmpS2731->$0 = _M0L6_2atmpS2732;
  _M0L6_2atmpS2731->$1 = 1;
  moonbit_incref(_M0L5paramS1012);
  _block_3055
  = (struct _M0TP26RiantR8snn__mbt14IstdpRateEntry*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt14IstdpRateEntry));
  Moonbit_object_header(_block_3055)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 27, 0);
  _block_3055->$0 = _M0L11conn__indexS1009;
  _block_3055->$1 = _M0L6n__preS1010;
  _block_3055->$2 = _M0L7n__postS1011;
  _block_3055->$3 = _M0L5paramS1012;
  _block_3055->$4 = _M0L6_2atmpS2730;
  _block_3055->$5 = _M0L6_2atmpS2731;
  return _block_3055;
}

struct _M0TP26RiantR8snn__mbt18IstdpRateVariables* _M0MP26RiantR8snn__mbt18IstdpRateVariables3new(
  int32_t _M0L6n__preS1007,
  int32_t _M0L7n__postS1008
) {
  struct _M0TPB5ArrayGfE* _M0L6_2atmpS2728;
  struct _M0TPB5ArrayGfE* _M0L6_2atmpS2729;
  struct _M0TP26RiantR8snn__mbt18IstdpRateVariables* _block_3056;
  #line 74 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
  #line 76 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
  _M0L6_2atmpS2728 = _M0MPC15array5Array4makeGfE(_M0L6n__preS1007, 0x0p+0f);
  #line 77 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
  _M0L6_2atmpS2729 = _M0MPC15array5Array4makeGfE(_M0L7n__postS1008, 0x0p+0f);
  _block_3056
  = (struct _M0TP26RiantR8snn__mbt18IstdpRateVariables*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt18IstdpRateVariables));
  Moonbit_object_header(_block_3056)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 32, 0);
  _block_3056->$0 = _M0L6_2atmpS2728;
  _block_3056->$1 = _M0L6_2atmpS2729;
  return _block_3056;
}

struct _M0TP26RiantR8snn__mbt9IstdpRate* _M0MP26RiantR8snn__mbt9IstdpRate3new(
  
) {
  float _M0L6_2atmpS2727;
  struct _M0TP26RiantR8snn__mbt9IstdpRate* _block_3057;
  #line 51 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
  _M0L6_2atmpS2727 = 0x1.8p+1f * _M0FP26RiantR8snn__mbt2hz;
  _block_3057
  = (struct _M0TP26RiantR8snn__mbt9IstdpRate*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt9IstdpRate));
  Moonbit_object_header(_block_3057)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_3057->$0 = 0x1.47ae147ae147bp-7f;
  _block_3057->$1 = _M0L6_2atmpS2727;
  _block_3057->$2 = 0x1.9p+5f;
  _block_3057->$3 = 0x1.e6p+7f;
  _block_3057->$4 = 0x1.47ae147ae147bp-7f;
  return _block_3057;
}

struct _M0TP26RiantR8snn__mbt11IFParameter* _M0MP26RiantR8snn__mbt11IFParameter6custom(
  float _M0L2tmS1002,
  float _M0L2vtS1003,
  float _M0L2vrS1004,
  float _M0L2elS1005,
  float _M0L1rS1006
) {
  float _M0L1cS1000;
  float _M0L2glS1001;
  struct _M0TP26RiantR8snn__mbt11IFParameter* _block_3058;
  #line 62 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L1cS1000 = -0x1p+0f;
  _M0L2glS1001 = -0x1p+0f;
  _block_3058
  = (struct _M0TP26RiantR8snn__mbt11IFParameter*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt11IFParameter));
  Moonbit_object_header(_block_3058)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_3058->$0 = _M0L1cS1000;
  _block_3058->$1 = _M0L2glS1001;
  _block_3058->$2 = _M0L2tmS1002;
  _block_3058->$3 = _M0L2vtS1003;
  _block_3058->$4 = _M0L2vrS1004;
  _block_3058->$5 = _M0L2elS1005;
  _block_3058->$6 = _M0L1rS1006;
  _block_3058->$7 = 0x1p+1f;
  _block_3058->$8 = 0x0p+0f;
  _block_3058->$9 = 0x0p+0f;
  _block_3058->$10 = 0x0p+0f;
  return _block_3058;
}

struct _M0TP26RiantR8snn__mbt2IF* _M0MP26RiantR8snn__mbt2IF3new(
  int32_t _M0L1nS974,
  struct _M0TP26RiantR8snn__mbt11IFParameter* _M0L5paramS976,
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS979
) {
  struct _M0TPB5ArrayGfE* _M0L1vS973;
  float _M0L2vtS2725;
  float _M0L2vrS2726;
  float _M0L6spreadS975;
  int32_t _M0L7_2abindS977;
  int32_t _M0L1kS978;
  struct _M0TPB5ArrayGfE* _M0L1wS981;
  struct _M0TPB5ArrayGbE* _M0L4fireS982;
  struct _M0TPB5ArrayGiE* _M0L4tabsS983;
  struct _M0TPB5ArrayGfE* _M0L1iS984;
  struct _M0TPB5ArrayGfE* _M0L9syn__currS985;
  struct _M0TPB5ArrayGfE* _M0L2geS986;
  struct _M0TPB5ArrayGfE* _M0L2giS987;
  struct _M0TPB5ArrayGfE* _M0L2heS988;
  struct _M0TPB5ArrayGfE* _M0L2hiS989;
  struct _M0TPB5ArrayGfE* _M0L3gluS990;
  struct _M0TPB5ArrayGfE* _M0L4gabaS991;
  struct _M0TPB5ArrayGfE* _M0L7gsyn__eS992;
  struct _M0TPB5ArrayGfE* _M0L7gsyn__iS993;
  float _M0L4e__eS994;
  float _M0L4e__iS995;
  float _M0L3treS996;
  float _M0L3tdeS997;
  float _M0L3triS998;
  float _M0L3tdiS999;
  struct _M0TP26RiantR8snn__mbt9PostSpike* _M0L6_2atmpS2724;
  struct _M0TP26RiantR8snn__mbt2IF* _block_3060;
  #line 113 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  #line 114 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L1vS973 = _M0MPC15array5Array4makeGfE(_M0L1nS974, 0x0p+0f);
  _M0L2vtS2725 = _M0L5paramS976->$3;
  _M0L2vrS2726 = _M0L5paramS976->$4;
  _M0L6spreadS975 = _M0L2vtS2725 - _M0L2vrS2726;
  _M0L7_2abindS977 = 0;
  _M0L1kS978 = _M0L7_2abindS977;
  while (1) {
    if (_M0L1kS978 < _M0L1nS974) {
      float _M0L2vrS2720 = _M0L5paramS976->$4;
      float _M0L6_2atmpS2722;
      float _M0L6_2atmpS2721;
      float _M0L6_2atmpS2719;
      int32_t _M0L6_2atmpS2723;
      #line 117 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2722 = _M0FP26RiantR8snn__mbt9next__f32(_M0L3rngS979);
      _M0L6_2atmpS2721 = _M0L6_2atmpS2722 * _M0L6spreadS975;
      _M0L6_2atmpS2719 = _M0L2vrS2720 + _M0L6_2atmpS2721;
      #line 117 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS973, _M0L1kS978, _M0L6_2atmpS2719);
      _M0L6_2atmpS2723 = _M0L1kS978 + 1;
      _M0L1kS978 = _M0L6_2atmpS2723;
      continue;
    }
    break;
  }
  #line 119 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L1wS981 = _M0MPC15array5Array4makeGfE(_M0L1nS974, 0x0p+0f);
  #line 120 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L4fireS982 = _M0MPC15array5Array4makeGbE(_M0L1nS974, 0);
  #line 121 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L4tabsS983 = _M0MPC15array5Array4makeGiE(_M0L1nS974, 0);
  #line 122 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L1iS984 = _M0MPC15array5Array4makeGfE(_M0L1nS974, 0x0p+0f);
  #line 123 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L9syn__currS985 = _M0MPC15array5Array4makeGfE(_M0L1nS974, 0x0p+0f);
  #line 124 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L2geS986 = _M0MPC15array5Array4makeGfE(_M0L1nS974, 0x0p+0f);
  #line 125 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L2giS987 = _M0MPC15array5Array4makeGfE(_M0L1nS974, 0x0p+0f);
  #line 126 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L2heS988 = _M0MPC15array5Array4makeGfE(_M0L1nS974, 0x0p+0f);
  #line 127 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L2hiS989 = _M0MPC15array5Array4makeGfE(_M0L1nS974, 0x0p+0f);
  #line 128 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L3gluS990 = _M0MPC15array5Array4makeGfE(_M0L1nS974, 0x0p+0f);
  #line 129 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L4gabaS991 = _M0MPC15array5Array4makeGfE(_M0L1nS974, 0x0p+0f);
  #line 130 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L7gsyn__eS992 = _M0MPC15array5Array4makeGfE(_M0L1nS974, 0x1p+0f);
  #line 131 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L7gsyn__iS993 = _M0MPC15array5Array4makeGfE(_M0L1nS974, 0x1p+0f);
  _M0L4e__eS994 = 0x0p+0f;
  _M0L4e__iS995 = -0x1.2cp+6f;
  _M0L3treS996 = 0x1p+0f;
  _M0L3tdeS997 = 0x1.8p+2f;
  _M0L3triS998 = 0x1p-1f;
  _M0L3tdiS999 = 0x1p+1f;
  #line 138 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L6_2atmpS2724 = _M0MP26RiantR8snn__mbt9PostSpike3new();
  moonbit_incref(_M0L5paramS976);
  _block_3060
  = (struct _M0TP26RiantR8snn__mbt2IF*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt2IF));
  Moonbit_object_header(_block_3060)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 36, 0);
  _block_3060->$0 = _M0L5paramS976;
  _block_3060->$1 = _M0L6_2atmpS2724;
  _block_3060->$2 = _M0L1nS974;
  _block_3060->$3 = _M0L1vS973;
  _block_3060->$4 = _M0L1wS981;
  _block_3060->$5 = _M0L4fireS982;
  _block_3060->$6 = _M0L4tabsS983;
  _block_3060->$7 = _M0L1iS984;
  _block_3060->$8 = _M0L9syn__currS985;
  _block_3060->$9 = _M0L2geS986;
  _block_3060->$10 = _M0L2giS987;
  _block_3060->$11 = _M0L2heS988;
  _block_3060->$12 = _M0L2hiS989;
  _block_3060->$13 = _M0L3gluS990;
  _block_3060->$14 = _M0L4gabaS991;
  _block_3060->$15 = _M0L7gsyn__eS992;
  _block_3060->$16 = _M0L7gsyn__iS993;
  _block_3060->$17 = _M0L4e__eS994;
  _block_3060->$18 = _M0L4e__iS995;
  _block_3060->$19 = _M0L3treS996;
  _block_3060->$20 = _M0L3tdeS997;
  _block_3060->$21 = _M0L3triS998;
  _block_3060->$22 = _M0L3tdiS999;
  return _block_3060;
}

struct _M0TP26RiantR8snn__mbt9PostSpike* _M0MP26RiantR8snn__mbt9PostSpike3new(
  
) {
  struct _M0TP26RiantR8snn__mbt9PostSpike* _block_3061;
  #line 75 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _block_3061
  = (struct _M0TP26RiantR8snn__mbt9PostSpike*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt9PostSpike));
  Moonbit_object_header(_block_3061)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_3061->$0 = 0x1p+1f;
  return _block_3061;
}

struct _M0TP26RiantR8snn__mbt13AdExParameter* _M0MP26RiantR8snn__mbt13AdExParameter3new(
  
) {
  float _M0L1cS969;
  float _M0L2glS970;
  float _M0L2tmS971;
  float _M0L1rS972;
  struct _M0TP26RiantR8snn__mbt13AdExParameter* _block_3062;
  #line 30 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
  _M0L1cS969 = 0x1.19p+8f;
  _M0L2glS970 = 0x1.4p+5f;
  _M0L2tmS971 = 0x1.19p+8f / 0x1.4p+5f;
  _M0L1rS972 = 0x1p+0f / 0x1.4p+5f;
  _block_3062
  = (struct _M0TP26RiantR8snn__mbt13AdExParameter*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt13AdExParameter));
  Moonbit_object_header(_block_3062)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_3062->$0 = _M0L1cS969;
  _block_3062->$1 = _M0L2glS970;
  _block_3062->$2 = -0x1.9p+5f;
  _block_3062->$3 = -0x1.1a66666666666p+6f;
  _block_3062->$4 = -0x1.1a66666666666p+6f;
  _block_3062->$5 = _M0L2tmS971;
  _block_3062->$6 = _M0L1rS972;
  _block_3062->$7 = 0x1p+1f;
  _block_3062->$8 = 0x1.2p+7f;
  _block_3062->$9 = 0x1p+2f;
  _block_3062->$10 = 0x1.42p+6f;
  return _block_3062;
}

int32_t _M0FP26RiantR8snn__mbt12step__tripod(
  struct _M0TP26RiantR8snn__mbt6Tripod* _M0L1pS948,
  float _M0L2dtS959
) {
  int32_t _M0L1nS947;
  struct _M0TP26RiantR8snn__mbt13AdExParameter* _M0L3p__S949;
  float _M0L2vtS950;
  float _M0L2vrS951;
  float _M0L1bS952;
  struct _M0TP26RiantR8snn__mbt13AdExPostSpike* _M0L11soma__spikeS2718;
  float _M0L2atS953;
  struct _M0TP26RiantR8snn__mbt13AdExPostSpike* _M0L11soma__spikeS2717;
  float _M0L6tau__aS954;
  struct _M0TP26RiantR8snn__mbt13AdExPostSpike* _M0L11soma__spikeS2716;
  float _M0L11tabs__constS955;
  struct _M0TP26RiantR8snn__mbt13AdExPostSpike* _M0L11soma__spikeS2715;
  float _M0L2upS956;
  struct _M0TP26RiantR8snn__mbt13AdExPostSpike* _M0L11soma__spikeS2714;
  float _M0L12ap__membraneS957;
  float _M0L6_2atmpS2713;
  float _M0L6_2atmpS2712;
  int32_t _M0L11tabs__stepsS958;
  int32_t _M0L7_2abindS960;
  int32_t _M0L7_2abindS961;
  int32_t _M0L1iS962;
  int32_t _M0L7_2abindS964;
  int32_t _M0L1kS965;
  #line 289 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
  _M0L1nS947 = _M0L1pS948->$25;
  _M0L3p__S949 = _M0L1pS948->$0;
  _M0L2vtS950 = _M0L3p__S949->$2;
  _M0L2vrS951 = _M0L3p__S949->$3;
  _M0L1bS952 = _M0L3p__S949->$10;
  _M0L11soma__spikeS2718 = _M0L1pS948->$1;
  _M0L2atS953 = _M0L11soma__spikeS2718->$0;
  _M0L11soma__spikeS2717 = _M0L1pS948->$1;
  _M0L6tau__aS954 = _M0L11soma__spikeS2717->$1;
  _M0L11soma__spikeS2716 = _M0L1pS948->$1;
  _M0L11tabs__constS955 = _M0L11soma__spikeS2716->$3;
  _M0L11soma__spikeS2715 = _M0L1pS948->$1;
  _M0L2upS956 = _M0L11soma__spikeS2715->$4;
  _M0L11soma__spikeS2714 = _M0L1pS948->$1;
  _M0L12ap__membraneS957 = _M0L11soma__spikeS2714->$2;
  _M0L6_2atmpS2713 = _M0L2upS956 + _M0L11tabs__constS955;
  _M0L6_2atmpS2712 = _M0L6_2atmpS2713 / _M0L2dtS959;
  #line 300 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
  _M0L11tabs__stepsS958 = _M0MPC15float5Float7to__int(_M0L6_2atmpS2712);
  #line 303 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
  _M0FP26RiantR8snn__mbt28tripod__soma__step__synapses(_M0L1pS948, _M0L2dtS959);
  #line 304 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
  _M0FP26RiantR8snn__mbt28tripod__dend__step__synapses(_M0L1pS948, _M0L2dtS959);
  #line 307 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
  _M0FP26RiantR8snn__mbt23tripod__syn__curr__soma(_M0L1pS948);
  #line 308 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
  _M0FP26RiantR8snn__mbt24tripod__syn__curr__dends(_M0L1pS948);
  #line 311 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
  _M0FP26RiantR8snn__mbt18tripod__heun__step(_M0L1pS948, _M0L2dtS959, 0);
  _M0L7_2abindS960 = 0;
  _M0L7_2abindS961 = _M0L1nS947 * 4;
  _M0L1iS962 = _M0L7_2abindS960;
  while (1) {
    if (_M0L1iS962 < _M0L7_2abindS961) {
      struct _M0TPB5ArrayGfE* _M0L8dv__tempS2573 = _M0L1pS948->$34;
      struct _M0TPB5ArrayGfE* _M0L2dvS2575 = _M0L1pS948->$33;
      float _M0L6_2atmpS2574;
      int32_t _M0L6_2atmpS2576;
      #line 313 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS2574 = _M0MPC15array5Array2atGfE(_M0L2dvS2575, _M0L1iS962);
      #line 313 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0MPC15array5Array3setGfE(_M0L8dv__tempS2573, _M0L1iS962, _M0L6_2atmpS2574);
      _M0L6_2atmpS2576 = _M0L1iS962 + 1;
      _M0L1iS962 = _M0L6_2atmpS2576;
      continue;
    }
    break;
  }
  #line 315 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
  _M0FP26RiantR8snn__mbt18tripod__heun__step(_M0L1pS948, _M0L2dtS959, 1);
  _M0L7_2abindS964 = 0;
  _M0L1kS965 = _M0L7_2abindS964;
  while (1) {
    if (_M0L1kS965 < _M0L1nS947) {
      struct _M0TPB5ArrayGiE* _M0L4tabsS2578 = _M0L1pS948->$32;
      struct _M0TPB5ArrayGiE* _M0L4tabsS2581 = _M0L1pS948->$32;
      int32_t _M0L6_2atmpS2580;
      int32_t _M0L6_2atmpS2579;
      struct _M0TPB5ArrayGfE* _M0L9thresholdS2582;
      struct _M0TPB5ArrayGfE* _M0L9thresholdS2590;
      float _M0L6_2atmpS2584;
      struct _M0TPB5ArrayGfE* _M0L9thresholdS2589;
      float _M0L6_2atmpS2588;
      float _M0L6_2atmpS2587;
      float _M0L6_2atmpS2586;
      float _M0L6_2atmpS2585;
      float _M0L6_2atmpS2583;
      struct _M0TPB5ArrayGiE* _M0L4tabsS2592;
      int32_t _M0L6_2atmpS2591;
      struct _M0TPB5ArrayGfE* _M0L4v__sS2711;
      float _M0L6_2atmpS2706;
      struct _M0TPB5ArrayGfE* _M0L2dvS2709;
      int32_t _M0L6_2atmpS2710;
      float _M0L6_2atmpS2708;
      float _M0L6_2atmpS2707;
      float _M0L10v__s__predS968;
      struct _M0TPB5ArrayGbE* _M0L4fireS2630;
      int32_t _M0L6_2atmpS2631;
      struct _M0TPB5ArrayGbE* _M0L4fireS2632;
      struct _M0TPB5ArrayGfE* _M0L4v__sS2648;
      struct _M0TPB5ArrayGfE* _M0L4v__sS2660;
      float _M0L6_2atmpS2650;
      float _M0L6_2atmpS2652;
      struct _M0TPB5ArrayGfE* _M0L2dvS2658;
      int32_t _M0L6_2atmpS2659;
      float _M0L6_2atmpS2654;
      struct _M0TPB5ArrayGfE* _M0L8dv__tempS2656;
      int32_t _M0L6_2atmpS2657;
      float _M0L6_2atmpS2655;
      float _M0L6_2atmpS2653;
      float _M0L6_2atmpS2651;
      float _M0L6_2atmpS2649;
      struct _M0TPB5ArrayGfE* _M0L5v__d1S2661;
      struct _M0TPB5ArrayGfE* _M0L5v__d1S2675;
      float _M0L6_2atmpS2663;
      float _M0L6_2atmpS2665;
      struct _M0TPB5ArrayGfE* _M0L2dvS2672;
      int32_t _M0L6_2atmpS2674;
      int32_t _M0L6_2atmpS2673;
      float _M0L6_2atmpS2667;
      struct _M0TPB5ArrayGfE* _M0L8dv__tempS2669;
      int32_t _M0L6_2atmpS2671;
      int32_t _M0L6_2atmpS2670;
      float _M0L6_2atmpS2668;
      float _M0L6_2atmpS2666;
      float _M0L6_2atmpS2664;
      float _M0L6_2atmpS2662;
      struct _M0TPB5ArrayGfE* _M0L5v__d2S2676;
      struct _M0TPB5ArrayGfE* _M0L5v__d2S2690;
      float _M0L6_2atmpS2678;
      float _M0L6_2atmpS2680;
      struct _M0TPB5ArrayGfE* _M0L2dvS2687;
      int32_t _M0L6_2atmpS2689;
      int32_t _M0L6_2atmpS2688;
      float _M0L6_2atmpS2682;
      struct _M0TPB5ArrayGfE* _M0L8dv__tempS2684;
      int32_t _M0L6_2atmpS2686;
      int32_t _M0L6_2atmpS2685;
      float _M0L6_2atmpS2683;
      float _M0L6_2atmpS2681;
      float _M0L6_2atmpS2679;
      float _M0L6_2atmpS2677;
      struct _M0TPB5ArrayGfE* _M0L4w__sS2691;
      struct _M0TPB5ArrayGfE* _M0L4w__sS2705;
      float _M0L6_2atmpS2693;
      float _M0L6_2atmpS2695;
      struct _M0TPB5ArrayGfE* _M0L2dvS2702;
      int32_t _M0L6_2atmpS2704;
      int32_t _M0L6_2atmpS2703;
      float _M0L6_2atmpS2697;
      struct _M0TPB5ArrayGfE* _M0L8dv__tempS2699;
      int32_t _M0L6_2atmpS2701;
      int32_t _M0L6_2atmpS2700;
      float _M0L6_2atmpS2698;
      float _M0L6_2atmpS2696;
      float _M0L6_2atmpS2694;
      float _M0L6_2atmpS2692;
      int32_t _M0L6_2atmpS2577;
      #line 319 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS2580
      = _M0MPC15array5Array2atGiE(_M0L4tabsS2581, _M0L1kS965);
      _M0L6_2atmpS2579 = _M0L6_2atmpS2580 - 1;
      #line 319 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0MPC15array5Array3setGiE(_M0L4tabsS2578, _M0L1kS965, _M0L6_2atmpS2579);
      _M0L9thresholdS2582 = _M0L1pS948->$31;
      _M0L9thresholdS2590 = _M0L1pS948->$31;
      #line 320 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS2584
      = _M0MPC15array5Array2atGfE(_M0L9thresholdS2590, _M0L1kS965);
      _M0L9thresholdS2589 = _M0L1pS948->$31;
      #line 320 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS2588
      = _M0MPC15array5Array2atGfE(_M0L9thresholdS2589, _M0L1kS965);
      _M0L6_2atmpS2587 = _M0L2vtS950 - _M0L6_2atmpS2588;
      _M0L6_2atmpS2586 = _M0L2dtS959 * _M0L6_2atmpS2587;
      _M0L6_2atmpS2585 = _M0L6_2atmpS2586 / _M0L6tau__aS954;
      _M0L6_2atmpS2583 = _M0L6_2atmpS2584 + _M0L6_2atmpS2585;
      #line 320 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0MPC15array5Array3setGfE(_M0L9thresholdS2582, _M0L1kS965, _M0L6_2atmpS2583);
      _M0L4tabsS2592 = _M0L1pS948->$32;
      #line 322 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS2591
      = _M0MPC15array5Array2atGiE(_M0L4tabsS2592, _M0L1kS965);
      if (_M0L6_2atmpS2591 > 0) {
        struct _M0TPB5ArrayGfE* _M0L4v__sS2593 = _M0L1pS948->$26;
        struct _M0TPB5ArrayGfE* _M0L5v__d1S2594;
        struct _M0TPB5ArrayGfE* _M0L5v__d1S2611;
        float _M0L6_2atmpS2596;
        struct _M0TPB5ArrayGfE* _M0L4v__sS2610;
        float _M0L6_2atmpS2607;
        struct _M0TPB5ArrayGfE* _M0L5v__d1S2609;
        float _M0L6_2atmpS2608;
        float _M0L6_2atmpS2606;
        float _M0L6_2atmpS2602;
        struct _M0TP26RiantR8snn__mbt8Dendrite* _M0L2d1S2605;
        struct _M0TPB5ArrayGfE* _M0L3gaxS2604;
        float _M0L6_2atmpS2603;
        float _M0L6_2atmpS2598;
        struct _M0TP26RiantR8snn__mbt8Dendrite* _M0L2d1S2601;
        struct _M0TPB5ArrayGfE* _M0L1cS2600;
        float _M0L6_2atmpS2599;
        float _M0L6_2atmpS2597;
        float _M0L6_2atmpS2595;
        struct _M0TPB5ArrayGfE* _M0L5v__d2S2612;
        struct _M0TPB5ArrayGfE* _M0L5v__d2S2629;
        float _M0L6_2atmpS2614;
        struct _M0TPB5ArrayGfE* _M0L4v__sS2628;
        float _M0L6_2atmpS2625;
        struct _M0TPB5ArrayGfE* _M0L5v__d2S2627;
        float _M0L6_2atmpS2626;
        float _M0L6_2atmpS2624;
        float _M0L6_2atmpS2620;
        struct _M0TP26RiantR8snn__mbt8Dendrite* _M0L2d2S2623;
        struct _M0TPB5ArrayGfE* _M0L3gaxS2622;
        float _M0L6_2atmpS2621;
        float _M0L6_2atmpS2616;
        struct _M0TP26RiantR8snn__mbt8Dendrite* _M0L2d2S2619;
        struct _M0TPB5ArrayGfE* _M0L1cS2618;
        float _M0L6_2atmpS2617;
        float _M0L6_2atmpS2615;
        float _M0L6_2atmpS2613;
        #line 324 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
        _M0MPC15array5Array3setGfE(_M0L4v__sS2593, _M0L1kS965, _M0L2vrS951);
        _M0L5v__d1S2594 = _M0L1pS948->$28;
        _M0L5v__d1S2611 = _M0L1pS948->$28;
        #line 325 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
        _M0L6_2atmpS2596
        = _M0MPC15array5Array2atGfE(_M0L5v__d1S2611, _M0L1kS965);
        _M0L4v__sS2610 = _M0L1pS948->$26;
        #line 325 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
        _M0L6_2atmpS2607
        = _M0MPC15array5Array2atGfE(_M0L4v__sS2610, _M0L1kS965);
        _M0L5v__d1S2609 = _M0L1pS948->$28;
        #line 325 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
        _M0L6_2atmpS2608
        = _M0MPC15array5Array2atGfE(_M0L5v__d1S2609, _M0L1kS965);
        _M0L6_2atmpS2606 = _M0L6_2atmpS2607 - _M0L6_2atmpS2608;
        _M0L6_2atmpS2602 = _M0L2dtS959 * _M0L6_2atmpS2606;
        _M0L2d1S2605 = _M0L1pS948->$2;
        _M0L3gaxS2604 = _M0L2d1S2605->$3;
        #line 325 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
        _M0L6_2atmpS2603
        = _M0MPC15array5Array2atGfE(_M0L3gaxS2604, _M0L1kS965);
        _M0L6_2atmpS2598 = _M0L6_2atmpS2602 * _M0L6_2atmpS2603;
        _M0L2d1S2601 = _M0L1pS948->$2;
        _M0L1cS2600 = _M0L2d1S2601->$2;
        #line 325 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
        _M0L6_2atmpS2599 = _M0MPC15array5Array2atGfE(_M0L1cS2600, _M0L1kS965);
        _M0L6_2atmpS2597 = _M0L6_2atmpS2598 / _M0L6_2atmpS2599;
        _M0L6_2atmpS2595 = _M0L6_2atmpS2596 + _M0L6_2atmpS2597;
        #line 325 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
        _M0MPC15array5Array3setGfE(_M0L5v__d1S2594, _M0L1kS965, _M0L6_2atmpS2595);
        _M0L5v__d2S2612 = _M0L1pS948->$29;
        _M0L5v__d2S2629 = _M0L1pS948->$29;
        #line 326 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
        _M0L6_2atmpS2614
        = _M0MPC15array5Array2atGfE(_M0L5v__d2S2629, _M0L1kS965);
        _M0L4v__sS2628 = _M0L1pS948->$26;
        #line 326 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
        _M0L6_2atmpS2625
        = _M0MPC15array5Array2atGfE(_M0L4v__sS2628, _M0L1kS965);
        _M0L5v__d2S2627 = _M0L1pS948->$29;
        #line 326 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
        _M0L6_2atmpS2626
        = _M0MPC15array5Array2atGfE(_M0L5v__d2S2627, _M0L1kS965);
        _M0L6_2atmpS2624 = _M0L6_2atmpS2625 - _M0L6_2atmpS2626;
        _M0L6_2atmpS2620 = _M0L2dtS959 * _M0L6_2atmpS2624;
        _M0L2d2S2623 = _M0L1pS948->$3;
        _M0L3gaxS2622 = _M0L2d2S2623->$3;
        #line 326 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
        _M0L6_2atmpS2621
        = _M0MPC15array5Array2atGfE(_M0L3gaxS2622, _M0L1kS965);
        _M0L6_2atmpS2616 = _M0L6_2atmpS2620 * _M0L6_2atmpS2621;
        _M0L2d2S2619 = _M0L1pS948->$3;
        _M0L1cS2618 = _M0L2d2S2619->$2;
        #line 326 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
        _M0L6_2atmpS2617 = _M0MPC15array5Array2atGfE(_M0L1cS2618, _M0L1kS965);
        _M0L6_2atmpS2615 = _M0L6_2atmpS2616 / _M0L6_2atmpS2617;
        _M0L6_2atmpS2613 = _M0L6_2atmpS2614 + _M0L6_2atmpS2615;
        #line 326 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
        _M0MPC15array5Array3setGfE(_M0L5v__d2S2612, _M0L1kS965, _M0L6_2atmpS2613);
        goto join_966;
      }
      _M0L4v__sS2711 = _M0L1pS948->$26;
      #line 331 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS2706
      = _M0MPC15array5Array2atGfE(_M0L4v__sS2711, _M0L1kS965);
      _M0L2dvS2709 = _M0L1pS948->$33;
      _M0L6_2atmpS2710 = _M0L1kS965 * 4;
      #line 331 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS2708
      = _M0MPC15array5Array2atGfE(_M0L2dvS2709, _M0L6_2atmpS2710);
      _M0L6_2atmpS2707 = _M0L6_2atmpS2708 * _M0L2dtS959;
      _M0L10v__s__predS968 = _M0L6_2atmpS2706 + _M0L6_2atmpS2707;
      _M0L4fireS2630 = _M0L1pS948->$30;
      _M0L6_2atmpS2631 = _M0L10v__s__predS968 >= -0x1.4p+3f;
      #line 332 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0MPC15array5Array3setGbE(_M0L4fireS2630, _M0L1kS965, _M0L6_2atmpS2631);
      _M0L4fireS2632 = _M0L1pS948->$30;
      #line 334 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS2632, _M0L1kS965)) {
        struct _M0TPB5ArrayGfE* _M0L2dvS2633 = _M0L1pS948->$33;
        int32_t _M0L6_2atmpS2634 = _M0L1kS965 * 4;
        struct _M0TPB5ArrayGfE* _M0L4v__sS2637 = _M0L1pS948->$26;
        float _M0L6_2atmpS2636;
        float _M0L6_2atmpS2635;
        struct _M0TPB5ArrayGfE* _M0L4v__sS2638;
        struct _M0TPB5ArrayGfE* _M0L4w__sS2639;
        struct _M0TPB5ArrayGfE* _M0L4w__sS2642;
        float _M0L6_2atmpS2641;
        float _M0L6_2atmpS2640;
        struct _M0TPB5ArrayGfE* _M0L9thresholdS2643;
        struct _M0TPB5ArrayGfE* _M0L9thresholdS2646;
        float _M0L6_2atmpS2645;
        float _M0L6_2atmpS2644;
        struct _M0TPB5ArrayGiE* _M0L4tabsS2647;
        #line 335 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
        _M0L6_2atmpS2636
        = _M0MPC15array5Array2atGfE(_M0L4v__sS2637, _M0L1kS965);
        _M0L6_2atmpS2635 = _M0L12ap__membraneS957 - _M0L6_2atmpS2636;
        #line 335 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
        _M0MPC15array5Array3setGfE(_M0L2dvS2633, _M0L6_2atmpS2634, _M0L6_2atmpS2635);
        _M0L4v__sS2638 = _M0L1pS948->$26;
        #line 336 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
        _M0MPC15array5Array3setGfE(_M0L4v__sS2638, _M0L1kS965, _M0L12ap__membraneS957);
        _M0L4w__sS2639 = _M0L1pS948->$27;
        _M0L4w__sS2642 = _M0L1pS948->$27;
        #line 337 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
        _M0L6_2atmpS2641
        = _M0MPC15array5Array2atGfE(_M0L4w__sS2642, _M0L1kS965);
        _M0L6_2atmpS2640 = _M0L6_2atmpS2641 + _M0L1bS952;
        #line 337 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
        _M0MPC15array5Array3setGfE(_M0L4w__sS2639, _M0L1kS965, _M0L6_2atmpS2640);
        _M0L9thresholdS2643 = _M0L1pS948->$31;
        _M0L9thresholdS2646 = _M0L1pS948->$31;
        #line 338 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
        _M0L6_2atmpS2645
        = _M0MPC15array5Array2atGfE(_M0L9thresholdS2646, _M0L1kS965);
        _M0L6_2atmpS2644 = _M0L6_2atmpS2645 + _M0L2atS953;
        #line 338 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
        _M0MPC15array5Array3setGfE(_M0L9thresholdS2643, _M0L1kS965, _M0L6_2atmpS2644);
        _M0L4tabsS2647 = _M0L1pS948->$32;
        #line 339 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
        _M0MPC15array5Array3setGiE(_M0L4tabsS2647, _M0L1kS965, _M0L11tabs__stepsS958);
        goto join_966;
      }
      _M0L4v__sS2648 = _M0L1pS948->$26;
      _M0L4v__sS2660 = _M0L1pS948->$26;
      #line 345 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS2650
      = _M0MPC15array5Array2atGfE(_M0L4v__sS2660, _M0L1kS965);
      _M0L6_2atmpS2652 = 0x1p-1f * _M0L2dtS959;
      _M0L2dvS2658 = _M0L1pS948->$33;
      _M0L6_2atmpS2659 = _M0L1kS965 * 4;
      #line 345 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS2654
      = _M0MPC15array5Array2atGfE(_M0L2dvS2658, _M0L6_2atmpS2659);
      _M0L8dv__tempS2656 = _M0L1pS948->$34;
      _M0L6_2atmpS2657 = _M0L1kS965 * 4;
      #line 345 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS2655
      = _M0MPC15array5Array2atGfE(_M0L8dv__tempS2656, _M0L6_2atmpS2657);
      _M0L6_2atmpS2653 = _M0L6_2atmpS2654 + _M0L6_2atmpS2655;
      _M0L6_2atmpS2651 = _M0L6_2atmpS2652 * _M0L6_2atmpS2653;
      _M0L6_2atmpS2649 = _M0L6_2atmpS2650 + _M0L6_2atmpS2651;
      #line 345 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0MPC15array5Array3setGfE(_M0L4v__sS2648, _M0L1kS965, _M0L6_2atmpS2649);
      _M0L5v__d1S2661 = _M0L1pS948->$28;
      _M0L5v__d1S2675 = _M0L1pS948->$28;
      #line 346 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS2663
      = _M0MPC15array5Array2atGfE(_M0L5v__d1S2675, _M0L1kS965);
      _M0L6_2atmpS2665 = 0x1p-1f * _M0L2dtS959;
      _M0L2dvS2672 = _M0L1pS948->$33;
      _M0L6_2atmpS2674 = _M0L1kS965 * 4;
      _M0L6_2atmpS2673 = _M0L6_2atmpS2674 + 1;
      #line 346 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS2667
      = _M0MPC15array5Array2atGfE(_M0L2dvS2672, _M0L6_2atmpS2673);
      _M0L8dv__tempS2669 = _M0L1pS948->$34;
      _M0L6_2atmpS2671 = _M0L1kS965 * 4;
      _M0L6_2atmpS2670 = _M0L6_2atmpS2671 + 1;
      #line 346 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS2668
      = _M0MPC15array5Array2atGfE(_M0L8dv__tempS2669, _M0L6_2atmpS2670);
      _M0L6_2atmpS2666 = _M0L6_2atmpS2667 + _M0L6_2atmpS2668;
      _M0L6_2atmpS2664 = _M0L6_2atmpS2665 * _M0L6_2atmpS2666;
      _M0L6_2atmpS2662 = _M0L6_2atmpS2663 + _M0L6_2atmpS2664;
      #line 346 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0MPC15array5Array3setGfE(_M0L5v__d1S2661, _M0L1kS965, _M0L6_2atmpS2662);
      _M0L5v__d2S2676 = _M0L1pS948->$29;
      _M0L5v__d2S2690 = _M0L1pS948->$29;
      #line 347 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS2678
      = _M0MPC15array5Array2atGfE(_M0L5v__d2S2690, _M0L1kS965);
      _M0L6_2atmpS2680 = 0x1p-1f * _M0L2dtS959;
      _M0L2dvS2687 = _M0L1pS948->$33;
      _M0L6_2atmpS2689 = _M0L1kS965 * 4;
      _M0L6_2atmpS2688 = _M0L6_2atmpS2689 + 2;
      #line 347 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS2682
      = _M0MPC15array5Array2atGfE(_M0L2dvS2687, _M0L6_2atmpS2688);
      _M0L8dv__tempS2684 = _M0L1pS948->$34;
      _M0L6_2atmpS2686 = _M0L1kS965 * 4;
      _M0L6_2atmpS2685 = _M0L6_2atmpS2686 + 2;
      #line 347 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS2683
      = _M0MPC15array5Array2atGfE(_M0L8dv__tempS2684, _M0L6_2atmpS2685);
      _M0L6_2atmpS2681 = _M0L6_2atmpS2682 + _M0L6_2atmpS2683;
      _M0L6_2atmpS2679 = _M0L6_2atmpS2680 * _M0L6_2atmpS2681;
      _M0L6_2atmpS2677 = _M0L6_2atmpS2678 + _M0L6_2atmpS2679;
      #line 347 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0MPC15array5Array3setGfE(_M0L5v__d2S2676, _M0L1kS965, _M0L6_2atmpS2677);
      _M0L4w__sS2691 = _M0L1pS948->$27;
      _M0L4w__sS2705 = _M0L1pS948->$27;
      #line 348 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS2693
      = _M0MPC15array5Array2atGfE(_M0L4w__sS2705, _M0L1kS965);
      _M0L6_2atmpS2695 = 0x1p-1f * _M0L2dtS959;
      _M0L2dvS2702 = _M0L1pS948->$33;
      _M0L6_2atmpS2704 = _M0L1kS965 * 4;
      _M0L6_2atmpS2703 = _M0L6_2atmpS2704 + 3;
      #line 348 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS2697
      = _M0MPC15array5Array2atGfE(_M0L2dvS2702, _M0L6_2atmpS2703);
      _M0L8dv__tempS2699 = _M0L1pS948->$34;
      _M0L6_2atmpS2701 = _M0L1kS965 * 4;
      _M0L6_2atmpS2700 = _M0L6_2atmpS2701 + 3;
      #line 348 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS2698
      = _M0MPC15array5Array2atGfE(_M0L8dv__tempS2699, _M0L6_2atmpS2700);
      _M0L6_2atmpS2696 = _M0L6_2atmpS2697 + _M0L6_2atmpS2698;
      _M0L6_2atmpS2694 = _M0L6_2atmpS2695 * _M0L6_2atmpS2696;
      _M0L6_2atmpS2692 = _M0L6_2atmpS2693 + _M0L6_2atmpS2694;
      #line 348 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0MPC15array5Array3setGfE(_M0L4w__sS2691, _M0L1kS965, _M0L6_2atmpS2692);
      goto join_966;
      goto joinlet_3065;
      join_966:;
      _M0L6_2atmpS2577 = _M0L1kS965 + 1;
      _M0L1kS965 = _M0L6_2atmpS2577;
      continue;
      joinlet_3065:;
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt18tripod__heun__step(
  struct _M0TP26RiantR8snn__mbt6Tripod* _M0L1pS924,
  float _M0L2dtS935,
  int32_t _M0L11store__tempS934
) {
  int32_t _M0L1nS923;
  struct _M0TP26RiantR8snn__mbt13AdExParameter* _M0L3p__S925;
  float _M0L1cS926;
  float _M0L2glS927;
  float _M0L2elS928;
  float _M0L9dt__slopeS929;
  float _M0L2twS930;
  float _M0L1aS931;
  struct _M0TPB8MutLocalGiE* _M0L1kS932;
  #line 209 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
  _M0L1nS923 = _M0L1pS924->$25;
  _M0L3p__S925 = _M0L1pS924->$0;
  _M0L1cS926 = _M0L3p__S925->$0;
  _M0L2glS927 = _M0L3p__S925->$1;
  _M0L2elS928 = _M0L3p__S925->$4;
  _M0L9dt__slopeS929 = _M0L3p__S925->$7;
  _M0L2twS930 = _M0L3p__S925->$8;
  _M0L1aS931 = _M0L3p__S925->$9;
  _M0L1kS932
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1kS932)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1kS932->$0 = 0;
  while (1) {
    int32_t _M0L3valS2405 = _M0L1kS932->$0;
    if (_M0L3valS2405 < _M0L1nS923) {
      float _M0L2dsS933;
      float _M0L3dd1S936;
      float _M0L3dd2S937;
      float _M0L2dwS938;
      struct _M0TPB5ArrayGfE* _M0L5v__d1S2553;
      int32_t _M0L3valS2554;
      float _M0L6_2atmpS2552;
      float _M0L6_2atmpS2548;
      struct _M0TPB5ArrayGfE* _M0L4v__sS2550;
      int32_t _M0L3valS2551;
      float _M0L6_2atmpS2549;
      float _M0L6_2atmpS2547;
      float _M0L6_2atmpS2546;
      float _M0L6_2atmpS2541;
      struct _M0TP26RiantR8snn__mbt8Dendrite* _M0L2d1S2545;
      struct _M0TPB5ArrayGfE* _M0L3gaxS2543;
      int32_t _M0L3valS2544;
      float _M0L6_2atmpS2542;
      float _M0L3ic1S939;
      struct _M0TPB5ArrayGfE* _M0L5v__d2S2539;
      int32_t _M0L3valS2540;
      float _M0L6_2atmpS2538;
      float _M0L6_2atmpS2534;
      struct _M0TPB5ArrayGfE* _M0L4v__sS2536;
      int32_t _M0L3valS2537;
      float _M0L6_2atmpS2535;
      float _M0L6_2atmpS2533;
      float _M0L6_2atmpS2532;
      float _M0L6_2atmpS2527;
      struct _M0TP26RiantR8snn__mbt8Dendrite* _M0L2d2S2531;
      struct _M0TPB5ArrayGfE* _M0L3gaxS2529;
      int32_t _M0L3valS2530;
      float _M0L6_2atmpS2528;
      float _M0L3ic2S940;
      float _M0L9exp__termS941;
      struct _M0TPB5ArrayGfE* _M0L4v__sS2515;
      int32_t _M0L3valS2516;
      float _M0L6_2atmpS2514;
      float _M0L6_2atmpS2513;
      float _M0L6_2atmpS2512;
      float _M0L6_2atmpS2511;
      float _M0L6_2atmpS2507;
      struct _M0TPB5ArrayGfE* _M0L4w__sS2509;
      int32_t _M0L3valS2510;
      float _M0L6_2atmpS2508;
      float _M0L6_2atmpS2506;
      float _M0L6_2atmpS2502;
      struct _M0TPB5ArrayGfE* _M0L12syn__curr__sS2504;
      int32_t _M0L3valS2505;
      float _M0L6_2atmpS2503;
      float _M0L6_2atmpS2500;
      float _M0L6_2atmpS2501;
      float _M0L6_2atmpS2496;
      struct _M0TPB5ArrayGfE* _M0L4i__sS2498;
      int32_t _M0L3valS2499;
      float _M0L6_2atmpS2497;
      float _M0L6_2atmpS2495;
      float _M0L10dv__s__valS942;
      struct _M0TPB5ArrayGfE* _M0L5v__d1S2493;
      int32_t _M0L3valS2494;
      float _M0L6_2atmpS2492;
      float _M0L6_2atmpS2491;
      float _M0L6_2atmpS2486;
      struct _M0TP26RiantR8snn__mbt8Dendrite* _M0L2d1S2490;
      struct _M0TPB5ArrayGfE* _M0L2gmS2488;
      int32_t _M0L3valS2489;
      float _M0L6_2atmpS2487;
      float _M0L6_2atmpS2482;
      struct _M0TPB5ArrayGfE* _M0L13syn__curr__d1S2484;
      int32_t _M0L3valS2485;
      float _M0L6_2atmpS2483;
      float _M0L6_2atmpS2481;
      float _M0L6_2atmpS2477;
      struct _M0TPB5ArrayGfE* _M0L5i__d1S2479;
      int32_t _M0L3valS2480;
      float _M0L6_2atmpS2478;
      float _M0L6_2atmpS2472;
      struct _M0TP26RiantR8snn__mbt8Dendrite* _M0L2d1S2476;
      struct _M0TPB5ArrayGfE* _M0L1cS2474;
      int32_t _M0L3valS2475;
      float _M0L6_2atmpS2473;
      float _M0L11dv__d1__valS943;
      struct _M0TPB5ArrayGfE* _M0L5v__d2S2470;
      int32_t _M0L3valS2471;
      float _M0L6_2atmpS2469;
      float _M0L6_2atmpS2468;
      float _M0L6_2atmpS2463;
      struct _M0TP26RiantR8snn__mbt8Dendrite* _M0L2d2S2467;
      struct _M0TPB5ArrayGfE* _M0L2gmS2465;
      int32_t _M0L3valS2466;
      float _M0L6_2atmpS2464;
      float _M0L6_2atmpS2459;
      struct _M0TPB5ArrayGfE* _M0L13syn__curr__d2S2461;
      int32_t _M0L3valS2462;
      float _M0L6_2atmpS2460;
      float _M0L6_2atmpS2458;
      float _M0L6_2atmpS2454;
      struct _M0TPB5ArrayGfE* _M0L5i__d2S2456;
      int32_t _M0L3valS2457;
      float _M0L6_2atmpS2455;
      float _M0L6_2atmpS2449;
      struct _M0TP26RiantR8snn__mbt8Dendrite* _M0L2d2S2453;
      struct _M0TPB5ArrayGfE* _M0L1cS2451;
      int32_t _M0L3valS2452;
      float _M0L6_2atmpS2450;
      float _M0L11dv__d2__valS944;
      struct _M0TPB5ArrayGfE* _M0L4v__sS2447;
      int32_t _M0L3valS2448;
      float _M0L6_2atmpS2446;
      float _M0L6_2atmpS2445;
      float _M0L6_2atmpS2444;
      float _M0L6_2atmpS2439;
      struct _M0TPB5ArrayGfE* _M0L4w__sS2442;
      int32_t _M0L3valS2443;
      float _M0L6_2atmpS2441;
      float _M0L6_2atmpS2440;
      float _M0L6_2atmpS2438;
      float _M0L7dw__valS945;
      int32_t _M0L3valS2437;
      int32_t _M0L6_2atmpS2436;
      if (_M0L11store__tempS934) {
        struct _M0TPB5ArrayGfE* _M0L8dv__tempS2570 = _M0L1pS924->$34;
        int32_t _M0L3valS2572 = _M0L1kS932->$0;
        int32_t _M0L6_2atmpS2571 = _M0L3valS2572 * 4;
        float _M0L6_2atmpS2569;
        #line 223 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
        _M0L6_2atmpS2569
        = _M0MPC15array5Array2atGfE(_M0L8dv__tempS2570, _M0L6_2atmpS2571);
        _M0L2dsS933 = _M0L6_2atmpS2569 * _M0L2dtS935;
      } else {
        _M0L2dsS933 = 0x0p+0f;
      }
      if (_M0L11store__tempS934) {
        struct _M0TPB5ArrayGfE* _M0L8dv__tempS2565 = _M0L1pS924->$34;
        int32_t _M0L3valS2568 = _M0L1kS932->$0;
        int32_t _M0L6_2atmpS2567 = _M0L3valS2568 * 4;
        int32_t _M0L6_2atmpS2566 = _M0L6_2atmpS2567 + 1;
        float _M0L6_2atmpS2564;
        #line 224 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
        _M0L6_2atmpS2564
        = _M0MPC15array5Array2atGfE(_M0L8dv__tempS2565, _M0L6_2atmpS2566);
        _M0L3dd1S936 = _M0L6_2atmpS2564 * _M0L2dtS935;
      } else {
        _M0L3dd1S936 = 0x0p+0f;
      }
      if (_M0L11store__tempS934) {
        struct _M0TPB5ArrayGfE* _M0L8dv__tempS2560 = _M0L1pS924->$34;
        int32_t _M0L3valS2563 = _M0L1kS932->$0;
        int32_t _M0L6_2atmpS2562 = _M0L3valS2563 * 4;
        int32_t _M0L6_2atmpS2561 = _M0L6_2atmpS2562 + 2;
        float _M0L6_2atmpS2559;
        #line 225 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
        _M0L6_2atmpS2559
        = _M0MPC15array5Array2atGfE(_M0L8dv__tempS2560, _M0L6_2atmpS2561);
        _M0L3dd2S937 = _M0L6_2atmpS2559 * _M0L2dtS935;
      } else {
        _M0L3dd2S937 = 0x0p+0f;
      }
      if (_M0L11store__tempS934) {
        struct _M0TPB5ArrayGfE* _M0L8dv__tempS2555 = _M0L1pS924->$34;
        int32_t _M0L3valS2558 = _M0L1kS932->$0;
        int32_t _M0L6_2atmpS2557 = _M0L3valS2558 * 4;
        int32_t _M0L6_2atmpS2556 = _M0L6_2atmpS2557 + 3;
        #line 226 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
        _M0L2dwS938
        = _M0MPC15array5Array2atGfE(_M0L8dv__tempS2555, _M0L6_2atmpS2556);
      } else {
        _M0L2dwS938 = 0x0p+0f;
      }
      _M0L5v__d1S2553 = _M0L1pS924->$28;
      _M0L3valS2554 = _M0L1kS932->$0;
      #line 230 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS2552
      = _M0MPC15array5Array2atGfE(_M0L5v__d1S2553, _M0L3valS2554);
      _M0L6_2atmpS2548 = _M0L6_2atmpS2552 + _M0L3dd1S936;
      _M0L4v__sS2550 = _M0L1pS924->$26;
      _M0L3valS2551 = _M0L1kS932->$0;
      #line 230 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS2549
      = _M0MPC15array5Array2atGfE(_M0L4v__sS2550, _M0L3valS2551);
      _M0L6_2atmpS2547 = _M0L6_2atmpS2548 - _M0L6_2atmpS2549;
      _M0L6_2atmpS2546 = _M0L6_2atmpS2547 - _M0L2dsS933;
      _M0L6_2atmpS2541 = -_M0L6_2atmpS2546;
      _M0L2d1S2545 = _M0L1pS924->$2;
      _M0L3gaxS2543 = _M0L2d1S2545->$3;
      _M0L3valS2544 = _M0L1kS932->$0;
      #line 230 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS2542
      = _M0MPC15array5Array2atGfE(_M0L3gaxS2543, _M0L3valS2544);
      _M0L3ic1S939 = _M0L6_2atmpS2541 * _M0L6_2atmpS2542;
      _M0L5v__d2S2539 = _M0L1pS924->$29;
      _M0L3valS2540 = _M0L1kS932->$0;
      #line 231 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS2538
      = _M0MPC15array5Array2atGfE(_M0L5v__d2S2539, _M0L3valS2540);
      _M0L6_2atmpS2534 = _M0L6_2atmpS2538 + _M0L3dd2S937;
      _M0L4v__sS2536 = _M0L1pS924->$26;
      _M0L3valS2537 = _M0L1kS932->$0;
      #line 231 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS2535
      = _M0MPC15array5Array2atGfE(_M0L4v__sS2536, _M0L3valS2537);
      _M0L6_2atmpS2533 = _M0L6_2atmpS2534 - _M0L6_2atmpS2535;
      _M0L6_2atmpS2532 = _M0L6_2atmpS2533 - _M0L2dsS933;
      _M0L6_2atmpS2527 = -_M0L6_2atmpS2532;
      _M0L2d2S2531 = _M0L1pS924->$3;
      _M0L3gaxS2529 = _M0L2d2S2531->$3;
      _M0L3valS2530 = _M0L1kS932->$0;
      #line 231 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS2528
      = _M0MPC15array5Array2atGfE(_M0L3gaxS2529, _M0L3valS2530);
      _M0L3ic2S940 = _M0L6_2atmpS2527 * _M0L6_2atmpS2528;
      if (_M0L9dt__slopeS929 < 0x0p+0f) {
        _M0L9exp__termS941 = 0x0p+0f;
      } else {
        struct _M0TPB5ArrayGfE* _M0L4v__sS2525 = _M0L1pS924->$26;
        int32_t _M0L3valS2526 = _M0L1kS932->$0;
        float _M0L6_2atmpS2524;
        float _M0L6_2atmpS2520;
        struct _M0TPB5ArrayGfE* _M0L9thresholdS2522;
        int32_t _M0L3valS2523;
        float _M0L6_2atmpS2521;
        float _M0L6_2atmpS2519;
        float _M0L6_2atmpS2518;
        float _M0L6_2atmpS2517;
        #line 237 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
        _M0L6_2atmpS2524
        = _M0MPC15array5Array2atGfE(_M0L4v__sS2525, _M0L3valS2526);
        _M0L6_2atmpS2520 = _M0L6_2atmpS2524 + _M0L2dsS933;
        _M0L9thresholdS2522 = _M0L1pS924->$31;
        _M0L3valS2523 = _M0L1kS932->$0;
        #line 237 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
        _M0L6_2atmpS2521
        = _M0MPC15array5Array2atGfE(_M0L9thresholdS2522, _M0L3valS2523);
        _M0L6_2atmpS2519 = _M0L6_2atmpS2520 - _M0L6_2atmpS2521;
        _M0L6_2atmpS2518 = _M0L6_2atmpS2519 / _M0L9dt__slopeS929;
        #line 237 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
        _M0L6_2atmpS2517 = _M0FP26RiantR8snn__mbt4expf(_M0L6_2atmpS2518);
        _M0L9exp__termS941 = _M0L9dt__slopeS929 * _M0L6_2atmpS2517;
      }
      _M0L4v__sS2515 = _M0L1pS924->$26;
      _M0L3valS2516 = _M0L1kS932->$0;
      #line 239 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS2514
      = _M0MPC15array5Array2atGfE(_M0L4v__sS2515, _M0L3valS2516);
      _M0L6_2atmpS2513 = _M0L2elS928 - _M0L6_2atmpS2514;
      _M0L6_2atmpS2512 = _M0L6_2atmpS2513 - _M0L2dsS933;
      _M0L6_2atmpS2511 = _M0L2glS927 * _M0L6_2atmpS2512;
      _M0L6_2atmpS2507 = _M0L6_2atmpS2511 + _M0L9exp__termS941;
      _M0L4w__sS2509 = _M0L1pS924->$27;
      _M0L3valS2510 = _M0L1kS932->$0;
      #line 240 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS2508
      = _M0MPC15array5Array2atGfE(_M0L4w__sS2509, _M0L3valS2510);
      _M0L6_2atmpS2506 = _M0L6_2atmpS2507 - _M0L6_2atmpS2508;
      _M0L6_2atmpS2502 = _M0L6_2atmpS2506 - _M0L2dwS938;
      _M0L12syn__curr__sS2504 = _M0L1pS924->$35;
      _M0L3valS2505 = _M0L1kS932->$0;
      #line 240 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS2503
      = _M0MPC15array5Array2atGfE(_M0L12syn__curr__sS2504, _M0L3valS2505);
      _M0L6_2atmpS2500 = _M0L6_2atmpS2502 - _M0L6_2atmpS2503;
      _M0L6_2atmpS2501 = _M0L3ic1S939 + _M0L3ic2S940;
      _M0L6_2atmpS2496 = _M0L6_2atmpS2500 - _M0L6_2atmpS2501;
      _M0L4i__sS2498 = _M0L1pS924->$4;
      _M0L3valS2499 = _M0L1kS932->$0;
      #line 241 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS2497
      = _M0MPC15array5Array2atGfE(_M0L4i__sS2498, _M0L3valS2499);
      _M0L6_2atmpS2495 = _M0L6_2atmpS2496 + _M0L6_2atmpS2497;
      _M0L10dv__s__valS942 = _M0L6_2atmpS2495 / _M0L1cS926;
      _M0L5v__d1S2493 = _M0L1pS924->$28;
      _M0L3valS2494 = _M0L1kS932->$0;
      #line 244 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS2492
      = _M0MPC15array5Array2atGfE(_M0L5v__d1S2493, _M0L3valS2494);
      _M0L6_2atmpS2491 = _M0L2elS928 - _M0L6_2atmpS2492;
      _M0L6_2atmpS2486 = _M0L6_2atmpS2491 - _M0L3dd1S936;
      _M0L2d1S2490 = _M0L1pS924->$2;
      _M0L2gmS2488 = _M0L2d1S2490->$4;
      _M0L3valS2489 = _M0L1kS932->$0;
      #line 244 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS2487
      = _M0MPC15array5Array2atGfE(_M0L2gmS2488, _M0L3valS2489);
      _M0L6_2atmpS2482 = _M0L6_2atmpS2486 * _M0L6_2atmpS2487;
      _M0L13syn__curr__d1S2484 = _M0L1pS924->$36;
      _M0L3valS2485 = _M0L1kS932->$0;
      #line 245 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS2483
      = _M0MPC15array5Array2atGfE(_M0L13syn__curr__d1S2484, _M0L3valS2485);
      _M0L6_2atmpS2481 = _M0L6_2atmpS2482 - _M0L6_2atmpS2483;
      _M0L6_2atmpS2477 = _M0L6_2atmpS2481 + _M0L3ic1S939;
      _M0L5i__d1S2479 = _M0L1pS924->$5;
      _M0L3valS2480 = _M0L1kS932->$0;
      #line 245 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS2478
      = _M0MPC15array5Array2atGfE(_M0L5i__d1S2479, _M0L3valS2480);
      _M0L6_2atmpS2472 = _M0L6_2atmpS2477 + _M0L6_2atmpS2478;
      _M0L2d1S2476 = _M0L1pS924->$2;
      _M0L1cS2474 = _M0L2d1S2476->$2;
      _M0L3valS2475 = _M0L1kS932->$0;
      #line 245 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS2473
      = _M0MPC15array5Array2atGfE(_M0L1cS2474, _M0L3valS2475);
      _M0L11dv__d1__valS943 = _M0L6_2atmpS2472 / _M0L6_2atmpS2473;
      _M0L5v__d2S2470 = _M0L1pS924->$29;
      _M0L3valS2471 = _M0L1kS932->$0;
      #line 247 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS2469
      = _M0MPC15array5Array2atGfE(_M0L5v__d2S2470, _M0L3valS2471);
      _M0L6_2atmpS2468 = _M0L2elS928 - _M0L6_2atmpS2469;
      _M0L6_2atmpS2463 = _M0L6_2atmpS2468 - _M0L3dd2S937;
      _M0L2d2S2467 = _M0L1pS924->$3;
      _M0L2gmS2465 = _M0L2d2S2467->$4;
      _M0L3valS2466 = _M0L1kS932->$0;
      #line 247 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS2464
      = _M0MPC15array5Array2atGfE(_M0L2gmS2465, _M0L3valS2466);
      _M0L6_2atmpS2459 = _M0L6_2atmpS2463 * _M0L6_2atmpS2464;
      _M0L13syn__curr__d2S2461 = _M0L1pS924->$37;
      _M0L3valS2462 = _M0L1kS932->$0;
      #line 248 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS2460
      = _M0MPC15array5Array2atGfE(_M0L13syn__curr__d2S2461, _M0L3valS2462);
      _M0L6_2atmpS2458 = _M0L6_2atmpS2459 - _M0L6_2atmpS2460;
      _M0L6_2atmpS2454 = _M0L6_2atmpS2458 + _M0L3ic2S940;
      _M0L5i__d2S2456 = _M0L1pS924->$6;
      _M0L3valS2457 = _M0L1kS932->$0;
      #line 248 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS2455
      = _M0MPC15array5Array2atGfE(_M0L5i__d2S2456, _M0L3valS2457);
      _M0L6_2atmpS2449 = _M0L6_2atmpS2454 + _M0L6_2atmpS2455;
      _M0L2d2S2453 = _M0L1pS924->$3;
      _M0L1cS2451 = _M0L2d2S2453->$2;
      _M0L3valS2452 = _M0L1kS932->$0;
      #line 248 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS2450
      = _M0MPC15array5Array2atGfE(_M0L1cS2451, _M0L3valS2452);
      _M0L11dv__d2__valS944 = _M0L6_2atmpS2449 / _M0L6_2atmpS2450;
      _M0L4v__sS2447 = _M0L1pS924->$26;
      _M0L3valS2448 = _M0L1kS932->$0;
      #line 251 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS2446
      = _M0MPC15array5Array2atGfE(_M0L4v__sS2447, _M0L3valS2448);
      _M0L6_2atmpS2445 = _M0L6_2atmpS2446 + _M0L2dsS933;
      _M0L6_2atmpS2444 = _M0L6_2atmpS2445 - _M0L2elS928;
      _M0L6_2atmpS2439 = _M0L1aS931 * _M0L6_2atmpS2444;
      _M0L4w__sS2442 = _M0L1pS924->$27;
      _M0L3valS2443 = _M0L1kS932->$0;
      #line 251 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS2441
      = _M0MPC15array5Array2atGfE(_M0L4w__sS2442, _M0L3valS2443);
      _M0L6_2atmpS2440 = _M0L6_2atmpS2441 + _M0L2dwS938;
      _M0L6_2atmpS2438 = _M0L6_2atmpS2439 - _M0L6_2atmpS2440;
      _M0L7dw__valS945 = _M0L6_2atmpS2438 / _M0L2twS930;
      if (_M0L11store__tempS934) {
        struct _M0TPB5ArrayGfE* _M0L8dv__tempS2406 = _M0L1pS924->$34;
        int32_t _M0L3valS2408 = _M0L1kS932->$0;
        int32_t _M0L6_2atmpS2407 = _M0L3valS2408 * 4;
        struct _M0TPB5ArrayGfE* _M0L8dv__tempS2409;
        int32_t _M0L3valS2412;
        int32_t _M0L6_2atmpS2411;
        int32_t _M0L6_2atmpS2410;
        struct _M0TPB5ArrayGfE* _M0L8dv__tempS2413;
        int32_t _M0L3valS2416;
        int32_t _M0L6_2atmpS2415;
        int32_t _M0L6_2atmpS2414;
        struct _M0TPB5ArrayGfE* _M0L8dv__tempS2417;
        int32_t _M0L3valS2420;
        int32_t _M0L6_2atmpS2419;
        int32_t _M0L6_2atmpS2418;
        #line 255 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
        _M0MPC15array5Array3setGfE(_M0L8dv__tempS2406, _M0L6_2atmpS2407, _M0L10dv__s__valS942);
        _M0L8dv__tempS2409 = _M0L1pS924->$34;
        _M0L3valS2412 = _M0L1kS932->$0;
        _M0L6_2atmpS2411 = _M0L3valS2412 * 4;
        _M0L6_2atmpS2410 = _M0L6_2atmpS2411 + 1;
        #line 256 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
        _M0MPC15array5Array3setGfE(_M0L8dv__tempS2409, _M0L6_2atmpS2410, _M0L11dv__d1__valS943);
        _M0L8dv__tempS2413 = _M0L1pS924->$34;
        _M0L3valS2416 = _M0L1kS932->$0;
        _M0L6_2atmpS2415 = _M0L3valS2416 * 4;
        _M0L6_2atmpS2414 = _M0L6_2atmpS2415 + 2;
        #line 257 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
        _M0MPC15array5Array3setGfE(_M0L8dv__tempS2413, _M0L6_2atmpS2414, _M0L11dv__d2__valS944);
        _M0L8dv__tempS2417 = _M0L1pS924->$34;
        _M0L3valS2420 = _M0L1kS932->$0;
        _M0L6_2atmpS2419 = _M0L3valS2420 * 4;
        _M0L6_2atmpS2418 = _M0L6_2atmpS2419 + 3;
        #line 258 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
        _M0MPC15array5Array3setGfE(_M0L8dv__tempS2417, _M0L6_2atmpS2418, _M0L7dw__valS945);
      } else {
        struct _M0TPB5ArrayGfE* _M0L2dvS2421 = _M0L1pS924->$33;
        int32_t _M0L3valS2423 = _M0L1kS932->$0;
        int32_t _M0L6_2atmpS2422 = _M0L3valS2423 * 4;
        struct _M0TPB5ArrayGfE* _M0L2dvS2424;
        int32_t _M0L3valS2427;
        int32_t _M0L6_2atmpS2426;
        int32_t _M0L6_2atmpS2425;
        struct _M0TPB5ArrayGfE* _M0L2dvS2428;
        int32_t _M0L3valS2431;
        int32_t _M0L6_2atmpS2430;
        int32_t _M0L6_2atmpS2429;
        struct _M0TPB5ArrayGfE* _M0L2dvS2432;
        int32_t _M0L3valS2435;
        int32_t _M0L6_2atmpS2434;
        int32_t _M0L6_2atmpS2433;
        #line 260 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
        _M0MPC15array5Array3setGfE(_M0L2dvS2421, _M0L6_2atmpS2422, _M0L10dv__s__valS942);
        _M0L2dvS2424 = _M0L1pS924->$33;
        _M0L3valS2427 = _M0L1kS932->$0;
        _M0L6_2atmpS2426 = _M0L3valS2427 * 4;
        _M0L6_2atmpS2425 = _M0L6_2atmpS2426 + 1;
        #line 261 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
        _M0MPC15array5Array3setGfE(_M0L2dvS2424, _M0L6_2atmpS2425, _M0L11dv__d1__valS943);
        _M0L2dvS2428 = _M0L1pS924->$33;
        _M0L3valS2431 = _M0L1kS932->$0;
        _M0L6_2atmpS2430 = _M0L3valS2431 * 4;
        _M0L6_2atmpS2429 = _M0L6_2atmpS2430 + 2;
        #line 262 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
        _M0MPC15array5Array3setGfE(_M0L2dvS2428, _M0L6_2atmpS2429, _M0L11dv__d2__valS944);
        _M0L2dvS2432 = _M0L1pS924->$33;
        _M0L3valS2435 = _M0L1kS932->$0;
        _M0L6_2atmpS2434 = _M0L3valS2435 * 4;
        _M0L6_2atmpS2433 = _M0L6_2atmpS2434 + 3;
        #line 263 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
        _M0MPC15array5Array3setGfE(_M0L2dvS2432, _M0L6_2atmpS2433, _M0L7dw__valS945);
      }
      _M0L3valS2437 = _M0L1kS932->$0;
      _M0L6_2atmpS2436 = _M0L3valS2437 + 1;
      _M0L1kS932->$0 = _M0L6_2atmpS2436;
      continue;
    } else {
      moonbit_decref(_M0L1kS932);
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt24tripod__syn__curr__dends(
  struct _M0TP26RiantR8snn__mbt6Tripod* _M0L1pS919
) {
  int32_t _M0L1nS918;
  int32_t _M0L7_2abindS920;
  int32_t _M0L1iS921;
  #line 191 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
  _M0L1nS918 = _M0L1pS919->$25;
  _M0L7_2abindS920 = 0;
  _M0L1iS921 = _M0L7_2abindS920;
  while (1) {
    if (_M0L1iS921 < _M0L1nS918) {
      struct _M0TPB5ArrayGfE* _M0L13syn__curr__d1S2364 = _M0L1pS919->$36;
      struct _M0TPB5ArrayGfE* _M0L6ge__d1S2383 = _M0L1pS919->$9;
      float _M0L6_2atmpS2378;
      struct _M0TPB5ArrayGfE* _M0L5v__d1S2382;
      float _M0L6_2atmpS2380;
      float _M0L4e__eS2381;
      float _M0L6_2atmpS2379;
      float _M0L6_2atmpS2376;
      float _M0L7gsyn__eS2377;
      float _M0L6_2atmpS2366;
      struct _M0TPB5ArrayGfE* _M0L6gi__d1S2375;
      float _M0L6_2atmpS2370;
      struct _M0TPB5ArrayGfE* _M0L5v__d1S2374;
      float _M0L6_2atmpS2372;
      float _M0L4e__iS2373;
      float _M0L6_2atmpS2371;
      float _M0L6_2atmpS2368;
      float _M0L7gsyn__iS2369;
      float _M0L6_2atmpS2367;
      float _M0L6_2atmpS2365;
      struct _M0TPB5ArrayGfE* _M0L13syn__curr__d2S2384;
      struct _M0TPB5ArrayGfE* _M0L6ge__d2S2403;
      float _M0L6_2atmpS2398;
      struct _M0TPB5ArrayGfE* _M0L5v__d2S2402;
      float _M0L6_2atmpS2400;
      float _M0L4e__eS2401;
      float _M0L6_2atmpS2399;
      float _M0L6_2atmpS2396;
      float _M0L7gsyn__eS2397;
      float _M0L6_2atmpS2386;
      struct _M0TPB5ArrayGfE* _M0L6gi__d2S2395;
      float _M0L6_2atmpS2390;
      struct _M0TPB5ArrayGfE* _M0L5v__d2S2394;
      float _M0L6_2atmpS2392;
      float _M0L4e__iS2393;
      float _M0L6_2atmpS2391;
      float _M0L6_2atmpS2388;
      float _M0L7gsyn__iS2389;
      float _M0L6_2atmpS2387;
      float _M0L6_2atmpS2385;
      int32_t _M0L6_2atmpS2404;
      #line 194 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS2378
      = _M0MPC15array5Array2atGfE(_M0L6ge__d1S2383, _M0L1iS921);
      _M0L5v__d1S2382 = _M0L1pS919->$28;
      #line 194 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS2380
      = _M0MPC15array5Array2atGfE(_M0L5v__d1S2382, _M0L1iS921);
      _M0L4e__eS2381 = _M0L1pS919->$19;
      _M0L6_2atmpS2379 = _M0L6_2atmpS2380 - _M0L4e__eS2381;
      _M0L6_2atmpS2376 = _M0L6_2atmpS2378 * _M0L6_2atmpS2379;
      _M0L7gsyn__eS2377 = _M0L1pS919->$23;
      _M0L6_2atmpS2366 = _M0L6_2atmpS2376 * _M0L7gsyn__eS2377;
      _M0L6gi__d1S2375 = _M0L1pS919->$10;
      #line 195 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS2370
      = _M0MPC15array5Array2atGfE(_M0L6gi__d1S2375, _M0L1iS921);
      _M0L5v__d1S2374 = _M0L1pS919->$28;
      #line 195 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS2372
      = _M0MPC15array5Array2atGfE(_M0L5v__d1S2374, _M0L1iS921);
      _M0L4e__iS2373 = _M0L1pS919->$20;
      _M0L6_2atmpS2371 = _M0L6_2atmpS2372 - _M0L4e__iS2373;
      _M0L6_2atmpS2368 = _M0L6_2atmpS2370 * _M0L6_2atmpS2371;
      _M0L7gsyn__iS2369 = _M0L1pS919->$24;
      _M0L6_2atmpS2367 = _M0L6_2atmpS2368 * _M0L7gsyn__iS2369;
      _M0L6_2atmpS2365 = _M0L6_2atmpS2366 + _M0L6_2atmpS2367;
      #line 194 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0MPC15array5Array3setGfE(_M0L13syn__curr__d1S2364, _M0L1iS921, _M0L6_2atmpS2365);
      _M0L13syn__curr__d2S2384 = _M0L1pS919->$37;
      _M0L6ge__d2S2403 = _M0L1pS919->$11;
      #line 196 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS2398
      = _M0MPC15array5Array2atGfE(_M0L6ge__d2S2403, _M0L1iS921);
      _M0L5v__d2S2402 = _M0L1pS919->$29;
      #line 196 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS2400
      = _M0MPC15array5Array2atGfE(_M0L5v__d2S2402, _M0L1iS921);
      _M0L4e__eS2401 = _M0L1pS919->$19;
      _M0L6_2atmpS2399 = _M0L6_2atmpS2400 - _M0L4e__eS2401;
      _M0L6_2atmpS2396 = _M0L6_2atmpS2398 * _M0L6_2atmpS2399;
      _M0L7gsyn__eS2397 = _M0L1pS919->$23;
      _M0L6_2atmpS2386 = _M0L6_2atmpS2396 * _M0L7gsyn__eS2397;
      _M0L6gi__d2S2395 = _M0L1pS919->$12;
      #line 197 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS2390
      = _M0MPC15array5Array2atGfE(_M0L6gi__d2S2395, _M0L1iS921);
      _M0L5v__d2S2394 = _M0L1pS919->$29;
      #line 197 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS2392
      = _M0MPC15array5Array2atGfE(_M0L5v__d2S2394, _M0L1iS921);
      _M0L4e__iS2393 = _M0L1pS919->$20;
      _M0L6_2atmpS2391 = _M0L6_2atmpS2392 - _M0L4e__iS2393;
      _M0L6_2atmpS2388 = _M0L6_2atmpS2390 * _M0L6_2atmpS2391;
      _M0L7gsyn__iS2389 = _M0L1pS919->$24;
      _M0L6_2atmpS2387 = _M0L6_2atmpS2388 * _M0L7gsyn__iS2389;
      _M0L6_2atmpS2385 = _M0L6_2atmpS2386 + _M0L6_2atmpS2387;
      #line 196 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0MPC15array5Array3setGfE(_M0L13syn__curr__d2S2384, _M0L1iS921, _M0L6_2atmpS2385);
      _M0L6_2atmpS2404 = _M0L1iS921 + 1;
      _M0L1iS921 = _M0L6_2atmpS2404;
      continue;
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt23tripod__syn__curr__soma(
  struct _M0TP26RiantR8snn__mbt6Tripod* _M0L1pS914
) {
  int32_t _M0L1nS913;
  int32_t _M0L7_2abindS915;
  int32_t _M0L1iS916;
  #line 181 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
  _M0L1nS913 = _M0L1pS914->$25;
  _M0L7_2abindS915 = 0;
  _M0L1iS916 = _M0L7_2abindS915;
  while (1) {
    if (_M0L1iS916 < _M0L1nS913) {
      struct _M0TPB5ArrayGfE* _M0L12syn__curr__sS2343 = _M0L1pS914->$35;
      struct _M0TPB5ArrayGfE* _M0L5ge__sS2362 = _M0L1pS914->$7;
      float _M0L6_2atmpS2357;
      struct _M0TPB5ArrayGfE* _M0L4v__sS2361;
      float _M0L6_2atmpS2359;
      float _M0L4e__eS2360;
      float _M0L6_2atmpS2358;
      float _M0L6_2atmpS2355;
      float _M0L7gsyn__eS2356;
      float _M0L6_2atmpS2345;
      struct _M0TPB5ArrayGfE* _M0L5gi__sS2354;
      float _M0L6_2atmpS2349;
      struct _M0TPB5ArrayGfE* _M0L4v__sS2353;
      float _M0L6_2atmpS2351;
      float _M0L4e__iS2352;
      float _M0L6_2atmpS2350;
      float _M0L6_2atmpS2347;
      float _M0L7gsyn__iS2348;
      float _M0L6_2atmpS2346;
      float _M0L6_2atmpS2344;
      int32_t _M0L6_2atmpS2363;
      #line 184 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS2357
      = _M0MPC15array5Array2atGfE(_M0L5ge__sS2362, _M0L1iS916);
      _M0L4v__sS2361 = _M0L1pS914->$26;
      #line 184 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS2359
      = _M0MPC15array5Array2atGfE(_M0L4v__sS2361, _M0L1iS916);
      _M0L4e__eS2360 = _M0L1pS914->$19;
      _M0L6_2atmpS2358 = _M0L6_2atmpS2359 - _M0L4e__eS2360;
      _M0L6_2atmpS2355 = _M0L6_2atmpS2357 * _M0L6_2atmpS2358;
      _M0L7gsyn__eS2356 = _M0L1pS914->$23;
      _M0L6_2atmpS2345 = _M0L6_2atmpS2355 * _M0L7gsyn__eS2356;
      _M0L5gi__sS2354 = _M0L1pS914->$8;
      #line 185 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS2349
      = _M0MPC15array5Array2atGfE(_M0L5gi__sS2354, _M0L1iS916);
      _M0L4v__sS2353 = _M0L1pS914->$26;
      #line 185 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS2351
      = _M0MPC15array5Array2atGfE(_M0L4v__sS2353, _M0L1iS916);
      _M0L4e__iS2352 = _M0L1pS914->$20;
      _M0L6_2atmpS2350 = _M0L6_2atmpS2351 - _M0L4e__iS2352;
      _M0L6_2atmpS2347 = _M0L6_2atmpS2349 * _M0L6_2atmpS2350;
      _M0L7gsyn__iS2348 = _M0L1pS914->$24;
      _M0L6_2atmpS2346 = _M0L6_2atmpS2347 * _M0L7gsyn__iS2348;
      _M0L6_2atmpS2344 = _M0L6_2atmpS2345 + _M0L6_2atmpS2346;
      #line 184 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0MPC15array5Array3setGfE(_M0L12syn__curr__sS2343, _M0L1iS916, _M0L6_2atmpS2344);
      _M0L6_2atmpS2363 = _M0L1iS916 + 1;
      _M0L1iS916 = _M0L6_2atmpS2363;
      continue;
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt28tripod__dend__step__synapses(
  struct _M0TP26RiantR8snn__mbt6Tripod* _M0L1pS899,
  float _M0L2dtS902
) {
  int32_t _M0L1nS898;
  int32_t _M0L7_2abindS900;
  int32_t _M0L1iS901;
  int32_t _M0L7_2abindS904;
  int32_t _M0L1iS905;
  int32_t _M0L7_2abindS907;
  int32_t _M0L1iS908;
  int32_t _M0L7_2abindS910;
  int32_t _M0L1iS911;
  #line 153 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
  _M0L1nS898 = _M0L1pS899->$25;
  _M0L7_2abindS900 = 0;
  _M0L1iS901 = _M0L7_2abindS900;
  while (1) {
    if (_M0L1iS901 < _M0L1nS898) {
      struct _M0TPB5ArrayGfE* _M0L6ge__d1S2271 = _M0L1pS899->$9;
      struct _M0TPB5ArrayGfE* _M0L6ge__d1S2276 = _M0L1pS899->$9;
      float _M0L6_2atmpS2273;
      struct _M0TPB5ArrayGfE* _M0L7glu__d1S2275;
      float _M0L6_2atmpS2274;
      float _M0L6_2atmpS2272;
      struct _M0TPB5ArrayGfE* _M0L6gi__d1S2277;
      struct _M0TPB5ArrayGfE* _M0L6gi__d1S2282;
      float _M0L6_2atmpS2279;
      struct _M0TPB5ArrayGfE* _M0L8gaba__d1S2281;
      float _M0L6_2atmpS2280;
      float _M0L6_2atmpS2278;
      struct _M0TPB5ArrayGfE* _M0L6ge__d1S2283;
      struct _M0TPB5ArrayGfE* _M0L6ge__d1S2292;
      float _M0L6_2atmpS2285;
      struct _M0TPB5ArrayGfE* _M0L6ge__d1S2291;
      float _M0L6_2atmpS2290;
      float _M0L6_2atmpS2288;
      float _M0L6tau__eS2289;
      float _M0L6_2atmpS2287;
      float _M0L6_2atmpS2286;
      float _M0L6_2atmpS2284;
      struct _M0TPB5ArrayGfE* _M0L6gi__d1S2293;
      struct _M0TPB5ArrayGfE* _M0L6gi__d1S2302;
      float _M0L6_2atmpS2295;
      struct _M0TPB5ArrayGfE* _M0L6gi__d1S2301;
      float _M0L6_2atmpS2300;
      float _M0L6_2atmpS2298;
      float _M0L6tau__iS2299;
      float _M0L6_2atmpS2297;
      float _M0L6_2atmpS2296;
      float _M0L6_2atmpS2294;
      int32_t _M0L6_2atmpS2303;
      #line 157 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS2273
      = _M0MPC15array5Array2atGfE(_M0L6ge__d1S2276, _M0L1iS901);
      _M0L7glu__d1S2275 = _M0L1pS899->$15;
      #line 157 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS2274
      = _M0MPC15array5Array2atGfE(_M0L7glu__d1S2275, _M0L1iS901);
      _M0L6_2atmpS2272 = _M0L6_2atmpS2273 + _M0L6_2atmpS2274;
      #line 157 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0MPC15array5Array3setGfE(_M0L6ge__d1S2271, _M0L1iS901, _M0L6_2atmpS2272);
      _M0L6gi__d1S2277 = _M0L1pS899->$10;
      _M0L6gi__d1S2282 = _M0L1pS899->$10;
      #line 158 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS2279
      = _M0MPC15array5Array2atGfE(_M0L6gi__d1S2282, _M0L1iS901);
      _M0L8gaba__d1S2281 = _M0L1pS899->$16;
      #line 158 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS2280
      = _M0MPC15array5Array2atGfE(_M0L8gaba__d1S2281, _M0L1iS901);
      _M0L6_2atmpS2278 = _M0L6_2atmpS2279 + _M0L6_2atmpS2280;
      #line 158 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0MPC15array5Array3setGfE(_M0L6gi__d1S2277, _M0L1iS901, _M0L6_2atmpS2278);
      _M0L6ge__d1S2283 = _M0L1pS899->$9;
      _M0L6ge__d1S2292 = _M0L1pS899->$9;
      #line 159 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS2285
      = _M0MPC15array5Array2atGfE(_M0L6ge__d1S2292, _M0L1iS901);
      _M0L6ge__d1S2291 = _M0L1pS899->$9;
      #line 159 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS2290
      = _M0MPC15array5Array2atGfE(_M0L6ge__d1S2291, _M0L1iS901);
      _M0L6_2atmpS2288 = -_M0L6_2atmpS2290;
      _M0L6tau__eS2289 = _M0L1pS899->$21;
      _M0L6_2atmpS2287 = _M0L6_2atmpS2288 / _M0L6tau__eS2289;
      _M0L6_2atmpS2286 = _M0L2dtS902 * _M0L6_2atmpS2287;
      _M0L6_2atmpS2284 = _M0L6_2atmpS2285 + _M0L6_2atmpS2286;
      #line 159 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0MPC15array5Array3setGfE(_M0L6ge__d1S2283, _M0L1iS901, _M0L6_2atmpS2284);
      _M0L6gi__d1S2293 = _M0L1pS899->$10;
      _M0L6gi__d1S2302 = _M0L1pS899->$10;
      #line 160 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS2295
      = _M0MPC15array5Array2atGfE(_M0L6gi__d1S2302, _M0L1iS901);
      _M0L6gi__d1S2301 = _M0L1pS899->$10;
      #line 160 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS2300
      = _M0MPC15array5Array2atGfE(_M0L6gi__d1S2301, _M0L1iS901);
      _M0L6_2atmpS2298 = -_M0L6_2atmpS2300;
      _M0L6tau__iS2299 = _M0L1pS899->$22;
      _M0L6_2atmpS2297 = _M0L6_2atmpS2298 / _M0L6tau__iS2299;
      _M0L6_2atmpS2296 = _M0L2dtS902 * _M0L6_2atmpS2297;
      _M0L6_2atmpS2294 = _M0L6_2atmpS2295 + _M0L6_2atmpS2296;
      #line 160 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0MPC15array5Array3setGfE(_M0L6gi__d1S2293, _M0L1iS901, _M0L6_2atmpS2294);
      _M0L6_2atmpS2303 = _M0L1iS901 + 1;
      _M0L1iS901 = _M0L6_2atmpS2303;
      continue;
    }
    break;
  }
  _M0L7_2abindS904 = 0;
  _M0L1iS905 = _M0L7_2abindS904;
  while (1) {
    if (_M0L1iS905 < _M0L1nS898) {
      struct _M0TPB5ArrayGfE* _M0L7glu__d1S2304 = _M0L1pS899->$15;
      struct _M0TPB5ArrayGfE* _M0L8gaba__d1S2305;
      int32_t _M0L6_2atmpS2306;
      #line 163 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0MPC15array5Array3setGfE(_M0L7glu__d1S2304, _M0L1iS905, 0x0p+0f);
      _M0L8gaba__d1S2305 = _M0L1pS899->$16;
      #line 164 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0MPC15array5Array3setGfE(_M0L8gaba__d1S2305, _M0L1iS905, 0x0p+0f);
      _M0L6_2atmpS2306 = _M0L1iS905 + 1;
      _M0L1iS905 = _M0L6_2atmpS2306;
      continue;
    }
    break;
  }
  _M0L7_2abindS907 = 0;
  _M0L1iS908 = _M0L7_2abindS907;
  while (1) {
    if (_M0L1iS908 < _M0L1nS898) {
      struct _M0TPB5ArrayGfE* _M0L6ge__d2S2307 = _M0L1pS899->$11;
      struct _M0TPB5ArrayGfE* _M0L6ge__d2S2312 = _M0L1pS899->$11;
      float _M0L6_2atmpS2309;
      struct _M0TPB5ArrayGfE* _M0L7glu__d2S2311;
      float _M0L6_2atmpS2310;
      float _M0L6_2atmpS2308;
      struct _M0TPB5ArrayGfE* _M0L6gi__d2S2313;
      struct _M0TPB5ArrayGfE* _M0L6gi__d2S2318;
      float _M0L6_2atmpS2315;
      struct _M0TPB5ArrayGfE* _M0L8gaba__d2S2317;
      float _M0L6_2atmpS2316;
      float _M0L6_2atmpS2314;
      struct _M0TPB5ArrayGfE* _M0L6ge__d2S2319;
      struct _M0TPB5ArrayGfE* _M0L6ge__d2S2328;
      float _M0L6_2atmpS2321;
      struct _M0TPB5ArrayGfE* _M0L6ge__d2S2327;
      float _M0L6_2atmpS2326;
      float _M0L6_2atmpS2324;
      float _M0L6tau__eS2325;
      float _M0L6_2atmpS2323;
      float _M0L6_2atmpS2322;
      float _M0L6_2atmpS2320;
      struct _M0TPB5ArrayGfE* _M0L6gi__d2S2329;
      struct _M0TPB5ArrayGfE* _M0L6gi__d2S2338;
      float _M0L6_2atmpS2331;
      struct _M0TPB5ArrayGfE* _M0L6gi__d2S2337;
      float _M0L6_2atmpS2336;
      float _M0L6_2atmpS2334;
      float _M0L6tau__iS2335;
      float _M0L6_2atmpS2333;
      float _M0L6_2atmpS2332;
      float _M0L6_2atmpS2330;
      int32_t _M0L6_2atmpS2339;
      #line 168 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS2309
      = _M0MPC15array5Array2atGfE(_M0L6ge__d2S2312, _M0L1iS908);
      _M0L7glu__d2S2311 = _M0L1pS899->$17;
      #line 168 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS2310
      = _M0MPC15array5Array2atGfE(_M0L7glu__d2S2311, _M0L1iS908);
      _M0L6_2atmpS2308 = _M0L6_2atmpS2309 + _M0L6_2atmpS2310;
      #line 168 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0MPC15array5Array3setGfE(_M0L6ge__d2S2307, _M0L1iS908, _M0L6_2atmpS2308);
      _M0L6gi__d2S2313 = _M0L1pS899->$12;
      _M0L6gi__d2S2318 = _M0L1pS899->$12;
      #line 169 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS2315
      = _M0MPC15array5Array2atGfE(_M0L6gi__d2S2318, _M0L1iS908);
      _M0L8gaba__d2S2317 = _M0L1pS899->$18;
      #line 169 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS2316
      = _M0MPC15array5Array2atGfE(_M0L8gaba__d2S2317, _M0L1iS908);
      _M0L6_2atmpS2314 = _M0L6_2atmpS2315 + _M0L6_2atmpS2316;
      #line 169 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0MPC15array5Array3setGfE(_M0L6gi__d2S2313, _M0L1iS908, _M0L6_2atmpS2314);
      _M0L6ge__d2S2319 = _M0L1pS899->$11;
      _M0L6ge__d2S2328 = _M0L1pS899->$11;
      #line 170 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS2321
      = _M0MPC15array5Array2atGfE(_M0L6ge__d2S2328, _M0L1iS908);
      _M0L6ge__d2S2327 = _M0L1pS899->$11;
      #line 170 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS2326
      = _M0MPC15array5Array2atGfE(_M0L6ge__d2S2327, _M0L1iS908);
      _M0L6_2atmpS2324 = -_M0L6_2atmpS2326;
      _M0L6tau__eS2325 = _M0L1pS899->$21;
      _M0L6_2atmpS2323 = _M0L6_2atmpS2324 / _M0L6tau__eS2325;
      _M0L6_2atmpS2322 = _M0L2dtS902 * _M0L6_2atmpS2323;
      _M0L6_2atmpS2320 = _M0L6_2atmpS2321 + _M0L6_2atmpS2322;
      #line 170 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0MPC15array5Array3setGfE(_M0L6ge__d2S2319, _M0L1iS908, _M0L6_2atmpS2320);
      _M0L6gi__d2S2329 = _M0L1pS899->$12;
      _M0L6gi__d2S2338 = _M0L1pS899->$12;
      #line 171 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS2331
      = _M0MPC15array5Array2atGfE(_M0L6gi__d2S2338, _M0L1iS908);
      _M0L6gi__d2S2337 = _M0L1pS899->$12;
      #line 171 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS2336
      = _M0MPC15array5Array2atGfE(_M0L6gi__d2S2337, _M0L1iS908);
      _M0L6_2atmpS2334 = -_M0L6_2atmpS2336;
      _M0L6tau__iS2335 = _M0L1pS899->$22;
      _M0L6_2atmpS2333 = _M0L6_2atmpS2334 / _M0L6tau__iS2335;
      _M0L6_2atmpS2332 = _M0L2dtS902 * _M0L6_2atmpS2333;
      _M0L6_2atmpS2330 = _M0L6_2atmpS2331 + _M0L6_2atmpS2332;
      #line 171 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0MPC15array5Array3setGfE(_M0L6gi__d2S2329, _M0L1iS908, _M0L6_2atmpS2330);
      _M0L6_2atmpS2339 = _M0L1iS908 + 1;
      _M0L1iS908 = _M0L6_2atmpS2339;
      continue;
    }
    break;
  }
  _M0L7_2abindS910 = 0;
  _M0L1iS911 = _M0L7_2abindS910;
  while (1) {
    if (_M0L1iS911 < _M0L1nS898) {
      struct _M0TPB5ArrayGfE* _M0L7glu__d2S2340 = _M0L1pS899->$17;
      struct _M0TPB5ArrayGfE* _M0L8gaba__d2S2341;
      int32_t _M0L6_2atmpS2342;
      #line 174 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0MPC15array5Array3setGfE(_M0L7glu__d2S2340, _M0L1iS911, 0x0p+0f);
      _M0L8gaba__d2S2341 = _M0L1pS899->$18;
      #line 175 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0MPC15array5Array3setGfE(_M0L8gaba__d2S2341, _M0L1iS911, 0x0p+0f);
      _M0L6_2atmpS2342 = _M0L1iS911 + 1;
      _M0L1iS911 = _M0L6_2atmpS2342;
      continue;
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt28tripod__soma__step__synapses(
  struct _M0TP26RiantR8snn__mbt6Tripod* _M0L1pS890,
  float _M0L2dtS893
) {
  int32_t _M0L1nS889;
  int32_t _M0L7_2abindS891;
  int32_t _M0L1iS892;
  int32_t _M0L7_2abindS895;
  int32_t _M0L1iS896;
  #line 137 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
  _M0L1nS889 = _M0L1pS890->$25;
  _M0L7_2abindS891 = 0;
  _M0L1iS892 = _M0L7_2abindS891;
  while (1) {
    if (_M0L1iS892 < _M0L1nS889) {
      struct _M0TPB5ArrayGfE* _M0L5ge__sS2235 = _M0L1pS890->$7;
      struct _M0TPB5ArrayGfE* _M0L5ge__sS2240 = _M0L1pS890->$7;
      float _M0L6_2atmpS2237;
      struct _M0TPB5ArrayGfE* _M0L6glu__sS2239;
      float _M0L6_2atmpS2238;
      float _M0L6_2atmpS2236;
      struct _M0TPB5ArrayGfE* _M0L5gi__sS2241;
      struct _M0TPB5ArrayGfE* _M0L5gi__sS2246;
      float _M0L6_2atmpS2243;
      struct _M0TPB5ArrayGfE* _M0L7gaba__sS2245;
      float _M0L6_2atmpS2244;
      float _M0L6_2atmpS2242;
      struct _M0TPB5ArrayGfE* _M0L5ge__sS2247;
      struct _M0TPB5ArrayGfE* _M0L5ge__sS2256;
      float _M0L6_2atmpS2249;
      struct _M0TPB5ArrayGfE* _M0L5ge__sS2255;
      float _M0L6_2atmpS2254;
      float _M0L6_2atmpS2252;
      float _M0L6tau__eS2253;
      float _M0L6_2atmpS2251;
      float _M0L6_2atmpS2250;
      float _M0L6_2atmpS2248;
      struct _M0TPB5ArrayGfE* _M0L5gi__sS2257;
      struct _M0TPB5ArrayGfE* _M0L5gi__sS2266;
      float _M0L6_2atmpS2259;
      struct _M0TPB5ArrayGfE* _M0L5gi__sS2265;
      float _M0L6_2atmpS2264;
      float _M0L6_2atmpS2262;
      float _M0L6tau__iS2263;
      float _M0L6_2atmpS2261;
      float _M0L6_2atmpS2260;
      float _M0L6_2atmpS2258;
      int32_t _M0L6_2atmpS2267;
      #line 140 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS2237
      = _M0MPC15array5Array2atGfE(_M0L5ge__sS2240, _M0L1iS892);
      _M0L6glu__sS2239 = _M0L1pS890->$13;
      #line 140 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS2238
      = _M0MPC15array5Array2atGfE(_M0L6glu__sS2239, _M0L1iS892);
      _M0L6_2atmpS2236 = _M0L6_2atmpS2237 + _M0L6_2atmpS2238;
      #line 140 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0MPC15array5Array3setGfE(_M0L5ge__sS2235, _M0L1iS892, _M0L6_2atmpS2236);
      _M0L5gi__sS2241 = _M0L1pS890->$8;
      _M0L5gi__sS2246 = _M0L1pS890->$8;
      #line 141 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS2243
      = _M0MPC15array5Array2atGfE(_M0L5gi__sS2246, _M0L1iS892);
      _M0L7gaba__sS2245 = _M0L1pS890->$14;
      #line 141 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS2244
      = _M0MPC15array5Array2atGfE(_M0L7gaba__sS2245, _M0L1iS892);
      _M0L6_2atmpS2242 = _M0L6_2atmpS2243 + _M0L6_2atmpS2244;
      #line 141 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0MPC15array5Array3setGfE(_M0L5gi__sS2241, _M0L1iS892, _M0L6_2atmpS2242);
      _M0L5ge__sS2247 = _M0L1pS890->$7;
      _M0L5ge__sS2256 = _M0L1pS890->$7;
      #line 142 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS2249
      = _M0MPC15array5Array2atGfE(_M0L5ge__sS2256, _M0L1iS892);
      _M0L5ge__sS2255 = _M0L1pS890->$7;
      #line 142 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS2254
      = _M0MPC15array5Array2atGfE(_M0L5ge__sS2255, _M0L1iS892);
      _M0L6_2atmpS2252 = -_M0L6_2atmpS2254;
      _M0L6tau__eS2253 = _M0L1pS890->$21;
      _M0L6_2atmpS2251 = _M0L6_2atmpS2252 / _M0L6tau__eS2253;
      _M0L6_2atmpS2250 = _M0L2dtS893 * _M0L6_2atmpS2251;
      _M0L6_2atmpS2248 = _M0L6_2atmpS2249 + _M0L6_2atmpS2250;
      #line 142 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0MPC15array5Array3setGfE(_M0L5ge__sS2247, _M0L1iS892, _M0L6_2atmpS2248);
      _M0L5gi__sS2257 = _M0L1pS890->$8;
      _M0L5gi__sS2266 = _M0L1pS890->$8;
      #line 143 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS2259
      = _M0MPC15array5Array2atGfE(_M0L5gi__sS2266, _M0L1iS892);
      _M0L5gi__sS2265 = _M0L1pS890->$8;
      #line 143 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS2264
      = _M0MPC15array5Array2atGfE(_M0L5gi__sS2265, _M0L1iS892);
      _M0L6_2atmpS2262 = -_M0L6_2atmpS2264;
      _M0L6tau__iS2263 = _M0L1pS890->$22;
      _M0L6_2atmpS2261 = _M0L6_2atmpS2262 / _M0L6tau__iS2263;
      _M0L6_2atmpS2260 = _M0L2dtS893 * _M0L6_2atmpS2261;
      _M0L6_2atmpS2258 = _M0L6_2atmpS2259 + _M0L6_2atmpS2260;
      #line 143 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0MPC15array5Array3setGfE(_M0L5gi__sS2257, _M0L1iS892, _M0L6_2atmpS2258);
      _M0L6_2atmpS2267 = _M0L1iS892 + 1;
      _M0L1iS892 = _M0L6_2atmpS2267;
      continue;
    }
    break;
  }
  _M0L7_2abindS895 = 0;
  _M0L1iS896 = _M0L7_2abindS895;
  while (1) {
    if (_M0L1iS896 < _M0L1nS889) {
      struct _M0TPB5ArrayGfE* _M0L6glu__sS2268 = _M0L1pS890->$13;
      struct _M0TPB5ArrayGfE* _M0L7gaba__sS2269;
      int32_t _M0L6_2atmpS2270;
      #line 146 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0MPC15array5Array3setGfE(_M0L6glu__sS2268, _M0L1iS896, 0x0p+0f);
      _M0L7gaba__sS2269 = _M0L1pS890->$14;
      #line 147 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0MPC15array5Array3setGfE(_M0L7gaba__sS2269, _M0L1iS896, 0x0p+0f);
      _M0L6_2atmpS2270 = _M0L1iS896 + 1;
      _M0L1iS896 = _M0L6_2atmpS2270;
      continue;
    }
    break;
  }
  return 0;
}

struct _M0TP26RiantR8snn__mbt6Tripod* _M0MP26RiantR8snn__mbt6Tripod3new(
  int32_t _M0L1nS850,
  struct _M0TP26RiantR8snn__mbt13AdExParameter* _M0L11soma__paramS852,
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS855
) {
  struct _M0TPB5ArrayGfE* _M0L4v__sS849;
  float _M0L2vtS2233;
  float _M0L2vrS2234;
  float _M0L6spreadS851;
  int32_t _M0L7_2abindS853;
  int32_t _M0L1kS854;
  struct _M0TPB5ArrayGfE* _M0L4w__sS857;
  struct _M0TPB5ArrayGfE* _M0L5v__d1S858;
  struct _M0TPB5ArrayGfE* _M0L5v__d2S859;
  int32_t _M0L7_2abindS860;
  int32_t _M0L1kS861;
  struct _M0TPB5ArrayGbE* _M0L4fireS863;
  float _M0L2vtS2232;
  struct _M0TPB5ArrayGfE* _M0L9thresholdS864;
  struct _M0TPB5ArrayGiE* _M0L4tabsS865;
  struct _M0TPB5ArrayGfE* _M0L4i__sS866;
  struct _M0TPB5ArrayGfE* _M0L5i__d1S867;
  struct _M0TPB5ArrayGfE* _M0L5i__d2S868;
  struct _M0TPB5ArrayGfE* _M0L5ge__sS869;
  struct _M0TPB5ArrayGfE* _M0L5gi__sS870;
  struct _M0TPB5ArrayGfE* _M0L6ge__d1S871;
  struct _M0TPB5ArrayGfE* _M0L6gi__d1S872;
  struct _M0TPB5ArrayGfE* _M0L6ge__d2S873;
  struct _M0TPB5ArrayGfE* _M0L6gi__d2S874;
  struct _M0TPB5ArrayGfE* _M0L6glu__sS875;
  struct _M0TPB5ArrayGfE* _M0L7gaba__sS876;
  struct _M0TPB5ArrayGfE* _M0L7glu__d1S877;
  struct _M0TPB5ArrayGfE* _M0L8gaba__d1S878;
  struct _M0TPB5ArrayGfE* _M0L7glu__d2S879;
  struct _M0TPB5ArrayGfE* _M0L8gaba__d2S880;
  int32_t _M0L6total4S881;
  struct _M0TPB5ArrayGfE* _M0L2dvS882;
  struct _M0TPB5ArrayGfE* _M0L8dv__tempS883;
  struct _M0TPB5ArrayGfE* _M0L12syn__curr__sS884;
  struct _M0TPB5ArrayGfE* _M0L13syn__curr__d1S885;
  struct _M0TPB5ArrayGfE* _M0L13syn__curr__d2S886;
  struct _M0TP26RiantR8snn__mbt8Dendrite* _M0L2d1S887;
  struct _M0TP26RiantR8snn__mbt8Dendrite* _M0L2d2S888;
  struct _M0TP26RiantR8snn__mbt13AdExPostSpike* _M0L6_2atmpS2231;
  struct _M0TP26RiantR8snn__mbt6Tripod* _block_3077;
  #line 71 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
  #line 77 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
  _M0L4v__sS849 = _M0MPC15array5Array4makeGfE(_M0L1nS850, 0x0p+0f);
  _M0L2vtS2233 = _M0L11soma__paramS852->$2;
  _M0L2vrS2234 = _M0L11soma__paramS852->$3;
  _M0L6spreadS851 = _M0L2vtS2233 - _M0L2vrS2234;
  _M0L7_2abindS853 = 0;
  _M0L1kS854 = _M0L7_2abindS853;
  while (1) {
    if (_M0L1kS854 < _M0L1nS850) {
      float _M0L2vrS2218 = _M0L11soma__paramS852->$3;
      float _M0L6_2atmpS2220;
      float _M0L6_2atmpS2219;
      float _M0L6_2atmpS2217;
      int32_t _M0L6_2atmpS2221;
      #line 80 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS2220 = _M0FP26RiantR8snn__mbt9next__f32(_M0L3rngS855);
      _M0L6_2atmpS2219 = _M0L6_2atmpS2220 * _M0L6spreadS851;
      _M0L6_2atmpS2217 = _M0L2vrS2218 + _M0L6_2atmpS2219;
      #line 80 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0MPC15array5Array3setGfE(_M0L4v__sS849, _M0L1kS854, _M0L6_2atmpS2217);
      _M0L6_2atmpS2221 = _M0L1kS854 + 1;
      _M0L1kS854 = _M0L6_2atmpS2221;
      continue;
    }
    break;
  }
  #line 82 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
  _M0L4w__sS857 = _M0MPC15array5Array4makeGfE(_M0L1nS850, 0x0p+0f);
  #line 84 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
  _M0L5v__d1S858 = _M0MPC15array5Array4makeGfE(_M0L1nS850, 0x0p+0f);
  #line 85 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
  _M0L5v__d2S859 = _M0MPC15array5Array4makeGfE(_M0L1nS850, 0x0p+0f);
  _M0L7_2abindS860 = 0;
  _M0L1kS861 = _M0L7_2abindS860;
  while (1) {
    if (_M0L1kS861 < _M0L1nS850) {
      float _M0L2vrS2223 = _M0L11soma__paramS852->$3;
      float _M0L6_2atmpS2225;
      float _M0L6_2atmpS2224;
      float _M0L6_2atmpS2222;
      float _M0L2vrS2227;
      float _M0L6_2atmpS2229;
      float _M0L6_2atmpS2228;
      float _M0L6_2atmpS2226;
      int32_t _M0L6_2atmpS2230;
      #line 87 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS2225 = _M0FP26RiantR8snn__mbt9next__f32(_M0L3rngS855);
      _M0L6_2atmpS2224 = _M0L6_2atmpS2225 * _M0L6spreadS851;
      _M0L6_2atmpS2222 = _M0L2vrS2223 + _M0L6_2atmpS2224;
      #line 87 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0MPC15array5Array3setGfE(_M0L5v__d1S858, _M0L1kS861, _M0L6_2atmpS2222);
      _M0L2vrS2227 = _M0L11soma__paramS852->$3;
      #line 88 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0L6_2atmpS2229 = _M0FP26RiantR8snn__mbt9next__f32(_M0L3rngS855);
      _M0L6_2atmpS2228 = _M0L6_2atmpS2229 * _M0L6spreadS851;
      _M0L6_2atmpS2226 = _M0L2vrS2227 + _M0L6_2atmpS2228;
      #line 88 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
      _M0MPC15array5Array3setGfE(_M0L5v__d2S859, _M0L1kS861, _M0L6_2atmpS2226);
      _M0L6_2atmpS2230 = _M0L1kS861 + 1;
      _M0L1kS861 = _M0L6_2atmpS2230;
      continue;
    }
    break;
  }
  #line 90 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
  _M0L4fireS863 = _M0MPC15array5Array4makeGbE(_M0L1nS850, 0);
  _M0L2vtS2232 = _M0L11soma__paramS852->$2;
  #line 91 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
  _M0L9thresholdS864 = _M0MPC15array5Array4makeGfE(_M0L1nS850, _M0L2vtS2232);
  #line 92 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
  _M0L4tabsS865 = _M0MPC15array5Array4makeGiE(_M0L1nS850, 1);
  #line 94 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
  _M0L4i__sS866 = _M0MPC15array5Array4makeGfE(_M0L1nS850, 0x0p+0f);
  #line 95 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
  _M0L5i__d1S867 = _M0MPC15array5Array4makeGfE(_M0L1nS850, 0x0p+0f);
  #line 96 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
  _M0L5i__d2S868 = _M0MPC15array5Array4makeGfE(_M0L1nS850, 0x0p+0f);
  #line 98 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
  _M0L5ge__sS869 = _M0MPC15array5Array4makeGfE(_M0L1nS850, 0x0p+0f);
  #line 99 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
  _M0L5gi__sS870 = _M0MPC15array5Array4makeGfE(_M0L1nS850, 0x0p+0f);
  #line 100 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
  _M0L6ge__d1S871 = _M0MPC15array5Array4makeGfE(_M0L1nS850, 0x0p+0f);
  #line 101 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
  _M0L6gi__d1S872 = _M0MPC15array5Array4makeGfE(_M0L1nS850, 0x0p+0f);
  #line 102 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
  _M0L6ge__d2S873 = _M0MPC15array5Array4makeGfE(_M0L1nS850, 0x0p+0f);
  #line 103 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
  _M0L6gi__d2S874 = _M0MPC15array5Array4makeGfE(_M0L1nS850, 0x0p+0f);
  #line 104 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
  _M0L6glu__sS875 = _M0MPC15array5Array4makeGfE(_M0L1nS850, 0x0p+0f);
  #line 105 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
  _M0L7gaba__sS876 = _M0MPC15array5Array4makeGfE(_M0L1nS850, 0x0p+0f);
  #line 106 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
  _M0L7glu__d1S877 = _M0MPC15array5Array4makeGfE(_M0L1nS850, 0x0p+0f);
  #line 107 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
  _M0L8gaba__d1S878 = _M0MPC15array5Array4makeGfE(_M0L1nS850, 0x0p+0f);
  #line 108 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
  _M0L7glu__d2S879 = _M0MPC15array5Array4makeGfE(_M0L1nS850, 0x0p+0f);
  #line 109 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
  _M0L8gaba__d2S880 = _M0MPC15array5Array4makeGfE(_M0L1nS850, 0x0p+0f);
  _M0L6total4S881 = _M0L1nS850 * 4;
  #line 112 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
  _M0L2dvS882 = _M0MPC15array5Array4makeGfE(_M0L6total4S881, 0x0p+0f);
  #line 113 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
  _M0L8dv__tempS883 = _M0MPC15array5Array4makeGfE(_M0L6total4S881, 0x0p+0f);
  #line 114 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
  _M0L12syn__curr__sS884 = _M0MPC15array5Array4makeGfE(_M0L1nS850, 0x0p+0f);
  #line 115 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
  _M0L13syn__curr__d1S885 = _M0MPC15array5Array4makeGfE(_M0L1nS850, 0x0p+0f);
  #line 116 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
  _M0L13syn__curr__d2S886 = _M0MPC15array5Array4makeGfE(_M0L1nS850, 0x0p+0f);
  #line 117 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
  _M0L2d1S887 = _M0MP26RiantR8snn__mbt8Dendrite3new(_M0L1nS850);
  #line 118 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
  _M0L2d2S888 = _M0MP26RiantR8snn__mbt8Dendrite3new(_M0L1nS850);
  #line 121 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod.mbt"
  _M0L6_2atmpS2231 = _M0MP26RiantR8snn__mbt13AdExPostSpike3new();
  moonbit_incref(_M0L11soma__paramS852);
  _block_3077
  = (struct _M0TP26RiantR8snn__mbt6Tripod*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt6Tripod));
  Moonbit_object_header(_block_3077)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 54, 0);
  _block_3077->$0 = _M0L11soma__paramS852;
  _block_3077->$1 = _M0L6_2atmpS2231;
  _block_3077->$2 = _M0L2d1S887;
  _block_3077->$3 = _M0L2d2S888;
  _block_3077->$4 = _M0L4i__sS866;
  _block_3077->$5 = _M0L5i__d1S867;
  _block_3077->$6 = _M0L5i__d2S868;
  _block_3077->$7 = _M0L5ge__sS869;
  _block_3077->$8 = _M0L5gi__sS870;
  _block_3077->$9 = _M0L6ge__d1S871;
  _block_3077->$10 = _M0L6gi__d1S872;
  _block_3077->$11 = _M0L6ge__d2S873;
  _block_3077->$12 = _M0L6gi__d2S874;
  _block_3077->$13 = _M0L6glu__sS875;
  _block_3077->$14 = _M0L7gaba__sS876;
  _block_3077->$15 = _M0L7glu__d1S877;
  _block_3077->$16 = _M0L8gaba__d1S878;
  _block_3077->$17 = _M0L7glu__d2S879;
  _block_3077->$18 = _M0L8gaba__d2S880;
  _block_3077->$19 = 0x0p+0f;
  _block_3077->$20 = -0x1.2cp+6f;
  _block_3077->$21 = 0x1.8p+2f;
  _block_3077->$22 = 0x1p+1f;
  _block_3077->$23 = 0x1p+0f;
  _block_3077->$24 = 0x1p+0f;
  _block_3077->$25 = _M0L1nS850;
  _block_3077->$26 = _M0L4v__sS849;
  _block_3077->$27 = _M0L4w__sS857;
  _block_3077->$28 = _M0L5v__d1S858;
  _block_3077->$29 = _M0L5v__d2S859;
  _block_3077->$30 = _M0L4fireS863;
  _block_3077->$31 = _M0L9thresholdS864;
  _block_3077->$32 = _M0L4tabsS865;
  _block_3077->$33 = _M0L2dvS882;
  _block_3077->$34 = _M0L8dv__tempS883;
  _block_3077->$35 = _M0L12syn__curr__sS884;
  _block_3077->$36 = _M0L13syn__curr__d1S885;
  _block_3077->$37 = _M0L13syn__curr__d2S886;
  return _block_3077;
}

struct _M0TP26RiantR8snn__mbt8Dendrite* _M0MP26RiantR8snn__mbt8Dendrite3new(
  int32_t _M0L1nS842
) {
  struct _M0TPB5ArrayGfE* _M0L2elS841;
  struct _M0TPB5ArrayGfE* _M0L1cS843;
  struct _M0TPB5ArrayGfE* _M0L3gaxS844;
  struct _M0TPB5ArrayGfE* _M0L2gmS845;
  struct _M0TPB5ArrayGfE* _M0L1lS846;
  struct _M0TPB5ArrayGfE* _M0L1dS847;
  struct _M0TPB5ArrayGfE* _M0L11gax__parentS848;
  struct _M0TP26RiantR8snn__mbt8Dendrite* _block_3078;
  #line 47 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_dendrite.mbt"
  #line 48 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_dendrite.mbt"
  _M0L2elS841
  = _M0MPC15array5Array4makeGfE(_M0L1nS842, -0x1.1a66666666666p+6f);
  #line 49 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_dendrite.mbt"
  _M0L1cS843 = _M0MPC15array5Array4makeGfE(_M0L1nS842, 0x1.4p+3f);
  #line 50 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_dendrite.mbt"
  _M0L3gaxS844 = _M0MPC15array5Array4makeGfE(_M0L1nS842, 0x1.4p+3f);
  #line 51 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_dendrite.mbt"
  _M0L2gmS845 = _M0MPC15array5Array4makeGfE(_M0L1nS842, 0x1p+0f);
  #line 52 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_dendrite.mbt"
  _M0L1lS846 = _M0MPC15array5Array4makeGfE(_M0L1nS842, 0x1.2cp+7f);
  #line 53 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_dendrite.mbt"
  _M0L1dS847 = _M0MPC15array5Array4makeGfE(_M0L1nS842, 0x1p+2f);
  #line 54 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_dendrite.mbt"
  _M0L11gax__parentS848 = _M0MPC15array5Array4makeGfE(_M0L1nS842, 0x0p+0f);
  _block_3078
  = (struct _M0TP26RiantR8snn__mbt8Dendrite*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt8Dendrite));
  Moonbit_object_header(_block_3078)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 87, 0);
  _block_3078->$0 = _M0L1nS842;
  _block_3078->$1 = _M0L2elS841;
  _block_3078->$2 = _M0L1cS843;
  _block_3078->$3 = _M0L3gaxS844;
  _block_3078->$4 = _M0L2gmS845;
  _block_3078->$5 = _M0L1lS846;
  _block_3078->$6 = _M0L1dS847;
  _block_3078->$7 = _M0L11gax__parentS848;
  return _block_3078;
}

struct _M0TP26RiantR8snn__mbt13AdExPostSpike* _M0MP26RiantR8snn__mbt13AdExPostSpike3new(
  
) {
  struct _M0TP26RiantR8snn__mbt13AdExPostSpike* _block_3079;
  #line 94 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
  _block_3079
  = (struct _M0TP26RiantR8snn__mbt13AdExPostSpike*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt13AdExPostSpike));
  Moonbit_object_header(_block_3079)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_3079->$0 = 0x0p+0f;
  _block_3079->$1 = 0x1.4p+3f;
  _block_3079->$2 = 0x1.4p+3f;
  _block_3079->$3 = 0x1p+0f;
  _block_3079->$4 = 0x1p+0f;
  return _block_3079;
}

struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0MP26RiantR8snn__mbt7Xoshiro7default(
  
) {
  #line 56 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  #line 57 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  return _M0MP26RiantR8snn__mbt7Xoshiro3new(0ull);
}

int32_t _M0FP26RiantR8snn__mbt17synaptic__current(
  struct _M0TP26RiantR8snn__mbt2IF* _M0L1pS837
) {
  int32_t _M0L1nS836;
  int32_t _M0L7_2abindS838;
  int32_t _M0L1iS839;
  #line 165 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L1nS836 = _M0L1pS837->$2;
  _M0L7_2abindS838 = 0;
  _M0L1iS839 = _M0L7_2abindS838;
  while (1) {
    if (_M0L1iS839 < _M0L1nS836) {
      struct _M0TPB5ArrayGfE* _M0L9syn__currS2194 = _M0L1pS837->$8;
      struct _M0TPB5ArrayGfE* _M0L2geS2215 = _M0L1pS837->$9;
      float _M0L6_2atmpS2210;
      struct _M0TPB5ArrayGfE* _M0L1vS2214;
      float _M0L6_2atmpS2212;
      float _M0L4e__eS2213;
      float _M0L6_2atmpS2211;
      float _M0L6_2atmpS2207;
      struct _M0TPB5ArrayGfE* _M0L7gsyn__eS2209;
      float _M0L6_2atmpS2208;
      float _M0L6_2atmpS2196;
      struct _M0TPB5ArrayGfE* _M0L2giS2206;
      float _M0L6_2atmpS2201;
      struct _M0TPB5ArrayGfE* _M0L1vS2205;
      float _M0L6_2atmpS2203;
      float _M0L4e__iS2204;
      float _M0L6_2atmpS2202;
      float _M0L6_2atmpS2198;
      struct _M0TPB5ArrayGfE* _M0L7gsyn__iS2200;
      float _M0L6_2atmpS2199;
      float _M0L6_2atmpS2197;
      float _M0L6_2atmpS2195;
      int32_t _M0L6_2atmpS2216;
      #line 168 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2210 = _M0MPC15array5Array2atGfE(_M0L2geS2215, _M0L1iS839);
      _M0L1vS2214 = _M0L1pS837->$3;
      #line 168 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2212 = _M0MPC15array5Array2atGfE(_M0L1vS2214, _M0L1iS839);
      _M0L4e__eS2213 = _M0L1pS837->$17;
      _M0L6_2atmpS2211 = _M0L6_2atmpS2212 - _M0L4e__eS2213;
      _M0L6_2atmpS2207 = _M0L6_2atmpS2210 * _M0L6_2atmpS2211;
      _M0L7gsyn__eS2209 = _M0L1pS837->$15;
      #line 168 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2208
      = _M0MPC15array5Array2atGfE(_M0L7gsyn__eS2209, _M0L1iS839);
      _M0L6_2atmpS2196 = _M0L6_2atmpS2207 * _M0L6_2atmpS2208;
      _M0L2giS2206 = _M0L1pS837->$10;
      #line 169 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2201 = _M0MPC15array5Array2atGfE(_M0L2giS2206, _M0L1iS839);
      _M0L1vS2205 = _M0L1pS837->$3;
      #line 169 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2203 = _M0MPC15array5Array2atGfE(_M0L1vS2205, _M0L1iS839);
      _M0L4e__iS2204 = _M0L1pS837->$18;
      _M0L6_2atmpS2202 = _M0L6_2atmpS2203 - _M0L4e__iS2204;
      _M0L6_2atmpS2198 = _M0L6_2atmpS2201 * _M0L6_2atmpS2202;
      _M0L7gsyn__iS2200 = _M0L1pS837->$16;
      #line 169 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2199
      = _M0MPC15array5Array2atGfE(_M0L7gsyn__iS2200, _M0L1iS839);
      _M0L6_2atmpS2197 = _M0L6_2atmpS2198 * _M0L6_2atmpS2199;
      _M0L6_2atmpS2195 = _M0L6_2atmpS2196 + _M0L6_2atmpS2197;
      #line 168 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L9syn__currS2194, _M0L1iS839, _M0L6_2atmpS2195);
      _M0L6_2atmpS2216 = _M0L1iS839 + 1;
      _M0L1iS839 = _M0L6_2atmpS2216;
      continue;
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt14step__synapses(
  struct _M0TP26RiantR8snn__mbt2IF* _M0L1pS828,
  float _M0L2dtS831
) {
  int32_t _M0L1nS827;
  int32_t _M0L7_2abindS829;
  int32_t _M0L1iS830;
  int32_t _M0L7_2abindS833;
  int32_t _M0L1iS834;
  #line 146 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L1nS827 = _M0L1pS828->$2;
  _M0L7_2abindS829 = 0;
  _M0L1iS830 = _M0L7_2abindS829;
  while (1) {
    if (_M0L1iS830 < _M0L1nS827) {
      struct _M0TPB5ArrayGfE* _M0L2heS2132 = _M0L1pS828->$11;
      struct _M0TPB5ArrayGfE* _M0L2heS2137 = _M0L1pS828->$11;
      float _M0L6_2atmpS2134;
      struct _M0TPB5ArrayGfE* _M0L3gluS2136;
      float _M0L6_2atmpS2135;
      float _M0L6_2atmpS2133;
      struct _M0TPB5ArrayGfE* _M0L2hiS2138;
      struct _M0TPB5ArrayGfE* _M0L2hiS2143;
      float _M0L6_2atmpS2140;
      struct _M0TPB5ArrayGfE* _M0L4gabaS2142;
      float _M0L6_2atmpS2141;
      float _M0L6_2atmpS2139;
      struct _M0TPB5ArrayGfE* _M0L2geS2144;
      struct _M0TPB5ArrayGfE* _M0L2geS2156;
      float _M0L6_2atmpS2146;
      struct _M0TPB5ArrayGfE* _M0L2geS2155;
      float _M0L6_2atmpS2154;
      float _M0L6_2atmpS2152;
      float _M0L3tdeS2153;
      float _M0L6_2atmpS2149;
      struct _M0TPB5ArrayGfE* _M0L2heS2151;
      float _M0L6_2atmpS2150;
      float _M0L6_2atmpS2148;
      float _M0L6_2atmpS2147;
      float _M0L6_2atmpS2145;
      struct _M0TPB5ArrayGfE* _M0L2heS2157;
      struct _M0TPB5ArrayGfE* _M0L2heS2166;
      float _M0L6_2atmpS2159;
      struct _M0TPB5ArrayGfE* _M0L2heS2165;
      float _M0L6_2atmpS2164;
      float _M0L6_2atmpS2162;
      float _M0L3treS2163;
      float _M0L6_2atmpS2161;
      float _M0L6_2atmpS2160;
      float _M0L6_2atmpS2158;
      struct _M0TPB5ArrayGfE* _M0L2giS2167;
      struct _M0TPB5ArrayGfE* _M0L2giS2179;
      float _M0L6_2atmpS2169;
      struct _M0TPB5ArrayGfE* _M0L2giS2178;
      float _M0L6_2atmpS2177;
      float _M0L6_2atmpS2175;
      float _M0L3tdiS2176;
      float _M0L6_2atmpS2172;
      struct _M0TPB5ArrayGfE* _M0L2hiS2174;
      float _M0L6_2atmpS2173;
      float _M0L6_2atmpS2171;
      float _M0L6_2atmpS2170;
      float _M0L6_2atmpS2168;
      struct _M0TPB5ArrayGfE* _M0L2hiS2180;
      struct _M0TPB5ArrayGfE* _M0L2hiS2189;
      float _M0L6_2atmpS2182;
      struct _M0TPB5ArrayGfE* _M0L2hiS2188;
      float _M0L6_2atmpS2187;
      float _M0L6_2atmpS2185;
      float _M0L3triS2186;
      float _M0L6_2atmpS2184;
      float _M0L6_2atmpS2183;
      float _M0L6_2atmpS2181;
      int32_t _M0L6_2atmpS2190;
      #line 149 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2134 = _M0MPC15array5Array2atGfE(_M0L2heS2137, _M0L1iS830);
      _M0L3gluS2136 = _M0L1pS828->$13;
      #line 149 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2135 = _M0MPC15array5Array2atGfE(_M0L3gluS2136, _M0L1iS830);
      _M0L6_2atmpS2133 = _M0L6_2atmpS2134 + _M0L6_2atmpS2135;
      #line 149 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L2heS2132, _M0L1iS830, _M0L6_2atmpS2133);
      _M0L2hiS2138 = _M0L1pS828->$12;
      _M0L2hiS2143 = _M0L1pS828->$12;
      #line 150 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2140 = _M0MPC15array5Array2atGfE(_M0L2hiS2143, _M0L1iS830);
      _M0L4gabaS2142 = _M0L1pS828->$14;
      #line 150 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2141
      = _M0MPC15array5Array2atGfE(_M0L4gabaS2142, _M0L1iS830);
      _M0L6_2atmpS2139 = _M0L6_2atmpS2140 + _M0L6_2atmpS2141;
      #line 150 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L2hiS2138, _M0L1iS830, _M0L6_2atmpS2139);
      _M0L2geS2144 = _M0L1pS828->$9;
      _M0L2geS2156 = _M0L1pS828->$9;
      #line 151 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2146 = _M0MPC15array5Array2atGfE(_M0L2geS2156, _M0L1iS830);
      _M0L2geS2155 = _M0L1pS828->$9;
      #line 151 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2154 = _M0MPC15array5Array2atGfE(_M0L2geS2155, _M0L1iS830);
      _M0L6_2atmpS2152 = -_M0L6_2atmpS2154;
      _M0L3tdeS2153 = _M0L1pS828->$20;
      _M0L6_2atmpS2149 = _M0L6_2atmpS2152 / _M0L3tdeS2153;
      _M0L2heS2151 = _M0L1pS828->$11;
      #line 151 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2150 = _M0MPC15array5Array2atGfE(_M0L2heS2151, _M0L1iS830);
      _M0L6_2atmpS2148 = _M0L6_2atmpS2149 + _M0L6_2atmpS2150;
      _M0L6_2atmpS2147 = _M0L2dtS831 * _M0L6_2atmpS2148;
      _M0L6_2atmpS2145 = _M0L6_2atmpS2146 + _M0L6_2atmpS2147;
      #line 151 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L2geS2144, _M0L1iS830, _M0L6_2atmpS2145);
      _M0L2heS2157 = _M0L1pS828->$11;
      _M0L2heS2166 = _M0L1pS828->$11;
      #line 152 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2159 = _M0MPC15array5Array2atGfE(_M0L2heS2166, _M0L1iS830);
      _M0L2heS2165 = _M0L1pS828->$11;
      #line 152 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2164 = _M0MPC15array5Array2atGfE(_M0L2heS2165, _M0L1iS830);
      _M0L6_2atmpS2162 = -_M0L6_2atmpS2164;
      _M0L3treS2163 = _M0L1pS828->$19;
      _M0L6_2atmpS2161 = _M0L6_2atmpS2162 / _M0L3treS2163;
      _M0L6_2atmpS2160 = _M0L2dtS831 * _M0L6_2atmpS2161;
      _M0L6_2atmpS2158 = _M0L6_2atmpS2159 + _M0L6_2atmpS2160;
      #line 152 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L2heS2157, _M0L1iS830, _M0L6_2atmpS2158);
      _M0L2giS2167 = _M0L1pS828->$10;
      _M0L2giS2179 = _M0L1pS828->$10;
      #line 153 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2169 = _M0MPC15array5Array2atGfE(_M0L2giS2179, _M0L1iS830);
      _M0L2giS2178 = _M0L1pS828->$10;
      #line 153 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2177 = _M0MPC15array5Array2atGfE(_M0L2giS2178, _M0L1iS830);
      _M0L6_2atmpS2175 = -_M0L6_2atmpS2177;
      _M0L3tdiS2176 = _M0L1pS828->$22;
      _M0L6_2atmpS2172 = _M0L6_2atmpS2175 / _M0L3tdiS2176;
      _M0L2hiS2174 = _M0L1pS828->$12;
      #line 153 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2173 = _M0MPC15array5Array2atGfE(_M0L2hiS2174, _M0L1iS830);
      _M0L6_2atmpS2171 = _M0L6_2atmpS2172 + _M0L6_2atmpS2173;
      _M0L6_2atmpS2170 = _M0L2dtS831 * _M0L6_2atmpS2171;
      _M0L6_2atmpS2168 = _M0L6_2atmpS2169 + _M0L6_2atmpS2170;
      #line 153 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L2giS2167, _M0L1iS830, _M0L6_2atmpS2168);
      _M0L2hiS2180 = _M0L1pS828->$12;
      _M0L2hiS2189 = _M0L1pS828->$12;
      #line 154 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2182 = _M0MPC15array5Array2atGfE(_M0L2hiS2189, _M0L1iS830);
      _M0L2hiS2188 = _M0L1pS828->$12;
      #line 154 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2187 = _M0MPC15array5Array2atGfE(_M0L2hiS2188, _M0L1iS830);
      _M0L6_2atmpS2185 = -_M0L6_2atmpS2187;
      _M0L3triS2186 = _M0L1pS828->$21;
      _M0L6_2atmpS2184 = _M0L6_2atmpS2185 / _M0L3triS2186;
      _M0L6_2atmpS2183 = _M0L2dtS831 * _M0L6_2atmpS2184;
      _M0L6_2atmpS2181 = _M0L6_2atmpS2182 + _M0L6_2atmpS2183;
      #line 154 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L2hiS2180, _M0L1iS830, _M0L6_2atmpS2181);
      _M0L6_2atmpS2190 = _M0L1iS830 + 1;
      _M0L1iS830 = _M0L6_2atmpS2190;
      continue;
    }
    break;
  }
  _M0L7_2abindS833 = 0;
  _M0L1iS834 = _M0L7_2abindS833;
  while (1) {
    if (_M0L1iS834 < _M0L1nS827) {
      struct _M0TPB5ArrayGfE* _M0L3gluS2191 = _M0L1pS828->$13;
      struct _M0TPB5ArrayGfE* _M0L4gabaS2192;
      int32_t _M0L6_2atmpS2193;
      #line 157 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L3gluS2191, _M0L1iS834, 0x0p+0f);
      _M0L4gabaS2192 = _M0L1pS828->$14;
      #line 158 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L4gabaS2192, _M0L1iS834, 0x0p+0f);
      _M0L6_2atmpS2193 = _M0L1iS834 + 1;
      _M0L1iS834 = _M0L6_2atmpS2193;
      continue;
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt12step__neuron(
  struct _M0TP26RiantR8snn__mbt2IF* _M0L1pS813,
  float _M0L2dtS822
) {
  int32_t _M0L1nS812;
  struct _M0TP26RiantR8snn__mbt11IFParameter* _M0L3p__S814;
  float _M0L2tmS815;
  float _M0L2elS816;
  float _M0L1rS817;
  float _M0L2vtS818;
  float _M0L2vrS819;
  struct _M0TP26RiantR8snn__mbt9PostSpike* _M0L5spikeS2131;
  float _M0L11tabs__constS820;
  float _M0L6_2atmpS2130;
  int32_t _M0L11tabs__stepsS821;
  int32_t _M0L7_2abindS823;
  int32_t _M0L1iS824;
  #line 176 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L1nS812 = _M0L1pS813->$2;
  _M0L3p__S814 = _M0L1pS813->$0;
  _M0L2tmS815 = _M0L3p__S814->$2;
  _M0L2elS816 = _M0L3p__S814->$5;
  _M0L1rS817 = _M0L3p__S814->$6;
  _M0L2vtS818 = _M0L3p__S814->$3;
  _M0L2vrS819 = _M0L3p__S814->$4;
  _M0L5spikeS2131 = _M0L1pS813->$1;
  _M0L11tabs__constS820 = _M0L5spikeS2131->$0;
  _M0L6_2atmpS2130 = _M0L11tabs__constS820 / _M0L2dtS822;
  #line 185 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L11tabs__stepsS821 = _M0MPC15float5Float7to__int(_M0L6_2atmpS2130);
  _M0L7_2abindS823 = 0;
  _M0L1iS824 = _M0L7_2abindS823;
  while (1) {
    if (_M0L1iS824 < _M0L1nS812) {
      struct _M0TPB5ArrayGiE* _M0L4tabsS2090 = _M0L1pS813->$6;
      int32_t _M0L6_2atmpS2089;
      struct _M0TPB5ArrayGfE* _M0L1vS2096;
      struct _M0TPB5ArrayGfE* _M0L1vS2117;
      float _M0L6_2atmpS2098;
      float _M0L6_2atmpS2100;
      struct _M0TPB5ArrayGfE* _M0L1vS2116;
      float _M0L6_2atmpS2115;
      float _M0L6_2atmpS2114;
      float _M0L6_2atmpS2106;
      struct _M0TPB5ArrayGfE* _M0L1wS2113;
      float _M0L6_2atmpS2112;
      float _M0L6_2atmpS2109;
      struct _M0TPB5ArrayGfE* _M0L1iS2111;
      float _M0L6_2atmpS2110;
      float _M0L6_2atmpS2108;
      float _M0L6_2atmpS2107;
      float _M0L6_2atmpS2102;
      struct _M0TPB5ArrayGfE* _M0L9syn__currS2105;
      float _M0L6_2atmpS2104;
      float _M0L6_2atmpS2103;
      float _M0L6_2atmpS2101;
      float _M0L6_2atmpS2099;
      float _M0L6_2atmpS2097;
      struct _M0TPB5ArrayGbE* _M0L4fireS2118;
      struct _M0TPB5ArrayGfE* _M0L1vS2121;
      float _M0L6_2atmpS2120;
      int32_t _M0L6_2atmpS2119;
      struct _M0TPB5ArrayGfE* _M0L1vS2122;
      struct _M0TPB5ArrayGbE* _M0L4fireS2124;
      float _M0L6_2atmpS2123;
      struct _M0TPB5ArrayGiE* _M0L4tabsS2126;
      struct _M0TPB5ArrayGbE* _M0L4fireS2128;
      int32_t _M0L6_2atmpS2127;
      int32_t _M0L6_2atmpS2088;
      #line 188 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2089
      = _M0MPC15array5Array2atGiE(_M0L4tabsS2090, _M0L1iS824);
      if (_M0L6_2atmpS2089 > 0) {
        struct _M0TPB5ArrayGbE* _M0L4fireS2091 = _M0L1pS813->$5;
        struct _M0TPB5ArrayGiE* _M0L4tabsS2092;
        struct _M0TPB5ArrayGiE* _M0L4tabsS2095;
        int32_t _M0L6_2atmpS2094;
        int32_t _M0L6_2atmpS2093;
        #line 189 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
        _M0MPC15array5Array3setGbE(_M0L4fireS2091, _M0L1iS824, 0);
        _M0L4tabsS2092 = _M0L1pS813->$6;
        _M0L4tabsS2095 = _M0L1pS813->$6;
        #line 190 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
        _M0L6_2atmpS2094
        = _M0MPC15array5Array2atGiE(_M0L4tabsS2095, _M0L1iS824);
        _M0L6_2atmpS2093 = _M0L6_2atmpS2094 - 1;
        #line 190 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
        _M0MPC15array5Array3setGiE(_M0L4tabsS2092, _M0L1iS824, _M0L6_2atmpS2093);
        goto join_825;
      }
      _M0L1vS2096 = _M0L1pS813->$3;
      _M0L1vS2117 = _M0L1pS813->$3;
      #line 193 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2098 = _M0MPC15array5Array2atGfE(_M0L1vS2117, _M0L1iS824);
      _M0L6_2atmpS2100 = _M0L2dtS822 / _M0L2tmS815;
      _M0L1vS2116 = _M0L1pS813->$3;
      #line 194 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2115 = _M0MPC15array5Array2atGfE(_M0L1vS2116, _M0L1iS824);
      _M0L6_2atmpS2114 = _M0L6_2atmpS2115 - _M0L2elS816;
      _M0L6_2atmpS2106 = -_M0L6_2atmpS2114;
      _M0L1wS2113 = _M0L1pS813->$4;
      #line 194 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2112 = _M0MPC15array5Array2atGfE(_M0L1wS2113, _M0L1iS824);
      _M0L6_2atmpS2109 = -_M0L6_2atmpS2112;
      _M0L1iS2111 = _M0L1pS813->$7;
      #line 194 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2110 = _M0MPC15array5Array2atGfE(_M0L1iS2111, _M0L1iS824);
      _M0L6_2atmpS2108 = _M0L6_2atmpS2109 + _M0L6_2atmpS2110;
      _M0L6_2atmpS2107 = _M0L1rS817 * _M0L6_2atmpS2108;
      _M0L6_2atmpS2102 = _M0L6_2atmpS2106 + _M0L6_2atmpS2107;
      _M0L9syn__currS2105 = _M0L1pS813->$8;
      #line 194 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2104
      = _M0MPC15array5Array2atGfE(_M0L9syn__currS2105, _M0L1iS824);
      _M0L6_2atmpS2103 = _M0L1rS817 * _M0L6_2atmpS2104;
      _M0L6_2atmpS2101 = _M0L6_2atmpS2102 - _M0L6_2atmpS2103;
      _M0L6_2atmpS2099 = _M0L6_2atmpS2100 * _M0L6_2atmpS2101;
      _M0L6_2atmpS2097 = _M0L6_2atmpS2098 + _M0L6_2atmpS2099;
      #line 193 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS2096, _M0L1iS824, _M0L6_2atmpS2097);
      _M0L4fireS2118 = _M0L1pS813->$5;
      _M0L1vS2121 = _M0L1pS813->$3;
      #line 195 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2120 = _M0MPC15array5Array2atGfE(_M0L1vS2121, _M0L1iS824);
      _M0L6_2atmpS2119 = _M0L6_2atmpS2120 > _M0L2vtS818;
      #line 195 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGbE(_M0L4fireS2118, _M0L1iS824, _M0L6_2atmpS2119);
      _M0L1vS2122 = _M0L1pS813->$3;
      _M0L4fireS2124 = _M0L1pS813->$5;
      #line 196 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS2124, _M0L1iS824)) {
        _M0L6_2atmpS2123 = _M0L2vrS819;
      } else {
        struct _M0TPB5ArrayGfE* _M0L1vS2125 = _M0L1pS813->$3;
        #line 196 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
        _M0L6_2atmpS2123 = _M0MPC15array5Array2atGfE(_M0L1vS2125, _M0L1iS824);
      }
      #line 196 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS2122, _M0L1iS824, _M0L6_2atmpS2123);
      _M0L4tabsS2126 = _M0L1pS813->$6;
      _M0L4fireS2128 = _M0L1pS813->$5;
      #line 197 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS2128, _M0L1iS824)) {
        _M0L6_2atmpS2127 = _M0L11tabs__stepsS821;
      } else {
        struct _M0TPB5ArrayGiE* _M0L4tabsS2129 = _M0L1pS813->$6;
        #line 197 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
        _M0L6_2atmpS2127
        = _M0MPC15array5Array2atGiE(_M0L4tabsS2129, _M0L1iS824);
      }
      #line 197 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGiE(_M0L4tabsS2126, _M0L1iS824, _M0L6_2atmpS2127);
      goto join_825;
      goto joinlet_3084;
      join_825:;
      _M0L6_2atmpS2088 = _M0L1iS824 + 1;
      _M0L1iS824 = _M0L6_2atmpS2088;
      continue;
      joinlet_3084:;
    }
    break;
  }
  return 0;
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
      int32_t _M0L6_2atmpS2087;
      #line 294 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L9pre__fireS803, _M0L1iS802)) {
        struct _M0TPB5ArrayGiE* _M0L6rowptrS2086 = _M0L1mS800->$2;
        int32_t _M0L5startS804;
        struct _M0TPB5ArrayGiE* _M0L6rowptrS2084;
        int32_t _M0L6_2atmpS2085;
        int32_t _M0L3endS805;
        int32_t _M0L1kS806;
        #line 295 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
        _M0L5startS804
        = _M0MPC15array5Array2atGiE(_M0L6rowptrS2086, _M0L1iS802);
        _M0L6rowptrS2084 = _M0L1mS800->$2;
        _M0L6_2atmpS2085 = _M0L1iS802 + 1;
        #line 296 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
        _M0L3endS805
        = _M0MPC15array5Array2atGiE(_M0L6rowptrS2084, _M0L6_2atmpS2085);
        _M0L1kS806 = _M0L5startS804;
        while (1) {
          if (_M0L1kS806 < _M0L3endS805) {
            struct _M0TPB5ArrayGiE* _M0L6colptrS2082 = _M0L1mS800->$3;
            int32_t _M0L9post__idxS807;
            struct _M0TPB5ArrayGfE* _M0L4valsS2081;
            float _M0L1wS808;
            float _M0L6_2atmpS2080;
            float _M0L6_2atmpS2079;
            int32_t _M0L6_2atmpS2083;
            #line 298 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
            _M0L9post__idxS807
            = _M0MPC15array5Array2atGiE(_M0L6colptrS2082, _M0L1kS806);
            _M0L4valsS2081 = _M0L1mS800->$4;
            #line 299 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
            _M0L1wS808
            = _M0MPC15array5Array2atGfE(_M0L4valsS2081, _M0L1kS806);
            #line 300 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
            _M0L6_2atmpS2080
            = _M0MPC15array5Array2atGfE(_M0L7post__gS809, _M0L9post__idxS807);
            _M0L6_2atmpS2079 = _M0L6_2atmpS2080 + _M0L1wS808;
            #line 300 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
            _M0MPC15array5Array3setGfE(_M0L7post__gS809, _M0L9post__idxS807, _M0L6_2atmpS2079);
            _M0L6_2atmpS2083 = _M0L1kS806 + 1;
            _M0L1kS806 = _M0L6_2atmpS2083;
            continue;
          }
          break;
        }
      }
      _M0L6_2atmpS2087 = _M0L1iS802 + 1;
      _M0L1iS802 = _M0L6_2atmpS2087;
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
  float* _M0L6_2atmpS2078;
  struct _M0TPB5ArrayGfE* _M0L6_2atmpS2077;
  struct _M0TPB5ArrayGRPB5ArrayGfEE* _M0L5denseS706;
  int32_t _M0L7_2abindS708;
  int32_t _M0L1iS709;
  int32_t _M0L6_2atmpS2076;
  struct _M0TPB5ArrayGiE* _M0L6rowptrS783;
  int32_t* _M0L6_2atmpS2075;
  struct _M0TPB5ArrayGiE* _M0L6colptrS784;
  float* _M0L6_2atmpS2074;
  struct _M0TPB5ArrayGfE* _M0L4valsS785;
  int32_t _M0L7_2abindS786;
  int32_t _M0L1iS787;
  int32_t _M0L6_2atmpS2073;
  struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _block_3106;
  #line 109 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  _M0L6_2atmpS2078 = moonbit_empty_float_array;
  _M0L6_2atmpS2077
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_M0L6_2atmpS2077)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 0, 0);
  _M0L6_2atmpS2077->$0 = _M0L6_2atmpS2078;
  _M0L6_2atmpS2077->$1 = 0;
  #line 120 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  _M0L5denseS706
  = _M0MPC15array5Array4makeGRPB5ArrayGfEE(_M0L4rowsS707, _M0L6_2atmpS2077);
  _M0L7_2abindS708 = 0;
  _M0L1iS709 = _M0L7_2abindS708;
  while (1) {
    if (_M0L1iS709 < _M0L4rowsS707) {
      struct _M0TPB5ArrayGfE* _M0L3rowS710;
      int32_t _M0L7_2abindS712;
      int32_t _M0L1jS713;
      int32_t _M0L6_2atmpS2031;
      #line 122 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
      _M0L3rowS710 = _M0MPC15array5Array4makeGfE(_M0L4colsS711, 0x0p+0f);
      _M0L7_2abindS712 = 0;
      _M0L1jS713 = _M0L7_2abindS712;
      while (1) {
        if (_M0L1jS713 < _M0L4colsS711) {
          double _M0L2z1S715;
          struct _M0TUddE* _M0L7_2abindS719;
          double _M0L5_2az1S721;
          float _M0L6_2atmpS2029;
          float _M0L6_2atmpS2028;
          float _M0L1wS716;
          int32_t _M0L6_2atmpS2030;
          #line 131 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
          _M0L7_2abindS719
          = _M0FP26RiantR8snn__mbt11box__muller(_M0L3rngS720);
          _M0L5_2az1S721 = _M0L7_2abindS719->$0;
          moonbit_decref(_M0L7_2abindS719);
          _M0L2z1S715 = _M0L5_2az1S721;
          goto join_714;
          goto joinlet_3089;
          join_714:;
          _M0L6_2atmpS2029 = (float)_M0L2z1S715;
          _M0L6_2atmpS2028 = _M0L5sigmaS718 * _M0L6_2atmpS2029;
          _M0L1wS716 = _M0L2muS717 + _M0L6_2atmpS2028;
          #line 133 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
          _M0MPC15array5Array3setGfE(_M0L3rowS710, _M0L1jS713, _M0L1wS716);
          joinlet_3089:;
          _M0L6_2atmpS2030 = _M0L1jS713 + 1;
          _M0L1jS713 = _M0L6_2atmpS2030;
          continue;
        }
        break;
      }
      #line 135 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
      _M0MPC15array5Array3setGRPB5ArrayGfEE(_M0L5denseS706, _M0L1iS709, _M0L3rowS710);
      _M0L6_2atmpS2031 = _M0L1iS709 + 1;
      _M0L1iS709 = _M0L6_2atmpS2031;
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
          int32_t _M0L6_2atmpS2034;
          while (1) {
            if (_M0L1jS728 < _M0L4colsS711) {
              float _M0L1uS729;
              int32_t _M0L6_2atmpS2033;
              #line 143 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
              _M0L1uS729 = _M0FP26RiantR8snn__mbt9next__f32(_M0L3rngS720);
              if (_M0L1uS729 >= _M0L1pS730) {
                struct _M0TPB5ArrayGfE* _M0L6_2atmpS2032;
                #line 145 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS2032
                = _M0MPC15array5Array2atGRPB5ArrayGfEE(_M0L5denseS706, _M0L1iS726);
                #line 145 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array3setGfE(_M0L6_2atmpS2032, _M0L1jS728, 0x0p+0f);
                moonbit_decref(_M0L6_2atmpS2032);
              }
              _M0L6_2atmpS2033 = _M0L1jS728 + 1;
              _M0L1jS728 = _M0L6_2atmpS2033;
              continue;
            }
            break;
          }
          _M0L6_2atmpS2034 = _M0L1iS726 + 1;
          _M0L1iS726 = _M0L6_2atmpS2034;
          continue;
        }
        break;
      }
      break;
    }
    
    case 1: {
      float _M0L6_2atmpS2051 = (float)_M0L4rowsS707;
      float _M0L6_2atmpS2050 = _M0L6_2atmpS2051 * _M0L1pS730;
      int32_t _M0L7n__keepS733;
      #line 154 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
      _M0L7n__keepS733 = _M0MPC15float5Float7to__int(_M0L6_2atmpS2050);
      if (_M0L7n__keepS733 > 0 && _M0L7n__keepS733 <= _M0L4rowsS707) {
        int32_t _M0L7_2abindS734 = 0;
        int32_t _M0L1jS735 = _M0L7_2abindS734;
        while (1) {
          if (_M0L1jS735 < _M0L4colsS711) {
            int32_t* _M0L6_2atmpS2045 = (int32_t*)moonbit_empty_int32_array;
            struct _M0TPB5ArrayGiE* _M0L8pre__idxS736 =
              (struct _M0TPB5ArrayGiE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGiE));
            int32_t _M0L7_2abindS737;
            int32_t _M0L1kS738;
            int32_t _M0L7n__dropS740;
            int32_t _M0L7_2abindS741;
            int32_t _M0L1kS742;
            int32_t _M0L7_2abindS748;
            int32_t _M0L1kS749;
            int32_t _M0L6_2atmpS2046;
            Moonbit_object_header(_M0L8pre__idxS736)->meta
            = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 3, 0);
            _M0L8pre__idxS736->$0 = _M0L6_2atmpS2045;
            _M0L8pre__idxS736->$1 = 0;
            _M0L7_2abindS737 = 0;
            _M0L1kS738 = _M0L7_2abindS737;
            while (1) {
              if (_M0L1kS738 < _M0L4rowsS707) {
                int32_t _M0L6_2atmpS2035;
                #line 161 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array4pushGiE(_M0L8pre__idxS736, _M0L1kS738);
                _M0L6_2atmpS2035 = _M0L1kS738 + 1;
                _M0L1kS738 = _M0L6_2atmpS2035;
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
                int32_t _M0L6_2atmpS2040;
                float _M0L6_2atmpS2039;
                float _M0L6_2atmpS2038;
                int32_t _M0L6_2atmpS2037;
                int32_t _M0L6r__idxS744;
                int32_t _M0L10r__clampedS745;
                int32_t _M0L3tmpS746;
                int32_t _M0L6_2atmpS2036;
                int32_t _M0L6_2atmpS2041;
                #line 167 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L1uS743 = _M0FP26RiantR8snn__mbt9next__f32(_M0L3rngS720);
                _M0L6_2atmpS2040 = _M0L4rowsS707 - _M0L1kS742;
                _M0L6_2atmpS2039 = (float)_M0L6_2atmpS2040;
                _M0L6_2atmpS2038 = _M0L6_2atmpS2039 * _M0L1uS743;
                #line 168 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS2037
                = _M0MPC15float5Float7to__int(_M0L6_2atmpS2038);
                _M0L6r__idxS744 = _M0L1kS742 + _M0L6_2atmpS2037;
                if (_M0L6r__idxS744 >= _M0L4rowsS707) {
                  _M0L10r__clampedS745 = _M0L4rowsS707 - 1;
                } else {
                  _M0L10r__clampedS745 = _M0L6r__idxS744;
                }
                #line 171 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L3tmpS746
                = _M0MPC15array5Array2atGiE(_M0L8pre__idxS736, _M0L1kS742);
                #line 172 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS2036
                = _M0MPC15array5Array2atGiE(_M0L8pre__idxS736, _M0L10r__clampedS745);
                #line 172 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array3setGiE(_M0L8pre__idxS736, _M0L1kS742, _M0L6_2atmpS2036);
                #line 173 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array3setGiE(_M0L8pre__idxS736, _M0L10r__clampedS745, _M0L3tmpS746);
                _M0L6_2atmpS2041 = _M0L1kS742 + 1;
                _M0L1kS742 = _M0L6_2atmpS2041;
                continue;
              }
              break;
            }
            _M0L7_2abindS748 = 0;
            _M0L1kS749 = _M0L7_2abindS748;
            while (1) {
              if (_M0L1kS749 < _M0L7n__dropS740) {
                int32_t _M0L6_2atmpS2043;
                struct _M0TPB5ArrayGfE* _M0L6_2atmpS2042;
                int32_t _M0L6_2atmpS2044;
                #line 176 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS2043
                = _M0MPC15array5Array2atGiE(_M0L8pre__idxS736, _M0L1kS749);
                #line 176 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS2042
                = _M0MPC15array5Array2atGRPB5ArrayGfEE(_M0L5denseS706, _M0L6_2atmpS2043);
                #line 176 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array3setGfE(_M0L6_2atmpS2042, _M0L1jS735, 0x0p+0f);
                moonbit_decref(_M0L6_2atmpS2042);
                _M0L6_2atmpS2044 = _M0L1kS749 + 1;
                _M0L1kS749 = _M0L6_2atmpS2044;
                continue;
              } else {
                moonbit_decref(_M0L8pre__idxS736);
              }
              break;
            }
            _M0L6_2atmpS2046 = _M0L1jS735 + 1;
            _M0L1jS735 = _M0L6_2atmpS2046;
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
            int32_t _M0L6_2atmpS2049;
            while (1) {
              if (_M0L1jS755 < _M0L4colsS711) {
                struct _M0TPB5ArrayGfE* _M0L6_2atmpS2047;
                int32_t _M0L6_2atmpS2048;
                #line 183 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS2047
                = _M0MPC15array5Array2atGRPB5ArrayGfEE(_M0L5denseS706, _M0L1iS753);
                #line 183 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array3setGfE(_M0L6_2atmpS2047, _M0L1jS755, 0x0p+0f);
                moonbit_decref(_M0L6_2atmpS2047);
                _M0L6_2atmpS2048 = _M0L1jS755 + 1;
                _M0L1jS755 = _M0L6_2atmpS2048;
                continue;
              }
              break;
            }
            _M0L6_2atmpS2049 = _M0L1iS753 + 1;
            _M0L1iS753 = _M0L6_2atmpS2049;
            continue;
          }
          break;
        }
      }
      break;
    }
    default: {
      float _M0L6_2atmpS2068 = (float)_M0L4colsS711;
      float _M0L6_2atmpS2067 = _M0L6_2atmpS2068 * _M0L1pS730;
      int32_t _M0L7n__keepS758;
      #line 191 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
      _M0L7n__keepS758 = _M0MPC15float5Float7to__int(_M0L6_2atmpS2067);
      if (_M0L7n__keepS758 > 0 && _M0L7n__keepS758 <= _M0L4colsS711) {
        int32_t _M0L7_2abindS759 = 0;
        int32_t _M0L1iS760 = _M0L7_2abindS759;
        while (1) {
          if (_M0L1iS760 < _M0L4rowsS707) {
            int32_t* _M0L6_2atmpS2062 = (int32_t*)moonbit_empty_int32_array;
            struct _M0TPB5ArrayGiE* _M0L9post__idxS761 =
              (struct _M0TPB5ArrayGiE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGiE));
            int32_t _M0L7_2abindS762;
            int32_t _M0L1kS763;
            int32_t _M0L7n__dropS765;
            int32_t _M0L7_2abindS766;
            int32_t _M0L1kS767;
            int32_t _M0L7_2abindS773;
            int32_t _M0L1kS774;
            int32_t _M0L6_2atmpS2063;
            Moonbit_object_header(_M0L9post__idxS761)->meta
            = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 3, 0);
            _M0L9post__idxS761->$0 = _M0L6_2atmpS2062;
            _M0L9post__idxS761->$1 = 0;
            _M0L7_2abindS762 = 0;
            _M0L1kS763 = _M0L7_2abindS762;
            while (1) {
              if (_M0L1kS763 < _M0L4colsS711) {
                int32_t _M0L6_2atmpS2052;
                #line 196 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array4pushGiE(_M0L9post__idxS761, _M0L1kS763);
                _M0L6_2atmpS2052 = _M0L1kS763 + 1;
                _M0L1kS763 = _M0L6_2atmpS2052;
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
                int32_t _M0L6_2atmpS2057;
                float _M0L6_2atmpS2056;
                float _M0L6_2atmpS2055;
                int32_t _M0L6_2atmpS2054;
                int32_t _M0L6r__idxS769;
                int32_t _M0L10r__clampedS770;
                int32_t _M0L3tmpS771;
                int32_t _M0L6_2atmpS2053;
                int32_t _M0L6_2atmpS2058;
                #line 200 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L1uS768 = _M0FP26RiantR8snn__mbt9next__f32(_M0L3rngS720);
                _M0L6_2atmpS2057 = _M0L4colsS711 - _M0L1kS767;
                _M0L6_2atmpS2056 = (float)_M0L6_2atmpS2057;
                _M0L6_2atmpS2055 = _M0L6_2atmpS2056 * _M0L1uS768;
                #line 201 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS2054
                = _M0MPC15float5Float7to__int(_M0L6_2atmpS2055);
                _M0L6r__idxS769 = _M0L1kS767 + _M0L6_2atmpS2054;
                if (_M0L6r__idxS769 >= _M0L4colsS711) {
                  _M0L10r__clampedS770 = _M0L4colsS711 - 1;
                } else {
                  _M0L10r__clampedS770 = _M0L6r__idxS769;
                }
                #line 203 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L3tmpS771
                = _M0MPC15array5Array2atGiE(_M0L9post__idxS761, _M0L1kS767);
                #line 204 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS2053
                = _M0MPC15array5Array2atGiE(_M0L9post__idxS761, _M0L10r__clampedS770);
                #line 204 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array3setGiE(_M0L9post__idxS761, _M0L1kS767, _M0L6_2atmpS2053);
                #line 205 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array3setGiE(_M0L9post__idxS761, _M0L10r__clampedS770, _M0L3tmpS771);
                _M0L6_2atmpS2058 = _M0L1kS767 + 1;
                _M0L1kS767 = _M0L6_2atmpS2058;
                continue;
              }
              break;
            }
            _M0L7_2abindS773 = 0;
            _M0L1kS774 = _M0L7_2abindS773;
            while (1) {
              if (_M0L1kS774 < _M0L7n__dropS765) {
                struct _M0TPB5ArrayGfE* _M0L6_2atmpS2059;
                int32_t _M0L6_2atmpS2060;
                int32_t _M0L6_2atmpS2061;
                #line 208 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS2059
                = _M0MPC15array5Array2atGRPB5ArrayGfEE(_M0L5denseS706, _M0L1iS760);
                #line 208 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS2060
                = _M0MPC15array5Array2atGiE(_M0L9post__idxS761, _M0L1kS774);
                #line 208 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array3setGfE(_M0L6_2atmpS2059, _M0L6_2atmpS2060, 0x0p+0f);
                moonbit_decref(_M0L6_2atmpS2059);
                _M0L6_2atmpS2061 = _M0L1kS774 + 1;
                _M0L1kS774 = _M0L6_2atmpS2061;
                continue;
              } else {
                moonbit_decref(_M0L9post__idxS761);
              }
              break;
            }
            _M0L6_2atmpS2063 = _M0L1iS760 + 1;
            _M0L1iS760 = _M0L6_2atmpS2063;
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
            int32_t _M0L6_2atmpS2066;
            while (1) {
              if (_M0L1jS780 < _M0L4colsS711) {
                struct _M0TPB5ArrayGfE* _M0L6_2atmpS2064;
                int32_t _M0L6_2atmpS2065;
                #line 214 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS2064
                = _M0MPC15array5Array2atGRPB5ArrayGfEE(_M0L5denseS706, _M0L1iS778);
                #line 214 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array3setGfE(_M0L6_2atmpS2064, _M0L1jS780, 0x0p+0f);
                moonbit_decref(_M0L6_2atmpS2064);
                _M0L6_2atmpS2065 = _M0L1jS780 + 1;
                _M0L1jS780 = _M0L6_2atmpS2065;
                continue;
              }
              break;
            }
            _M0L6_2atmpS2066 = _M0L1iS778 + 1;
            _M0L1iS778 = _M0L6_2atmpS2066;
            continue;
          }
          break;
        }
      }
      break;
    }
  }
  _M0L6_2atmpS2076 = _M0L4rowsS707 + 1;
  #line 221 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  _M0L6rowptrS783 = _M0MPC15array5Array4makeGiE(_M0L6_2atmpS2076, 0);
  _M0L6_2atmpS2075 = (int32_t*)moonbit_empty_int32_array;
  _M0L6colptrS784
  = (struct _M0TPB5ArrayGiE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGiE));
  Moonbit_object_header(_M0L6colptrS784)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 3, 0);
  _M0L6colptrS784->$0 = _M0L6_2atmpS2075;
  _M0L6colptrS784->$1 = 0;
  _M0L6_2atmpS2074 = moonbit_empty_float_array;
  _M0L4valsS785
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_M0L4valsS785)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 0, 0);
  _M0L4valsS785->$0 = _M0L6_2atmpS2074;
  _M0L4valsS785->$1 = 0;
  _M0L7_2abindS786 = 0;
  _M0L1iS787 = _M0L7_2abindS786;
  while (1) {
    if (_M0L1iS787 < _M0L4rowsS707) {
      int32_t _M0L6_2atmpS2069;
      int32_t _M0L7_2abindS788;
      int32_t _M0L1jS789;
      int32_t _M0L6_2atmpS2072;
      #line 225 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
      _M0L6_2atmpS2069 = _M0MPC15array5Array6lengthGfE(_M0L4valsS785);
      #line 225 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
      _M0MPC15array5Array3setGiE(_M0L6rowptrS783, _M0L1iS787, _M0L6_2atmpS2069);
      _M0L7_2abindS788 = 0;
      _M0L1jS789 = _M0L7_2abindS788;
      while (1) {
        if (_M0L1jS789 < _M0L4colsS711) {
          struct _M0TPB5ArrayGfE* _M0L6_2atmpS2070;
          float _M0L1vS790;
          int32_t _M0L6_2atmpS2071;
          #line 227 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
          _M0L6_2atmpS2070
          = _M0MPC15array5Array2atGRPB5ArrayGfEE(_M0L5denseS706, _M0L1iS787);
          #line 227 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
          _M0L1vS790
          = _M0MPC15array5Array2atGfE(_M0L6_2atmpS2070, _M0L1jS789);
          moonbit_decref(_M0L6_2atmpS2070);
          if (_M0L1vS790 != 0x0p+0f) {
            #line 229 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
            _M0MPC15array5Array4pushGiE(_M0L6colptrS784, _M0L1jS789);
            #line 230 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
            _M0MPC15array5Array4pushGfE(_M0L4valsS785, _M0L1vS790);
          }
          _M0L6_2atmpS2071 = _M0L1jS789 + 1;
          _M0L1jS789 = _M0L6_2atmpS2071;
          continue;
        }
        break;
      }
      _M0L6_2atmpS2072 = _M0L1iS787 + 1;
      _M0L1iS787 = _M0L6_2atmpS2072;
      continue;
    } else {
      moonbit_decref(_M0L5denseS706);
    }
    break;
  }
  #line 234 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  _M0L6_2atmpS2073 = _M0MPC15array5Array6lengthGfE(_M0L4valsS785);
  #line 234 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  _M0MPC15array5Array3setGiE(_M0L6rowptrS783, _M0L4rowsS707, _M0L6_2atmpS2073);
  _block_3106
  = (struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR));
  Moonbit_object_header(_block_3106)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 96, 0);
  _block_3106->$0 = _M0L4rowsS707;
  _block_3106->$1 = _M0L4colsS711;
  _block_3106->$2 = _M0L6rowptrS783;
  _block_3106->$3 = _M0L6colptrS784;
  _block_3106->$4 = _M0L4valsS785;
  return _block_3106;
}

struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0MP26RiantR8snn__mbt7Xoshiro3new(
  uint64_t _M0L4seedS704
) {
  struct _M0TUmmmmE* _M0L1sS703;
  uint64_t _M0L6_2atmpS2027;
  struct _M0TUmmmmE* _M0L1tS705;
  uint64_t _M0L6_2atmpS2023;
  uint64_t _M0L6_2atmpS2024;
  uint64_t _M0L6_2atmpS2025;
  uint64_t _M0L6_2atmpS2026;
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _block_3107;
  #line 48 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  #line 49 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L1sS703 = _M0FP26RiantR8snn__mbt10splitmix64(_M0L4seedS704);
  _M0L6_2atmpS2027 = _M0L1sS703->$3;
  #line 50 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L1tS705 = _M0FP26RiantR8snn__mbt10splitmix64(_M0L6_2atmpS2027);
  _M0L6_2atmpS2023 = _M0L1sS703->$0;
  _M0L6_2atmpS2024 = _M0L1sS703->$1;
  _M0L6_2atmpS2025 = _M0L1sS703->$2;
  moonbit_decref(_M0L1sS703);
  _M0L6_2atmpS2026 = _M0L1tS705->$0;
  moonbit_decref(_M0L1tS705);
  _block_3107
  = (struct _M0TP26RiantR8snn__mbt7Xoshiro*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt7Xoshiro));
  Moonbit_object_header(_block_3107)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_3107->$0 = _M0L6_2atmpS2023;
  _block_3107->$1 = _M0L6_2atmpS2024;
  _block_3107->$2 = _M0L6_2atmpS2025;
  _block_3107->$3 = _M0L6_2atmpS2026;
  return _block_3107;
}

struct _M0TUmmmmE* _M0FP26RiantR8snn__mbt10splitmix64(uint64_t _M0L4seedS695) {
  uint64_t _M0L2s1S694;
  uint64_t _M0L2z1S696;
  uint64_t _M0L2s2S697;
  uint64_t _M0L2z2S698;
  uint64_t _M0L2s3S699;
  uint64_t _M0L2z3S700;
  uint64_t _M0L2s4S701;
  uint64_t _M0L2z4S702;
  struct _M0TUmmmmE* _block_3108;
  #line 62 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2s1S694 = _M0L4seedS695 + 11400714819323198485ull;
  #line 64 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2z1S696 = _M0FP26RiantR8snn__mbt5mix64(_M0L2s1S694);
  _M0L2s2S697 = _M0L2s1S694 + 11400714819323198485ull;
  #line 66 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2z2S698 = _M0FP26RiantR8snn__mbt5mix64(_M0L2s2S697);
  _M0L2s3S699 = _M0L2s2S697 + 11400714819323198485ull;
  #line 68 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2z3S700 = _M0FP26RiantR8snn__mbt5mix64(_M0L2s3S699);
  _M0L2s4S701 = _M0L2s3S699 + 11400714819323198485ull;
  #line 70 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2z4S702 = _M0FP26RiantR8snn__mbt5mix64(_M0L2s4S701);
  _block_3108 = (struct _M0TUmmmmE*)moonbit_malloc(sizeof(struct _M0TUmmmmE));
  Moonbit_object_header(_block_3108)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_3108->$0 = _M0L2z1S696;
  _block_3108->$1 = _M0L2z2S698;
  _block_3108->$2 = _M0L2z3S700;
  _block_3108->$3 = _M0L2z4S702;
  return _block_3108;
}

uint64_t _M0FP26RiantR8snn__mbt5mix64(uint64_t _M0L1zS692) {
  uint64_t _M0L6_2atmpS2022;
  uint64_t _M0L6_2atmpS2021;
  uint64_t _M0L1zS691;
  uint64_t _M0L6_2atmpS2020;
  uint64_t _M0L6_2atmpS2019;
  uint64_t _M0L1zS693;
  uint64_t _M0L6_2atmpS2018;
  #line 75 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L6_2atmpS2022 = _M0L1zS692 >> 30;
  _M0L6_2atmpS2021 = _M0L1zS692 ^ _M0L6_2atmpS2022;
  _M0L1zS691 = _M0L6_2atmpS2021 * 13787848793156543929ull;
  _M0L6_2atmpS2020 = _M0L1zS691 >> 27;
  _M0L6_2atmpS2019 = _M0L1zS691 ^ _M0L6_2atmpS2020;
  _M0L1zS693 = _M0L6_2atmpS2019 * 10723151780598845931ull;
  _M0L6_2atmpS2018 = _M0L1zS693 >> 31;
  return _M0L1zS693 ^ _M0L6_2atmpS2018;
}

struct _M0TUddE* _M0FP26RiantR8snn__mbt11box__muller(
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS686
) {
  double _M0L2u1S685;
  double _M0L8u1__safeS687;
  double _M0L2u2S688;
  double _M0L6_2atmpS2017;
  double _M0L6_2atmpS2016;
  double _M0L1rS689;
  double _M0L5thetaS690;
  double _M0L6_2atmpS2015;
  double _M0L6_2atmpS2012;
  double _M0L6_2atmpS2014;
  double _M0L6_2atmpS2013;
  struct _M0TUddE* _block_3109;
  #line 126 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  #line 127 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  _M0L2u1S685 = _M0FP26RiantR8snn__mbt9next__f64(_M0L3rngS686);
  if (_M0L2u1S685 < 0x1.203af9ee75616p-50) {
    _M0L8u1__safeS687 = 0x1.203af9ee75616p-50;
  } else {
    _M0L8u1__safeS687 = _M0L2u1S685;
  }
  #line 130 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  _M0L2u2S688 = _M0FP26RiantR8snn__mbt9next__f64(_M0L3rngS686);
  #line 131 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  _M0L6_2atmpS2017 = _M0FPC14math2ln(_M0L8u1__safeS687);
  _M0L6_2atmpS2016 = -0x1p+1 * _M0L6_2atmpS2017;
  #line 131 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  _M0L1rS689 = sqrt(_M0L6_2atmpS2016);
  _M0L5thetaS690 = 0x1.921fb54442d18p+2 * _M0L2u2S688;
  #line 133 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  _M0L6_2atmpS2015 = _M0FPC14math3cos(_M0L5thetaS690);
  _M0L6_2atmpS2012 = _M0L1rS689 * _M0L6_2atmpS2015;
  #line 133 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  _M0L6_2atmpS2014 = _M0FPC14math3sin(_M0L5thetaS690);
  _M0L6_2atmpS2013 = _M0L1rS689 * _M0L6_2atmpS2014;
  _block_3109 = (struct _M0TUddE*)moonbit_malloc(sizeof(struct _M0TUddE));
  Moonbit_object_header(_block_3109)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_3109->$0 = _M0L6_2atmpS2012;
  _block_3109->$1 = _M0L6_2atmpS2013;
  return _block_3109;
}

double _M0FP26RiantR8snn__mbt9next__f64(
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L1rS683
) {
  uint64_t _M0L1uS682;
  uint64_t _M0L4bitsS684;
  double _M0L6_2atmpS2011;
  #line 118 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  #line 119 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L1uS682 = _M0FP26RiantR8snn__mbt9next__u64(_M0L1rS683);
  _M0L4bitsS684 = _M0L1uS682 >> 11;
  _M0L6_2atmpS2011 = (double)_M0L4bitsS684;
  return _M0L6_2atmpS2011 * 0x1p-53;
}

float _M0FP26RiantR8snn__mbt9next__f32(
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L1rS680
) {
  uint32_t _M0L1uS679;
  uint32_t _M0L4bitsS681;
  double _M0L6_2atmpS2010;
  double _M0L6_2atmpS2009;
  #line 128 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  #line 129 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L1uS679 = _M0FP26RiantR8snn__mbt9next__u32(_M0L1rS680);
  _M0L4bitsS681 = _M0L1uS679 >> 8;
  _M0L6_2atmpS2010 = (double)_M0L4bitsS681;
  _M0L6_2atmpS2009 = _M0L6_2atmpS2010 * 0x1p-24;
  return (float)_M0L6_2atmpS2009;
}

uint32_t _M0FP26RiantR8snn__mbt9next__u32(
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L1rS678
) {
  uint64_t _M0L1uS677;
  uint64_t _M0L6_2atmpS2008;
  #line 110 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  #line 111 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L1uS677 = _M0FP26RiantR8snn__mbt9next__u64(_M0L1rS678);
  _M0L6_2atmpS2008 = _M0L1uS677 >> 32;
  return (uint32_t)_M0L6_2atmpS2008;
}

uint64_t _M0FP26RiantR8snn__mbt9next__u64(
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L1rS670
) {
  uint64_t _M0L2s0S669;
  uint64_t _M0L2s1S671;
  uint64_t _M0L2s2S672;
  uint64_t _M0L2s3S673;
  uint64_t _M0L3tmpS674;
  uint64_t _M0L6_2atmpS2007;
  uint64_t _M0L3resS675;
  uint64_t _M0L1tS676;
  uint64_t _M0L6_2atmpS1997;
  uint64_t _M0L6_2atmpS1998;
  uint64_t _M0L2s2S2000;
  uint64_t _M0L6_2atmpS1999;
  uint64_t _M0L2s3S2002;
  uint64_t _M0L6_2atmpS2001;
  uint64_t _M0L2s2S2004;
  uint64_t _M0L6_2atmpS2003;
  uint64_t _M0L2s3S2006;
  uint64_t _M0L6_2atmpS2005;
  #line 90 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2s0S669 = _M0L1rS670->$0;
  _M0L2s1S671 = _M0L1rS670->$1;
  _M0L2s2S672 = _M0L1rS670->$2;
  _M0L2s3S673 = _M0L1rS670->$3;
  _M0L3tmpS674 = _M0L2s0S669 + _M0L2s3S673;
  #line 96 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L6_2atmpS2007 = _M0FP26RiantR8snn__mbt4rotl(_M0L3tmpS674, 23);
  _M0L3resS675 = _M0L6_2atmpS2007 + _M0L2s0S669;
  _M0L1tS676 = _M0L2s1S671 << 17;
  _M0L6_2atmpS1997 = _M0L2s2S672 ^ _M0L2s0S669;
  _M0L1rS670->$2 = _M0L6_2atmpS1997;
  _M0L6_2atmpS1998 = _M0L2s3S673 ^ _M0L2s1S671;
  _M0L1rS670->$3 = _M0L6_2atmpS1998;
  _M0L2s2S2000 = _M0L1rS670->$2;
  _M0L6_2atmpS1999 = _M0L2s1S671 ^ _M0L2s2S2000;
  _M0L1rS670->$1 = _M0L6_2atmpS1999;
  _M0L2s3S2002 = _M0L1rS670->$3;
  _M0L6_2atmpS2001 = _M0L2s0S669 ^ _M0L2s3S2002;
  _M0L1rS670->$0 = _M0L6_2atmpS2001;
  _M0L2s2S2004 = _M0L1rS670->$2;
  _M0L6_2atmpS2003 = _M0L2s2S2004 ^ _M0L1tS676;
  _M0L1rS670->$2 = _M0L6_2atmpS2003;
  _M0L2s3S2006 = _M0L1rS670->$3;
  #line 103 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L6_2atmpS2005 = _M0FP26RiantR8snn__mbt4rotl(_M0L2s3S2006, 45);
  _M0L1rS670->$3 = _M0L6_2atmpS2005;
  return _M0L3resS675;
}

uint64_t _M0FP26RiantR8snn__mbt4rotl(uint64_t _M0L1xS667, int32_t _M0L1kS668) {
  uint64_t _M0L6_2atmpS1994;
  int32_t _M0L6_2atmpS1996;
  uint64_t _M0L6_2atmpS1995;
  #line 83 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L6_2atmpS1994 = _M0L1xS667 << (_M0L1kS668 & 63);
  _M0L6_2atmpS1996 = 64 - _M0L1kS668;
  _M0L6_2atmpS1995 = _M0L1xS667 >> (_M0L6_2atmpS1996 & 63);
  return _M0L6_2atmpS1994 | _M0L6_2atmpS1995;
}

double _M0FPC14math2ln(double _M0L1xS653) {
  struct _M0TUdiE* _M0L7_2abindS654;
  double _M0L5_2af1S655;
  int32_t _M0L5_2akiS656;
  double _M0L1fS658;
  double _M0L1kS659;
  double _M0L6_2atmpS1987;
  double _M0L1sS660;
  double _M0L2s2S661;
  double _M0L2s4S662;
  double _M0L6_2atmpS1986;
  double _M0L6_2atmpS1985;
  double _M0L6_2atmpS1984;
  double _M0L6_2atmpS1983;
  double _M0L6_2atmpS1982;
  double _M0L6_2atmpS1981;
  double _M0L2t1S663;
  double _M0L6_2atmpS1980;
  double _M0L6_2atmpS1979;
  double _M0L6_2atmpS1978;
  double _M0L6_2atmpS1977;
  double _M0L2t2S664;
  double _M0L1rS665;
  double _M0L6_2atmpS1976;
  double _M0L4hfsqS666;
  double _M0L6_2atmpS1969;
  double _M0L6_2atmpS1975;
  double _M0L6_2atmpS1973;
  double _M0L6_2atmpS1974;
  double _M0L6_2atmpS1972;
  double _M0L6_2atmpS1971;
  double _M0L6_2atmpS1970;
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
    double _M0L6_2atmpS1991 = _M0L5_2af1S655 * 0x1p+1;
    double _M0L6_2atmpS1988 = _M0L6_2atmpS1991 - 0x1p+0;
    int32_t _M0L6_2atmpS1990 = _M0L5_2akiS656 - 1;
    double _M0L6_2atmpS1989 = (double)_M0L6_2atmpS1990;
    _M0L1fS658 = _M0L6_2atmpS1988;
    _M0L1kS659 = _M0L6_2atmpS1989;
    goto join_657;
  } else {
    double _M0L6_2atmpS1992 = _M0L5_2af1S655 - 0x1p+0;
    double _M0L6_2atmpS1993 = (double)_M0L5_2akiS656;
    _M0L1fS658 = _M0L6_2atmpS1992;
    _M0L1kS659 = _M0L6_2atmpS1993;
    goto join_657;
  }
  join_657:;
  _M0L6_2atmpS1987 = 0x1p+1 + _M0L1fS658;
  _M0L1sS660 = _M0L1fS658 / _M0L6_2atmpS1987;
  _M0L2s2S661 = _M0L1sS660 * _M0L1sS660;
  _M0L2s4S662 = _M0L2s2S661 * _M0L2s2S661;
  _M0L6_2atmpS1986 = _M0L2s4S662 * 0x1.2f112df3e5244p-3;
  _M0L6_2atmpS1985 = 0x1.7466496cb03dep-3 + _M0L6_2atmpS1986;
  _M0L6_2atmpS1984 = _M0L2s4S662 * _M0L6_2atmpS1985;
  _M0L6_2atmpS1983 = 0x1.2492494229359p-2 + _M0L6_2atmpS1984;
  _M0L6_2atmpS1982 = _M0L2s4S662 * _M0L6_2atmpS1983;
  _M0L6_2atmpS1981 = 0x1.5555555555593p-1 + _M0L6_2atmpS1982;
  _M0L2t1S663 = _M0L2s2S661 * _M0L6_2atmpS1981;
  _M0L6_2atmpS1980 = _M0L2s4S662 * 0x1.39a09d078c69fp-3;
  _M0L6_2atmpS1979 = 0x1.c71c51d8e78afp-3 + _M0L6_2atmpS1980;
  _M0L6_2atmpS1978 = _M0L2s4S662 * _M0L6_2atmpS1979;
  _M0L6_2atmpS1977 = 0x1.999999997fa04p-2 + _M0L6_2atmpS1978;
  _M0L2t2S664 = _M0L2s4S662 * _M0L6_2atmpS1977;
  _M0L1rS665 = _M0L2t1S663 + _M0L2t2S664;
  _M0L6_2atmpS1976 = 0x1p-1 * _M0L1fS658;
  _M0L4hfsqS666 = _M0L6_2atmpS1976 * _M0L1fS658;
  _M0L6_2atmpS1969 = _M0L1kS659 * 0x1.62e42feep-1;
  _M0L6_2atmpS1975 = _M0L4hfsqS666 + _M0L1rS665;
  _M0L6_2atmpS1973 = _M0L1sS660 * _M0L6_2atmpS1975;
  _M0L6_2atmpS1974 = _M0L1kS659 * 0x1.a39ef35793c76p-33;
  _M0L6_2atmpS1972 = _M0L6_2atmpS1973 + _M0L6_2atmpS1974;
  _M0L6_2atmpS1971 = _M0L4hfsqS666 - _M0L6_2atmpS1972;
  _M0L6_2atmpS1970 = _M0L6_2atmpS1971 - _M0L1fS658;
  return _M0L6_2atmpS1969 - _M0L6_2atmpS1970;
}

struct _M0TUdiE* _M0FPC14math5frexp(double _M0L1fS646) {
  struct _M0TUdiE* _M0L7_2abindS647;
  double _M0L10_2anorm__fS648;
  int32_t _M0L6_2aexpS649;
  uint64_t _M0L1uS650;
  uint64_t _M0L6_2atmpS1968;
  uint64_t _M0L6_2atmpS1967;
  int32_t _M0L6_2atmpS1966;
  int32_t _M0L6_2atmpS1965;
  int32_t _M0L3expS651;
  uint64_t _M0L6_2atmpS1964;
  uint64_t _M0L6_2atmpS1963;
  uint64_t _M0L6_2atmpS1962;
  double _M0L4fracS652;
  struct _M0TUdiE* _block_3112;
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\math\\utils.mbt"
  #line 134 "C:\\Users\\31379\\.moon\\lib\\core\\math\\utils.mbt"
  #line 134 "C:\\Users\\31379\\.moon\\lib\\core\\math\\utils.mbt"
  if (
    _M0L1fS646 == 0x0p+0
    || _M0MPC16double6Double7is__inf(_M0L1fS646)
    || _M0MPC16double6Double7is__nan(_M0L1fS646)
  ) {
    struct _M0TUdiE* _block_3111 =
      (struct _M0TUdiE*)moonbit_malloc(sizeof(struct _M0TUdiE));
    Moonbit_object_header(_block_3111)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
    _block_3111->$0 = _M0L1fS646;
    _block_3111->$1 = 0;
    return _block_3111;
  }
  #line 137 "C:\\Users\\31379\\.moon\\lib\\core\\math\\utils.mbt"
  _M0L7_2abindS647 = _M0FPC14math9normalize(_M0L1fS646);
  _M0L10_2anorm__fS648 = _M0L7_2abindS647->$0;
  _M0L6_2aexpS649 = _M0L7_2abindS647->$1;
  moonbit_decref(_M0L7_2abindS647);
  _M0L1uS650 = *(int64_t*)&_M0L10_2anorm__fS648;
  _M0L6_2atmpS1968 = _M0L1uS650 >> 52;
  _M0L6_2atmpS1967 = _M0L6_2atmpS1968 & 2047ull;
  _M0L6_2atmpS1966 = (int32_t)_M0L6_2atmpS1967;
  _M0L6_2atmpS1965 = _M0L6_2aexpS649 + _M0L6_2atmpS1966;
  _M0L3expS651 = _M0L6_2atmpS1965 - 1022;
  _M0L6_2atmpS1964 = ~9218868437227405312ull;
  _M0L6_2atmpS1963 = _M0L1uS650 & _M0L6_2atmpS1964;
  _M0L6_2atmpS1962 = _M0L6_2atmpS1963 | 4602678819172646912ull;
  _M0L4fracS652 = *(double*)&_M0L6_2atmpS1962;
  _block_3112 = (struct _M0TUdiE*)moonbit_malloc(sizeof(struct _M0TUdiE));
  Moonbit_object_header(_block_3112)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_3112->$0 = _M0L4fracS652;
  _block_3112->$1 = _M0L3expS651;
  return _block_3112;
}

struct _M0TUdiE* _M0FPC14math9normalize(double _M0L1fS645) {
  double _M0L6_2atmpS1959;
  struct _M0TUdiE* _block_3114;
  #line 125 "C:\\Users\\31379\\.moon\\lib\\core\\math\\utils.mbt"
  #line 126 "C:\\Users\\31379\\.moon\\lib\\core\\math\\utils.mbt"
  _M0L6_2atmpS1959 = fabs(_M0L1fS645);
  if (_M0L6_2atmpS1959 < _M0FPC16double13min__positive) {
    double _M0L6_2atmpS1961 = (double)4503599627370496ll;
    double _M0L6_2atmpS1960 = _M0L1fS645 * _M0L6_2atmpS1961;
    struct _M0TUdiE* _block_3113 =
      (struct _M0TUdiE*)moonbit_malloc(sizeof(struct _M0TUdiE));
    Moonbit_object_header(_block_3113)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
    _block_3113->$0 = _M0L6_2atmpS1960;
    _block_3113->$1 = -52;
    return _block_3113;
  }
  _block_3114 = (struct _M0TUdiE*)moonbit_malloc(sizeof(struct _M0TUdiE));
  Moonbit_object_header(_block_3114)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_3114->$0 = _M0L1fS645;
  _block_3114->$1 = 0;
  return _block_3114;
}

moonbit_string_t _M0IPC15float5FloatPB4Show10to__string(float _M0L4selfS644) {
  double _M0L6_2atmpS1958;
  #line 16 "C:\\Users\\31379\\.moon\\lib\\core\\float\\methods.mbt"
  _M0L6_2atmpS1958 = (double)_M0L4selfS644;
  #line 17 "C:\\Users\\31379\\.moon\\lib\\core\\float\\methods.mbt"
  return _M0MPC16double6Double10to__string(_M0L6_2atmpS1958);
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
      float* _M0L3bufS1950 = _M0L3arrS623->$0;
      int32_t _M0L6_2atmpS1951;
      _M0L3bufS1950[_M0L1iS625] = _M0L4elemS626;
      _M0L6_2atmpS1951 = _M0L1iS625 + 1;
      _M0L1iS625 = _M0L6_2atmpS1951;
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
      uint8_t* _M0L3bufS1952 = _M0L3arrS628->$0;
      int32_t _M0L6_2atmpS1953;
      _M0L3bufS1952[_M0L1iS630] = _M0L4elemS631;
      _M0L6_2atmpS1953 = _M0L1iS630 + 1;
      _M0L1iS630 = _M0L6_2atmpS1953;
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
      int32_t* _M0L3bufS1954 = _M0L3arrS633->$0;
      int32_t _M0L6_2atmpS1955;
      _M0L3bufS1954[_M0L1iS635] = _M0L4elemS636;
      _M0L6_2atmpS1955 = _M0L1iS635 + 1;
      _M0L1iS635 = _M0L6_2atmpS1955;
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
      struct _M0TPB5ArrayGfE** _M0L3bufS1956 = _M0L3arrS638->$0;
      struct _M0TPB5ArrayGfE* _M0L6_2aoldS3026 =
        (struct _M0TPB5ArrayGfE*)_M0L3bufS1956[_M0L1iS640];
      int32_t _M0L6_2atmpS1957;
      moonbit_incref(_M0L4elemS641);
      if (_M0L6_2aoldS3026) {
        moonbit_decref(_M0L6_2aoldS3026);
      }
      _M0L3bufS1956[_M0L1iS640] = _M0L4elemS641;
      _M0L6_2atmpS1957 = _M0L1iS640 + 1;
      _M0L1iS640 = _M0L6_2atmpS1957;
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
    float* _M0L6_2atmpS1946;
    #line 268 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS1946 = _M0MPC15array5Array6bufferGfE(_M0L4selfS608);
    _M0L6_2atmpS1946[_M0L5indexS609] = _M0L5valueS610;
    moonbit_decref(_M0L6_2atmpS1946);
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
    uint8_t* _M0L6_2atmpS1947;
    #line 268 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS1947 = _M0MPC15array5Array6bufferGbE(_M0L4selfS612);
    _M0L6_2atmpS1947[_M0L5indexS613] = _M0L5valueS614;
    moonbit_decref(_M0L6_2atmpS1947);
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
    int32_t* _M0L6_2atmpS1948;
    #line 268 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS1948 = _M0MPC15array5Array6bufferGiE(_M0L4selfS616);
    _M0L6_2atmpS1948[_M0L5indexS617] = _M0L5valueS618;
    moonbit_decref(_M0L6_2atmpS1948);
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
    struct _M0TPB5ArrayGfE** _M0L6_2atmpS1949;
    struct _M0TPB5ArrayGfE* _M0L6_2aoldS3027;
    #line 268 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS1949
    = _M0MPC15array5Array6bufferGRPB5ArrayGfEE(_M0L4selfS620);
    _M0L6_2aoldS3027
    = (struct _M0TPB5ArrayGfE*)_M0L6_2atmpS1949[_M0L5indexS621];
    if (_M0L6_2aoldS3027) {
      moonbit_decref(_M0L6_2aoldS3027);
    }
    _M0L6_2atmpS1949[_M0L5indexS621] = _M0L5valueS622;
    moonbit_decref(_M0L6_2atmpS1949);
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
    uint8_t* _M0L6_2atmpS1942;
    int32_t _result_3119;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS1942 = _M0MPC15array5Array6bufferGbE(_M0L4selfS596);
    _result_3119 = (int32_t)_M0L6_2atmpS1942[_M0L5indexS597];
    moonbit_decref(_M0L6_2atmpS1942);
    return _result_3119;
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
    int32_t* _M0L6_2atmpS1943;
    int32_t _result_3120;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS1943 = _M0MPC15array5Array6bufferGiE(_M0L4selfS599);
    _result_3120 = (int32_t)_M0L6_2atmpS1943[_M0L5indexS600];
    moonbit_decref(_M0L6_2atmpS1943);
    return _result_3120;
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
    float* _M0L6_2atmpS1944;
    float _result_3121;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS1944 = _M0MPC15array5Array6bufferGfE(_M0L4selfS602);
    _result_3121 = (float)_M0L6_2atmpS1944[_M0L5indexS603];
    moonbit_decref(_M0L6_2atmpS1944);
    return _result_3121;
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
    struct _M0TPB5ArrayGfE** _M0L6_2atmpS1945;
    struct _M0TPB5ArrayGfE* _M0L6_2atmpS3028;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS1945
    = _M0MPC15array5Array6bufferGRPB5ArrayGfEE(_M0L4selfS605);
    _M0L6_2atmpS3028
    = (struct _M0TPB5ArrayGfE*)_M0L6_2atmpS1945[_M0L5indexS606];
    if (_M0L6_2atmpS3028) {
      moonbit_incref(_M0L6_2atmpS3028);
    }
    moonbit_decref(_M0L6_2atmpS1945);
    return _M0L6_2atmpS3028;
  } else {
    #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
}

int32_t _M0FPB7printlnGsE(moonbit_string_t _M0L5inputS594) {
  moonbit_string_t _M0L6_2atmpS1941;
  #line 36 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\console.mbt"
  #line 37 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\console.mbt"
  _M0L6_2atmpS1941 = _M0IPC16string6StringPB4Show10to__string(_M0L5inputS594);
  #line 37 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\console.mbt"
  moonbit_println(_M0L6_2atmpS1941);
  moonbit_decref(_M0L6_2atmpS1941);
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
  uint64_t _M0L6_2atmpS1940;
  uint64_t _M0L6_2atmpS1939;
  int32_t _M0L8ieeeSignS580;
  uint64_t _M0L12ieeeMantissaS581;
  uint64_t _M0L6_2atmpS1938;
  uint64_t _M0L6_2atmpS1937;
  int32_t _M0L12ieeeExponentS582;
  struct _M0TPB17FloatingDecimal64* _M0L7_2abindS583;
  struct _M0TPB17FloatingDecimal64* _M0L1vS584;
  moonbit_string_t _result_3123;
  #line 668 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  if (_M0L3valS576 == 0x0p+0) {
    return (moonbit_string_t)moonbit_string_literal_4.data;
  }
  if (_M0L3valS576 >= -0x1p+53 && _M0L3valS576 <= 0x1p+53) {
    if (_M0L3valS576 >= -0x1p+31 && _M0L3valS576 <= 0x1.fffffffcp+30) {
      int32_t _M0L1iS577;
      double _M0L6_2atmpS1926;
      #line 683 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0L1iS577 = _M0MPC16double6Double7to__int(_M0L3valS576);
      _M0L6_2atmpS1926 = (double)_M0L1iS577;
      if (_M0L6_2atmpS1926 == _M0L3valS576) {
        #line 685 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        return _M0MPC13int3Int18to__string_2einner(_M0L1iS577, 10);
      }
    } else {
      int64_t _M0L1iS578;
      double _M0L6_2atmpS1927;
      #line 688 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0L1iS578 = _M0MPC16double6Double9to__int64(_M0L3valS576);
      _M0L6_2atmpS1927 = (double)_M0L1iS578;
      if (_M0L6_2atmpS1927 == _M0L3valS576) {
        #line 690 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        return _M0MPC15int645Int6418to__string_2einner(_M0L1iS578, 10);
      }
    }
  }
  _M0L4bitsS579 = *(int64_t*)&_M0L3valS576;
  _M0L6_2atmpS1940 = _M0L4bitsS579 >> 63;
  _M0L6_2atmpS1939 = _M0L6_2atmpS1940 & 1ull;
  _M0L8ieeeSignS580 = _M0L6_2atmpS1939 != 0ull;
  _M0L12ieeeMantissaS581 = _M0L4bitsS579 & 4503599627370495ull;
  _M0L6_2atmpS1938 = _M0L4bitsS579 >> 52;
  _M0L6_2atmpS1937 = _M0L6_2atmpS1938 & 2047ull;
  _M0L12ieeeExponentS582 = (int32_t)_M0L6_2atmpS1937;
  if (
    _M0L12ieeeExponentS582 == 2047
    || _M0L12ieeeExponentS582 == 0 && _M0L12ieeeMantissaS581 == 0ull
  ) {
    int32_t _M0L6_2atmpS1928 = _M0L12ieeeExponentS582 != 0;
    int32_t _M0L6_2atmpS1929 = _M0L12ieeeMantissaS581 != 0ull;
    #line 707 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    return _M0FPB18copy__special__str(_M0L8ieeeSignS580, _M0L6_2atmpS1928, _M0L6_2atmpS1929);
  }
  #line 709 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS583
  = _M0FPB15d2d__small__int(_M0L12ieeeMantissaS581, _M0L12ieeeExponentS582);
  if (_M0L7_2abindS583 == 0) {
    uint32_t _M0L6_2atmpS1930;
    if (_M0L7_2abindS583) {
      moonbit_decref(_M0L7_2abindS583);
    }
    _M0L6_2atmpS1930 = *(uint32_t*)&_M0L12ieeeExponentS582;
    #line 719 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L1vS584 = _M0FPB3d2d(_M0L12ieeeMantissaS581, _M0L6_2atmpS1930);
  } else {
    struct _M0TPB17FloatingDecimal64* _M0L7_2aSomeS585 = _M0L7_2abindS583;
    struct _M0TPB17FloatingDecimal64* _M0L4_2afS586 = _M0L7_2aSomeS585;
    struct _M0TPB17FloatingDecimal64* _M0L1xS587 = _M0L4_2afS586;
    while (1) {
      uint64_t _M0L8mantissaS1936 = _M0L1xS587->$0;
      uint64_t _M0L1qS588 = _M0L8mantissaS1936 / 10ull;
      uint64_t _M0L8mantissaS1934 = _M0L1xS587->$0;
      uint64_t _M0L6_2atmpS1935 = 10ull * _M0L1qS588;
      uint64_t _M0L1rS589 = _M0L8mantissaS1934 - _M0L6_2atmpS1935;
      int32_t _M0L8exponentS1933;
      int32_t _M0L6_2atmpS1932;
      struct _M0TPB17FloatingDecimal64* _M0L6_2atmpS1931;
      if (_M0L1rS589 != 0ull) {
        _M0L1vS584 = _M0L1xS587;
        break;
      }
      _M0L8exponentS1933 = _M0L1xS587->$1;
      moonbit_decref(_M0L1xS587);
      _M0L6_2atmpS1932 = _M0L8exponentS1933 + 1;
      _M0L6_2atmpS1931
      = (struct _M0TPB17FloatingDecimal64*)moonbit_malloc(sizeof(struct _M0TPB17FloatingDecimal64));
      Moonbit_object_header(_M0L6_2atmpS1931)->meta
      = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
      _M0L6_2atmpS1931->$0 = _M0L1qS588;
      _M0L6_2atmpS1931->$1 = _M0L6_2atmpS1932;
      _M0L1xS587 = _M0L6_2atmpS1931;
      continue;
      break;
    }
  }
  #line 721 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _result_3123 = _M0FPB9to__chars(_M0L1vS584, _M0L8ieeeSignS580);
  moonbit_decref(_M0L1vS584);
  return _result_3123;
}

struct _M0TPB17FloatingDecimal64* _M0FPB15d2d__small__int(
  uint64_t _M0L12ieeeMantissaS571,
  int32_t _M0L12ieeeExponentS573
) {
  uint64_t _M0L2m2S570;
  int32_t _M0L6_2atmpS1925;
  int32_t _M0L2e2S572;
  int32_t _M0L6_2atmpS1924;
  uint64_t _M0L6_2atmpS1923;
  uint64_t _M0L4maskS574;
  uint64_t _M0L8fractionS575;
  int32_t _M0L6_2atmpS1922;
  uint64_t _M0L6_2atmpS1921;
  struct _M0TPB17FloatingDecimal64* _M0L6_2atmpS1920;
  #line 637 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L2m2S570 = 4503599627370496ull | _M0L12ieeeMantissaS571;
  _M0L6_2atmpS1925 = _M0L12ieeeExponentS573 - 1023;
  _M0L2e2S572 = _M0L6_2atmpS1925 - 52;
  if (_M0L2e2S572 > 0) {
    return 0;
  }
  if (_M0L2e2S572 < -52) {
    return 0;
  }
  _M0L6_2atmpS1924 = -_M0L2e2S572;
  _M0L6_2atmpS1923 = 1ull << (_M0L6_2atmpS1924 & 63);
  _M0L4maskS574 = _M0L6_2atmpS1923 - 1ull;
  _M0L8fractionS575 = _M0L2m2S570 & _M0L4maskS574;
  if (_M0L8fractionS575 != 0ull) {
    return 0;
  }
  _M0L6_2atmpS1922 = -_M0L2e2S572;
  _M0L6_2atmpS1921 = _M0L2m2S570 >> (_M0L6_2atmpS1922 & 63);
  _M0L6_2atmpS1920
  = (struct _M0TPB17FloatingDecimal64*)moonbit_malloc(sizeof(struct _M0TPB17FloatingDecimal64));
  Moonbit_object_header(_M0L6_2atmpS1920)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L6_2atmpS1920->$0 = _M0L6_2atmpS1921;
  _M0L6_2atmpS1920->$1 = 0;
  return _M0L6_2atmpS1920;
}

moonbit_string_t _M0FPB9to__chars(
  struct _M0TPB17FloatingDecimal64* _M0L1vS538,
  int32_t _M0L4signS536
) {
  moonbit_bytes_t _M0L6resultS534;
  int32_t _M0Lm5indexS535;
  uint64_t _M0L6outputS537;
  int32_t _M0L7olengthS539;
  int32_t _M0L8exponentS1919;
  int32_t _M0L6_2atmpS1918;
  int32_t _M0Lm3expS540;
  int32_t _M0L6_2atmpS1917;
  int32_t _M0L6_2atmpS1915;
  int32_t _M0L18scientificNotationS541;
  #line 530 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6resultS534 = (moonbit_bytes_t)moonbit_make_bytes(25, 0);
  _M0Lm5indexS535 = 0;
  if (_M0L4signS536) {
    int32_t _M0L6_2atmpS1789 = _M0Lm5indexS535;
    int32_t _M0L6_2atmpS1790;
    if (
      _M0L6_2atmpS1789 < 0
      || _M0L6_2atmpS1789 >= Moonbit_array_length(_M0L6resultS534)
    ) {
      #line 535 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      moonbit_panic();
    }
    _M0L6resultS534[_M0L6_2atmpS1789] = 45;
    _M0L6_2atmpS1790 = _M0Lm5indexS535;
    _M0Lm5indexS535 = _M0L6_2atmpS1790 + 1;
  }
  _M0L6outputS537 = _M0L1vS538->$0;
  #line 539 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7olengthS539 = _M0FPB17decimal__length17(_M0L6outputS537);
  _M0L8exponentS1919 = _M0L1vS538->$1;
  _M0L6_2atmpS1918 = _M0L8exponentS1919 + _M0L7olengthS539;
  _M0Lm3expS540 = _M0L6_2atmpS1918 - 1;
  _M0L6_2atmpS1917 = _M0Lm3expS540;
  if (_M0L6_2atmpS1917 >= -6) {
    int32_t _M0L6_2atmpS1916 = _M0Lm3expS540;
    _M0L6_2atmpS1915 = _M0L6_2atmpS1916 < 21;
  } else {
    _M0L6_2atmpS1915 = 0;
  }
  _M0L18scientificNotationS541 = !_M0L6_2atmpS1915;
  if (_M0L18scientificNotationS541) {
    int32_t _M0L7_2abindS542 = _M0L7olengthS539 - 1;
    uint64_t _M0L6outputS543;
    int32_t _M0L1iS544 = 0;
    uint64_t _M0L6outputS545 = _M0L6outputS537;
    int32_t _M0L6_2atmpS1791;
    int32_t _M0L6_2atmpS1795;
    int32_t _M0L6_2atmpS1794;
    int32_t _M0L6_2atmpS1793;
    int32_t _M0L6_2atmpS1792;
    int32_t _M0L6_2atmpS1799;
    int32_t _M0L6_2atmpS1800;
    int32_t _M0L6_2atmpS1801;
    int32_t _M0L6_2atmpS1802;
    int32_t _M0L6_2atmpS1803;
    int32_t _M0L6_2atmpS1809;
    int32_t _M0L6_2atmpS1842;
    moonbit_string_t _result_3125;
    while (1) {
      if (_M0L1iS544 < _M0L7_2abindS542) {
        uint64_t _M0L1cS546 = _M0L6outputS545 % 10ull;
        int32_t _M0L6_2atmpS1848 = _M0Lm5indexS535;
        int32_t _M0L6_2atmpS1847 = _M0L6_2atmpS1848 + _M0L7olengthS539;
        int32_t _M0L6_2atmpS1843 = _M0L6_2atmpS1847 - _M0L1iS544;
        int32_t _M0L6_2atmpS1846 = (int32_t)_M0L1cS546;
        int32_t _M0L6_2atmpS1845 = 48 + _M0L6_2atmpS1846;
        int32_t _M0L6_2atmpS1844 = _M0L6_2atmpS1845 & 0xff;
        int32_t _M0L6_2atmpS1849;
        uint64_t _M0L6_2atmpS1850;
        if (
          _M0L6_2atmpS1843 < 0
          || _M0L6_2atmpS1843 >= Moonbit_array_length(_M0L6resultS534)
        ) {
          #line 547 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
          moonbit_panic();
        }
        _M0L6resultS534[_M0L6_2atmpS1843] = _M0L6_2atmpS1844;
        _M0L6_2atmpS1849 = _M0L1iS544 + 1;
        _M0L6_2atmpS1850 = _M0L6outputS545 / 10ull;
        _M0L1iS544 = _M0L6_2atmpS1849;
        _M0L6outputS545 = _M0L6_2atmpS1850;
        continue;
      } else {
        _M0L6outputS543 = _M0L6outputS545;
      }
      break;
    }
    _M0L6_2atmpS1791 = _M0Lm5indexS535;
    _M0L6_2atmpS1795 = (int32_t)_M0L6outputS543;
    _M0L6_2atmpS1794 = _M0L6_2atmpS1795 % 10;
    _M0L6_2atmpS1793 = 48 + _M0L6_2atmpS1794;
    _M0L6_2atmpS1792 = _M0L6_2atmpS1793 & 0xff;
    if (
      _M0L6_2atmpS1791 < 0
      || _M0L6_2atmpS1791 >= Moonbit_array_length(_M0L6resultS534)
    ) {
      #line 552 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      moonbit_panic();
    }
    _M0L6resultS534[_M0L6_2atmpS1791] = _M0L6_2atmpS1792;
    if (_M0L7olengthS539 > 1) {
      int32_t _M0L6_2atmpS1797 = _M0Lm5indexS535;
      int32_t _M0L6_2atmpS1796 = _M0L6_2atmpS1797 + 1;
      if (
        _M0L6_2atmpS1796 < 0
        || _M0L6_2atmpS1796 >= Moonbit_array_length(_M0L6resultS534)
      ) {
        #line 554 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS534[_M0L6_2atmpS1796] = 46;
    } else {
      int32_t _M0L6_2atmpS1798 = _M0Lm5indexS535;
      _M0Lm5indexS535 = _M0L6_2atmpS1798 - 1;
    }
    _M0L6_2atmpS1799 = _M0Lm5indexS535;
    _M0L6_2atmpS1800 = _M0L7olengthS539 + 1;
    _M0Lm5indexS535 = _M0L6_2atmpS1799 + _M0L6_2atmpS1800;
    _M0L6_2atmpS1801 = _M0Lm5indexS535;
    if (
      _M0L6_2atmpS1801 < 0
      || _M0L6_2atmpS1801 >= Moonbit_array_length(_M0L6resultS534)
    ) {
      #line 562 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      moonbit_panic();
    }
    _M0L6resultS534[_M0L6_2atmpS1801] = 101;
    _M0L6_2atmpS1802 = _M0Lm5indexS535;
    _M0Lm5indexS535 = _M0L6_2atmpS1802 + 1;
    _M0L6_2atmpS1803 = _M0Lm3expS540;
    if (_M0L6_2atmpS1803 < 0) {
      int32_t _M0L6_2atmpS1804 = _M0Lm5indexS535;
      int32_t _M0L6_2atmpS1805;
      int32_t _M0L6_2atmpS1806;
      if (
        _M0L6_2atmpS1804 < 0
        || _M0L6_2atmpS1804 >= Moonbit_array_length(_M0L6resultS534)
      ) {
        #line 565 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS534[_M0L6_2atmpS1804] = 45;
      _M0L6_2atmpS1805 = _M0Lm5indexS535;
      _M0Lm5indexS535 = _M0L6_2atmpS1805 + 1;
      _M0L6_2atmpS1806 = _M0Lm3expS540;
      _M0Lm3expS540 = -_M0L6_2atmpS1806;
    } else {
      int32_t _M0L6_2atmpS1807 = _M0Lm5indexS535;
      int32_t _M0L6_2atmpS1808;
      if (
        _M0L6_2atmpS1807 < 0
        || _M0L6_2atmpS1807 >= Moonbit_array_length(_M0L6resultS534)
      ) {
        #line 569 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS534[_M0L6_2atmpS1807] = 43;
      _M0L6_2atmpS1808 = _M0Lm5indexS535;
      _M0Lm5indexS535 = _M0L6_2atmpS1808 + 1;
    }
    _M0L6_2atmpS1809 = _M0Lm3expS540;
    if (_M0L6_2atmpS1809 >= 100) {
      int32_t _M0L6_2atmpS1825 = _M0Lm3expS540;
      int32_t _M0L1aS548 = _M0L6_2atmpS1825 / 100;
      int32_t _M0L6_2atmpS1824 = _M0Lm3expS540;
      int32_t _M0L6_2atmpS1823 = _M0L6_2atmpS1824 / 10;
      int32_t _M0L1bS549 = _M0L6_2atmpS1823 % 10;
      int32_t _M0L6_2atmpS1822 = _M0Lm3expS540;
      int32_t _M0L1cS550 = _M0L6_2atmpS1822 % 10;
      int32_t _M0L6_2atmpS1810 = _M0Lm5indexS535;
      int32_t _M0L6_2atmpS1812 = 48 + _M0L1aS548;
      int32_t _M0L6_2atmpS1811 = _M0L6_2atmpS1812 & 0xff;
      int32_t _M0L6_2atmpS1816;
      int32_t _M0L6_2atmpS1813;
      int32_t _M0L6_2atmpS1815;
      int32_t _M0L6_2atmpS1814;
      int32_t _M0L6_2atmpS1820;
      int32_t _M0L6_2atmpS1817;
      int32_t _M0L6_2atmpS1819;
      int32_t _M0L6_2atmpS1818;
      int32_t _M0L6_2atmpS1821;
      if (
        _M0L6_2atmpS1810 < 0
        || _M0L6_2atmpS1810 >= Moonbit_array_length(_M0L6resultS534)
      ) {
        #line 576 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS534[_M0L6_2atmpS1810] = _M0L6_2atmpS1811;
      _M0L6_2atmpS1816 = _M0Lm5indexS535;
      _M0L6_2atmpS1813 = _M0L6_2atmpS1816 + 1;
      _M0L6_2atmpS1815 = 48 + _M0L1bS549;
      _M0L6_2atmpS1814 = _M0L6_2atmpS1815 & 0xff;
      if (
        _M0L6_2atmpS1813 < 0
        || _M0L6_2atmpS1813 >= Moonbit_array_length(_M0L6resultS534)
      ) {
        #line 577 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS534[_M0L6_2atmpS1813] = _M0L6_2atmpS1814;
      _M0L6_2atmpS1820 = _M0Lm5indexS535;
      _M0L6_2atmpS1817 = _M0L6_2atmpS1820 + 2;
      _M0L6_2atmpS1819 = 48 + _M0L1cS550;
      _M0L6_2atmpS1818 = _M0L6_2atmpS1819 & 0xff;
      if (
        _M0L6_2atmpS1817 < 0
        || _M0L6_2atmpS1817 >= Moonbit_array_length(_M0L6resultS534)
      ) {
        #line 578 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS534[_M0L6_2atmpS1817] = _M0L6_2atmpS1818;
      _M0L6_2atmpS1821 = _M0Lm5indexS535;
      _M0Lm5indexS535 = _M0L6_2atmpS1821 + 3;
    } else {
      int32_t _M0L6_2atmpS1826 = _M0Lm3expS540;
      if (_M0L6_2atmpS1826 >= 10) {
        int32_t _M0L6_2atmpS1836 = _M0Lm3expS540;
        int32_t _M0L1aS551 = _M0L6_2atmpS1836 / 10;
        int32_t _M0L6_2atmpS1835 = _M0Lm3expS540;
        int32_t _M0L1bS552 = _M0L6_2atmpS1835 % 10;
        int32_t _M0L6_2atmpS1827 = _M0Lm5indexS535;
        int32_t _M0L6_2atmpS1829 = 48 + _M0L1aS551;
        int32_t _M0L6_2atmpS1828 = _M0L6_2atmpS1829 & 0xff;
        int32_t _M0L6_2atmpS1833;
        int32_t _M0L6_2atmpS1830;
        int32_t _M0L6_2atmpS1832;
        int32_t _M0L6_2atmpS1831;
        int32_t _M0L6_2atmpS1834;
        if (
          _M0L6_2atmpS1827 < 0
          || _M0L6_2atmpS1827 >= Moonbit_array_length(_M0L6resultS534)
        ) {
          #line 583 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
          moonbit_panic();
        }
        _M0L6resultS534[_M0L6_2atmpS1827] = _M0L6_2atmpS1828;
        _M0L6_2atmpS1833 = _M0Lm5indexS535;
        _M0L6_2atmpS1830 = _M0L6_2atmpS1833 + 1;
        _M0L6_2atmpS1832 = 48 + _M0L1bS552;
        _M0L6_2atmpS1831 = _M0L6_2atmpS1832 & 0xff;
        if (
          _M0L6_2atmpS1830 < 0
          || _M0L6_2atmpS1830 >= Moonbit_array_length(_M0L6resultS534)
        ) {
          #line 584 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
          moonbit_panic();
        }
        _M0L6resultS534[_M0L6_2atmpS1830] = _M0L6_2atmpS1831;
        _M0L6_2atmpS1834 = _M0Lm5indexS535;
        _M0Lm5indexS535 = _M0L6_2atmpS1834 + 2;
      } else {
        int32_t _M0L6_2atmpS1837 = _M0Lm5indexS535;
        int32_t _M0L6_2atmpS1840 = _M0Lm3expS540;
        int32_t _M0L6_2atmpS1839 = 48 + _M0L6_2atmpS1840;
        int32_t _M0L6_2atmpS1838 = _M0L6_2atmpS1839 & 0xff;
        int32_t _M0L6_2atmpS1841;
        if (
          _M0L6_2atmpS1837 < 0
          || _M0L6_2atmpS1837 >= Moonbit_array_length(_M0L6resultS534)
        ) {
          #line 587 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
          moonbit_panic();
        }
        _M0L6resultS534[_M0L6_2atmpS1837] = _M0L6_2atmpS1838;
        _M0L6_2atmpS1841 = _M0Lm5indexS535;
        _M0Lm5indexS535 = _M0L6_2atmpS1841 + 1;
      }
    }
    _M0L6_2atmpS1842 = _M0Lm5indexS535;
    #line 590 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _result_3125
    = _M0FPB19string__from__bytes(_M0L6resultS534, 0, _M0L6_2atmpS1842);
    moonbit_decref(_M0L6resultS534);
    return _result_3125;
  } else {
    int32_t _M0L6_2atmpS1851 = _M0Lm3expS540;
    int32_t _M0L6_2atmpS1914;
    moonbit_string_t _result_3131;
    if (_M0L6_2atmpS1851 < 0) {
      int32_t _M0L6_2atmpS1852 = _M0Lm5indexS535;
      int32_t _M0L6_2atmpS1854;
      int32_t _M0L6_2atmpS1853;
      int32_t _M0L6_2atmpS1855;
      int32_t _M0L1iS553;
      int32_t _M0L6_2atmpS1870;
      int32_t _M0L6_2atmpS1872;
      int32_t _M0L6_2atmpS1871;
      int32_t _M0L7currentS555;
      int32_t _M0L1iS556;
      uint64_t _M0L6outputS557;
      if (
        _M0L6_2atmpS1852 < 0
        || _M0L6_2atmpS1852 >= Moonbit_array_length(_M0L6resultS534)
      ) {
        #line 595 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS534[_M0L6_2atmpS1852] = 48;
      _M0L6_2atmpS1854 = _M0Lm5indexS535;
      _M0L6_2atmpS1853 = _M0L6_2atmpS1854 + 1;
      if (
        _M0L6_2atmpS1853 < 0
        || _M0L6_2atmpS1853 >= Moonbit_array_length(_M0L6resultS534)
      ) {
        #line 596 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS534[_M0L6_2atmpS1853] = 46;
      _M0L6_2atmpS1855 = _M0Lm5indexS535;
      _M0Lm5indexS535 = _M0L6_2atmpS1855 + 2;
      _M0L1iS553 = -1;
      while (1) {
        int32_t _M0L6_2atmpS1856 = _M0Lm3expS540;
        if (_M0L1iS553 > _M0L6_2atmpS1856) {
          int32_t _M0L6_2atmpS1859 = _M0Lm5indexS535;
          int32_t _M0L6_2atmpS1858 = _M0L6_2atmpS1859 - _M0L1iS553;
          int32_t _M0L6_2atmpS1857 = _M0L6_2atmpS1858 - 1;
          int32_t _M0L6_2atmpS1860;
          if (
            _M0L6_2atmpS1857 < 0
            || _M0L6_2atmpS1857 >= Moonbit_array_length(_M0L6resultS534)
          ) {
            #line 599 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
            moonbit_panic();
          }
          _M0L6resultS534[_M0L6_2atmpS1857] = 48;
          _M0L6_2atmpS1860 = _M0L1iS553 - 1;
          _M0L1iS553 = _M0L6_2atmpS1860;
          continue;
        }
        break;
      }
      _M0L6_2atmpS1870 = _M0Lm5indexS535;
      _M0L6_2atmpS1872 = _M0Lm3expS540;
      _M0L6_2atmpS1871 = -1 - _M0L6_2atmpS1872;
      _M0L7currentS555 = _M0L6_2atmpS1870 + _M0L6_2atmpS1871;
      _M0L1iS556 = 0;
      _M0L6outputS557 = _M0L6outputS537;
      while (1) {
        if (_M0L1iS556 < _M0L7olengthS539) {
          int32_t _M0L6_2atmpS1867 = _M0L7currentS555 + _M0L7olengthS539;
          int32_t _M0L6_2atmpS1866 = _M0L6_2atmpS1867 - _M0L1iS556;
          int32_t _M0L6_2atmpS1861 = _M0L6_2atmpS1866 - 1;
          uint64_t _M0L6_2atmpS1865 = _M0L6outputS557 % 10ull;
          int32_t _M0L6_2atmpS1864 = (int32_t)_M0L6_2atmpS1865;
          int32_t _M0L6_2atmpS1863 = 48 + _M0L6_2atmpS1864;
          int32_t _M0L6_2atmpS1862 = _M0L6_2atmpS1863 & 0xff;
          int32_t _M0L6_2atmpS1868;
          uint64_t _M0L6_2atmpS1869;
          if (
            _M0L6_2atmpS1861 < 0
            || _M0L6_2atmpS1861 >= Moonbit_array_length(_M0L6resultS534)
          ) {
            #line 603 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
            moonbit_panic();
          }
          _M0L6resultS534[_M0L6_2atmpS1861] = _M0L6_2atmpS1862;
          _M0L6_2atmpS1868 = _M0L1iS556 + 1;
          _M0L6_2atmpS1869 = _M0L6outputS557 / 10ull;
          _M0L1iS556 = _M0L6_2atmpS1868;
          _M0L6outputS557 = _M0L6_2atmpS1869;
          continue;
        }
        break;
      }
      _M0Lm5indexS535 = _M0L7currentS555 + _M0L7olengthS539;
    } else {
      int32_t _M0L6_2atmpS1874 = _M0Lm3expS540;
      int32_t _M0L6_2atmpS1873 = _M0L6_2atmpS1874 + 1;
      if (_M0L6_2atmpS1873 >= _M0L7olengthS539) {
        int32_t _M0L1iS559 = 0;
        uint64_t _M0L6outputS560 = _M0L6outputS537;
        int32_t _M0L6_2atmpS1885;
        int32_t _M0L6_2atmpS1890;
        int32_t _M0L7_2abindS562;
        int32_t _M0L1iS563;
        int32_t _M0L6_2atmpS1891;
        int32_t _M0L6_2atmpS1894;
        int32_t _M0L6_2atmpS1893;
        int32_t _M0L6_2atmpS1892;
        while (1) {
          if (_M0L1iS559 < _M0L7olengthS539) {
            int32_t _M0L6_2atmpS1882 = _M0Lm5indexS535;
            int32_t _M0L6_2atmpS1881 = _M0L6_2atmpS1882 + _M0L7olengthS539;
            int32_t _M0L6_2atmpS1880 = _M0L6_2atmpS1881 - _M0L1iS559;
            int32_t _M0L6_2atmpS1875 = _M0L6_2atmpS1880 - 1;
            uint64_t _M0L6_2atmpS1879 = _M0L6outputS560 % 10ull;
            int32_t _M0L6_2atmpS1878 = (int32_t)_M0L6_2atmpS1879;
            int32_t _M0L6_2atmpS1877 = 48 + _M0L6_2atmpS1878;
            int32_t _M0L6_2atmpS1876 = _M0L6_2atmpS1877 & 0xff;
            int32_t _M0L6_2atmpS1883;
            uint64_t _M0L6_2atmpS1884;
            if (
              _M0L6_2atmpS1875 < 0
              || _M0L6_2atmpS1875 >= Moonbit_array_length(_M0L6resultS534)
            ) {
              #line 610 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
              moonbit_panic();
            }
            _M0L6resultS534[_M0L6_2atmpS1875] = _M0L6_2atmpS1876;
            _M0L6_2atmpS1883 = _M0L1iS559 + 1;
            _M0L6_2atmpS1884 = _M0L6outputS560 / 10ull;
            _M0L1iS559 = _M0L6_2atmpS1883;
            _M0L6outputS560 = _M0L6_2atmpS1884;
            continue;
          }
          break;
        }
        _M0L6_2atmpS1885 = _M0Lm5indexS535;
        _M0Lm5indexS535 = _M0L6_2atmpS1885 + _M0L7olengthS539;
        _M0L6_2atmpS1890 = _M0Lm3expS540;
        _M0L7_2abindS562 = _M0L6_2atmpS1890 + 1;
        _M0L1iS563 = _M0L7olengthS539;
        while (1) {
          if (_M0L1iS563 < _M0L7_2abindS562) {
            int32_t _M0L6_2atmpS1888 = _M0Lm5indexS535;
            int32_t _M0L6_2atmpS1887 = _M0L6_2atmpS1888 + _M0L1iS563;
            int32_t _M0L6_2atmpS1886 = _M0L6_2atmpS1887 - _M0L7olengthS539;
            int32_t _M0L6_2atmpS1889;
            if (
              _M0L6_2atmpS1886 < 0
              || _M0L6_2atmpS1886 >= Moonbit_array_length(_M0L6resultS534)
            ) {
              #line 615 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
              moonbit_panic();
            }
            _M0L6resultS534[_M0L6_2atmpS1886] = 48;
            _M0L6_2atmpS1889 = _M0L1iS563 + 1;
            _M0L1iS563 = _M0L6_2atmpS1889;
            continue;
          }
          break;
        }
        _M0L6_2atmpS1891 = _M0Lm5indexS535;
        _M0L6_2atmpS1894 = _M0Lm3expS540;
        _M0L6_2atmpS1893 = _M0L6_2atmpS1894 + 1;
        _M0L6_2atmpS1892 = _M0L6_2atmpS1893 - _M0L7olengthS539;
        _M0Lm5indexS535 = _M0L6_2atmpS1891 + _M0L6_2atmpS1892;
      } else {
        int32_t _M0L6_2atmpS1911 = _M0Lm5indexS535;
        int32_t _M0L6_2atmpS1910 = _M0L6_2atmpS1911 + 1;
        int32_t _M0L1iS565 = 0;
        int32_t _M0L7currentS566 = _M0L6_2atmpS1910;
        uint64_t _M0L6outputS567 = _M0L6outputS537;
        int32_t _M0L6_2atmpS1912;
        int32_t _M0L6_2atmpS1913;
        while (1) {
          if (_M0L1iS565 < _M0L7olengthS539) {
            int32_t _M0L6_2atmpS1906 = _M0L7olengthS539 - _M0L1iS565;
            int32_t _M0L6_2atmpS1904 = _M0L6_2atmpS1906 - 1;
            int32_t _M0L6_2atmpS1905 = _M0Lm3expS540;
            int32_t _M0L7currentS568;
            int32_t _M0L6_2atmpS1901;
            int32_t _M0L6_2atmpS1900;
            int32_t _M0L6_2atmpS1895;
            uint64_t _M0L6_2atmpS1899;
            int32_t _M0L6_2atmpS1898;
            int32_t _M0L6_2atmpS1897;
            int32_t _M0L6_2atmpS1896;
            int32_t _M0L6_2atmpS1902;
            uint64_t _M0L6_2atmpS1903;
            if (_M0L6_2atmpS1904 == _M0L6_2atmpS1905) {
              int32_t _M0L6_2atmpS1909 = _M0L7currentS566 + _M0L7olengthS539;
              int32_t _M0L6_2atmpS1908 = _M0L6_2atmpS1909 - _M0L1iS565;
              int32_t _M0L6_2atmpS1907 = _M0L6_2atmpS1908 - 1;
              if (
                _M0L6_2atmpS1907 < 0
                || _M0L6_2atmpS1907 >= Moonbit_array_length(_M0L6resultS534)
              ) {
                #line 622 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
                moonbit_panic();
              }
              _M0L6resultS534[_M0L6_2atmpS1907] = 46;
              _M0L7currentS568 = _M0L7currentS566 - 1;
            } else {
              _M0L7currentS568 = _M0L7currentS566;
            }
            _M0L6_2atmpS1901 = _M0L7currentS568 + _M0L7olengthS539;
            _M0L6_2atmpS1900 = _M0L6_2atmpS1901 - _M0L1iS565;
            _M0L6_2atmpS1895 = _M0L6_2atmpS1900 - 1;
            _M0L6_2atmpS1899 = _M0L6outputS567 % 10ull;
            _M0L6_2atmpS1898 = (int32_t)_M0L6_2atmpS1899;
            _M0L6_2atmpS1897 = 48 + _M0L6_2atmpS1898;
            _M0L6_2atmpS1896 = _M0L6_2atmpS1897 & 0xff;
            if (
              _M0L6_2atmpS1895 < 0
              || _M0L6_2atmpS1895 >= Moonbit_array_length(_M0L6resultS534)
            ) {
              #line 627 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
              moonbit_panic();
            }
            _M0L6resultS534[_M0L6_2atmpS1895] = _M0L6_2atmpS1896;
            _M0L6_2atmpS1902 = _M0L1iS565 + 1;
            _M0L6_2atmpS1903 = _M0L6outputS567 / 10ull;
            _M0L1iS565 = _M0L6_2atmpS1902;
            _M0L7currentS566 = _M0L7currentS568;
            _M0L6outputS567 = _M0L6_2atmpS1903;
            continue;
          }
          break;
        }
        _M0L6_2atmpS1912 = _M0Lm5indexS535;
        _M0L6_2atmpS1913 = _M0L7olengthS539 + 1;
        _M0Lm5indexS535 = _M0L6_2atmpS1912 + _M0L6_2atmpS1913;
      }
    }
    _M0L6_2atmpS1914 = _M0Lm5indexS535;
    #line 632 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _result_3131
    = _M0FPB19string__from__bytes(_M0L6resultS534, 0, _M0L6_2atmpS1914);
    moonbit_decref(_M0L6resultS534);
    return _result_3131;
  }
}

struct _M0TPB17FloatingDecimal64* _M0FPB3d2d(
  uint64_t _M0L12ieeeMantissaS480,
  uint32_t _M0L12ieeeExponentS479
) {
  int32_t _M0Lm2e2S477;
  uint64_t _M0Lm2m2S478;
  uint64_t _M0L6_2atmpS1788;
  uint64_t _M0L6_2atmpS1787;
  int32_t _M0L4evenS481;
  uint64_t _M0L6_2atmpS1786;
  uint64_t _M0L2mvS482;
  int32_t _M0L7mmShiftS483;
  uint64_t _M0Lm2vrS484;
  uint64_t _M0Lm2vpS485;
  uint64_t _M0Lm2vmS486;
  int32_t _M0Lm3e10S487;
  int32_t _M0Lm17vmIsTrailingZerosS488;
  int32_t _M0Lm17vrIsTrailingZerosS489;
  int32_t _M0L6_2atmpS1688;
  int32_t _M0Lm7removedS508;
  int32_t _M0Lm16lastRemovedDigitS509;
  uint64_t _M0Lm6outputS510;
  int32_t _M0L6_2atmpS1784;
  int32_t _M0L6_2atmpS1785;
  int32_t _M0L3expS533;
  uint64_t _M0L6_2atmpS1783;
  struct _M0TPB17FloatingDecimal64* _block_3137;
  #line 347 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0Lm2e2S477 = 0;
  _M0Lm2m2S478 = 0ull;
  if (_M0L12ieeeExponentS479 == 0u) {
    _M0Lm2e2S477 = -1076;
    _M0Lm2m2S478 = _M0L12ieeeMantissaS480;
  } else {
    int32_t _M0L6_2atmpS1687 = *(int32_t*)&_M0L12ieeeExponentS479;
    int32_t _M0L6_2atmpS1686 = _M0L6_2atmpS1687 - 1023;
    int32_t _M0L6_2atmpS1685 = _M0L6_2atmpS1686 - 52;
    _M0Lm2e2S477 = _M0L6_2atmpS1685 - 2;
    _M0Lm2m2S478 = 4503599627370496ull | _M0L12ieeeMantissaS480;
  }
  _M0L6_2atmpS1788 = _M0Lm2m2S478;
  _M0L6_2atmpS1787 = _M0L6_2atmpS1788 & 1ull;
  _M0L4evenS481 = _M0L6_2atmpS1787 == 0ull;
  _M0L6_2atmpS1786 = _M0Lm2m2S478;
  _M0L2mvS482 = 4ull * _M0L6_2atmpS1786;
  _M0L7mmShiftS483
  = _M0L12ieeeMantissaS480 != 0ull || _M0L12ieeeExponentS479 <= 1u;
  _M0Lm2vrS484 = 0ull;
  _M0Lm2vpS485 = 0ull;
  _M0Lm2vmS486 = 0ull;
  _M0Lm3e10S487 = 0;
  _M0Lm17vmIsTrailingZerosS488 = 0;
  _M0Lm17vrIsTrailingZerosS489 = 0;
  _M0L6_2atmpS1688 = _M0Lm2e2S477;
  if (_M0L6_2atmpS1688 >= 0) {
    int32_t _M0L6_2atmpS1710 = _M0Lm2e2S477;
    int32_t _M0L6_2atmpS1706;
    int32_t _M0L6_2atmpS1709;
    int32_t _M0L6_2atmpS1708;
    int32_t _M0L6_2atmpS1707;
    int32_t _M0L1qS490;
    int32_t _M0L6_2atmpS1705;
    int32_t _M0L6_2atmpS1704;
    int32_t _M0L1kS491;
    int32_t _M0L6_2atmpS1703;
    int32_t _M0L6_2atmpS1702;
    int32_t _M0L6_2atmpS1701;
    int32_t _M0L1iS492;
    struct _M0TPB8Pow5Pair _M0L4pow5S493;
    uint64_t _M0L6_2atmpS1700;
    struct _M0TPB19MulShiftAll64Result _M0L7_2abindS494;
    uint64_t _M0L8_2avrOutS495;
    uint64_t _M0L8_2avpOutS496;
    uint64_t _M0L8_2avmOutS497;
    #line 383 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1706 = _M0FPB9log10Pow2(_M0L6_2atmpS1710);
    _M0L6_2atmpS1709 = _M0Lm2e2S477;
    _M0L6_2atmpS1708 = _M0L6_2atmpS1709 > 3;
    #line 383 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1707 = _M0MPC14bool4Bool7to__int(_M0L6_2atmpS1708);
    _M0L1qS490 = _M0L6_2atmpS1706 - _M0L6_2atmpS1707;
    _M0Lm3e10S487 = _M0L1qS490;
    #line 385 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1705 = _M0FPB8pow5bits(_M0L1qS490);
    _M0L6_2atmpS1704 = 125 + _M0L6_2atmpS1705;
    _M0L1kS491 = _M0L6_2atmpS1704 - 1;
    _M0L6_2atmpS1703 = _M0Lm2e2S477;
    _M0L6_2atmpS1702 = -_M0L6_2atmpS1703;
    _M0L6_2atmpS1701 = _M0L6_2atmpS1702 + _M0L1qS490;
    _M0L1iS492 = _M0L6_2atmpS1701 + _M0L1kS491;
    #line 387 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L4pow5S493 = _M0FPB22double__computeInvPow5(_M0L1qS490);
    _M0L6_2atmpS1700 = _M0Lm2m2S478;
    #line 388 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L7_2abindS494
    = _M0FPB13mulShiftAll64(_M0L6_2atmpS1700, _M0L4pow5S493, _M0L1iS492, _M0L7mmShiftS483);
    _M0L8_2avrOutS495 = _M0L7_2abindS494.$0;
    _M0L8_2avpOutS496 = _M0L7_2abindS494.$1;
    _M0L8_2avmOutS497 = _M0L7_2abindS494.$2;
    _M0Lm2vrS484 = _M0L8_2avrOutS495;
    _M0Lm2vpS485 = _M0L8_2avpOutS496;
    _M0Lm2vmS486 = _M0L8_2avmOutS497;
    if (_M0L1qS490 <= 21) {
      int32_t _M0L6_2atmpS1696 = (int32_t)_M0L2mvS482;
      uint64_t _M0L6_2atmpS1699 = _M0L2mvS482 / 5ull;
      int32_t _M0L6_2atmpS1698 = (int32_t)_M0L6_2atmpS1699;
      int32_t _M0L6_2atmpS1697 = 5 * _M0L6_2atmpS1698;
      int32_t _M0L6mvMod5S498 = _M0L6_2atmpS1696 - _M0L6_2atmpS1697;
      if (_M0L6mvMod5S498 == 0) {
        #line 400 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0Lm17vrIsTrailingZerosS489
        = _M0FPB18multipleOfPowerOf5(_M0L2mvS482, _M0L1qS490);
      } else if (_M0L4evenS481) {
        uint64_t _M0L6_2atmpS1690 = _M0L2mvS482 - 1ull;
        uint64_t _M0L6_2atmpS1691;
        uint64_t _M0L6_2atmpS1689;
        #line 406 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0L6_2atmpS1691 = _M0MPC14bool4Bool10to__uint64(_M0L7mmShiftS483);
        _M0L6_2atmpS1689 = _M0L6_2atmpS1690 - _M0L6_2atmpS1691;
        #line 405 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0Lm17vmIsTrailingZerosS488
        = _M0FPB18multipleOfPowerOf5(_M0L6_2atmpS1689, _M0L1qS490);
      } else {
        uint64_t _M0L6_2atmpS1692 = _M0Lm2vpS485;
        uint64_t _M0L6_2atmpS1695 = _M0L2mvS482 + 2ull;
        int32_t _M0L6_2atmpS1694;
        uint64_t _M0L6_2atmpS1693;
        #line 410 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0L6_2atmpS1694
        = _M0FPB18multipleOfPowerOf5(_M0L6_2atmpS1695, _M0L1qS490);
        #line 410 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0L6_2atmpS1693 = _M0MPC14bool4Bool10to__uint64(_M0L6_2atmpS1694);
        _M0Lm2vpS485 = _M0L6_2atmpS1692 - _M0L6_2atmpS1693;
      }
    }
  } else {
    int32_t _M0L6_2atmpS1724 = _M0Lm2e2S477;
    int32_t _M0L6_2atmpS1723 = -_M0L6_2atmpS1724;
    int32_t _M0L6_2atmpS1718;
    int32_t _M0L6_2atmpS1722;
    int32_t _M0L6_2atmpS1721;
    int32_t _M0L6_2atmpS1720;
    int32_t _M0L6_2atmpS1719;
    int32_t _M0L1qS499;
    int32_t _M0L6_2atmpS1711;
    int32_t _M0L6_2atmpS1717;
    int32_t _M0L6_2atmpS1716;
    int32_t _M0L1iS500;
    int32_t _M0L6_2atmpS1715;
    int32_t _M0L1kS501;
    int32_t _M0L1jS502;
    struct _M0TPB8Pow5Pair _M0L4pow5S503;
    uint64_t _M0L6_2atmpS1714;
    struct _M0TPB19MulShiftAll64Result _M0L7_2abindS504;
    uint64_t _M0L8_2avrOutS505;
    uint64_t _M0L8_2avpOutS506;
    uint64_t _M0L8_2avmOutS507;
    #line 415 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1718 = _M0FPB9log10Pow5(_M0L6_2atmpS1723);
    _M0L6_2atmpS1722 = _M0Lm2e2S477;
    _M0L6_2atmpS1721 = -_M0L6_2atmpS1722;
    _M0L6_2atmpS1720 = _M0L6_2atmpS1721 > 1;
    #line 415 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1719 = _M0MPC14bool4Bool7to__int(_M0L6_2atmpS1720);
    _M0L1qS499 = _M0L6_2atmpS1718 - _M0L6_2atmpS1719;
    _M0L6_2atmpS1711 = _M0Lm2e2S477;
    _M0Lm3e10S487 = _M0L1qS499 + _M0L6_2atmpS1711;
    _M0L6_2atmpS1717 = _M0Lm2e2S477;
    _M0L6_2atmpS1716 = -_M0L6_2atmpS1717;
    _M0L1iS500 = _M0L6_2atmpS1716 - _M0L1qS499;
    #line 418 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1715 = _M0FPB8pow5bits(_M0L1iS500);
    _M0L1kS501 = _M0L6_2atmpS1715 - 125;
    _M0L1jS502 = _M0L1qS499 - _M0L1kS501;
    #line 420 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L4pow5S503 = _M0FPB19double__computePow5(_M0L1iS500);
    _M0L6_2atmpS1714 = _M0Lm2m2S478;
    #line 421 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L7_2abindS504
    = _M0FPB13mulShiftAll64(_M0L6_2atmpS1714, _M0L4pow5S503, _M0L1jS502, _M0L7mmShiftS483);
    _M0L8_2avrOutS505 = _M0L7_2abindS504.$0;
    _M0L8_2avpOutS506 = _M0L7_2abindS504.$1;
    _M0L8_2avmOutS507 = _M0L7_2abindS504.$2;
    _M0Lm2vrS484 = _M0L8_2avrOutS505;
    _M0Lm2vpS485 = _M0L8_2avpOutS506;
    _M0Lm2vmS486 = _M0L8_2avmOutS507;
    if (_M0L1qS499 <= 1) {
      _M0Lm17vrIsTrailingZerosS489 = 1;
      if (_M0L4evenS481) {
        int32_t _M0L6_2atmpS1712;
        #line 432 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0L6_2atmpS1712 = _M0MPC14bool4Bool7to__int(_M0L7mmShiftS483);
        _M0Lm17vmIsTrailingZerosS488 = _M0L6_2atmpS1712 == 1;
      } else {
        uint64_t _M0L6_2atmpS1713 = _M0Lm2vpS485;
        _M0Lm2vpS485 = _M0L6_2atmpS1713 - 1ull;
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
    int32_t _if__result_3134;
    uint64_t _M0L6_2atmpS1754;
    uint64_t _M0L6_2atmpS1760;
    uint64_t _M0L6_2atmpS1761;
    int32_t _if__result_3135;
    int32_t _M0L6_2atmpS1757;
    int64_t _M0L6_2atmpS1756;
    uint64_t _M0L6_2atmpS1755;
    while (1) {
      uint64_t _M0L6_2atmpS1737 = _M0Lm2vpS485;
      uint64_t _M0L7vpDiv10S511 = _M0L6_2atmpS1737 / 10ull;
      uint64_t _M0L6_2atmpS1736 = _M0Lm2vmS486;
      uint64_t _M0L7vmDiv10S512 = _M0L6_2atmpS1736 / 10ull;
      uint64_t _M0L6_2atmpS1735;
      int32_t _M0L6_2atmpS1732;
      int32_t _M0L6_2atmpS1734;
      int32_t _M0L6_2atmpS1733;
      int32_t _M0L7vmMod10S514;
      uint64_t _M0L6_2atmpS1731;
      uint64_t _M0L7vrDiv10S515;
      uint64_t _M0L6_2atmpS1730;
      int32_t _M0L6_2atmpS1727;
      int32_t _M0L6_2atmpS1729;
      int32_t _M0L6_2atmpS1728;
      int32_t _M0L7vrMod10S516;
      int32_t _M0L6_2atmpS1726;
      if (_M0L7vpDiv10S511 <= _M0L7vmDiv10S512) {
        break;
      }
      _M0L6_2atmpS1735 = _M0Lm2vmS486;
      _M0L6_2atmpS1732 = (int32_t)_M0L6_2atmpS1735;
      _M0L6_2atmpS1734 = (int32_t)_M0L7vmDiv10S512;
      _M0L6_2atmpS1733 = 10 * _M0L6_2atmpS1734;
      _M0L7vmMod10S514 = _M0L6_2atmpS1732 - _M0L6_2atmpS1733;
      _M0L6_2atmpS1731 = _M0Lm2vrS484;
      _M0L7vrDiv10S515 = _M0L6_2atmpS1731 / 10ull;
      _M0L6_2atmpS1730 = _M0Lm2vrS484;
      _M0L6_2atmpS1727 = (int32_t)_M0L6_2atmpS1730;
      _M0L6_2atmpS1729 = (int32_t)_M0L7vrDiv10S515;
      _M0L6_2atmpS1728 = 10 * _M0L6_2atmpS1729;
      _M0L7vrMod10S516 = _M0L6_2atmpS1727 - _M0L6_2atmpS1728;
      _M0Lm17vmIsTrailingZerosS488
      = _M0Lm17vmIsTrailingZerosS488 && _M0L7vmMod10S514 == 0;
      if (_M0Lm17vrIsTrailingZerosS489) {
        int32_t _M0L6_2atmpS1725 = _M0Lm16lastRemovedDigitS509;
        _M0Lm17vrIsTrailingZerosS489 = _M0L6_2atmpS1725 == 0;
      } else {
        _M0Lm17vrIsTrailingZerosS489 = 0;
      }
      _M0Lm16lastRemovedDigitS509 = _M0L7vrMod10S516;
      _M0Lm2vrS484 = _M0L7vrDiv10S515;
      _M0Lm2vpS485 = _M0L7vpDiv10S511;
      _M0Lm2vmS486 = _M0L7vmDiv10S512;
      _M0L6_2atmpS1726 = _M0Lm7removedS508;
      _M0Lm7removedS508 = _M0L6_2atmpS1726 + 1;
      continue;
      break;
    }
    if (_M0Lm17vmIsTrailingZerosS488) {
      while (1) {
        uint64_t _M0L6_2atmpS1750 = _M0Lm2vmS486;
        uint64_t _M0L7vmDiv10S517 = _M0L6_2atmpS1750 / 10ull;
        uint64_t _M0L6_2atmpS1749 = _M0Lm2vmS486;
        int32_t _M0L6_2atmpS1746 = (int32_t)_M0L6_2atmpS1749;
        int32_t _M0L6_2atmpS1748 = (int32_t)_M0L7vmDiv10S517;
        int32_t _M0L6_2atmpS1747 = 10 * _M0L6_2atmpS1748;
        int32_t _M0L7vmMod10S518 = _M0L6_2atmpS1746 - _M0L6_2atmpS1747;
        uint64_t _M0L6_2atmpS1745;
        uint64_t _M0L7vpDiv10S520;
        uint64_t _M0L6_2atmpS1744;
        uint64_t _M0L7vrDiv10S521;
        uint64_t _M0L6_2atmpS1743;
        int32_t _M0L6_2atmpS1740;
        int32_t _M0L6_2atmpS1742;
        int32_t _M0L6_2atmpS1741;
        int32_t _M0L7vrMod10S522;
        int32_t _M0L6_2atmpS1739;
        if (_M0L7vmMod10S518 != 0) {
          break;
        }
        _M0L6_2atmpS1745 = _M0Lm2vpS485;
        _M0L7vpDiv10S520 = _M0L6_2atmpS1745 / 10ull;
        _M0L6_2atmpS1744 = _M0Lm2vrS484;
        _M0L7vrDiv10S521 = _M0L6_2atmpS1744 / 10ull;
        _M0L6_2atmpS1743 = _M0Lm2vrS484;
        _M0L6_2atmpS1740 = (int32_t)_M0L6_2atmpS1743;
        _M0L6_2atmpS1742 = (int32_t)_M0L7vrDiv10S521;
        _M0L6_2atmpS1741 = 10 * _M0L6_2atmpS1742;
        _M0L7vrMod10S522 = _M0L6_2atmpS1740 - _M0L6_2atmpS1741;
        if (_M0Lm17vrIsTrailingZerosS489) {
          int32_t _M0L6_2atmpS1738 = _M0Lm16lastRemovedDigitS509;
          _M0Lm17vrIsTrailingZerosS489 = _M0L6_2atmpS1738 == 0;
        } else {
          _M0Lm17vrIsTrailingZerosS489 = 0;
        }
        _M0Lm16lastRemovedDigitS509 = _M0L7vrMod10S522;
        _M0Lm2vrS484 = _M0L7vrDiv10S521;
        _M0Lm2vpS485 = _M0L7vpDiv10S520;
        _M0Lm2vmS486 = _M0L7vmDiv10S517;
        _M0L6_2atmpS1739 = _M0Lm7removedS508;
        _M0Lm7removedS508 = _M0L6_2atmpS1739 + 1;
        continue;
        break;
      }
    }
    if (_M0Lm17vrIsTrailingZerosS489) {
      int32_t _M0L6_2atmpS1753 = _M0Lm16lastRemovedDigitS509;
      if (_M0L6_2atmpS1753 == 5) {
        uint64_t _M0L6_2atmpS1752 = _M0Lm2vrS484;
        uint64_t _M0L6_2atmpS1751 = _M0L6_2atmpS1752 % 2ull;
        _if__result_3134 = _M0L6_2atmpS1751 == 0ull;
      } else {
        _if__result_3134 = 0;
      }
    } else {
      _if__result_3134 = 0;
    }
    if (_if__result_3134) {
      _M0Lm16lastRemovedDigitS509 = 4;
    }
    _M0L6_2atmpS1754 = _M0Lm2vrS484;
    _M0L6_2atmpS1760 = _M0Lm2vrS484;
    _M0L6_2atmpS1761 = _M0Lm2vmS486;
    if (_M0L6_2atmpS1760 == _M0L6_2atmpS1761) {
      if (!_M0L4evenS481) {
        _if__result_3135 = 1;
      } else {
        int32_t _M0L6_2atmpS1759 = _M0Lm17vmIsTrailingZerosS488;
        _if__result_3135 = !_M0L6_2atmpS1759;
      }
    } else {
      _if__result_3135 = 0;
    }
    if (_if__result_3135) {
      _M0L6_2atmpS1757 = 1;
    } else {
      int32_t _M0L6_2atmpS1758 = _M0Lm16lastRemovedDigitS509;
      _M0L6_2atmpS1757 = _M0L6_2atmpS1758 >= 5;
    }
    #line 487 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1756 = _M0MPC14bool4Bool9to__int64(_M0L6_2atmpS1757);
    _M0L6_2atmpS1755 = *(uint64_t*)&_M0L6_2atmpS1756;
    _M0Lm6outputS510 = _M0L6_2atmpS1754 + _M0L6_2atmpS1755;
  } else {
    int32_t _M0Lm7roundUpS523 = 0;
    uint64_t _M0L6_2atmpS1782 = _M0Lm2vpS485;
    uint64_t _M0L8vpDiv100S524 = _M0L6_2atmpS1782 / 100ull;
    uint64_t _M0L6_2atmpS1781 = _M0Lm2vmS486;
    uint64_t _M0L8vmDiv100S525 = _M0L6_2atmpS1781 / 100ull;
    uint64_t _M0L6_2atmpS1776;
    uint64_t _M0L6_2atmpS1779;
    uint64_t _M0L6_2atmpS1780;
    int32_t _M0L6_2atmpS1778;
    uint64_t _M0L6_2atmpS1777;
    if (_M0L8vpDiv100S524 > _M0L8vmDiv100S525) {
      uint64_t _M0L6_2atmpS1767 = _M0Lm2vrS484;
      uint64_t _M0L8vrDiv100S526 = _M0L6_2atmpS1767 / 100ull;
      uint64_t _M0L6_2atmpS1766 = _M0Lm2vrS484;
      int32_t _M0L6_2atmpS1763 = (int32_t)_M0L6_2atmpS1766;
      int32_t _M0L6_2atmpS1765 = (int32_t)_M0L8vrDiv100S526;
      int32_t _M0L6_2atmpS1764 = 100 * _M0L6_2atmpS1765;
      int32_t _M0L8vrMod100S527 = _M0L6_2atmpS1763 - _M0L6_2atmpS1764;
      int32_t _M0L6_2atmpS1762;
      _M0Lm7roundUpS523 = _M0L8vrMod100S527 >= 50;
      _M0Lm2vrS484 = _M0L8vrDiv100S526;
      _M0Lm2vpS485 = _M0L8vpDiv100S524;
      _M0Lm2vmS486 = _M0L8vmDiv100S525;
      _M0L6_2atmpS1762 = _M0Lm7removedS508;
      _M0Lm7removedS508 = _M0L6_2atmpS1762 + 2;
    }
    while (1) {
      uint64_t _M0L6_2atmpS1775 = _M0Lm2vpS485;
      uint64_t _M0L7vpDiv10S528 = _M0L6_2atmpS1775 / 10ull;
      uint64_t _M0L6_2atmpS1774 = _M0Lm2vmS486;
      uint64_t _M0L7vmDiv10S529 = _M0L6_2atmpS1774 / 10ull;
      uint64_t _M0L6_2atmpS1773;
      uint64_t _M0L7vrDiv10S531;
      uint64_t _M0L6_2atmpS1772;
      int32_t _M0L6_2atmpS1769;
      int32_t _M0L6_2atmpS1771;
      int32_t _M0L6_2atmpS1770;
      int32_t _M0L7vrMod10S532;
      int32_t _M0L6_2atmpS1768;
      if (_M0L7vpDiv10S528 <= _M0L7vmDiv10S529) {
        break;
      }
      _M0L6_2atmpS1773 = _M0Lm2vrS484;
      _M0L7vrDiv10S531 = _M0L6_2atmpS1773 / 10ull;
      _M0L6_2atmpS1772 = _M0Lm2vrS484;
      _M0L6_2atmpS1769 = (int32_t)_M0L6_2atmpS1772;
      _M0L6_2atmpS1771 = (int32_t)_M0L7vrDiv10S531;
      _M0L6_2atmpS1770 = 10 * _M0L6_2atmpS1771;
      _M0L7vrMod10S532 = _M0L6_2atmpS1769 - _M0L6_2atmpS1770;
      _M0Lm7roundUpS523 = _M0L7vrMod10S532 >= 5;
      _M0Lm2vrS484 = _M0L7vrDiv10S531;
      _M0Lm2vpS485 = _M0L7vpDiv10S528;
      _M0Lm2vmS486 = _M0L7vmDiv10S529;
      _M0L6_2atmpS1768 = _M0Lm7removedS508;
      _M0Lm7removedS508 = _M0L6_2atmpS1768 + 1;
      continue;
      break;
    }
    _M0L6_2atmpS1776 = _M0Lm2vrS484;
    _M0L6_2atmpS1779 = _M0Lm2vrS484;
    _M0L6_2atmpS1780 = _M0Lm2vmS486;
    _M0L6_2atmpS1778
    = _M0L6_2atmpS1779 == _M0L6_2atmpS1780 || _M0Lm7roundUpS523;
    #line 522 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1777 = _M0MPC14bool4Bool10to__uint64(_M0L6_2atmpS1778);
    _M0Lm6outputS510 = _M0L6_2atmpS1776 + _M0L6_2atmpS1777;
  }
  _M0L6_2atmpS1784 = _M0Lm3e10S487;
  _M0L6_2atmpS1785 = _M0Lm7removedS508;
  _M0L3expS533 = _M0L6_2atmpS1784 + _M0L6_2atmpS1785;
  _M0L6_2atmpS1783 = _M0Lm6outputS510;
  _block_3137
  = (struct _M0TPB17FloatingDecimal64*)moonbit_malloc(sizeof(struct _M0TPB17FloatingDecimal64));
  Moonbit_object_header(_block_3137)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_3137->$0 = _M0L6_2atmpS1783;
  _block_3137->$1 = _M0L3expS533;
  return _block_3137;
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
  int32_t _M0L6_2atmpS1684;
  int32_t _M0L6_2atmpS1683;
  int32_t _M0L4baseS455;
  int32_t _M0L5base2S457;
  int32_t _M0L6offsetS458;
  int32_t _M0L6_2atmpS1682;
  uint64_t _M0L4mul0S459;
  int32_t _M0L6_2atmpS1681;
  int32_t _M0L6_2atmpS1680;
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
  int32_t _M0L6_2atmpS1678;
  int32_t _M0L6_2atmpS1679;
  int32_t _M0L5deltaS470;
  uint64_t _M0L6_2atmpS1677;
  uint64_t _M0L6_2atmpS1669;
  int32_t _M0L6_2atmpS1676;
  uint32_t _M0L6_2atmpS1673;
  int32_t _M0L6_2atmpS1675;
  int32_t _M0L6_2atmpS1674;
  uint32_t _M0L6_2atmpS1672;
  uint32_t _M0L6_2atmpS1671;
  uint64_t _M0L6_2atmpS1670;
  uint64_t _M0L1aS471;
  uint64_t _M0L6_2atmpS1668;
  uint64_t _M0L1bS472;
  #line 239 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1684 = _M0L1iS456 + 26;
  _M0L6_2atmpS1683 = _M0L6_2atmpS1684 - 1;
  _M0L4baseS455 = _M0L6_2atmpS1683 / 26;
  _M0L5base2S457 = _M0L4baseS455 * 26;
  _M0L6offsetS458 = _M0L5base2S457 - _M0L1iS456;
  _M0L6_2atmpS1682 = _M0L4baseS455 * 2;
  #line 243 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4mul0S459
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB26gDOUBLE__POW5__INV__SPLIT2, _M0L6_2atmpS1682);
  _M0L6_2atmpS1681 = _M0L4baseS455 * 2;
  _M0L6_2atmpS1680 = _M0L6_2atmpS1681 + 1;
  #line 244 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4mul1S460
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB26gDOUBLE__POW5__INV__SPLIT2, _M0L6_2atmpS1680);
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
    uint64_t _M0L6_2atmpS1667 = _M0Lm5high1S469;
    _M0Lm5high1S469 = _M0L6_2atmpS1667 + 1ull;
  }
  #line 256 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1678 = _M0FPB8pow5bits(_M0L5base2S457);
  #line 256 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1679 = _M0FPB8pow5bits(_M0L1iS456);
  _M0L5deltaS470 = _M0L6_2atmpS1678 - _M0L6_2atmpS1679;
  #line 257 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1677
  = _M0FPB13shiftright128(_M0L7_2alow0S466, _M0L3sumS468, _M0L5deltaS470);
  _M0L6_2atmpS1669 = _M0L6_2atmpS1677 + 1ull;
  _M0L6_2atmpS1676 = _M0L1iS456 / 16;
  #line 259 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1673
  = _M0MPC15array13ReadOnlyArray2atGjE(_M0FPB19gPOW5__INV__OFFSETS, _M0L6_2atmpS1676);
  _M0L6_2atmpS1675 = _M0L1iS456 % 16;
  _M0L6_2atmpS1674 = _M0L6_2atmpS1675 << 1;
  _M0L6_2atmpS1672 = _M0L6_2atmpS1673 >> (_M0L6_2atmpS1674 & 31);
  _M0L6_2atmpS1671 = _M0L6_2atmpS1672 & 3u;
  #line 259 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1670 = _M0MPC14uint4UInt10to__uint64(_M0L6_2atmpS1671);
  _M0L1aS471 = _M0L6_2atmpS1669 + _M0L6_2atmpS1670;
  _M0L6_2atmpS1668 = _M0Lm5high1S469;
  #line 260 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L1bS472
  = _M0FPB13shiftright128(_M0L3sumS468, _M0L6_2atmpS1668, _M0L5deltaS470);
  return (struct _M0TPB8Pow5Pair){.$0 = _M0L1aS471, .$1 = _M0L1bS472};
}

struct _M0TPB8Pow5Pair _M0FPB19double__computePow5(int32_t _M0L1iS438) {
  int32_t _M0L4baseS437;
  int32_t _M0L5base2S439;
  int32_t _M0L6offsetS440;
  int32_t _M0L6_2atmpS1666;
  uint64_t _M0L4mul0S441;
  int32_t _M0L6_2atmpS1665;
  int32_t _M0L6_2atmpS1664;
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
  int32_t _M0L6_2atmpS1662;
  int32_t _M0L6_2atmpS1663;
  int32_t _M0L5deltaS452;
  uint64_t _M0L6_2atmpS1654;
  int32_t _M0L6_2atmpS1661;
  uint32_t _M0L6_2atmpS1658;
  int32_t _M0L6_2atmpS1660;
  int32_t _M0L6_2atmpS1659;
  uint32_t _M0L6_2atmpS1657;
  uint32_t _M0L6_2atmpS1656;
  uint64_t _M0L6_2atmpS1655;
  uint64_t _M0L1aS453;
  uint64_t _M0L6_2atmpS1653;
  uint64_t _M0L1bS454;
  #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4baseS437 = _M0L1iS438 / 26;
  _M0L5base2S439 = _M0L4baseS437 * 26;
  _M0L6offsetS440 = _M0L1iS438 - _M0L5base2S439;
  _M0L6_2atmpS1666 = _M0L4baseS437 * 2;
  #line 217 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4mul0S441
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB21gDOUBLE__POW5__SPLIT2, _M0L6_2atmpS1666);
  _M0L6_2atmpS1665 = _M0L4baseS437 * 2;
  _M0L6_2atmpS1664 = _M0L6_2atmpS1665 + 1;
  #line 218 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4mul1S442
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB21gDOUBLE__POW5__SPLIT2, _M0L6_2atmpS1664);
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
    uint64_t _M0L6_2atmpS1652 = _M0Lm5high1S451;
    _M0Lm5high1S451 = _M0L6_2atmpS1652 + 1ull;
  }
  #line 230 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1662 = _M0FPB8pow5bits(_M0L1iS438);
  #line 230 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1663 = _M0FPB8pow5bits(_M0L5base2S439);
  _M0L5deltaS452 = _M0L6_2atmpS1662 - _M0L6_2atmpS1663;
  #line 231 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1654
  = _M0FPB13shiftright128(_M0L7_2alow0S448, _M0L3sumS450, _M0L5deltaS452);
  _M0L6_2atmpS1661 = _M0L1iS438 / 16;
  #line 232 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1658
  = _M0MPC15array13ReadOnlyArray2atGjE(_M0FPB14gPOW5__OFFSETS, _M0L6_2atmpS1661);
  _M0L6_2atmpS1660 = _M0L1iS438 % 16;
  _M0L6_2atmpS1659 = _M0L6_2atmpS1660 << 1;
  _M0L6_2atmpS1657 = _M0L6_2atmpS1658 >> (_M0L6_2atmpS1659 & 31);
  _M0L6_2atmpS1656 = _M0L6_2atmpS1657 & 3u;
  #line 232 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1655 = _M0MPC14uint4UInt10to__uint64(_M0L6_2atmpS1656);
  _M0L1aS453 = _M0L6_2atmpS1654 + _M0L6_2atmpS1655;
  _M0L6_2atmpS1653 = _M0Lm5high1S451;
  #line 233 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L1bS454
  = _M0FPB13shiftright128(_M0L3sumS450, _M0L6_2atmpS1653, _M0L5deltaS452);
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
  uint64_t _M0L6_2atmpS1651;
  uint64_t _M0L2hiS419;
  uint64_t _M0L3lo2S420;
  uint64_t _M0L6_2atmpS1649;
  uint64_t _M0L6_2atmpS1650;
  uint64_t _M0L4mid2S421;
  uint64_t _M0L6_2atmpS1648;
  uint64_t _M0L3hi2S422;
  int32_t _M0L6_2atmpS1647;
  int32_t _M0L6_2atmpS1646;
  uint64_t _M0L2vpS423;
  uint64_t _M0Lm2vmS425;
  int32_t _M0L6_2atmpS1645;
  int32_t _M0L6_2atmpS1644;
  uint64_t _M0L2vrS436;
  uint64_t _M0L6_2atmpS1643;
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
    _M0L6_2atmpS1651 = 1ull;
  } else {
    _M0L6_2atmpS1651 = 0ull;
  }
  _M0L2hiS419 = _M0L6_2ahi2S417 + _M0L6_2atmpS1651;
  _M0L3lo2S420 = _M0L5_2aloS413 + _M0L7_2amul0S407;
  _M0L6_2atmpS1649 = _M0L3midS418 + _M0L7_2amul1S409;
  if (_M0L3lo2S420 < _M0L5_2aloS413) {
    _M0L6_2atmpS1650 = 1ull;
  } else {
    _M0L6_2atmpS1650 = 0ull;
  }
  _M0L4mid2S421 = _M0L6_2atmpS1649 + _M0L6_2atmpS1650;
  if (_M0L4mid2S421 < _M0L3midS418) {
    _M0L6_2atmpS1648 = 1ull;
  } else {
    _M0L6_2atmpS1648 = 0ull;
  }
  _M0L3hi2S422 = _M0L2hiS419 + _M0L6_2atmpS1648;
  _M0L6_2atmpS1647 = _M0L1jS424 - 64;
  _M0L6_2atmpS1646 = _M0L6_2atmpS1647 - 1;
  #line 144 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L2vpS423
  = _M0FPB13shiftright128(_M0L4mid2S421, _M0L3hi2S422, _M0L6_2atmpS1646);
  _M0Lm2vmS425 = 0ull;
  if (_M0L7mmShiftS426) {
    uint64_t _M0L3lo3S427 = _M0L5_2aloS413 - _M0L7_2amul0S407;
    uint64_t _M0L6_2atmpS1633 = _M0L3midS418 - _M0L7_2amul1S409;
    uint64_t _M0L6_2atmpS1634;
    uint64_t _M0L4mid3S428;
    uint64_t _M0L6_2atmpS1632;
    uint64_t _M0L3hi3S429;
    int32_t _M0L6_2atmpS1631;
    int32_t _M0L6_2atmpS1630;
    if (_M0L5_2aloS413 < _M0L3lo3S427) {
      _M0L6_2atmpS1634 = 1ull;
    } else {
      _M0L6_2atmpS1634 = 0ull;
    }
    _M0L4mid3S428 = _M0L6_2atmpS1633 - _M0L6_2atmpS1634;
    if (_M0L3midS418 < _M0L4mid3S428) {
      _M0L6_2atmpS1632 = 1ull;
    } else {
      _M0L6_2atmpS1632 = 0ull;
    }
    _M0L3hi3S429 = _M0L2hiS419 - _M0L6_2atmpS1632;
    _M0L6_2atmpS1631 = _M0L1jS424 - 64;
    _M0L6_2atmpS1630 = _M0L6_2atmpS1631 - 1;
    #line 150 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0Lm2vmS425
    = _M0FPB13shiftright128(_M0L4mid3S428, _M0L3hi3S429, _M0L6_2atmpS1630);
  } else {
    uint64_t _M0L3lo3S430 = _M0L5_2aloS413 + _M0L5_2aloS413;
    uint64_t _M0L6_2atmpS1641 = _M0L3midS418 + _M0L3midS418;
    uint64_t _M0L6_2atmpS1642;
    uint64_t _M0L4mid3S431;
    uint64_t _M0L6_2atmpS1639;
    uint64_t _M0L6_2atmpS1640;
    uint64_t _M0L3hi3S432;
    uint64_t _M0L3lo4S433;
    uint64_t _M0L6_2atmpS1637;
    uint64_t _M0L6_2atmpS1638;
    uint64_t _M0L4mid4S434;
    uint64_t _M0L6_2atmpS1636;
    uint64_t _M0L3hi4S435;
    int32_t _M0L6_2atmpS1635;
    if (_M0L3lo3S430 < _M0L5_2aloS413) {
      _M0L6_2atmpS1642 = 1ull;
    } else {
      _M0L6_2atmpS1642 = 0ull;
    }
    _M0L4mid3S431 = _M0L6_2atmpS1641 + _M0L6_2atmpS1642;
    _M0L6_2atmpS1639 = _M0L2hiS419 + _M0L2hiS419;
    if (_M0L4mid3S431 < _M0L3midS418) {
      _M0L6_2atmpS1640 = 1ull;
    } else {
      _M0L6_2atmpS1640 = 0ull;
    }
    _M0L3hi3S432 = _M0L6_2atmpS1639 + _M0L6_2atmpS1640;
    _M0L3lo4S433 = _M0L3lo3S430 - _M0L7_2amul0S407;
    _M0L6_2atmpS1637 = _M0L4mid3S431 - _M0L7_2amul1S409;
    if (_M0L3lo3S430 < _M0L3lo4S433) {
      _M0L6_2atmpS1638 = 1ull;
    } else {
      _M0L6_2atmpS1638 = 0ull;
    }
    _M0L4mid4S434 = _M0L6_2atmpS1637 - _M0L6_2atmpS1638;
    if (_M0L4mid3S431 < _M0L4mid4S434) {
      _M0L6_2atmpS1636 = 1ull;
    } else {
      _M0L6_2atmpS1636 = 0ull;
    }
    _M0L3hi4S435 = _M0L3hi3S432 - _M0L6_2atmpS1636;
    _M0L6_2atmpS1635 = _M0L1jS424 - 64;
    #line 158 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0Lm2vmS425
    = _M0FPB13shiftright128(_M0L4mid4S434, _M0L3hi4S435, _M0L6_2atmpS1635);
  }
  _M0L6_2atmpS1645 = _M0L1jS424 - 64;
  _M0L6_2atmpS1644 = _M0L6_2atmpS1645 - 1;
  #line 160 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L2vrS436
  = _M0FPB13shiftright128(_M0L3midS418, _M0L2hiS419, _M0L6_2atmpS1644);
  _M0L6_2atmpS1643 = _M0Lm2vmS425;
  return (struct _M0TPB19MulShiftAll64Result){.$0 = _M0L2vrS436,
                                                .$1 = _M0L2vpS423,
                                                .$2 = _M0L6_2atmpS1643};
}

int32_t _M0FPB18multipleOfPowerOf2(
  uint64_t _M0L5valueS405,
  int32_t _M0L1pS406
) {
  uint64_t _M0L6_2atmpS1629;
  uint64_t _M0L6_2atmpS1628;
  uint64_t _M0L6_2atmpS1627;
  #line 124 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1629 = 1ull << (_M0L1pS406 & 63);
  _M0L6_2atmpS1628 = _M0L6_2atmpS1629 - 1ull;
  _M0L6_2atmpS1627 = _M0L5valueS405 & _M0L6_2atmpS1628;
  return _M0L6_2atmpS1627 == 0ull;
}

int32_t _M0FPB18multipleOfPowerOf5(
  uint64_t _M0L5valueS403,
  int32_t _M0L1pS404
) {
  int32_t _M0L6_2atmpS1626;
  #line 119 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  #line 120 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1626 = _M0FPB10pow5Factor(_M0L5valueS403);
  return _M0L6_2atmpS1626 >= _M0L1pS404;
}

int32_t _M0FPB10pow5Factor(uint64_t _M0L5valueS398) {
  uint64_t _M0L6_2atmpS1617;
  uint64_t _M0L6_2atmpS1618;
  uint64_t _M0L6_2atmpS1619;
  uint64_t _M0L6_2atmpS1620;
  uint64_t _M0L6_2atmpS1625;
  int32_t _M0L5countS399;
  uint64_t _M0L1vS400;
  #line 94 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1617 = _M0L5valueS398 % 5ull;
  if (_M0L6_2atmpS1617 != 0ull) {
    return 0;
  }
  _M0L6_2atmpS1618 = _M0L5valueS398 % 25ull;
  if (_M0L6_2atmpS1618 != 0ull) {
    return 1;
  }
  _M0L6_2atmpS1619 = _M0L5valueS398 % 125ull;
  if (_M0L6_2atmpS1619 != 0ull) {
    return 2;
  }
  _M0L6_2atmpS1620 = _M0L5valueS398 % 625ull;
  if (_M0L6_2atmpS1620 != 0ull) {
    return 3;
  }
  _M0L6_2atmpS1625 = _M0L5valueS398 / 625ull;
  _M0L5countS399 = 4;
  _M0L1vS400 = _M0L6_2atmpS1625;
  while (1) {
    if (_M0L1vS400 > 0ull) {
      uint64_t _M0L6_2atmpS1621 = _M0L1vS400 % 5ull;
      int32_t _M0L6_2atmpS1622;
      uint64_t _M0L6_2atmpS1623;
      if (_M0L6_2atmpS1621 != 0ull) {
        return _M0L5countS399;
      }
      _M0L6_2atmpS1622 = _M0L5countS399 + 1;
      _M0L6_2atmpS1623 = _M0L1vS400 / 5ull;
      _M0L5countS399 = _M0L6_2atmpS1622;
      _M0L1vS400 = _M0L6_2atmpS1623;
      continue;
    } else {
      struct _M0TPB13StringBuilder* _M0L18_2astring__builderS402;
      moonbit_string_t _M0L6_2atmpS1624;
      int32_t _result_3139;
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0L18_2astring__builderS402
      = _M0MPB13StringBuilder21StringBuilder_2einner(25);
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS402, (moonbit_string_t)moonbit_string_literal_5.data);
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0MPB13StringBuilder13write__objectGmE(_M0L18_2astring__builderS402, _M0L5valueS398);
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0L6_2atmpS1624
      = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS402);
      moonbit_decref(_M0L18_2astring__builderS402);
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _result_3139 = _M0FPC15abort5abortGiE(_M0L6_2atmpS1624);
      moonbit_decref(_M0L6_2atmpS1624);
      return _result_3139;
    }
    break;
  }
}

uint64_t _M0FPB13shiftright128(
  uint64_t _M0L2loS397,
  uint64_t _M0L2hiS395,
  int32_t _M0L4distS396
) {
  int32_t _M0L6_2atmpS1616;
  uint64_t _M0L6_2atmpS1614;
  uint64_t _M0L6_2atmpS1615;
  #line 89 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1616 = 64 - _M0L4distS396;
  _M0L6_2atmpS1614 = _M0L2hiS395 << (_M0L6_2atmpS1616 & 63);
  _M0L6_2atmpS1615 = _M0L2loS397 >> (_M0L4distS396 & 63);
  return _M0L6_2atmpS1614 | _M0L6_2atmpS1615;
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
  uint64_t _M0L6_2atmpS1612;
  uint64_t _M0L6_2atmpS1613;
  uint64_t _M0L1yS391;
  uint64_t _M0L6_2atmpS1610;
  uint64_t _M0L6_2atmpS1611;
  uint64_t _M0L1zS392;
  uint64_t _M0L6_2atmpS1608;
  uint64_t _M0L6_2atmpS1609;
  uint64_t _M0L6_2atmpS1606;
  uint64_t _M0L6_2atmpS1607;
  uint64_t _M0L1wS393;
  uint64_t _M0L2loS394;
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L3aLoS384 = _M0L1aS385 & 4294967295ull;
  _M0L3aHiS386 = _M0L1aS385 >> 32;
  _M0L3bLoS387 = _M0L1bS388 & 4294967295ull;
  _M0L3bHiS389 = _M0L1bS388 >> 32;
  _M0L1xS390 = _M0L3aLoS384 * _M0L3bLoS387;
  _M0L6_2atmpS1612 = _M0L3aHiS386 * _M0L3bLoS387;
  _M0L6_2atmpS1613 = _M0L1xS390 >> 32;
  _M0L1yS391 = _M0L6_2atmpS1612 + _M0L6_2atmpS1613;
  _M0L6_2atmpS1610 = _M0L3aLoS384 * _M0L3bHiS389;
  _M0L6_2atmpS1611 = _M0L1yS391 & 4294967295ull;
  _M0L1zS392 = _M0L6_2atmpS1610 + _M0L6_2atmpS1611;
  _M0L6_2atmpS1608 = _M0L3aHiS386 * _M0L3bHiS389;
  _M0L6_2atmpS1609 = _M0L1yS391 >> 32;
  _M0L6_2atmpS1606 = _M0L6_2atmpS1608 + _M0L6_2atmpS1609;
  _M0L6_2atmpS1607 = _M0L1zS392 >> 32;
  _M0L1wS393 = _M0L6_2atmpS1606 + _M0L6_2atmpS1607;
  _M0L2loS394 = _M0L1aS385 * _M0L1bS388;
  return (struct _M0TPB7Umul128){.$0 = _M0L2loS394, .$1 = _M0L1wS393};
}

moonbit_string_t _M0FPB19string__from__bytes(
  moonbit_bytes_t _M0L5bytesS382,
  int32_t _M0L4fromS379,
  int32_t _M0L2toS378
) {
  int32_t _M0L3lenS377;
  int32_t _M0L6_2atmpS1605;
  uint16_t* _M0L6bufferS380;
  int32_t _M0L1iS381;
  #line 52 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L3lenS377 = _M0L2toS378 - _M0L4fromS379;
  #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1605 = _M0IPC16uint166UInt16PB7Default7default();
  _M0L6bufferS380
  = (uint16_t*)moonbit_make_string(_M0L3lenS377, _M0L6_2atmpS1605);
  _M0L1iS381 = 0;
  while (1) {
    if (_M0L1iS381 < _M0L3lenS377) {
      int32_t _M0L6_2atmpS1603 = _M0L4fromS379 + _M0L1iS381;
      int32_t _M0L6_2atmpS1602;
      int32_t _M0L6_2atmpS1601;
      int32_t _M0L6_2atmpS1604;
      if (
        _M0L6_2atmpS1603 < 0
        || _M0L6_2atmpS1603 >= Moonbit_array_length(_M0L5bytesS382)
      ) {
        #line 56 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6_2atmpS1602 = (int32_t)_M0L5bytesS382[_M0L6_2atmpS1603];
      _M0L6_2atmpS1601 = (uint16_t)_M0L6_2atmpS1602;
      if (
        _M0L1iS381 < 0 || _M0L1iS381 >= Moonbit_array_length(_M0L6bufferS380)
      ) {
        #line 56 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6bufferS380[_M0L1iS381] = _M0L6_2atmpS1601;
      _M0L6_2atmpS1604 = _M0L1iS381 + 1;
      _M0L1iS381 = _M0L6_2atmpS1604;
      continue;
    }
    break;
  }
  return _M0L6bufferS380;
}

int32_t _M0FPB9log10Pow2(int32_t _M0L1eS376) {
  int32_t _M0L6_2atmpS1600;
  uint32_t _M0L6_2atmpS1599;
  uint32_t _M0L6_2atmpS1598;
  #line 44 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1600 = _M0L1eS376 * 78913;
  _M0L6_2atmpS1599 = *(uint32_t*)&_M0L6_2atmpS1600;
  _M0L6_2atmpS1598 = _M0L6_2atmpS1599 >> 18;
  return *(int32_t*)&_M0L6_2atmpS1598;
}

int32_t _M0FPB9log10Pow5(int32_t _M0L1eS375) {
  int32_t _M0L6_2atmpS1597;
  uint32_t _M0L6_2atmpS1596;
  uint32_t _M0L6_2atmpS1595;
  #line 37 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1597 = _M0L1eS375 * 732923;
  _M0L6_2atmpS1596 = *(uint32_t*)&_M0L6_2atmpS1597;
  _M0L6_2atmpS1595 = _M0L6_2atmpS1596 >> 20;
  return *(int32_t*)&_M0L6_2atmpS1595;
}

moonbit_string_t _M0FPB18copy__special__str(
  int32_t _M0L4signS373,
  int32_t _M0L8exponentS374,
  int32_t _M0L8mantissaS371
) {
  moonbit_string_t _M0L1sS372;
  moonbit_string_t _result_3142;
  #line 23 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  if (_M0L8mantissaS371) {
    return (moonbit_string_t)moonbit_string_literal_6.data;
  }
  if (_M0L4signS373) {
    _M0L1sS372 = (moonbit_string_t)moonbit_string_literal_7.data;
  } else {
    _M0L1sS372 = (moonbit_string_t)moonbit_string_literal_8.data;
  }
  if (_M0L8exponentS374) {
    moonbit_string_t _result_3141;
    #line 29 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _result_3141
    = moonbit_add_string(_M0L1sS372, (moonbit_string_t)moonbit_string_literal_9.data);
    moonbit_decref(_M0L1sS372);
    return _result_3141;
  }
  #line 31 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _result_3142
  = moonbit_add_string(_M0L1sS372, (moonbit_string_t)moonbit_string_literal_10.data);
  moonbit_decref(_M0L1sS372);
  return _result_3142;
}

int32_t _M0FPB8pow5bits(int32_t _M0L1eS370) {
  int32_t _M0L6_2atmpS1594;
  uint32_t _M0L6_2atmpS1593;
  uint32_t _M0L6_2atmpS1592;
  int32_t _M0L6_2atmpS1591;
  #line 18 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1594 = _M0L1eS370 * 1217359;
  _M0L6_2atmpS1593 = *(uint32_t*)&_M0L6_2atmpS1594;
  _M0L6_2atmpS1592 = _M0L6_2atmpS1593 >> 19;
  _M0L6_2atmpS1591 = *(int32_t*)&_M0L6_2atmpS1592;
  return _M0L6_2atmpS1591 + 1;
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
  float* _M0L6_2atmpS1587;
  struct _M0TPB5ArrayGfE* _block_3143;
  #line 30 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1587 = (float*)moonbit_make_float_array_raw(_M0L3lenS364);
  _block_3143
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_block_3143)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 0, 0);
  _block_3143->$0 = _M0L6_2atmpS1587;
  _block_3143->$1 = _M0L3lenS364;
  return _block_3143;
}

struct _M0TPB5ArrayGbE* _M0MPC15array5Array20unsafe__make__uninitGbE(
  int32_t _M0L3lenS365
) {
  uint8_t* _M0L6_2atmpS1588;
  struct _M0TPB5ArrayGbE* _block_3144;
  #line 30 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1588 = (uint8_t*)moonbit_make_bytes_raw(_M0L3lenS365);
  _block_3144
  = (struct _M0TPB5ArrayGbE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGbE));
  Moonbit_object_header(_block_3144)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 101, 0);
  _block_3144->$0 = _M0L6_2atmpS1588;
  _block_3144->$1 = _M0L3lenS365;
  return _block_3144;
}

struct _M0TPB5ArrayGiE* _M0MPC15array5Array20unsafe__make__uninitGiE(
  int32_t _M0L3lenS366
) {
  int32_t* _M0L6_2atmpS1589;
  struct _M0TPB5ArrayGiE* _block_3145;
  #line 30 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1589 = (int32_t*)moonbit_make_int32_array_raw(_M0L3lenS366);
  _block_3145
  = (struct _M0TPB5ArrayGiE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGiE));
  Moonbit_object_header(_block_3145)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 3, 0);
  _block_3145->$0 = _M0L6_2atmpS1589;
  _block_3145->$1 = _M0L3lenS366;
  return _block_3145;
}

struct _M0TPB5ArrayGRPB5ArrayGfEE* _M0MPC15array5Array20unsafe__make__uninitGRPB5ArrayGfEE(
  int32_t _M0L3lenS367
) {
  struct _M0TPB5ArrayGfE** _M0L6_2atmpS1590;
  struct _M0TPB5ArrayGRPB5ArrayGfEE* _block_3146;
  #line 30 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1590
  = (struct _M0TPB5ArrayGfE**)moonbit_make_ref_array(_M0L3lenS367, 0);
  _block_3146
  = (struct _M0TPB5ArrayGRPB5ArrayGfEE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGRPB5ArrayGfEE));
  Moonbit_object_header(_block_3146)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 104, 0);
  _block_3146->$0 = _M0L6_2atmpS1590;
  _block_3146->$1 = _M0L3lenS367;
  return _block_3146;
}

uint64_t _M0MPC15array13ReadOnlyArray2atGmE(
  uint64_t* _M0L4selfS360,
  int32_t _M0L5indexS361
) {
  uint64_t* _M0L6_2atmpS1585;
  #line 38 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\readonlyarray.mbt"
  _M0L6_2atmpS1585 = _M0L4selfS360;
  if (
    _M0L5indexS361 < 0
    || _M0L5indexS361 >= Moonbit_array_length(_M0L6_2atmpS1585)
  ) {
    #line 40 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\readonlyarray.mbt"
    moonbit_panic();
  }
  return (uint64_t)_M0L6_2atmpS1585[_M0L5indexS361];
}

uint32_t _M0MPC15array13ReadOnlyArray2atGjE(
  uint32_t* _M0L4selfS362,
  int32_t _M0L5indexS363
) {
  uint32_t* _M0L6_2atmpS1586;
  #line 38 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\readonlyarray.mbt"
  _M0L6_2atmpS1586 = _M0L4selfS362;
  if (
    _M0L5indexS363 < 0
    || _M0L5indexS363 >= Moonbit_array_length(_M0L6_2atmpS1586)
  ) {
    #line 40 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\readonlyarray.mbt"
    moonbit_panic();
  }
  return (uint32_t)_M0L6_2atmpS1586[_M0L5indexS363];
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
  int32_t _M0L3lenS1571;
  float* _M0L6_2atmpS1573;
  int32_t _M0L6_2atmpS1572;
  int32_t _M0L6lengthS352;
  float* _M0L3bufS1576;
  int32_t _M0L6_2atmpS1577;
  #line 406 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L3lenS1571 = _M0L4selfS351->$1;
  #line 408 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1573 = _M0MPC15array5Array6bufferGfE(_M0L4selfS351);
  _M0L6_2atmpS1572 = Moonbit_array_length(_M0L6_2atmpS1573);
  moonbit_decref(_M0L6_2atmpS1573);
  if (_M0L3lenS1571 == _M0L6_2atmpS1572) {
    int32_t _M0L3lenS1575 = _M0L4selfS351->$1;
    int32_t _M0L6_2atmpS1574 = _M0L3lenS1575 + 1;
    #line 409 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0MPC15array5Array7reallocGfE(_M0L4selfS351, _M0L6_2atmpS1574);
  }
  _M0L6lengthS352 = _M0L4selfS351->$1;
  _M0L3bufS1576 = _M0L4selfS351->$0;
  _M0L3bufS1576[_M0L6lengthS352] = _M0L5valueS353;
  _M0L6_2atmpS1577 = _M0L6lengthS352 + 1;
  _M0L4selfS351->$1 = _M0L6_2atmpS1577;
  return 0;
}

int32_t _M0MPC15array5Array4pushGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS354,
  int32_t _M0L5valueS356
) {
  int32_t _M0L3lenS1578;
  int32_t* _M0L6_2atmpS1580;
  int32_t _M0L6_2atmpS1579;
  int32_t _M0L6lengthS355;
  int32_t* _M0L3bufS1583;
  int32_t _M0L6_2atmpS1584;
  #line 406 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L3lenS1578 = _M0L4selfS354->$1;
  #line 408 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1580 = _M0MPC15array5Array6bufferGiE(_M0L4selfS354);
  _M0L6_2atmpS1579 = Moonbit_array_length(_M0L6_2atmpS1580);
  moonbit_decref(_M0L6_2atmpS1580);
  if (_M0L3lenS1578 == _M0L6_2atmpS1579) {
    int32_t _M0L3lenS1582 = _M0L4selfS354->$1;
    int32_t _M0L6_2atmpS1581 = _M0L3lenS1582 + 1;
    #line 409 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0MPC15array5Array7reallocGiE(_M0L4selfS354, _M0L6_2atmpS1581);
  }
  _M0L6lengthS355 = _M0L4selfS354->$1;
  _M0L3bufS1583 = _M0L4selfS354->$0;
  _M0L3bufS1583[_M0L6lengthS355] = _M0L5valueS356;
  _M0L6_2atmpS1584 = _M0L6lengthS355 + 1;
  _M0L4selfS354->$1 = _M0L6_2atmpS1584;
  return 0;
}

int32_t _M0MPC15array5Array7reallocGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS344,
  int32_t _M0L8requiredS346
) {
  int32_t _M0L8old__capS343;
  int32_t _M0L3lenS1569;
  int32_t _M0L8new__capS345;
  #line 302 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  #line 304 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__capS343 = _M0MPC15array5Array8capacityGfE(_M0L4selfS344);
  _M0L3lenS1569 = _M0L4selfS344->$1;
  #line 305 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__capS345
  = _M0FPB23array__growth__capacity(_M0L8old__capS343, _M0L3lenS1569, _M0L8requiredS346);
  #line 306 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0MPC15array5Array14resize__bufferGfE(_M0L4selfS344, _M0L8new__capS345);
  return 0;
}

int32_t _M0MPC15array5Array7reallocGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS348,
  int32_t _M0L8requiredS350
) {
  int32_t _M0L8old__capS347;
  int32_t _M0L3lenS1570;
  int32_t _M0L8new__capS349;
  #line 302 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  #line 304 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__capS347 = _M0MPC15array5Array8capacityGiE(_M0L4selfS348);
  _M0L3lenS1570 = _M0L4selfS348->$1;
  #line 305 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__capS349
  = _M0FPB23array__growth__capacity(_M0L8old__capS347, _M0L3lenS1570, _M0L8requiredS350);
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
  float* _M0L6_2aoldS3029;
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
  _M0L6_2aoldS3029 = _M0L4selfS332->$0;
  moonbit_decref(_M0L6_2aoldS3029);
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
  int32_t* _M0L6_2aoldS3030;
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
  _M0L6_2aoldS3030 = _M0L4selfS338->$0;
  moonbit_decref(_M0L6_2aoldS3030);
  _M0L4selfS338->$0 = _M0L8new__bufS342;
  return 0;
}

int32_t _M0MPC15array5Array8capacityGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS329
) {
  float* _M0L6_2atmpS1567;
  int32_t _result_3147;
  #line 132 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L6_2atmpS1567 = _M0MPC15array5Array6bufferGfE(_M0L4selfS329);
  _result_3147 = Moonbit_array_length(_M0L6_2atmpS1567);
  moonbit_decref(_M0L6_2atmpS1567);
  return _result_3147;
}

int32_t _M0MPC15array5Array8capacityGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS330
) {
  int32_t* _M0L6_2atmpS1568;
  int32_t _result_3148;
  #line 132 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L6_2atmpS1568 = _M0MPC15array5Array6bufferGiE(_M0L4selfS330);
  _result_3148 = Moonbit_array_length(_M0L6_2atmpS1568);
  moonbit_decref(_M0L6_2atmpS1568);
  return _result_3148;
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
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_11.data);
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
  float* _M0L8_2afieldS3031;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS3031 = _M0L4selfS316->$0;
  moonbit_incref(_M0L8_2afieldS3031);
  return _M0L8_2afieldS3031;
}

uint8_t* _M0MPC15array5Array6bufferGbE(struct _M0TPB5ArrayGbE* _M0L4selfS317) {
  uint8_t* _M0L8_2afieldS3032;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS3032 = _M0L4selfS317->$0;
  moonbit_incref(_M0L8_2afieldS3032);
  return _M0L8_2afieldS3032;
}

int32_t* _M0MPC15array5Array6bufferGiE(struct _M0TPB5ArrayGiE* _M0L4selfS318) {
  int32_t* _M0L8_2afieldS3033;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS3033 = _M0L4selfS318->$0;
  moonbit_incref(_M0L8_2afieldS3033);
  return _M0L8_2afieldS3033;
}

struct _M0TPB5ArrayGfE** _M0MPC15array5Array6bufferGRPB5ArrayGfEE(
  struct _M0TPB5ArrayGRPB5ArrayGfEE* _M0L4selfS319
) {
  struct _M0TPB5ArrayGfE** _M0L8_2afieldS3034;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS3034 = _M0L4selfS319->$0;
  moonbit_incref(_M0L8_2afieldS3034);
  return _M0L8_2afieldS3034;
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
  int32_t _M0L3endS1565;
  int32_t _M0L5startS1566;
  int32_t _M0L8str__lenS311;
  int32_t _M0L3lenS1564;
  int32_t _M0L8requiredS313;
  uint16_t* _M0L4dataS1557;
  int32_t _M0L6_2atmpS1556;
  int32_t _if__result_3150;
  uint16_t* _M0L4dataS1558;
  int32_t _M0L3lenS1559;
  moonbit_string_t _M0L6_2atmpS1560;
  int32_t _M0L6_2atmpS1561;
  int32_t _M0L3lenS1563;
  int32_t _M0L6_2atmpS1562;
  #line 158 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L3endS1565 = _M0L3strS312.$2;
  _M0L5startS1566 = _M0L3strS312.$1;
  _M0L8str__lenS311 = _M0L3endS1565 - _M0L5startS1566;
  if (_M0L8str__lenS311 == 0) {
    return 0;
  }
  _M0L3lenS1564 = _M0L4selfS314->$1;
  _M0L8requiredS313 = _M0L3lenS1564 + _M0L8str__lenS311;
  _M0L4dataS1557 = _M0L4selfS314->$0;
  _M0L6_2atmpS1556 = Moonbit_array_length(_M0L4dataS1557);
  if (_M0L8requiredS313 > _M0L6_2atmpS1556) {
    _if__result_3150 = 1;
  } else {
    int32_t _M0L3lenS1555 = _M0L4selfS314->$1;
    _if__result_3150 = _M0L8requiredS313 < _M0L3lenS1555;
  }
  if (_if__result_3150) {
    #line 168 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0MPB13StringBuilder4grow(_M0L4selfS314, _M0L8requiredS313);
  }
  _M0L4dataS1558 = _M0L4selfS314->$0;
  _M0L3lenS1559 = _M0L4selfS314->$1;
  moonbit_incref(_M0L4dataS1558);
  #line 172 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L6_2atmpS1560 = _M0MPC16string10StringView4data(_M0L3strS312);
  #line 173 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L6_2atmpS1561 = _M0MPC16string10StringView13start__offset(_M0L3strS312);
  #line 170 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0MPC15array10FixedArray26unsafe__blit__from__string(_M0L4dataS1558, _M0L3lenS1559, _M0L6_2atmpS1560, _M0L6_2atmpS1561, _M0L8str__lenS311);
  moonbit_decref(_M0L4dataS1558);
  moonbit_decref(_M0L6_2atmpS1560);
  _M0L3lenS1563 = _M0L4selfS314->$1;
  _M0L6_2atmpS1562 = _M0L3lenS1563 + _M0L8str__lenS311;
  _M0L4selfS314->$1 = _M0L6_2atmpS1562;
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
    int64_t _M0L6_2atmpS1554 = -_M0L4selfS286;
    _M0L3numS288 = *(uint64_t*)&_M0L6_2atmpS1554;
  } else {
    _M0L3numS288 = *(uint64_t*)&_M0L4selfS286;
  }
  switch (_M0L5radixS285) {
    case 10: {
      int32_t _M0L10digit__lenS290;
      int32_t _M0L6_2atmpS1551;
      int32_t _M0L10total__lenS291;
      uint16_t* _M0L6bufferS292;
      int32_t _M0L12digit__startS293;
      #line 573 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS290 = _M0FPB12dec__count64(_M0L3numS288);
      if (_M0L12is__negativeS287) {
        _M0L6_2atmpS1551 = 1;
      } else {
        _M0L6_2atmpS1551 = 0;
      }
      _M0L10total__lenS291 = _M0L10digit__lenS290 + _M0L6_2atmpS1551;
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
      int32_t _M0L6_2atmpS1552;
      int32_t _M0L10total__lenS295;
      uint16_t* _M0L6bufferS296;
      int32_t _M0L12digit__startS297;
      #line 581 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS294 = _M0FPB12hex__count64(_M0L3numS288);
      if (_M0L12is__negativeS287) {
        _M0L6_2atmpS1552 = 1;
      } else {
        _M0L6_2atmpS1552 = 0;
      }
      _M0L10total__lenS295 = _M0L10digit__lenS294 + _M0L6_2atmpS1552;
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
      int32_t _M0L6_2atmpS1553;
      int32_t _M0L10total__lenS299;
      uint16_t* _M0L6bufferS300;
      int32_t _M0L12digit__startS301;
      #line 589 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS298
      = _M0FPB14radix__count64(_M0L3numS288, _M0L5radixS285);
      if (_M0L12is__negativeS287) {
        _M0L6_2atmpS1553 = 1;
      } else {
        _M0L6_2atmpS1553 = 0;
      }
      _M0L10total__lenS299 = _M0L10digit__lenS298 + _M0L6_2atmpS1553;
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
  int32_t _M0L6_2atmpS1550;
  uint64_t _M0L3numS261;
  int32_t _M0L6offsetS262;
  #line 493 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L6_2atmpS1550 = _M0L10total__lenS284 - _M0L12digit__startS272;
  _M0L3numS261 = _M0L3numS283;
  _M0L6offsetS262 = _M0L6_2atmpS1550;
  while (1) {
    if (_M0L3numS261 >= 10000ull) {
      uint64_t _M0L1tS263 = _M0L3numS261 / 10000ull;
      uint64_t _M0L6_2atmpS1527 = _M0L3numS261 % 10000ull;
      int32_t _M0L1rS264 = (int32_t)_M0L6_2atmpS1527;
      int32_t _M0L2d1S265 = _M0L1rS264 / 100;
      int32_t _M0L2d2S266 = _M0L1rS264 % 100;
      int32_t _M0L6_2atmpS1526 = _M0L2d1S265 / 10;
      int32_t _M0L6_2atmpS1525 = 48 + _M0L6_2atmpS1526;
      int32_t _M0L6d1__hiS267 = (uint16_t)_M0L6_2atmpS1525;
      int32_t _M0L6_2atmpS1524 = _M0L2d1S265 % 10;
      int32_t _M0L6_2atmpS1523 = 48 + _M0L6_2atmpS1524;
      int32_t _M0L6d1__loS268 = (uint16_t)_M0L6_2atmpS1523;
      int32_t _M0L6_2atmpS1522 = _M0L2d2S266 / 10;
      int32_t _M0L6_2atmpS1521 = 48 + _M0L6_2atmpS1522;
      int32_t _M0L6d2__hiS269 = (uint16_t)_M0L6_2atmpS1521;
      int32_t _M0L6_2atmpS1520 = _M0L2d2S266 % 10;
      int32_t _M0L6_2atmpS1519 = 48 + _M0L6_2atmpS1520;
      int32_t _M0L6d2__loS270 = (uint16_t)_M0L6_2atmpS1519;
      int32_t _M0L6_2atmpS1511 = _M0L12digit__startS272 + _M0L6offsetS262;
      int32_t _M0L6_2atmpS1510 = _M0L6_2atmpS1511 - 4;
      int32_t _M0L6_2atmpS1513;
      int32_t _M0L6_2atmpS1512;
      int32_t _M0L6_2atmpS1515;
      int32_t _M0L6_2atmpS1514;
      int32_t _M0L6_2atmpS1517;
      int32_t _M0L6_2atmpS1516;
      int32_t _M0L6_2atmpS1518;
      _M0L6bufferS271[_M0L6_2atmpS1510] = _M0L6d1__hiS267;
      _M0L6_2atmpS1513 = _M0L12digit__startS272 + _M0L6offsetS262;
      _M0L6_2atmpS1512 = _M0L6_2atmpS1513 - 3;
      _M0L6bufferS271[_M0L6_2atmpS1512] = _M0L6d1__loS268;
      _M0L6_2atmpS1515 = _M0L12digit__startS272 + _M0L6offsetS262;
      _M0L6_2atmpS1514 = _M0L6_2atmpS1515 - 2;
      _M0L6bufferS271[_M0L6_2atmpS1514] = _M0L6d2__hiS269;
      _M0L6_2atmpS1517 = _M0L12digit__startS272 + _M0L6offsetS262;
      _M0L6_2atmpS1516 = _M0L6_2atmpS1517 - 1;
      _M0L6bufferS271[_M0L6_2atmpS1516] = _M0L6d2__loS270;
      _M0L6_2atmpS1518 = _M0L6offsetS262 - 4;
      _M0L3numS261 = _M0L1tS263;
      _M0L6offsetS262 = _M0L6_2atmpS1518;
      continue;
    } else {
      int32_t _M0L6_2atmpS1549 = (int32_t)_M0L3numS261;
      int32_t _M0L9remainingS274 = _M0L6_2atmpS1549;
      int32_t _M0L6offsetS275 = _M0L6offsetS262;
      while (1) {
        if (_M0L9remainingS274 >= 100) {
          int32_t _M0L1tS276 = _M0L9remainingS274 / 100;
          int32_t _M0L1dS277 = _M0L9remainingS274 % 100;
          int32_t _M0L6_2atmpS1536 = _M0L1dS277 / 10;
          int32_t _M0L6_2atmpS1535 = 48 + _M0L6_2atmpS1536;
          int32_t _M0L5d__hiS278 = (uint16_t)_M0L6_2atmpS1535;
          int32_t _M0L6_2atmpS1534 = _M0L1dS277 % 10;
          int32_t _M0L6_2atmpS1533 = 48 + _M0L6_2atmpS1534;
          int32_t _M0L5d__loS279 = (uint16_t)_M0L6_2atmpS1533;
          int32_t _M0L6_2atmpS1529 = _M0L12digit__startS272 + _M0L6offsetS275;
          int32_t _M0L6_2atmpS1528 = _M0L6_2atmpS1529 - 2;
          int32_t _M0L6_2atmpS1531;
          int32_t _M0L6_2atmpS1530;
          int32_t _M0L6_2atmpS1532;
          _M0L6bufferS271[_M0L6_2atmpS1528] = _M0L5d__hiS278;
          _M0L6_2atmpS1531 = _M0L12digit__startS272 + _M0L6offsetS275;
          _M0L6_2atmpS1530 = _M0L6_2atmpS1531 - 1;
          _M0L6bufferS271[_M0L6_2atmpS1530] = _M0L5d__loS279;
          _M0L6_2atmpS1532 = _M0L6offsetS275 - 2;
          _M0L9remainingS274 = _M0L1tS276;
          _M0L6offsetS275 = _M0L6_2atmpS1532;
          continue;
        } else if (_M0L9remainingS274 >= 10) {
          int32_t _M0L6_2atmpS1544 = _M0L9remainingS274 / 10;
          int32_t _M0L6_2atmpS1543 = 48 + _M0L6_2atmpS1544;
          int32_t _M0L5d__hiS281 = (uint16_t)_M0L6_2atmpS1543;
          int32_t _M0L6_2atmpS1542 = _M0L9remainingS274 % 10;
          int32_t _M0L6_2atmpS1541 = 48 + _M0L6_2atmpS1542;
          int32_t _M0L5d__loS282 = (uint16_t)_M0L6_2atmpS1541;
          int32_t _M0L6_2atmpS1538 = _M0L12digit__startS272 + _M0L6offsetS275;
          int32_t _M0L6_2atmpS1537 = _M0L6_2atmpS1538 - 2;
          int32_t _M0L6_2atmpS1540;
          int32_t _M0L6_2atmpS1539;
          _M0L6bufferS271[_M0L6_2atmpS1537] = _M0L5d__hiS281;
          _M0L6_2atmpS1540 = _M0L12digit__startS272 + _M0L6offsetS275;
          _M0L6_2atmpS1539 = _M0L6_2atmpS1540 - 1;
          _M0L6bufferS271[_M0L6_2atmpS1539] = _M0L5d__loS282;
        } else {
          int32_t _M0L6_2atmpS1548 = _M0L12digit__startS272 + _M0L6offsetS275;
          int32_t _M0L6_2atmpS1545 = _M0L6_2atmpS1548 - 1;
          int32_t _M0L6_2atmpS1547 = 48 + _M0L9remainingS274;
          int32_t _M0L6_2atmpS1546 = (uint16_t)_M0L6_2atmpS1547;
          _M0L6bufferS271[_M0L6_2atmpS1545] = _M0L6_2atmpS1546;
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
  int32_t _M0L6_2atmpS1495;
  int32_t _M0L6_2atmpS1494;
  #line 462 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  #line 470 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L4baseS244 = _M0MPC13int3Int10to__uint64(_M0L5radixS245);
  _M0L6_2atmpS1495 = _M0L5radixS245 - 1;
  _M0L6_2atmpS1494 = _M0L5radixS245 & _M0L6_2atmpS1495;
  if (_M0L6_2atmpS1494 == 0) {
    int32_t _M0L5shiftS246;
    uint64_t _M0L4maskS247;
    int32_t _M0L6_2atmpS1502;
    int32_t _M0L6offsetS248;
    uint64_t _M0L1nS249;
    #line 473 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0L5shiftS246 = moonbit_ctz32(_M0L5radixS245);
    _M0L4maskS247 = _M0L4baseS244 - 1ull;
    _M0L6_2atmpS1502 = _M0L10total__lenS254 - _M0L12digit__startS252;
    _M0L6offsetS248 = _M0L6_2atmpS1502;
    _M0L1nS249 = _M0L3numS255;
    while (1) {
      if (_M0L1nS249 > 0ull) {
        uint64_t _M0L6_2atmpS1501 = _M0L1nS249 & _M0L4maskS247;
        int32_t _M0L5digitS250 = (int32_t)_M0L6_2atmpS1501;
        int32_t _M0L6_2atmpS1498 = _M0L12digit__startS252 + _M0L6offsetS248;
        int32_t _M0L6_2atmpS1496 = _M0L6_2atmpS1498 - 1;
        int32_t _M0L6_2atmpS1497 =
          ((moonbit_string_t)moonbit_string_literal_13.data)[_M0L5digitS250];
        int32_t _M0L6_2atmpS1499;
        uint64_t _M0L6_2atmpS1500;
        _M0L6bufferS251[_M0L6_2atmpS1496] = _M0L6_2atmpS1497;
        _M0L6_2atmpS1499 = _M0L6offsetS248 - 1;
        _M0L6_2atmpS1500 = _M0L1nS249 >> (_M0L5shiftS246 & 63);
        _M0L6offsetS248 = _M0L6_2atmpS1499;
        _M0L1nS249 = _M0L6_2atmpS1500;
        continue;
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1509 = _M0L10total__lenS254 - _M0L12digit__startS252;
    int32_t _M0L6offsetS256 = _M0L6_2atmpS1509;
    uint64_t _M0L1nS257 = _M0L3numS255;
    while (1) {
      if (_M0L1nS257 > 0ull) {
        uint64_t _M0L1qS258 = _M0L1nS257 / _M0L4baseS244;
        uint64_t _M0L6_2atmpS1508 = _M0L1qS258 * _M0L4baseS244;
        uint64_t _M0L6_2atmpS1507 = _M0L1nS257 - _M0L6_2atmpS1508;
        int32_t _M0L5digitS259 = (int32_t)_M0L6_2atmpS1507;
        int32_t _M0L6_2atmpS1505 = _M0L12digit__startS252 + _M0L6offsetS256;
        int32_t _M0L6_2atmpS1503 = _M0L6_2atmpS1505 - 1;
        int32_t _M0L6_2atmpS1504 =
          ((moonbit_string_t)moonbit_string_literal_13.data)[_M0L5digitS259];
        int32_t _M0L6_2atmpS1506;
        _M0L6bufferS251[_M0L6_2atmpS1503] = _M0L6_2atmpS1504;
        _M0L6_2atmpS1506 = _M0L6offsetS256 - 1;
        _M0L6offsetS256 = _M0L6_2atmpS1506;
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
  int32_t _M0L6_2atmpS1493;
  int32_t _M0L6offsetS233;
  uint64_t _M0L1nS234;
  #line 434 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L6_2atmpS1493 = _M0L10total__lenS242 - _M0L12digit__startS239;
  _M0L6offsetS233 = _M0L6_2atmpS1493;
  _M0L1nS234 = _M0L3numS243;
  while (1) {
    if (_M0L6offsetS233 >= 2) {
      uint64_t _M0L6_2atmpS1490 = _M0L1nS234 & 255ull;
      int32_t _M0L9byte__valS235 = (int32_t)_M0L6_2atmpS1490;
      int32_t _M0L2hiS236 = _M0L9byte__valS235 / 16;
      int32_t _M0L2loS237 = _M0L9byte__valS235 % 16;
      int32_t _M0L6_2atmpS1484 = _M0L12digit__startS239 + _M0L6offsetS233;
      int32_t _M0L6_2atmpS1482 = _M0L6_2atmpS1484 - 2;
      int32_t _M0L6_2atmpS1483 =
        ((moonbit_string_t)moonbit_string_literal_13.data)[_M0L2hiS236];
      int32_t _M0L6_2atmpS1487;
      int32_t _M0L6_2atmpS1485;
      int32_t _M0L6_2atmpS1486;
      int32_t _M0L6_2atmpS1488;
      uint64_t _M0L6_2atmpS1489;
      _M0L6bufferS238[_M0L6_2atmpS1482] = _M0L6_2atmpS1483;
      _M0L6_2atmpS1487 = _M0L12digit__startS239 + _M0L6offsetS233;
      _M0L6_2atmpS1485 = _M0L6_2atmpS1487 - 1;
      _M0L6_2atmpS1486
      = ((moonbit_string_t)moonbit_string_literal_13.data)[
        _M0L2loS237
      ];
      _M0L6bufferS238[_M0L6_2atmpS1485] = _M0L6_2atmpS1486;
      _M0L6_2atmpS1488 = _M0L6offsetS233 - 2;
      _M0L6_2atmpS1489 = _M0L1nS234 >> 8;
      _M0L6offsetS233 = _M0L6_2atmpS1488;
      _M0L1nS234 = _M0L6_2atmpS1489;
      continue;
    } else if (_M0L6offsetS233 == 1) {
      uint64_t _M0L6_2atmpS1492 = _M0L1nS234 & 15ull;
      int32_t _M0L6nibbleS241 = (int32_t)_M0L6_2atmpS1492;
      int32_t _M0L6_2atmpS1491 =
        ((moonbit_string_t)moonbit_string_literal_13.data)[_M0L6nibbleS241];
      _M0L6bufferS238[_M0L12digit__startS239] = _M0L6_2atmpS1491;
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
      uint64_t _M0L6_2atmpS1480 = _M0L3numS230 / _M0L4baseS228;
      int32_t _M0L6_2atmpS1481 = _M0L5countS231 + 1;
      _M0L3numS230 = _M0L6_2atmpS1480;
      _M0L5countS231 = _M0L6_2atmpS1481;
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
    int32_t _M0L6_2atmpS1479;
    int32_t _M0L6_2atmpS1478;
    #line 412 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0L14leading__zerosS226 = moonbit_clz64(_M0L5valueS225);
    _M0L6_2atmpS1479 = 63 - _M0L14leading__zerosS226;
    _M0L6_2atmpS1478 = _M0L6_2atmpS1479 / 4;
    return _M0L6_2atmpS1478 + 1;
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
    int32_t _M0L6_2atmpS1477 = -_M0L4selfS208;
    _M0L3numS210 = *(uint32_t*)&_M0L6_2atmpS1477;
  } else {
    _M0L3numS210 = *(uint32_t*)&_M0L4selfS208;
  }
  switch (_M0L5radixS207) {
    case 10: {
      int32_t _M0L10digit__lenS212;
      int32_t _M0L6_2atmpS1474;
      int32_t _M0L10total__lenS213;
      uint16_t* _M0L6bufferS214;
      int32_t _M0L12digit__startS215;
      #line 235 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS212 = _M0FPB12dec__count32(_M0L3numS210);
      if (_M0L12is__negativeS209) {
        _M0L6_2atmpS1474 = 1;
      } else {
        _M0L6_2atmpS1474 = 0;
      }
      _M0L10total__lenS213 = _M0L10digit__lenS212 + _M0L6_2atmpS1474;
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
      int32_t _M0L6_2atmpS1475;
      int32_t _M0L10total__lenS217;
      uint16_t* _M0L6bufferS218;
      int32_t _M0L12digit__startS219;
      #line 243 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS216 = _M0FPB12hex__count32(_M0L3numS210);
      if (_M0L12is__negativeS209) {
        _M0L6_2atmpS1475 = 1;
      } else {
        _M0L6_2atmpS1475 = 0;
      }
      _M0L10total__lenS217 = _M0L10digit__lenS216 + _M0L6_2atmpS1475;
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
      int32_t _M0L6_2atmpS1476;
      int32_t _M0L10total__lenS221;
      uint16_t* _M0L6bufferS222;
      int32_t _M0L12digit__startS223;
      #line 251 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS220
      = _M0FPB14radix__count32(_M0L3numS210, _M0L5radixS207);
      if (_M0L12is__negativeS209) {
        _M0L6_2atmpS1476 = 1;
      } else {
        _M0L6_2atmpS1476 = 0;
      }
      _M0L10total__lenS221 = _M0L10digit__lenS220 + _M0L6_2atmpS1476;
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
      uint32_t _M0L6_2atmpS1472 = _M0L3numS204 / _M0L4baseS202;
      int32_t _M0L6_2atmpS1473 = _M0L5countS205 + 1;
      _M0L3numS204 = _M0L6_2atmpS1472;
      _M0L5countS205 = _M0L6_2atmpS1473;
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
    int32_t _M0L6_2atmpS1471;
    int32_t _M0L6_2atmpS1470;
    #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0L14leading__zerosS200 = moonbit_clz32(_M0L5valueS199);
    _M0L6_2atmpS1471 = 31 - _M0L14leading__zerosS200;
    _M0L6_2atmpS1470 = _M0L6_2atmpS1471 / 4;
    return _M0L6_2atmpS1470 + 1;
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
  int32_t _M0L6_2atmpS1469;
  uint32_t _M0L3numS174;
  int32_t _M0L6offsetS175;
  #line 88 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L6_2atmpS1469 = _M0L10total__lenS197 - _M0L12digit__startS185;
  _M0L3numS174 = _M0L3numS196;
  _M0L6offsetS175 = _M0L6_2atmpS1469;
  while (1) {
    if (_M0L3numS174 >= 10000u) {
      uint32_t _M0L1tS176 = _M0L3numS174 / 10000u;
      uint32_t _M0L6_2atmpS1446 = _M0L3numS174 % 10000u;
      int32_t _M0L1rS177 = *(int32_t*)&_M0L6_2atmpS1446;
      int32_t _M0L2d1S178 = _M0L1rS177 / 100;
      int32_t _M0L2d2S179 = _M0L1rS177 % 100;
      int32_t _M0L6_2atmpS1445 = _M0L2d1S178 / 10;
      int32_t _M0L6_2atmpS1444 = 48 + _M0L6_2atmpS1445;
      int32_t _M0L6d1__hiS180 = (uint16_t)_M0L6_2atmpS1444;
      int32_t _M0L6_2atmpS1443 = _M0L2d1S178 % 10;
      int32_t _M0L6_2atmpS1442 = 48 + _M0L6_2atmpS1443;
      int32_t _M0L6d1__loS181 = (uint16_t)_M0L6_2atmpS1442;
      int32_t _M0L6_2atmpS1441 = _M0L2d2S179 / 10;
      int32_t _M0L6_2atmpS1440 = 48 + _M0L6_2atmpS1441;
      int32_t _M0L6d2__hiS182 = (uint16_t)_M0L6_2atmpS1440;
      int32_t _M0L6_2atmpS1439 = _M0L2d2S179 % 10;
      int32_t _M0L6_2atmpS1438 = 48 + _M0L6_2atmpS1439;
      int32_t _M0L6d2__loS183 = (uint16_t)_M0L6_2atmpS1438;
      int32_t _M0L6_2atmpS1430 = _M0L12digit__startS185 + _M0L6offsetS175;
      int32_t _M0L6_2atmpS1429 = _M0L6_2atmpS1430 - 4;
      int32_t _M0L6_2atmpS1432;
      int32_t _M0L6_2atmpS1431;
      int32_t _M0L6_2atmpS1434;
      int32_t _M0L6_2atmpS1433;
      int32_t _M0L6_2atmpS1436;
      int32_t _M0L6_2atmpS1435;
      int32_t _M0L6_2atmpS1437;
      _M0L6bufferS184[_M0L6_2atmpS1429] = _M0L6d1__hiS180;
      _M0L6_2atmpS1432 = _M0L12digit__startS185 + _M0L6offsetS175;
      _M0L6_2atmpS1431 = _M0L6_2atmpS1432 - 3;
      _M0L6bufferS184[_M0L6_2atmpS1431] = _M0L6d1__loS181;
      _M0L6_2atmpS1434 = _M0L12digit__startS185 + _M0L6offsetS175;
      _M0L6_2atmpS1433 = _M0L6_2atmpS1434 - 2;
      _M0L6bufferS184[_M0L6_2atmpS1433] = _M0L6d2__hiS182;
      _M0L6_2atmpS1436 = _M0L12digit__startS185 + _M0L6offsetS175;
      _M0L6_2atmpS1435 = _M0L6_2atmpS1436 - 1;
      _M0L6bufferS184[_M0L6_2atmpS1435] = _M0L6d2__loS183;
      _M0L6_2atmpS1437 = _M0L6offsetS175 - 4;
      _M0L3numS174 = _M0L1tS176;
      _M0L6offsetS175 = _M0L6_2atmpS1437;
      continue;
    } else {
      int32_t _M0L6_2atmpS1468 = *(int32_t*)&_M0L3numS174;
      int32_t _M0L9remainingS187 = _M0L6_2atmpS1468;
      int32_t _M0L6offsetS188 = _M0L6offsetS175;
      while (1) {
        if (_M0L9remainingS187 >= 100) {
          int32_t _M0L1tS189 = _M0L9remainingS187 / 100;
          int32_t _M0L1dS190 = _M0L9remainingS187 % 100;
          int32_t _M0L6_2atmpS1455 = _M0L1dS190 / 10;
          int32_t _M0L6_2atmpS1454 = 48 + _M0L6_2atmpS1455;
          int32_t _M0L5d__hiS191 = (uint16_t)_M0L6_2atmpS1454;
          int32_t _M0L6_2atmpS1453 = _M0L1dS190 % 10;
          int32_t _M0L6_2atmpS1452 = 48 + _M0L6_2atmpS1453;
          int32_t _M0L5d__loS192 = (uint16_t)_M0L6_2atmpS1452;
          int32_t _M0L6_2atmpS1448 = _M0L12digit__startS185 + _M0L6offsetS188;
          int32_t _M0L6_2atmpS1447 = _M0L6_2atmpS1448 - 2;
          int32_t _M0L6_2atmpS1450;
          int32_t _M0L6_2atmpS1449;
          int32_t _M0L6_2atmpS1451;
          _M0L6bufferS184[_M0L6_2atmpS1447] = _M0L5d__hiS191;
          _M0L6_2atmpS1450 = _M0L12digit__startS185 + _M0L6offsetS188;
          _M0L6_2atmpS1449 = _M0L6_2atmpS1450 - 1;
          _M0L6bufferS184[_M0L6_2atmpS1449] = _M0L5d__loS192;
          _M0L6_2atmpS1451 = _M0L6offsetS188 - 2;
          _M0L9remainingS187 = _M0L1tS189;
          _M0L6offsetS188 = _M0L6_2atmpS1451;
          continue;
        } else if (_M0L9remainingS187 >= 10) {
          int32_t _M0L6_2atmpS1463 = _M0L9remainingS187 / 10;
          int32_t _M0L6_2atmpS1462 = 48 + _M0L6_2atmpS1463;
          int32_t _M0L5d__hiS194 = (uint16_t)_M0L6_2atmpS1462;
          int32_t _M0L6_2atmpS1461 = _M0L9remainingS187 % 10;
          int32_t _M0L6_2atmpS1460 = 48 + _M0L6_2atmpS1461;
          int32_t _M0L5d__loS195 = (uint16_t)_M0L6_2atmpS1460;
          int32_t _M0L6_2atmpS1457 = _M0L12digit__startS185 + _M0L6offsetS188;
          int32_t _M0L6_2atmpS1456 = _M0L6_2atmpS1457 - 2;
          int32_t _M0L6_2atmpS1459;
          int32_t _M0L6_2atmpS1458;
          _M0L6bufferS184[_M0L6_2atmpS1456] = _M0L5d__hiS194;
          _M0L6_2atmpS1459 = _M0L12digit__startS185 + _M0L6offsetS188;
          _M0L6_2atmpS1458 = _M0L6_2atmpS1459 - 1;
          _M0L6bufferS184[_M0L6_2atmpS1458] = _M0L5d__loS195;
        } else {
          int32_t _M0L6_2atmpS1467 = _M0L12digit__startS185 + _M0L6offsetS188;
          int32_t _M0L6_2atmpS1464 = _M0L6_2atmpS1467 - 1;
          int32_t _M0L6_2atmpS1466 = 48 + _M0L9remainingS187;
          int32_t _M0L6_2atmpS1465 = (uint16_t)_M0L6_2atmpS1466;
          _M0L6bufferS184[_M0L6_2atmpS1464] = _M0L6_2atmpS1465;
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
  int32_t _M0L6_2atmpS1414;
  int32_t _M0L6_2atmpS1413;
  #line 57 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L4baseS157 = *(uint32_t*)&_M0L5radixS158;
  _M0L6_2atmpS1414 = _M0L5radixS158 - 1;
  _M0L6_2atmpS1413 = _M0L5radixS158 & _M0L6_2atmpS1414;
  if (_M0L6_2atmpS1413 == 0) {
    int32_t _M0L5shiftS159;
    uint32_t _M0L4maskS160;
    int32_t _M0L6_2atmpS1421;
    int32_t _M0L6offsetS161;
    uint32_t _M0L1nS162;
    #line 68 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0L5shiftS159 = moonbit_ctz32(_M0L5radixS158);
    _M0L4maskS160 = _M0L4baseS157 - 1u;
    _M0L6_2atmpS1421 = _M0L10total__lenS167 - _M0L12digit__startS165;
    _M0L6offsetS161 = _M0L6_2atmpS1421;
    _M0L1nS162 = _M0L3numS168;
    while (1) {
      if (_M0L1nS162 > 0u) {
        uint32_t _M0L6_2atmpS1420 = _M0L1nS162 & _M0L4maskS160;
        int32_t _M0L5digitS163 = *(int32_t*)&_M0L6_2atmpS1420;
        int32_t _M0L6_2atmpS1417 = _M0L12digit__startS165 + _M0L6offsetS161;
        int32_t _M0L6_2atmpS1415 = _M0L6_2atmpS1417 - 1;
        int32_t _M0L6_2atmpS1416 =
          ((moonbit_string_t)moonbit_string_literal_13.data)[_M0L5digitS163];
        int32_t _M0L6_2atmpS1418;
        uint32_t _M0L6_2atmpS1419;
        _M0L6bufferS164[_M0L6_2atmpS1415] = _M0L6_2atmpS1416;
        _M0L6_2atmpS1418 = _M0L6offsetS161 - 1;
        _M0L6_2atmpS1419 = _M0L1nS162 >> (_M0L5shiftS159 & 31);
        _M0L6offsetS161 = _M0L6_2atmpS1418;
        _M0L1nS162 = _M0L6_2atmpS1419;
        continue;
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1428 = _M0L10total__lenS167 - _M0L12digit__startS165;
    int32_t _M0L6offsetS169 = _M0L6_2atmpS1428;
    uint32_t _M0L1nS170 = _M0L3numS168;
    while (1) {
      if (_M0L1nS170 > 0u) {
        uint32_t _M0L1qS171 = _M0L1nS170 / _M0L4baseS157;
        uint32_t _M0L6_2atmpS1427 = _M0L1qS171 * _M0L4baseS157;
        uint32_t _M0L6_2atmpS1426 = _M0L1nS170 - _M0L6_2atmpS1427;
        int32_t _M0L5digitS172 = *(int32_t*)&_M0L6_2atmpS1426;
        int32_t _M0L6_2atmpS1424 = _M0L12digit__startS165 + _M0L6offsetS169;
        int32_t _M0L6_2atmpS1422 = _M0L6_2atmpS1424 - 1;
        int32_t _M0L6_2atmpS1423 =
          ((moonbit_string_t)moonbit_string_literal_13.data)[_M0L5digitS172];
        int32_t _M0L6_2atmpS1425;
        _M0L6bufferS164[_M0L6_2atmpS1422] = _M0L6_2atmpS1423;
        _M0L6_2atmpS1425 = _M0L6offsetS169 - 1;
        _M0L6offsetS169 = _M0L6_2atmpS1425;
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
  int32_t _M0L6_2atmpS1412;
  int32_t _M0L6offsetS146;
  uint32_t _M0L1nS147;
  #line 29 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L6_2atmpS1412 = _M0L10total__lenS155 - _M0L12digit__startS152;
  _M0L6offsetS146 = _M0L6_2atmpS1412;
  _M0L1nS147 = _M0L3numS156;
  while (1) {
    if (_M0L6offsetS146 >= 2) {
      uint32_t _M0L6_2atmpS1409 = _M0L1nS147 & 255u;
      int32_t _M0L9byte__valS148 = *(int32_t*)&_M0L6_2atmpS1409;
      int32_t _M0L2hiS149 = _M0L9byte__valS148 / 16;
      int32_t _M0L2loS150 = _M0L9byte__valS148 % 16;
      int32_t _M0L6_2atmpS1403 = _M0L12digit__startS152 + _M0L6offsetS146;
      int32_t _M0L6_2atmpS1401 = _M0L6_2atmpS1403 - 2;
      int32_t _M0L6_2atmpS1402 =
        ((moonbit_string_t)moonbit_string_literal_13.data)[_M0L2hiS149];
      int32_t _M0L6_2atmpS1406;
      int32_t _M0L6_2atmpS1404;
      int32_t _M0L6_2atmpS1405;
      int32_t _M0L6_2atmpS1407;
      uint32_t _M0L6_2atmpS1408;
      _M0L6bufferS151[_M0L6_2atmpS1401] = _M0L6_2atmpS1402;
      _M0L6_2atmpS1406 = _M0L12digit__startS152 + _M0L6offsetS146;
      _M0L6_2atmpS1404 = _M0L6_2atmpS1406 - 1;
      _M0L6_2atmpS1405
      = ((moonbit_string_t)moonbit_string_literal_13.data)[
        _M0L2loS150
      ];
      _M0L6bufferS151[_M0L6_2atmpS1404] = _M0L6_2atmpS1405;
      _M0L6_2atmpS1407 = _M0L6offsetS146 - 2;
      _M0L6_2atmpS1408 = _M0L1nS147 >> 8;
      _M0L6offsetS146 = _M0L6_2atmpS1407;
      _M0L1nS147 = _M0L6_2atmpS1408;
      continue;
    } else if (_M0L6offsetS146 == 1) {
      uint32_t _M0L6_2atmpS1411 = _M0L1nS147 & 15u;
      int32_t _M0L6nibbleS154 = *(int32_t*)&_M0L6_2atmpS1411;
      int32_t _M0L6_2atmpS1410 =
        ((moonbit_string_t)moonbit_string_literal_13.data)[_M0L6nibbleS154];
      _M0L6bufferS151[_M0L12digit__startS152] = _M0L6_2atmpS1410;
    }
    break;
  }
  return 0;
}

int32_t _M0IP016_24default__implPB4Show6outputGiE(
  int32_t _M0L4selfS143,
  struct _M0TPB6Logger _M0L6loggerS142
) {
  moonbit_string_t _M0L6_2atmpS1399;
  #line 159 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  #line 160 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS1399 = _M0IPC13int3IntPB4Show10to__string(_M0L4selfS143);
  #line 160 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6loggerS142.$0->$method_0(_M0L6loggerS142.$1, _M0L6_2atmpS1399);
  moonbit_decref(_M0L6_2atmpS1399);
  return 0;
}

int32_t _M0IP016_24default__implPB4Show6outputGmE(
  uint64_t _M0L4selfS145,
  struct _M0TPB6Logger _M0L6loggerS144
) {
  moonbit_string_t _M0L6_2atmpS1400;
  #line 159 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  #line 160 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS1400 = _M0IPC16uint646UInt64PB4Show10to__string(_M0L4selfS145);
  #line 160 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6loggerS144.$0->$method_0(_M0L6loggerS144.$1, _M0L6_2atmpS1400);
  moonbit_decref(_M0L6_2atmpS1400);
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
  moonbit_string_t _M0L8_2afieldS3035;
  #line 92 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
  _M0L8_2afieldS3035 = _M0L4selfS140.$0;
  moonbit_incref(_M0L8_2afieldS3035);
  return _M0L8_2afieldS3035;
}

int32_t _M0IP016_24default__implPB6Logger16write__substringGRPB13StringBuilderE(
  struct _M0TPB13StringBuilder* _M0L4selfS136,
  moonbit_string_t _M0L5valueS137,
  int32_t _M0L5startS138,
  int32_t _M0L3lenS139
) {
  int32_t _M0L6_2atmpS1398;
  int64_t _M0L6_2atmpS1397;
  struct _M0TPC16string10StringView _M0L6_2atmpS1396;
  #line 122 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS1398 = _M0L5startS138 + _M0L3lenS139;
  _M0L6_2atmpS1397 = (int64_t)_M0L6_2atmpS1398;
  #line 123 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS1396
  = _M0MPC16string6String11sub_2einner(_M0L5valueS137, _M0L5startS138, _M0L6_2atmpS1397);
  #line 123 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0IPB13StringBuilderPB6Logger11write__view(_M0L4selfS136, _M0L6_2atmpS1396);
  moonbit_decref(_M0L6_2atmpS1396.$0);
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
  goto joinlet_3163;
  join_130:;
  _M0L3endS129 = _M0L3endS131;
  joinlet_3163:;
  if (
    _M0L5startS135 >= 0
    && _M0L5startS135 <= _M0L3endS129
    && _M0L3endS129 <= _M0L3lenS127
  ) {
    if (_M0L5startS135 < _M0L3lenS127) {
      int32_t _M0L6_2atmpS1393 = _M0L4selfS128[_M0L5startS135];
      int32_t _M0L6_2atmpS1392;
      #line 931 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
      _M0L6_2atmpS1392
      = _M0MPC16uint166UInt1623is__trailing__surrogate(_M0L6_2atmpS1393);
      if (!_M0L6_2atmpS1392) {
        
      } else {
        #line 931 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
        moonbit_panic();
      }
    }
    if (_M0L3endS129 < _M0L3lenS127) {
      int32_t _M0L6_2atmpS1395 = _M0L4selfS128[_M0L3endS129];
      int32_t _M0L6_2atmpS1394;
      #line 934 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
      _M0L6_2atmpS1394
      = _M0MPC16uint166UInt1623is__trailing__surrogate(_M0L6_2atmpS1395);
      if (!_M0L6_2atmpS1394) {
        
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
  struct _M0TPB6Logger _M0L6_2atmpS1391;
  #line 116 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  moonbit_incref(_M0L4selfS126);
  _M0L6_2atmpS1391
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS126
  };
  #line 117 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L4showS125.$0->$method_0(_M0L4showS125.$1, _M0L6_2atmpS1391);
  if (_M0L6_2atmpS1391.$1) {
    moonbit_decref(_M0L6_2atmpS1391.$1);
  }
  return 0;
}

int32_t _M0IP016_24default__implPB6Logger28write__string__interpolationGRPB13StringBuilderE(
  struct _M0TPB13StringBuilder* _M0L4selfS124,
  struct _M0TPB4Show _M0L4showS123
) {
  struct _M0TPB6Logger _M0L6_2atmpS1390;
  #line 111 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  moonbit_incref(_M0L4selfS124);
  _M0L6_2atmpS1390
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS124
  };
  #line 112 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L4showS123.$0->$method_0(_M0L4showS123.$1, _M0L6_2atmpS1390);
  if (_M0L6_2atmpS1390.$1) {
    moonbit_decref(_M0L6_2atmpS1390.$1);
  }
  return 0;
}

uint64_t _M0MPC13int3Int10to__uint64(int32_t _M0L4selfS122) {
  int64_t _M0L6_2atmpS1389;
  #line 989 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS1389 = (int64_t)_M0L4selfS122;
  return *(uint64_t*)&_M0L6_2atmpS1389;
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
  int32_t _M0L3lenS1388;
  int32_t _M0L8requiredS119;
  uint16_t* _M0L4dataS1383;
  int32_t _M0L6_2atmpS1382;
  int32_t _if__result_3164;
  uint16_t* _M0L4dataS1384;
  int32_t _M0L3lenS1385;
  int32_t _M0L3lenS1387;
  int32_t _M0L6_2atmpS1386;
  #line 105 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L8str__lenS117 = Moonbit_array_length(_M0L3strS118);
  if (_M0L8str__lenS117 == 0) {
    return 0;
  }
  _M0L3lenS1388 = _M0L4selfS120->$1;
  _M0L8requiredS119 = _M0L3lenS1388 + _M0L8str__lenS117;
  _M0L4dataS1383 = _M0L4selfS120->$0;
  _M0L6_2atmpS1382 = Moonbit_array_length(_M0L4dataS1383);
  if (_M0L8requiredS119 > _M0L6_2atmpS1382) {
    _if__result_3164 = 1;
  } else {
    int32_t _M0L3lenS1381 = _M0L4selfS120->$1;
    _if__result_3164 = _M0L8requiredS119 < _M0L3lenS1381;
  }
  if (_if__result_3164) {
    #line 112 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0MPB13StringBuilder4grow(_M0L4selfS120, _M0L8requiredS119);
  }
  _M0L4dataS1384 = _M0L4selfS120->$0;
  _M0L3lenS1385 = _M0L4selfS120->$1;
  moonbit_incref(_M0L4dataS1384);
  #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0MPC15array10FixedArray26unsafe__blit__from__string(_M0L4dataS1384, _M0L3lenS1385, _M0L3strS118, 0, _M0L8str__lenS117);
  moonbit_decref(_M0L4dataS1384);
  _M0L3lenS1387 = _M0L4selfS120->$1;
  _M0L6_2atmpS1386 = _M0L3lenS1387 + _M0L8str__lenS117;
  _M0L4selfS120->$1 = _M0L6_2atmpS1386;
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
      int32_t _M0L6_2atmpS1378 = _M0L3strS114[_M0L1iS111];
      int32_t _M0L6_2atmpS1379;
      int32_t _M0L6_2atmpS1380;
      _M0L4selfS113[_M0L1jS112] = _M0L6_2atmpS1378;
      _M0L6_2atmpS1379 = _M0L1iS111 + 1;
      _M0L6_2atmpS1380 = _M0L1jS112 + 1;
      _M0L1iS111 = _M0L6_2atmpS1379;
      _M0L1jS112 = _M0L6_2atmpS1380;
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
    int32_t _M0L3lenS1349 = _M0L4selfS106->$1;
    uint16_t* _M0L4dataS1351 = _M0L4selfS106->$0;
    int32_t _M0L6_2atmpS1350 = Moonbit_array_length(_M0L4dataS1351);
    uint16_t* _M0L4dataS1354;
    int32_t _M0L3lenS1355;
    int32_t _M0L6_2atmpS1356;
    int32_t _M0L3lenS1358;
    int32_t _M0L6_2atmpS1357;
    if (_M0L3lenS1349 >= _M0L6_2atmpS1350) {
      int32_t _M0L3lenS1353 = _M0L4selfS106->$1;
      int32_t _M0L6_2atmpS1352 = _M0L3lenS1353 + 1;
      #line 124 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0MPB13StringBuilder4grow(_M0L4selfS106, _M0L6_2atmpS1352);
    }
    _M0L4dataS1354 = _M0L4selfS106->$0;
    _M0L3lenS1355 = _M0L4selfS106->$1;
    moonbit_incref(_M0L4dataS1354);
    #line 126 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0L6_2atmpS1356 = _M0MPC14uint4UInt10to__uint16(_M0L4codeS104);
    if (
      _M0L3lenS1355 < 0
      || _M0L3lenS1355 >= Moonbit_array_length(_M0L4dataS1354)
    ) {
      #line 126 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      moonbit_panic();
    }
    _M0L4dataS1354[_M0L3lenS1355] = _M0L6_2atmpS1356;
    moonbit_decref(_M0L4dataS1354);
    _M0L3lenS1358 = _M0L4selfS106->$1;
    _M0L6_2atmpS1357 = _M0L3lenS1358 + 1;
    _M0L4selfS106->$1 = _M0L6_2atmpS1357;
  } else if (_M0L4codeS104 <= 1114111u) {
    uint16_t* _M0L4dataS1362 = _M0L4selfS106->$0;
    int32_t _M0L6_2atmpS1360 = Moonbit_array_length(_M0L4dataS1362);
    int32_t _M0L3lenS1361 = _M0L4selfS106->$1;
    int32_t _M0L6_2atmpS1359 = _M0L6_2atmpS1360 - _M0L3lenS1361;
    uint32_t _M0L4codeS107;
    uint16_t* _M0L4dataS1365;
    int32_t _M0L3lenS1366;
    uint32_t _M0L6_2atmpS1369;
    uint32_t _M0L6_2atmpS1368;
    int32_t _M0L6_2atmpS1367;
    uint16_t* _M0L4dataS1370;
    int32_t _M0L3lenS1375;
    int32_t _M0L6_2atmpS1371;
    uint32_t _M0L6_2atmpS1374;
    uint32_t _M0L6_2atmpS1373;
    int32_t _M0L6_2atmpS1372;
    int32_t _M0L3lenS1377;
    int32_t _M0L6_2atmpS1376;
    if (_M0L6_2atmpS1359 < 2) {
      int32_t _M0L3lenS1364 = _M0L4selfS106->$1;
      int32_t _M0L6_2atmpS1363 = _M0L3lenS1364 + 2;
      #line 130 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0MPB13StringBuilder4grow(_M0L4selfS106, _M0L6_2atmpS1363);
    }
    _M0L4codeS107 = _M0L4codeS104 - 65536u;
    _M0L4dataS1365 = _M0L4selfS106->$0;
    _M0L3lenS1366 = _M0L4selfS106->$1;
    _M0L6_2atmpS1369 = _M0L4codeS107 >> 10;
    _M0L6_2atmpS1368 = 55296u + _M0L6_2atmpS1369;
    moonbit_incref(_M0L4dataS1365);
    #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0L6_2atmpS1367 = _M0MPC14uint4UInt10to__uint16(_M0L6_2atmpS1368);
    if (
      _M0L3lenS1366 < 0
      || _M0L3lenS1366 >= Moonbit_array_length(_M0L4dataS1365)
    ) {
      #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      moonbit_panic();
    }
    _M0L4dataS1365[_M0L3lenS1366] = _M0L6_2atmpS1367;
    moonbit_decref(_M0L4dataS1365);
    _M0L4dataS1370 = _M0L4selfS106->$0;
    _M0L3lenS1375 = _M0L4selfS106->$1;
    _M0L6_2atmpS1371 = _M0L3lenS1375 + 1;
    _M0L6_2atmpS1374 = _M0L4codeS107 & 1023u;
    _M0L6_2atmpS1373 = 56320u + _M0L6_2atmpS1374;
    moonbit_incref(_M0L4dataS1370);
    #line 134 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0L6_2atmpS1372 = _M0MPC14uint4UInt10to__uint16(_M0L6_2atmpS1373);
    if (
      _M0L6_2atmpS1371 < 0
      || _M0L6_2atmpS1371 >= Moonbit_array_length(_M0L4dataS1370)
    ) {
      #line 134 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      moonbit_panic();
    }
    _M0L4dataS1370[_M0L6_2atmpS1371] = _M0L6_2atmpS1372;
    moonbit_decref(_M0L4dataS1370);
    _M0L3lenS1377 = _M0L4selfS106->$1;
    _M0L6_2atmpS1376 = _M0L3lenS1377 + 2;
    _M0L4selfS106->$1 = _M0L6_2atmpS1376;
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
  uint16_t* _M0L4dataS1348;
  int32_t _M0L6_2atmpS1346;
  int32_t _M0L3lenS1347;
  int32_t _M0L13new__capacityS100;
  uint16_t* _M0L4dataS1343;
  int32_t _M0L6_2atmpS1344;
  int32_t _M0L3lenS1345;
  uint16_t* _M0L9new__dataS103;
  uint16_t* _M0L6_2aoldS3036;
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L4dataS1348 = _M0L4selfS101->$0;
  _M0L6_2atmpS1346 = Moonbit_array_length(_M0L4dataS1348);
  _M0L3lenS1347 = _M0L4selfS101->$1;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L13new__capacityS100
  = _M0FPB31stringbuilder__growth__capacity(_M0L6_2atmpS1346, _M0L3lenS1347, _M0L8requiredS102);
  _M0L4dataS1343 = _M0L4selfS101->$0;
  moonbit_incref(_M0L4dataS1343);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L6_2atmpS1344 = _M0IPC16uint166UInt16PB7Default7default();
  _M0L3lenS1345 = _M0L4selfS101->$1;
  #line 80 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L9new__dataS103
  = _M0MPC15array10FixedArray23make__and__blit_2einnerGkE(_M0L4dataS1343, _M0L13new__capacityS100, _M0L6_2atmpS1344, _M0L3lenS1345, 0, 0);
  _M0L6_2aoldS3036 = _M0L4selfS101->$0;
  moonbit_decref(_M0L6_2aoldS3036);
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
  int32_t _M0L6_2atmpS1342;
  #line 2785 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS1342 = *(int32_t*)&_M0L4selfS93;
  return (uint16_t)_M0L6_2atmpS1342;
}

uint32_t _M0MPC14char4Char8to__uint(int32_t _M0L4selfS92) {
  int32_t _M0L6_2atmpS1341;
  #line 1335 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS1341 = _M0L4selfS92;
  return *(uint32_t*)&_M0L6_2atmpS1341;
}

moonbit_string_t _M0MPB13StringBuilder10to__string(
  struct _M0TPB13StringBuilder* _M0L4selfS90
) {
  int32_t _M0L3lenS1332;
  #line 181 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L3lenS1332 = _M0L4selfS90->$1;
  if (_M0L3lenS1332 == 0) {
    return (moonbit_string_t)moonbit_string_literal_8.data;
  } else {
    int32_t _M0L3lenS1333 = _M0L4selfS90->$1;
    uint16_t* _M0L4dataS1335 = _M0L4selfS90->$0;
    int32_t _M0L6_2atmpS1334 = Moonbit_array_length(_M0L4dataS1335);
    if (_M0L3lenS1333 == _M0L6_2atmpS1334) {
      uint16_t* _M0L4dataS1336 = _M0L4selfS90->$0;
      moonbit_incref(_M0L4dataS1336);
      return _M0L4dataS1336;
    } else {
      uint16_t* _M0L4dataS1337 = _M0L4selfS90->$0;
      int32_t _M0L3lenS1338 = _M0L4selfS90->$1;
      int32_t _M0L6_2atmpS1339;
      int32_t _M0L3lenS1340;
      uint16_t* _M0L4dataS91;
      moonbit_incref(_M0L4dataS1337);
      #line 190 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0L6_2atmpS1339 = _M0IPC16uint166UInt16PB7Default7default();
      _M0L3lenS1340 = _M0L4selfS90->$1;
      #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0L4dataS91
      = _M0MPC15array10FixedArray23make__and__blit_2einnerGkE(_M0L4dataS1337, _M0L3lenS1338, _M0L6_2atmpS1339, _M0L3lenS1340, 0, 0);
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
  int32_t _if__result_3167;
  #line 97 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
  if (_M0L13allocate__lenS83 >= 0) {
    if (_M0L3lenS84 >= 0) {
      if (_M0L11src__offsetS85 >= 0) {
        if (_M0L11dst__offsetS86 >= 0) {
          int32_t _M0L6_2atmpS1328 = _M0L11src__offsetS85 + _M0L3lenS84;
          int32_t _M0L6_2atmpS1329 = Moonbit_array_length(_M0L3srcS87);
          if (_M0L6_2atmpS1328 <= _M0L6_2atmpS1329) {
            int32_t _M0L6_2atmpS1327 = _M0L11dst__offsetS86 + _M0L3lenS84;
            _if__result_3167 = _M0L6_2atmpS1327 <= _M0L13allocate__lenS83;
          } else {
            _if__result_3167 = 0;
          }
        } else {
          _if__result_3167 = 0;
        }
      } else {
        _if__result_3167 = 0;
      }
    } else {
      _if__result_3167 = 0;
    }
  } else {
    _if__result_3167 = 0;
  }
  if (_if__result_3167) {
    #line 116 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    return _M0MPC15array10FixedArray23unsafe__make__and__blitGkE(_M0L3srcS87, _M0L13allocate__lenS83, _M0L4initS88, _M0L11src__offsetS85, _M0L11dst__offsetS86, _M0L3lenS84);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS89;
    int32_t _M0L6_2atmpS1331;
    moonbit_string_t _M0L6_2atmpS1330;
    uint16_t* _result_3168;
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
    _M0L6_2atmpS1331 = Moonbit_array_length(_M0L3srcS87);
    moonbit_decref(_M0L3srcS87);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS89, _M0L6_2atmpS1331);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0L6_2atmpS1330
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS89);
    moonbit_decref(_M0L18_2astring__builderS89);
    #line 112 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _result_3168 = _M0FPC15abort5abortGAkE(_M0L6_2atmpS1330);
    moonbit_decref(_M0L6_2atmpS1330);
    return _result_3168;
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
  struct _M0TPB13StringBuilder* _block_3169;
  #line 32 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  if (_M0L10size__hintS74 < 1) {
    _M0L7initialS73 = 1;
  } else {
    int32_t _M0L6_2atmpS1326 = _M0L10size__hintS74 + 1;
    _M0L7initialS73 = _M0L6_2atmpS1326 / 2;
  }
  _M0L4dataS75 = (uint16_t*)moonbit_make_string(_M0L7initialS73, 0);
  _block_3169
  = (struct _M0TPB13StringBuilder*)moonbit_malloc(sizeof(struct _M0TPB13StringBuilder));
  Moonbit_object_header(_block_3169)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 107, 0);
  _block_3169->$0 = _M0L4dataS75;
  _block_3169->$1 = 0;
  return _block_3169;
}

float* _M0MPB18UninitializedArray23make__and__blit_2einnerGfE(
  float* _M0L3srcS65,
  int32_t _M0L13allocate__lenS61,
  int32_t _M0L3lenS62,
  int32_t _M0L11src__offsetS63,
  int32_t _M0L11dst__offsetS64
) {
  int32_t _if__result_3170;
  #line 167 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  if (_M0L13allocate__lenS61 >= 0) {
    if (_M0L3lenS62 >= 0) {
      if (_M0L11src__offsetS63 >= 0) {
        if (_M0L11dst__offsetS64 >= 0) {
          int32_t _M0L6_2atmpS1317 = _M0L11src__offsetS63 + _M0L3lenS62;
          int32_t _M0L6_2atmpS1318;
          #line 179 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
          _M0L6_2atmpS1318
          = _M0MPB18UninitializedArray6lengthGfE(_M0L3srcS65);
          if (_M0L6_2atmpS1317 <= _M0L6_2atmpS1318) {
            int32_t _M0L6_2atmpS1316 = _M0L11dst__offsetS64 + _M0L3lenS62;
            _if__result_3170 = _M0L6_2atmpS1316 <= _M0L13allocate__lenS61;
          } else {
            _if__result_3170 = 0;
          }
        } else {
          _if__result_3170 = 0;
        }
      } else {
        _if__result_3170 = 0;
      }
    } else {
      _if__result_3170 = 0;
    }
  } else {
    _if__result_3170 = 0;
  }
  if (_if__result_3170) {
    #line 185 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    return _M0MPB18UninitializedArray23unsafe__make__and__blitGfE(_M0L3srcS65, _M0L13allocate__lenS61, _M0L11src__offsetS63, _M0L11dst__offsetS64, _M0L3lenS62);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS66;
    int32_t _M0L6_2atmpS1320;
    moonbit_string_t _M0L6_2atmpS1319;
    float* _result_3171;
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
    _M0L6_2atmpS1320 = _M0MPB18UninitializedArray6lengthGfE(_M0L3srcS65);
    moonbit_decref(_M0L3srcS65);
    #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS66, _M0L6_2atmpS1320);
    #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS1319
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS66);
    moonbit_decref(_M0L18_2astring__builderS66);
    #line 181 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _result_3171
    = _M0FPC15abort5abortGRPB18UninitializedArrayGfEE(_M0L6_2atmpS1319);
    moonbit_decref(_M0L6_2atmpS1319);
    return _result_3171;
  }
}

int32_t* _M0MPB18UninitializedArray23make__and__blit_2einnerGiE(
  int32_t* _M0L3srcS71,
  int32_t _M0L13allocate__lenS67,
  int32_t _M0L3lenS68,
  int32_t _M0L11src__offsetS69,
  int32_t _M0L11dst__offsetS70
) {
  int32_t _if__result_3172;
  #line 167 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  if (_M0L13allocate__lenS67 >= 0) {
    if (_M0L3lenS68 >= 0) {
      if (_M0L11src__offsetS69 >= 0) {
        if (_M0L11dst__offsetS70 >= 0) {
          int32_t _M0L6_2atmpS1322 = _M0L11src__offsetS69 + _M0L3lenS68;
          int32_t _M0L6_2atmpS1323;
          #line 179 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
          _M0L6_2atmpS1323
          = _M0MPB18UninitializedArray6lengthGiE(_M0L3srcS71);
          if (_M0L6_2atmpS1322 <= _M0L6_2atmpS1323) {
            int32_t _M0L6_2atmpS1321 = _M0L11dst__offsetS70 + _M0L3lenS68;
            _if__result_3172 = _M0L6_2atmpS1321 <= _M0L13allocate__lenS67;
          } else {
            _if__result_3172 = 0;
          }
        } else {
          _if__result_3172 = 0;
        }
      } else {
        _if__result_3172 = 0;
      }
    } else {
      _if__result_3172 = 0;
    }
  } else {
    _if__result_3172 = 0;
  }
  if (_if__result_3172) {
    #line 185 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    return _M0MPB18UninitializedArray23unsafe__make__and__blitGiE(_M0L3srcS71, _M0L13allocate__lenS67, _M0L11src__offsetS69, _M0L11dst__offsetS70, _M0L3lenS68);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS72;
    int32_t _M0L6_2atmpS1325;
    moonbit_string_t _M0L6_2atmpS1324;
    int32_t* _result_3173;
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
    _M0L6_2atmpS1325 = _M0MPB18UninitializedArray6lengthGiE(_M0L3srcS71);
    moonbit_decref(_M0L3srcS71);
    #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS72, _M0L6_2atmpS1325);
    #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS1324
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS72);
    moonbit_decref(_M0L18_2astring__builderS72);
    #line 181 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _result_3173
    = _M0FPC15abort5abortGRPB18UninitializedArrayGiEE(_M0L6_2atmpS1324);
    moonbit_decref(_M0L6_2atmpS1324);
    return _result_3173;
  }
}

int32_t _M0MPB13StringBuilder13write__objectGiE(
  struct _M0TPB13StringBuilder* _M0L4selfS58,
  int32_t _M0L3objS57
) {
  struct _M0TPB6Logger _M0L6_2atmpS1314;
  #line 17 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  moonbit_incref(_M0L4selfS58);
  _M0L6_2atmpS1314
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS58
  };
  #line 23 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  _M0IP016_24default__implPB4Show6outputGiE(_M0L3objS57, _M0L6_2atmpS1314);
  if (_M0L6_2atmpS1314.$1) {
    moonbit_decref(_M0L6_2atmpS1314.$1);
  }
  return 0;
}

int32_t _M0MPB13StringBuilder13write__objectGmE(
  struct _M0TPB13StringBuilder* _M0L4selfS60,
  uint64_t _M0L3objS59
) {
  struct _M0TPB6Logger _M0L6_2atmpS1315;
  #line 17 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  moonbit_incref(_M0L4selfS60);
  _M0L6_2atmpS1315
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS60
  };
  #line 23 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  _M0IP016_24default__implPB4Show6outputGmE(_M0L3objS59, _M0L6_2atmpS1315);
  if (_M0L6_2atmpS1315.$1) {
    moonbit_decref(_M0L6_2atmpS1315.$1);
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
        int32_t _M0L6_2atmpS1287 = _M0L11dst__offsetS10 + _M0L1iS12;
        int32_t _M0L6_2atmpS1289 = _M0L11src__offsetS11 + _M0L1iS12;
        float _M0L6_2atmpS1288;
        int32_t _M0L6_2atmpS1290;
        if (
          _M0L6_2atmpS1289 < 0
          || _M0L6_2atmpS1289 >= Moonbit_array_length(_M0L3srcS9)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1288 = (float)_M0L3srcS9[_M0L6_2atmpS1289];
        if (
          _M0L6_2atmpS1287 < 0
          || _M0L6_2atmpS1287 >= Moonbit_array_length(_M0L3dstS8)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS8[_M0L6_2atmpS1287] = _M0L6_2atmpS1288;
        _M0L6_2atmpS1290 = _M0L1iS12 + 1;
        _M0L1iS12 = _M0L6_2atmpS1290;
        continue;
      } else {
        moonbit_decref(_M0L3srcS9);
        moonbit_decref(_M0L3dstS8);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1295 = _M0L3lenS13 - 1;
    int32_t _M0L1iS15 = _M0L6_2atmpS1295;
    while (1) {
      if (_M0L1iS15 >= 0) {
        int32_t _M0L6_2atmpS1291 = _M0L11dst__offsetS10 + _M0L1iS15;
        int32_t _M0L6_2atmpS1293 = _M0L11src__offsetS11 + _M0L1iS15;
        float _M0L6_2atmpS1292;
        int32_t _M0L6_2atmpS1294;
        if (
          _M0L6_2atmpS1293 < 0
          || _M0L6_2atmpS1293 >= Moonbit_array_length(_M0L3srcS9)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1292 = (float)_M0L3srcS9[_M0L6_2atmpS1293];
        if (
          _M0L6_2atmpS1291 < 0
          || _M0L6_2atmpS1291 >= Moonbit_array_length(_M0L3dstS8)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS8[_M0L6_2atmpS1291] = _M0L6_2atmpS1292;
        _M0L6_2atmpS1294 = _M0L1iS15 - 1;
        _M0L1iS15 = _M0L6_2atmpS1294;
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
        int32_t _M0L6_2atmpS1296 = _M0L11dst__offsetS19 + _M0L1iS21;
        int32_t _M0L6_2atmpS1298 = _M0L11src__offsetS20 + _M0L1iS21;
        int32_t _M0L6_2atmpS1297;
        int32_t _M0L6_2atmpS1299;
        if (
          _M0L6_2atmpS1298 < 0
          || _M0L6_2atmpS1298 >= Moonbit_array_length(_M0L3srcS18)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1297 = (int32_t)_M0L3srcS18[_M0L6_2atmpS1298];
        if (
          _M0L6_2atmpS1296 < 0
          || _M0L6_2atmpS1296 >= Moonbit_array_length(_M0L3dstS17)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS17[_M0L6_2atmpS1296] = _M0L6_2atmpS1297;
        _M0L6_2atmpS1299 = _M0L1iS21 + 1;
        _M0L1iS21 = _M0L6_2atmpS1299;
        continue;
      } else {
        moonbit_decref(_M0L3srcS18);
        moonbit_decref(_M0L3dstS17);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1304 = _M0L3lenS22 - 1;
    int32_t _M0L1iS24 = _M0L6_2atmpS1304;
    while (1) {
      if (_M0L1iS24 >= 0) {
        int32_t _M0L6_2atmpS1300 = _M0L11dst__offsetS19 + _M0L1iS24;
        int32_t _M0L6_2atmpS1302 = _M0L11src__offsetS20 + _M0L1iS24;
        int32_t _M0L6_2atmpS1301;
        int32_t _M0L6_2atmpS1303;
        if (
          _M0L6_2atmpS1302 < 0
          || _M0L6_2atmpS1302 >= Moonbit_array_length(_M0L3srcS18)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1301 = (int32_t)_M0L3srcS18[_M0L6_2atmpS1302];
        if (
          _M0L6_2atmpS1300 < 0
          || _M0L6_2atmpS1300 >= Moonbit_array_length(_M0L3dstS17)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS17[_M0L6_2atmpS1300] = _M0L6_2atmpS1301;
        _M0L6_2atmpS1303 = _M0L1iS24 - 1;
        _M0L1iS24 = _M0L6_2atmpS1303;
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
        int32_t _M0L6_2atmpS1305 = _M0L11dst__offsetS28 + _M0L1iS30;
        int32_t _M0L6_2atmpS1307 = _M0L11src__offsetS29 + _M0L1iS30;
        int32_t _M0L6_2atmpS1306;
        int32_t _M0L6_2atmpS1308;
        if (
          _M0L6_2atmpS1307 < 0
          || _M0L6_2atmpS1307 >= Moonbit_array_length(_M0L3srcS27)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1306 = (int32_t)_M0L3srcS27[_M0L6_2atmpS1307];
        if (
          _M0L6_2atmpS1305 < 0
          || _M0L6_2atmpS1305 >= Moonbit_array_length(_M0L3dstS26)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS26[_M0L6_2atmpS1305] = _M0L6_2atmpS1306;
        _M0L6_2atmpS1308 = _M0L1iS30 + 1;
        _M0L1iS30 = _M0L6_2atmpS1308;
        continue;
      } else {
        moonbit_decref(_M0L3srcS27);
        moonbit_decref(_M0L3dstS26);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1313 = _M0L3lenS31 - 1;
    int32_t _M0L1iS33 = _M0L6_2atmpS1313;
    while (1) {
      if (_M0L1iS33 >= 0) {
        int32_t _M0L6_2atmpS1309 = _M0L11dst__offsetS28 + _M0L1iS33;
        int32_t _M0L6_2atmpS1311 = _M0L11src__offsetS29 + _M0L1iS33;
        int32_t _M0L6_2atmpS1310;
        int32_t _M0L6_2atmpS1312;
        if (
          _M0L6_2atmpS1311 < 0
          || _M0L6_2atmpS1311 >= Moonbit_array_length(_M0L3srcS27)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1310 = (int32_t)_M0L3srcS27[_M0L6_2atmpS1311];
        if (
          _M0L6_2atmpS1309 < 0
          || _M0L6_2atmpS1309 >= Moonbit_array_length(_M0L3dstS26)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS26[_M0L6_2atmpS1309] = _M0L6_2atmpS1310;
        _M0L6_2atmpS1312 = _M0L1iS33 - 1;
        _M0L1iS33 = _M0L6_2atmpS1312;
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
  void* _M0L11_2aobj__ptrS1192,
  struct _M0TPB4Show _M0L8_2aparamS1191
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS1190 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS1192;
  _M0IP016_24default__implPB6Logger5writeGRPB13StringBuilderE(_M0L7_2aselfS1190, _M0L8_2aparamS1191);
  return 0;
}

int32_t _M0IP016_24default__implPB6Logger84write__string__interpolation_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLoggerGRPB13StringBuilderE(
  void* _M0L11_2aobj__ptrS1189,
  struct _M0TPB4Show _M0L8_2aparamS1188
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS1187 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS1189;
  _M0IP016_24default__implPB6Logger28write__string__interpolationGRPB13StringBuilderE(_M0L7_2aselfS1187, _M0L8_2aparamS1188);
  return 0;
}

int32_t _M0IPB13StringBuilderPB6Logger67write__char_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLogger(
  void* _M0L11_2aobj__ptrS1186,
  int32_t _M0L8_2aparamS1185
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS1184 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS1186;
  _M0IPB13StringBuilderPB6Logger11write__char(_M0L7_2aselfS1184, _M0L8_2aparamS1185);
  return 0;
}

int32_t _M0IPB13StringBuilderPB6Logger67write__view_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLogger(
  void* _M0L11_2aobj__ptrS1183,
  struct _M0TPC16string10StringView _M0L8_2aparamS1182
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS1181 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS1183;
  _M0IPB13StringBuilderPB6Logger11write__view(_M0L7_2aselfS1181, _M0L8_2aparamS1182);
  return 0;
}

int32_t _M0IP016_24default__implPB6Logger72write__substring_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLoggerGRPB13StringBuilderE(
  void* _M0L11_2aobj__ptrS1180,
  moonbit_string_t _M0L8_2aparamS1177,
  int32_t _M0L8_2aparamS1178,
  int32_t _M0L8_2aparamS1179
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS1176 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS1180;
  _M0IP016_24default__implPB6Logger16write__substringGRPB13StringBuilderE(_M0L7_2aselfS1176, _M0L8_2aparamS1177, _M0L8_2aparamS1178, _M0L8_2aparamS1179);
  return 0;
}

int32_t _M0IPB13StringBuilderPB6Logger69write__string_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLogger(
  void* _M0L11_2aobj__ptrS1175,
  moonbit_string_t _M0L8_2aparamS1174
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS1173 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS1175;
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L7_2aselfS1173, _M0L8_2aparamS1174);
  return 0;
}

void moonbit_init() {
  moonbit_layout_table = moonbit_layout_table_data;
  int64_t _tmp_3180 = 9218868437227405311ll;
  int64_t _tmp_3181;
  int64_t _tmp_3182;
  int64_t _tmp_3183;
  int64_t _tmp_3184;
  _M0FPB18double__max__value = *(double*)&_tmp_3180;
  _tmp_3181 = -4503599627370497ll;
  _M0FPB18double__min__value = *(double*)&_tmp_3181;
  _tmp_3182 = 9221120237041090561ll;
  _M0FPC16double14not__a__number = *(double*)&_tmp_3182;
  _tmp_3183 = -4503599627370496ll;
  _M0FPC16double13neg__infinity = *(double*)&_tmp_3183;
  _tmp_3184 = 4503599627370496ll;
  _M0FPC16double13min__positive = *(double*)&_tmp_3184;
}

int main(int argc, char** argv) {
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS1129;
  float _M0L2dtS1130;
  int32_t _M0L2neS1131;
  int32_t _M0L3ni1S1132;
  int32_t _M0L3ni2S1133;
  struct _M0TP26RiantR8snn__mbt13AdExParameter* _M0L8e__paramS1134;
  struct _M0TP26RiantR8snn__mbt6Tripod* _M0L6e__popS1135;
  struct _M0TP26RiantR8snn__mbt11IFParameter* _M0L9i1__paramS1136;
  struct _M0TP26RiantR8snn__mbt2IF* _M0L7i1__popS1137;
  struct _M0TP26RiantR8snn__mbt11IFParameter* _M0L9i2__paramS1138;
  struct _M0TP26RiantR8snn__mbt2IF* _M0L7i2__popS1139;
  int32_t _M0L7_2abindS1140;
  int32_t _M0L1kS1141;
  int32_t _M0L7_2abindS1143;
  int32_t _M0L1kS1144;
  struct _M0TP26RiantR8snn__mbt24CompartmentSynapseTripod* _M0L9i1__to__eS1146;
  struct _M0TP26RiantR8snn__mbt24CompartmentSynapseTripod* _M0L9i2__to__eS1147;
  struct _M0TP26RiantR8snn__mbt9IstdpRate* _M0L6_2atmpS1286;
  struct _M0TP26RiantR8snn__mbt14IstdpRateEntry* _M0L16i1__to__e__entryS1148;
  struct _M0TP26RiantR8snn__mbt14IstdpPotential* _M0L6_2atmpS1285;
  struct _M0TP26RiantR8snn__mbt19IstdpPotentialEntry* _M0L16i2__to__e__entryS1149;
  moonbit_string_t _M0L6_2atmpS1199;
  moonbit_string_t _M0L6_2atmpS1198;
  moonbit_string_t _M0L6_2atmpS1197;
  moonbit_string_t _M0L6_2atmpS1202;
  moonbit_string_t _M0L6_2atmpS1201;
  moonbit_string_t _M0L6_2atmpS1200;
  moonbit_string_t _M0L6_2atmpS1205;
  moonbit_string_t _M0L6_2atmpS1204;
  moonbit_string_t _M0L6_2atmpS1203;
  float _M0L12duration__msS1150;
  float _M0L6_2atmpS1284;
  int32_t _M0L12total__stepsS1151;
  struct _M0TPB8MutLocalGiE* _M0L9e__spikesS1152;
  struct _M0TPB8MutLocalGiE* _M0L10i1__spikesS1153;
  struct _M0TPB8MutLocalGiE* _M0L10i2__spikesS1154;
  int32_t _M0L7_2abindS1155;
  int32_t _M0L4stepS1156;
  moonbit_string_t _M0L6_2atmpS1250;
  moonbit_string_t _M0L6_2atmpS1249;
  moonbit_string_t _M0L6_2atmpS1248;
  int32_t _M0L3valS1253;
  moonbit_string_t _M0L6_2atmpS1252;
  moonbit_string_t _M0L6_2atmpS1251;
  int32_t _M0L3valS1256;
  moonbit_string_t _M0L6_2atmpS1255;
  moonbit_string_t _M0L6_2atmpS1254;
  int32_t _M0L3valS1259;
  moonbit_string_t _M0L6_2atmpS1258;
  moonbit_string_t _M0L6_2atmpS1257;
  int32_t _M0L3valS1283;
  float _M0L6_2atmpS1282;
  float _M0L6_2atmpS1281;
  float _M0L6_2atmpS1279;
  float _M0L6_2atmpS1280;
  float _M0L7e__rateS1167;
  int32_t _M0L3valS1278;
  float _M0L6_2atmpS1277;
  float _M0L6_2atmpS1276;
  float _M0L6_2atmpS1274;
  float _M0L6_2atmpS1275;
  float _M0L8i1__rateS1168;
  int32_t _M0L3valS1273;
  float _M0L6_2atmpS1272;
  float _M0L6_2atmpS1271;
  float _M0L6_2atmpS1269;
  float _M0L6_2atmpS1270;
  float _M0L8i2__rateS1169;
  moonbit_string_t _M0L6_2atmpS1262;
  moonbit_string_t _M0L6_2atmpS1261;
  moonbit_string_t _M0L6_2atmpS1260;
  moonbit_string_t _M0L6_2atmpS1265;
  moonbit_string_t _M0L6_2atmpS1264;
  moonbit_string_t _M0L6_2atmpS1263;
  moonbit_string_t _M0L6_2atmpS1268;
  moonbit_string_t _M0L6_2atmpS1267;
  moonbit_string_t _M0L6_2atmpS1266;
  moonbit_runtime_init(argc, argv);
  moonbit_init();
  #line 45 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_network\\main.mbt"
  _M0L3rngS1129 = _M0MP26RiantR8snn__mbt7Xoshiro7default();
  _M0L2dtS1130 = 0x1p-3f;
  _M0L2neS1131 = 20;
  _M0L3ni1S1132 = 5;
  _M0L3ni2S1133 = 5;
  #line 55 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_network\\main.mbt"
  _M0L8e__paramS1134 = _M0MP26RiantR8snn__mbt13AdExParameter3new();
  #line 56 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_network\\main.mbt"
  _M0L6e__popS1135
  = _M0MP26RiantR8snn__mbt6Tripod3new(_M0L2neS1131, _M0L8e__paramS1134, _M0L3rngS1129);
  moonbit_decref(_M0L8e__paramS1134);
  #line 59 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_network\\main.mbt"
  _M0L9i1__paramS1136
  = _M0MP26RiantR8snn__mbt11IFParameter6custom(0x1.cp+2f, -0x1.9p+5f, -0x1.ep+5f, -0x1.b8p+5f, 0x1p+0f);
  #line 60 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_network\\main.mbt"
  _M0L7i1__popS1137
  = _M0MP26RiantR8snn__mbt2IF3new(_M0L3ni1S1132, _M0L9i1__paramS1136, _M0L3rngS1129);
  moonbit_decref(_M0L9i1__paramS1136);
  #line 63 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_network\\main.mbt"
  _M0L9i2__paramS1138
  = _M0MP26RiantR8snn__mbt11IFParameter6custom(0x1.4p+4f, -0x1.9p+5f, -0x1.ep+5f, -0x1.b8p+5f, 0x1p+0f);
  #line 64 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_network\\main.mbt"
  _M0L7i2__popS1139
  = _M0MP26RiantR8snn__mbt2IF3new(_M0L3ni2S1133, _M0L9i2__paramS1138, _M0L3rngS1129);
  moonbit_decref(_M0L9i2__paramS1138);
  _M0L7_2abindS1140 = 0;
  _M0L1kS1141 = _M0L7_2abindS1140;
  while (1) {
    if (_M0L1kS1141 < _M0L3ni1S1132) {
      struct _M0TPB5ArrayGfE* _M0L1iS1193 = _M0L7i1__popS1137->$7;
      int32_t _M0L6_2atmpS1194;
      moonbit_incref(_M0L1iS1193);
      #line 73 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_network\\main.mbt"
      _M0MPC15array5Array3setGfE(_M0L1iS1193, _M0L1kS1141, 0x1.f4p+7f);
      moonbit_decref(_M0L1iS1193);
      _M0L6_2atmpS1194 = _M0L1kS1141 + 1;
      _M0L1kS1141 = _M0L6_2atmpS1194;
      continue;
    }
    break;
  }
  _M0L7_2abindS1143 = 0;
  _M0L1kS1144 = _M0L7_2abindS1143;
  while (1) {
    if (_M0L1kS1144 < _M0L3ni2S1133) {
      struct _M0TPB5ArrayGfE* _M0L1iS1195 = _M0L7i2__popS1139->$7;
      int32_t _M0L6_2atmpS1196;
      moonbit_incref(_M0L1iS1195);
      #line 74 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_network\\main.mbt"
      _M0MPC15array5Array3setGfE(_M0L1iS1195, _M0L1kS1144, 0x1.f4p+7f);
      moonbit_decref(_M0L1iS1195);
      _M0L6_2atmpS1196 = _M0L1kS1144 + 1;
      _M0L1kS1144 = _M0L6_2atmpS1196;
      continue;
    }
    break;
  }
  #line 77 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_network\\main.mbt"
  _M0L9i1__to__eS1146
  = _M0MP26RiantR8snn__mbt24CompartmentSynapseTripod6random(_M0L7i1__popS1137, _M0L6e__popS1135, (moonbit_string_t)moonbit_string_literal_0.data, (moonbit_string_t)moonbit_string_literal_3.data, 0x1.4p+2f, 0x0p+0f, 0x1.999999999999ap-3f, _M0L3rngS1129);
  #line 82 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_network\\main.mbt"
  _M0L9i2__to__eS1147
  = _M0MP26RiantR8snn__mbt24CompartmentSynapseTripod6random(_M0L7i2__popS1139, _M0L6e__popS1135, (moonbit_string_t)moonbit_string_literal_0.data, (moonbit_string_t)moonbit_string_literal_2.data, 0x1.4p+2f, 0x0p+0f, 0x1.999999999999ap-3f, _M0L3rngS1129);
  moonbit_decref(_M0L3rngS1129);
  _M0L6_2atmpS1286 = 0;
  #line 89 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_network\\main.mbt"
  _M0L16i1__to__e__entryS1148
  = _M0MP26RiantR8snn__mbt14IstdpRateEntry3new(0, _M0L3ni1S1132, _M0L2neS1131, _M0L6_2atmpS1286);
  if (_M0L6_2atmpS1286) {
    moonbit_decref(_M0L6_2atmpS1286);
  }
  _M0L6_2atmpS1285 = 0;
  #line 90 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_network\\main.mbt"
  _M0L16i2__to__e__entryS1149
  = _M0MP26RiantR8snn__mbt19IstdpPotentialEntry3new(0, _M0L3ni2S1133, _M0L2neS1131, _M0L6_2atmpS1285);
  if (_M0L6_2atmpS1285) {
    moonbit_decref(_M0L6_2atmpS1285);
  }
  #line 92 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_network\\main.mbt"
  _M0FPB7printlnGsE((moonbit_string_t)moonbit_string_literal_21.data);
  #line 93 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_network\\main.mbt"
  _M0FPB7printlnGsE((moonbit_string_t)moonbit_string_literal_8.data);
  #line 94 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_network\\main.mbt"
  _M0FPB7printlnGsE((moonbit_string_t)moonbit_string_literal_22.data);
  #line 95 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_network\\main.mbt"
  _M0L6_2atmpS1199 = _M0MPC13int3Int18to__string_2einner(_M0L2neS1131, 10);
  #line 95 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_network\\main.mbt"
  _M0L6_2atmpS1198
  = moonbit_add_string((moonbit_string_t)moonbit_string_literal_23.data, _M0L6_2atmpS1199);
  moonbit_decref(_M0L6_2atmpS1199);
  #line 95 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_network\\main.mbt"
  _M0L6_2atmpS1197
  = moonbit_add_string(_M0L6_2atmpS1198, (moonbit_string_t)moonbit_string_literal_24.data);
  moonbit_decref(_M0L6_2atmpS1198);
  #line 95 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_network\\main.mbt"
  _M0FPB7printlnGsE(_M0L6_2atmpS1197);
  moonbit_decref(_M0L6_2atmpS1197);
  #line 96 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_network\\main.mbt"
  _M0L6_2atmpS1202 = _M0MPC13int3Int18to__string_2einner(_M0L3ni1S1132, 10);
  #line 96 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_network\\main.mbt"
  _M0L6_2atmpS1201
  = moonbit_add_string((moonbit_string_t)moonbit_string_literal_25.data, _M0L6_2atmpS1202);
  moonbit_decref(_M0L6_2atmpS1202);
  #line 96 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_network\\main.mbt"
  _M0L6_2atmpS1200
  = moonbit_add_string(_M0L6_2atmpS1201, (moonbit_string_t)moonbit_string_literal_26.data);
  moonbit_decref(_M0L6_2atmpS1201);
  #line 96 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_network\\main.mbt"
  _M0FPB7printlnGsE(_M0L6_2atmpS1200);
  moonbit_decref(_M0L6_2atmpS1200);
  #line 97 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_network\\main.mbt"
  _M0L6_2atmpS1205 = _M0MPC13int3Int18to__string_2einner(_M0L3ni2S1133, 10);
  #line 97 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_network\\main.mbt"
  _M0L6_2atmpS1204
  = moonbit_add_string((moonbit_string_t)moonbit_string_literal_27.data, _M0L6_2atmpS1205);
  moonbit_decref(_M0L6_2atmpS1205);
  #line 97 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_network\\main.mbt"
  _M0L6_2atmpS1203
  = moonbit_add_string(_M0L6_2atmpS1204, (moonbit_string_t)moonbit_string_literal_28.data);
  moonbit_decref(_M0L6_2atmpS1204);
  #line 97 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_network\\main.mbt"
  _M0FPB7printlnGsE(_M0L6_2atmpS1203);
  moonbit_decref(_M0L6_2atmpS1203);
  #line 98 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_network\\main.mbt"
  _M0FPB7printlnGsE((moonbit_string_t)moonbit_string_literal_8.data);
  #line 99 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_network\\main.mbt"
  _M0FPB7printlnGsE((moonbit_string_t)moonbit_string_literal_29.data);
  #line 100 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_network\\main.mbt"
  _M0FPB7printlnGsE((moonbit_string_t)moonbit_string_literal_30.data);
  #line 101 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_network\\main.mbt"
  _M0FPB7printlnGsE((moonbit_string_t)moonbit_string_literal_31.data);
  #line 102 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_network\\main.mbt"
  _M0FPB7printlnGsE((moonbit_string_t)moonbit_string_literal_32.data);
  #line 103 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_network\\main.mbt"
  _M0FPB7printlnGsE((moonbit_string_t)moonbit_string_literal_33.data);
  #line 104 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_network\\main.mbt"
  _M0FPB7printlnGsE((moonbit_string_t)moonbit_string_literal_34.data);
  #line 105 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_network\\main.mbt"
  _M0FPB7printlnGsE((moonbit_string_t)moonbit_string_literal_8.data);
  _M0L12duration__msS1150 = 0x1.f4p+8f;
  _M0L6_2atmpS1284 = _M0L12duration__msS1150 / _M0L2dtS1130;
  #line 109 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_network\\main.mbt"
  _M0L12total__stepsS1151 = _M0MPC15float5Float7to__int(_M0L6_2atmpS1284);
  _M0L9e__spikesS1152
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L9e__spikesS1152)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L9e__spikesS1152->$0 = 0;
  _M0L10i1__spikesS1153
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L10i1__spikesS1153)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L10i1__spikesS1153->$0 = 0;
  _M0L10i2__spikesS1154
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L10i2__spikesS1154)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L10i2__spikesS1154->$0 = 0;
  _M0L7_2abindS1155 = 0;
  _M0L4stepS1156 = _M0L7_2abindS1155;
  while (1) {
    if (_M0L4stepS1156 < _M0L12total__stepsS1151) {
      float _M0L6_2atmpS1207 = (float)_M0L4stepS1156;
      float _M0L6_2atmpS1206 = _M0L6_2atmpS1207 * _M0L2dtS1130;
      float _M0L6_2atmpS1209;
      float _M0L6_2atmpS1208;
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS1221;
      struct _M0TPB5ArrayGfE* _M0L4valsS1210;
      struct _M0TPB5ArrayGbE* _M0L4fireS1211;
      struct _M0TPB5ArrayGbE* _M0L4fireS1212;
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS1220;
      struct _M0TPB5ArrayGiE* _M0L6colptrS1213;
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS1219;
      struct _M0TPB5ArrayGiE* _M0L6rowptrS1214;
      struct _M0TP26RiantR8snn__mbt18IstdpRateVariables* _M0L4varsS1215;
      struct _M0TP26RiantR8snn__mbt9IstdpRate* _M0L5paramS1216;
      float _M0L6_2atmpS1218;
      float _M0L6_2atmpS1217;
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS1234;
      struct _M0TPB5ArrayGfE* _M0L4valsS1222;
      struct _M0TPB5ArrayGbE* _M0L4fireS1223;
      struct _M0TPB5ArrayGbE* _M0L4fireS1224;
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS1233;
      struct _M0TPB5ArrayGiE* _M0L6colptrS1225;
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS1232;
      struct _M0TPB5ArrayGiE* _M0L6rowptrS1226;
      struct _M0TPB5ArrayGfE* _M0L5v__d1S1227;
      struct _M0TP26RiantR8snn__mbt23IstdpPotentialVariables* _M0L4varsS1228;
      struct _M0TP26RiantR8snn__mbt14IstdpPotential* _M0L5paramS1229;
      float _M0L6_2atmpS1231;
      float _M0L6_2atmpS1230;
      int32_t _M0L7_2abindS1157;
      int32_t _M0L1kS1158;
      int32_t _M0L7_2abindS1160;
      int32_t _M0L1kS1161;
      int32_t _M0L7_2abindS1163;
      int32_t _M0L1kS1164;
      int32_t _M0L6_2atmpS1247;
      #line 116 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_network\\main.mbt"
      _M0FP26RiantR8snn__mbt28forward__compartment__tripod(_M0L9i1__to__eS1146, _M0L6_2atmpS1206);
      _M0L6_2atmpS1209 = (float)_M0L4stepS1156;
      _M0L6_2atmpS1208 = _M0L6_2atmpS1209 * _M0L2dtS1130;
      #line 117 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_network\\main.mbt"
      _M0FP26RiantR8snn__mbt28forward__compartment__tripod(_M0L9i2__to__eS1147, _M0L6_2atmpS1208);
      #line 119 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_network\\main.mbt"
      _M0FP26RiantR8snn__mbt14step__synapses(_M0L7i1__popS1137, _M0L2dtS1130);
      #line 120 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_network\\main.mbt"
      _M0FP26RiantR8snn__mbt17synaptic__current(_M0L7i1__popS1137);
      #line 121 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_network\\main.mbt"
      _M0FP26RiantR8snn__mbt12step__neuron(_M0L7i1__popS1137, _M0L2dtS1130);
      #line 122 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_network\\main.mbt"
      _M0FP26RiantR8snn__mbt14step__synapses(_M0L7i2__popS1139, _M0L2dtS1130);
      #line 123 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_network\\main.mbt"
      _M0FP26RiantR8snn__mbt17synaptic__current(_M0L7i2__popS1139);
      #line 124 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_network\\main.mbt"
      _M0FP26RiantR8snn__mbt12step__neuron(_M0L7i2__popS1139, _M0L2dtS1130);
      #line 126 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_network\\main.mbt"
      _M0FP26RiantR8snn__mbt12step__tripod(_M0L6e__popS1135, _M0L2dtS1130);
      _M0L6matrixS1221 = _M0L9i1__to__eS1146->$4;
      _M0L4valsS1210 = _M0L6matrixS1221->$4;
      _M0L4fireS1211 = _M0L7i1__popS1137->$5;
      _M0L4fireS1212 = _M0L6e__popS1135->$30;
      _M0L6matrixS1220 = _M0L9i1__to__eS1146->$4;
      _M0L6colptrS1213 = _M0L6matrixS1220->$3;
      _M0L6matrixS1219 = _M0L9i1__to__eS1146->$4;
      _M0L6rowptrS1214 = _M0L6matrixS1219->$2;
      _M0L4varsS1215 = _M0L16i1__to__e__entryS1148->$4;
      _M0L5paramS1216 = _M0L16i1__to__e__entryS1148->$3;
      _M0L6_2atmpS1218 = (float)_M0L4stepS1156;
      _M0L6_2atmpS1217 = _M0L6_2atmpS1218 * _M0L2dtS1130;
      moonbit_incref(_M0L5paramS1216);
      moonbit_incref(_M0L4varsS1215);
      moonbit_incref(_M0L6rowptrS1214);
      moonbit_incref(_M0L6colptrS1213);
      moonbit_incref(_M0L4fireS1212);
      moonbit_incref(_M0L4fireS1211);
      moonbit_incref(_M0L4valsS1210);
      #line 130 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_network\\main.mbt"
      _M0FP26RiantR8snn__mbt17istdp__rate__step(_M0L4valsS1210, _M0L4fireS1211, _M0L4fireS1212, _M0L6colptrS1213, _M0L6rowptrS1214, _M0L4varsS1215, _M0L5paramS1216, _M0L6_2atmpS1217, _M0L2dtS1130);
      moonbit_decref(_M0L4valsS1210);
      moonbit_decref(_M0L4fireS1211);
      moonbit_decref(_M0L4fireS1212);
      moonbit_decref(_M0L6colptrS1213);
      moonbit_decref(_M0L6rowptrS1214);
      moonbit_decref(_M0L4varsS1215);
      moonbit_decref(_M0L5paramS1216);
      _M0L6matrixS1234 = _M0L9i2__to__eS1147->$4;
      _M0L4valsS1222 = _M0L6matrixS1234->$4;
      _M0L4fireS1223 = _M0L7i2__popS1139->$5;
      _M0L4fireS1224 = _M0L6e__popS1135->$30;
      _M0L6matrixS1233 = _M0L9i2__to__eS1147->$4;
      _M0L6colptrS1225 = _M0L6matrixS1233->$3;
      _M0L6matrixS1232 = _M0L9i2__to__eS1147->$4;
      _M0L6rowptrS1226 = _M0L6matrixS1232->$2;
      _M0L5v__d1S1227 = _M0L6e__popS1135->$28;
      _M0L4varsS1228 = _M0L16i2__to__e__entryS1149->$4;
      _M0L5paramS1229 = _M0L16i2__to__e__entryS1149->$3;
      _M0L6_2atmpS1231 = (float)_M0L4stepS1156;
      _M0L6_2atmpS1230 = _M0L6_2atmpS1231 * _M0L2dtS1130;
      moonbit_incref(_M0L5paramS1229);
      moonbit_incref(_M0L4varsS1228);
      moonbit_incref(_M0L5v__d1S1227);
      moonbit_incref(_M0L6rowptrS1226);
      moonbit_incref(_M0L6colptrS1225);
      moonbit_incref(_M0L4fireS1224);
      moonbit_incref(_M0L4fireS1223);
      moonbit_incref(_M0L4valsS1222);
      #line 141 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_network\\main.mbt"
      _M0FP26RiantR8snn__mbt22istdp__potential__step(_M0L4valsS1222, _M0L4fireS1223, _M0L4fireS1224, _M0L6colptrS1225, _M0L6rowptrS1226, _M0L5v__d1S1227, _M0L4varsS1228, _M0L5paramS1229, _M0L6_2atmpS1230, _M0L2dtS1130);
      moonbit_decref(_M0L4valsS1222);
      moonbit_decref(_M0L4fireS1223);
      moonbit_decref(_M0L4fireS1224);
      moonbit_decref(_M0L6colptrS1225);
      moonbit_decref(_M0L6rowptrS1226);
      moonbit_decref(_M0L5v__d1S1227);
      moonbit_decref(_M0L4varsS1228);
      moonbit_decref(_M0L5paramS1229);
      _M0L7_2abindS1157 = 0;
      _M0L1kS1158 = _M0L7_2abindS1157;
      while (1) {
        if (_M0L1kS1158 < _M0L2neS1131) {
          struct _M0TPB5ArrayGbE* _M0L4fireS1235 = _M0L6e__popS1135->$30;
          int32_t _result_3189;
          int32_t _M0L6_2atmpS1238;
          moonbit_incref(_M0L4fireS1235);
          #line 154 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_network\\main.mbt"
          _result_3189
          = _M0MPC15array5Array2atGbE(_M0L4fireS1235, _M0L1kS1158);
          moonbit_decref(_M0L4fireS1235);
          if (_result_3189) {
            int32_t _M0L3valS1237 = _M0L9e__spikesS1152->$0;
            int32_t _M0L6_2atmpS1236 = _M0L3valS1237 + 1;
            _M0L9e__spikesS1152->$0 = _M0L6_2atmpS1236;
          }
          _M0L6_2atmpS1238 = _M0L1kS1158 + 1;
          _M0L1kS1158 = _M0L6_2atmpS1238;
          continue;
        }
        break;
      }
      _M0L7_2abindS1160 = 0;
      _M0L1kS1161 = _M0L7_2abindS1160;
      while (1) {
        if (_M0L1kS1161 < _M0L3ni1S1132) {
          struct _M0TPB5ArrayGbE* _M0L4fireS1239 = _M0L7i1__popS1137->$5;
          int32_t _result_3191;
          int32_t _M0L6_2atmpS1242;
          moonbit_incref(_M0L4fireS1239);
          #line 155 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_network\\main.mbt"
          _result_3191
          = _M0MPC15array5Array2atGbE(_M0L4fireS1239, _M0L1kS1161);
          moonbit_decref(_M0L4fireS1239);
          if (_result_3191) {
            int32_t _M0L3valS1241 = _M0L10i1__spikesS1153->$0;
            int32_t _M0L6_2atmpS1240 = _M0L3valS1241 + 1;
            _M0L10i1__spikesS1153->$0 = _M0L6_2atmpS1240;
          }
          _M0L6_2atmpS1242 = _M0L1kS1161 + 1;
          _M0L1kS1161 = _M0L6_2atmpS1242;
          continue;
        }
        break;
      }
      _M0L7_2abindS1163 = 0;
      _M0L1kS1164 = _M0L7_2abindS1163;
      while (1) {
        if (_M0L1kS1164 < _M0L3ni2S1133) {
          struct _M0TPB5ArrayGbE* _M0L4fireS1243 = _M0L7i2__popS1139->$5;
          int32_t _result_3193;
          int32_t _M0L6_2atmpS1246;
          moonbit_incref(_M0L4fireS1243);
          #line 156 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_network\\main.mbt"
          _result_3193
          = _M0MPC15array5Array2atGbE(_M0L4fireS1243, _M0L1kS1164);
          moonbit_decref(_M0L4fireS1243);
          if (_result_3193) {
            int32_t _M0L3valS1245 = _M0L10i2__spikesS1154->$0;
            int32_t _M0L6_2atmpS1244 = _M0L3valS1245 + 1;
            _M0L10i2__spikesS1154->$0 = _M0L6_2atmpS1244;
          }
          _M0L6_2atmpS1246 = _M0L1kS1164 + 1;
          _M0L1kS1164 = _M0L6_2atmpS1246;
          continue;
        }
        break;
      }
      _M0L6_2atmpS1247 = _M0L4stepS1156 + 1;
      _M0L4stepS1156 = _M0L6_2atmpS1247;
      continue;
    } else {
      moonbit_decref(_M0L16i2__to__e__entryS1149);
      moonbit_decref(_M0L16i1__to__e__entryS1148);
      moonbit_decref(_M0L9i2__to__eS1147);
      moonbit_decref(_M0L9i1__to__eS1146);
      moonbit_decref(_M0L7i2__popS1139);
      moonbit_decref(_M0L7i1__popS1137);
      moonbit_decref(_M0L6e__popS1135);
    }
    break;
  }
  #line 159 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_network\\main.mbt"
  _M0L6_2atmpS1250
  = _M0IPC15float5FloatPB4Show10to__string(_M0L12duration__msS1150);
  #line 159 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_network\\main.mbt"
  _M0L6_2atmpS1249
  = moonbit_add_string((moonbit_string_t)moonbit_string_literal_35.data, _M0L6_2atmpS1250);
  moonbit_decref(_M0L6_2atmpS1250);
  #line 159 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_network\\main.mbt"
  _M0L6_2atmpS1248
  = moonbit_add_string(_M0L6_2atmpS1249, (moonbit_string_t)moonbit_string_literal_36.data);
  moonbit_decref(_M0L6_2atmpS1249);
  #line 159 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_network\\main.mbt"
  _M0FPB7printlnGsE(_M0L6_2atmpS1248);
  moonbit_decref(_M0L6_2atmpS1248);
  _M0L3valS1253 = _M0L9e__spikesS1152->$0;
  #line 160 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_network\\main.mbt"
  _M0L6_2atmpS1252 = _M0MPC13int3Int18to__string_2einner(_M0L3valS1253, 10);
  #line 160 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_network\\main.mbt"
  _M0L6_2atmpS1251
  = moonbit_add_string((moonbit_string_t)moonbit_string_literal_37.data, _M0L6_2atmpS1252);
  moonbit_decref(_M0L6_2atmpS1252);
  #line 160 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_network\\main.mbt"
  _M0FPB7printlnGsE(_M0L6_2atmpS1251);
  moonbit_decref(_M0L6_2atmpS1251);
  _M0L3valS1256 = _M0L10i1__spikesS1153->$0;
  #line 161 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_network\\main.mbt"
  _M0L6_2atmpS1255 = _M0MPC13int3Int18to__string_2einner(_M0L3valS1256, 10);
  #line 161 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_network\\main.mbt"
  _M0L6_2atmpS1254
  = moonbit_add_string((moonbit_string_t)moonbit_string_literal_38.data, _M0L6_2atmpS1255);
  moonbit_decref(_M0L6_2atmpS1255);
  #line 161 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_network\\main.mbt"
  _M0FPB7printlnGsE(_M0L6_2atmpS1254);
  moonbit_decref(_M0L6_2atmpS1254);
  _M0L3valS1259 = _M0L10i2__spikesS1154->$0;
  #line 162 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_network\\main.mbt"
  _M0L6_2atmpS1258 = _M0MPC13int3Int18to__string_2einner(_M0L3valS1259, 10);
  #line 162 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_network\\main.mbt"
  _M0L6_2atmpS1257
  = moonbit_add_string((moonbit_string_t)moonbit_string_literal_39.data, _M0L6_2atmpS1258);
  moonbit_decref(_M0L6_2atmpS1258);
  #line 162 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_network\\main.mbt"
  _M0FPB7printlnGsE(_M0L6_2atmpS1257);
  moonbit_decref(_M0L6_2atmpS1257);
  _M0L3valS1283 = _M0L9e__spikesS1152->$0;
  moonbit_decref(_M0L9e__spikesS1152);
  _M0L6_2atmpS1282 = (float)_M0L3valS1283;
  _M0L6_2atmpS1281 = _M0L6_2atmpS1282 * 0x1.f4p+9f;
  _M0L6_2atmpS1279 = _M0L6_2atmpS1281 / _M0L12duration__msS1150;
  _M0L6_2atmpS1280 = (float)_M0L2neS1131;
  _M0L7e__rateS1167 = _M0L6_2atmpS1279 / _M0L6_2atmpS1280;
  _M0L3valS1278 = _M0L10i1__spikesS1153->$0;
  moonbit_decref(_M0L10i1__spikesS1153);
  _M0L6_2atmpS1277 = (float)_M0L3valS1278;
  _M0L6_2atmpS1276 = _M0L6_2atmpS1277 * 0x1.f4p+9f;
  _M0L6_2atmpS1274 = _M0L6_2atmpS1276 / _M0L12duration__msS1150;
  _M0L6_2atmpS1275 = (float)_M0L3ni1S1132;
  _M0L8i1__rateS1168 = _M0L6_2atmpS1274 / _M0L6_2atmpS1275;
  _M0L3valS1273 = _M0L10i2__spikesS1154->$0;
  moonbit_decref(_M0L10i2__spikesS1154);
  _M0L6_2atmpS1272 = (float)_M0L3valS1273;
  _M0L6_2atmpS1271 = _M0L6_2atmpS1272 * 0x1.f4p+9f;
  _M0L6_2atmpS1269 = _M0L6_2atmpS1271 / _M0L12duration__msS1150;
  _M0L6_2atmpS1270 = (float)_M0L3ni2S1133;
  _M0L8i2__rateS1169 = _M0L6_2atmpS1269 / _M0L6_2atmpS1270;
  #line 166 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_network\\main.mbt"
  _M0L6_2atmpS1262
  = _M0IPC15float5FloatPB4Show10to__string(_M0L7e__rateS1167);
  #line 166 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_network\\main.mbt"
  _M0L6_2atmpS1261
  = moonbit_add_string((moonbit_string_t)moonbit_string_literal_40.data, _M0L6_2atmpS1262);
  moonbit_decref(_M0L6_2atmpS1262);
  #line 166 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_network\\main.mbt"
  _M0L6_2atmpS1260
  = moonbit_add_string(_M0L6_2atmpS1261, (moonbit_string_t)moonbit_string_literal_41.data);
  moonbit_decref(_M0L6_2atmpS1261);
  #line 166 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_network\\main.mbt"
  _M0FPB7printlnGsE(_M0L6_2atmpS1260);
  moonbit_decref(_M0L6_2atmpS1260);
  #line 167 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_network\\main.mbt"
  _M0L6_2atmpS1265
  = _M0IPC15float5FloatPB4Show10to__string(_M0L8i1__rateS1168);
  #line 167 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_network\\main.mbt"
  _M0L6_2atmpS1264
  = moonbit_add_string((moonbit_string_t)moonbit_string_literal_42.data, _M0L6_2atmpS1265);
  moonbit_decref(_M0L6_2atmpS1265);
  #line 167 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_network\\main.mbt"
  _M0L6_2atmpS1263
  = moonbit_add_string(_M0L6_2atmpS1264, (moonbit_string_t)moonbit_string_literal_41.data);
  moonbit_decref(_M0L6_2atmpS1264);
  #line 167 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_network\\main.mbt"
  _M0FPB7printlnGsE(_M0L6_2atmpS1263);
  moonbit_decref(_M0L6_2atmpS1263);
  #line 168 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_network\\main.mbt"
  _M0L6_2atmpS1268
  = _M0IPC15float5FloatPB4Show10to__string(_M0L8i2__rateS1169);
  #line 168 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_network\\main.mbt"
  _M0L6_2atmpS1267
  = moonbit_add_string((moonbit_string_t)moonbit_string_literal_43.data, _M0L6_2atmpS1268);
  moonbit_decref(_M0L6_2atmpS1268);
  #line 168 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_network\\main.mbt"
  _M0L6_2atmpS1266
  = moonbit_add_string(_M0L6_2atmpS1267, (moonbit_string_t)moonbit_string_literal_41.data);
  moonbit_decref(_M0L6_2atmpS1267);
  #line 168 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_network\\main.mbt"
  _M0FPB7printlnGsE(_M0L6_2atmpS1266);
  moonbit_decref(_M0L6_2atmpS1266);
  #line 169 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_network\\main.mbt"
  _M0FPB7printlnGsE((moonbit_string_t)moonbit_string_literal_8.data);
  #line 170 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_network\\main.mbt"
  _M0FPB7printlnGsE((moonbit_string_t)moonbit_string_literal_44.data);
  #line 171 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_network\\main.mbt"
  _M0FPB7printlnGsE((moonbit_string_t)moonbit_string_literal_45.data);
  #line 172 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\tripod_network\\main.mbt"
  _M0FPB7printlnGsE((moonbit_string_t)moonbit_string_literal_46.data);
  return 0;
}