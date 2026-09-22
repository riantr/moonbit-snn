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
#if MOONBIT_TRIAL_DELETION &&                                      \
    MOONBIT_ALLOCATOR == MOONBIT_ALLOCATOR_SYSTEM
  // System-allocator builds collect at allocation safe points. Mimalloc
  // builds use its registered deferred-free callback instead.
  if (moonbit_cycle_collection_threshold()) {
    moonbit_collect_cycles();
  }
#endif
  struct moonbit_object *ptr = (struct moonbit_object *)MOONBIT_MALLOC_RAW(
      sizeof(struct moonbit_object) + size);
  Moonbit_init_dynamic_rc(ptr, moonbit_BLOCK_KIND_REGULAR);
  return ptr + 1;
}

#define moonbit_malloc(obj) moonbit_malloc_inlined(obj)

#define MOONBIT_RC_COUNT_UNIT ((int32_t)(1u << MOONBIT_RC_COUNT_SHIFT))
#define raw_rc_is_dynamic(rc) ((int32_t)(rc) >= MOONBIT_RC_COUNT_UNIT)
#define raw_rc_is_shared(rc) ((int32_t)(rc) >= (MOONBIT_RC_COUNT_UNIT * 2))

extern const uint32_t *moonbit_layout_table;

// Borrows ptr; null and static/immortal objects are not unique. Keep external
// linkage here so native object backends can link this helper from runtime_core.c.
int32_t moonbit_is_unique_ptr(void *ptr) {
  return ptr != 0 && Moonbit_rc_count(Moonbit_object_header(ptr)) == 1;
}

// The compiler only emits this retain for references whose static type cannot
// participate in a cycle. They never need trial-deletion liveness bookkeeping.
static void moonbit_incref_cycle_free_inlined(void *ptr) {
  struct moonbit_object *header = Moonbit_object_header(ptr);
  int32_t const rc = header->rc;
  if (raw_rc_is_dynamic(rc)) {
    Moonbit_increase_rc_count(header);
  }
}

#define moonbit_incref_cycle_free moonbit_incref_cycle_free_inlined

static void moonbit_incref_inlined(void *ptr) {
  struct moonbit_object *header = Moonbit_object_header(ptr);
  int32_t const rc = header->rc;
  if (raw_rc_is_dynamic(rc)) {
    Moonbit_increase_rc_count(header);
    MOONBIT_MARK_LIVE(ptr);
  }
}

#define moonbit_incref moonbit_incref_inlined

static void moonbit_decref_cycle_free_inlined(void *ptr) {
  struct moonbit_object *header = Moonbit_object_header(ptr);
  int32_t const rc = header->rc;
  if (raw_rc_is_shared(rc)) {
    header->rc = rc - MOONBIT_RC_COUNT_UNIT;
  } else if (raw_rc_is_dynamic(rc)) {
    moonbit_drop_object(ptr);
  }
}

#define moonbit_decref_cycle_free moonbit_decref_cycle_free_inlined

static void moonbit_decref_inlined(void *ptr) {
  struct moonbit_object *header = Moonbit_object_header(ptr);
  int32_t const rc = header->rc;
  if (raw_rc_is_shared(rc)) {
    header->rc = rc - MOONBIT_RC_COUNT_UNIT;
    MOONBIT_ADD_POSSIBLE_ROOT(ptr);
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
struct _M0DTPC15error5Error48moonbitlang_2fcore_2fbuiltin_2eFailure_2eFailure;

struct _M0DTPC15error5Error58moonbitlang_2fcore_2fbuiltin_2eInspectError_2eInspectError;

struct _M0TP26RiantR8snn__mbt4AdEx;

struct _M0TP26RiantR8snn__mbt13AdExPostSpike;

struct _M0R124_24RiantR_2fsnn__mbt_2fexamples_2fadex__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c987;

struct _M0TWRPC15error5ErrorEs;

struct _M0DTPC15error5Error123RiantR_2fsnn__mbt_2fexamples_2fadex__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest;

struct _M0TWssbEu;

struct _M0TUsiE;

struct _M0TPB13StringBuilder;

struct _M0TPB5ArrayGfE;

struct _M0TPB17FloatingDecimal64;

struct _M0TP26RiantR8snn__mbt13AdExParameter;

struct _M0TP26RiantR8snn__mbt9AdExModel;

struct _M0DTPC16result6ResultGbRP46RiantR8snn__mbt8examples20adex__blackbox__test33MoonBitTestDriverInternalSkipTestE3Err;

struct _M0BTPB6Logger;

struct _M0TPB6Logger;

struct _M0TPB5ArrayGUsiEE;

struct _M0DTPC16result6ResultGOuRPC15error5ErrorE2Ok;

struct _M0TP26RiantR8snn__mbt7Xoshiro;

struct _M0TUmmmmE;

struct _M0TPB5ArrayGiE;

struct _M0TPB19MulShiftAll64Result;

struct _M0TP26RiantR8snn__mbt4Time;

struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE;

struct _M0DTPC16result6ResultGbRP46RiantR8snn__mbt8examples20adex__blackbox__test33MoonBitTestDriverInternalSkipTestE2Ok;

struct _M0DTPC15error5Error60moonbitlang_2fcore_2fbuiltin_2eSnapshotError_2eSnapshotError;

struct _M0TWRPC15error5ErrorEu;

struct _M0TURPC16string10StringViewRPB6LoggerE;

struct _M0TPB8MutLocalGiE;

struct _M0TPB5ArrayGRP26RiantR8snn__mbt18SpikingSynapseAdExE;

struct _M0TPB4Show;

struct _M0TWWuEuWRPC15error5ErrorEuEOuQRPC15error5Error;

struct _M0TPB5ArrayGbE;

struct _M0DTPC16result6ResultGOuRPC15error5ErrorE3Err;

struct _M0TPB5ArrayGRP26RiantR8snn__mbt11MonitorAdExE;

struct _M0BTPB4Show;

struct _M0TP26RiantR8snn__mbt11MonitorAdEx;

struct _M0TWuEu;

struct _M0TPC16string10StringView;

struct _M0KTPB6LoggerTPB13StringBuilder;

struct _M0R123_24RiantR_2fsnn__mbt_2fexamples_2fadex__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c982;

struct _M0TPB5ArrayGsE;

struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR;

struct _M0TWEu;

struct _M0DTPC15error5Error121RiantR_2fsnn__mbt_2fexamples_2fadex__blackbox__test_2eMoonBitTestDriverInternalJsError_2eMoonBitTestDriverInternalJsError;

struct _M0TPB5ArrayGRP26RiantR8snn__mbt4AdExE;

struct _M0TP26RiantR8snn__mbt18SpikingSynapseAdEx;

struct _M0TPB7Umul128;

struct _M0TPB8Pow5Pair;

struct _M0DTPC15error5Error48moonbitlang_2fcore_2fbuiltin_2eFailure_2eFailure {
  moonbit_string_t $0;
  
};

struct _M0DTPC15error5Error58moonbitlang_2fcore_2fbuiltin_2eInspectError_2eInspectError {
  moonbit_string_t $0;
  
};

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

struct _M0R124_24RiantR_2fsnn__mbt_2fexamples_2fadex__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c987 {
  int32_t(* code)(
    struct _M0TWssbEu*,
    moonbit_string_t,
    moonbit_string_t,
    int32_t
  );
  int32_t $0;
  moonbit_string_t $1;
  
};

struct _M0TWRPC15error5ErrorEs {
  moonbit_string_t(* code)(struct _M0TWRPC15error5ErrorEs*, void*);
  
};

struct _M0DTPC15error5Error123RiantR_2fsnn__mbt_2fexamples_2fadex__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest {
  moonbit_string_t $0;
  
};

struct _M0TWssbEu {
  int32_t(* code)(
    struct _M0TWssbEu*,
    moonbit_string_t,
    moonbit_string_t,
    int32_t
  );
  
};

struct _M0TUsiE {
  moonbit_string_t $0;
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

struct _M0TPB17FloatingDecimal64 {
  uint64_t $0;
  int32_t $1;
  
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

struct _M0TP26RiantR8snn__mbt9AdExModel {
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt4AdExE* $0;
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt18SpikingSynapseAdExE* $1;
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt11MonitorAdExE* $2;
  
};

struct _M0DTPC16result6ResultGbRP46RiantR8snn__mbt8examples20adex__blackbox__test33MoonBitTestDriverInternalSkipTestE3Err {
  void* $0;
  
};

struct _M0BTPB6Logger {
  int32_t(* $method_0)(void*, moonbit_string_t);
  int32_t(* $method_1)(void*, moonbit_string_t, int32_t, int32_t);
  int32_t(* $method_2)(void*, struct _M0TPC16string10StringView);
  int32_t(* $method_3)(void*, int32_t);
  int32_t(* $method_4)(void*, struct _M0TPB4Show);
  int32_t(* $method_5)(void*, struct _M0TPB4Show);
  
};

struct _M0TPB6Logger {
  struct _M0BTPB6Logger* $0;
  void* $1;
  
};

struct _M0TPB5ArrayGUsiEE {
  struct _M0TUsiE** $0;
  int32_t $1;
  
};

struct _M0DTPC16result6ResultGOuRPC15error5ErrorE2Ok {
  int32_t $0;
  
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

struct _M0TP26RiantR8snn__mbt4Time {
  struct _M0TPB5ArrayGfE* $0;
  struct _M0TPB5ArrayGiE* $1;
  float $2;
  
};

struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE {
  struct _M0TWWuEuWRPC15error5ErrorEuEOuQRPC15error5Error** $0;
  int32_t $1;
  
};

struct _M0DTPC16result6ResultGbRP46RiantR8snn__mbt8examples20adex__blackbox__test33MoonBitTestDriverInternalSkipTestE2Ok {
  int32_t $0;
  
};

struct _M0DTPC15error5Error60moonbitlang_2fcore_2fbuiltin_2eSnapshotError_2eSnapshotError {
  moonbit_string_t $0;
  
};

struct _M0TWRPC15error5ErrorEu {
  int32_t(* code)(struct _M0TWRPC15error5ErrorEu*, void*);
  
};

struct _M0TPC16string10StringView {
  moonbit_string_t $0;
  int32_t $1;
  int32_t $2;
  
};

struct _M0TURPC16string10StringViewRPB6LoggerE {
  struct _M0TPC16string10StringView $0;
  struct _M0TPB6Logger $1;
  
};

struct _M0TPB8MutLocalGiE {
  int32_t $0;
  
};

struct _M0TPB5ArrayGRP26RiantR8snn__mbt18SpikingSynapseAdExE {
  struct _M0TP26RiantR8snn__mbt18SpikingSynapseAdEx** $0;
  int32_t $1;
  
};

struct _M0TPB4Show {
  struct _M0BTPB4Show* $0;
  void* $1;
  
};

struct _M0TWWuEuWRPC15error5ErrorEuEOuQRPC15error5Error {
  struct moonbit_result_0(* code)(
    struct _M0TWWuEuWRPC15error5ErrorEuEOuQRPC15error5Error*,
    struct _M0TWuEu*,
    struct _M0TWRPC15error5ErrorEu*
  );
  
};

struct _M0TPB5ArrayGbE {
  uint8_t* $0;
  int32_t $1;
  
};

struct _M0DTPC16result6ResultGOuRPC15error5ErrorE3Err {
  void* $0;
  
};

struct _M0TPB5ArrayGRP26RiantR8snn__mbt11MonitorAdExE {
  struct _M0TP26RiantR8snn__mbt11MonitorAdEx** $0;
  int32_t $1;
  
};

struct _M0BTPB4Show {
  int32_t(* $method_0)(void*, struct _M0TPB6Logger);
  moonbit_string_t(* $method_1)(void*);
  
};

struct _M0TP26RiantR8snn__mbt11MonitorAdEx {
  struct _M0TP26RiantR8snn__mbt4AdEx* $0;
  moonbit_string_t $1;
  struct _M0TPB5ArrayGfE* $2;
  struct _M0TPB5ArrayGfE* $3;
  int32_t $4;
  
};

struct _M0TWuEu {
  int32_t(* code)(struct _M0TWuEu*, int32_t);
  
};

struct _M0KTPB6LoggerTPB13StringBuilder {
  struct _M0BTPB6Logger* $0;
  void* $1;
  
};

struct _M0R123_24RiantR_2fsnn__mbt_2fexamples_2fadex__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c982 {
  int32_t(* code)(struct _M0TWEu*);
  int32_t $0;
  moonbit_string_t $1;
  
};

struct _M0TPB5ArrayGsE {
  moonbit_string_t* $0;
  int32_t $1;
  
};

struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR {
  int32_t $0;
  int32_t $1;
  struct _M0TPB5ArrayGiE* $2;
  struct _M0TPB5ArrayGiE* $3;
  struct _M0TPB5ArrayGfE* $4;
  
};

struct _M0TWEu {
  int32_t(* code)(struct _M0TWEu*);
  
};

struct _M0DTPC15error5Error121RiantR_2fsnn__mbt_2fexamples_2fadex__blackbox__test_2eMoonBitTestDriverInternalJsError_2eMoonBitTestDriverInternalJsError {
  moonbit_string_t $0;
  
};

struct _M0TPB5ArrayGRP26RiantR8snn__mbt4AdExE {
  struct _M0TP26RiantR8snn__mbt4AdEx** $0;
  int32_t $1;
  
};

struct _M0TP26RiantR8snn__mbt18SpikingSynapseAdEx {
  struct _M0TP26RiantR8snn__mbt4AdEx* $0;
  struct _M0TP26RiantR8snn__mbt4AdEx* $1;
  moonbit_string_t $2;
  struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* $3;
  
};

struct _M0TPB7Umul128 {
  uint64_t $0;
  uint64_t $1;
  
};

struct _M0TPB8Pow5Pair {
  uint64_t $0;
  uint64_t $1;
  
};

struct moonbit_result_0 {
  int tag;
  union { int32_t ok; void* err;  } data;
  
};

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples20adex__blackbox__test28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples20adex__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  struct _M0TWEu*
);

int32_t _M0FP46RiantR8snn__mbt8examples20adex__blackbox__test44moonbit__test__driver__internal__do__execute(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t
);

moonbit_string_t _M0FP46RiantR8snn__mbt8examples20adex__blackbox__test44moonbit__test__driver__internal__do__executeN17error__to__stringS994(
  struct _M0TWRPC15error5ErrorEs*,
  void*
);

int32_t _M0FP46RiantR8snn__mbt8examples20adex__blackbox__test44moonbit__test__driver__internal__do__executeN14handle__resultS987(
  struct _M0TWssbEu*,
  moonbit_string_t,
  moonbit_string_t,
  int32_t
);

int32_t _M0FP46RiantR8snn__mbt8examples20adex__blackbox__test44moonbit__test__driver__internal__do__executeN13handle__startS982(
  struct _M0TWEu*
);

struct _M0TPB5ArrayGUsiEE* _M0FP46RiantR8snn__mbt8examples20adex__blackbox__test52moonbit__test__driver__internal__native__parse__args(
  
);

struct _M0TPB5ArrayGsE* _M0FP46RiantR8snn__mbt8examples20adex__blackbox__test52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS959(
  int32_t,
  moonbit_string_t,
  int32_t
);

int32_t _M0FP46RiantR8snn__mbt8examples20adex__blackbox__test52moonbit__test__driver__internal__native__parse__argsN45moonbit__test__driver__internal__parse__int__S952(
  int32_t,
  moonbit_string_t
);

#define _M0FP46RiantR8snn__mbt8examples20adex__blackbox__test52moonbit__test__driver__internal__get__cli__args__ffi moonbit_rt_get_cli_args

moonbit_string_t _M0MP46RiantR8snn__mbt8examples20adex__blackbox__test33MoonBitTestDriverInternalOsString10to__string(
  moonbit_string_t
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples20adex__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples20adex__blackbox__test41MoonBit__Test__Driver__Internal__No__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples20adex__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples20adex__blackbox__test43MoonBit__Test__Driver__Internal__With__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples20adex__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples20adex__blackbox__test48MoonBit__Test__Driver__Internal__Async__No__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples20adex__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples20adex__blackbox__test50MoonBit__Test__Driver__Internal__Async__With__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples20adex__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples20adex__blackbox__test50MoonBit__Test__Driver__Internal__With__Bench__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples20adex__blackbox__test28MoonBit__Async__Test__Driver20is__being__cancelledGRP46RiantR8snn__mbt8examples20adex__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  
);

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples20adex__blackbox__test28MoonBit__Async__Test__Driver17run__async__testsGRP46RiantR8snn__mbt8examples20adex__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*
);

struct _M0TP26RiantR8snn__mbt4AdEx* _M0MP26RiantR8snn__mbt4AdEx3new(
  int32_t,
  struct _M0TP26RiantR8snn__mbt13AdExParameter*,
  struct _M0TP26RiantR8snn__mbt7Xoshiro*
);

struct _M0TP26RiantR8snn__mbt4AdEx* _M0MP26RiantR8snn__mbt4AdEx16new__with__spike(
  int32_t,
  struct _M0TP26RiantR8snn__mbt13AdExParameter*,
  struct _M0TP26RiantR8snn__mbt13AdExPostSpike*,
  struct _M0TP26RiantR8snn__mbt7Xoshiro*
);

struct _M0TP26RiantR8snn__mbt13AdExParameter* _M0MP26RiantR8snn__mbt13AdExParameter3new(
  
);

struct _M0TP26RiantR8snn__mbt13AdExPostSpike* _M0MP26RiantR8snn__mbt13AdExPostSpike3new(
  
);

struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0MP26RiantR8snn__mbt7Xoshiro7default(
  
);

int32_t _M0FP26RiantR8snn__mbt14adex__sim__for(
  struct _M0TP26RiantR8snn__mbt9AdExModel*,
  float
);

int32_t _M0FP26RiantR8snn__mbt17step__adex__model(
  struct _M0TP26RiantR8snn__mbt9AdExModel*,
  struct _M0TP26RiantR8snn__mbt4Time*
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

int32_t _M0FP26RiantR8snn__mbt22forward__adex__synapse(
  struct _M0TP26RiantR8snn__mbt18SpikingSynapseAdEx*
);

int32_t _M0FP26RiantR8snn__mbt17record__one__adex(
  struct _M0TP26RiantR8snn__mbt11MonitorAdEx*,
  float
);

struct _M0TP26RiantR8snn__mbt11MonitorAdEx* _M0MP26RiantR8snn__mbt11MonitorAdEx6new__v(
  struct _M0TP26RiantR8snn__mbt4AdEx*,
  int32_t
);

int32_t _M0MP26RiantR8snn__mbt15SparseMatrixCSR7forward(
  struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR*,
  struct _M0TPB5ArrayGbE*,
  struct _M0TPB5ArrayGfE*
);

struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0MP26RiantR8snn__mbt7Xoshiro3new(
  uint64_t
);

struct _M0TUmmmmE* _M0FP26RiantR8snn__mbt10splitmix64(uint64_t);

uint64_t _M0FP26RiantR8snn__mbt5mix64(uint64_t);

int32_t _M0FP26RiantR8snn__mbt12update__time(
  struct _M0TP26RiantR8snn__mbt4Time*,
  float
);

int32_t _M0FP26RiantR8snn__mbt7set__dt(
  struct _M0TP26RiantR8snn__mbt4Time*,
  float
);

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

moonbit_string_t _M0IPC15float5FloatPB4Show10to__string(float);

int32_t _M0MPC15float5Float7to__int(float);

struct _M0TPB5ArrayGfE* _M0MPC15array5Array4makeGfE(int32_t, float);

struct _M0TPB5ArrayGbE* _M0MPC15array5Array4makeGbE(int32_t, int32_t);

struct _M0TPB5ArrayGiE* _M0MPC15array5Array4makeGiE(int32_t, int32_t);

int32_t _M0MPC15array5Array3setGfE(struct _M0TPB5ArrayGfE*, int32_t, float);

int32_t _M0MPC15array5Array3setGbE(struct _M0TPB5ArrayGbE*, int32_t, int32_t);

int32_t _M0MPC15array5Array3setGiE(struct _M0TPB5ArrayGiE*, int32_t, int32_t);

float _M0MPC15array5Array2atGfE(struct _M0TPB5ArrayGfE*, int32_t);

int32_t _M0MPC15array5Array2atGbE(struct _M0TPB5ArrayGbE*, int32_t);

struct _M0TP26RiantR8snn__mbt11MonitorAdEx* _M0MPC15array5Array2atGRP26RiantR8snn__mbt11MonitorAdExE(
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt11MonitorAdExE*,
  int32_t
);

moonbit_string_t _M0MPC15array5Array2atGsE(struct _M0TPB5ArrayGsE*, int32_t);

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

int32_t _M0MPC15array5Array4pushGsE(
  struct _M0TPB5ArrayGsE*,
  moonbit_string_t
);

int32_t _M0MPC15array5Array4pushGUsiEE(
  struct _M0TPB5ArrayGUsiEE*,
  struct _M0TUsiE*
);

int32_t _M0MPC15array5Array4pushGfE(struct _M0TPB5ArrayGfE*, float);

int32_t _M0MPC15array5Array7reallocGsE(struct _M0TPB5ArrayGsE*, int32_t);

int32_t _M0MPC15array5Array7reallocGUsiEE(
  struct _M0TPB5ArrayGUsiEE*,
  int32_t
);

int32_t _M0MPC15array5Array7reallocGfE(struct _M0TPB5ArrayGfE*, int32_t);

int32_t _M0MPC15array5Array14resize__bufferGsE(
  struct _M0TPB5ArrayGsE*,
  int32_t
);

int32_t _M0MPC15array5Array14resize__bufferGUsiEE(
  struct _M0TPB5ArrayGUsiEE*,
  int32_t
);

int32_t _M0MPC15array5Array14resize__bufferGfE(
  struct _M0TPB5ArrayGfE*,
  int32_t
);

int32_t _M0MPC15array5Array8capacityGsE(struct _M0TPB5ArrayGsE*);

int32_t _M0MPC15array5Array8capacityGUsiEE(struct _M0TPB5ArrayGUsiEE*);

int32_t _M0MPC15array5Array8capacityGfE(struct _M0TPB5ArrayGfE*);

int32_t _M0FPB23array__growth__capacity(int32_t, int32_t, int32_t);

int32_t _M0MPC15array5Array6lengthGfE(struct _M0TPB5ArrayGfE*);

float* _M0MPC15array5Array6bufferGfE(struct _M0TPB5ArrayGfE*);

uint8_t* _M0MPC15array5Array6bufferGbE(struct _M0TPB5ArrayGbE*);

struct _M0TP26RiantR8snn__mbt11MonitorAdEx** _M0MPC15array5Array6bufferGRP26RiantR8snn__mbt11MonitorAdExE(
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt11MonitorAdExE*
);

moonbit_string_t* _M0MPC15array5Array6bufferGsE(struct _M0TPB5ArrayGsE*);

struct _M0TUsiE** _M0MPC15array5Array6bufferGUsiEE(
  struct _M0TPB5ArrayGUsiEE*
);

int32_t* _M0MPC15array5Array6bufferGiE(struct _M0TPB5ArrayGiE*);

moonbit_string_t _M0IPC16string6StringPB4Show10to__string(moonbit_string_t);

int32_t _M0IPB13StringBuilderPB6Logger11write__view(
  struct _M0TPB13StringBuilder*,
  struct _M0TPC16string10StringView
);

moonbit_string_t _M0MPC16string6String17unsafe__substring(
  moonbit_string_t,
  int32_t,
  int32_t
);

moonbit_string_t _M0MPC15bytes5Bytes29to__unchecked__string_2einner(
  moonbit_bytes_t,
  int32_t,
  int64_t
);

#define _M0FPB19unsafe__sub__string moonbit_unsafe_bytes_sub_string

int32_t _M0MPC15array10FixedArray18blit__from__string(
  moonbit_bytes_t,
  int32_t,
  moonbit_string_t,
  int32_t,
  int32_t
);

int32_t _M0MPC14uint4UInt8to__byte(uint32_t);

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

moonbit_string_t _M0IP016_24default__implPB4Show10to__stringGRPB7FailureE(
  void*
);

int32_t _M0IP016_24default__implPB4Show6outputGsE(
  moonbit_string_t,
  struct _M0TPB6Logger
);

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

struct _M0TPC16string10StringView _M0MPC16string6String21clamped__view_2einner(
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

moonbit_string_t _M0MPC16string6String14escape_2einner(
  moonbit_string_t,
  int32_t
);

int32_t _M0MPC16string10StringView18escape__to_2einner(
  struct _M0TPC16string10StringView,
  struct _M0TPB6Logger,
  int32_t
);

int32_t _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(
  struct _M0TURPC16string10StringViewRPB6LoggerE*,
  int32_t,
  int32_t
);

struct _M0TPC16string10StringView _M0MPC16string10StringView21clamped__view_2einner(
  struct _M0TPC16string10StringView,
  int32_t,
  int64_t
);

moonbit_string_t _M0MPC14byte4Byte7to__hex(int32_t);

int32_t _M0MPC14byte4Byte7to__hexN14to__hex__digitS4391(int32_t);

int32_t _M0IPC14byte4BytePB3Sub3sub(int32_t, int32_t);

int32_t _M0IPC14byte4BytePB3Mod3mod(int32_t, int32_t);

int32_t _M0IPC14byte4BytePB3Div3div(int32_t, int32_t);

int32_t _M0IPC14byte4BytePB3Add3add(int32_t, int32_t);

int32_t _M0MPC16uint166UInt1616unsafe__to__char(int32_t);

int32_t _M0MPC16uint166UInt1623is__trailing__surrogate(int32_t);

int32_t _M0MPC16uint166UInt1622is__leading__surrogate(int32_t);

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

int32_t _M0MPC14byte4Byte8to__char(int32_t);

moonbit_string_t* _M0MPB18UninitializedArray23make__and__blit_2einnerGsE(
  moonbit_string_t*,
  int32_t,
  int32_t,
  int32_t,
  int32_t
);

struct _M0TUsiE** _M0MPB18UninitializedArray23make__and__blit_2einnerGUsiEE(
  struct _M0TUsiE**,
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

int32_t _M0MPB13StringBuilder13write__objectGsE(
  struct _M0TPB13StringBuilder*,
  moonbit_string_t
);

int32_t _M0MPB13StringBuilder13write__objectGiE(
  struct _M0TPB13StringBuilder*,
  int32_t
);

int32_t _M0MPB13StringBuilder13write__objectGmE(
  struct _M0TPB13StringBuilder*,
  uint64_t
);

moonbit_string_t* _M0MPB18UninitializedArray23unsafe__make__and__blitGsE(
  moonbit_string_t*,
  int32_t,
  int32_t,
  int32_t,
  int32_t
);

struct _M0TUsiE** _M0MPB18UninitializedArray23unsafe__make__and__blitGUsiEE(
  struct _M0TUsiE**,
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

int32_t _M0MPB18UninitializedArray12unsafe__blitGsE(
  moonbit_string_t*,
  int32_t,
  moonbit_string_t*,
  int32_t,
  int32_t
);

int32_t _M0MPB18UninitializedArray12unsafe__blitGUsiEE(
  struct _M0TUsiE**,
  int32_t,
  struct _M0TUsiE**,
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

int32_t _M0MPC15array10FixedArray12unsafe__blitGkE(
  uint16_t*,
  int32_t,
  uint16_t*,
  int32_t,
  int32_t
);

int32_t _M0MPC15array10FixedArray12unsafe__blitGRPB17UnsafeMaybeUninitGsEE(
  moonbit_string_t*,
  int32_t,
  moonbit_string_t*,
  int32_t,
  int32_t
);

int32_t _M0MPC15array10FixedArray12unsafe__blitGRPB17UnsafeMaybeUninitGUsiEEE(
  struct _M0TUsiE**,
  int32_t,
  struct _M0TUsiE**,
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

int32_t _M0MPB18UninitializedArray6lengthGsE(moonbit_string_t*);

int32_t _M0MPB18UninitializedArray6lengthGUsiEE(struct _M0TUsiE**);

int32_t _M0MPB18UninitializedArray6lengthGfE(float*);

int32_t _M0IPB7FailurePB4Show6output(void*, struct _M0TPB6Logger);

int32_t _M0MPB6Logger13write__objectGsE(
  struct _M0TPB6Logger,
  moonbit_string_t
);

int32_t _M0FPC15abort5abortGuE(moonbit_string_t);

uint16_t* _M0FPC15abort5abortGAkE(moonbit_string_t);

moonbit_string_t* _M0FPC15abort5abortGRPB18UninitializedArrayGsEE(
  moonbit_string_t
);

struct _M0TUsiE** _M0FPC15abort5abortGRPB18UninitializedArrayGUsiEEE(
  moonbit_string_t
);

float* _M0FPC15abort5abortGRPB18UninitializedArrayGfEE(moonbit_string_t);

int32_t _M0FPC15abort5abortGiE(moonbit_string_t);

moonbit_string_t _M0FP15Error10to__string(void*);

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

moonbit_string_t* moonbit_rt_get_cli_args();

float expf(float);

struct { int32_t rc; uint32_t meta; uint16_t const data[35]; 
} const moonbit_string_literal_2 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 34, 45, 45, 
    45, 45, 45, 32, 66, 69, 71, 73, 78, 32, 77, 79, 79, 78, 32, 84, 69, 
    83, 84, 32, 82, 69, 83, 85, 76, 84, 32, 45, 45, 45, 45, 45, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[13]; 
} const moonbit_string_literal_1 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 12, 115, 107, 
    105, 112, 112, 101, 100, 32, 116, 101, 115, 116, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[1]; 
} const moonbit_string_literal_0 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 0, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[3]; 
} const moonbit_string_literal_28 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 2, 92, 116, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[3]; 
} const moonbit_string_literal_26 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 2, 92, 114, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[16]; 
} const moonbit_string_literal_34 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 15, 44, 32, 
    100, 115, 116, 95, 111, 102, 102, 115, 101, 116, 32, 61, 32, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[19]; 
} const moonbit_string_literal_30 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 18, 105, 110, 
    118, 97, 108, 105, 100, 32, 99, 111, 100, 101, 32, 112, 111, 105, 
    110, 116, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[2]; 
} const moonbit_string_literal_17 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 1, 45, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[12]; 
} const moonbit_string_literal_5 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 11, 44, 34, 
    109, 101, 115, 115, 97, 103, 101, 34, 58, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[109]; 
} const moonbit_string_literal_42 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 108, 82, 105, 
    97, 110, 116, 82, 47, 115, 110, 110, 95, 109, 98, 116, 47, 101, 120, 
    97, 109, 112, 108, 101, 115, 47, 97, 100, 101, 120, 95, 98, 108, 
    97, 99, 107, 98, 111, 120, 95, 116, 101, 115, 116, 46, 77, 111, 111, 
    110, 66, 105, 116, 84, 101, 115, 116, 68, 114, 105, 118, 101, 114, 
    73, 110, 116, 101, 114, 110, 97, 108, 74, 115, 69, 114, 114, 111, 
    114, 46, 77, 111, 111, 110, 66, 105, 116, 84, 101, 115, 116, 68, 
    114, 105, 118, 101, 114, 73, 110, 116, 101, 114, 110, 97, 108, 74, 
    115, 69, 114, 114, 111, 114, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[53]; 
} const moonbit_string_literal_40 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 52, 109, 111, 
    111, 110, 98, 105, 116, 108, 97, 110, 103, 47, 99, 111, 114, 101, 
    47, 98, 117, 105, 108, 116, 105, 110, 46, 83, 110, 97, 112, 115, 
    104, 111, 116, 69, 114, 114, 111, 114, 46, 83, 110, 97, 112, 115, 
    104, 111, 116, 69, 114, 114, 111, 114, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[3]; 
} const moonbit_string_literal_25 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 2, 92, 110, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[31]; 
} const moonbit_string_literal_23 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 30, 114, 97, 
    100, 105, 120, 32, 109, 117, 115, 116, 32, 98, 101, 32, 98, 101, 
    116, 119, 101, 101, 110, 32, 50, 32, 97, 110, 100, 32, 51, 54, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[9]; 
} const moonbit_string_literal_18 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 8, 73, 110, 
    102, 105, 110, 105, 116, 121, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[4]; 
} const moonbit_string_literal_16 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 3, 78, 97, 78, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[2]; 
} const moonbit_string_literal_13 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 1, 119, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[25]; 
} const moonbit_string_literal_3 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 24, 123, 34, 
    116, 121, 112, 101, 34, 58, 34, 114, 101, 115, 117, 108, 116, 34, 
    44, 34, 102, 105, 108, 101, 34, 58, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[2]; 
} const moonbit_string_literal_14 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 1, 48, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[9]; 
} const moonbit_string_literal_35 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 8, 44, 32, 
    108, 101, 110, 32, 61, 32, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[6]; 
} const moonbit_string_literal_21 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 5, 102, 97, 
    108, 115, 101, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[37]; 
} const moonbit_string_literal_32 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 36, 98, 111, 
    117, 110, 100, 115, 32, 99, 104, 101, 99, 107, 32, 102, 97, 105, 
    108, 101, 100, 58, 32, 97, 108, 108, 111, 99, 97, 116, 101, 95, 108, 
    101, 110, 32, 61, 32, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[4]; 
} const moonbit_string_literal_29 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 3, 92, 117, 123, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[3]; 
} const moonbit_string_literal_10 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 2, 104, 101, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[3]; 
} const moonbit_string_literal_9 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 2, 103, 101, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[5]; 
} const moonbit_string_literal_20 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 4, 116, 114, 
    117, 101, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[111]; 
} const moonbit_string_literal_41 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 110, 82, 105, 
    97, 110, 116, 82, 47, 115, 110, 110, 95, 109, 98, 116, 47, 101, 120, 
    97, 109, 112, 108, 101, 115, 47, 97, 100, 101, 120, 95, 98, 108, 
    97, 99, 107, 98, 111, 120, 95, 116, 101, 115, 116, 46, 77, 111, 111, 
    110, 66, 105, 116, 84, 101, 115, 116, 68, 114, 105, 118, 101, 114, 
    73, 110, 116, 101, 114, 110, 97, 108, 83, 107, 105, 112, 84, 101, 
    115, 116, 46, 77, 111, 111, 110, 66, 105, 116, 84, 101, 115, 116, 
    68, 114, 105, 118, 101, 114, 73, 110, 116, 101, 114, 110, 97, 108, 
    83, 107, 105, 112, 84, 101, 115, 116, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[2]; 
} const moonbit_string_literal_38 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 1, 41, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[37]; 
} const moonbit_string_literal_24 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 36, 48, 49, 
    50, 51, 52, 53, 54, 55, 56, 57, 97, 98, 99, 100, 101, 102, 103, 104, 
    105, 106, 107, 108, 109, 110, 111, 112, 113, 114, 115, 116, 117, 
    118, 119, 120, 121, 122, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[3]; 
} const moonbit_string_literal_27 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 2, 92, 98, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[5]; 
} const moonbit_string_literal_12 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 4, 102, 105, 
    114, 101, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[51]; 
} const moonbit_string_literal_39 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 50, 109, 111, 
    111, 110, 98, 105, 116, 108, 97, 110, 103, 47, 99, 111, 114, 101, 
    47, 98, 117, 105, 108, 116, 105, 110, 46, 73, 110, 115, 112, 101, 
    99, 116, 69, 114, 114, 111, 114, 46, 73, 110, 115, 112, 101, 99, 
    116, 69, 114, 114, 111, 114, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[16]; 
} const moonbit_string_literal_36 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 15, 44, 32, 
    115, 114, 99, 46, 108, 101, 110, 103, 116, 104, 32, 61, 32, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[33]; 
} const moonbit_string_literal_7 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 32, 45, 45, 
    45, 45, 45, 32, 69, 78, 68, 32, 77, 79, 79, 78, 32, 84, 69, 83, 84, 
    32, 82, 69, 83, 85, 76, 84, 32, 45, 45, 45, 45, 45, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[16]; 
} const moonbit_string_literal_33 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 15, 44, 32, 
    115, 114, 99, 95, 111, 102, 102, 115, 101, 116, 32, 61, 32, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[2]; 
} const moonbit_string_literal_11 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 1, 118, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[24]; 
} const moonbit_string_literal_8 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 23, 123, 34, 
    116, 121, 112, 101, 34, 58, 34, 115, 116, 97, 114, 116, 34, 44, 34, 
    102, 105, 108, 101, 34, 58, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[24]; 
} const moonbit_string_literal_22 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 23, 65, 114, 
    114, 97, 121, 32, 99, 97, 112, 97, 99, 105, 116, 121, 32, 111, 118, 
    101, 114, 102, 108, 111, 119, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[10]; 
} const moonbit_string_literal_4 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 9, 44, 34, 
    105, 110, 100, 101, 120, 34, 58, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[26]; 
} const moonbit_string_literal_15 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 25, 73, 108, 
    108, 101, 103, 97, 108, 65, 114, 103, 117, 109, 101, 110, 116, 69, 
    120, 99, 101, 112, 116, 105, 111, 110, 32, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[9]; 
} const moonbit_string_literal_37 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 8, 70, 97, 
    105, 108, 117, 114, 101, 40, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[32]; 
} const moonbit_string_literal_31 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 31, 83, 116, 
    114, 105, 110, 103, 66, 117, 105, 108, 100, 101, 114, 32, 99, 97, 
    112, 97, 99, 105, 116, 121, 32, 111, 118, 101, 114, 102, 108, 111, 
    119, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[4]; 
} const moonbit_string_literal_19 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 3, 48, 46, 48, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[2]; 
} const moonbit_string_literal_6 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 1, 125, 0};

struct { int32_t rc; uint32_t meta; struct _M0TWRPC15error5ErrorEs data; 
} const _M0FP46RiantR8snn__mbt8examples20adex__blackbox__test44moonbit__test__driver__internal__do__executeN17error__to__stringS994$closure =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_REGULAR),
    Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0),
    _M0FP46RiantR8snn__mbt8examples20adex__blackbox__test44moonbit__test__driver__internal__do__executeN17error__to__stringS994
  };

struct { int32_t rc; uint32_t meta; struct _M0TWEu data; 
} const _M0IP016_24default__implP46RiantR8snn__mbt8examples20adex__blackbox__test28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples20adex__blackbox__test34MoonBit__Async__Test__Driver__ImplE$closure =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_REGULAR),
    Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0),
    _M0IP016_24default__implP46RiantR8snn__mbt8examples20adex__blackbox__test28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples20adex__blackbox__test34MoonBit__Async__Test__Driver__ImplE
  };

uint32_t const moonbit_layout_table_data[74] =
  {
    sizeof(struct _M0R123_24RiantR_2fsnn__mbt_2fexamples_2fadex__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c982)
    / 4, 1,
    offsetof(struct _M0R123_24RiantR_2fsnn__mbt_2fexamples_2fadex__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c982, $1)
    / 4
    * 2,
    sizeof(struct _M0R124_24RiantR_2fsnn__mbt_2fexamples_2fadex__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c987)
    / 4, 1,
    offsetof(struct _M0R124_24RiantR_2fsnn__mbt_2fexamples_2fadex__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c987, $1)
    / 4
    * 2,
    sizeof(struct _M0DTPC15error5Error123RiantR_2fsnn__mbt_2fexamples_2fadex__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest)
    / 4, 1,
    offsetof(struct _M0DTPC15error5Error123RiantR_2fsnn__mbt_2fexamples_2fadex__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest, $0)
    / 4
    * 2, sizeof(struct _M0TPB5ArrayGUsiEE) / 4, 1,
    offsetof(struct _M0TPB5ArrayGUsiEE, $0) / 4 * 2,
    sizeof(struct _M0TUsiE) / 4, 1, offsetof(struct _M0TUsiE, $0) / 4 * 2,
    sizeof(struct _M0TPB5ArrayGsE) / 4, 1,
    offsetof(struct _M0TPB5ArrayGsE, $0) / 4 * 2,
    sizeof(struct _M0TP26RiantR8snn__mbt4AdEx) / 4, 17,
    offsetof(struct _M0TP26RiantR8snn__mbt4AdEx, $0) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt4AdEx, $1) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt4AdEx, $3) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt4AdEx, $4) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt4AdEx, $5) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt4AdEx, $6) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt4AdEx, $7) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt4AdEx, $8) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt4AdEx, $9) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt4AdEx, $10) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt4AdEx, $11) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt4AdEx, $12) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt4AdEx, $13) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt4AdEx, $14) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt4AdEx, $15) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt4AdEx, $16) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt4AdEx, $17) / 4 * 2,
    sizeof(struct _M0TPB5ArrayGfE) / 4, 1,
    offsetof(struct _M0TPB5ArrayGfE, $0) / 4 * 2,
    sizeof(struct _M0TP26RiantR8snn__mbt11MonitorAdEx) / 4, 4,
    offsetof(struct _M0TP26RiantR8snn__mbt11MonitorAdEx, $0) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt11MonitorAdEx, $1) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt11MonitorAdEx, $2) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt11MonitorAdEx, $3) / 4 * 2,
    sizeof(struct _M0TPB5ArrayGiE) / 4, 1,
    offsetof(struct _M0TPB5ArrayGiE, $0) / 4 * 2,
    sizeof(struct _M0TP26RiantR8snn__mbt4Time) / 4, 2,
    offsetof(struct _M0TP26RiantR8snn__mbt4Time, $0) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt4Time, $1) / 4 * 2,
    sizeof(struct _M0TPB5ArrayGbE) / 4, 1,
    offsetof(struct _M0TPB5ArrayGbE, $0) / 4 * 2,
    sizeof(struct _M0TPC16string10StringView) / 4, 1,
    offsetof(struct _M0TPC16string10StringView, $0) / 4 * 2,
    sizeof(struct _M0TPB6Logger) / 4, 2,
    offsetof(struct _M0TPB6Logger, $0) / 4 * 2,
    offsetof(struct _M0TPB6Logger, $1) / 4 * 2,
    sizeof(struct _M0TURPC16string10StringViewRPB6LoggerE) / 4, 3,
    (offsetof(struct _M0TURPC16string10StringViewRPB6LoggerE, $0)
     + offsetof(struct _M0TPC16string10StringView, $0))
    / 4
    * 2,
    (offsetof(struct _M0TURPC16string10StringViewRPB6LoggerE, $1)
     + offsetof(struct _M0TPB6Logger, $0))
    / 4
    * 2,
    (offsetof(struct _M0TURPC16string10StringViewRPB6LoggerE, $1)
     + offsetof(struct _M0TPB6Logger, $1))
    / 4
    * 2, sizeof(struct _M0TPB13StringBuilder) / 4, 1,
    offsetof(struct _M0TPB13StringBuilder, $0) / 4 * 2,
    sizeof(struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE) / 4, 1,
    offsetof(struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE, $0) / 4 * 2
  };

struct _M0TWEu* _M0IP016_24default__implP46RiantR8snn__mbt8examples20adex__blackbox__test28MoonBit__Async__Test__Driver26is__being__cancelled_2ecloGRP46RiantR8snn__mbt8examples20adex__blackbox__test34MoonBit__Async__Test__Driver__ImplE =
  (struct _M0TWEu*)&_M0IP016_24default__implP46RiantR8snn__mbt8examples20adex__blackbox__test28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples20adex__blackbox__test34MoonBit__Async__Test__Driver__ImplE$closure.data;

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

float _M0FP26RiantR8snn__mbt2ms = 0x1p+0f;

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples20adex__blackbox__test28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples20adex__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  struct _M0TWEu* _M0L6_2aenvS2182
) {
  return _M0IP016_24default__implP46RiantR8snn__mbt8examples20adex__blackbox__test28MoonBit__Async__Test__Driver20is__being__cancelledGRP46RiantR8snn__mbt8examples20adex__blackbox__test34MoonBit__Async__Test__Driver__ImplE();
}

int32_t _M0FP46RiantR8snn__mbt8examples20adex__blackbox__test44moonbit__test__driver__internal__do__execute(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12async__testsS1015,
  moonbit_string_t _M0L8filenameS984,
  int32_t _M0L5indexS986
) {
  struct _M0R123_24RiantR_2fsnn__mbt_2fexamples_2fadex__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c982* _closure_2210;
  struct _M0TWEu* _M0L13handle__startS982;
  struct _M0R124_24RiantR_2fsnn__mbt_2fexamples_2fadex__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c987* _closure_2211;
  struct _M0TWssbEu* _M0L14handle__resultS987;
  struct _M0TWRPC15error5ErrorEs* _M0L17error__to__stringS994;
  void* _M0L11_2atry__errS1009;
  struct moonbit_result_0 _tmp_2213;
  int32_t _handle__error__result_2214;
  int32_t _M0L6_2atmpS2170;
  void* _M0L3errS1010;
  moonbit_string_t _M0L4nameS1012;
  struct _M0DTPC15error5Error123RiantR_2fsnn__mbt_2fexamples_2fadex__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest* _M0L36_2aMoonBitTestDriverInternalSkipTestS1013;
  moonbit_string_t _M0L7_2anameS1014;
  int32_t _M0L6_2acntS2204;
  #line 563 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\adex\\__generated_driver_for_blackbox_test.mbt"
  moonbit_incref_cycle_free(_M0L8filenameS984);
  _closure_2210
  = (struct _M0R123_24RiantR_2fsnn__mbt_2fexamples_2fadex__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c982*)moonbit_malloc(sizeof(struct _M0R123_24RiantR_2fsnn__mbt_2fexamples_2fadex__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c982));
  Moonbit_object_header(_closure_2210)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 0, 0);
  _closure_2210->code
  = &_M0FP46RiantR8snn__mbt8examples20adex__blackbox__test44moonbit__test__driver__internal__do__executeN13handle__startS982;
  _closure_2210->$0 = _M0L5indexS986;
  _closure_2210->$1 = _M0L8filenameS984;
  _M0L13handle__startS982 = (struct _M0TWEu*)_closure_2210;
  moonbit_incref_cycle_free(_M0L8filenameS984);
  _closure_2211
  = (struct _M0R124_24RiantR_2fsnn__mbt_2fexamples_2fadex__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c987*)moonbit_malloc(sizeof(struct _M0R124_24RiantR_2fsnn__mbt_2fexamples_2fadex__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c987));
  Moonbit_object_header(_closure_2211)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 3, 0);
  _closure_2211->code
  = &_M0FP46RiantR8snn__mbt8examples20adex__blackbox__test44moonbit__test__driver__internal__do__executeN14handle__resultS987;
  _closure_2211->$0 = _M0L5indexS986;
  _closure_2211->$1 = _M0L8filenameS984;
  _M0L14handle__resultS987 = (struct _M0TWssbEu*)_closure_2211;
  _M0L17error__to__stringS994
  = (struct _M0TWRPC15error5ErrorEs*)&_M0FP46RiantR8snn__mbt8examples20adex__blackbox__test44moonbit__test__driver__internal__do__executeN17error__to__stringS994$closure.data;
  #line 605 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\adex\\__generated_driver_for_blackbox_test.mbt"
  _tmp_2213
  = _M0IP016_24default__implP46RiantR8snn__mbt8examples20adex__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples20adex__blackbox__test41MoonBit__Test__Driver__Internal__No__ArgsE(_M0L12async__testsS1015, _M0L8filenameS984, _M0L5indexS986, _M0L13handle__startS982, _M0L14handle__resultS987, _M0L17error__to__stringS994);
  if (_tmp_2213.tag) {
    int32_t const _M0L5_2aokS2179 = _tmp_2213.data.ok;
    _handle__error__result_2214 = _M0L5_2aokS2179;
  } else {
    void* const _M0L6_2aerrS2180 = _tmp_2213.data.err;
    moonbit_decref_cycle_free(_M0L17error__to__stringS994);
    moonbit_decref_cycle_free(_M0L13handle__startS982);
    _M0L11_2atry__errS1009 = _M0L6_2aerrS2180;
    goto join_1008;
  }
  if (_handle__error__result_2214) {
    moonbit_decref_cycle_free(_M0L17error__to__stringS994);
    moonbit_decref_cycle_free(_M0L13handle__startS982);
    _M0L6_2atmpS2170 = 1;
  } else {
    struct moonbit_result_0 _tmp_2215;
    int32_t _handle__error__result_2216;
    #line 608 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\adex\\__generated_driver_for_blackbox_test.mbt"
    _tmp_2215
    = _M0IP016_24default__implP46RiantR8snn__mbt8examples20adex__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples20adex__blackbox__test43MoonBit__Test__Driver__Internal__With__ArgsE(_M0L12async__testsS1015, _M0L8filenameS984, _M0L5indexS986, _M0L13handle__startS982, _M0L14handle__resultS987, _M0L17error__to__stringS994);
    if (_tmp_2215.tag) {
      int32_t const _M0L5_2aokS2177 = _tmp_2215.data.ok;
      _handle__error__result_2216 = _M0L5_2aokS2177;
    } else {
      void* const _M0L6_2aerrS2178 = _tmp_2215.data.err;
      moonbit_decref_cycle_free(_M0L17error__to__stringS994);
      moonbit_decref_cycle_free(_M0L13handle__startS982);
      _M0L11_2atry__errS1009 = _M0L6_2aerrS2178;
      goto join_1008;
    }
    if (_handle__error__result_2216) {
      moonbit_decref_cycle_free(_M0L17error__to__stringS994);
      moonbit_decref_cycle_free(_M0L13handle__startS982);
      _M0L6_2atmpS2170 = 1;
    } else {
      struct moonbit_result_0 _tmp_2217;
      int32_t _handle__error__result_2218;
      #line 611 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\adex\\__generated_driver_for_blackbox_test.mbt"
      _tmp_2217
      = _M0IP016_24default__implP46RiantR8snn__mbt8examples20adex__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples20adex__blackbox__test48MoonBit__Test__Driver__Internal__Async__No__ArgsE(_M0L12async__testsS1015, _M0L8filenameS984, _M0L5indexS986, _M0L13handle__startS982, _M0L14handle__resultS987, _M0L17error__to__stringS994);
      if (_tmp_2217.tag) {
        int32_t const _M0L5_2aokS2175 = _tmp_2217.data.ok;
        _handle__error__result_2218 = _M0L5_2aokS2175;
      } else {
        void* const _M0L6_2aerrS2176 = _tmp_2217.data.err;
        moonbit_decref_cycle_free(_M0L17error__to__stringS994);
        moonbit_decref_cycle_free(_M0L13handle__startS982);
        _M0L11_2atry__errS1009 = _M0L6_2aerrS2176;
        goto join_1008;
      }
      if (_handle__error__result_2218) {
        moonbit_decref_cycle_free(_M0L17error__to__stringS994);
        moonbit_decref_cycle_free(_M0L13handle__startS982);
        _M0L6_2atmpS2170 = 1;
      } else {
        struct moonbit_result_0 _tmp_2219;
        int32_t _handle__error__result_2220;
        #line 614 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\adex\\__generated_driver_for_blackbox_test.mbt"
        _tmp_2219
        = _M0IP016_24default__implP46RiantR8snn__mbt8examples20adex__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples20adex__blackbox__test50MoonBit__Test__Driver__Internal__Async__With__ArgsE(_M0L12async__testsS1015, _M0L8filenameS984, _M0L5indexS986, _M0L13handle__startS982, _M0L14handle__resultS987, _M0L17error__to__stringS994);
        if (_tmp_2219.tag) {
          int32_t const _M0L5_2aokS2173 = _tmp_2219.data.ok;
          _handle__error__result_2220 = _M0L5_2aokS2173;
        } else {
          void* const _M0L6_2aerrS2174 = _tmp_2219.data.err;
          moonbit_decref_cycle_free(_M0L17error__to__stringS994);
          moonbit_decref_cycle_free(_M0L13handle__startS982);
          _M0L11_2atry__errS1009 = _M0L6_2aerrS2174;
          goto join_1008;
        }
        if (_handle__error__result_2220) {
          moonbit_decref_cycle_free(_M0L17error__to__stringS994);
          moonbit_decref_cycle_free(_M0L13handle__startS982);
          _M0L6_2atmpS2170 = 1;
        } else {
          struct moonbit_result_0 _tmp_2221;
          #line 617 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\adex\\__generated_driver_for_blackbox_test.mbt"
          _tmp_2221
          = _M0IP016_24default__implP46RiantR8snn__mbt8examples20adex__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples20adex__blackbox__test50MoonBit__Test__Driver__Internal__With__Bench__ArgsE(_M0L12async__testsS1015, _M0L8filenameS984, _M0L5indexS986, _M0L13handle__startS982, _M0L14handle__resultS987, _M0L17error__to__stringS994);
          moonbit_decref_cycle_free(_M0L13handle__startS982);
          moonbit_decref_cycle_free(_M0L17error__to__stringS994);
          if (_tmp_2221.tag) {
            int32_t const _M0L5_2aokS2171 = _tmp_2221.data.ok;
            _M0L6_2atmpS2170 = _M0L5_2aokS2171;
          } else {
            void* const _M0L6_2aerrS2172 = _tmp_2221.data.err;
            _M0L11_2atry__errS1009 = _M0L6_2aerrS2172;
            goto join_1008;
          }
        }
      }
    }
  }
  if (!_M0L6_2atmpS2170) {
    void* _M0L123RiantR_2fsnn__mbt_2fexamples_2fadex__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTestS2181 =
      (void*)moonbit_malloc(sizeof(struct _M0DTPC15error5Error123RiantR_2fsnn__mbt_2fexamples_2fadex__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest));
    Moonbit_object_header(_M0L123RiantR_2fsnn__mbt_2fexamples_2fadex__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTestS2181)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 6, 1);
    ((struct _M0DTPC15error5Error123RiantR_2fsnn__mbt_2fexamples_2fadex__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest*)_M0L123RiantR_2fsnn__mbt_2fexamples_2fadex__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTestS2181)->$0
    = (moonbit_string_t)moonbit_string_literal_0.data;
    _M0L11_2atry__errS1009
    = _M0L123RiantR_2fsnn__mbt_2fexamples_2fadex__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTestS2181;
    goto join_1008;
  } else {
    moonbit_decref_cycle_free(_M0L14handle__resultS987);
  }
  goto joinlet_2212;
  join_1008:;
  _M0L3errS1010 = _M0L11_2atry__errS1009;
  _M0L36_2aMoonBitTestDriverInternalSkipTestS1013
  = (struct _M0DTPC15error5Error123RiantR_2fsnn__mbt_2fexamples_2fadex__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest*)_M0L3errS1010;
  _M0L7_2anameS1014 = _M0L36_2aMoonBitTestDriverInternalSkipTestS1013->$0;
  _M0L6_2acntS2204
  = Moonbit_rc_count(Moonbit_object_header(_M0L36_2aMoonBitTestDriverInternalSkipTestS1013));
  if (_M0L6_2acntS2204 > 1) {
    int32_t _M0L11_2anew__cntS2205 = _M0L6_2acntS2204 - 1;
    Moonbit_set_rc_count(Moonbit_object_header(_M0L36_2aMoonBitTestDriverInternalSkipTestS1013), _M0L11_2anew__cntS2205);
    moonbit_incref_cycle_free(_M0L7_2anameS1014);
  } else if (_M0L6_2acntS2204 == 1) {
    #line 624 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\adex\\__generated_driver_for_blackbox_test.mbt"
    moonbit_free(_M0L36_2aMoonBitTestDriverInternalSkipTestS1013);
  }
  _M0L4nameS1012 = _M0L7_2anameS1014;
  goto join_1011;
  goto joinlet_2222;
  join_1011:;
  #line 625 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\adex\\__generated_driver_for_blackbox_test.mbt"
  _M0FP46RiantR8snn__mbt8examples20adex__blackbox__test44moonbit__test__driver__internal__do__executeN14handle__resultS987(_M0L14handle__resultS987, _M0L4nameS1012, (moonbit_string_t)moonbit_string_literal_1.data, 1);
  moonbit_decref_cycle_free(_M0L14handle__resultS987);
  moonbit_decref_cycle_free(_M0L4nameS1012);
  joinlet_2222:;
  joinlet_2212:;
  return 0;
}

moonbit_string_t _M0FP46RiantR8snn__mbt8examples20adex__blackbox__test44moonbit__test__driver__internal__do__executeN17error__to__stringS994(
  struct _M0TWRPC15error5ErrorEs* _M0L6_2aenvS2169,
  void* _M0L3errS995
) {
  void* _M0L1eS997;
  moonbit_string_t _M0L1eS999;
  moonbit_string_t _result_2225;
  #line 594 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\adex\\__generated_driver_for_blackbox_test.mbt"
  switch (Moonbit_object_tag(_M0L3errS995)) {
    case 0: {
      struct _M0DTPC15error5Error48moonbitlang_2fcore_2fbuiltin_2eFailure_2eFailure* _M0L10_2aFailureS1000 =
        (struct _M0DTPC15error5Error48moonbitlang_2fcore_2fbuiltin_2eFailure_2eFailure*)_M0L3errS995;
      moonbit_string_t _M0L4_2aeS1001 = _M0L10_2aFailureS1000->$0;
      moonbit_incref_cycle_free(_M0L4_2aeS1001);
      _M0L1eS999 = _M0L4_2aeS1001;
      goto join_998;
      break;
    }
    
    case 2: {
      struct _M0DTPC15error5Error58moonbitlang_2fcore_2fbuiltin_2eInspectError_2eInspectError* _M0L15_2aInspectErrorS1002 =
        (struct _M0DTPC15error5Error58moonbitlang_2fcore_2fbuiltin_2eInspectError_2eInspectError*)_M0L3errS995;
      moonbit_string_t _M0L4_2aeS1003 = _M0L15_2aInspectErrorS1002->$0;
      moonbit_incref_cycle_free(_M0L4_2aeS1003);
      _M0L1eS999 = _M0L4_2aeS1003;
      goto join_998;
      break;
    }
    
    case 3: {
      struct _M0DTPC15error5Error60moonbitlang_2fcore_2fbuiltin_2eSnapshotError_2eSnapshotError* _M0L16_2aSnapshotErrorS1004 =
        (struct _M0DTPC15error5Error60moonbitlang_2fcore_2fbuiltin_2eSnapshotError_2eSnapshotError*)_M0L3errS995;
      moonbit_string_t _M0L4_2aeS1005 = _M0L16_2aSnapshotErrorS1004->$0;
      moonbit_incref_cycle_free(_M0L4_2aeS1005);
      _M0L1eS999 = _M0L4_2aeS1005;
      goto join_998;
      break;
    }
    
    case 4: {
      struct _M0DTPC15error5Error121RiantR_2fsnn__mbt_2fexamples_2fadex__blackbox__test_2eMoonBitTestDriverInternalJsError_2eMoonBitTestDriverInternalJsError* _M0L35_2aMoonBitTestDriverInternalJsErrorS1006 =
        (struct _M0DTPC15error5Error121RiantR_2fsnn__mbt_2fexamples_2fadex__blackbox__test_2eMoonBitTestDriverInternalJsError_2eMoonBitTestDriverInternalJsError*)_M0L3errS995;
      moonbit_string_t _M0L4_2aeS1007 =
        _M0L35_2aMoonBitTestDriverInternalJsErrorS1006->$0;
      moonbit_incref_cycle_free(_M0L4_2aeS1007);
      _M0L1eS999 = _M0L4_2aeS1007;
      goto join_998;
      break;
    }
    default: {
      moonbit_incref_cycle_free(_M0L3errS995);
      _M0L1eS997 = _M0L3errS995;
      goto join_996;
      break;
    }
  }
  join_998:;
  return _M0L1eS999;
  join_996:;
  #line 600 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\adex\\__generated_driver_for_blackbox_test.mbt"
  _result_2225 = _M0FP15Error10to__string(_M0L1eS997);
  moonbit_decref_cycle_free(_M0L1eS997);
  return _result_2225;
}

int32_t _M0FP46RiantR8snn__mbt8examples20adex__blackbox__test44moonbit__test__driver__internal__do__executeN14handle__resultS987(
  struct _M0TWssbEu* _M0L6_2aenvS2166,
  moonbit_string_t _M0L10__testnameS988,
  moonbit_string_t _M0L7messageS989,
  int32_t _M0L7skippedS990
) {
  struct _M0R124_24RiantR_2fsnn__mbt_2fexamples_2fadex__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c987* _M0L14_2acasted__envS2167;
  moonbit_string_t _M0L8filenameS984;
  int32_t _M0L5indexS986;
  moonbit_string_t _M0L10file__nameS991;
  moonbit_string_t _M0L7messageS992;
  struct _M0TPB13StringBuilder* _M0L18_2astring__builderS993;
  moonbit_string_t _M0L6_2atmpS2168;
  #line 579 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\adex\\__generated_driver_for_blackbox_test.mbt"
  _M0L14_2acasted__envS2167
  = (struct _M0R124_24RiantR_2fsnn__mbt_2fexamples_2fadex__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c987*)_M0L6_2aenvS2166;
  _M0L8filenameS984 = _M0L14_2acasted__envS2167->$1;
  _M0L5indexS986 = _M0L14_2acasted__envS2167->$0;
  if (!_M0L7skippedS990 || 0) {
    
  }
  #line 585 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\adex\\__generated_driver_for_blackbox_test.mbt"
  _M0L10file__nameS991
  = _M0MPC16string6String14escape_2einner(_M0L8filenameS984, 1);
  #line 586 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\adex\\__generated_driver_for_blackbox_test.mbt"
  _M0L7messageS992
  = _M0MPC16string6String14escape_2einner(_M0L7messageS989, 1);
  #line 587 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\adex\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE((moonbit_string_t)moonbit_string_literal_2.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\adex\\__generated_driver_for_blackbox_test.mbt"
  _M0L18_2astring__builderS993
  = _M0MPB13StringBuilder21StringBuilder_2einner(45);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\adex\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS993, (moonbit_string_t)moonbit_string_literal_3.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\adex\\__generated_driver_for_blackbox_test.mbt"
  _M0MPB13StringBuilder13write__objectGsE(_M0L18_2astring__builderS993, _M0L10file__nameS991);
  moonbit_decref_cycle_free(_M0L10file__nameS991);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\adex\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS993, (moonbit_string_t)moonbit_string_literal_4.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\adex\\__generated_driver_for_blackbox_test.mbt"
  _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS993, _M0L5indexS986);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\adex\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS993, (moonbit_string_t)moonbit_string_literal_5.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\adex\\__generated_driver_for_blackbox_test.mbt"
  _M0MPB13StringBuilder13write__objectGsE(_M0L18_2astring__builderS993, _M0L7messageS992);
  moonbit_decref_cycle_free(_M0L7messageS992);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\adex\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS993, (moonbit_string_t)moonbit_string_literal_6.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\adex\\__generated_driver_for_blackbox_test.mbt"
  _M0L6_2atmpS2168
  = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS993);
  moonbit_decref_cycle_free(_M0L18_2astring__builderS993);
  #line 588 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\adex\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE(_M0L6_2atmpS2168);
  moonbit_decref_cycle_free(_M0L6_2atmpS2168);
  #line 591 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\adex\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE((moonbit_string_t)moonbit_string_literal_7.data);
  return 0;
}

int32_t _M0FP46RiantR8snn__mbt8examples20adex__blackbox__test44moonbit__test__driver__internal__do__executeN13handle__startS982(
  struct _M0TWEu* _M0L6_2aenvS2163
) {
  struct _M0R123_24RiantR_2fsnn__mbt_2fexamples_2fadex__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c982* _M0L14_2acasted__envS2164;
  moonbit_string_t _M0L8filenameS984;
  int32_t _M0L5indexS986;
  moonbit_string_t _M0L10file__nameS983;
  struct _M0TPB13StringBuilder* _M0L18_2astring__builderS985;
  moonbit_string_t _M0L6_2atmpS2165;
  #line 570 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\adex\\__generated_driver_for_blackbox_test.mbt"
  _M0L14_2acasted__envS2164
  = (struct _M0R123_24RiantR_2fsnn__mbt_2fexamples_2fadex__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c982*)_M0L6_2aenvS2163;
  _M0L8filenameS984 = _M0L14_2acasted__envS2164->$1;
  _M0L5indexS986 = _M0L14_2acasted__envS2164->$0;
  #line 571 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\adex\\__generated_driver_for_blackbox_test.mbt"
  _M0L10file__nameS983
  = _M0MPC16string6String14escape_2einner(_M0L8filenameS984, 1);
  #line 572 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\adex\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE((moonbit_string_t)moonbit_string_literal_2.data);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\adex\\__generated_driver_for_blackbox_test.mbt"
  _M0L18_2astring__builderS985
  = _M0MPB13StringBuilder21StringBuilder_2einner(33);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\adex\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS985, (moonbit_string_t)moonbit_string_literal_8.data);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\adex\\__generated_driver_for_blackbox_test.mbt"
  _M0MPB13StringBuilder13write__objectGsE(_M0L18_2astring__builderS985, _M0L10file__nameS983);
  moonbit_decref_cycle_free(_M0L10file__nameS983);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\adex\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS985, (moonbit_string_t)moonbit_string_literal_4.data);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\adex\\__generated_driver_for_blackbox_test.mbt"
  _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS985, _M0L5indexS986);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\adex\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS985, (moonbit_string_t)moonbit_string_literal_6.data);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\adex\\__generated_driver_for_blackbox_test.mbt"
  _M0L6_2atmpS2165
  = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS985);
  moonbit_decref_cycle_free(_M0L18_2astring__builderS985);
  #line 573 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\adex\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE(_M0L6_2atmpS2165);
  moonbit_decref_cycle_free(_M0L6_2atmpS2165);
  #line 576 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\adex\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE((moonbit_string_t)moonbit_string_literal_7.data);
  return 0;
}

struct _M0TPB5ArrayGUsiEE* _M0FP46RiantR8snn__mbt8examples20adex__blackbox__test52moonbit__test__driver__internal__native__parse__args(
  
) {
  int32_t _M0L45moonbit__test__driver__internal__parse__int__S952;
  int32_t _M0L51moonbit__test__driver__internal__split__mbt__stringS959;
  struct _M0TUsiE** _M0L6_2atmpS2162;
  struct _M0TPB5ArrayGUsiEE* _M0L16file__and__indexS966;
  moonbit_string_t* _M0L9cli__argsS967;
  moonbit_string_t _M0L6_2atmpS2161;
  moonbit_string_t _M0L6_2atmpS2160;
  struct _M0TPB5ArrayGsE* _M0L10test__argsS968;
  int32_t _M0L7_2abindS969;
  moonbit_string_t* _M0L7_2abindS970;
  int32_t _M0L6_2acntS2206;
  int32_t _M0L2__S971;
  #line 295 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\adex\\__generated_driver_for_blackbox_test.mbt"
  _M0L45moonbit__test__driver__internal__parse__int__S952 = 0;
  _M0L51moonbit__test__driver__internal__split__mbt__stringS959 = 0;
  _M0L6_2atmpS2162 = (struct _M0TUsiE**)moonbit_empty_ref_array;
  _M0L16file__and__indexS966
  = (struct _M0TPB5ArrayGUsiEE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGUsiEE));
  Moonbit_object_header(_M0L16file__and__indexS966)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 9, 0);
  _M0L16file__and__indexS966->$0 = _M0L6_2atmpS2162;
  _M0L16file__and__indexS966->$1 = 0;
  #line 329 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\adex\\__generated_driver_for_blackbox_test.mbt"
  _M0L9cli__argsS967
  = _M0FP46RiantR8snn__mbt8examples20adex__blackbox__test52moonbit__test__driver__internal__get__cli__args__ffi();
  if (1 < 0 || 1 >= Moonbit_array_length(_M0L9cli__argsS967)) {
    #line 331 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\adex\\__generated_driver_for_blackbox_test.mbt"
    moonbit_panic();
  }
  _M0L6_2atmpS2161 = (moonbit_string_t)_M0L9cli__argsS967[1];
  moonbit_incref_cycle_free(_M0L6_2atmpS2161);
  moonbit_decref_cycle_free(_M0L9cli__argsS967);
  #line 331 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\adex\\__generated_driver_for_blackbox_test.mbt"
  _M0L6_2atmpS2160
  = _M0MP46RiantR8snn__mbt8examples20adex__blackbox__test33MoonBitTestDriverInternalOsString10to__string(_M0L6_2atmpS2161);
  moonbit_decref_cycle_free(_M0L6_2atmpS2161);
  #line 330 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\adex\\__generated_driver_for_blackbox_test.mbt"
  _M0L10test__argsS968
  = _M0FP46RiantR8snn__mbt8examples20adex__blackbox__test52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS959(_M0L51moonbit__test__driver__internal__split__mbt__stringS959, _M0L6_2atmpS2160, 47);
  moonbit_decref_cycle_free(_M0L6_2atmpS2160);
  _M0L7_2abindS969 = _M0L10test__argsS968->$1;
  _M0L7_2abindS970 = _M0L10test__argsS968->$0;
  _M0L6_2acntS2206
  = Moonbit_rc_count(Moonbit_object_header(_M0L10test__argsS968));
  if (_M0L6_2acntS2206 > 1) {
    int32_t _M0L11_2anew__cntS2207 = _M0L6_2acntS2206 - 1;
    Moonbit_set_rc_count(Moonbit_object_header(_M0L10test__argsS968), _M0L11_2anew__cntS2207);
    moonbit_incref_cycle_free(_M0L7_2abindS970);
  } else if (_M0L6_2acntS2206 == 1) {
    #line 330 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\adex\\__generated_driver_for_blackbox_test.mbt"
    moonbit_free(_M0L10test__argsS968);
  }
  _M0L2__S971 = 0;
  while (1) {
    if (_M0L2__S971 < _M0L7_2abindS969) {
      moonbit_string_t _M0L3argS972 =
        (moonbit_string_t)_M0L7_2abindS970[_M0L2__S971];
      struct _M0TPB5ArrayGsE* _M0L16file__and__rangeS973;
      moonbit_string_t _M0L4fileS974;
      moonbit_string_t _M0L5rangeS975;
      struct _M0TPB5ArrayGsE* _M0L15start__and__endS976;
      moonbit_string_t _M0L6_2atmpS2158;
      int32_t _M0L5startS977;
      moonbit_string_t _M0L6_2atmpS2157;
      int32_t _M0L3endS978;
      int32_t _M0L1iS979;
      int32_t _M0L6_2atmpS2159;
      moonbit_incref_cycle_free(_M0L3argS972);
      #line 335 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\adex\\__generated_driver_for_blackbox_test.mbt"
      _M0L16file__and__rangeS973
      = _M0FP46RiantR8snn__mbt8examples20adex__blackbox__test52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS959(_M0L51moonbit__test__driver__internal__split__mbt__stringS959, _M0L3argS972, 58);
      moonbit_decref_cycle_free(_M0L3argS972);
      #line 336 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\adex\\__generated_driver_for_blackbox_test.mbt"
      _M0L4fileS974
      = _M0MPC15array5Array2atGsE(_M0L16file__and__rangeS973, 0);
      #line 337 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\adex\\__generated_driver_for_blackbox_test.mbt"
      _M0L5rangeS975
      = _M0MPC15array5Array2atGsE(_M0L16file__and__rangeS973, 1);
      moonbit_decref_cycle_free(_M0L16file__and__rangeS973);
      #line 338 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\adex\\__generated_driver_for_blackbox_test.mbt"
      _M0L15start__and__endS976
      = _M0FP46RiantR8snn__mbt8examples20adex__blackbox__test52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS959(_M0L51moonbit__test__driver__internal__split__mbt__stringS959, _M0L5rangeS975, 45);
      moonbit_decref_cycle_free(_M0L5rangeS975);
      #line 341 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\adex\\__generated_driver_for_blackbox_test.mbt"
      _M0L6_2atmpS2158
      = _M0MPC15array5Array2atGsE(_M0L15start__and__endS976, 0);
      #line 341 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\adex\\__generated_driver_for_blackbox_test.mbt"
      _M0L5startS977
      = _M0FP46RiantR8snn__mbt8examples20adex__blackbox__test52moonbit__test__driver__internal__native__parse__argsN45moonbit__test__driver__internal__parse__int__S952(_M0L45moonbit__test__driver__internal__parse__int__S952, _M0L6_2atmpS2158);
      moonbit_decref_cycle_free(_M0L6_2atmpS2158);
      #line 342 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\adex\\__generated_driver_for_blackbox_test.mbt"
      _M0L6_2atmpS2157
      = _M0MPC15array5Array2atGsE(_M0L15start__and__endS976, 1);
      moonbit_decref_cycle_free(_M0L15start__and__endS976);
      #line 342 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\adex\\__generated_driver_for_blackbox_test.mbt"
      _M0L3endS978
      = _M0FP46RiantR8snn__mbt8examples20adex__blackbox__test52moonbit__test__driver__internal__native__parse__argsN45moonbit__test__driver__internal__parse__int__S952(_M0L45moonbit__test__driver__internal__parse__int__S952, _M0L6_2atmpS2157);
      moonbit_decref_cycle_free(_M0L6_2atmpS2157);
      _M0L1iS979 = _M0L5startS977;
      while (1) {
        if (_M0L1iS979 < _M0L3endS978) {
          struct _M0TUsiE* _M0L8_2atupleS2155;
          int32_t _M0L6_2atmpS2156;
          moonbit_incref_cycle_free(_M0L4fileS974);
          _M0L8_2atupleS2155
          = (struct _M0TUsiE*)moonbit_malloc(sizeof(struct _M0TUsiE));
          Moonbit_object_header(_M0L8_2atupleS2155)->meta
          = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 12, 0);
          _M0L8_2atupleS2155->$0 = _M0L4fileS974;
          _M0L8_2atupleS2155->$1 = _M0L1iS979;
          #line 344 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\adex\\__generated_driver_for_blackbox_test.mbt"
          _M0MPC15array5Array4pushGUsiEE(_M0L16file__and__indexS966, _M0L8_2atupleS2155);
          _M0L6_2atmpS2156 = _M0L1iS979 + 1;
          _M0L1iS979 = _M0L6_2atmpS2156;
          continue;
        } else {
          moonbit_decref_cycle_free(_M0L4fileS974);
        }
        break;
      }
      _M0L6_2atmpS2159 = _M0L2__S971 + 1;
      _M0L2__S971 = _M0L6_2atmpS2159;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L7_2abindS970);
    }
    break;
  }
  return _M0L16file__and__indexS966;
}

struct _M0TPB5ArrayGsE* _M0FP46RiantR8snn__mbt8examples20adex__blackbox__test52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS959(
  int32_t _M0L6_2aenvS2136,
  moonbit_string_t _M0L1sS960,
  int32_t _M0L3sepS961
) {
  moonbit_string_t* _M0L6_2atmpS2154;
  struct _M0TPB5ArrayGsE* _M0L3resS962;
  struct _M0TPB8MutLocalGiE* _M0L1iS963;
  struct _M0TPB8MutLocalGiE* _M0L5startS964;
  int32_t _M0L3valS2149;
  int32_t _M0L6_2atmpS2150;
  #line 308 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\adex\\__generated_driver_for_blackbox_test.mbt"
  _M0L6_2atmpS2154 = (moonbit_string_t*)moonbit_empty_ref_array;
  _M0L3resS962
  = (struct _M0TPB5ArrayGsE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGsE));
  Moonbit_object_header(_M0L3resS962)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 15, 0);
  _M0L3resS962->$0 = _M0L6_2atmpS2154;
  _M0L3resS962->$1 = 0;
  _M0L1iS963
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1iS963)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1iS963->$0 = 0;
  _M0L5startS964
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L5startS964)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L5startS964->$0 = 0;
  while (1) {
    int32_t _M0L3valS2137 = _M0L1iS963->$0;
    int32_t _M0L6_2atmpS2138 = Moonbit_array_length(_M0L1sS960);
    if (_M0L3valS2137 < _M0L6_2atmpS2138) {
      int32_t _M0L3valS2141 = _M0L1iS963->$0;
      int32_t _M0L6_2atmpS2140;
      int32_t _M0L6_2atmpS2139;
      int32_t _M0L3valS2148;
      int32_t _M0L6_2atmpS2147;
      if (
        _M0L3valS2141 < 0
        || _M0L3valS2141 >= Moonbit_array_length(_M0L1sS960)
      ) {
        #line 316 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\adex\\__generated_driver_for_blackbox_test.mbt"
        moonbit_panic();
      }
      _M0L6_2atmpS2140 = _M0L1sS960[_M0L3valS2141];
      _M0L6_2atmpS2139 = _M0L6_2atmpS2140;
      if (_M0L6_2atmpS2139 == _M0L3sepS961) {
        int32_t _M0L3valS2143 = _M0L5startS964->$0;
        int32_t _M0L3valS2144 = _M0L1iS963->$0;
        moonbit_string_t _M0L6_2atmpS2142;
        int32_t _M0L3valS2146;
        int32_t _M0L6_2atmpS2145;
        #line 317 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\adex\\__generated_driver_for_blackbox_test.mbt"
        _M0L6_2atmpS2142
        = _M0MPC16string6String17unsafe__substring(_M0L1sS960, _M0L3valS2143, _M0L3valS2144);
        #line 317 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\adex\\__generated_driver_for_blackbox_test.mbt"
        _M0MPC15array5Array4pushGsE(_M0L3resS962, _M0L6_2atmpS2142);
        _M0L3valS2146 = _M0L1iS963->$0;
        _M0L6_2atmpS2145 = _M0L3valS2146 + 1;
        _M0L5startS964->$0 = _M0L6_2atmpS2145;
      }
      _M0L3valS2148 = _M0L1iS963->$0;
      _M0L6_2atmpS2147 = _M0L3valS2148 + 1;
      _M0L1iS963->$0 = _M0L6_2atmpS2147;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1iS963);
    }
    break;
  }
  _M0L3valS2149 = _M0L5startS964->$0;
  _M0L6_2atmpS2150 = Moonbit_array_length(_M0L1sS960);
  if (_M0L3valS2149 < _M0L6_2atmpS2150) {
    int32_t _M0L3valS2152 = _M0L5startS964->$0;
    int32_t _M0L6_2atmpS2153;
    moonbit_string_t _M0L6_2atmpS2151;
    moonbit_decref_cycle_free(_M0L5startS964);
    _M0L6_2atmpS2153 = Moonbit_array_length(_M0L1sS960);
    #line 323 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\adex\\__generated_driver_for_blackbox_test.mbt"
    _M0L6_2atmpS2151
    = _M0MPC16string6String17unsafe__substring(_M0L1sS960, _M0L3valS2152, _M0L6_2atmpS2153);
    #line 323 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\adex\\__generated_driver_for_blackbox_test.mbt"
    _M0MPC15array5Array4pushGsE(_M0L3resS962, _M0L6_2atmpS2151);
  } else {
    moonbit_decref_cycle_free(_M0L5startS964);
  }
  return _M0L3resS962;
}

int32_t _M0FP46RiantR8snn__mbt8examples20adex__blackbox__test52moonbit__test__driver__internal__native__parse__argsN45moonbit__test__driver__internal__parse__int__S952(
  int32_t _M0L6_2aenvS2129,
  moonbit_string_t _M0L1sS953
) {
  struct _M0TPB8MutLocalGiE* _M0L3resS954;
  int32_t _M0L3lenS955;
  int32_t _M0L7_2abindS956;
  int32_t _M0L1iS957;
  int32_t _result_2230;
  #line 299 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\adex\\__generated_driver_for_blackbox_test.mbt"
  _M0L3resS954
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L3resS954)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L3resS954->$0 = 0;
  _M0L3lenS955 = Moonbit_array_length(_M0L1sS953);
  _M0L7_2abindS956 = 0;
  _M0L1iS957 = _M0L7_2abindS956;
  while (1) {
    if (_M0L1iS957 < _M0L3lenS955) {
      int32_t _M0L3valS2134 = _M0L3resS954->$0;
      int32_t _M0L6_2atmpS2131 = _M0L3valS2134 * 10;
      int32_t _M0L6_2atmpS2133;
      int32_t _M0L6_2atmpS2132;
      int32_t _M0L6_2atmpS2130;
      int32_t _M0L6_2atmpS2135;
      if (_M0L1iS957 < 0 || _M0L1iS957 >= Moonbit_array_length(_M0L1sS953)) {
        #line 303 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\adex\\__generated_driver_for_blackbox_test.mbt"
        moonbit_panic();
      }
      _M0L6_2atmpS2133 = _M0L1sS953[_M0L1iS957];
      _M0L6_2atmpS2132 = _M0L6_2atmpS2133 - 48;
      _M0L6_2atmpS2130 = _M0L6_2atmpS2131 + _M0L6_2atmpS2132;
      _M0L3resS954->$0 = _M0L6_2atmpS2130;
      _M0L6_2atmpS2135 = _M0L1iS957 + 1;
      _M0L1iS957 = _M0L6_2atmpS2135;
      continue;
    }
    break;
  }
  _result_2230 = _M0L3resS954->$0;
  moonbit_decref_cycle_free(_M0L3resS954);
  return _result_2230;
}

moonbit_string_t _M0MP46RiantR8snn__mbt8examples20adex__blackbox__test33MoonBitTestDriverInternalOsString10to__string(
  moonbit_string_t _M0L4selfS951
) {
  #line 164 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\adex\\__generated_driver_for_blackbox_test.mbt"
  moonbit_incref_cycle_free(_M0L4selfS951);
  return _M0L4selfS951;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples20adex__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples20adex__blackbox__test41MoonBit__Test__Driver__Internal__No__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S921,
  moonbit_string_t _M0L12_2adiscard__S922,
  int32_t _M0L12_2adiscard__S923,
  struct _M0TWEu* _M0L12_2adiscard__S924,
  struct _M0TWssbEu* _M0L12_2adiscard__S925,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S926
) {
  struct moonbit_result_0 _result_2231;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\adex\\__generated_driver_for_blackbox_test.mbt"
  _result_2231.tag = 1;
  _result_2231.data.ok = 0;
  return _result_2231;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples20adex__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples20adex__blackbox__test43MoonBit__Test__Driver__Internal__With__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S927,
  moonbit_string_t _M0L12_2adiscard__S928,
  int32_t _M0L12_2adiscard__S929,
  struct _M0TWEu* _M0L12_2adiscard__S930,
  struct _M0TWssbEu* _M0L12_2adiscard__S931,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S932
) {
  struct moonbit_result_0 _result_2232;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\adex\\__generated_driver_for_blackbox_test.mbt"
  _result_2232.tag = 1;
  _result_2232.data.ok = 0;
  return _result_2232;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples20adex__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples20adex__blackbox__test48MoonBit__Test__Driver__Internal__Async__No__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S933,
  moonbit_string_t _M0L12_2adiscard__S934,
  int32_t _M0L12_2adiscard__S935,
  struct _M0TWEu* _M0L12_2adiscard__S936,
  struct _M0TWssbEu* _M0L12_2adiscard__S937,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S938
) {
  struct moonbit_result_0 _result_2233;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\adex\\__generated_driver_for_blackbox_test.mbt"
  _result_2233.tag = 1;
  _result_2233.data.ok = 0;
  return _result_2233;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples20adex__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples20adex__blackbox__test50MoonBit__Test__Driver__Internal__Async__With__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S939,
  moonbit_string_t _M0L12_2adiscard__S940,
  int32_t _M0L12_2adiscard__S941,
  struct _M0TWEu* _M0L12_2adiscard__S942,
  struct _M0TWssbEu* _M0L12_2adiscard__S943,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S944
) {
  struct moonbit_result_0 _result_2234;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\adex\\__generated_driver_for_blackbox_test.mbt"
  _result_2234.tag = 1;
  _result_2234.data.ok = 0;
  return _result_2234;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples20adex__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples20adex__blackbox__test50MoonBit__Test__Driver__Internal__With__Bench__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S945,
  moonbit_string_t _M0L12_2adiscard__S946,
  int32_t _M0L12_2adiscard__S947,
  struct _M0TWEu* _M0L12_2adiscard__S948,
  struct _M0TWssbEu* _M0L12_2adiscard__S949,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S950
) {
  struct moonbit_result_0 _result_2235;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\adex\\__generated_driver_for_blackbox_test.mbt"
  _result_2235.tag = 1;
  _result_2235.data.ok = 0;
  return _result_2235;
}

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples20adex__blackbox__test28MoonBit__Async__Test__Driver20is__being__cancelledGRP46RiantR8snn__mbt8examples20adex__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  
) {
  #line 17 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\adex\\__generated_driver_for_blackbox_test.mbt"
  return 0;
}

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples20adex__blackbox__test28MoonBit__Async__Test__Driver17run__async__testsGRP46RiantR8snn__mbt8examples20adex__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S920
) {
  #line 12 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\adex\\__generated_driver_for_blackbox_test.mbt"
  return 0;
}

struct _M0TP26RiantR8snn__mbt4AdEx* _M0MP26RiantR8snn__mbt4AdEx3new(
  int32_t _M0L1nS912,
  struct _M0TP26RiantR8snn__mbt13AdExParameter* _M0L5paramS913,
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS914
) {
  struct _M0TP26RiantR8snn__mbt13AdExPostSpike* _M0L6_2atmpS2128;
  struct _M0TP26RiantR8snn__mbt4AdEx* _result_2236;
  #line 141 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
  #line 142 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
  _M0L6_2atmpS2128 = _M0MP26RiantR8snn__mbt13AdExPostSpike3new();
  #line 142 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
  _result_2236
  = _M0MP26RiantR8snn__mbt4AdEx16new__with__spike(_M0L1nS912, _M0L5paramS913, _M0L6_2atmpS2128, _M0L3rngS914);
  moonbit_decref_cycle_free(_M0L6_2atmpS2128);
  return _result_2236;
}

struct _M0TP26RiantR8snn__mbt4AdEx* _M0MP26RiantR8snn__mbt4AdEx16new__with__spike(
  int32_t _M0L1nS890,
  struct _M0TP26RiantR8snn__mbt13AdExParameter* _M0L5paramS892,
  struct _M0TP26RiantR8snn__mbt13AdExPostSpike* _M0L5spikeS911,
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS895
) {
  struct _M0TPB5ArrayGfE* _M0L1vS889;
  float _M0L2vtS2126;
  float _M0L2vrS2127;
  float _M0L6spreadS891;
  int32_t _M0L7_2abindS893;
  int32_t _M0L1kS894;
  struct _M0TPB5ArrayGfE* _M0L1wS897;
  struct _M0TPB5ArrayGbE* _M0L4fireS898;
  float _M0L2vtS2125;
  struct _M0TPB5ArrayGfE* _M0L9thresholdS899;
  struct _M0TPB5ArrayGiE* _M0L4tabsS900;
  struct _M0TPB5ArrayGfE* _M0L1iS901;
  struct _M0TPB5ArrayGfE* _M0L9syn__currS902;
  struct _M0TPB5ArrayGfE* _M0L2geS903;
  struct _M0TPB5ArrayGfE* _M0L2giS904;
  struct _M0TPB5ArrayGfE* _M0L2heS905;
  struct _M0TPB5ArrayGfE* _M0L2hiS906;
  struct _M0TPB5ArrayGfE* _M0L3gluS907;
  struct _M0TPB5ArrayGfE* _M0L4gabaS908;
  struct _M0TPB5ArrayGfE* _M0L7gsyn__eS909;
  struct _M0TPB5ArrayGfE* _M0L7gsyn__iS910;
  struct _M0TP26RiantR8snn__mbt4AdEx* _block_2238;
  #line 147 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
  #line 153 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
  _M0L1vS889 = _M0MPC15array5Array4makeGfE(_M0L1nS890, 0x0p+0f);
  _M0L2vtS2126 = _M0L5paramS892->$2;
  _M0L2vrS2127 = _M0L5paramS892->$3;
  _M0L6spreadS891 = _M0L2vtS2126 - _M0L2vrS2127;
  _M0L7_2abindS893 = 0;
  _M0L1kS894 = _M0L7_2abindS893;
  while (1) {
    if (_M0L1kS894 < _M0L1nS890) {
      float _M0L2vrS2121 = _M0L5paramS892->$3;
      float _M0L6_2atmpS2123;
      float _M0L6_2atmpS2122;
      float _M0L6_2atmpS2120;
      int32_t _M0L6_2atmpS2124;
      #line 156 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS2123 = _M0FP26RiantR8snn__mbt9next__f32(_M0L3rngS895);
      _M0L6_2atmpS2122 = _M0L6_2atmpS2123 * _M0L6spreadS891;
      _M0L6_2atmpS2120 = _M0L2vrS2121 + _M0L6_2atmpS2122;
      #line 156 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS889, _M0L1kS894, _M0L6_2atmpS2120);
      _M0L6_2atmpS2124 = _M0L1kS894 + 1;
      _M0L1kS894 = _M0L6_2atmpS2124;
      continue;
    }
    break;
  }
  #line 158 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
  _M0L1wS897 = _M0MPC15array5Array4makeGfE(_M0L1nS890, 0x0p+0f);
  #line 159 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
  _M0L4fireS898 = _M0MPC15array5Array4makeGbE(_M0L1nS890, 0);
  _M0L2vtS2125 = _M0L5paramS892->$2;
  #line 160 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
  _M0L9thresholdS899 = _M0MPC15array5Array4makeGfE(_M0L1nS890, _M0L2vtS2125);
  #line 162 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
  _M0L4tabsS900 = _M0MPC15array5Array4makeGiE(_M0L1nS890, 1);
  #line 163 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
  _M0L1iS901 = _M0MPC15array5Array4makeGfE(_M0L1nS890, 0x0p+0f);
  #line 164 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
  _M0L9syn__currS902 = _M0MPC15array5Array4makeGfE(_M0L1nS890, 0x0p+0f);
  #line 165 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
  _M0L2geS903 = _M0MPC15array5Array4makeGfE(_M0L1nS890, 0x0p+0f);
  #line 166 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
  _M0L2giS904 = _M0MPC15array5Array4makeGfE(_M0L1nS890, 0x0p+0f);
  #line 167 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
  _M0L2heS905 = _M0MPC15array5Array4makeGfE(_M0L1nS890, 0x0p+0f);
  #line 168 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
  _M0L2hiS906 = _M0MPC15array5Array4makeGfE(_M0L1nS890, 0x0p+0f);
  #line 169 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
  _M0L3gluS907 = _M0MPC15array5Array4makeGfE(_M0L1nS890, 0x0p+0f);
  #line 170 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
  _M0L4gabaS908 = _M0MPC15array5Array4makeGfE(_M0L1nS890, 0x0p+0f);
  #line 171 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
  _M0L7gsyn__eS909 = _M0MPC15array5Array4makeGfE(_M0L1nS890, 0x1p+0f);
  #line 172 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
  _M0L7gsyn__iS910 = _M0MPC15array5Array4makeGfE(_M0L1nS890, 0x1p+0f);
  moonbit_incref_cycle_free(_M0L5paramS892);
  moonbit_incref_cycle_free(_M0L5spikeS911);
  _block_2238
  = (struct _M0TP26RiantR8snn__mbt4AdEx*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt4AdEx));
  Moonbit_object_header(_block_2238)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 18, 0);
  _block_2238->$0 = _M0L5paramS892;
  _block_2238->$1 = _M0L5spikeS911;
  _block_2238->$2 = _M0L1nS890;
  _block_2238->$3 = _M0L1vS889;
  _block_2238->$4 = _M0L1wS897;
  _block_2238->$5 = _M0L4fireS898;
  _block_2238->$6 = _M0L9thresholdS899;
  _block_2238->$7 = _M0L4tabsS900;
  _block_2238->$8 = _M0L1iS901;
  _block_2238->$9 = _M0L9syn__currS902;
  _block_2238->$10 = _M0L2geS903;
  _block_2238->$11 = _M0L2giS904;
  _block_2238->$12 = _M0L2heS905;
  _block_2238->$13 = _M0L2hiS906;
  _block_2238->$14 = _M0L3gluS907;
  _block_2238->$15 = _M0L4gabaS908;
  _block_2238->$16 = _M0L7gsyn__eS909;
  _block_2238->$17 = _M0L7gsyn__iS910;
  _block_2238->$18 = 0x0p+0f;
  _block_2238->$19 = -0x1.2cp+6f;
  _block_2238->$20 = 0x1p+0f;
  _block_2238->$21 = 0x1.8p+2f;
  _block_2238->$22 = 0x1p-1f;
  _block_2238->$23 = 0x1p+1f;
  return _block_2238;
}

struct _M0TP26RiantR8snn__mbt13AdExParameter* _M0MP26RiantR8snn__mbt13AdExParameter3new(
  
) {
  float _M0L1cS885;
  float _M0L2glS886;
  float _M0L2tmS887;
  float _M0L1rS888;
  struct _M0TP26RiantR8snn__mbt13AdExParameter* _block_2239;
  #line 30 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
  _M0L1cS885 = 0x1.19p+8f;
  _M0L2glS886 = 0x1.4p+5f;
  _M0L2tmS887 = 0x1.19p+8f / 0x1.4p+5f;
  _M0L1rS888 = 0x1p+0f / 0x1.4p+5f;
  _block_2239
  = (struct _M0TP26RiantR8snn__mbt13AdExParameter*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt13AdExParameter));
  Moonbit_object_header(_block_2239)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_2239->$0 = _M0L1cS885;
  _block_2239->$1 = _M0L2glS886;
  _block_2239->$2 = -0x1.9p+5f;
  _block_2239->$3 = -0x1.1a66666666666p+6f;
  _block_2239->$4 = -0x1.1a66666666666p+6f;
  _block_2239->$5 = _M0L2tmS887;
  _block_2239->$6 = _M0L1rS888;
  _block_2239->$7 = 0x1p+1f;
  _block_2239->$8 = 0x1.2p+7f;
  _block_2239->$9 = 0x1p+2f;
  _block_2239->$10 = 0x1.42p+6f;
  return _block_2239;
}

struct _M0TP26RiantR8snn__mbt13AdExPostSpike* _M0MP26RiantR8snn__mbt13AdExPostSpike3new(
  
) {
  struct _M0TP26RiantR8snn__mbt13AdExPostSpike* _block_2240;
  #line 94 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
  _block_2240
  = (struct _M0TP26RiantR8snn__mbt13AdExPostSpike*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt13AdExPostSpike));
  Moonbit_object_header(_block_2240)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_2240->$0 = 0x0p+0f;
  _block_2240->$1 = 0x1.4p+3f;
  _block_2240->$2 = 0x1.4p+3f;
  _block_2240->$3 = 0x1p+0f;
  _block_2240->$4 = 0x1p+0f;
  return _block_2240;
}

struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0MP26RiantR8snn__mbt7Xoshiro7default(
  
) {
  #line 56 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  #line 57 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  return _M0MP26RiantR8snn__mbt7Xoshiro3new(0ull);
}

int32_t _M0FP26RiantR8snn__mbt14adex__sim__for(
  struct _M0TP26RiantR8snn__mbt9AdExModel* _M0L5modelS876,
  float _M0L8durationS874
) {
  float _M0L2dtS871;
  struct _M0TP26RiantR8snn__mbt4Time* _M0L4timeS872;
  float _M0L6_2atmpS2119;
  int32_t _M0L5stepsS873;
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt11MonitorAdExE* _M0L7_2abindS875;
  int32_t _M0L7_2abindS877;
  struct _M0TP26RiantR8snn__mbt11MonitorAdEx** _M0L7_2abindS878;
  int32_t _M0L2__S879;
  int32_t _M0L7_2abindS882;
  int32_t _M0L2__S883;
  #line 261 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
  _M0L2dtS871 = 0x1p-3f;
  #line 263 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
  _M0L4timeS872 = _M0MP26RiantR8snn__mbt4Time3new();
  #line 264 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
  _M0FP26RiantR8snn__mbt7set__dt(_M0L4timeS872, _M0L2dtS871);
  _M0L6_2atmpS2119 = _M0L8durationS874 / _M0L2dtS871;
  #line 265 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
  _M0L5stepsS873 = _M0MPC15float5Float7to__int(_M0L6_2atmpS2119);
  _M0L7_2abindS875 = _M0L5modelS876->$2;
  _M0L7_2abindS877 = _M0L7_2abindS875->$1;
  _M0L7_2abindS878 = _M0L7_2abindS875->$0;
  moonbit_incref_cycle_free(_M0L7_2abindS878);
  _M0L2__S879 = 0;
  while (1) {
    if (_M0L2__S879 < _M0L7_2abindS877) {
      struct _M0TP26RiantR8snn__mbt11MonitorAdEx* _M0L1mS880 =
        (struct _M0TP26RiantR8snn__mbt11MonitorAdEx*)_M0L7_2abindS878[
          _M0L2__S879
        ];
      int32_t _M0L6_2atmpS2117;
      moonbit_incref_cycle_free(_M0L1mS880);
      #line 267 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
      _M0FP26RiantR8snn__mbt17record__one__adex(_M0L1mS880, 0x0p+0f);
      moonbit_decref_cycle_free(_M0L1mS880);
      _M0L6_2atmpS2117 = _M0L2__S879 + 1;
      _M0L2__S879 = _M0L6_2atmpS2117;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L7_2abindS878);
    }
    break;
  }
  _M0L7_2abindS882 = 0;
  _M0L2__S883 = _M0L7_2abindS882;
  while (1) {
    if (_M0L2__S883 < _M0L5stepsS873) {
      int32_t _M0L6_2atmpS2118;
      #line 270 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
      _M0FP26RiantR8snn__mbt17step__adex__model(_M0L5modelS876, _M0L4timeS872);
      _M0L6_2atmpS2118 = _M0L2__S883 + 1;
      _M0L2__S883 = _M0L6_2atmpS2118;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L4timeS872);
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt17step__adex__model(
  struct _M0TP26RiantR8snn__mbt9AdExModel* _M0L5modelS851,
  struct _M0TP26RiantR8snn__mbt4Time* _M0L4timeS858
) {
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt18SpikingSynapseAdExE* _M0L7_2abindS850;
  int32_t _M0L7_2abindS852;
  struct _M0TP26RiantR8snn__mbt18SpikingSynapseAdEx** _M0L7_2abindS853;
  int32_t _M0L2__S854;
  float _M0L2dtS857;
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt4AdExE* _M0L7_2abindS859;
  int32_t _M0L7_2abindS860;
  struct _M0TP26RiantR8snn__mbt4AdEx** _M0L7_2abindS861;
  int32_t _M0L2__S862;
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt11MonitorAdExE* _M0L7_2abindS865;
  int32_t _M0L7_2abindS866;
  struct _M0TP26RiantR8snn__mbt11MonitorAdEx** _M0L7_2abindS867;
  int32_t _M0L2__S868;
  #line 244 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
  _M0L7_2abindS850 = _M0L5modelS851->$1;
  _M0L7_2abindS852 = _M0L7_2abindS850->$1;
  _M0L7_2abindS853 = _M0L7_2abindS850->$0;
  moonbit_incref_cycle_free(_M0L7_2abindS853);
  _M0L2__S854 = 0;
  while (1) {
    if (_M0L2__S854 < _M0L7_2abindS852) {
      struct _M0TP26RiantR8snn__mbt18SpikingSynapseAdEx* _M0L1cS855 =
        (struct _M0TP26RiantR8snn__mbt18SpikingSynapseAdEx*)_M0L7_2abindS853[
          _M0L2__S854
        ];
      int32_t _M0L6_2atmpS2112;
      moonbit_incref_cycle_free(_M0L1cS855);
      #line 246 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
      _M0FP26RiantR8snn__mbt22forward__adex__synapse(_M0L1cS855);
      moonbit_decref_cycle_free(_M0L1cS855);
      _M0L6_2atmpS2112 = _M0L2__S854 + 1;
      _M0L2__S854 = _M0L6_2atmpS2112;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L7_2abindS853);
    }
    break;
  }
  _M0L2dtS857 = _M0L4timeS858->$2;
  _M0L7_2abindS859 = _M0L5modelS851->$0;
  _M0L7_2abindS860 = _M0L7_2abindS859->$1;
  _M0L7_2abindS861 = _M0L7_2abindS859->$0;
  moonbit_incref_cycle_free(_M0L7_2abindS861);
  _M0L2__S862 = 0;
  while (1) {
    if (_M0L2__S862 < _M0L7_2abindS860) {
      struct _M0TP26RiantR8snn__mbt4AdEx* _M0L1pS863 =
        (struct _M0TP26RiantR8snn__mbt4AdEx*)_M0L7_2abindS861[_M0L2__S862];
      int32_t _M0L6_2atmpS2113;
      moonbit_incref_cycle_free(_M0L1pS863);
      #line 250 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
      _M0FP26RiantR8snn__mbt20adex__step__synapses(_M0L1pS863, _M0L2dtS857);
      #line 251 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
      _M0FP26RiantR8snn__mbt23adex__synaptic__current(_M0L1pS863);
      #line 252 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
      _M0FP26RiantR8snn__mbt10step__adex(_M0L1pS863, _M0L2dtS857);
      moonbit_decref_cycle_free(_M0L1pS863);
      _M0L6_2atmpS2113 = _M0L2__S862 + 1;
      _M0L2__S862 = _M0L6_2atmpS2113;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L7_2abindS861);
    }
    break;
  }
  _M0L7_2abindS865 = _M0L5modelS851->$2;
  _M0L7_2abindS866 = _M0L7_2abindS865->$1;
  _M0L7_2abindS867 = _M0L7_2abindS865->$0;
  moonbit_incref_cycle_free(_M0L7_2abindS867);
  _M0L2__S868 = 0;
  while (1) {
    if (_M0L2__S868 < _M0L7_2abindS866) {
      struct _M0TP26RiantR8snn__mbt11MonitorAdEx* _M0L1mS869 =
        (struct _M0TP26RiantR8snn__mbt11MonitorAdEx*)_M0L7_2abindS867[
          _M0L2__S868
        ];
      struct _M0TPB5ArrayGfE* _M0L1tS2115 = _M0L4timeS858->$0;
      float _M0L6_2atmpS2114;
      int32_t _M0L6_2atmpS2116;
      moonbit_incref_cycle_free(_M0L1mS869);
      #line 255 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
      _M0L6_2atmpS2114 = _M0MPC15array5Array2atGfE(_M0L1tS2115, 0);
      #line 255 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
      _M0FP26RiantR8snn__mbt17record__one__adex(_M0L1mS869, _M0L6_2atmpS2114);
      moonbit_decref_cycle_free(_M0L1mS869);
      _M0L6_2atmpS2116 = _M0L2__S868 + 1;
      _M0L2__S868 = _M0L6_2atmpS2116;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L7_2abindS867);
    }
    break;
  }
  #line 257 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
  _M0FP26RiantR8snn__mbt12update__time(_M0L4timeS858, _M0L2dtS857);
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt10step__adex(
  struct _M0TP26RiantR8snn__mbt4AdEx* _M0L1pS829,
  float _M0L2dtS844
) {
  int32_t _M0L1nS828;
  struct _M0TP26RiantR8snn__mbt13AdExParameter* _M0L3p__S830;
  float _M0L2tmS831;
  float _M0L2vtS832;
  float _M0L2vrS833;
  float _M0L2elS834;
  float _M0L1rS835;
  float _M0L9dt__slopeS836;
  float _M0L2twS837;
  float _M0L1aS838;
  float _M0L1bS839;
  struct _M0TP26RiantR8snn__mbt13AdExPostSpike* _M0L5spikeS2111;
  float _M0L2atS840;
  struct _M0TP26RiantR8snn__mbt13AdExPostSpike* _M0L5spikeS2110;
  float _M0L6tau__aS841;
  struct _M0TP26RiantR8snn__mbt13AdExPostSpike* _M0L5spikeS2109;
  float _M0L11tabs__constS842;
  float _M0L6_2atmpS2108;
  int32_t _M0L11tabs__stepsS843;
  int32_t _M0L7_2abindS845;
  int32_t _M0L1iS846;
  #line 182 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
  _M0L1nS828 = _M0L1pS829->$2;
  _M0L3p__S830 = _M0L1pS829->$0;
  _M0L2tmS831 = _M0L3p__S830->$5;
  _M0L2vtS832 = _M0L3p__S830->$2;
  _M0L2vrS833 = _M0L3p__S830->$3;
  _M0L2elS834 = _M0L3p__S830->$4;
  _M0L1rS835 = _M0L3p__S830->$6;
  _M0L9dt__slopeS836 = _M0L3p__S830->$7;
  _M0L2twS837 = _M0L3p__S830->$8;
  _M0L1aS838 = _M0L3p__S830->$9;
  _M0L1bS839 = _M0L3p__S830->$10;
  _M0L5spikeS2111 = _M0L1pS829->$1;
  _M0L2atS840 = _M0L5spikeS2111->$0;
  _M0L5spikeS2110 = _M0L1pS829->$1;
  _M0L6tau__aS841 = _M0L5spikeS2110->$1;
  _M0L5spikeS2109 = _M0L1pS829->$1;
  _M0L11tabs__constS842 = _M0L5spikeS2109->$3;
  _M0L6_2atmpS2108 = _M0L11tabs__constS842 / _M0L2dtS844;
  #line 197 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
  _M0L11tabs__stepsS843 = _M0MPC15float5Float7to__int(_M0L6_2atmpS2108);
  _M0L7_2abindS845 = 0;
  _M0L1iS846 = _M0L7_2abindS845;
  while (1) {
    if (_M0L1iS846 < _M0L1nS828) {
      struct _M0TPB5ArrayGfE* _M0L1vS2021 = _M0L1pS829->$3;
      struct _M0TPB5ArrayGbE* _M0L4fireS2023 = _M0L1pS829->$5;
      float _M0L6_2atmpS2022;
      struct _M0TPB5ArrayGbE* _M0L4fireS2025;
      struct _M0TPB5ArrayGiE* _M0L4tabsS2026;
      struct _M0TPB5ArrayGiE* _M0L4tabsS2029;
      int32_t _M0L6_2atmpS2028;
      int32_t _M0L6_2atmpS2027;
      struct _M0TPB5ArrayGiE* _M0L4tabsS2031;
      int32_t _M0L6_2atmpS2030;
      struct _M0TPB5ArrayGfE* _M0L1wS2032;
      struct _M0TPB5ArrayGfE* _M0L1wS2044;
      float _M0L6_2atmpS2034;
      struct _M0TPB5ArrayGfE* _M0L1vS2043;
      float _M0L6_2atmpS2042;
      float _M0L6_2atmpS2041;
      float _M0L6_2atmpS2038;
      struct _M0TPB5ArrayGfE* _M0L1wS2040;
      float _M0L6_2atmpS2039;
      float _M0L6_2atmpS2037;
      float _M0L6_2atmpS2036;
      float _M0L6_2atmpS2035;
      float _M0L6_2atmpS2033;
      float _M0L9exp__termS849;
      struct _M0TPB5ArrayGfE* _M0L1vS2045;
      struct _M0TPB5ArrayGfE* _M0L1vS2067;
      float _M0L6_2atmpS2047;
      struct _M0TPB5ArrayGfE* _M0L1vS2066;
      float _M0L6_2atmpS2065;
      float _M0L6_2atmpS2064;
      float _M0L6_2atmpS2063;
      float _M0L6_2atmpS2059;
      struct _M0TPB5ArrayGfE* _M0L9syn__currS2062;
      float _M0L6_2atmpS2061;
      float _M0L6_2atmpS2060;
      float _M0L6_2atmpS2055;
      struct _M0TPB5ArrayGfE* _M0L1wS2058;
      float _M0L6_2atmpS2057;
      float _M0L6_2atmpS2056;
      float _M0L6_2atmpS2051;
      struct _M0TPB5ArrayGfE* _M0L1iS2054;
      float _M0L6_2atmpS2053;
      float _M0L6_2atmpS2052;
      float _M0L6_2atmpS2050;
      float _M0L6_2atmpS2049;
      float _M0L6_2atmpS2048;
      float _M0L6_2atmpS2046;
      struct _M0TPB5ArrayGfE* _M0L9thresholdS2068;
      struct _M0TPB5ArrayGfE* _M0L9thresholdS2076;
      float _M0L6_2atmpS2070;
      struct _M0TPB5ArrayGfE* _M0L9thresholdS2075;
      float _M0L6_2atmpS2074;
      float _M0L6_2atmpS2073;
      float _M0L6_2atmpS2072;
      float _M0L6_2atmpS2071;
      float _M0L6_2atmpS2069;
      struct _M0TPB5ArrayGbE* _M0L4fireS2077;
      struct _M0TPB5ArrayGfE* _M0L1vS2080;
      float _M0L6_2atmpS2079;
      int32_t _M0L6_2atmpS2078;
      struct _M0TPB5ArrayGfE* _M0L1vS2081;
      struct _M0TPB5ArrayGbE* _M0L4fireS2083;
      float _M0L6_2atmpS2082;
      struct _M0TPB5ArrayGfE* _M0L1wS2085;
      struct _M0TPB5ArrayGbE* _M0L4fireS2087;
      float _M0L6_2atmpS2086;
      struct _M0TPB5ArrayGfE* _M0L9thresholdS2091;
      struct _M0TPB5ArrayGbE* _M0L4fireS2093;
      float _M0L6_2atmpS2092;
      struct _M0TPB5ArrayGiE* _M0L4tabsS2097;
      struct _M0TPB5ArrayGbE* _M0L4fireS2099;
      int32_t _M0L6_2atmpS2098;
      int32_t _M0L6_2atmpS2020;
      #line 201 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS2023, _M0L1iS846)) {
        _M0L6_2atmpS2022 = _M0L2vrS833;
      } else {
        struct _M0TPB5ArrayGfE* _M0L1vS2024 = _M0L1pS829->$3;
        #line 201 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
        _M0L6_2atmpS2022 = _M0MPC15array5Array2atGfE(_M0L1vS2024, _M0L1iS846);
      }
      #line 201 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS2021, _M0L1iS846, _M0L6_2atmpS2022);
      _M0L4fireS2025 = _M0L1pS829->$5;
      #line 204 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGbE(_M0L4fireS2025, _M0L1iS846, 0);
      _M0L4tabsS2026 = _M0L1pS829->$7;
      _M0L4tabsS2029 = _M0L1pS829->$7;
      #line 205 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS2028
      = _M0MPC15array5Array2atGiE(_M0L4tabsS2029, _M0L1iS846);
      _M0L6_2atmpS2027 = _M0L6_2atmpS2028 - 1;
      #line 205 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGiE(_M0L4tabsS2026, _M0L1iS846, _M0L6_2atmpS2027);
      _M0L4tabsS2031 = _M0L1pS829->$7;
      #line 206 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS2030
      = _M0MPC15array5Array2atGiE(_M0L4tabsS2031, _M0L1iS846);
      if (_M0L6_2atmpS2030 > 0) {
        goto join_847;
      }
      _M0L1wS2032 = _M0L1pS829->$4;
      _M0L1wS2044 = _M0L1pS829->$4;
      #line 211 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS2034 = _M0MPC15array5Array2atGfE(_M0L1wS2044, _M0L1iS846);
      _M0L1vS2043 = _M0L1pS829->$3;
      #line 211 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS2042 = _M0MPC15array5Array2atGfE(_M0L1vS2043, _M0L1iS846);
      _M0L6_2atmpS2041 = _M0L6_2atmpS2042 - _M0L2elS834;
      _M0L6_2atmpS2038 = _M0L1aS838 * _M0L6_2atmpS2041;
      _M0L1wS2040 = _M0L1pS829->$4;
      #line 211 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS2039 = _M0MPC15array5Array2atGfE(_M0L1wS2040, _M0L1iS846);
      _M0L6_2atmpS2037 = _M0L6_2atmpS2038 - _M0L6_2atmpS2039;
      _M0L6_2atmpS2036 = _M0L2dtS844 * _M0L6_2atmpS2037;
      _M0L6_2atmpS2035 = _M0L6_2atmpS2036 / _M0L2twS837;
      _M0L6_2atmpS2033 = _M0L6_2atmpS2034 + _M0L6_2atmpS2035;
      #line 211 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGfE(_M0L1wS2032, _M0L1iS846, _M0L6_2atmpS2033);
      if (_M0L9dt__slopeS836 < 0x0p+0f) {
        _M0L9exp__termS849 = 0x0p+0f;
      } else {
        struct _M0TPB5ArrayGfE* _M0L1vS2107 = _M0L1pS829->$3;
        float _M0L6_2atmpS2104;
        struct _M0TPB5ArrayGfE* _M0L9thresholdS2106;
        float _M0L6_2atmpS2105;
        float _M0L6_2atmpS2103;
        float _M0L6_2atmpS2102;
        float _M0L6_2atmpS2101;
        #line 217 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
        _M0L6_2atmpS2104 = _M0MPC15array5Array2atGfE(_M0L1vS2107, _M0L1iS846);
        _M0L9thresholdS2106 = _M0L1pS829->$6;
        #line 217 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
        _M0L6_2atmpS2105
        = _M0MPC15array5Array2atGfE(_M0L9thresholdS2106, _M0L1iS846);
        _M0L6_2atmpS2103 = _M0L6_2atmpS2104 - _M0L6_2atmpS2105;
        _M0L6_2atmpS2102 = _M0L6_2atmpS2103 / _M0L9dt__slopeS836;
        #line 217 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
        _M0L6_2atmpS2101 = _M0FP26RiantR8snn__mbt4expf(_M0L6_2atmpS2102);
        _M0L9exp__termS849 = _M0L9dt__slopeS836 * _M0L6_2atmpS2101;
      }
      _M0L1vS2045 = _M0L1pS829->$3;
      _M0L1vS2067 = _M0L1pS829->$3;
      #line 219 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS2047 = _M0MPC15array5Array2atGfE(_M0L1vS2067, _M0L1iS846);
      _M0L1vS2066 = _M0L1pS829->$3;
      #line 222 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS2065 = _M0MPC15array5Array2atGfE(_M0L1vS2066, _M0L1iS846);
      _M0L6_2atmpS2064 = _M0L6_2atmpS2065 - _M0L2elS834;
      _M0L6_2atmpS2063 = -_M0L6_2atmpS2064;
      _M0L6_2atmpS2059 = _M0L6_2atmpS2063 + _M0L9exp__termS849;
      _M0L9syn__currS2062 = _M0L1pS829->$9;
      #line 222 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS2061
      = _M0MPC15array5Array2atGfE(_M0L9syn__currS2062, _M0L1iS846);
      _M0L6_2atmpS2060 = _M0L1rS835 * _M0L6_2atmpS2061;
      _M0L6_2atmpS2055 = _M0L6_2atmpS2059 - _M0L6_2atmpS2060;
      _M0L1wS2058 = _M0L1pS829->$4;
      #line 222 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS2057 = _M0MPC15array5Array2atGfE(_M0L1wS2058, _M0L1iS846);
      _M0L6_2atmpS2056 = _M0L1rS835 * _M0L6_2atmpS2057;
      _M0L6_2atmpS2051 = _M0L6_2atmpS2055 - _M0L6_2atmpS2056;
      _M0L1iS2054 = _M0L1pS829->$8;
      #line 223 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS2053 = _M0MPC15array5Array2atGfE(_M0L1iS2054, _M0L1iS846);
      _M0L6_2atmpS2052 = _M0L1rS835 * _M0L6_2atmpS2053;
      _M0L6_2atmpS2050 = _M0L6_2atmpS2051 + _M0L6_2atmpS2052;
      _M0L6_2atmpS2049 = _M0L2dtS844 * _M0L6_2atmpS2050;
      _M0L6_2atmpS2048 = _M0L6_2atmpS2049 / _M0L2tmS831;
      _M0L6_2atmpS2046 = _M0L6_2atmpS2047 + _M0L6_2atmpS2048;
      #line 219 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS2045, _M0L1iS846, _M0L6_2atmpS2046);
      _M0L9thresholdS2068 = _M0L1pS829->$6;
      _M0L9thresholdS2076 = _M0L1pS829->$6;
      #line 227 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS2070
      = _M0MPC15array5Array2atGfE(_M0L9thresholdS2076, _M0L1iS846);
      _M0L9thresholdS2075 = _M0L1pS829->$6;
      #line 227 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS2074
      = _M0MPC15array5Array2atGfE(_M0L9thresholdS2075, _M0L1iS846);
      _M0L6_2atmpS2073 = _M0L2vtS832 - _M0L6_2atmpS2074;
      _M0L6_2atmpS2072 = _M0L2dtS844 * _M0L6_2atmpS2073;
      _M0L6_2atmpS2071 = _M0L6_2atmpS2072 / _M0L6tau__aS841;
      _M0L6_2atmpS2069 = _M0L6_2atmpS2070 + _M0L6_2atmpS2071;
      #line 227 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGfE(_M0L9thresholdS2068, _M0L1iS846, _M0L6_2atmpS2069);
      _M0L4fireS2077 = _M0L1pS829->$5;
      _M0L1vS2080 = _M0L1pS829->$3;
      #line 230 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS2079 = _M0MPC15array5Array2atGfE(_M0L1vS2080, _M0L1iS846);
      _M0L6_2atmpS2078 = _M0L6_2atmpS2079 >= 0x0p+0f;
      #line 230 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGbE(_M0L4fireS2077, _M0L1iS846, _M0L6_2atmpS2078);
      _M0L1vS2081 = _M0L1pS829->$3;
      _M0L4fireS2083 = _M0L1pS829->$5;
      #line 231 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS2083, _M0L1iS846)) {
        _M0L6_2atmpS2082 = 0x1.4p+4f;
      } else {
        struct _M0TPB5ArrayGfE* _M0L1vS2084 = _M0L1pS829->$3;
        #line 231 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
        _M0L6_2atmpS2082 = _M0MPC15array5Array2atGfE(_M0L1vS2084, _M0L1iS846);
      }
      #line 231 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS2081, _M0L1iS846, _M0L6_2atmpS2082);
      _M0L1wS2085 = _M0L1pS829->$4;
      _M0L4fireS2087 = _M0L1pS829->$5;
      #line 232 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS2087, _M0L1iS846)) {
        struct _M0TPB5ArrayGfE* _M0L1wS2089 = _M0L1pS829->$4;
        float _M0L6_2atmpS2088;
        #line 232 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
        _M0L6_2atmpS2088 = _M0MPC15array5Array2atGfE(_M0L1wS2089, _M0L1iS846);
        _M0L6_2atmpS2086 = _M0L6_2atmpS2088 + _M0L1bS839;
      } else {
        struct _M0TPB5ArrayGfE* _M0L1wS2090 = _M0L1pS829->$4;
        #line 232 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
        _M0L6_2atmpS2086 = _M0MPC15array5Array2atGfE(_M0L1wS2090, _M0L1iS846);
      }
      #line 232 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGfE(_M0L1wS2085, _M0L1iS846, _M0L6_2atmpS2086);
      _M0L9thresholdS2091 = _M0L1pS829->$6;
      _M0L4fireS2093 = _M0L1pS829->$5;
      #line 233 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS2093, _M0L1iS846)) {
        struct _M0TPB5ArrayGfE* _M0L9thresholdS2095 = _M0L1pS829->$6;
        float _M0L6_2atmpS2094;
        #line 233 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
        _M0L6_2atmpS2094
        = _M0MPC15array5Array2atGfE(_M0L9thresholdS2095, _M0L1iS846);
        _M0L6_2atmpS2092 = _M0L6_2atmpS2094 + _M0L2atS840;
      } else {
        struct _M0TPB5ArrayGfE* _M0L9thresholdS2096 = _M0L1pS829->$6;
        #line 233 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
        _M0L6_2atmpS2092
        = _M0MPC15array5Array2atGfE(_M0L9thresholdS2096, _M0L1iS846);
      }
      #line 233 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGfE(_M0L9thresholdS2091, _M0L1iS846, _M0L6_2atmpS2092);
      _M0L4tabsS2097 = _M0L1pS829->$7;
      _M0L4fireS2099 = _M0L1pS829->$5;
      #line 234 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS2099, _M0L1iS846)) {
        _M0L6_2atmpS2098 = _M0L11tabs__stepsS843;
      } else {
        struct _M0TPB5ArrayGiE* _M0L4tabsS2100 = _M0L1pS829->$7;
        #line 234 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
        _M0L6_2atmpS2098
        = _M0MPC15array5Array2atGiE(_M0L4tabsS2100, _M0L1iS846);
      }
      #line 234 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGiE(_M0L4tabsS2097, _M0L1iS846, _M0L6_2atmpS2098);
      goto join_847;
      goto joinlet_2247;
      join_847:;
      _M0L6_2atmpS2020 = _M0L1iS846 + 1;
      _M0L1iS846 = _M0L6_2atmpS2020;
      continue;
      joinlet_2247:;
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt23adex__synaptic__current(
  struct _M0TP26RiantR8snn__mbt4AdEx* _M0L1pS824
) {
  int32_t _M0L1nS823;
  int32_t _M0L7_2abindS825;
  int32_t _M0L1iS826;
  #line 259 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
  _M0L1nS823 = _M0L1pS824->$2;
  _M0L7_2abindS825 = 0;
  _M0L1iS826 = _M0L7_2abindS825;
  while (1) {
    if (_M0L1iS826 < _M0L1nS823) {
      struct _M0TPB5ArrayGfE* _M0L9syn__currS1997 = _M0L1pS824->$9;
      struct _M0TPB5ArrayGfE* _M0L2geS2018 = _M0L1pS824->$10;
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
      #line 262 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS2013 = _M0MPC15array5Array2atGfE(_M0L2geS2018, _M0L1iS826);
      _M0L1vS2017 = _M0L1pS824->$3;
      #line 262 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS2015 = _M0MPC15array5Array2atGfE(_M0L1vS2017, _M0L1iS826);
      _M0L4e__eS2016 = _M0L1pS824->$18;
      _M0L6_2atmpS2014 = _M0L6_2atmpS2015 - _M0L4e__eS2016;
      _M0L6_2atmpS2010 = _M0L6_2atmpS2013 * _M0L6_2atmpS2014;
      _M0L7gsyn__eS2012 = _M0L1pS824->$16;
      #line 262 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS2011
      = _M0MPC15array5Array2atGfE(_M0L7gsyn__eS2012, _M0L1iS826);
      _M0L6_2atmpS1999 = _M0L6_2atmpS2010 * _M0L6_2atmpS2011;
      _M0L2giS2009 = _M0L1pS824->$11;
      #line 263 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS2004 = _M0MPC15array5Array2atGfE(_M0L2giS2009, _M0L1iS826);
      _M0L1vS2008 = _M0L1pS824->$3;
      #line 263 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS2006 = _M0MPC15array5Array2atGfE(_M0L1vS2008, _M0L1iS826);
      _M0L4e__iS2007 = _M0L1pS824->$19;
      _M0L6_2atmpS2005 = _M0L6_2atmpS2006 - _M0L4e__iS2007;
      _M0L6_2atmpS2001 = _M0L6_2atmpS2004 * _M0L6_2atmpS2005;
      _M0L7gsyn__iS2003 = _M0L1pS824->$17;
      #line 263 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS2002
      = _M0MPC15array5Array2atGfE(_M0L7gsyn__iS2003, _M0L1iS826);
      _M0L6_2atmpS2000 = _M0L6_2atmpS2001 * _M0L6_2atmpS2002;
      _M0L6_2atmpS1998 = _M0L6_2atmpS1999 + _M0L6_2atmpS2000;
      #line 262 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGfE(_M0L9syn__currS1997, _M0L1iS826, _M0L6_2atmpS1998);
      _M0L6_2atmpS2019 = _M0L1iS826 + 1;
      _M0L1iS826 = _M0L6_2atmpS2019;
      continue;
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt20adex__step__synapses(
  struct _M0TP26RiantR8snn__mbt4AdEx* _M0L1pS815,
  float _M0L2dtS818
) {
  int32_t _M0L1nS814;
  int32_t _M0L7_2abindS816;
  int32_t _M0L1iS817;
  int32_t _M0L7_2abindS820;
  int32_t _M0L1iS821;
  #line 241 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
  _M0L1nS814 = _M0L1pS815->$2;
  _M0L7_2abindS816 = 0;
  _M0L1iS817 = _M0L7_2abindS816;
  while (1) {
    if (_M0L1iS817 < _M0L1nS814) {
      struct _M0TPB5ArrayGfE* _M0L2heS1935 = _M0L1pS815->$12;
      struct _M0TPB5ArrayGfE* _M0L2heS1940 = _M0L1pS815->$12;
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
      #line 244 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS1937 = _M0MPC15array5Array2atGfE(_M0L2heS1940, _M0L1iS817);
      _M0L3gluS1939 = _M0L1pS815->$14;
      #line 244 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS1938 = _M0MPC15array5Array2atGfE(_M0L3gluS1939, _M0L1iS817);
      _M0L6_2atmpS1936 = _M0L6_2atmpS1937 + _M0L6_2atmpS1938;
      #line 244 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGfE(_M0L2heS1935, _M0L1iS817, _M0L6_2atmpS1936);
      _M0L2hiS1941 = _M0L1pS815->$13;
      _M0L2hiS1946 = _M0L1pS815->$13;
      #line 245 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS1943 = _M0MPC15array5Array2atGfE(_M0L2hiS1946, _M0L1iS817);
      _M0L4gabaS1945 = _M0L1pS815->$15;
      #line 245 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS1944
      = _M0MPC15array5Array2atGfE(_M0L4gabaS1945, _M0L1iS817);
      _M0L6_2atmpS1942 = _M0L6_2atmpS1943 + _M0L6_2atmpS1944;
      #line 245 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGfE(_M0L2hiS1941, _M0L1iS817, _M0L6_2atmpS1942);
      _M0L2geS1947 = _M0L1pS815->$10;
      _M0L2geS1959 = _M0L1pS815->$10;
      #line 246 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS1949 = _M0MPC15array5Array2atGfE(_M0L2geS1959, _M0L1iS817);
      _M0L2geS1958 = _M0L1pS815->$10;
      #line 246 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS1957 = _M0MPC15array5Array2atGfE(_M0L2geS1958, _M0L1iS817);
      _M0L6_2atmpS1955 = -_M0L6_2atmpS1957;
      _M0L3tdeS1956 = _M0L1pS815->$21;
      _M0L6_2atmpS1952 = _M0L6_2atmpS1955 / _M0L3tdeS1956;
      _M0L2heS1954 = _M0L1pS815->$12;
      #line 246 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS1953 = _M0MPC15array5Array2atGfE(_M0L2heS1954, _M0L1iS817);
      _M0L6_2atmpS1951 = _M0L6_2atmpS1952 + _M0L6_2atmpS1953;
      _M0L6_2atmpS1950 = _M0L2dtS818 * _M0L6_2atmpS1951;
      _M0L6_2atmpS1948 = _M0L6_2atmpS1949 + _M0L6_2atmpS1950;
      #line 246 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGfE(_M0L2geS1947, _M0L1iS817, _M0L6_2atmpS1948);
      _M0L2heS1960 = _M0L1pS815->$12;
      _M0L2heS1969 = _M0L1pS815->$12;
      #line 247 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS1962 = _M0MPC15array5Array2atGfE(_M0L2heS1969, _M0L1iS817);
      _M0L2heS1968 = _M0L1pS815->$12;
      #line 247 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS1967 = _M0MPC15array5Array2atGfE(_M0L2heS1968, _M0L1iS817);
      _M0L6_2atmpS1965 = -_M0L6_2atmpS1967;
      _M0L3treS1966 = _M0L1pS815->$20;
      _M0L6_2atmpS1964 = _M0L6_2atmpS1965 / _M0L3treS1966;
      _M0L6_2atmpS1963 = _M0L2dtS818 * _M0L6_2atmpS1964;
      _M0L6_2atmpS1961 = _M0L6_2atmpS1962 + _M0L6_2atmpS1963;
      #line 247 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGfE(_M0L2heS1960, _M0L1iS817, _M0L6_2atmpS1961);
      _M0L2giS1970 = _M0L1pS815->$11;
      _M0L2giS1982 = _M0L1pS815->$11;
      #line 248 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS1972 = _M0MPC15array5Array2atGfE(_M0L2giS1982, _M0L1iS817);
      _M0L2giS1981 = _M0L1pS815->$11;
      #line 248 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS1980 = _M0MPC15array5Array2atGfE(_M0L2giS1981, _M0L1iS817);
      _M0L6_2atmpS1978 = -_M0L6_2atmpS1980;
      _M0L3tdiS1979 = _M0L1pS815->$23;
      _M0L6_2atmpS1975 = _M0L6_2atmpS1978 / _M0L3tdiS1979;
      _M0L2hiS1977 = _M0L1pS815->$13;
      #line 248 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS1976 = _M0MPC15array5Array2atGfE(_M0L2hiS1977, _M0L1iS817);
      _M0L6_2atmpS1974 = _M0L6_2atmpS1975 + _M0L6_2atmpS1976;
      _M0L6_2atmpS1973 = _M0L2dtS818 * _M0L6_2atmpS1974;
      _M0L6_2atmpS1971 = _M0L6_2atmpS1972 + _M0L6_2atmpS1973;
      #line 248 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGfE(_M0L2giS1970, _M0L1iS817, _M0L6_2atmpS1971);
      _M0L2hiS1983 = _M0L1pS815->$13;
      _M0L2hiS1992 = _M0L1pS815->$13;
      #line 249 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS1985 = _M0MPC15array5Array2atGfE(_M0L2hiS1992, _M0L1iS817);
      _M0L2hiS1991 = _M0L1pS815->$13;
      #line 249 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS1990 = _M0MPC15array5Array2atGfE(_M0L2hiS1991, _M0L1iS817);
      _M0L6_2atmpS1988 = -_M0L6_2atmpS1990;
      _M0L3triS1989 = _M0L1pS815->$22;
      _M0L6_2atmpS1987 = _M0L6_2atmpS1988 / _M0L3triS1989;
      _M0L6_2atmpS1986 = _M0L2dtS818 * _M0L6_2atmpS1987;
      _M0L6_2atmpS1984 = _M0L6_2atmpS1985 + _M0L6_2atmpS1986;
      #line 249 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGfE(_M0L2hiS1983, _M0L1iS817, _M0L6_2atmpS1984);
      _M0L6_2atmpS1993 = _M0L1iS817 + 1;
      _M0L1iS817 = _M0L6_2atmpS1993;
      continue;
    }
    break;
  }
  _M0L7_2abindS820 = 0;
  _M0L1iS821 = _M0L7_2abindS820;
  while (1) {
    if (_M0L1iS821 < _M0L1nS814) {
      struct _M0TPB5ArrayGfE* _M0L3gluS1994 = _M0L1pS815->$14;
      struct _M0TPB5ArrayGfE* _M0L4gabaS1995;
      int32_t _M0L6_2atmpS1996;
      #line 252 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGfE(_M0L3gluS1994, _M0L1iS821, 0x0p+0f);
      _M0L4gabaS1995 = _M0L1pS815->$15;
      #line 253 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGfE(_M0L4gabaS1995, _M0L1iS821, 0x0p+0f);
      _M0L6_2atmpS1996 = _M0L1iS821 + 1;
      _M0L1iS821 = _M0L6_2atmpS1996;
      continue;
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt22forward__adex__synapse(
  struct _M0TP26RiantR8snn__mbt18SpikingSynapseAdEx* _M0L1cS813
) {
  moonbit_string_t _M0L3symS1932;
  int32_t _if__result_2251;
  struct _M0TPB5ArrayGfE* _M0L6targetS812;
  struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS1928;
  struct _M0TP26RiantR8snn__mbt4AdEx* _M0L3preS1930;
  struct _M0TPB5ArrayGbE* _M0L4fireS1929;
  #line 234 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
  _M0L3symS1932 = _M0L1cS813->$2;
  #line 235 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
  if (
    _M0L3symS1932 == (moonbit_string_t)moonbit_string_literal_9.data
    || Moonbit_array_length(_M0L3symS1932)
       == Moonbit_array_length((moonbit_string_t)moonbit_string_literal_9.data)
       && 0
          == memcmp(_M0L3symS1932, (moonbit_string_t)moonbit_string_literal_9.data, Moonbit_array_length(_M0L3symS1932) * 2)
  ) {
    _if__result_2251 = 1;
  } else {
    moonbit_string_t _M0L3symS1931 = _M0L1cS813->$2;
    #line 235 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
    _if__result_2251
    = _M0L3symS1931 == (moonbit_string_t)moonbit_string_literal_10.data
      || Moonbit_array_length(_M0L3symS1931)
         == Moonbit_array_length((moonbit_string_t)moonbit_string_literal_10.data)
         && 0
            == memcmp(_M0L3symS1931, (moonbit_string_t)moonbit_string_literal_10.data, Moonbit_array_length(_M0L3symS1931) * 2);
  }
  if (_if__result_2251) {
    struct _M0TP26RiantR8snn__mbt4AdEx* _M0L4postS1933 = _M0L1cS813->$1;
    struct _M0TPB5ArrayGfE* _M0L8_2afieldS2183 = _M0L4postS1933->$14;
    moonbit_incref_cycle_free(_M0L8_2afieldS2183);
    _M0L6targetS812 = _M0L8_2afieldS2183;
  } else {
    struct _M0TP26RiantR8snn__mbt4AdEx* _M0L4postS1934 = _M0L1cS813->$1;
    struct _M0TPB5ArrayGfE* _M0L8_2afieldS2184 = _M0L4postS1934->$15;
    moonbit_incref_cycle_free(_M0L8_2afieldS2184);
    _M0L6targetS812 = _M0L8_2afieldS2184;
  }
  _M0L6matrixS1928 = _M0L1cS813->$3;
  _M0L3preS1930 = _M0L1cS813->$0;
  _M0L4fireS1929 = _M0L3preS1930->$5;
  #line 240 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
  _M0MP26RiantR8snn__mbt15SparseMatrixCSR7forward(_M0L6matrixS1928, _M0L4fireS1929, _M0L6targetS812);
  moonbit_decref_cycle_free(_M0L6targetS812);
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt17record__one__adex(
  struct _M0TP26RiantR8snn__mbt11MonitorAdEx* _M0L1mS810,
  float _M0L1tS811
) {
  moonbit_string_t _M0L3symS1916;
  float _M0L1vS809;
  struct _M0TPB5ArrayGfE* _M0L4dataS1914;
  struct _M0TPB5ArrayGfE* _M0L5timesS1915;
  #line 158 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
  _M0L3symS1916 = _M0L1mS810->$1;
  #line 159 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
  if (
    _M0L3symS1916 == (moonbit_string_t)moonbit_string_literal_11.data
    || Moonbit_array_length(_M0L3symS1916)
       == Moonbit_array_length((moonbit_string_t)moonbit_string_literal_11.data)
       && 0
          == memcmp(_M0L3symS1916, (moonbit_string_t)moonbit_string_literal_11.data, Moonbit_array_length(_M0L3symS1916) * 2)
  ) {
    struct _M0TP26RiantR8snn__mbt4AdEx* _M0L3popS1919 = _M0L1mS810->$0;
    struct _M0TPB5ArrayGfE* _M0L1vS1917 = _M0L3popS1919->$3;
    int32_t _M0L6neuronS1918 = _M0L1mS810->$4;
    #line 160 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
    _M0L1vS809 = _M0MPC15array5Array2atGfE(_M0L1vS1917, _M0L6neuronS1918);
  } else {
    moonbit_string_t _M0L3symS1920 = _M0L1mS810->$1;
    #line 161 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
    if (
      _M0L3symS1920 == (moonbit_string_t)moonbit_string_literal_12.data
      || Moonbit_array_length(_M0L3symS1920)
         == Moonbit_array_length((moonbit_string_t)moonbit_string_literal_12.data)
         && 0
            == memcmp(_M0L3symS1920, (moonbit_string_t)moonbit_string_literal_12.data, Moonbit_array_length(_M0L3symS1920) * 2)
    ) {
      struct _M0TP26RiantR8snn__mbt4AdEx* _M0L3popS1923 = _M0L1mS810->$0;
      struct _M0TPB5ArrayGbE* _M0L4fireS1921 = _M0L3popS1923->$5;
      int32_t _M0L6neuronS1922 = _M0L1mS810->$4;
      #line 162 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS1921, _M0L6neuronS1922)) {
        _M0L1vS809 = 0x1p+0f;
      } else {
        _M0L1vS809 = 0x0p+0f;
      }
    } else {
      moonbit_string_t _M0L3symS1924 = _M0L1mS810->$1;
      #line 163 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
      if (
        _M0L3symS1924 == (moonbit_string_t)moonbit_string_literal_13.data
        || Moonbit_array_length(_M0L3symS1924)
           == Moonbit_array_length((moonbit_string_t)moonbit_string_literal_13.data)
           && 0
              == memcmp(_M0L3symS1924, (moonbit_string_t)moonbit_string_literal_13.data, Moonbit_array_length(_M0L3symS1924) * 2)
      ) {
        struct _M0TP26RiantR8snn__mbt4AdEx* _M0L3popS1927 = _M0L1mS810->$0;
        struct _M0TPB5ArrayGfE* _M0L1wS1925 = _M0L3popS1927->$4;
        int32_t _M0L6neuronS1926 = _M0L1mS810->$4;
        #line 164 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
        _M0L1vS809 = _M0MPC15array5Array2atGfE(_M0L1wS1925, _M0L6neuronS1926);
      } else {
        _M0L1vS809 = 0x0p+0f;
      }
    }
  }
  _M0L4dataS1914 = _M0L1mS810->$2;
  #line 168 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
  _M0MPC15array5Array4pushGfE(_M0L4dataS1914, _M0L1vS809);
  _M0L5timesS1915 = _M0L1mS810->$3;
  #line 169 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
  _M0MPC15array5Array4pushGfE(_M0L5timesS1915, _M0L1tS811);
  return 0;
}

struct _M0TP26RiantR8snn__mbt11MonitorAdEx* _M0MP26RiantR8snn__mbt11MonitorAdEx6new__v(
  struct _M0TP26RiantR8snn__mbt4AdEx* _M0L3popS807,
  int32_t _M0L6neuronS808
) {
  float* _M0L6_2atmpS1913;
  struct _M0TPB5ArrayGfE* _M0L6_2atmpS1910;
  float* _M0L6_2atmpS1912;
  struct _M0TPB5ArrayGfE* _M0L6_2atmpS1911;
  struct _M0TP26RiantR8snn__mbt11MonitorAdEx* _block_2252;
  #line 153 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
  _M0L6_2atmpS1913 = moonbit_empty_float_array;
  _M0L6_2atmpS1910
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_M0L6_2atmpS1910)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 37, 0);
  _M0L6_2atmpS1910->$0 = _M0L6_2atmpS1913;
  _M0L6_2atmpS1910->$1 = 0;
  _M0L6_2atmpS1912 = moonbit_empty_float_array;
  _M0L6_2atmpS1911
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_M0L6_2atmpS1911)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 37, 0);
  _M0L6_2atmpS1911->$0 = _M0L6_2atmpS1912;
  _M0L6_2atmpS1911->$1 = 0;
  moonbit_incref_cycle_free(_M0L3popS807);
  _block_2252
  = (struct _M0TP26RiantR8snn__mbt11MonitorAdEx*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt11MonitorAdEx));
  Moonbit_object_header(_block_2252)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 40, 0);
  _block_2252->$0 = _M0L3popS807;
  _block_2252->$1 = (moonbit_string_t)moonbit_string_literal_11.data;
  _block_2252->$2 = _M0L6_2atmpS1910;
  _block_2252->$3 = _M0L6_2atmpS1911;
  _block_2252->$4 = _M0L6neuronS808;
  return _block_2252;
}

int32_t _M0MP26RiantR8snn__mbt15SparseMatrixCSR7forward(
  struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L1mS795,
  struct _M0TPB5ArrayGbE* _M0L9pre__fireS798,
  struct _M0TPB5ArrayGfE* _M0L7post__gS804
) {
  int32_t _M0L4rowsS794;
  int32_t _M0L7_2abindS796;
  int32_t _M0L1iS797;
  #line 287 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  _M0L4rowsS794 = _M0L1mS795->$0;
  _M0L7_2abindS796 = 0;
  _M0L1iS797 = _M0L7_2abindS796;
  while (1) {
    if (_M0L1iS797 < _M0L4rowsS794) {
      int32_t _M0L6_2atmpS1909;
      #line 294 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L9pre__fireS798, _M0L1iS797)) {
        struct _M0TPB5ArrayGiE* _M0L6rowptrS1908 = _M0L1mS795->$2;
        int32_t _M0L5startS799;
        struct _M0TPB5ArrayGiE* _M0L6rowptrS1906;
        int32_t _M0L6_2atmpS1907;
        int32_t _M0L3endS800;
        int32_t _M0L1kS801;
        #line 295 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
        _M0L5startS799
        = _M0MPC15array5Array2atGiE(_M0L6rowptrS1908, _M0L1iS797);
        _M0L6rowptrS1906 = _M0L1mS795->$2;
        _M0L6_2atmpS1907 = _M0L1iS797 + 1;
        #line 296 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
        _M0L3endS800
        = _M0MPC15array5Array2atGiE(_M0L6rowptrS1906, _M0L6_2atmpS1907);
        _M0L1kS801 = _M0L5startS799;
        while (1) {
          if (_M0L1kS801 < _M0L3endS800) {
            struct _M0TPB5ArrayGiE* _M0L6colptrS1904 = _M0L1mS795->$3;
            int32_t _M0L9post__idxS802;
            struct _M0TPB5ArrayGfE* _M0L4valsS1903;
            float _M0L1wS803;
            float _M0L6_2atmpS1902;
            float _M0L6_2atmpS1901;
            int32_t _M0L6_2atmpS1905;
            #line 298 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
            _M0L9post__idxS802
            = _M0MPC15array5Array2atGiE(_M0L6colptrS1904, _M0L1kS801);
            _M0L4valsS1903 = _M0L1mS795->$4;
            #line 299 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
            _M0L1wS803
            = _M0MPC15array5Array2atGfE(_M0L4valsS1903, _M0L1kS801);
            #line 300 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
            _M0L6_2atmpS1902
            = _M0MPC15array5Array2atGfE(_M0L7post__gS804, _M0L9post__idxS802);
            _M0L6_2atmpS1901 = _M0L6_2atmpS1902 + _M0L1wS803;
            #line 300 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
            _M0MPC15array5Array3setGfE(_M0L7post__gS804, _M0L9post__idxS802, _M0L6_2atmpS1901);
            _M0L6_2atmpS1905 = _M0L1kS801 + 1;
            _M0L1kS801 = _M0L6_2atmpS1905;
            continue;
          }
          break;
        }
      }
      _M0L6_2atmpS1909 = _M0L1iS797 + 1;
      _M0L1iS797 = _M0L6_2atmpS1909;
      continue;
    }
    break;
  }
  return 0;
}

struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0MP26RiantR8snn__mbt7Xoshiro3new(
  uint64_t _M0L4seedS792
) {
  struct _M0TUmmmmE* _M0L1sS791;
  uint64_t _M0L6_2atmpS1900;
  struct _M0TUmmmmE* _M0L1tS793;
  uint64_t _M0L6_2atmpS1896;
  uint64_t _M0L6_2atmpS1897;
  uint64_t _M0L6_2atmpS1898;
  uint64_t _M0L6_2atmpS1899;
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _block_2255;
  #line 48 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  #line 49 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L1sS791 = _M0FP26RiantR8snn__mbt10splitmix64(_M0L4seedS792);
  _M0L6_2atmpS1900 = _M0L1sS791->$3;
  #line 50 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L1tS793 = _M0FP26RiantR8snn__mbt10splitmix64(_M0L6_2atmpS1900);
  _M0L6_2atmpS1896 = _M0L1sS791->$0;
  _M0L6_2atmpS1897 = _M0L1sS791->$1;
  _M0L6_2atmpS1898 = _M0L1sS791->$2;
  moonbit_decref_cycle_free(_M0L1sS791);
  _M0L6_2atmpS1899 = _M0L1tS793->$0;
  moonbit_decref_cycle_free(_M0L1tS793);
  _block_2255
  = (struct _M0TP26RiantR8snn__mbt7Xoshiro*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt7Xoshiro));
  Moonbit_object_header(_block_2255)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_2255->$0 = _M0L6_2atmpS1896;
  _block_2255->$1 = _M0L6_2atmpS1897;
  _block_2255->$2 = _M0L6_2atmpS1898;
  _block_2255->$3 = _M0L6_2atmpS1899;
  return _block_2255;
}

struct _M0TUmmmmE* _M0FP26RiantR8snn__mbt10splitmix64(uint64_t _M0L4seedS783) {
  uint64_t _M0L2s1S782;
  uint64_t _M0L2z1S784;
  uint64_t _M0L2s2S785;
  uint64_t _M0L2z2S786;
  uint64_t _M0L2s3S787;
  uint64_t _M0L2z3S788;
  uint64_t _M0L2s4S789;
  uint64_t _M0L2z4S790;
  struct _M0TUmmmmE* _block_2256;
  #line 62 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2s1S782 = _M0L4seedS783 + 11400714819323198485ull;
  #line 64 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2z1S784 = _M0FP26RiantR8snn__mbt5mix64(_M0L2s1S782);
  _M0L2s2S785 = _M0L2s1S782 + 11400714819323198485ull;
  #line 66 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2z2S786 = _M0FP26RiantR8snn__mbt5mix64(_M0L2s2S785);
  _M0L2s3S787 = _M0L2s2S785 + 11400714819323198485ull;
  #line 68 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2z3S788 = _M0FP26RiantR8snn__mbt5mix64(_M0L2s3S787);
  _M0L2s4S789 = _M0L2s3S787 + 11400714819323198485ull;
  #line 70 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2z4S790 = _M0FP26RiantR8snn__mbt5mix64(_M0L2s4S789);
  _block_2256 = (struct _M0TUmmmmE*)moonbit_malloc(sizeof(struct _M0TUmmmmE));
  Moonbit_object_header(_block_2256)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_2256->$0 = _M0L2z1S784;
  _block_2256->$1 = _M0L2z2S786;
  _block_2256->$2 = _M0L2z3S788;
  _block_2256->$3 = _M0L2z4S790;
  return _block_2256;
}

uint64_t _M0FP26RiantR8snn__mbt5mix64(uint64_t _M0L1zS780) {
  uint64_t _M0L6_2atmpS1895;
  uint64_t _M0L6_2atmpS1894;
  uint64_t _M0L1zS779;
  uint64_t _M0L6_2atmpS1893;
  uint64_t _M0L6_2atmpS1892;
  uint64_t _M0L1zS781;
  uint64_t _M0L6_2atmpS1891;
  #line 75 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L6_2atmpS1895 = _M0L1zS780 >> 30;
  _M0L6_2atmpS1894 = _M0L1zS780 ^ _M0L6_2atmpS1895;
  _M0L1zS779 = _M0L6_2atmpS1894 * 13787848793156543929ull;
  _M0L6_2atmpS1893 = _M0L1zS779 >> 27;
  _M0L6_2atmpS1892 = _M0L1zS779 ^ _M0L6_2atmpS1893;
  _M0L1zS781 = _M0L6_2atmpS1892 * 10723151780598845931ull;
  _M0L6_2atmpS1891 = _M0L1zS781 >> 31;
  return _M0L1zS781 ^ _M0L6_2atmpS1891;
}

int32_t _M0FP26RiantR8snn__mbt12update__time(
  struct _M0TP26RiantR8snn__mbt4Time* _M0L1tS777,
  float _M0L2dtS778
) {
  struct _M0TPB5ArrayGfE* _M0L1tS1883;
  struct _M0TPB5ArrayGfE* _M0L1tS1886;
  float _M0L6_2atmpS1885;
  float _M0L6_2atmpS1884;
  struct _M0TPB5ArrayGiE* _M0L2ttS1887;
  struct _M0TPB5ArrayGiE* _M0L2ttS1890;
  int32_t _M0L6_2atmpS1889;
  int32_t _M0L6_2atmpS1888;
  #line 89 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\time.mbt"
  _M0L1tS1883 = _M0L1tS777->$0;
  _M0L1tS1886 = _M0L1tS777->$0;
  #line 90 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\time.mbt"
  _M0L6_2atmpS1885 = _M0MPC15array5Array2atGfE(_M0L1tS1886, 0);
  _M0L6_2atmpS1884 = _M0L6_2atmpS1885 + _M0L2dtS778;
  #line 90 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\time.mbt"
  _M0MPC15array5Array3setGfE(_M0L1tS1883, 0, _M0L6_2atmpS1884);
  _M0L2ttS1887 = _M0L1tS777->$1;
  _M0L2ttS1890 = _M0L1tS777->$1;
  #line 91 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\time.mbt"
  _M0L6_2atmpS1889 = _M0MPC15array5Array2atGiE(_M0L2ttS1890, 0);
  _M0L6_2atmpS1888 = _M0L6_2atmpS1889 + 1;
  #line 91 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\time.mbt"
  _M0MPC15array5Array3setGiE(_M0L2ttS1887, 0, _M0L6_2atmpS1888);
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt7set__dt(
  struct _M0TP26RiantR8snn__mbt4Time* _M0L1tS775,
  float _M0L1vS776
) {
  #line 80 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\time.mbt"
  _M0L1tS775->$2 = _M0L1vS776;
  return 0;
}

struct _M0TP26RiantR8snn__mbt4Time* _M0MP26RiantR8snn__mbt4Time3new() {
  float* _M0L6_2atmpS1882;
  struct _M0TPB5ArrayGfE* _M0L6_2atmpS1879;
  int32_t* _M0L6_2atmpS1881;
  struct _M0TPB5ArrayGiE* _M0L6_2atmpS1880;
  struct _M0TP26RiantR8snn__mbt4Time* _block_2257;
  #line 34 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\time.mbt"
  _M0L6_2atmpS1882 = (float*)moonbit_make_float_array_raw(1);
  _M0L6_2atmpS1882[0] = 0x0p+0f;
  _M0L6_2atmpS1879
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_M0L6_2atmpS1879)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 37, 0);
  _M0L6_2atmpS1879->$0 = _M0L6_2atmpS1882;
  _M0L6_2atmpS1879->$1 = 1;
  _M0L6_2atmpS1881 = (int32_t*)moonbit_make_int32_array_raw(1);
  _M0L6_2atmpS1881[0] = 0;
  _M0L6_2atmpS1880
  = (struct _M0TPB5ArrayGiE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGiE));
  Moonbit_object_header(_M0L6_2atmpS1880)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 46, 0);
  _M0L6_2atmpS1880->$0 = _M0L6_2atmpS1881;
  _M0L6_2atmpS1880->$1 = 1;
  _block_2257
  = (struct _M0TP26RiantR8snn__mbt4Time*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt4Time));
  Moonbit_object_header(_block_2257)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 49, 0);
  _block_2257->$0 = _M0L6_2atmpS1879;
  _block_2257->$1 = _M0L6_2atmpS1880;
  _block_2257->$2 = 0x1p-3f;
  return _block_2257;
}

float _M0FP26RiantR8snn__mbt9next__f32(
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L1rS773
) {
  uint32_t _M0L1uS772;
  uint32_t _M0L4bitsS774;
  double _M0L6_2atmpS1878;
  double _M0L6_2atmpS1877;
  #line 128 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  #line 129 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L1uS772 = _M0FP26RiantR8snn__mbt9next__u32(_M0L1rS773);
  _M0L4bitsS774 = _M0L1uS772 >> 8;
  _M0L6_2atmpS1878 = (double)_M0L4bitsS774;
  _M0L6_2atmpS1877 = _M0L6_2atmpS1878 * 0x1p-24;
  return (float)_M0L6_2atmpS1877;
}

uint32_t _M0FP26RiantR8snn__mbt9next__u32(
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L1rS771
) {
  uint64_t _M0L1uS770;
  uint64_t _M0L6_2atmpS1876;
  #line 110 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  #line 111 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L1uS770 = _M0FP26RiantR8snn__mbt9next__u64(_M0L1rS771);
  _M0L6_2atmpS1876 = _M0L1uS770 >> 32;
  return (uint32_t)_M0L6_2atmpS1876;
}

uint64_t _M0FP26RiantR8snn__mbt9next__u64(
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L1rS763
) {
  uint64_t _M0L2s0S762;
  uint64_t _M0L2s1S764;
  uint64_t _M0L2s2S765;
  uint64_t _M0L2s3S766;
  uint64_t _M0L3tmpS767;
  uint64_t _M0L6_2atmpS1875;
  uint64_t _M0L3resS768;
  uint64_t _M0L1tS769;
  uint64_t _M0L6_2atmpS1865;
  uint64_t _M0L6_2atmpS1866;
  uint64_t _M0L2s2S1868;
  uint64_t _M0L6_2atmpS1867;
  uint64_t _M0L2s3S1870;
  uint64_t _M0L6_2atmpS1869;
  uint64_t _M0L2s2S1872;
  uint64_t _M0L6_2atmpS1871;
  uint64_t _M0L2s3S1874;
  uint64_t _M0L6_2atmpS1873;
  #line 90 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2s0S762 = _M0L1rS763->$0;
  _M0L2s1S764 = _M0L1rS763->$1;
  _M0L2s2S765 = _M0L1rS763->$2;
  _M0L2s3S766 = _M0L1rS763->$3;
  _M0L3tmpS767 = _M0L2s0S762 + _M0L2s3S766;
  #line 96 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L6_2atmpS1875 = _M0FP26RiantR8snn__mbt4rotl(_M0L3tmpS767, 23);
  _M0L3resS768 = _M0L6_2atmpS1875 + _M0L2s0S762;
  _M0L1tS769 = _M0L2s1S764 << 17;
  _M0L6_2atmpS1865 = _M0L2s2S765 ^ _M0L2s0S762;
  _M0L1rS763->$2 = _M0L6_2atmpS1865;
  _M0L6_2atmpS1866 = _M0L2s3S766 ^ _M0L2s1S764;
  _M0L1rS763->$3 = _M0L6_2atmpS1866;
  _M0L2s2S1868 = _M0L1rS763->$2;
  _M0L6_2atmpS1867 = _M0L2s1S764 ^ _M0L2s2S1868;
  _M0L1rS763->$1 = _M0L6_2atmpS1867;
  _M0L2s3S1870 = _M0L1rS763->$3;
  _M0L6_2atmpS1869 = _M0L2s0S762 ^ _M0L2s3S1870;
  _M0L1rS763->$0 = _M0L6_2atmpS1869;
  _M0L2s2S1872 = _M0L1rS763->$2;
  _M0L6_2atmpS1871 = _M0L2s2S1872 ^ _M0L1tS769;
  _M0L1rS763->$2 = _M0L6_2atmpS1871;
  _M0L2s3S1874 = _M0L1rS763->$3;
  #line 103 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L6_2atmpS1873 = _M0FP26RiantR8snn__mbt4rotl(_M0L2s3S1874, 45);
  _M0L1rS763->$3 = _M0L6_2atmpS1873;
  return _M0L3resS768;
}

uint64_t _M0FP26RiantR8snn__mbt4rotl(uint64_t _M0L1xS760, int32_t _M0L1kS761) {
  uint64_t _M0L6_2atmpS1862;
  int32_t _M0L6_2atmpS1864;
  uint64_t _M0L6_2atmpS1863;
  #line 83 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L6_2atmpS1862 = _M0L1xS760 << (_M0L1kS761 & 63);
  _M0L6_2atmpS1864 = 64 - _M0L1kS761;
  _M0L6_2atmpS1863 = _M0L1xS760 >> (_M0L6_2atmpS1864 & 63);
  return _M0L6_2atmpS1862 | _M0L6_2atmpS1863;
}

moonbit_string_t _M0IPC15float5FloatPB4Show10to__string(float _M0L4selfS759) {
  double _M0L6_2atmpS1861;
  #line 16 "C:\\Users\\31379\\.moon\\lib\\core\\float\\methods.mbt"
  _M0L6_2atmpS1861 = (double)_M0L4selfS759;
  #line 17 "C:\\Users\\31379\\.moon\\lib\\core\\float\\methods.mbt"
  return _M0MPC16double6Double10to__string(_M0L6_2atmpS1861);
}

int32_t _M0MPC15float5Float7to__int(float _M0L4selfS758) {
  #line 43 "C:\\Users\\31379\\.moon\\lib\\core\\float\\to_int.mbt"
  if (_M0L4selfS758 != _M0L4selfS758) {
    return 0;
  } else if (_M0L4selfS758 >= 0x1.fffffffcp+30f) {
    return 2147483647;
  } else if (_M0L4selfS758 <= -0x1p+31f) {
    return (int32_t)0x80000000;
  } else {
    return (int32_t)_M0L4selfS758;
  }
}

struct _M0TPB5ArrayGfE* _M0MPC15array5Array4makeGfE(
  int32_t _M0L3lenS744,
  float _M0L4elemS746
) {
  struct _M0TPB5ArrayGfE* _M0L3arrS743;
  int32_t _M0L1iS745;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3arrS743 = _M0MPC15array5Array20unsafe__make__uninitGfE(_M0L3lenS744);
  _M0L1iS745 = 0;
  while (1) {
    if (_M0L1iS745 < _M0L3lenS744) {
      float* _M0L3bufS1855 = _M0L3arrS743->$0;
      int32_t _M0L6_2atmpS1856;
      _M0L3bufS1855[_M0L1iS745] = _M0L4elemS746;
      _M0L6_2atmpS1856 = _M0L1iS745 + 1;
      _M0L1iS745 = _M0L6_2atmpS1856;
      continue;
    }
    break;
  }
  return _M0L3arrS743;
}

struct _M0TPB5ArrayGbE* _M0MPC15array5Array4makeGbE(
  int32_t _M0L3lenS749,
  int32_t _M0L4elemS751
) {
  struct _M0TPB5ArrayGbE* _M0L3arrS748;
  int32_t _M0L1iS750;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3arrS748 = _M0MPC15array5Array20unsafe__make__uninitGbE(_M0L3lenS749);
  _M0L1iS750 = 0;
  while (1) {
    if (_M0L1iS750 < _M0L3lenS749) {
      uint8_t* _M0L3bufS1857 = _M0L3arrS748->$0;
      int32_t _M0L6_2atmpS1858;
      _M0L3bufS1857[_M0L1iS750] = _M0L4elemS751;
      _M0L6_2atmpS1858 = _M0L1iS750 + 1;
      _M0L1iS750 = _M0L6_2atmpS1858;
      continue;
    }
    break;
  }
  return _M0L3arrS748;
}

struct _M0TPB5ArrayGiE* _M0MPC15array5Array4makeGiE(
  int32_t _M0L3lenS754,
  int32_t _M0L4elemS756
) {
  struct _M0TPB5ArrayGiE* _M0L3arrS753;
  int32_t _M0L1iS755;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3arrS753 = _M0MPC15array5Array20unsafe__make__uninitGiE(_M0L3lenS754);
  _M0L1iS755 = 0;
  while (1) {
    if (_M0L1iS755 < _M0L3lenS754) {
      int32_t* _M0L3bufS1859 = _M0L3arrS753->$0;
      int32_t _M0L6_2atmpS1860;
      _M0L3bufS1859[_M0L1iS755] = _M0L4elemS756;
      _M0L6_2atmpS1860 = _M0L1iS755 + 1;
      _M0L1iS755 = _M0L6_2atmpS1860;
      continue;
    }
    break;
  }
  return _M0L3arrS753;
}

int32_t _M0MPC15array5Array3setGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS732,
  int32_t _M0L5indexS733,
  float _M0L5valueS734
) {
  int32_t _M0L3lenS731;
  #line 262 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS731 = _M0L4selfS732->$1;
  if (_M0L5indexS733 >= 0 && _M0L5indexS733 < _M0L3lenS731) {
    float* _M0L6_2atmpS1852;
    #line 268 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS1852 = _M0MPC15array5Array6bufferGfE(_M0L4selfS732);
    _M0L6_2atmpS1852[_M0L5indexS733] = _M0L5valueS734;
    moonbit_decref_cycle_free(_M0L6_2atmpS1852);
  } else {
    #line 267 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
  return 0;
}

int32_t _M0MPC15array5Array3setGbE(
  struct _M0TPB5ArrayGbE* _M0L4selfS736,
  int32_t _M0L5indexS737,
  int32_t _M0L5valueS738
) {
  int32_t _M0L3lenS735;
  #line 262 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS735 = _M0L4selfS736->$1;
  if (_M0L5indexS737 >= 0 && _M0L5indexS737 < _M0L3lenS735) {
    uint8_t* _M0L6_2atmpS1853;
    #line 268 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS1853 = _M0MPC15array5Array6bufferGbE(_M0L4selfS736);
    _M0L6_2atmpS1853[_M0L5indexS737] = _M0L5valueS738;
    moonbit_decref_cycle_free(_M0L6_2atmpS1853);
  } else {
    #line 267 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
  return 0;
}

int32_t _M0MPC15array5Array3setGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS740,
  int32_t _M0L5indexS741,
  int32_t _M0L5valueS742
) {
  int32_t _M0L3lenS739;
  #line 262 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS739 = _M0L4selfS740->$1;
  if (_M0L5indexS741 >= 0 && _M0L5indexS741 < _M0L3lenS739) {
    int32_t* _M0L6_2atmpS1854;
    #line 268 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS1854 = _M0MPC15array5Array6bufferGiE(_M0L4selfS740);
    _M0L6_2atmpS1854[_M0L5indexS741] = _M0L5valueS742;
    moonbit_decref_cycle_free(_M0L6_2atmpS1854);
  } else {
    #line 267 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
  return 0;
}

float _M0MPC15array5Array2atGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS717,
  int32_t _M0L5indexS718
) {
  int32_t _M0L3lenS716;
  #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS716 = _M0L4selfS717->$1;
  if (_M0L5indexS718 >= 0 && _M0L5indexS718 < _M0L3lenS716) {
    float* _M0L6_2atmpS1847;
    float _result_2261;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS1847 = _M0MPC15array5Array6bufferGfE(_M0L4selfS717);
    _result_2261 = (float)_M0L6_2atmpS1847[_M0L5indexS718];
    moonbit_decref_cycle_free(_M0L6_2atmpS1847);
    return _result_2261;
  } else {
    #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
}

int32_t _M0MPC15array5Array2atGbE(
  struct _M0TPB5ArrayGbE* _M0L4selfS720,
  int32_t _M0L5indexS721
) {
  int32_t _M0L3lenS719;
  #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS719 = _M0L4selfS720->$1;
  if (_M0L5indexS721 >= 0 && _M0L5indexS721 < _M0L3lenS719) {
    uint8_t* _M0L6_2atmpS1848;
    int32_t _result_2262;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS1848 = _M0MPC15array5Array6bufferGbE(_M0L4selfS720);
    _result_2262 = (int32_t)_M0L6_2atmpS1848[_M0L5indexS721];
    moonbit_decref_cycle_free(_M0L6_2atmpS1848);
    return _result_2262;
  } else {
    #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
}

struct _M0TP26RiantR8snn__mbt11MonitorAdEx* _M0MPC15array5Array2atGRP26RiantR8snn__mbt11MonitorAdExE(
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt11MonitorAdExE* _M0L4selfS723,
  int32_t _M0L5indexS724
) {
  int32_t _M0L3lenS722;
  #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS722 = _M0L4selfS723->$1;
  if (_M0L5indexS724 >= 0 && _M0L5indexS724 < _M0L3lenS722) {
    struct _M0TP26RiantR8snn__mbt11MonitorAdEx** _M0L6_2atmpS1849;
    struct _M0TP26RiantR8snn__mbt11MonitorAdEx* _M0L6_2atmpS2185;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS1849
    = _M0MPC15array5Array6bufferGRP26RiantR8snn__mbt11MonitorAdExE(_M0L4selfS723);
    _M0L6_2atmpS2185
    = (struct _M0TP26RiantR8snn__mbt11MonitorAdEx*)_M0L6_2atmpS1849[
        _M0L5indexS724
      ];
    if (_M0L6_2atmpS2185) {
      moonbit_incref_cycle_free(_M0L6_2atmpS2185);
    }
    moonbit_decref_cycle_free(_M0L6_2atmpS1849);
    return _M0L6_2atmpS2185;
  } else {
    #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
}

moonbit_string_t _M0MPC15array5Array2atGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS726,
  int32_t _M0L5indexS727
) {
  int32_t _M0L3lenS725;
  #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS725 = _M0L4selfS726->$1;
  if (_M0L5indexS727 >= 0 && _M0L5indexS727 < _M0L3lenS725) {
    moonbit_string_t* _M0L6_2atmpS1850;
    moonbit_string_t _M0L6_2atmpS2186;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS1850 = _M0MPC15array5Array6bufferGsE(_M0L4selfS726);
    _M0L6_2atmpS2186 = (moonbit_string_t)_M0L6_2atmpS1850[_M0L5indexS727];
    moonbit_incref_cycle_free(_M0L6_2atmpS2186);
    moonbit_decref_cycle_free(_M0L6_2atmpS1850);
    return _M0L6_2atmpS2186;
  } else {
    #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
}

int32_t _M0MPC15array5Array2atGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS729,
  int32_t _M0L5indexS730
) {
  int32_t _M0L3lenS728;
  #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS728 = _M0L4selfS729->$1;
  if (_M0L5indexS730 >= 0 && _M0L5indexS730 < _M0L3lenS728) {
    int32_t* _M0L6_2atmpS1851;
    int32_t _result_2263;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS1851 = _M0MPC15array5Array6bufferGiE(_M0L4selfS729);
    _result_2263 = (int32_t)_M0L6_2atmpS1851[_M0L5indexS730];
    moonbit_decref_cycle_free(_M0L6_2atmpS1851);
    return _result_2263;
  } else {
    #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
}

int32_t _M0FPB7printlnGsE(moonbit_string_t _M0L5inputS715) {
  moonbit_string_t _M0L6_2atmpS1846;
  #line 36 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\console.mbt"
  #line 37 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\console.mbt"
  _M0L6_2atmpS1846 = _M0IPC16string6StringPB4Show10to__string(_M0L5inputS715);
  #line 37 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\console.mbt"
  moonbit_println(_M0L6_2atmpS1846);
  moonbit_decref_cycle_free(_M0L6_2atmpS1846);
  return 0;
}

moonbit_string_t _M0MPC16double6Double10to__string(double _M0L4selfS714) {
  #line 296 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double.mbt"
  #line 298 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double.mbt"
  return _M0FPB15ryu__to__string(_M0L4selfS714);
}

moonbit_string_t _M0FPB15ryu__to__string(double _M0L3valS699) {
  uint64_t _M0L4bitsS702;
  uint64_t _M0L6_2atmpS1845;
  uint64_t _M0L6_2atmpS1844;
  int32_t _M0L8ieeeSignS703;
  uint64_t _M0L12ieeeMantissaS704;
  uint64_t _M0L6_2atmpS1843;
  uint64_t _M0L6_2atmpS1842;
  int32_t _M0L12ieeeExponentS705;
  struct _M0TPB17FloatingDecimal64* _M0L7_2abindS706;
  struct _M0TPB17FloatingDecimal64* _M0L1vS707;
  moonbit_string_t _result_2265;
  #line 668 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  if (_M0L3valS699 == 0x0p+0) {
    return (moonbit_string_t)moonbit_string_literal_14.data;
  }
  if (_M0L3valS699 >= -0x1p+53 && _M0L3valS699 <= 0x1p+53) {
    if (_M0L3valS699 >= -0x1p+31 && _M0L3valS699 <= 0x1.fffffffcp+30) {
      int32_t _M0L1iS700;
      double _M0L6_2atmpS1831;
      #line 683 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0L1iS700 = _M0MPC16double6Double7to__int(_M0L3valS699);
      _M0L6_2atmpS1831 = (double)_M0L1iS700;
      if (_M0L6_2atmpS1831 == _M0L3valS699) {
        #line 685 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        return _M0MPC13int3Int18to__string_2einner(_M0L1iS700, 10);
      }
    } else {
      int64_t _M0L1iS701;
      double _M0L6_2atmpS1832;
      #line 688 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0L1iS701 = _M0MPC16double6Double9to__int64(_M0L3valS699);
      _M0L6_2atmpS1832 = (double)_M0L1iS701;
      if (_M0L6_2atmpS1832 == _M0L3valS699) {
        #line 690 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        return _M0MPC15int645Int6418to__string_2einner(_M0L1iS701, 10);
      }
    }
  }
  _M0L4bitsS702 = *(int64_t*)&_M0L3valS699;
  _M0L6_2atmpS1845 = _M0L4bitsS702 >> 63;
  _M0L6_2atmpS1844 = _M0L6_2atmpS1845 & 1ull;
  _M0L8ieeeSignS703 = _M0L6_2atmpS1844 != 0ull;
  _M0L12ieeeMantissaS704 = _M0L4bitsS702 & 4503599627370495ull;
  _M0L6_2atmpS1843 = _M0L4bitsS702 >> 52;
  _M0L6_2atmpS1842 = _M0L6_2atmpS1843 & 2047ull;
  _M0L12ieeeExponentS705 = (int32_t)_M0L6_2atmpS1842;
  if (
    _M0L12ieeeExponentS705 == 2047
    || _M0L12ieeeExponentS705 == 0 && _M0L12ieeeMantissaS704 == 0ull
  ) {
    int32_t _M0L6_2atmpS1833 = _M0L12ieeeExponentS705 != 0;
    int32_t _M0L6_2atmpS1834 = _M0L12ieeeMantissaS704 != 0ull;
    #line 707 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    return _M0FPB18copy__special__str(_M0L8ieeeSignS703, _M0L6_2atmpS1833, _M0L6_2atmpS1834);
  }
  #line 709 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS706
  = _M0FPB15d2d__small__int(_M0L12ieeeMantissaS704, _M0L12ieeeExponentS705);
  if (_M0L7_2abindS706 == 0) {
    uint32_t _M0L6_2atmpS1835;
    if (_M0L7_2abindS706) {
      moonbit_decref_cycle_free(_M0L7_2abindS706);
    }
    _M0L6_2atmpS1835 = *(uint32_t*)&_M0L12ieeeExponentS705;
    #line 719 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L1vS707 = _M0FPB3d2d(_M0L12ieeeMantissaS704, _M0L6_2atmpS1835);
  } else {
    struct _M0TPB17FloatingDecimal64* _M0L7_2aSomeS708 = _M0L7_2abindS706;
    struct _M0TPB17FloatingDecimal64* _M0L4_2afS709 = _M0L7_2aSomeS708;
    struct _M0TPB17FloatingDecimal64* _M0L1xS710 = _M0L4_2afS709;
    while (1) {
      uint64_t _M0L8mantissaS1841 = _M0L1xS710->$0;
      uint64_t _M0L1qS711 = _M0L8mantissaS1841 / 10ull;
      uint64_t _M0L8mantissaS1839 = _M0L1xS710->$0;
      uint64_t _M0L6_2atmpS1840 = 10ull * _M0L1qS711;
      uint64_t _M0L1rS712 = _M0L8mantissaS1839 - _M0L6_2atmpS1840;
      int32_t _M0L8exponentS1838;
      int32_t _M0L6_2atmpS1837;
      struct _M0TPB17FloatingDecimal64* _M0L6_2atmpS1836;
      if (_M0L1rS712 != 0ull) {
        _M0L1vS707 = _M0L1xS710;
        break;
      }
      _M0L8exponentS1838 = _M0L1xS710->$1;
      moonbit_decref_cycle_free(_M0L1xS710);
      _M0L6_2atmpS1837 = _M0L8exponentS1838 + 1;
      _M0L6_2atmpS1836
      = (struct _M0TPB17FloatingDecimal64*)moonbit_malloc(sizeof(struct _M0TPB17FloatingDecimal64));
      Moonbit_object_header(_M0L6_2atmpS1836)->meta
      = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
      _M0L6_2atmpS1836->$0 = _M0L1qS711;
      _M0L6_2atmpS1836->$1 = _M0L6_2atmpS1837;
      _M0L1xS710 = _M0L6_2atmpS1836;
      continue;
      break;
    }
  }
  #line 721 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _result_2265 = _M0FPB9to__chars(_M0L1vS707, _M0L8ieeeSignS703);
  moonbit_decref_cycle_free(_M0L1vS707);
  return _result_2265;
}

struct _M0TPB17FloatingDecimal64* _M0FPB15d2d__small__int(
  uint64_t _M0L12ieeeMantissaS694,
  int32_t _M0L12ieeeExponentS696
) {
  uint64_t _M0L2m2S693;
  int32_t _M0L6_2atmpS1830;
  int32_t _M0L2e2S695;
  int32_t _M0L6_2atmpS1829;
  uint64_t _M0L6_2atmpS1828;
  uint64_t _M0L4maskS697;
  uint64_t _M0L8fractionS698;
  int32_t _M0L6_2atmpS1827;
  uint64_t _M0L6_2atmpS1826;
  struct _M0TPB17FloatingDecimal64* _M0L6_2atmpS1825;
  #line 637 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L2m2S693 = 4503599627370496ull | _M0L12ieeeMantissaS694;
  _M0L6_2atmpS1830 = _M0L12ieeeExponentS696 - 1023;
  _M0L2e2S695 = _M0L6_2atmpS1830 - 52;
  if (_M0L2e2S695 > 0) {
    return 0;
  }
  if (_M0L2e2S695 < -52) {
    return 0;
  }
  _M0L6_2atmpS1829 = -_M0L2e2S695;
  _M0L6_2atmpS1828 = 1ull << (_M0L6_2atmpS1829 & 63);
  _M0L4maskS697 = _M0L6_2atmpS1828 - 1ull;
  _M0L8fractionS698 = _M0L2m2S693 & _M0L4maskS697;
  if (_M0L8fractionS698 != 0ull) {
    return 0;
  }
  _M0L6_2atmpS1827 = -_M0L2e2S695;
  _M0L6_2atmpS1826 = _M0L2m2S693 >> (_M0L6_2atmpS1827 & 63);
  _M0L6_2atmpS1825
  = (struct _M0TPB17FloatingDecimal64*)moonbit_malloc(sizeof(struct _M0TPB17FloatingDecimal64));
  Moonbit_object_header(_M0L6_2atmpS1825)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L6_2atmpS1825->$0 = _M0L6_2atmpS1826;
  _M0L6_2atmpS1825->$1 = 0;
  return _M0L6_2atmpS1825;
}

moonbit_string_t _M0FPB9to__chars(
  struct _M0TPB17FloatingDecimal64* _M0L1vS661,
  int32_t _M0L4signS659
) {
  moonbit_bytes_t _M0L6resultS657;
  int32_t _M0Lm5indexS658;
  uint64_t _M0L6outputS660;
  int32_t _M0L7olengthS662;
  int32_t _M0L8exponentS1824;
  int32_t _M0L6_2atmpS1823;
  int32_t _M0Lm3expS663;
  int32_t _M0L6_2atmpS1822;
  int32_t _M0L6_2atmpS1820;
  int32_t _M0L18scientificNotationS664;
  #line 530 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6resultS657 = (moonbit_bytes_t)moonbit_make_bytes(25, 0);
  _M0Lm5indexS658 = 0;
  if (_M0L4signS659) {
    int32_t _M0L6_2atmpS1694 = _M0Lm5indexS658;
    int32_t _M0L6_2atmpS1695;
    if (
      _M0L6_2atmpS1694 < 0
      || _M0L6_2atmpS1694 >= Moonbit_array_length(_M0L6resultS657)
    ) {
      #line 535 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      moonbit_panic();
    }
    _M0L6resultS657[_M0L6_2atmpS1694] = 45;
    _M0L6_2atmpS1695 = _M0Lm5indexS658;
    _M0Lm5indexS658 = _M0L6_2atmpS1695 + 1;
  }
  _M0L6outputS660 = _M0L1vS661->$0;
  #line 539 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7olengthS662 = _M0FPB17decimal__length17(_M0L6outputS660);
  _M0L8exponentS1824 = _M0L1vS661->$1;
  _M0L6_2atmpS1823 = _M0L8exponentS1824 + _M0L7olengthS662;
  _M0Lm3expS663 = _M0L6_2atmpS1823 - 1;
  _M0L6_2atmpS1822 = _M0Lm3expS663;
  if (_M0L6_2atmpS1822 >= -6) {
    int32_t _M0L6_2atmpS1821 = _M0Lm3expS663;
    _M0L6_2atmpS1820 = _M0L6_2atmpS1821 < 21;
  } else {
    _M0L6_2atmpS1820 = 0;
  }
  _M0L18scientificNotationS664 = !_M0L6_2atmpS1820;
  if (_M0L18scientificNotationS664) {
    int32_t _M0L7_2abindS665 = _M0L7olengthS662 - 1;
    uint64_t _M0L6outputS666;
    int32_t _M0L1iS667 = 0;
    uint64_t _M0L6outputS668 = _M0L6outputS660;
    int32_t _M0L6_2atmpS1696;
    int32_t _M0L6_2atmpS1700;
    int32_t _M0L6_2atmpS1699;
    int32_t _M0L6_2atmpS1698;
    int32_t _M0L6_2atmpS1697;
    int32_t _M0L6_2atmpS1704;
    int32_t _M0L6_2atmpS1705;
    int32_t _M0L6_2atmpS1706;
    int32_t _M0L6_2atmpS1707;
    int32_t _M0L6_2atmpS1708;
    int32_t _M0L6_2atmpS1714;
    int32_t _M0L6_2atmpS1747;
    moonbit_string_t _result_2267;
    while (1) {
      if (_M0L1iS667 < _M0L7_2abindS665) {
        uint64_t _M0L1cS669 = _M0L6outputS668 % 10ull;
        int32_t _M0L6_2atmpS1753 = _M0Lm5indexS658;
        int32_t _M0L6_2atmpS1752 = _M0L6_2atmpS1753 + _M0L7olengthS662;
        int32_t _M0L6_2atmpS1748 = _M0L6_2atmpS1752 - _M0L1iS667;
        int32_t _M0L6_2atmpS1751 = (int32_t)_M0L1cS669;
        int32_t _M0L6_2atmpS1750 = 48 + _M0L6_2atmpS1751;
        int32_t _M0L6_2atmpS1749 = _M0L6_2atmpS1750 & 0xff;
        int32_t _M0L6_2atmpS1754;
        uint64_t _M0L6_2atmpS1755;
        if (
          _M0L6_2atmpS1748 < 0
          || _M0L6_2atmpS1748 >= Moonbit_array_length(_M0L6resultS657)
        ) {
          #line 547 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
          moonbit_panic();
        }
        _M0L6resultS657[_M0L6_2atmpS1748] = _M0L6_2atmpS1749;
        _M0L6_2atmpS1754 = _M0L1iS667 + 1;
        _M0L6_2atmpS1755 = _M0L6outputS668 / 10ull;
        _M0L1iS667 = _M0L6_2atmpS1754;
        _M0L6outputS668 = _M0L6_2atmpS1755;
        continue;
      } else {
        _M0L6outputS666 = _M0L6outputS668;
      }
      break;
    }
    _M0L6_2atmpS1696 = _M0Lm5indexS658;
    _M0L6_2atmpS1700 = (int32_t)_M0L6outputS666;
    _M0L6_2atmpS1699 = _M0L6_2atmpS1700 % 10;
    _M0L6_2atmpS1698 = 48 + _M0L6_2atmpS1699;
    _M0L6_2atmpS1697 = _M0L6_2atmpS1698 & 0xff;
    if (
      _M0L6_2atmpS1696 < 0
      || _M0L6_2atmpS1696 >= Moonbit_array_length(_M0L6resultS657)
    ) {
      #line 552 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      moonbit_panic();
    }
    _M0L6resultS657[_M0L6_2atmpS1696] = _M0L6_2atmpS1697;
    if (_M0L7olengthS662 > 1) {
      int32_t _M0L6_2atmpS1702 = _M0Lm5indexS658;
      int32_t _M0L6_2atmpS1701 = _M0L6_2atmpS1702 + 1;
      if (
        _M0L6_2atmpS1701 < 0
        || _M0L6_2atmpS1701 >= Moonbit_array_length(_M0L6resultS657)
      ) {
        #line 554 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS657[_M0L6_2atmpS1701] = 46;
    } else {
      int32_t _M0L6_2atmpS1703 = _M0Lm5indexS658;
      _M0Lm5indexS658 = _M0L6_2atmpS1703 - 1;
    }
    _M0L6_2atmpS1704 = _M0Lm5indexS658;
    _M0L6_2atmpS1705 = _M0L7olengthS662 + 1;
    _M0Lm5indexS658 = _M0L6_2atmpS1704 + _M0L6_2atmpS1705;
    _M0L6_2atmpS1706 = _M0Lm5indexS658;
    if (
      _M0L6_2atmpS1706 < 0
      || _M0L6_2atmpS1706 >= Moonbit_array_length(_M0L6resultS657)
    ) {
      #line 562 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      moonbit_panic();
    }
    _M0L6resultS657[_M0L6_2atmpS1706] = 101;
    _M0L6_2atmpS1707 = _M0Lm5indexS658;
    _M0Lm5indexS658 = _M0L6_2atmpS1707 + 1;
    _M0L6_2atmpS1708 = _M0Lm3expS663;
    if (_M0L6_2atmpS1708 < 0) {
      int32_t _M0L6_2atmpS1709 = _M0Lm5indexS658;
      int32_t _M0L6_2atmpS1710;
      int32_t _M0L6_2atmpS1711;
      if (
        _M0L6_2atmpS1709 < 0
        || _M0L6_2atmpS1709 >= Moonbit_array_length(_M0L6resultS657)
      ) {
        #line 565 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS657[_M0L6_2atmpS1709] = 45;
      _M0L6_2atmpS1710 = _M0Lm5indexS658;
      _M0Lm5indexS658 = _M0L6_2atmpS1710 + 1;
      _M0L6_2atmpS1711 = _M0Lm3expS663;
      _M0Lm3expS663 = -_M0L6_2atmpS1711;
    } else {
      int32_t _M0L6_2atmpS1712 = _M0Lm5indexS658;
      int32_t _M0L6_2atmpS1713;
      if (
        _M0L6_2atmpS1712 < 0
        || _M0L6_2atmpS1712 >= Moonbit_array_length(_M0L6resultS657)
      ) {
        #line 569 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS657[_M0L6_2atmpS1712] = 43;
      _M0L6_2atmpS1713 = _M0Lm5indexS658;
      _M0Lm5indexS658 = _M0L6_2atmpS1713 + 1;
    }
    _M0L6_2atmpS1714 = _M0Lm3expS663;
    if (_M0L6_2atmpS1714 >= 100) {
      int32_t _M0L6_2atmpS1730 = _M0Lm3expS663;
      int32_t _M0L1aS671 = _M0L6_2atmpS1730 / 100;
      int32_t _M0L6_2atmpS1729 = _M0Lm3expS663;
      int32_t _M0L6_2atmpS1728 = _M0L6_2atmpS1729 / 10;
      int32_t _M0L1bS672 = _M0L6_2atmpS1728 % 10;
      int32_t _M0L6_2atmpS1727 = _M0Lm3expS663;
      int32_t _M0L1cS673 = _M0L6_2atmpS1727 % 10;
      int32_t _M0L6_2atmpS1715 = _M0Lm5indexS658;
      int32_t _M0L6_2atmpS1717 = 48 + _M0L1aS671;
      int32_t _M0L6_2atmpS1716 = _M0L6_2atmpS1717 & 0xff;
      int32_t _M0L6_2atmpS1721;
      int32_t _M0L6_2atmpS1718;
      int32_t _M0L6_2atmpS1720;
      int32_t _M0L6_2atmpS1719;
      int32_t _M0L6_2atmpS1725;
      int32_t _M0L6_2atmpS1722;
      int32_t _M0L6_2atmpS1724;
      int32_t _M0L6_2atmpS1723;
      int32_t _M0L6_2atmpS1726;
      if (
        _M0L6_2atmpS1715 < 0
        || _M0L6_2atmpS1715 >= Moonbit_array_length(_M0L6resultS657)
      ) {
        #line 576 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS657[_M0L6_2atmpS1715] = _M0L6_2atmpS1716;
      _M0L6_2atmpS1721 = _M0Lm5indexS658;
      _M0L6_2atmpS1718 = _M0L6_2atmpS1721 + 1;
      _M0L6_2atmpS1720 = 48 + _M0L1bS672;
      _M0L6_2atmpS1719 = _M0L6_2atmpS1720 & 0xff;
      if (
        _M0L6_2atmpS1718 < 0
        || _M0L6_2atmpS1718 >= Moonbit_array_length(_M0L6resultS657)
      ) {
        #line 577 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS657[_M0L6_2atmpS1718] = _M0L6_2atmpS1719;
      _M0L6_2atmpS1725 = _M0Lm5indexS658;
      _M0L6_2atmpS1722 = _M0L6_2atmpS1725 + 2;
      _M0L6_2atmpS1724 = 48 + _M0L1cS673;
      _M0L6_2atmpS1723 = _M0L6_2atmpS1724 & 0xff;
      if (
        _M0L6_2atmpS1722 < 0
        || _M0L6_2atmpS1722 >= Moonbit_array_length(_M0L6resultS657)
      ) {
        #line 578 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS657[_M0L6_2atmpS1722] = _M0L6_2atmpS1723;
      _M0L6_2atmpS1726 = _M0Lm5indexS658;
      _M0Lm5indexS658 = _M0L6_2atmpS1726 + 3;
    } else {
      int32_t _M0L6_2atmpS1731 = _M0Lm3expS663;
      if (_M0L6_2atmpS1731 >= 10) {
        int32_t _M0L6_2atmpS1741 = _M0Lm3expS663;
        int32_t _M0L1aS674 = _M0L6_2atmpS1741 / 10;
        int32_t _M0L6_2atmpS1740 = _M0Lm3expS663;
        int32_t _M0L1bS675 = _M0L6_2atmpS1740 % 10;
        int32_t _M0L6_2atmpS1732 = _M0Lm5indexS658;
        int32_t _M0L6_2atmpS1734 = 48 + _M0L1aS674;
        int32_t _M0L6_2atmpS1733 = _M0L6_2atmpS1734 & 0xff;
        int32_t _M0L6_2atmpS1738;
        int32_t _M0L6_2atmpS1735;
        int32_t _M0L6_2atmpS1737;
        int32_t _M0L6_2atmpS1736;
        int32_t _M0L6_2atmpS1739;
        if (
          _M0L6_2atmpS1732 < 0
          || _M0L6_2atmpS1732 >= Moonbit_array_length(_M0L6resultS657)
        ) {
          #line 583 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
          moonbit_panic();
        }
        _M0L6resultS657[_M0L6_2atmpS1732] = _M0L6_2atmpS1733;
        _M0L6_2atmpS1738 = _M0Lm5indexS658;
        _M0L6_2atmpS1735 = _M0L6_2atmpS1738 + 1;
        _M0L6_2atmpS1737 = 48 + _M0L1bS675;
        _M0L6_2atmpS1736 = _M0L6_2atmpS1737 & 0xff;
        if (
          _M0L6_2atmpS1735 < 0
          || _M0L6_2atmpS1735 >= Moonbit_array_length(_M0L6resultS657)
        ) {
          #line 584 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
          moonbit_panic();
        }
        _M0L6resultS657[_M0L6_2atmpS1735] = _M0L6_2atmpS1736;
        _M0L6_2atmpS1739 = _M0Lm5indexS658;
        _M0Lm5indexS658 = _M0L6_2atmpS1739 + 2;
      } else {
        int32_t _M0L6_2atmpS1742 = _M0Lm5indexS658;
        int32_t _M0L6_2atmpS1745 = _M0Lm3expS663;
        int32_t _M0L6_2atmpS1744 = 48 + _M0L6_2atmpS1745;
        int32_t _M0L6_2atmpS1743 = _M0L6_2atmpS1744 & 0xff;
        int32_t _M0L6_2atmpS1746;
        if (
          _M0L6_2atmpS1742 < 0
          || _M0L6_2atmpS1742 >= Moonbit_array_length(_M0L6resultS657)
        ) {
          #line 587 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
          moonbit_panic();
        }
        _M0L6resultS657[_M0L6_2atmpS1742] = _M0L6_2atmpS1743;
        _M0L6_2atmpS1746 = _M0Lm5indexS658;
        _M0Lm5indexS658 = _M0L6_2atmpS1746 + 1;
      }
    }
    _M0L6_2atmpS1747 = _M0Lm5indexS658;
    #line 590 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _result_2267
    = _M0FPB19string__from__bytes(_M0L6resultS657, 0, _M0L6_2atmpS1747);
    moonbit_decref_cycle_free(_M0L6resultS657);
    return _result_2267;
  } else {
    int32_t _M0L6_2atmpS1756 = _M0Lm3expS663;
    int32_t _M0L6_2atmpS1819;
    moonbit_string_t _result_2273;
    if (_M0L6_2atmpS1756 < 0) {
      int32_t _M0L6_2atmpS1757 = _M0Lm5indexS658;
      int32_t _M0L6_2atmpS1759;
      int32_t _M0L6_2atmpS1758;
      int32_t _M0L6_2atmpS1760;
      int32_t _M0L1iS676;
      int32_t _M0L6_2atmpS1775;
      int32_t _M0L6_2atmpS1777;
      int32_t _M0L6_2atmpS1776;
      int32_t _M0L7currentS678;
      int32_t _M0L1iS679;
      uint64_t _M0L6outputS680;
      if (
        _M0L6_2atmpS1757 < 0
        || _M0L6_2atmpS1757 >= Moonbit_array_length(_M0L6resultS657)
      ) {
        #line 595 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS657[_M0L6_2atmpS1757] = 48;
      _M0L6_2atmpS1759 = _M0Lm5indexS658;
      _M0L6_2atmpS1758 = _M0L6_2atmpS1759 + 1;
      if (
        _M0L6_2atmpS1758 < 0
        || _M0L6_2atmpS1758 >= Moonbit_array_length(_M0L6resultS657)
      ) {
        #line 596 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS657[_M0L6_2atmpS1758] = 46;
      _M0L6_2atmpS1760 = _M0Lm5indexS658;
      _M0Lm5indexS658 = _M0L6_2atmpS1760 + 2;
      _M0L1iS676 = -1;
      while (1) {
        int32_t _M0L6_2atmpS1761 = _M0Lm3expS663;
        if (_M0L1iS676 > _M0L6_2atmpS1761) {
          int32_t _M0L6_2atmpS1764 = _M0Lm5indexS658;
          int32_t _M0L6_2atmpS1763 = _M0L6_2atmpS1764 - _M0L1iS676;
          int32_t _M0L6_2atmpS1762 = _M0L6_2atmpS1763 - 1;
          int32_t _M0L6_2atmpS1765;
          if (
            _M0L6_2atmpS1762 < 0
            || _M0L6_2atmpS1762 >= Moonbit_array_length(_M0L6resultS657)
          ) {
            #line 599 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
            moonbit_panic();
          }
          _M0L6resultS657[_M0L6_2atmpS1762] = 48;
          _M0L6_2atmpS1765 = _M0L1iS676 - 1;
          _M0L1iS676 = _M0L6_2atmpS1765;
          continue;
        }
        break;
      }
      _M0L6_2atmpS1775 = _M0Lm5indexS658;
      _M0L6_2atmpS1777 = _M0Lm3expS663;
      _M0L6_2atmpS1776 = -1 - _M0L6_2atmpS1777;
      _M0L7currentS678 = _M0L6_2atmpS1775 + _M0L6_2atmpS1776;
      _M0L1iS679 = 0;
      _M0L6outputS680 = _M0L6outputS660;
      while (1) {
        if (_M0L1iS679 < _M0L7olengthS662) {
          int32_t _M0L6_2atmpS1772 = _M0L7currentS678 + _M0L7olengthS662;
          int32_t _M0L6_2atmpS1771 = _M0L6_2atmpS1772 - _M0L1iS679;
          int32_t _M0L6_2atmpS1766 = _M0L6_2atmpS1771 - 1;
          uint64_t _M0L6_2atmpS1770 = _M0L6outputS680 % 10ull;
          int32_t _M0L6_2atmpS1769 = (int32_t)_M0L6_2atmpS1770;
          int32_t _M0L6_2atmpS1768 = 48 + _M0L6_2atmpS1769;
          int32_t _M0L6_2atmpS1767 = _M0L6_2atmpS1768 & 0xff;
          int32_t _M0L6_2atmpS1773;
          uint64_t _M0L6_2atmpS1774;
          if (
            _M0L6_2atmpS1766 < 0
            || _M0L6_2atmpS1766 >= Moonbit_array_length(_M0L6resultS657)
          ) {
            #line 603 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
            moonbit_panic();
          }
          _M0L6resultS657[_M0L6_2atmpS1766] = _M0L6_2atmpS1767;
          _M0L6_2atmpS1773 = _M0L1iS679 + 1;
          _M0L6_2atmpS1774 = _M0L6outputS680 / 10ull;
          _M0L1iS679 = _M0L6_2atmpS1773;
          _M0L6outputS680 = _M0L6_2atmpS1774;
          continue;
        }
        break;
      }
      _M0Lm5indexS658 = _M0L7currentS678 + _M0L7olengthS662;
    } else {
      int32_t _M0L6_2atmpS1779 = _M0Lm3expS663;
      int32_t _M0L6_2atmpS1778 = _M0L6_2atmpS1779 + 1;
      if (_M0L6_2atmpS1778 >= _M0L7olengthS662) {
        int32_t _M0L1iS682 = 0;
        uint64_t _M0L6outputS683 = _M0L6outputS660;
        int32_t _M0L6_2atmpS1790;
        int32_t _M0L6_2atmpS1795;
        int32_t _M0L7_2abindS685;
        int32_t _M0L1iS686;
        int32_t _M0L6_2atmpS1796;
        int32_t _M0L6_2atmpS1799;
        int32_t _M0L6_2atmpS1798;
        int32_t _M0L6_2atmpS1797;
        while (1) {
          if (_M0L1iS682 < _M0L7olengthS662) {
            int32_t _M0L6_2atmpS1787 = _M0Lm5indexS658;
            int32_t _M0L6_2atmpS1786 = _M0L6_2atmpS1787 + _M0L7olengthS662;
            int32_t _M0L6_2atmpS1785 = _M0L6_2atmpS1786 - _M0L1iS682;
            int32_t _M0L6_2atmpS1780 = _M0L6_2atmpS1785 - 1;
            uint64_t _M0L6_2atmpS1784 = _M0L6outputS683 % 10ull;
            int32_t _M0L6_2atmpS1783 = (int32_t)_M0L6_2atmpS1784;
            int32_t _M0L6_2atmpS1782 = 48 + _M0L6_2atmpS1783;
            int32_t _M0L6_2atmpS1781 = _M0L6_2atmpS1782 & 0xff;
            int32_t _M0L6_2atmpS1788;
            uint64_t _M0L6_2atmpS1789;
            if (
              _M0L6_2atmpS1780 < 0
              || _M0L6_2atmpS1780 >= Moonbit_array_length(_M0L6resultS657)
            ) {
              #line 610 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
              moonbit_panic();
            }
            _M0L6resultS657[_M0L6_2atmpS1780] = _M0L6_2atmpS1781;
            _M0L6_2atmpS1788 = _M0L1iS682 + 1;
            _M0L6_2atmpS1789 = _M0L6outputS683 / 10ull;
            _M0L1iS682 = _M0L6_2atmpS1788;
            _M0L6outputS683 = _M0L6_2atmpS1789;
            continue;
          }
          break;
        }
        _M0L6_2atmpS1790 = _M0Lm5indexS658;
        _M0Lm5indexS658 = _M0L6_2atmpS1790 + _M0L7olengthS662;
        _M0L6_2atmpS1795 = _M0Lm3expS663;
        _M0L7_2abindS685 = _M0L6_2atmpS1795 + 1;
        _M0L1iS686 = _M0L7olengthS662;
        while (1) {
          if (_M0L1iS686 < _M0L7_2abindS685) {
            int32_t _M0L6_2atmpS1793 = _M0Lm5indexS658;
            int32_t _M0L6_2atmpS1792 = _M0L6_2atmpS1793 + _M0L1iS686;
            int32_t _M0L6_2atmpS1791 = _M0L6_2atmpS1792 - _M0L7olengthS662;
            int32_t _M0L6_2atmpS1794;
            if (
              _M0L6_2atmpS1791 < 0
              || _M0L6_2atmpS1791 >= Moonbit_array_length(_M0L6resultS657)
            ) {
              #line 615 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
              moonbit_panic();
            }
            _M0L6resultS657[_M0L6_2atmpS1791] = 48;
            _M0L6_2atmpS1794 = _M0L1iS686 + 1;
            _M0L1iS686 = _M0L6_2atmpS1794;
            continue;
          }
          break;
        }
        _M0L6_2atmpS1796 = _M0Lm5indexS658;
        _M0L6_2atmpS1799 = _M0Lm3expS663;
        _M0L6_2atmpS1798 = _M0L6_2atmpS1799 + 1;
        _M0L6_2atmpS1797 = _M0L6_2atmpS1798 - _M0L7olengthS662;
        _M0Lm5indexS658 = _M0L6_2atmpS1796 + _M0L6_2atmpS1797;
      } else {
        int32_t _M0L6_2atmpS1816 = _M0Lm5indexS658;
        int32_t _M0L6_2atmpS1815 = _M0L6_2atmpS1816 + 1;
        int32_t _M0L1iS688 = 0;
        int32_t _M0L7currentS689 = _M0L6_2atmpS1815;
        uint64_t _M0L6outputS690 = _M0L6outputS660;
        int32_t _M0L6_2atmpS1817;
        int32_t _M0L6_2atmpS1818;
        while (1) {
          if (_M0L1iS688 < _M0L7olengthS662) {
            int32_t _M0L6_2atmpS1811 = _M0L7olengthS662 - _M0L1iS688;
            int32_t _M0L6_2atmpS1809 = _M0L6_2atmpS1811 - 1;
            int32_t _M0L6_2atmpS1810 = _M0Lm3expS663;
            int32_t _M0L7currentS691;
            int32_t _M0L6_2atmpS1806;
            int32_t _M0L6_2atmpS1805;
            int32_t _M0L6_2atmpS1800;
            uint64_t _M0L6_2atmpS1804;
            int32_t _M0L6_2atmpS1803;
            int32_t _M0L6_2atmpS1802;
            int32_t _M0L6_2atmpS1801;
            int32_t _M0L6_2atmpS1807;
            uint64_t _M0L6_2atmpS1808;
            if (_M0L6_2atmpS1809 == _M0L6_2atmpS1810) {
              int32_t _M0L6_2atmpS1814 = _M0L7currentS689 + _M0L7olengthS662;
              int32_t _M0L6_2atmpS1813 = _M0L6_2atmpS1814 - _M0L1iS688;
              int32_t _M0L6_2atmpS1812 = _M0L6_2atmpS1813 - 1;
              if (
                _M0L6_2atmpS1812 < 0
                || _M0L6_2atmpS1812 >= Moonbit_array_length(_M0L6resultS657)
              ) {
                #line 622 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
                moonbit_panic();
              }
              _M0L6resultS657[_M0L6_2atmpS1812] = 46;
              _M0L7currentS691 = _M0L7currentS689 - 1;
            } else {
              _M0L7currentS691 = _M0L7currentS689;
            }
            _M0L6_2atmpS1806 = _M0L7currentS691 + _M0L7olengthS662;
            _M0L6_2atmpS1805 = _M0L6_2atmpS1806 - _M0L1iS688;
            _M0L6_2atmpS1800 = _M0L6_2atmpS1805 - 1;
            _M0L6_2atmpS1804 = _M0L6outputS690 % 10ull;
            _M0L6_2atmpS1803 = (int32_t)_M0L6_2atmpS1804;
            _M0L6_2atmpS1802 = 48 + _M0L6_2atmpS1803;
            _M0L6_2atmpS1801 = _M0L6_2atmpS1802 & 0xff;
            if (
              _M0L6_2atmpS1800 < 0
              || _M0L6_2atmpS1800 >= Moonbit_array_length(_M0L6resultS657)
            ) {
              #line 627 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
              moonbit_panic();
            }
            _M0L6resultS657[_M0L6_2atmpS1800] = _M0L6_2atmpS1801;
            _M0L6_2atmpS1807 = _M0L1iS688 + 1;
            _M0L6_2atmpS1808 = _M0L6outputS690 / 10ull;
            _M0L1iS688 = _M0L6_2atmpS1807;
            _M0L7currentS689 = _M0L7currentS691;
            _M0L6outputS690 = _M0L6_2atmpS1808;
            continue;
          }
          break;
        }
        _M0L6_2atmpS1817 = _M0Lm5indexS658;
        _M0L6_2atmpS1818 = _M0L7olengthS662 + 1;
        _M0Lm5indexS658 = _M0L6_2atmpS1817 + _M0L6_2atmpS1818;
      }
    }
    _M0L6_2atmpS1819 = _M0Lm5indexS658;
    #line 632 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _result_2273
    = _M0FPB19string__from__bytes(_M0L6resultS657, 0, _M0L6_2atmpS1819);
    moonbit_decref_cycle_free(_M0L6resultS657);
    return _result_2273;
  }
}

struct _M0TPB17FloatingDecimal64* _M0FPB3d2d(
  uint64_t _M0L12ieeeMantissaS603,
  uint32_t _M0L12ieeeExponentS602
) {
  int32_t _M0Lm2e2S600;
  uint64_t _M0Lm2m2S601;
  uint64_t _M0L6_2atmpS1693;
  uint64_t _M0L6_2atmpS1692;
  int32_t _M0L4evenS604;
  uint64_t _M0L6_2atmpS1691;
  uint64_t _M0L2mvS605;
  int32_t _M0L7mmShiftS606;
  uint64_t _M0Lm2vrS607;
  uint64_t _M0Lm2vpS608;
  uint64_t _M0Lm2vmS609;
  int32_t _M0Lm3e10S610;
  int32_t _M0Lm17vmIsTrailingZerosS611;
  int32_t _M0Lm17vrIsTrailingZerosS612;
  int32_t _M0L6_2atmpS1593;
  int32_t _M0Lm7removedS631;
  int32_t _M0Lm16lastRemovedDigitS632;
  uint64_t _M0Lm6outputS633;
  int32_t _M0L6_2atmpS1689;
  int32_t _M0L6_2atmpS1690;
  int32_t _M0L3expS656;
  uint64_t _M0L6_2atmpS1688;
  struct _M0TPB17FloatingDecimal64* _block_2279;
  #line 347 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0Lm2e2S600 = 0;
  _M0Lm2m2S601 = 0ull;
  if (_M0L12ieeeExponentS602 == 0u) {
    _M0Lm2e2S600 = -1076;
    _M0Lm2m2S601 = _M0L12ieeeMantissaS603;
  } else {
    int32_t _M0L6_2atmpS1592 = *(int32_t*)&_M0L12ieeeExponentS602;
    int32_t _M0L6_2atmpS1591 = _M0L6_2atmpS1592 - 1023;
    int32_t _M0L6_2atmpS1590 = _M0L6_2atmpS1591 - 52;
    _M0Lm2e2S600 = _M0L6_2atmpS1590 - 2;
    _M0Lm2m2S601 = 4503599627370496ull | _M0L12ieeeMantissaS603;
  }
  _M0L6_2atmpS1693 = _M0Lm2m2S601;
  _M0L6_2atmpS1692 = _M0L6_2atmpS1693 & 1ull;
  _M0L4evenS604 = _M0L6_2atmpS1692 == 0ull;
  _M0L6_2atmpS1691 = _M0Lm2m2S601;
  _M0L2mvS605 = 4ull * _M0L6_2atmpS1691;
  _M0L7mmShiftS606
  = _M0L12ieeeMantissaS603 != 0ull || _M0L12ieeeExponentS602 <= 1u;
  _M0Lm2vrS607 = 0ull;
  _M0Lm2vpS608 = 0ull;
  _M0Lm2vmS609 = 0ull;
  _M0Lm3e10S610 = 0;
  _M0Lm17vmIsTrailingZerosS611 = 0;
  _M0Lm17vrIsTrailingZerosS612 = 0;
  _M0L6_2atmpS1593 = _M0Lm2e2S600;
  if (_M0L6_2atmpS1593 >= 0) {
    int32_t _M0L6_2atmpS1615 = _M0Lm2e2S600;
    int32_t _M0L6_2atmpS1611;
    int32_t _M0L6_2atmpS1614;
    int32_t _M0L6_2atmpS1613;
    int32_t _M0L6_2atmpS1612;
    int32_t _M0L1qS613;
    int32_t _M0L6_2atmpS1610;
    int32_t _M0L6_2atmpS1609;
    int32_t _M0L1kS614;
    int32_t _M0L6_2atmpS1608;
    int32_t _M0L6_2atmpS1607;
    int32_t _M0L6_2atmpS1606;
    int32_t _M0L1iS615;
    struct _M0TPB8Pow5Pair _M0L4pow5S616;
    uint64_t _M0L6_2atmpS1605;
    struct _M0TPB19MulShiftAll64Result _M0L7_2abindS617;
    uint64_t _M0L8_2avrOutS618;
    uint64_t _M0L8_2avpOutS619;
    uint64_t _M0L8_2avmOutS620;
    #line 383 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1611 = _M0FPB9log10Pow2(_M0L6_2atmpS1615);
    _M0L6_2atmpS1614 = _M0Lm2e2S600;
    _M0L6_2atmpS1613 = _M0L6_2atmpS1614 > 3;
    #line 383 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1612 = _M0MPC14bool4Bool7to__int(_M0L6_2atmpS1613);
    _M0L1qS613 = _M0L6_2atmpS1611 - _M0L6_2atmpS1612;
    _M0Lm3e10S610 = _M0L1qS613;
    #line 385 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1610 = _M0FPB8pow5bits(_M0L1qS613);
    _M0L6_2atmpS1609 = 125 + _M0L6_2atmpS1610;
    _M0L1kS614 = _M0L6_2atmpS1609 - 1;
    _M0L6_2atmpS1608 = _M0Lm2e2S600;
    _M0L6_2atmpS1607 = -_M0L6_2atmpS1608;
    _M0L6_2atmpS1606 = _M0L6_2atmpS1607 + _M0L1qS613;
    _M0L1iS615 = _M0L6_2atmpS1606 + _M0L1kS614;
    #line 387 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L4pow5S616 = _M0FPB22double__computeInvPow5(_M0L1qS613);
    _M0L6_2atmpS1605 = _M0Lm2m2S601;
    #line 388 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L7_2abindS617
    = _M0FPB13mulShiftAll64(_M0L6_2atmpS1605, _M0L4pow5S616, _M0L1iS615, _M0L7mmShiftS606);
    _M0L8_2avrOutS618 = _M0L7_2abindS617.$0;
    _M0L8_2avpOutS619 = _M0L7_2abindS617.$1;
    _M0L8_2avmOutS620 = _M0L7_2abindS617.$2;
    _M0Lm2vrS607 = _M0L8_2avrOutS618;
    _M0Lm2vpS608 = _M0L8_2avpOutS619;
    _M0Lm2vmS609 = _M0L8_2avmOutS620;
    if (_M0L1qS613 <= 21) {
      int32_t _M0L6_2atmpS1601 = (int32_t)_M0L2mvS605;
      uint64_t _M0L6_2atmpS1604 = _M0L2mvS605 / 5ull;
      int32_t _M0L6_2atmpS1603 = (int32_t)_M0L6_2atmpS1604;
      int32_t _M0L6_2atmpS1602 = 5 * _M0L6_2atmpS1603;
      int32_t _M0L6mvMod5S621 = _M0L6_2atmpS1601 - _M0L6_2atmpS1602;
      if (_M0L6mvMod5S621 == 0) {
        #line 400 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0Lm17vrIsTrailingZerosS612
        = _M0FPB18multipleOfPowerOf5(_M0L2mvS605, _M0L1qS613);
      } else if (_M0L4evenS604) {
        uint64_t _M0L6_2atmpS1595 = _M0L2mvS605 - 1ull;
        uint64_t _M0L6_2atmpS1596;
        uint64_t _M0L6_2atmpS1594;
        #line 406 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0L6_2atmpS1596 = _M0MPC14bool4Bool10to__uint64(_M0L7mmShiftS606);
        _M0L6_2atmpS1594 = _M0L6_2atmpS1595 - _M0L6_2atmpS1596;
        #line 405 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0Lm17vmIsTrailingZerosS611
        = _M0FPB18multipleOfPowerOf5(_M0L6_2atmpS1594, _M0L1qS613);
      } else {
        uint64_t _M0L6_2atmpS1597 = _M0Lm2vpS608;
        uint64_t _M0L6_2atmpS1600 = _M0L2mvS605 + 2ull;
        int32_t _M0L6_2atmpS1599;
        uint64_t _M0L6_2atmpS1598;
        #line 410 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0L6_2atmpS1599
        = _M0FPB18multipleOfPowerOf5(_M0L6_2atmpS1600, _M0L1qS613);
        #line 410 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0L6_2atmpS1598 = _M0MPC14bool4Bool10to__uint64(_M0L6_2atmpS1599);
        _M0Lm2vpS608 = _M0L6_2atmpS1597 - _M0L6_2atmpS1598;
      }
    }
  } else {
    int32_t _M0L6_2atmpS1629 = _M0Lm2e2S600;
    int32_t _M0L6_2atmpS1628 = -_M0L6_2atmpS1629;
    int32_t _M0L6_2atmpS1623;
    int32_t _M0L6_2atmpS1627;
    int32_t _M0L6_2atmpS1626;
    int32_t _M0L6_2atmpS1625;
    int32_t _M0L6_2atmpS1624;
    int32_t _M0L1qS622;
    int32_t _M0L6_2atmpS1616;
    int32_t _M0L6_2atmpS1622;
    int32_t _M0L6_2atmpS1621;
    int32_t _M0L1iS623;
    int32_t _M0L6_2atmpS1620;
    int32_t _M0L1kS624;
    int32_t _M0L1jS625;
    struct _M0TPB8Pow5Pair _M0L4pow5S626;
    uint64_t _M0L6_2atmpS1619;
    struct _M0TPB19MulShiftAll64Result _M0L7_2abindS627;
    uint64_t _M0L8_2avrOutS628;
    uint64_t _M0L8_2avpOutS629;
    uint64_t _M0L8_2avmOutS630;
    #line 415 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1623 = _M0FPB9log10Pow5(_M0L6_2atmpS1628);
    _M0L6_2atmpS1627 = _M0Lm2e2S600;
    _M0L6_2atmpS1626 = -_M0L6_2atmpS1627;
    _M0L6_2atmpS1625 = _M0L6_2atmpS1626 > 1;
    #line 415 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1624 = _M0MPC14bool4Bool7to__int(_M0L6_2atmpS1625);
    _M0L1qS622 = _M0L6_2atmpS1623 - _M0L6_2atmpS1624;
    _M0L6_2atmpS1616 = _M0Lm2e2S600;
    _M0Lm3e10S610 = _M0L1qS622 + _M0L6_2atmpS1616;
    _M0L6_2atmpS1622 = _M0Lm2e2S600;
    _M0L6_2atmpS1621 = -_M0L6_2atmpS1622;
    _M0L1iS623 = _M0L6_2atmpS1621 - _M0L1qS622;
    #line 418 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1620 = _M0FPB8pow5bits(_M0L1iS623);
    _M0L1kS624 = _M0L6_2atmpS1620 - 125;
    _M0L1jS625 = _M0L1qS622 - _M0L1kS624;
    #line 420 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L4pow5S626 = _M0FPB19double__computePow5(_M0L1iS623);
    _M0L6_2atmpS1619 = _M0Lm2m2S601;
    #line 421 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L7_2abindS627
    = _M0FPB13mulShiftAll64(_M0L6_2atmpS1619, _M0L4pow5S626, _M0L1jS625, _M0L7mmShiftS606);
    _M0L8_2avrOutS628 = _M0L7_2abindS627.$0;
    _M0L8_2avpOutS629 = _M0L7_2abindS627.$1;
    _M0L8_2avmOutS630 = _M0L7_2abindS627.$2;
    _M0Lm2vrS607 = _M0L8_2avrOutS628;
    _M0Lm2vpS608 = _M0L8_2avpOutS629;
    _M0Lm2vmS609 = _M0L8_2avmOutS630;
    if (_M0L1qS622 <= 1) {
      _M0Lm17vrIsTrailingZerosS612 = 1;
      if (_M0L4evenS604) {
        int32_t _M0L6_2atmpS1617;
        #line 432 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0L6_2atmpS1617 = _M0MPC14bool4Bool7to__int(_M0L7mmShiftS606);
        _M0Lm17vmIsTrailingZerosS611 = _M0L6_2atmpS1617 == 1;
      } else {
        uint64_t _M0L6_2atmpS1618 = _M0Lm2vpS608;
        _M0Lm2vpS608 = _M0L6_2atmpS1618 - 1ull;
      }
    } else if (_M0L1qS622 < 63) {
      #line 437 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0Lm17vrIsTrailingZerosS612
      = _M0FPB18multipleOfPowerOf2(_M0L2mvS605, _M0L1qS622);
    }
  }
  _M0Lm7removedS631 = 0;
  _M0Lm16lastRemovedDigitS632 = 0;
  _M0Lm6outputS633 = 0ull;
  if (_M0Lm17vmIsTrailingZerosS611 || _M0Lm17vrIsTrailingZerosS612) {
    int32_t _if__result_2276;
    uint64_t _M0L6_2atmpS1659;
    uint64_t _M0L6_2atmpS1665;
    uint64_t _M0L6_2atmpS1666;
    int32_t _if__result_2277;
    int32_t _M0L6_2atmpS1662;
    int64_t _M0L6_2atmpS1661;
    uint64_t _M0L6_2atmpS1660;
    while (1) {
      uint64_t _M0L6_2atmpS1642 = _M0Lm2vpS608;
      uint64_t _M0L7vpDiv10S634 = _M0L6_2atmpS1642 / 10ull;
      uint64_t _M0L6_2atmpS1641 = _M0Lm2vmS609;
      uint64_t _M0L7vmDiv10S635 = _M0L6_2atmpS1641 / 10ull;
      uint64_t _M0L6_2atmpS1640;
      int32_t _M0L6_2atmpS1637;
      int32_t _M0L6_2atmpS1639;
      int32_t _M0L6_2atmpS1638;
      int32_t _M0L7vmMod10S637;
      uint64_t _M0L6_2atmpS1636;
      uint64_t _M0L7vrDiv10S638;
      uint64_t _M0L6_2atmpS1635;
      int32_t _M0L6_2atmpS1632;
      int32_t _M0L6_2atmpS1634;
      int32_t _M0L6_2atmpS1633;
      int32_t _M0L7vrMod10S639;
      int32_t _M0L6_2atmpS1631;
      if (_M0L7vpDiv10S634 <= _M0L7vmDiv10S635) {
        break;
      }
      _M0L6_2atmpS1640 = _M0Lm2vmS609;
      _M0L6_2atmpS1637 = (int32_t)_M0L6_2atmpS1640;
      _M0L6_2atmpS1639 = (int32_t)_M0L7vmDiv10S635;
      _M0L6_2atmpS1638 = 10 * _M0L6_2atmpS1639;
      _M0L7vmMod10S637 = _M0L6_2atmpS1637 - _M0L6_2atmpS1638;
      _M0L6_2atmpS1636 = _M0Lm2vrS607;
      _M0L7vrDiv10S638 = _M0L6_2atmpS1636 / 10ull;
      _M0L6_2atmpS1635 = _M0Lm2vrS607;
      _M0L6_2atmpS1632 = (int32_t)_M0L6_2atmpS1635;
      _M0L6_2atmpS1634 = (int32_t)_M0L7vrDiv10S638;
      _M0L6_2atmpS1633 = 10 * _M0L6_2atmpS1634;
      _M0L7vrMod10S639 = _M0L6_2atmpS1632 - _M0L6_2atmpS1633;
      _M0Lm17vmIsTrailingZerosS611
      = _M0Lm17vmIsTrailingZerosS611 && _M0L7vmMod10S637 == 0;
      if (_M0Lm17vrIsTrailingZerosS612) {
        int32_t _M0L6_2atmpS1630 = _M0Lm16lastRemovedDigitS632;
        _M0Lm17vrIsTrailingZerosS612 = _M0L6_2atmpS1630 == 0;
      } else {
        _M0Lm17vrIsTrailingZerosS612 = 0;
      }
      _M0Lm16lastRemovedDigitS632 = _M0L7vrMod10S639;
      _M0Lm2vrS607 = _M0L7vrDiv10S638;
      _M0Lm2vpS608 = _M0L7vpDiv10S634;
      _M0Lm2vmS609 = _M0L7vmDiv10S635;
      _M0L6_2atmpS1631 = _M0Lm7removedS631;
      _M0Lm7removedS631 = _M0L6_2atmpS1631 + 1;
      continue;
      break;
    }
    if (_M0Lm17vmIsTrailingZerosS611) {
      while (1) {
        uint64_t _M0L6_2atmpS1655 = _M0Lm2vmS609;
        uint64_t _M0L7vmDiv10S640 = _M0L6_2atmpS1655 / 10ull;
        uint64_t _M0L6_2atmpS1654 = _M0Lm2vmS609;
        int32_t _M0L6_2atmpS1651 = (int32_t)_M0L6_2atmpS1654;
        int32_t _M0L6_2atmpS1653 = (int32_t)_M0L7vmDiv10S640;
        int32_t _M0L6_2atmpS1652 = 10 * _M0L6_2atmpS1653;
        int32_t _M0L7vmMod10S641 = _M0L6_2atmpS1651 - _M0L6_2atmpS1652;
        uint64_t _M0L6_2atmpS1650;
        uint64_t _M0L7vpDiv10S643;
        uint64_t _M0L6_2atmpS1649;
        uint64_t _M0L7vrDiv10S644;
        uint64_t _M0L6_2atmpS1648;
        int32_t _M0L6_2atmpS1645;
        int32_t _M0L6_2atmpS1647;
        int32_t _M0L6_2atmpS1646;
        int32_t _M0L7vrMod10S645;
        int32_t _M0L6_2atmpS1644;
        if (_M0L7vmMod10S641 != 0) {
          break;
        }
        _M0L6_2atmpS1650 = _M0Lm2vpS608;
        _M0L7vpDiv10S643 = _M0L6_2atmpS1650 / 10ull;
        _M0L6_2atmpS1649 = _M0Lm2vrS607;
        _M0L7vrDiv10S644 = _M0L6_2atmpS1649 / 10ull;
        _M0L6_2atmpS1648 = _M0Lm2vrS607;
        _M0L6_2atmpS1645 = (int32_t)_M0L6_2atmpS1648;
        _M0L6_2atmpS1647 = (int32_t)_M0L7vrDiv10S644;
        _M0L6_2atmpS1646 = 10 * _M0L6_2atmpS1647;
        _M0L7vrMod10S645 = _M0L6_2atmpS1645 - _M0L6_2atmpS1646;
        if (_M0Lm17vrIsTrailingZerosS612) {
          int32_t _M0L6_2atmpS1643 = _M0Lm16lastRemovedDigitS632;
          _M0Lm17vrIsTrailingZerosS612 = _M0L6_2atmpS1643 == 0;
        } else {
          _M0Lm17vrIsTrailingZerosS612 = 0;
        }
        _M0Lm16lastRemovedDigitS632 = _M0L7vrMod10S645;
        _M0Lm2vrS607 = _M0L7vrDiv10S644;
        _M0Lm2vpS608 = _M0L7vpDiv10S643;
        _M0Lm2vmS609 = _M0L7vmDiv10S640;
        _M0L6_2atmpS1644 = _M0Lm7removedS631;
        _M0Lm7removedS631 = _M0L6_2atmpS1644 + 1;
        continue;
        break;
      }
    }
    if (_M0Lm17vrIsTrailingZerosS612) {
      int32_t _M0L6_2atmpS1658 = _M0Lm16lastRemovedDigitS632;
      if (_M0L6_2atmpS1658 == 5) {
        uint64_t _M0L6_2atmpS1657 = _M0Lm2vrS607;
        uint64_t _M0L6_2atmpS1656 = _M0L6_2atmpS1657 % 2ull;
        _if__result_2276 = _M0L6_2atmpS1656 == 0ull;
      } else {
        _if__result_2276 = 0;
      }
    } else {
      _if__result_2276 = 0;
    }
    if (_if__result_2276) {
      _M0Lm16lastRemovedDigitS632 = 4;
    }
    _M0L6_2atmpS1659 = _M0Lm2vrS607;
    _M0L6_2atmpS1665 = _M0Lm2vrS607;
    _M0L6_2atmpS1666 = _M0Lm2vmS609;
    if (_M0L6_2atmpS1665 == _M0L6_2atmpS1666) {
      if (!_M0L4evenS604) {
        _if__result_2277 = 1;
      } else {
        int32_t _M0L6_2atmpS1664 = _M0Lm17vmIsTrailingZerosS611;
        _if__result_2277 = !_M0L6_2atmpS1664;
      }
    } else {
      _if__result_2277 = 0;
    }
    if (_if__result_2277) {
      _M0L6_2atmpS1662 = 1;
    } else {
      int32_t _M0L6_2atmpS1663 = _M0Lm16lastRemovedDigitS632;
      _M0L6_2atmpS1662 = _M0L6_2atmpS1663 >= 5;
    }
    #line 487 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1661 = _M0MPC14bool4Bool9to__int64(_M0L6_2atmpS1662);
    _M0L6_2atmpS1660 = *(uint64_t*)&_M0L6_2atmpS1661;
    _M0Lm6outputS633 = _M0L6_2atmpS1659 + _M0L6_2atmpS1660;
  } else {
    int32_t _M0Lm7roundUpS646 = 0;
    uint64_t _M0L6_2atmpS1687 = _M0Lm2vpS608;
    uint64_t _M0L8vpDiv100S647 = _M0L6_2atmpS1687 / 100ull;
    uint64_t _M0L6_2atmpS1686 = _M0Lm2vmS609;
    uint64_t _M0L8vmDiv100S648 = _M0L6_2atmpS1686 / 100ull;
    uint64_t _M0L6_2atmpS1681;
    uint64_t _M0L6_2atmpS1684;
    uint64_t _M0L6_2atmpS1685;
    int32_t _M0L6_2atmpS1683;
    uint64_t _M0L6_2atmpS1682;
    if (_M0L8vpDiv100S647 > _M0L8vmDiv100S648) {
      uint64_t _M0L6_2atmpS1672 = _M0Lm2vrS607;
      uint64_t _M0L8vrDiv100S649 = _M0L6_2atmpS1672 / 100ull;
      uint64_t _M0L6_2atmpS1671 = _M0Lm2vrS607;
      int32_t _M0L6_2atmpS1668 = (int32_t)_M0L6_2atmpS1671;
      int32_t _M0L6_2atmpS1670 = (int32_t)_M0L8vrDiv100S649;
      int32_t _M0L6_2atmpS1669 = 100 * _M0L6_2atmpS1670;
      int32_t _M0L8vrMod100S650 = _M0L6_2atmpS1668 - _M0L6_2atmpS1669;
      int32_t _M0L6_2atmpS1667;
      _M0Lm7roundUpS646 = _M0L8vrMod100S650 >= 50;
      _M0Lm2vrS607 = _M0L8vrDiv100S649;
      _M0Lm2vpS608 = _M0L8vpDiv100S647;
      _M0Lm2vmS609 = _M0L8vmDiv100S648;
      _M0L6_2atmpS1667 = _M0Lm7removedS631;
      _M0Lm7removedS631 = _M0L6_2atmpS1667 + 2;
    }
    while (1) {
      uint64_t _M0L6_2atmpS1680 = _M0Lm2vpS608;
      uint64_t _M0L7vpDiv10S651 = _M0L6_2atmpS1680 / 10ull;
      uint64_t _M0L6_2atmpS1679 = _M0Lm2vmS609;
      uint64_t _M0L7vmDiv10S652 = _M0L6_2atmpS1679 / 10ull;
      uint64_t _M0L6_2atmpS1678;
      uint64_t _M0L7vrDiv10S654;
      uint64_t _M0L6_2atmpS1677;
      int32_t _M0L6_2atmpS1674;
      int32_t _M0L6_2atmpS1676;
      int32_t _M0L6_2atmpS1675;
      int32_t _M0L7vrMod10S655;
      int32_t _M0L6_2atmpS1673;
      if (_M0L7vpDiv10S651 <= _M0L7vmDiv10S652) {
        break;
      }
      _M0L6_2atmpS1678 = _M0Lm2vrS607;
      _M0L7vrDiv10S654 = _M0L6_2atmpS1678 / 10ull;
      _M0L6_2atmpS1677 = _M0Lm2vrS607;
      _M0L6_2atmpS1674 = (int32_t)_M0L6_2atmpS1677;
      _M0L6_2atmpS1676 = (int32_t)_M0L7vrDiv10S654;
      _M0L6_2atmpS1675 = 10 * _M0L6_2atmpS1676;
      _M0L7vrMod10S655 = _M0L6_2atmpS1674 - _M0L6_2atmpS1675;
      _M0Lm7roundUpS646 = _M0L7vrMod10S655 >= 5;
      _M0Lm2vrS607 = _M0L7vrDiv10S654;
      _M0Lm2vpS608 = _M0L7vpDiv10S651;
      _M0Lm2vmS609 = _M0L7vmDiv10S652;
      _M0L6_2atmpS1673 = _M0Lm7removedS631;
      _M0Lm7removedS631 = _M0L6_2atmpS1673 + 1;
      continue;
      break;
    }
    _M0L6_2atmpS1681 = _M0Lm2vrS607;
    _M0L6_2atmpS1684 = _M0Lm2vrS607;
    _M0L6_2atmpS1685 = _M0Lm2vmS609;
    _M0L6_2atmpS1683
    = _M0L6_2atmpS1684 == _M0L6_2atmpS1685 || _M0Lm7roundUpS646;
    #line 522 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1682 = _M0MPC14bool4Bool10to__uint64(_M0L6_2atmpS1683);
    _M0Lm6outputS633 = _M0L6_2atmpS1681 + _M0L6_2atmpS1682;
  }
  _M0L6_2atmpS1689 = _M0Lm3e10S610;
  _M0L6_2atmpS1690 = _M0Lm7removedS631;
  _M0L3expS656 = _M0L6_2atmpS1689 + _M0L6_2atmpS1690;
  _M0L6_2atmpS1688 = _M0Lm6outputS633;
  _block_2279
  = (struct _M0TPB17FloatingDecimal64*)moonbit_malloc(sizeof(struct _M0TPB17FloatingDecimal64));
  Moonbit_object_header(_block_2279)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_2279->$0 = _M0L6_2atmpS1688;
  _block_2279->$1 = _M0L3expS656;
  return _block_2279;
}

uint64_t _M0MPC14bool4Bool10to__uint64(int32_t _M0L4selfS599) {
  #line 110 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bool.mbt"
  if (_M0L4selfS599) {
    return 1ull;
  } else {
    return 0ull;
  }
}

int64_t _M0MPC14bool4Bool9to__int64(int32_t _M0L4selfS598) {
  #line 58 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bool.mbt"
  if (_M0L4selfS598) {
    return 1ll;
  } else {
    return 0ll;
  }
}

int32_t _M0MPC14bool4Bool7to__int(int32_t _M0L4selfS597) {
  #line 32 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bool.mbt"
  if (_M0L4selfS597) {
    return 1;
  } else {
    return 0;
  }
}

int32_t _M0FPB17decimal__length17(uint64_t _M0L1vS596) {
  #line 280 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  if (_M0L1vS596 >= 10000000000000000ull) {
    return 17;
  }
  if (_M0L1vS596 >= 1000000000000000ull) {
    return 16;
  }
  if (_M0L1vS596 >= 100000000000000ull) {
    return 15;
  }
  if (_M0L1vS596 >= 10000000000000ull) {
    return 14;
  }
  if (_M0L1vS596 >= 1000000000000ull) {
    return 13;
  }
  if (_M0L1vS596 >= 100000000000ull) {
    return 12;
  }
  if (_M0L1vS596 >= 10000000000ull) {
    return 11;
  }
  if (_M0L1vS596 >= 1000000000ull) {
    return 10;
  }
  if (_M0L1vS596 >= 100000000ull) {
    return 9;
  }
  if (_M0L1vS596 >= 10000000ull) {
    return 8;
  }
  if (_M0L1vS596 >= 1000000ull) {
    return 7;
  }
  if (_M0L1vS596 >= 100000ull) {
    return 6;
  }
  if (_M0L1vS596 >= 10000ull) {
    return 5;
  }
  if (_M0L1vS596 >= 1000ull) {
    return 4;
  }
  if (_M0L1vS596 >= 100ull) {
    return 3;
  }
  if (_M0L1vS596 >= 10ull) {
    return 2;
  }
  return 1;
}

struct _M0TPB8Pow5Pair _M0FPB22double__computeInvPow5(int32_t _M0L1iS579) {
  int32_t _M0L6_2atmpS1589;
  int32_t _M0L6_2atmpS1588;
  int32_t _M0L4baseS578;
  int32_t _M0L5base2S580;
  int32_t _M0L6offsetS581;
  int32_t _M0L6_2atmpS1587;
  uint64_t _M0L4mul0S582;
  int32_t _M0L6_2atmpS1586;
  int32_t _M0L6_2atmpS1585;
  uint64_t _M0L4mul1S583;
  uint64_t _M0L1mS584;
  struct _M0TPB7Umul128 _M0L7_2abindS585;
  uint64_t _M0L7_2alow1S586;
  uint64_t _M0L8_2ahigh1S587;
  struct _M0TPB7Umul128 _M0L7_2abindS588;
  uint64_t _M0L7_2alow0S589;
  uint64_t _M0L8_2ahigh0S590;
  uint64_t _M0L3sumS591;
  uint64_t _M0Lm5high1S592;
  int32_t _M0L6_2atmpS1583;
  int32_t _M0L6_2atmpS1584;
  int32_t _M0L5deltaS593;
  uint64_t _M0L6_2atmpS1582;
  uint64_t _M0L6_2atmpS1574;
  int32_t _M0L6_2atmpS1581;
  uint32_t _M0L6_2atmpS1578;
  int32_t _M0L6_2atmpS1580;
  int32_t _M0L6_2atmpS1579;
  uint32_t _M0L6_2atmpS1577;
  uint32_t _M0L6_2atmpS1576;
  uint64_t _M0L6_2atmpS1575;
  uint64_t _M0L1aS594;
  uint64_t _M0L6_2atmpS1573;
  uint64_t _M0L1bS595;
  #line 239 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1589 = _M0L1iS579 + 26;
  _M0L6_2atmpS1588 = _M0L6_2atmpS1589 - 1;
  _M0L4baseS578 = _M0L6_2atmpS1588 / 26;
  _M0L5base2S580 = _M0L4baseS578 * 26;
  _M0L6offsetS581 = _M0L5base2S580 - _M0L1iS579;
  _M0L6_2atmpS1587 = _M0L4baseS578 * 2;
  #line 243 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4mul0S582
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB26gDOUBLE__POW5__INV__SPLIT2, _M0L6_2atmpS1587);
  _M0L6_2atmpS1586 = _M0L4baseS578 * 2;
  _M0L6_2atmpS1585 = _M0L6_2atmpS1586 + 1;
  #line 244 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4mul1S583
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB26gDOUBLE__POW5__INV__SPLIT2, _M0L6_2atmpS1585);
  if (_M0L6offsetS581 == 0) {
    return (struct _M0TPB8Pow5Pair){.$0 = _M0L4mul0S582, .$1 = _M0L4mul1S583};
  }
  #line 248 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L1mS584
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB20gDOUBLE__POW5__TABLE, _M0L6offsetS581);
  #line 249 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS585 = _M0FPB7umul128(_M0L1mS584, _M0L4mul1S583);
  _M0L7_2alow1S586 = _M0L7_2abindS585.$0;
  _M0L8_2ahigh1S587 = _M0L7_2abindS585.$1;
  #line 250 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS588 = _M0FPB7umul128(_M0L1mS584, _M0L4mul0S582);
  _M0L7_2alow0S589 = _M0L7_2abindS588.$0;
  _M0L8_2ahigh0S590 = _M0L7_2abindS588.$1;
  _M0L3sumS591 = _M0L8_2ahigh0S590 + _M0L7_2alow1S586;
  _M0Lm5high1S592 = _M0L8_2ahigh1S587;
  if (_M0L3sumS591 < _M0L8_2ahigh0S590) {
    uint64_t _M0L6_2atmpS1572 = _M0Lm5high1S592;
    _M0Lm5high1S592 = _M0L6_2atmpS1572 + 1ull;
  }
  #line 256 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1583 = _M0FPB8pow5bits(_M0L5base2S580);
  #line 256 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1584 = _M0FPB8pow5bits(_M0L1iS579);
  _M0L5deltaS593 = _M0L6_2atmpS1583 - _M0L6_2atmpS1584;
  #line 257 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1582
  = _M0FPB13shiftright128(_M0L7_2alow0S589, _M0L3sumS591, _M0L5deltaS593);
  _M0L6_2atmpS1574 = _M0L6_2atmpS1582 + 1ull;
  _M0L6_2atmpS1581 = _M0L1iS579 / 16;
  #line 259 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1578
  = _M0MPC15array13ReadOnlyArray2atGjE(_M0FPB19gPOW5__INV__OFFSETS, _M0L6_2atmpS1581);
  _M0L6_2atmpS1580 = _M0L1iS579 % 16;
  _M0L6_2atmpS1579 = _M0L6_2atmpS1580 << 1;
  _M0L6_2atmpS1577 = _M0L6_2atmpS1578 >> (_M0L6_2atmpS1579 & 31);
  _M0L6_2atmpS1576 = _M0L6_2atmpS1577 & 3u;
  #line 259 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1575 = _M0MPC14uint4UInt10to__uint64(_M0L6_2atmpS1576);
  _M0L1aS594 = _M0L6_2atmpS1574 + _M0L6_2atmpS1575;
  _M0L6_2atmpS1573 = _M0Lm5high1S592;
  #line 260 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L1bS595
  = _M0FPB13shiftright128(_M0L3sumS591, _M0L6_2atmpS1573, _M0L5deltaS593);
  return (struct _M0TPB8Pow5Pair){.$0 = _M0L1aS594, .$1 = _M0L1bS595};
}

struct _M0TPB8Pow5Pair _M0FPB19double__computePow5(int32_t _M0L1iS561) {
  int32_t _M0L4baseS560;
  int32_t _M0L5base2S562;
  int32_t _M0L6offsetS563;
  int32_t _M0L6_2atmpS1571;
  uint64_t _M0L4mul0S564;
  int32_t _M0L6_2atmpS1570;
  int32_t _M0L6_2atmpS1569;
  uint64_t _M0L4mul1S565;
  uint64_t _M0L1mS566;
  struct _M0TPB7Umul128 _M0L7_2abindS567;
  uint64_t _M0L7_2alow1S568;
  uint64_t _M0L8_2ahigh1S569;
  struct _M0TPB7Umul128 _M0L7_2abindS570;
  uint64_t _M0L7_2alow0S571;
  uint64_t _M0L8_2ahigh0S572;
  uint64_t _M0L3sumS573;
  uint64_t _M0Lm5high1S574;
  int32_t _M0L6_2atmpS1567;
  int32_t _M0L6_2atmpS1568;
  int32_t _M0L5deltaS575;
  uint64_t _M0L6_2atmpS1559;
  int32_t _M0L6_2atmpS1566;
  uint32_t _M0L6_2atmpS1563;
  int32_t _M0L6_2atmpS1565;
  int32_t _M0L6_2atmpS1564;
  uint32_t _M0L6_2atmpS1562;
  uint32_t _M0L6_2atmpS1561;
  uint64_t _M0L6_2atmpS1560;
  uint64_t _M0L1aS576;
  uint64_t _M0L6_2atmpS1558;
  uint64_t _M0L1bS577;
  #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4baseS560 = _M0L1iS561 / 26;
  _M0L5base2S562 = _M0L4baseS560 * 26;
  _M0L6offsetS563 = _M0L1iS561 - _M0L5base2S562;
  _M0L6_2atmpS1571 = _M0L4baseS560 * 2;
  #line 217 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4mul0S564
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB21gDOUBLE__POW5__SPLIT2, _M0L6_2atmpS1571);
  _M0L6_2atmpS1570 = _M0L4baseS560 * 2;
  _M0L6_2atmpS1569 = _M0L6_2atmpS1570 + 1;
  #line 218 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4mul1S565
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB21gDOUBLE__POW5__SPLIT2, _M0L6_2atmpS1569);
  if (_M0L6offsetS563 == 0) {
    return (struct _M0TPB8Pow5Pair){.$0 = _M0L4mul0S564, .$1 = _M0L4mul1S565};
  }
  #line 222 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L1mS566
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB20gDOUBLE__POW5__TABLE, _M0L6offsetS563);
  #line 223 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS567 = _M0FPB7umul128(_M0L1mS566, _M0L4mul1S565);
  _M0L7_2alow1S568 = _M0L7_2abindS567.$0;
  _M0L8_2ahigh1S569 = _M0L7_2abindS567.$1;
  #line 224 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS570 = _M0FPB7umul128(_M0L1mS566, _M0L4mul0S564);
  _M0L7_2alow0S571 = _M0L7_2abindS570.$0;
  _M0L8_2ahigh0S572 = _M0L7_2abindS570.$1;
  _M0L3sumS573 = _M0L8_2ahigh0S572 + _M0L7_2alow1S568;
  _M0Lm5high1S574 = _M0L8_2ahigh1S569;
  if (_M0L3sumS573 < _M0L8_2ahigh0S572) {
    uint64_t _M0L6_2atmpS1557 = _M0Lm5high1S574;
    _M0Lm5high1S574 = _M0L6_2atmpS1557 + 1ull;
  }
  #line 230 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1567 = _M0FPB8pow5bits(_M0L1iS561);
  #line 230 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1568 = _M0FPB8pow5bits(_M0L5base2S562);
  _M0L5deltaS575 = _M0L6_2atmpS1567 - _M0L6_2atmpS1568;
  #line 231 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1559
  = _M0FPB13shiftright128(_M0L7_2alow0S571, _M0L3sumS573, _M0L5deltaS575);
  _M0L6_2atmpS1566 = _M0L1iS561 / 16;
  #line 232 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1563
  = _M0MPC15array13ReadOnlyArray2atGjE(_M0FPB14gPOW5__OFFSETS, _M0L6_2atmpS1566);
  _M0L6_2atmpS1565 = _M0L1iS561 % 16;
  _M0L6_2atmpS1564 = _M0L6_2atmpS1565 << 1;
  _M0L6_2atmpS1562 = _M0L6_2atmpS1563 >> (_M0L6_2atmpS1564 & 31);
  _M0L6_2atmpS1561 = _M0L6_2atmpS1562 & 3u;
  #line 232 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1560 = _M0MPC14uint4UInt10to__uint64(_M0L6_2atmpS1561);
  _M0L1aS576 = _M0L6_2atmpS1559 + _M0L6_2atmpS1560;
  _M0L6_2atmpS1558 = _M0Lm5high1S574;
  #line 233 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L1bS577
  = _M0FPB13shiftright128(_M0L3sumS573, _M0L6_2atmpS1558, _M0L5deltaS575);
  return (struct _M0TPB8Pow5Pair){.$0 = _M0L1aS576, .$1 = _M0L1bS577};
}

struct _M0TPB19MulShiftAll64Result _M0FPB13mulShiftAll64(
  uint64_t _M0L1mS534,
  struct _M0TPB8Pow5Pair _M0L3mulS531,
  int32_t _M0L1jS547,
  int32_t _M0L7mmShiftS549
) {
  uint64_t _M0L7_2amul0S530;
  uint64_t _M0L7_2amul1S532;
  uint64_t _M0L1mS533;
  struct _M0TPB7Umul128 _M0L7_2abindS535;
  uint64_t _M0L5_2aloS536;
  uint64_t _M0L6_2atmpS537;
  struct _M0TPB7Umul128 _M0L7_2abindS538;
  uint64_t _M0L6_2alo2S539;
  uint64_t _M0L6_2ahi2S540;
  uint64_t _M0L3midS541;
  uint64_t _M0L6_2atmpS1556;
  uint64_t _M0L2hiS542;
  uint64_t _M0L3lo2S543;
  uint64_t _M0L6_2atmpS1554;
  uint64_t _M0L6_2atmpS1555;
  uint64_t _M0L4mid2S544;
  uint64_t _M0L6_2atmpS1553;
  uint64_t _M0L3hi2S545;
  int32_t _M0L6_2atmpS1552;
  int32_t _M0L6_2atmpS1551;
  uint64_t _M0L2vpS546;
  uint64_t _M0Lm2vmS548;
  int32_t _M0L6_2atmpS1550;
  int32_t _M0L6_2atmpS1549;
  uint64_t _M0L2vrS559;
  uint64_t _M0L6_2atmpS1548;
  #line 129 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2amul0S530 = _M0L3mulS531.$0;
  _M0L7_2amul1S532 = _M0L3mulS531.$1;
  _M0L1mS533 = _M0L1mS534 << 1;
  #line 137 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS535 = _M0FPB7umul128(_M0L1mS533, _M0L7_2amul0S530);
  _M0L5_2aloS536 = _M0L7_2abindS535.$0;
  _M0L6_2atmpS537 = _M0L7_2abindS535.$1;
  #line 138 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS538 = _M0FPB7umul128(_M0L1mS533, _M0L7_2amul1S532);
  _M0L6_2alo2S539 = _M0L7_2abindS538.$0;
  _M0L6_2ahi2S540 = _M0L7_2abindS538.$1;
  _M0L3midS541 = _M0L6_2atmpS537 + _M0L6_2alo2S539;
  if (_M0L3midS541 < _M0L6_2atmpS537) {
    _M0L6_2atmpS1556 = 1ull;
  } else {
    _M0L6_2atmpS1556 = 0ull;
  }
  _M0L2hiS542 = _M0L6_2ahi2S540 + _M0L6_2atmpS1556;
  _M0L3lo2S543 = _M0L5_2aloS536 + _M0L7_2amul0S530;
  _M0L6_2atmpS1554 = _M0L3midS541 + _M0L7_2amul1S532;
  if (_M0L3lo2S543 < _M0L5_2aloS536) {
    _M0L6_2atmpS1555 = 1ull;
  } else {
    _M0L6_2atmpS1555 = 0ull;
  }
  _M0L4mid2S544 = _M0L6_2atmpS1554 + _M0L6_2atmpS1555;
  if (_M0L4mid2S544 < _M0L3midS541) {
    _M0L6_2atmpS1553 = 1ull;
  } else {
    _M0L6_2atmpS1553 = 0ull;
  }
  _M0L3hi2S545 = _M0L2hiS542 + _M0L6_2atmpS1553;
  _M0L6_2atmpS1552 = _M0L1jS547 - 64;
  _M0L6_2atmpS1551 = _M0L6_2atmpS1552 - 1;
  #line 144 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L2vpS546
  = _M0FPB13shiftright128(_M0L4mid2S544, _M0L3hi2S545, _M0L6_2atmpS1551);
  _M0Lm2vmS548 = 0ull;
  if (_M0L7mmShiftS549) {
    uint64_t _M0L3lo3S550 = _M0L5_2aloS536 - _M0L7_2amul0S530;
    uint64_t _M0L6_2atmpS1538 = _M0L3midS541 - _M0L7_2amul1S532;
    uint64_t _M0L6_2atmpS1539;
    uint64_t _M0L4mid3S551;
    uint64_t _M0L6_2atmpS1537;
    uint64_t _M0L3hi3S552;
    int32_t _M0L6_2atmpS1536;
    int32_t _M0L6_2atmpS1535;
    if (_M0L5_2aloS536 < _M0L3lo3S550) {
      _M0L6_2atmpS1539 = 1ull;
    } else {
      _M0L6_2atmpS1539 = 0ull;
    }
    _M0L4mid3S551 = _M0L6_2atmpS1538 - _M0L6_2atmpS1539;
    if (_M0L3midS541 < _M0L4mid3S551) {
      _M0L6_2atmpS1537 = 1ull;
    } else {
      _M0L6_2atmpS1537 = 0ull;
    }
    _M0L3hi3S552 = _M0L2hiS542 - _M0L6_2atmpS1537;
    _M0L6_2atmpS1536 = _M0L1jS547 - 64;
    _M0L6_2atmpS1535 = _M0L6_2atmpS1536 - 1;
    #line 150 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0Lm2vmS548
    = _M0FPB13shiftright128(_M0L4mid3S551, _M0L3hi3S552, _M0L6_2atmpS1535);
  } else {
    uint64_t _M0L3lo3S553 = _M0L5_2aloS536 + _M0L5_2aloS536;
    uint64_t _M0L6_2atmpS1546 = _M0L3midS541 + _M0L3midS541;
    uint64_t _M0L6_2atmpS1547;
    uint64_t _M0L4mid3S554;
    uint64_t _M0L6_2atmpS1544;
    uint64_t _M0L6_2atmpS1545;
    uint64_t _M0L3hi3S555;
    uint64_t _M0L3lo4S556;
    uint64_t _M0L6_2atmpS1542;
    uint64_t _M0L6_2atmpS1543;
    uint64_t _M0L4mid4S557;
    uint64_t _M0L6_2atmpS1541;
    uint64_t _M0L3hi4S558;
    int32_t _M0L6_2atmpS1540;
    if (_M0L3lo3S553 < _M0L5_2aloS536) {
      _M0L6_2atmpS1547 = 1ull;
    } else {
      _M0L6_2atmpS1547 = 0ull;
    }
    _M0L4mid3S554 = _M0L6_2atmpS1546 + _M0L6_2atmpS1547;
    _M0L6_2atmpS1544 = _M0L2hiS542 + _M0L2hiS542;
    if (_M0L4mid3S554 < _M0L3midS541) {
      _M0L6_2atmpS1545 = 1ull;
    } else {
      _M0L6_2atmpS1545 = 0ull;
    }
    _M0L3hi3S555 = _M0L6_2atmpS1544 + _M0L6_2atmpS1545;
    _M0L3lo4S556 = _M0L3lo3S553 - _M0L7_2amul0S530;
    _M0L6_2atmpS1542 = _M0L4mid3S554 - _M0L7_2amul1S532;
    if (_M0L3lo3S553 < _M0L3lo4S556) {
      _M0L6_2atmpS1543 = 1ull;
    } else {
      _M0L6_2atmpS1543 = 0ull;
    }
    _M0L4mid4S557 = _M0L6_2atmpS1542 - _M0L6_2atmpS1543;
    if (_M0L4mid3S554 < _M0L4mid4S557) {
      _M0L6_2atmpS1541 = 1ull;
    } else {
      _M0L6_2atmpS1541 = 0ull;
    }
    _M0L3hi4S558 = _M0L3hi3S555 - _M0L6_2atmpS1541;
    _M0L6_2atmpS1540 = _M0L1jS547 - 64;
    #line 158 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0Lm2vmS548
    = _M0FPB13shiftright128(_M0L4mid4S557, _M0L3hi4S558, _M0L6_2atmpS1540);
  }
  _M0L6_2atmpS1550 = _M0L1jS547 - 64;
  _M0L6_2atmpS1549 = _M0L6_2atmpS1550 - 1;
  #line 160 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L2vrS559
  = _M0FPB13shiftright128(_M0L3midS541, _M0L2hiS542, _M0L6_2atmpS1549);
  _M0L6_2atmpS1548 = _M0Lm2vmS548;
  return (struct _M0TPB19MulShiftAll64Result){.$0 = _M0L2vrS559,
                                                .$1 = _M0L2vpS546,
                                                .$2 = _M0L6_2atmpS1548};
}

int32_t _M0FPB18multipleOfPowerOf2(
  uint64_t _M0L5valueS528,
  int32_t _M0L1pS529
) {
  uint64_t _M0L6_2atmpS1534;
  uint64_t _M0L6_2atmpS1533;
  uint64_t _M0L6_2atmpS1532;
  #line 124 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1534 = 1ull << (_M0L1pS529 & 63);
  _M0L6_2atmpS1533 = _M0L6_2atmpS1534 - 1ull;
  _M0L6_2atmpS1532 = _M0L5valueS528 & _M0L6_2atmpS1533;
  return _M0L6_2atmpS1532 == 0ull;
}

int32_t _M0FPB18multipleOfPowerOf5(
  uint64_t _M0L5valueS526,
  int32_t _M0L1pS527
) {
  int32_t _M0L6_2atmpS1531;
  #line 119 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  #line 120 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1531 = _M0FPB10pow5Factor(_M0L5valueS526);
  return _M0L6_2atmpS1531 >= _M0L1pS527;
}

int32_t _M0FPB10pow5Factor(uint64_t _M0L5valueS521) {
  uint64_t _M0L6_2atmpS1522;
  uint64_t _M0L6_2atmpS1523;
  uint64_t _M0L6_2atmpS1524;
  uint64_t _M0L6_2atmpS1525;
  uint64_t _M0L6_2atmpS1530;
  int32_t _M0L5countS522;
  uint64_t _M0L1vS523;
  #line 94 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1522 = _M0L5valueS521 % 5ull;
  if (_M0L6_2atmpS1522 != 0ull) {
    return 0;
  }
  _M0L6_2atmpS1523 = _M0L5valueS521 % 25ull;
  if (_M0L6_2atmpS1523 != 0ull) {
    return 1;
  }
  _M0L6_2atmpS1524 = _M0L5valueS521 % 125ull;
  if (_M0L6_2atmpS1524 != 0ull) {
    return 2;
  }
  _M0L6_2atmpS1525 = _M0L5valueS521 % 625ull;
  if (_M0L6_2atmpS1525 != 0ull) {
    return 3;
  }
  _M0L6_2atmpS1530 = _M0L5valueS521 / 625ull;
  _M0L5countS522 = 4;
  _M0L1vS523 = _M0L6_2atmpS1530;
  while (1) {
    if (_M0L1vS523 > 0ull) {
      uint64_t _M0L6_2atmpS1526 = _M0L1vS523 % 5ull;
      int32_t _M0L6_2atmpS1527;
      uint64_t _M0L6_2atmpS1528;
      if (_M0L6_2atmpS1526 != 0ull) {
        return _M0L5countS522;
      }
      _M0L6_2atmpS1527 = _M0L5countS522 + 1;
      _M0L6_2atmpS1528 = _M0L1vS523 / 5ull;
      _M0L5countS522 = _M0L6_2atmpS1527;
      _M0L1vS523 = _M0L6_2atmpS1528;
      continue;
    } else {
      struct _M0TPB13StringBuilder* _M0L18_2astring__builderS525;
      moonbit_string_t _M0L6_2atmpS1529;
      int32_t _result_2281;
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0L18_2astring__builderS525
      = _M0MPB13StringBuilder21StringBuilder_2einner(25);
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS525, (moonbit_string_t)moonbit_string_literal_15.data);
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0MPB13StringBuilder13write__objectGmE(_M0L18_2astring__builderS525, _M0L5valueS521);
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0L6_2atmpS1529
      = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS525);
      moonbit_decref_cycle_free(_M0L18_2astring__builderS525);
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _result_2281 = _M0FPC15abort5abortGiE(_M0L6_2atmpS1529);
      moonbit_decref_cycle_free(_M0L6_2atmpS1529);
      return _result_2281;
    }
    break;
  }
}

uint64_t _M0FPB13shiftright128(
  uint64_t _M0L2loS520,
  uint64_t _M0L2hiS518,
  int32_t _M0L4distS519
) {
  int32_t _M0L6_2atmpS1521;
  uint64_t _M0L6_2atmpS1519;
  uint64_t _M0L6_2atmpS1520;
  #line 89 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1521 = 64 - _M0L4distS519;
  _M0L6_2atmpS1519 = _M0L2hiS518 << (_M0L6_2atmpS1521 & 63);
  _M0L6_2atmpS1520 = _M0L2loS520 >> (_M0L4distS519 & 63);
  return _M0L6_2atmpS1519 | _M0L6_2atmpS1520;
}

struct _M0TPB7Umul128 _M0FPB7umul128(
  uint64_t _M0L1aS508,
  uint64_t _M0L1bS511
) {
  uint64_t _M0L3aLoS507;
  uint64_t _M0L3aHiS509;
  uint64_t _M0L3bLoS510;
  uint64_t _M0L3bHiS512;
  uint64_t _M0L1xS513;
  uint64_t _M0L6_2atmpS1517;
  uint64_t _M0L6_2atmpS1518;
  uint64_t _M0L1yS514;
  uint64_t _M0L6_2atmpS1515;
  uint64_t _M0L6_2atmpS1516;
  uint64_t _M0L1zS515;
  uint64_t _M0L6_2atmpS1513;
  uint64_t _M0L6_2atmpS1514;
  uint64_t _M0L6_2atmpS1511;
  uint64_t _M0L6_2atmpS1512;
  uint64_t _M0L1wS516;
  uint64_t _M0L2loS517;
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L3aLoS507 = _M0L1aS508 & 4294967295ull;
  _M0L3aHiS509 = _M0L1aS508 >> 32;
  _M0L3bLoS510 = _M0L1bS511 & 4294967295ull;
  _M0L3bHiS512 = _M0L1bS511 >> 32;
  _M0L1xS513 = _M0L3aLoS507 * _M0L3bLoS510;
  _M0L6_2atmpS1517 = _M0L3aHiS509 * _M0L3bLoS510;
  _M0L6_2atmpS1518 = _M0L1xS513 >> 32;
  _M0L1yS514 = _M0L6_2atmpS1517 + _M0L6_2atmpS1518;
  _M0L6_2atmpS1515 = _M0L3aLoS507 * _M0L3bHiS512;
  _M0L6_2atmpS1516 = _M0L1yS514 & 4294967295ull;
  _M0L1zS515 = _M0L6_2atmpS1515 + _M0L6_2atmpS1516;
  _M0L6_2atmpS1513 = _M0L3aHiS509 * _M0L3bHiS512;
  _M0L6_2atmpS1514 = _M0L1yS514 >> 32;
  _M0L6_2atmpS1511 = _M0L6_2atmpS1513 + _M0L6_2atmpS1514;
  _M0L6_2atmpS1512 = _M0L1zS515 >> 32;
  _M0L1wS516 = _M0L6_2atmpS1511 + _M0L6_2atmpS1512;
  _M0L2loS517 = _M0L1aS508 * _M0L1bS511;
  return (struct _M0TPB7Umul128){.$0 = _M0L2loS517, .$1 = _M0L1wS516};
}

moonbit_string_t _M0FPB19string__from__bytes(
  moonbit_bytes_t _M0L5bytesS505,
  int32_t _M0L4fromS502,
  int32_t _M0L2toS501
) {
  int32_t _M0L3lenS500;
  int32_t _M0L6_2atmpS1510;
  uint16_t* _M0L6bufferS503;
  int32_t _M0L1iS504;
  #line 52 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L3lenS500 = _M0L2toS501 - _M0L4fromS502;
  #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1510 = _M0IPC16uint166UInt16PB7Default7default();
  _M0L6bufferS503
  = (uint16_t*)moonbit_make_string(_M0L3lenS500, _M0L6_2atmpS1510);
  _M0L1iS504 = 0;
  while (1) {
    if (_M0L1iS504 < _M0L3lenS500) {
      int32_t _M0L6_2atmpS1508 = _M0L4fromS502 + _M0L1iS504;
      int32_t _M0L6_2atmpS1507;
      int32_t _M0L6_2atmpS1506;
      int32_t _M0L6_2atmpS1509;
      if (
        _M0L6_2atmpS1508 < 0
        || _M0L6_2atmpS1508 >= Moonbit_array_length(_M0L5bytesS505)
      ) {
        #line 56 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6_2atmpS1507 = (int32_t)_M0L5bytesS505[_M0L6_2atmpS1508];
      _M0L6_2atmpS1506 = (uint16_t)_M0L6_2atmpS1507;
      if (
        _M0L1iS504 < 0 || _M0L1iS504 >= Moonbit_array_length(_M0L6bufferS503)
      ) {
        #line 56 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6bufferS503[_M0L1iS504] = _M0L6_2atmpS1506;
      _M0L6_2atmpS1509 = _M0L1iS504 + 1;
      _M0L1iS504 = _M0L6_2atmpS1509;
      continue;
    }
    break;
  }
  return _M0L6bufferS503;
}

int32_t _M0FPB9log10Pow2(int32_t _M0L1eS499) {
  int32_t _M0L6_2atmpS1505;
  uint32_t _M0L6_2atmpS1504;
  uint32_t _M0L6_2atmpS1503;
  #line 44 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1505 = _M0L1eS499 * 78913;
  _M0L6_2atmpS1504 = *(uint32_t*)&_M0L6_2atmpS1505;
  _M0L6_2atmpS1503 = _M0L6_2atmpS1504 >> 18;
  return *(int32_t*)&_M0L6_2atmpS1503;
}

int32_t _M0FPB9log10Pow5(int32_t _M0L1eS498) {
  int32_t _M0L6_2atmpS1502;
  uint32_t _M0L6_2atmpS1501;
  uint32_t _M0L6_2atmpS1500;
  #line 37 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1502 = _M0L1eS498 * 732923;
  _M0L6_2atmpS1501 = *(uint32_t*)&_M0L6_2atmpS1502;
  _M0L6_2atmpS1500 = _M0L6_2atmpS1501 >> 20;
  return *(int32_t*)&_M0L6_2atmpS1500;
}

moonbit_string_t _M0FPB18copy__special__str(
  int32_t _M0L4signS496,
  int32_t _M0L8exponentS497,
  int32_t _M0L8mantissaS494
) {
  moonbit_string_t _M0L1sS495;
  moonbit_string_t _result_2284;
  #line 23 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  if (_M0L8mantissaS494) {
    return (moonbit_string_t)moonbit_string_literal_16.data;
  }
  if (_M0L4signS496) {
    _M0L1sS495 = (moonbit_string_t)moonbit_string_literal_17.data;
  } else {
    _M0L1sS495 = (moonbit_string_t)moonbit_string_literal_0.data;
  }
  if (_M0L8exponentS497) {
    moonbit_string_t _result_2283;
    #line 29 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _result_2283
    = moonbit_add_string(_M0L1sS495, (moonbit_string_t)moonbit_string_literal_18.data);
    moonbit_decref_cycle_free(_M0L1sS495);
    return _result_2283;
  }
  #line 31 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _result_2284
  = moonbit_add_string(_M0L1sS495, (moonbit_string_t)moonbit_string_literal_19.data);
  moonbit_decref_cycle_free(_M0L1sS495);
  return _result_2284;
}

int32_t _M0FPB8pow5bits(int32_t _M0L1eS493) {
  int32_t _M0L6_2atmpS1499;
  uint32_t _M0L6_2atmpS1498;
  uint32_t _M0L6_2atmpS1497;
  int32_t _M0L6_2atmpS1496;
  #line 18 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1499 = _M0L1eS493 * 1217359;
  _M0L6_2atmpS1498 = *(uint32_t*)&_M0L6_2atmpS1499;
  _M0L6_2atmpS1497 = _M0L6_2atmpS1498 >> 19;
  _M0L6_2atmpS1496 = *(int32_t*)&_M0L6_2atmpS1497;
  return _M0L6_2atmpS1496 + 1;
}

int32_t _M0MPC16double6Double7to__int(double _M0L4selfS492) {
  #line 43 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_to_int.mbt"
  if (_M0L4selfS492 != _M0L4selfS492) {
    return 0;
  } else if (_M0L4selfS492 >= 0x1.fffffffcp+30) {
    return 2147483647;
  } else if (_M0L4selfS492 <= -0x1p+31) {
    return (int32_t)0x80000000;
  } else {
    return (int32_t)_M0L4selfS492;
  }
}

int64_t _M0MPC16double6Double9to__int64(double _M0L4selfS491) {
  #line 46 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_to_int64_native.mbt"
  if (_M0L4selfS491 != _M0L4selfS491) {
    return 0ll;
  } else if (_M0L4selfS491 >= 0x1p+63) {
    return 9223372036854775807ll;
  } else if (_M0L4selfS491 <= -0x1p+63) {
    return (int64_t)0x8000000000000000ll;
  } else {
    return (int64_t)_M0L4selfS491;
  }
}

struct _M0TPB5ArrayGfE* _M0MPC15array5Array20unsafe__make__uninitGfE(
  int32_t _M0L3lenS488
) {
  float* _M0L6_2atmpS1493;
  struct _M0TPB5ArrayGfE* _block_2285;
  #line 30 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1493 = (float*)moonbit_make_float_array_raw(_M0L3lenS488);
  _block_2285
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_block_2285)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 37, 0);
  _block_2285->$0 = _M0L6_2atmpS1493;
  _block_2285->$1 = _M0L3lenS488;
  return _block_2285;
}

struct _M0TPB5ArrayGbE* _M0MPC15array5Array20unsafe__make__uninitGbE(
  int32_t _M0L3lenS489
) {
  uint8_t* _M0L6_2atmpS1494;
  struct _M0TPB5ArrayGbE* _block_2286;
  #line 30 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1494 = (uint8_t*)moonbit_make_bytes_raw(_M0L3lenS489);
  _block_2286
  = (struct _M0TPB5ArrayGbE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGbE));
  Moonbit_object_header(_block_2286)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 53, 0);
  _block_2286->$0 = _M0L6_2atmpS1494;
  _block_2286->$1 = _M0L3lenS489;
  return _block_2286;
}

struct _M0TPB5ArrayGiE* _M0MPC15array5Array20unsafe__make__uninitGiE(
  int32_t _M0L3lenS490
) {
  int32_t* _M0L6_2atmpS1495;
  struct _M0TPB5ArrayGiE* _block_2287;
  #line 30 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1495 = (int32_t*)moonbit_make_int32_array_raw(_M0L3lenS490);
  _block_2287
  = (struct _M0TPB5ArrayGiE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGiE));
  Moonbit_object_header(_block_2287)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 46, 0);
  _block_2287->$0 = _M0L6_2atmpS1495;
  _block_2287->$1 = _M0L3lenS490;
  return _block_2287;
}

uint64_t _M0MPC15array13ReadOnlyArray2atGmE(
  uint64_t* _M0L4selfS484,
  int32_t _M0L5indexS485
) {
  uint64_t* _M0L6_2atmpS1491;
  #line 38 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\readonlyarray.mbt"
  _M0L6_2atmpS1491 = _M0L4selfS484;
  if (
    _M0L5indexS485 < 0
    || _M0L5indexS485 >= Moonbit_array_length(_M0L6_2atmpS1491)
  ) {
    #line 40 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\readonlyarray.mbt"
    moonbit_panic();
  }
  return (uint64_t)_M0L6_2atmpS1491[_M0L5indexS485];
}

uint32_t _M0MPC15array13ReadOnlyArray2atGjE(
  uint32_t* _M0L4selfS486,
  int32_t _M0L5indexS487
) {
  uint32_t* _M0L6_2atmpS1492;
  #line 38 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\readonlyarray.mbt"
  _M0L6_2atmpS1492 = _M0L4selfS486;
  if (
    _M0L5indexS487 < 0
    || _M0L5indexS487 >= Moonbit_array_length(_M0L6_2atmpS1492)
  ) {
    #line 40 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\readonlyarray.mbt"
    moonbit_panic();
  }
  return (uint32_t)_M0L6_2atmpS1492[_M0L5indexS487];
}

moonbit_string_t _M0IPC16uint646UInt64PB4Show10to__string(
  uint64_t _M0L4selfS483
) {
  #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  #line 51 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  return _M0MPC16uint646UInt6418to__string_2einner(_M0L4selfS483, 10);
}

moonbit_string_t _M0IPC13int3IntPB4Show10to__string(int32_t _M0L4selfS482) {
  #line 35 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  #line 36 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  return _M0MPC13int3Int18to__string_2einner(_M0L4selfS482, 10);
}

moonbit_string_t _M0IPC14bool4BoolPB4Show10to__string(int32_t _M0L4selfS481) {
  #line 26 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  if (_M0L4selfS481) {
    return (moonbit_string_t)moonbit_string_literal_20.data;
  } else {
    return (moonbit_string_t)moonbit_string_literal_21.data;
  }
}

uint64_t _M0MPC14uint4UInt10to__uint64(uint32_t _M0L4selfS480) {
  #line 2576 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  return (uint64_t)_M0L4selfS480;
}

int32_t _M0MPC15array5Array4pushGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS471,
  moonbit_string_t _M0L5valueS473
) {
  int32_t _M0L3lenS1470;
  moonbit_string_t* _M0L6_2atmpS1472;
  int32_t _M0L6_2atmpS1471;
  int32_t _M0L6lengthS472;
  moonbit_string_t* _M0L3bufS1475;
  moonbit_string_t _M0L6_2aoldS2187;
  int32_t _M0L6_2atmpS1476;
  #line 406 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L3lenS1470 = _M0L4selfS471->$1;
  #line 408 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1472 = _M0MPC15array5Array6bufferGsE(_M0L4selfS471);
  _M0L6_2atmpS1471 = Moonbit_array_length(_M0L6_2atmpS1472);
  moonbit_decref_cycle_free(_M0L6_2atmpS1472);
  if (_M0L3lenS1470 == _M0L6_2atmpS1471) {
    int32_t _M0L3lenS1474 = _M0L4selfS471->$1;
    int32_t _M0L6_2atmpS1473 = _M0L3lenS1474 + 1;
    #line 409 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0MPC15array5Array7reallocGsE(_M0L4selfS471, _M0L6_2atmpS1473);
  }
  _M0L6lengthS472 = _M0L4selfS471->$1;
  _M0L3bufS1475 = _M0L4selfS471->$0;
  _M0L6_2aoldS2187 = (moonbit_string_t)_M0L3bufS1475[_M0L6lengthS472];
  moonbit_decref_cycle_free(_M0L6_2aoldS2187);
  _M0L3bufS1475[_M0L6lengthS472] = _M0L5valueS473;
  _M0L6_2atmpS1476 = _M0L6lengthS472 + 1;
  _M0L4selfS471->$1 = _M0L6_2atmpS1476;
  return 0;
}

int32_t _M0MPC15array5Array4pushGUsiEE(
  struct _M0TPB5ArrayGUsiEE* _M0L4selfS474,
  struct _M0TUsiE* _M0L5valueS476
) {
  int32_t _M0L3lenS1477;
  struct _M0TUsiE** _M0L6_2atmpS1479;
  int32_t _M0L6_2atmpS1478;
  int32_t _M0L6lengthS475;
  struct _M0TUsiE** _M0L3bufS1482;
  struct _M0TUsiE* _M0L6_2aoldS2188;
  int32_t _M0L6_2atmpS1483;
  #line 406 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L3lenS1477 = _M0L4selfS474->$1;
  #line 408 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1479 = _M0MPC15array5Array6bufferGUsiEE(_M0L4selfS474);
  _M0L6_2atmpS1478 = Moonbit_array_length(_M0L6_2atmpS1479);
  moonbit_decref_cycle_free(_M0L6_2atmpS1479);
  if (_M0L3lenS1477 == _M0L6_2atmpS1478) {
    int32_t _M0L3lenS1481 = _M0L4selfS474->$1;
    int32_t _M0L6_2atmpS1480 = _M0L3lenS1481 + 1;
    #line 409 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0MPC15array5Array7reallocGUsiEE(_M0L4selfS474, _M0L6_2atmpS1480);
  }
  _M0L6lengthS475 = _M0L4selfS474->$1;
  _M0L3bufS1482 = _M0L4selfS474->$0;
  _M0L6_2aoldS2188 = (struct _M0TUsiE*)_M0L3bufS1482[_M0L6lengthS475];
  if (_M0L6_2aoldS2188) {
    moonbit_decref_cycle_free(_M0L6_2aoldS2188);
  }
  _M0L3bufS1482[_M0L6lengthS475] = _M0L5valueS476;
  _M0L6_2atmpS1483 = _M0L6lengthS475 + 1;
  _M0L4selfS474->$1 = _M0L6_2atmpS1483;
  return 0;
}

int32_t _M0MPC15array5Array4pushGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS477,
  float _M0L5valueS479
) {
  int32_t _M0L3lenS1484;
  float* _M0L6_2atmpS1486;
  int32_t _M0L6_2atmpS1485;
  int32_t _M0L6lengthS478;
  float* _M0L3bufS1489;
  int32_t _M0L6_2atmpS1490;
  #line 406 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L3lenS1484 = _M0L4selfS477->$1;
  #line 408 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1486 = _M0MPC15array5Array6bufferGfE(_M0L4selfS477);
  _M0L6_2atmpS1485 = Moonbit_array_length(_M0L6_2atmpS1486);
  moonbit_decref_cycle_free(_M0L6_2atmpS1486);
  if (_M0L3lenS1484 == _M0L6_2atmpS1485) {
    int32_t _M0L3lenS1488 = _M0L4selfS477->$1;
    int32_t _M0L6_2atmpS1487 = _M0L3lenS1488 + 1;
    #line 409 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0MPC15array5Array7reallocGfE(_M0L4selfS477, _M0L6_2atmpS1487);
  }
  _M0L6lengthS478 = _M0L4selfS477->$1;
  _M0L3bufS1489 = _M0L4selfS477->$0;
  _M0L3bufS1489[_M0L6lengthS478] = _M0L5valueS479;
  _M0L6_2atmpS1490 = _M0L6lengthS478 + 1;
  _M0L4selfS477->$1 = _M0L6_2atmpS1490;
  return 0;
}

int32_t _M0MPC15array5Array7reallocGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS460,
  int32_t _M0L8requiredS462
) {
  int32_t _M0L8old__capS459;
  int32_t _M0L3lenS1467;
  int32_t _M0L8new__capS461;
  #line 302 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  #line 304 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__capS459 = _M0MPC15array5Array8capacityGsE(_M0L4selfS460);
  _M0L3lenS1467 = _M0L4selfS460->$1;
  #line 305 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__capS461
  = _M0FPB23array__growth__capacity(_M0L8old__capS459, _M0L3lenS1467, _M0L8requiredS462);
  #line 306 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0MPC15array5Array14resize__bufferGsE(_M0L4selfS460, _M0L8new__capS461);
  return 0;
}

int32_t _M0MPC15array5Array7reallocGUsiEE(
  struct _M0TPB5ArrayGUsiEE* _M0L4selfS464,
  int32_t _M0L8requiredS466
) {
  int32_t _M0L8old__capS463;
  int32_t _M0L3lenS1468;
  int32_t _M0L8new__capS465;
  #line 302 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  #line 304 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__capS463 = _M0MPC15array5Array8capacityGUsiEE(_M0L4selfS464);
  _M0L3lenS1468 = _M0L4selfS464->$1;
  #line 305 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__capS465
  = _M0FPB23array__growth__capacity(_M0L8old__capS463, _M0L3lenS1468, _M0L8requiredS466);
  #line 306 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0MPC15array5Array14resize__bufferGUsiEE(_M0L4selfS464, _M0L8new__capS465);
  return 0;
}

int32_t _M0MPC15array5Array7reallocGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS468,
  int32_t _M0L8requiredS470
) {
  int32_t _M0L8old__capS467;
  int32_t _M0L3lenS1469;
  int32_t _M0L8new__capS469;
  #line 302 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  #line 304 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__capS467 = _M0MPC15array5Array8capacityGfE(_M0L4selfS468);
  _M0L3lenS1469 = _M0L4selfS468->$1;
  #line 305 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__capS469
  = _M0FPB23array__growth__capacity(_M0L8old__capS467, _M0L3lenS1469, _M0L8requiredS470);
  #line 306 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0MPC15array5Array14resize__bufferGfE(_M0L4selfS468, _M0L8new__capS469);
  return 0;
}

int32_t _M0MPC15array5Array14resize__bufferGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS442,
  int32_t _M0L13new__capacityS445
) {
  moonbit_string_t* _M0L8old__bufS441;
  int32_t _M0L3lenS443;
  int32_t _M0L9copy__lenS444;
  moonbit_string_t* _M0L8new__bufS446;
  moonbit_string_t* _M0L6_2aoldS2189;
  #line 242 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__bufS441 = _M0L4selfS442->$0;
  _M0L3lenS443 = _M0L4selfS442->$1;
  if (_M0L3lenS443 < _M0L13new__capacityS445) {
    _M0L9copy__lenS444 = _M0L3lenS443;
  } else {
    _M0L9copy__lenS444 = _M0L13new__capacityS445;
  }
  moonbit_incref_cycle_free(_M0L8old__bufS441);
  #line 249 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__bufS446
  = _M0MPB18UninitializedArray23make__and__blit_2einnerGsE(_M0L8old__bufS441, _M0L13new__capacityS445, _M0L9copy__lenS444, 0, 0);
  _M0L6_2aoldS2189 = _M0L4selfS442->$0;
  moonbit_decref_cycle_free(_M0L6_2aoldS2189);
  _M0L4selfS442->$0 = _M0L8new__bufS446;
  return 0;
}

int32_t _M0MPC15array5Array14resize__bufferGUsiEE(
  struct _M0TPB5ArrayGUsiEE* _M0L4selfS448,
  int32_t _M0L13new__capacityS451
) {
  struct _M0TUsiE** _M0L8old__bufS447;
  int32_t _M0L3lenS449;
  int32_t _M0L9copy__lenS450;
  struct _M0TUsiE** _M0L8new__bufS452;
  struct _M0TUsiE** _M0L6_2aoldS2190;
  #line 242 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__bufS447 = _M0L4selfS448->$0;
  _M0L3lenS449 = _M0L4selfS448->$1;
  if (_M0L3lenS449 < _M0L13new__capacityS451) {
    _M0L9copy__lenS450 = _M0L3lenS449;
  } else {
    _M0L9copy__lenS450 = _M0L13new__capacityS451;
  }
  moonbit_incref_cycle_free(_M0L8old__bufS447);
  #line 249 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__bufS452
  = _M0MPB18UninitializedArray23make__and__blit_2einnerGUsiEE(_M0L8old__bufS447, _M0L13new__capacityS451, _M0L9copy__lenS450, 0, 0);
  _M0L6_2aoldS2190 = _M0L4selfS448->$0;
  moonbit_decref_cycle_free(_M0L6_2aoldS2190);
  _M0L4selfS448->$0 = _M0L8new__bufS452;
  return 0;
}

int32_t _M0MPC15array5Array14resize__bufferGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS454,
  int32_t _M0L13new__capacityS457
) {
  float* _M0L8old__bufS453;
  int32_t _M0L3lenS455;
  int32_t _M0L9copy__lenS456;
  float* _M0L8new__bufS458;
  float* _M0L6_2aoldS2191;
  #line 242 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__bufS453 = _M0L4selfS454->$0;
  _M0L3lenS455 = _M0L4selfS454->$1;
  if (_M0L3lenS455 < _M0L13new__capacityS457) {
    _M0L9copy__lenS456 = _M0L3lenS455;
  } else {
    _M0L9copy__lenS456 = _M0L13new__capacityS457;
  }
  moonbit_incref_cycle_free(_M0L8old__bufS453);
  #line 249 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__bufS458
  = _M0MPB18UninitializedArray23make__and__blit_2einnerGfE(_M0L8old__bufS453, _M0L13new__capacityS457, _M0L9copy__lenS456, 0, 0);
  _M0L6_2aoldS2191 = _M0L4selfS454->$0;
  moonbit_decref_cycle_free(_M0L6_2aoldS2191);
  _M0L4selfS454->$0 = _M0L8new__bufS458;
  return 0;
}

int32_t _M0MPC15array5Array8capacityGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS438
) {
  moonbit_string_t* _M0L6_2atmpS1464;
  int32_t _result_2288;
  #line 132 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L6_2atmpS1464 = _M0MPC15array5Array6bufferGsE(_M0L4selfS438);
  _result_2288 = Moonbit_array_length(_M0L6_2atmpS1464);
  moonbit_decref_cycle_free(_M0L6_2atmpS1464);
  return _result_2288;
}

int32_t _M0MPC15array5Array8capacityGUsiEE(
  struct _M0TPB5ArrayGUsiEE* _M0L4selfS439
) {
  struct _M0TUsiE** _M0L6_2atmpS1465;
  int32_t _result_2289;
  #line 132 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L6_2atmpS1465 = _M0MPC15array5Array6bufferGUsiEE(_M0L4selfS439);
  _result_2289 = Moonbit_array_length(_M0L6_2atmpS1465);
  moonbit_decref_cycle_free(_M0L6_2atmpS1465);
  return _result_2289;
}

int32_t _M0MPC15array5Array8capacityGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS440
) {
  float* _M0L6_2atmpS1466;
  int32_t _result_2290;
  #line 132 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L6_2atmpS1466 = _M0MPC15array5Array6bufferGfE(_M0L4selfS440);
  _result_2290 = Moonbit_array_length(_M0L6_2atmpS1466);
  moonbit_decref_cycle_free(_M0L6_2atmpS1466);
  return _result_2290;
}

int32_t _M0FPB23array__growth__capacity(
  int32_t _M0L7currentS434,
  int32_t _M0L3lenS432,
  int32_t _M0L8requiredS431
) {
  int32_t _M0L5startS433;
  int32_t _M0L5spaceS435;
  #line 200 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  if (_M0L8requiredS431 < _M0L3lenS432) {
    #line 203 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_22.data);
  }
  if (_M0L7currentS434 == 0) {
    _M0L5startS433 = 8;
  } else {
    _M0L5startS433 = _M0L7currentS434;
  }
  _M0L5spaceS435 = _M0L5startS433;
  while (1) {
    if (_M0L5spaceS435 < _M0L8requiredS431) {
      int32_t _M0L4nextS436 = _M0L5spaceS435 * 2;
      if (_M0L4nextS436 <= _M0L5spaceS435) {
        return _M0L8requiredS431;
      }
      _M0L5spaceS435 = _M0L4nextS436;
      continue;
    } else {
      return _M0L5spaceS435;
    }
    break;
  }
}

int32_t _M0MPC15array5Array6lengthGfE(struct _M0TPB5ArrayGfE* _M0L4selfS430) {
  #line 147 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  return _M0L4selfS430->$1;
}

float* _M0MPC15array5Array6bufferGfE(struct _M0TPB5ArrayGfE* _M0L4selfS424) {
  float* _M0L8_2afieldS2192;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS2192 = _M0L4selfS424->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS2192);
  return _M0L8_2afieldS2192;
}

uint8_t* _M0MPC15array5Array6bufferGbE(struct _M0TPB5ArrayGbE* _M0L4selfS425) {
  uint8_t* _M0L8_2afieldS2193;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS2193 = _M0L4selfS425->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS2193);
  return _M0L8_2afieldS2193;
}

struct _M0TP26RiantR8snn__mbt11MonitorAdEx** _M0MPC15array5Array6bufferGRP26RiantR8snn__mbt11MonitorAdExE(
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt11MonitorAdExE* _M0L4selfS426
) {
  struct _M0TP26RiantR8snn__mbt11MonitorAdEx** _M0L8_2afieldS2194;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS2194 = _M0L4selfS426->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS2194);
  return _M0L8_2afieldS2194;
}

moonbit_string_t* _M0MPC15array5Array6bufferGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS427
) {
  moonbit_string_t* _M0L8_2afieldS2195;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS2195 = _M0L4selfS427->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS2195);
  return _M0L8_2afieldS2195;
}

struct _M0TUsiE** _M0MPC15array5Array6bufferGUsiEE(
  struct _M0TPB5ArrayGUsiEE* _M0L4selfS428
) {
  struct _M0TUsiE** _M0L8_2afieldS2196;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS2196 = _M0L4selfS428->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS2196);
  return _M0L8_2afieldS2196;
}

int32_t* _M0MPC15array5Array6bufferGiE(struct _M0TPB5ArrayGiE* _M0L4selfS429) {
  int32_t* _M0L8_2afieldS2197;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS2197 = _M0L4selfS429->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS2197);
  return _M0L8_2afieldS2197;
}

moonbit_string_t _M0IPC16string6StringPB4Show10to__string(
  moonbit_string_t _M0L4selfS423
) {
  #line 220 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  moonbit_incref_cycle_free(_M0L4selfS423);
  return _M0L4selfS423;
}

int32_t _M0IPB13StringBuilderPB6Logger11write__view(
  struct _M0TPB13StringBuilder* _M0L4selfS422,
  struct _M0TPC16string10StringView _M0L3strS420
) {
  int32_t _M0L3endS1462;
  int32_t _M0L5startS1463;
  int32_t _M0L8str__lenS419;
  int32_t _M0L3lenS1461;
  int32_t _M0L8requiredS421;
  uint16_t* _M0L4dataS1454;
  int32_t _M0L6_2atmpS1453;
  int32_t _if__result_2292;
  uint16_t* _M0L4dataS1455;
  int32_t _M0L3lenS1456;
  moonbit_string_t _M0L6_2atmpS1457;
  int32_t _M0L6_2atmpS1458;
  int32_t _M0L3lenS1460;
  int32_t _M0L6_2atmpS1459;
  #line 158 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L3endS1462 = _M0L3strS420.$2;
  _M0L5startS1463 = _M0L3strS420.$1;
  _M0L8str__lenS419 = _M0L3endS1462 - _M0L5startS1463;
  if (_M0L8str__lenS419 == 0) {
    return 0;
  }
  _M0L3lenS1461 = _M0L4selfS422->$1;
  _M0L8requiredS421 = _M0L3lenS1461 + _M0L8str__lenS419;
  _M0L4dataS1454 = _M0L4selfS422->$0;
  _M0L6_2atmpS1453 = Moonbit_array_length(_M0L4dataS1454);
  if (_M0L8requiredS421 > _M0L6_2atmpS1453) {
    _if__result_2292 = 1;
  } else {
    int32_t _M0L3lenS1452 = _M0L4selfS422->$1;
    _if__result_2292 = _M0L8requiredS421 < _M0L3lenS1452;
  }
  if (_if__result_2292) {
    #line 168 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0MPB13StringBuilder4grow(_M0L4selfS422, _M0L8requiredS421);
  }
  _M0L4dataS1455 = _M0L4selfS422->$0;
  _M0L3lenS1456 = _M0L4selfS422->$1;
  moonbit_incref_cycle_free(_M0L4dataS1455);
  #line 172 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L6_2atmpS1457 = _M0MPC16string10StringView4data(_M0L3strS420);
  #line 173 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L6_2atmpS1458 = _M0MPC16string10StringView13start__offset(_M0L3strS420);
  #line 170 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0MPC15array10FixedArray26unsafe__blit__from__string(_M0L4dataS1455, _M0L3lenS1456, _M0L6_2atmpS1457, _M0L6_2atmpS1458, _M0L8str__lenS419);
  moonbit_decref_cycle_free(_M0L4dataS1455);
  moonbit_decref_cycle_free(_M0L6_2atmpS1457);
  _M0L3lenS1460 = _M0L4selfS422->$1;
  _M0L6_2atmpS1459 = _M0L3lenS1460 + _M0L8str__lenS419;
  _M0L4selfS422->$1 = _M0L6_2atmpS1459;
  return 0;
}

moonbit_string_t _M0MPC16string6String17unsafe__substring(
  moonbit_string_t _M0L3strS416,
  int32_t _M0L5startS414,
  int32_t _M0L3endS415
) {
  int32_t _if__result_2293;
  int32_t _M0L3lenS417;
  int32_t _M0L6_2atmpS1451;
  moonbit_bytes_t _M0L5bytesS418;
  moonbit_bytes_t _M0L6_2atmpS1450;
  moonbit_string_t _result_2294;
  #line 91 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\string.mbt"
  if (_M0L5startS414 == 0) {
    int32_t _M0L6_2atmpS1449 = Moonbit_array_length(_M0L3strS416);
    _if__result_2293 = _M0L3endS415 == _M0L6_2atmpS1449;
  } else {
    _if__result_2293 = 0;
  }
  if (_if__result_2293) {
    moonbit_incref_cycle_free(_M0L3strS416);
    return _M0L3strS416;
  }
  _M0L3lenS417 = _M0L3endS415 - _M0L5startS414;
  _M0L6_2atmpS1451 = _M0L3lenS417 * 2;
  _M0L5bytesS418 = (moonbit_bytes_t)moonbit_make_bytes(_M0L6_2atmpS1451, 0);
  #line 102 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\string.mbt"
  _M0MPC15array10FixedArray18blit__from__string(_M0L5bytesS418, 0, _M0L3strS416, _M0L5startS414, _M0L3lenS417);
  _M0L6_2atmpS1450 = _M0L5bytesS418;
  #line 103 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\string.mbt"
  _result_2294
  = _M0MPC15bytes5Bytes29to__unchecked__string_2einner(_M0L6_2atmpS1450, 0, 4294967296ll);
  moonbit_decref_cycle_free(_M0L6_2atmpS1450);
  return _result_2294;
}

moonbit_string_t _M0MPC15bytes5Bytes29to__unchecked__string_2einner(
  moonbit_bytes_t _M0L4selfS409,
  int32_t _M0L6offsetS413,
  int64_t _M0L6lengthS411
) {
  int32_t _M0L3lenS408;
  int32_t _M0L6lengthS410;
  int32_t _if__result_2295;
  #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
  _M0L3lenS408 = Moonbit_array_length(_M0L4selfS409);
  if (_M0L6lengthS411 == 4294967296ll) {
    _M0L6lengthS410 = _M0L3lenS408 - _M0L6offsetS413;
  } else {
    int64_t _M0L7_2aSomeS412 = _M0L6lengthS411;
    _M0L6lengthS410 = (int32_t)_M0L7_2aSomeS412;
  }
  if (_M0L6offsetS413 >= 0) {
    if (_M0L6lengthS410 >= 0) {
      int32_t _M0L6_2atmpS1448 = _M0L6offsetS413 + _M0L6lengthS410;
      _if__result_2295 = _M0L6_2atmpS1448 <= _M0L3lenS408;
    } else {
      _if__result_2295 = 0;
    }
  } else {
    _if__result_2295 = 0;
  }
  if (_if__result_2295) {
    moonbit_incref_cycle_free(_M0L4selfS409);
    #line 85 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
    return _M0FPB19unsafe__sub__string(_M0L4selfS409, _M0L6offsetS413, _M0L6lengthS410);
  } else {
    #line 84 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
    moonbit_panic();
  }
}

int32_t _M0MPC15array10FixedArray18blit__from__string(
  moonbit_bytes_t _M0L4selfS400,
  int32_t _M0L13bytes__offsetS395,
  moonbit_string_t _M0L3strS402,
  int32_t _M0L11str__offsetS398,
  int32_t _M0L6lengthS396
) {
  int32_t _M0L6_2atmpS1447;
  int32_t _M0L6_2atmpS1446;
  int32_t _M0L2e1S394;
  int32_t _M0L6_2atmpS1445;
  int32_t _M0L2e2S397;
  int32_t _M0L4len1S399;
  int32_t _M0L4len2S401;
  #line 125 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
  _M0L6_2atmpS1447 = _M0L6lengthS396 * 2;
  _M0L6_2atmpS1446 = _M0L13bytes__offsetS395 + _M0L6_2atmpS1447;
  _M0L2e1S394 = _M0L6_2atmpS1446 - 1;
  _M0L6_2atmpS1445 = _M0L11str__offsetS398 + _M0L6lengthS396;
  _M0L2e2S397 = _M0L6_2atmpS1445 - 1;
  _M0L4len1S399 = Moonbit_array_length(_M0L4selfS400);
  _M0L4len2S401 = Moonbit_array_length(_M0L3strS402);
  if (
    _M0L6lengthS396 >= 0
    && _M0L13bytes__offsetS395 >= 0
    && _M0L2e1S394 < _M0L4len1S399
    && _M0L11str__offsetS398 >= 0
    && _M0L2e2S397 < _M0L4len2S401
  ) {
    int32_t _M0L16end__str__offsetS403 =
      _M0L11str__offsetS398 + _M0L6lengthS396;
    int32_t _M0L1iS404 = _M0L11str__offsetS398;
    int32_t _M0L1jS405 = _M0L13bytes__offsetS395;
    while (1) {
      if (_M0L1iS404 < _M0L16end__str__offsetS403) {
        int32_t _M0L6_2atmpS1442 = _M0L3strS402[_M0L1iS404];
        int32_t _M0L6_2atmpS1441 = (int32_t)_M0L6_2atmpS1442;
        uint32_t _M0L1cS406 = *(uint32_t*)&_M0L6_2atmpS1441;
        uint32_t _M0L6_2atmpS1437 = _M0L1cS406 & 255u;
        int32_t _M0L6_2atmpS1436;
        int32_t _M0L6_2atmpS1438;
        uint32_t _M0L6_2atmpS1440;
        int32_t _M0L6_2atmpS1439;
        int32_t _M0L6_2atmpS1443;
        int32_t _M0L6_2atmpS1444;
        #line 142 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
        _M0L6_2atmpS1436 = _M0MPC14uint4UInt8to__byte(_M0L6_2atmpS1437);
        if (
          _M0L1jS405 < 0 || _M0L1jS405 >= Moonbit_array_length(_M0L4selfS400)
        ) {
          #line 142 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
          moonbit_panic();
        }
        _M0L4selfS400[_M0L1jS405] = _M0L6_2atmpS1436;
        _M0L6_2atmpS1438 = _M0L1jS405 + 1;
        _M0L6_2atmpS1440 = _M0L1cS406 >> 8;
        #line 143 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
        _M0L6_2atmpS1439 = _M0MPC14uint4UInt8to__byte(_M0L6_2atmpS1440);
        if (
          _M0L6_2atmpS1438 < 0
          || _M0L6_2atmpS1438 >= Moonbit_array_length(_M0L4selfS400)
        ) {
          #line 143 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
          moonbit_panic();
        }
        _M0L4selfS400[_M0L6_2atmpS1438] = _M0L6_2atmpS1439;
        _M0L6_2atmpS1443 = _M0L1iS404 + 1;
        _M0L6_2atmpS1444 = _M0L1jS405 + 2;
        _M0L1iS404 = _M0L6_2atmpS1443;
        _M0L1jS405 = _M0L6_2atmpS1444;
        continue;
      }
      break;
    }
  } else {
    #line 138 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
    moonbit_panic();
  }
  return 0;
}

int32_t _M0MPC14uint4UInt8to__byte(uint32_t _M0L4selfS393) {
  int32_t _M0L6_2atmpS1435;
  #line 2601 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS1435 = *(int32_t*)&_M0L4selfS393;
  return _M0L6_2atmpS1435 & 0xff;
}

moonbit_string_t _M0MPC16uint646UInt6418to__string_2einner(
  uint64_t _M0L4selfS385,
  int32_t _M0L5radixS384
) {
  uint16_t* _M0L6bufferS386;
  #line 607 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  if (_M0L5radixS384 < 2 || _M0L5radixS384 > 36) {
    #line 611 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_23.data);
  }
  if (_M0L4selfS385 == 0ull) {
    return (moonbit_string_t)moonbit_string_literal_14.data;
  }
  switch (_M0L5radixS384) {
    case 10: {
      int32_t _M0L3lenS387;
      uint16_t* _M0L6bufferS388;
      #line 622 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L3lenS387 = _M0FPB12dec__count64(_M0L4selfS385);
      _M0L6bufferS388 = (uint16_t*)moonbit_make_string(_M0L3lenS387, 0);
      #line 624 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0FPB22int64__to__string__dec(_M0L6bufferS388, _M0L4selfS385, 0, _M0L3lenS387);
      _M0L6bufferS386 = _M0L6bufferS388;
      break;
    }
    
    case 16: {
      int32_t _M0L3lenS389;
      uint16_t* _M0L6bufferS390;
      #line 628 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L3lenS389 = _M0FPB12hex__count64(_M0L4selfS385);
      _M0L6bufferS390 = (uint16_t*)moonbit_make_string(_M0L3lenS389, 0);
      #line 630 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0FPB22int64__to__string__hex(_M0L6bufferS390, _M0L4selfS385, 0, _M0L3lenS389);
      _M0L6bufferS386 = _M0L6bufferS390;
      break;
    }
    default: {
      int32_t _M0L3lenS391;
      uint16_t* _M0L6bufferS392;
      #line 634 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L3lenS391 = _M0FPB14radix__count64(_M0L4selfS385, _M0L5radixS384);
      _M0L6bufferS392 = (uint16_t*)moonbit_make_string(_M0L3lenS391, 0);
      #line 636 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0FPB26int64__to__string__generic(_M0L6bufferS392, _M0L4selfS385, 0, _M0L3lenS391, _M0L5radixS384);
      _M0L6bufferS386 = _M0L6bufferS392;
      break;
    }
  }
  return _M0L6bufferS386;
}

moonbit_string_t _M0MPC15int645Int6418to__string_2einner(
  int64_t _M0L4selfS368,
  int32_t _M0L5radixS367
) {
  int32_t _M0L12is__negativeS369;
  uint64_t _M0L3numS370;
  uint16_t* _M0L6bufferS371;
  #line 548 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  if (_M0L5radixS367 < 2 || _M0L5radixS367 > 36) {
    #line 552 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_23.data);
  }
  if (_M0L4selfS368 == 0ll) {
    return (moonbit_string_t)moonbit_string_literal_14.data;
  }
  _M0L12is__negativeS369 = _M0L4selfS368 < 0ll;
  if (_M0L12is__negativeS369) {
    int64_t _M0L6_2atmpS1434 = -_M0L4selfS368;
    _M0L3numS370 = *(uint64_t*)&_M0L6_2atmpS1434;
  } else {
    _M0L3numS370 = *(uint64_t*)&_M0L4selfS368;
  }
  switch (_M0L5radixS367) {
    case 10: {
      int32_t _M0L10digit__lenS372;
      int32_t _M0L6_2atmpS1431;
      int32_t _M0L10total__lenS373;
      uint16_t* _M0L6bufferS374;
      int32_t _M0L12digit__startS375;
      #line 573 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS372 = _M0FPB12dec__count64(_M0L3numS370);
      if (_M0L12is__negativeS369) {
        _M0L6_2atmpS1431 = 1;
      } else {
        _M0L6_2atmpS1431 = 0;
      }
      _M0L10total__lenS373 = _M0L10digit__lenS372 + _M0L6_2atmpS1431;
      _M0L6bufferS374
      = (uint16_t*)moonbit_make_string(_M0L10total__lenS373, 0);
      if (_M0L12is__negativeS369) {
        _M0L12digit__startS375 = 1;
      } else {
        _M0L12digit__startS375 = 0;
      }
      #line 577 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0FPB22int64__to__string__dec(_M0L6bufferS374, _M0L3numS370, _M0L12digit__startS375, _M0L10total__lenS373);
      _M0L6bufferS371 = _M0L6bufferS374;
      break;
    }
    
    case 16: {
      int32_t _M0L10digit__lenS376;
      int32_t _M0L6_2atmpS1432;
      int32_t _M0L10total__lenS377;
      uint16_t* _M0L6bufferS378;
      int32_t _M0L12digit__startS379;
      #line 581 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS376 = _M0FPB12hex__count64(_M0L3numS370);
      if (_M0L12is__negativeS369) {
        _M0L6_2atmpS1432 = 1;
      } else {
        _M0L6_2atmpS1432 = 0;
      }
      _M0L10total__lenS377 = _M0L10digit__lenS376 + _M0L6_2atmpS1432;
      _M0L6bufferS378
      = (uint16_t*)moonbit_make_string(_M0L10total__lenS377, 0);
      if (_M0L12is__negativeS369) {
        _M0L12digit__startS379 = 1;
      } else {
        _M0L12digit__startS379 = 0;
      }
      #line 585 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0FPB22int64__to__string__hex(_M0L6bufferS378, _M0L3numS370, _M0L12digit__startS379, _M0L10total__lenS377);
      _M0L6bufferS371 = _M0L6bufferS378;
      break;
    }
    default: {
      int32_t _M0L10digit__lenS380;
      int32_t _M0L6_2atmpS1433;
      int32_t _M0L10total__lenS381;
      uint16_t* _M0L6bufferS382;
      int32_t _M0L12digit__startS383;
      #line 589 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS380
      = _M0FPB14radix__count64(_M0L3numS370, _M0L5radixS367);
      if (_M0L12is__negativeS369) {
        _M0L6_2atmpS1433 = 1;
      } else {
        _M0L6_2atmpS1433 = 0;
      }
      _M0L10total__lenS381 = _M0L10digit__lenS380 + _M0L6_2atmpS1433;
      _M0L6bufferS382
      = (uint16_t*)moonbit_make_string(_M0L10total__lenS381, 0);
      if (_M0L12is__negativeS369) {
        _M0L12digit__startS383 = 1;
      } else {
        _M0L12digit__startS383 = 0;
      }
      #line 593 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0FPB26int64__to__string__generic(_M0L6bufferS382, _M0L3numS370, _M0L12digit__startS383, _M0L10total__lenS381, _M0L5radixS367);
      _M0L6bufferS371 = _M0L6bufferS382;
      break;
    }
  }
  if (_M0L12is__negativeS369) {
    _M0L6bufferS371[0] = 45;
  }
  return _M0L6bufferS371;
}

int32_t _M0FPB22int64__to__string__dec(
  uint16_t* _M0L6bufferS353,
  uint64_t _M0L3numS365,
  int32_t _M0L12digit__startS354,
  int32_t _M0L10total__lenS366
) {
  int32_t _M0L6_2atmpS1430;
  uint64_t _M0L3numS343;
  int32_t _M0L6offsetS344;
  #line 493 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L6_2atmpS1430 = _M0L10total__lenS366 - _M0L12digit__startS354;
  _M0L3numS343 = _M0L3numS365;
  _M0L6offsetS344 = _M0L6_2atmpS1430;
  while (1) {
    if (_M0L3numS343 >= 10000ull) {
      uint64_t _M0L1tS345 = _M0L3numS343 / 10000ull;
      uint64_t _M0L6_2atmpS1407 = _M0L3numS343 % 10000ull;
      int32_t _M0L1rS346 = (int32_t)_M0L6_2atmpS1407;
      int32_t _M0L2d1S347 = _M0L1rS346 / 100;
      int32_t _M0L2d2S348 = _M0L1rS346 % 100;
      int32_t _M0L6_2atmpS1406 = _M0L2d1S347 / 10;
      int32_t _M0L6_2atmpS1405 = 48 + _M0L6_2atmpS1406;
      int32_t _M0L6d1__hiS349 = (uint16_t)_M0L6_2atmpS1405;
      int32_t _M0L6_2atmpS1404 = _M0L2d1S347 % 10;
      int32_t _M0L6_2atmpS1403 = 48 + _M0L6_2atmpS1404;
      int32_t _M0L6d1__loS350 = (uint16_t)_M0L6_2atmpS1403;
      int32_t _M0L6_2atmpS1402 = _M0L2d2S348 / 10;
      int32_t _M0L6_2atmpS1401 = 48 + _M0L6_2atmpS1402;
      int32_t _M0L6d2__hiS351 = (uint16_t)_M0L6_2atmpS1401;
      int32_t _M0L6_2atmpS1400 = _M0L2d2S348 % 10;
      int32_t _M0L6_2atmpS1399 = 48 + _M0L6_2atmpS1400;
      int32_t _M0L6d2__loS352 = (uint16_t)_M0L6_2atmpS1399;
      int32_t _M0L6_2atmpS1391 = _M0L12digit__startS354 + _M0L6offsetS344;
      int32_t _M0L6_2atmpS1390 = _M0L6_2atmpS1391 - 4;
      int32_t _M0L6_2atmpS1393;
      int32_t _M0L6_2atmpS1392;
      int32_t _M0L6_2atmpS1395;
      int32_t _M0L6_2atmpS1394;
      int32_t _M0L6_2atmpS1397;
      int32_t _M0L6_2atmpS1396;
      int32_t _M0L6_2atmpS1398;
      _M0L6bufferS353[_M0L6_2atmpS1390] = _M0L6d1__hiS349;
      _M0L6_2atmpS1393 = _M0L12digit__startS354 + _M0L6offsetS344;
      _M0L6_2atmpS1392 = _M0L6_2atmpS1393 - 3;
      _M0L6bufferS353[_M0L6_2atmpS1392] = _M0L6d1__loS350;
      _M0L6_2atmpS1395 = _M0L12digit__startS354 + _M0L6offsetS344;
      _M0L6_2atmpS1394 = _M0L6_2atmpS1395 - 2;
      _M0L6bufferS353[_M0L6_2atmpS1394] = _M0L6d2__hiS351;
      _M0L6_2atmpS1397 = _M0L12digit__startS354 + _M0L6offsetS344;
      _M0L6_2atmpS1396 = _M0L6_2atmpS1397 - 1;
      _M0L6bufferS353[_M0L6_2atmpS1396] = _M0L6d2__loS352;
      _M0L6_2atmpS1398 = _M0L6offsetS344 - 4;
      _M0L3numS343 = _M0L1tS345;
      _M0L6offsetS344 = _M0L6_2atmpS1398;
      continue;
    } else {
      int32_t _M0L6_2atmpS1429 = (int32_t)_M0L3numS343;
      int32_t _M0L9remainingS356 = _M0L6_2atmpS1429;
      int32_t _M0L6offsetS357 = _M0L6offsetS344;
      while (1) {
        if (_M0L9remainingS356 >= 100) {
          int32_t _M0L1tS358 = _M0L9remainingS356 / 100;
          int32_t _M0L1dS359 = _M0L9remainingS356 % 100;
          int32_t _M0L6_2atmpS1416 = _M0L1dS359 / 10;
          int32_t _M0L6_2atmpS1415 = 48 + _M0L6_2atmpS1416;
          int32_t _M0L5d__hiS360 = (uint16_t)_M0L6_2atmpS1415;
          int32_t _M0L6_2atmpS1414 = _M0L1dS359 % 10;
          int32_t _M0L6_2atmpS1413 = 48 + _M0L6_2atmpS1414;
          int32_t _M0L5d__loS361 = (uint16_t)_M0L6_2atmpS1413;
          int32_t _M0L6_2atmpS1409 = _M0L12digit__startS354 + _M0L6offsetS357;
          int32_t _M0L6_2atmpS1408 = _M0L6_2atmpS1409 - 2;
          int32_t _M0L6_2atmpS1411;
          int32_t _M0L6_2atmpS1410;
          int32_t _M0L6_2atmpS1412;
          _M0L6bufferS353[_M0L6_2atmpS1408] = _M0L5d__hiS360;
          _M0L6_2atmpS1411 = _M0L12digit__startS354 + _M0L6offsetS357;
          _M0L6_2atmpS1410 = _M0L6_2atmpS1411 - 1;
          _M0L6bufferS353[_M0L6_2atmpS1410] = _M0L5d__loS361;
          _M0L6_2atmpS1412 = _M0L6offsetS357 - 2;
          _M0L9remainingS356 = _M0L1tS358;
          _M0L6offsetS357 = _M0L6_2atmpS1412;
          continue;
        } else if (_M0L9remainingS356 >= 10) {
          int32_t _M0L6_2atmpS1424 = _M0L9remainingS356 / 10;
          int32_t _M0L6_2atmpS1423 = 48 + _M0L6_2atmpS1424;
          int32_t _M0L5d__hiS363 = (uint16_t)_M0L6_2atmpS1423;
          int32_t _M0L6_2atmpS1422 = _M0L9remainingS356 % 10;
          int32_t _M0L6_2atmpS1421 = 48 + _M0L6_2atmpS1422;
          int32_t _M0L5d__loS364 = (uint16_t)_M0L6_2atmpS1421;
          int32_t _M0L6_2atmpS1418 = _M0L12digit__startS354 + _M0L6offsetS357;
          int32_t _M0L6_2atmpS1417 = _M0L6_2atmpS1418 - 2;
          int32_t _M0L6_2atmpS1420;
          int32_t _M0L6_2atmpS1419;
          _M0L6bufferS353[_M0L6_2atmpS1417] = _M0L5d__hiS363;
          _M0L6_2atmpS1420 = _M0L12digit__startS354 + _M0L6offsetS357;
          _M0L6_2atmpS1419 = _M0L6_2atmpS1420 - 1;
          _M0L6bufferS353[_M0L6_2atmpS1419] = _M0L5d__loS364;
        } else {
          int32_t _M0L6_2atmpS1428 = _M0L12digit__startS354 + _M0L6offsetS357;
          int32_t _M0L6_2atmpS1425 = _M0L6_2atmpS1428 - 1;
          int32_t _M0L6_2atmpS1427 = 48 + _M0L9remainingS356;
          int32_t _M0L6_2atmpS1426 = (uint16_t)_M0L6_2atmpS1427;
          _M0L6bufferS353[_M0L6_2atmpS1425] = _M0L6_2atmpS1426;
        }
        break;
      }
    }
    break;
  }
  return 0;
}

int32_t _M0FPB26int64__to__string__generic(
  uint16_t* _M0L6bufferS333,
  uint64_t _M0L3numS337,
  int32_t _M0L12digit__startS334,
  int32_t _M0L10total__lenS336,
  int32_t _M0L5radixS327
) {
  uint64_t _M0L4baseS326;
  int32_t _M0L6_2atmpS1375;
  int32_t _M0L6_2atmpS1374;
  #line 462 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  #line 470 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L4baseS326 = _M0MPC13int3Int10to__uint64(_M0L5radixS327);
  _M0L6_2atmpS1375 = _M0L5radixS327 - 1;
  _M0L6_2atmpS1374 = _M0L5radixS327 & _M0L6_2atmpS1375;
  if (_M0L6_2atmpS1374 == 0) {
    int32_t _M0L5shiftS328;
    uint64_t _M0L4maskS329;
    int32_t _M0L6_2atmpS1382;
    int32_t _M0L6offsetS330;
    uint64_t _M0L1nS331;
    #line 473 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0L5shiftS328 = moonbit_ctz32(_M0L5radixS327);
    _M0L4maskS329 = _M0L4baseS326 - 1ull;
    _M0L6_2atmpS1382 = _M0L10total__lenS336 - _M0L12digit__startS334;
    _M0L6offsetS330 = _M0L6_2atmpS1382;
    _M0L1nS331 = _M0L3numS337;
    while (1) {
      if (_M0L1nS331 > 0ull) {
        uint64_t _M0L6_2atmpS1381 = _M0L1nS331 & _M0L4maskS329;
        int32_t _M0L5digitS332 = (int32_t)_M0L6_2atmpS1381;
        int32_t _M0L6_2atmpS1378 = _M0L12digit__startS334 + _M0L6offsetS330;
        int32_t _M0L6_2atmpS1376 = _M0L6_2atmpS1378 - 1;
        int32_t _M0L6_2atmpS1377 =
          ((moonbit_string_t)moonbit_string_literal_24.data)[_M0L5digitS332];
        int32_t _M0L6_2atmpS1379;
        uint64_t _M0L6_2atmpS1380;
        _M0L6bufferS333[_M0L6_2atmpS1376] = _M0L6_2atmpS1377;
        _M0L6_2atmpS1379 = _M0L6offsetS330 - 1;
        _M0L6_2atmpS1380 = _M0L1nS331 >> (_M0L5shiftS328 & 63);
        _M0L6offsetS330 = _M0L6_2atmpS1379;
        _M0L1nS331 = _M0L6_2atmpS1380;
        continue;
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1389 = _M0L10total__lenS336 - _M0L12digit__startS334;
    int32_t _M0L6offsetS338 = _M0L6_2atmpS1389;
    uint64_t _M0L1nS339 = _M0L3numS337;
    while (1) {
      if (_M0L1nS339 > 0ull) {
        uint64_t _M0L1qS340 = _M0L1nS339 / _M0L4baseS326;
        uint64_t _M0L6_2atmpS1388 = _M0L1qS340 * _M0L4baseS326;
        uint64_t _M0L6_2atmpS1387 = _M0L1nS339 - _M0L6_2atmpS1388;
        int32_t _M0L5digitS341 = (int32_t)_M0L6_2atmpS1387;
        int32_t _M0L6_2atmpS1385 = _M0L12digit__startS334 + _M0L6offsetS338;
        int32_t _M0L6_2atmpS1383 = _M0L6_2atmpS1385 - 1;
        int32_t _M0L6_2atmpS1384 =
          ((moonbit_string_t)moonbit_string_literal_24.data)[_M0L5digitS341];
        int32_t _M0L6_2atmpS1386;
        _M0L6bufferS333[_M0L6_2atmpS1383] = _M0L6_2atmpS1384;
        _M0L6_2atmpS1386 = _M0L6offsetS338 - 1;
        _M0L6offsetS338 = _M0L6_2atmpS1386;
        _M0L1nS339 = _M0L1qS340;
        continue;
      }
      break;
    }
  }
  return 0;
}

int32_t _M0FPB22int64__to__string__hex(
  uint16_t* _M0L6bufferS320,
  uint64_t _M0L3numS325,
  int32_t _M0L12digit__startS321,
  int32_t _M0L10total__lenS324
) {
  int32_t _M0L6_2atmpS1373;
  int32_t _M0L6offsetS315;
  uint64_t _M0L1nS316;
  #line 434 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L6_2atmpS1373 = _M0L10total__lenS324 - _M0L12digit__startS321;
  _M0L6offsetS315 = _M0L6_2atmpS1373;
  _M0L1nS316 = _M0L3numS325;
  while (1) {
    if (_M0L6offsetS315 >= 2) {
      uint64_t _M0L6_2atmpS1370 = _M0L1nS316 & 255ull;
      int32_t _M0L9byte__valS317 = (int32_t)_M0L6_2atmpS1370;
      int32_t _M0L2hiS318 = _M0L9byte__valS317 / 16;
      int32_t _M0L2loS319 = _M0L9byte__valS317 % 16;
      int32_t _M0L6_2atmpS1364 = _M0L12digit__startS321 + _M0L6offsetS315;
      int32_t _M0L6_2atmpS1362 = _M0L6_2atmpS1364 - 2;
      int32_t _M0L6_2atmpS1363 =
        ((moonbit_string_t)moonbit_string_literal_24.data)[_M0L2hiS318];
      int32_t _M0L6_2atmpS1367;
      int32_t _M0L6_2atmpS1365;
      int32_t _M0L6_2atmpS1366;
      int32_t _M0L6_2atmpS1368;
      uint64_t _M0L6_2atmpS1369;
      _M0L6bufferS320[_M0L6_2atmpS1362] = _M0L6_2atmpS1363;
      _M0L6_2atmpS1367 = _M0L12digit__startS321 + _M0L6offsetS315;
      _M0L6_2atmpS1365 = _M0L6_2atmpS1367 - 1;
      _M0L6_2atmpS1366
      = ((moonbit_string_t)moonbit_string_literal_24.data)[
        _M0L2loS319
      ];
      _M0L6bufferS320[_M0L6_2atmpS1365] = _M0L6_2atmpS1366;
      _M0L6_2atmpS1368 = _M0L6offsetS315 - 2;
      _M0L6_2atmpS1369 = _M0L1nS316 >> 8;
      _M0L6offsetS315 = _M0L6_2atmpS1368;
      _M0L1nS316 = _M0L6_2atmpS1369;
      continue;
    } else if (_M0L6offsetS315 == 1) {
      uint64_t _M0L6_2atmpS1372 = _M0L1nS316 & 15ull;
      int32_t _M0L6nibbleS323 = (int32_t)_M0L6_2atmpS1372;
      int32_t _M0L6_2atmpS1371 =
        ((moonbit_string_t)moonbit_string_literal_24.data)[_M0L6nibbleS323];
      _M0L6bufferS320[_M0L12digit__startS321] = _M0L6_2atmpS1371;
    }
    break;
  }
  return 0;
}

int32_t _M0FPB14radix__count64(
  uint64_t _M0L5valueS309,
  int32_t _M0L5radixS311
) {
  uint64_t _M0L4baseS310;
  uint64_t _M0L3numS312;
  int32_t _M0L5countS313;
  #line 419 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  if (_M0L5valueS309 == 0ull) {
    return 1;
  }
  #line 424 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L4baseS310 = _M0MPC13int3Int10to__uint64(_M0L5radixS311);
  _M0L3numS312 = _M0L5valueS309;
  _M0L5countS313 = 0;
  while (1) {
    if (_M0L3numS312 > 0ull) {
      uint64_t _M0L6_2atmpS1360 = _M0L3numS312 / _M0L4baseS310;
      int32_t _M0L6_2atmpS1361 = _M0L5countS313 + 1;
      _M0L3numS312 = _M0L6_2atmpS1360;
      _M0L5countS313 = _M0L6_2atmpS1361;
      continue;
    } else {
      return _M0L5countS313;
    }
    break;
  }
}

int32_t _M0FPB12hex__count64(uint64_t _M0L5valueS307) {
  #line 407 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  if (_M0L5valueS307 == 0ull) {
    return 1;
  } else {
    int32_t _M0L14leading__zerosS308;
    int32_t _M0L6_2atmpS1359;
    int32_t _M0L6_2atmpS1358;
    #line 412 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0L14leading__zerosS308 = moonbit_clz64(_M0L5valueS307);
    _M0L6_2atmpS1359 = 63 - _M0L14leading__zerosS308;
    _M0L6_2atmpS1358 = _M0L6_2atmpS1359 / 4;
    return _M0L6_2atmpS1358 + 1;
  }
}

int32_t _M0FPB12dec__count64(uint64_t _M0L5valueS306) {
  #line 343 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  if (_M0L5valueS306 >= 10000000000ull) {
    if (_M0L5valueS306 >= 100000000000000ull) {
      if (_M0L5valueS306 >= 10000000000000000ull) {
        if (_M0L5valueS306 >= 1000000000000000000ull) {
          if (_M0L5valueS306 >= 10000000000000000000ull) {
            return 20;
          } else {
            return 19;
          }
        } else if (_M0L5valueS306 >= 100000000000000000ull) {
          return 18;
        } else {
          return 17;
        }
      } else if (_M0L5valueS306 >= 1000000000000000ull) {
        return 16;
      } else {
        return 15;
      }
    } else if (_M0L5valueS306 >= 1000000000000ull) {
      if (_M0L5valueS306 >= 10000000000000ull) {
        return 14;
      } else {
        return 13;
      }
    } else if (_M0L5valueS306 >= 100000000000ull) {
      return 12;
    } else {
      return 11;
    }
  } else if (_M0L5valueS306 >= 100000ull) {
    if (_M0L5valueS306 >= 10000000ull) {
      if (_M0L5valueS306 >= 1000000000ull) {
        return 10;
      } else if (_M0L5valueS306 >= 100000000ull) {
        return 9;
      } else {
        return 8;
      }
    } else if (_M0L5valueS306 >= 1000000ull) {
      return 7;
    } else {
      return 6;
    }
  } else if (_M0L5valueS306 >= 1000ull) {
    if (_M0L5valueS306 >= 10000ull) {
      return 5;
    } else {
      return 4;
    }
  } else if (_M0L5valueS306 >= 100ull) {
    return 3;
  } else if (_M0L5valueS306 >= 10ull) {
    return 2;
  } else {
    return 1;
  }
}

moonbit_string_t _M0MPC13int3Int18to__string_2einner(
  int32_t _M0L4selfS290,
  int32_t _M0L5radixS289
) {
  int32_t _M0L12is__negativeS291;
  uint32_t _M0L3numS292;
  uint16_t* _M0L6bufferS293;
  #line 209 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  if (_M0L5radixS289 < 2 || _M0L5radixS289 > 36) {
    #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_23.data);
  }
  if (_M0L4selfS290 == 0) {
    return (moonbit_string_t)moonbit_string_literal_14.data;
  }
  _M0L12is__negativeS291 = _M0L4selfS290 < 0;
  if (_M0L12is__negativeS291) {
    int32_t _M0L6_2atmpS1357 = -_M0L4selfS290;
    _M0L3numS292 = *(uint32_t*)&_M0L6_2atmpS1357;
  } else {
    _M0L3numS292 = *(uint32_t*)&_M0L4selfS290;
  }
  switch (_M0L5radixS289) {
    case 10: {
      int32_t _M0L10digit__lenS294;
      int32_t _M0L6_2atmpS1354;
      int32_t _M0L10total__lenS295;
      uint16_t* _M0L6bufferS296;
      int32_t _M0L12digit__startS297;
      #line 235 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS294 = _M0FPB12dec__count32(_M0L3numS292);
      if (_M0L12is__negativeS291) {
        _M0L6_2atmpS1354 = 1;
      } else {
        _M0L6_2atmpS1354 = 0;
      }
      _M0L10total__lenS295 = _M0L10digit__lenS294 + _M0L6_2atmpS1354;
      _M0L6bufferS296
      = (uint16_t*)moonbit_make_string(_M0L10total__lenS295, 0);
      if (_M0L12is__negativeS291) {
        _M0L12digit__startS297 = 1;
      } else {
        _M0L12digit__startS297 = 0;
      }
      #line 239 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0FPB20int__to__string__dec(_M0L6bufferS296, _M0L3numS292, _M0L12digit__startS297, _M0L10total__lenS295);
      _M0L6bufferS293 = _M0L6bufferS296;
      break;
    }
    
    case 16: {
      int32_t _M0L10digit__lenS298;
      int32_t _M0L6_2atmpS1355;
      int32_t _M0L10total__lenS299;
      uint16_t* _M0L6bufferS300;
      int32_t _M0L12digit__startS301;
      #line 243 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS298 = _M0FPB12hex__count32(_M0L3numS292);
      if (_M0L12is__negativeS291) {
        _M0L6_2atmpS1355 = 1;
      } else {
        _M0L6_2atmpS1355 = 0;
      }
      _M0L10total__lenS299 = _M0L10digit__lenS298 + _M0L6_2atmpS1355;
      _M0L6bufferS300
      = (uint16_t*)moonbit_make_string(_M0L10total__lenS299, 0);
      if (_M0L12is__negativeS291) {
        _M0L12digit__startS301 = 1;
      } else {
        _M0L12digit__startS301 = 0;
      }
      #line 247 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0FPB20int__to__string__hex(_M0L6bufferS300, _M0L3numS292, _M0L12digit__startS301, _M0L10total__lenS299);
      _M0L6bufferS293 = _M0L6bufferS300;
      break;
    }
    default: {
      int32_t _M0L10digit__lenS302;
      int32_t _M0L6_2atmpS1356;
      int32_t _M0L10total__lenS303;
      uint16_t* _M0L6bufferS304;
      int32_t _M0L12digit__startS305;
      #line 251 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS302
      = _M0FPB14radix__count32(_M0L3numS292, _M0L5radixS289);
      if (_M0L12is__negativeS291) {
        _M0L6_2atmpS1356 = 1;
      } else {
        _M0L6_2atmpS1356 = 0;
      }
      _M0L10total__lenS303 = _M0L10digit__lenS302 + _M0L6_2atmpS1356;
      _M0L6bufferS304
      = (uint16_t*)moonbit_make_string(_M0L10total__lenS303, 0);
      if (_M0L12is__negativeS291) {
        _M0L12digit__startS305 = 1;
      } else {
        _M0L12digit__startS305 = 0;
      }
      #line 255 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0FPB24int__to__string__generic(_M0L6bufferS304, _M0L3numS292, _M0L12digit__startS305, _M0L10total__lenS303, _M0L5radixS289);
      _M0L6bufferS293 = _M0L6bufferS304;
      break;
    }
  }
  if (_M0L12is__negativeS291) {
    _M0L6bufferS293[0] = 45;
  }
  return _M0L6bufferS293;
}

int32_t _M0FPB14radix__count32(
  uint32_t _M0L5valueS283,
  int32_t _M0L5radixS285
) {
  uint32_t _M0L4baseS284;
  uint32_t _M0L3numS286;
  int32_t _M0L5countS287;
  #line 189 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  if (_M0L5valueS283 == 0u) {
    return 1;
  }
  _M0L4baseS284 = *(uint32_t*)&_M0L5radixS285;
  _M0L3numS286 = _M0L5valueS283;
  _M0L5countS287 = 0;
  while (1) {
    if (_M0L3numS286 > 0u) {
      uint32_t _M0L6_2atmpS1352 = _M0L3numS286 / _M0L4baseS284;
      int32_t _M0L6_2atmpS1353 = _M0L5countS287 + 1;
      _M0L3numS286 = _M0L6_2atmpS1352;
      _M0L5countS287 = _M0L6_2atmpS1353;
      continue;
    } else {
      return _M0L5countS287;
    }
    break;
  }
}

int32_t _M0FPB12hex__count32(uint32_t _M0L5valueS281) {
  #line 177 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  if (_M0L5valueS281 == 0u) {
    return 1;
  } else {
    int32_t _M0L14leading__zerosS282;
    int32_t _M0L6_2atmpS1351;
    int32_t _M0L6_2atmpS1350;
    #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0L14leading__zerosS282 = moonbit_clz32(_M0L5valueS281);
    _M0L6_2atmpS1351 = 31 - _M0L14leading__zerosS282;
    _M0L6_2atmpS1350 = _M0L6_2atmpS1351 / 4;
    return _M0L6_2atmpS1350 + 1;
  }
}

int32_t _M0FPB12dec__count32(uint32_t _M0L5valueS280) {
  #line 143 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  if (_M0L5valueS280 >= 100000u) {
    if (_M0L5valueS280 >= 10000000u) {
      if (_M0L5valueS280 >= 1000000000u) {
        return 10;
      } else if (_M0L5valueS280 >= 100000000u) {
        return 9;
      } else {
        return 8;
      }
    } else if (_M0L5valueS280 >= 1000000u) {
      return 7;
    } else {
      return 6;
    }
  } else if (_M0L5valueS280 >= 1000u) {
    if (_M0L5valueS280 >= 10000u) {
      return 5;
    } else {
      return 4;
    }
  } else if (_M0L5valueS280 >= 100u) {
    return 3;
  } else if (_M0L5valueS280 >= 10u) {
    return 2;
  } else {
    return 1;
  }
}

int32_t _M0FPB20int__to__string__dec(
  uint16_t* _M0L6bufferS266,
  uint32_t _M0L3numS278,
  int32_t _M0L12digit__startS267,
  int32_t _M0L10total__lenS279
) {
  int32_t _M0L6_2atmpS1349;
  uint32_t _M0L3numS256;
  int32_t _M0L6offsetS257;
  #line 88 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L6_2atmpS1349 = _M0L10total__lenS279 - _M0L12digit__startS267;
  _M0L3numS256 = _M0L3numS278;
  _M0L6offsetS257 = _M0L6_2atmpS1349;
  while (1) {
    if (_M0L3numS256 >= 10000u) {
      uint32_t _M0L1tS258 = _M0L3numS256 / 10000u;
      uint32_t _M0L6_2atmpS1326 = _M0L3numS256 % 10000u;
      int32_t _M0L1rS259 = *(int32_t*)&_M0L6_2atmpS1326;
      int32_t _M0L2d1S260 = _M0L1rS259 / 100;
      int32_t _M0L2d2S261 = _M0L1rS259 % 100;
      int32_t _M0L6_2atmpS1325 = _M0L2d1S260 / 10;
      int32_t _M0L6_2atmpS1324 = 48 + _M0L6_2atmpS1325;
      int32_t _M0L6d1__hiS262 = (uint16_t)_M0L6_2atmpS1324;
      int32_t _M0L6_2atmpS1323 = _M0L2d1S260 % 10;
      int32_t _M0L6_2atmpS1322 = 48 + _M0L6_2atmpS1323;
      int32_t _M0L6d1__loS263 = (uint16_t)_M0L6_2atmpS1322;
      int32_t _M0L6_2atmpS1321 = _M0L2d2S261 / 10;
      int32_t _M0L6_2atmpS1320 = 48 + _M0L6_2atmpS1321;
      int32_t _M0L6d2__hiS264 = (uint16_t)_M0L6_2atmpS1320;
      int32_t _M0L6_2atmpS1319 = _M0L2d2S261 % 10;
      int32_t _M0L6_2atmpS1318 = 48 + _M0L6_2atmpS1319;
      int32_t _M0L6d2__loS265 = (uint16_t)_M0L6_2atmpS1318;
      int32_t _M0L6_2atmpS1310 = _M0L12digit__startS267 + _M0L6offsetS257;
      int32_t _M0L6_2atmpS1309 = _M0L6_2atmpS1310 - 4;
      int32_t _M0L6_2atmpS1312;
      int32_t _M0L6_2atmpS1311;
      int32_t _M0L6_2atmpS1314;
      int32_t _M0L6_2atmpS1313;
      int32_t _M0L6_2atmpS1316;
      int32_t _M0L6_2atmpS1315;
      int32_t _M0L6_2atmpS1317;
      _M0L6bufferS266[_M0L6_2atmpS1309] = _M0L6d1__hiS262;
      _M0L6_2atmpS1312 = _M0L12digit__startS267 + _M0L6offsetS257;
      _M0L6_2atmpS1311 = _M0L6_2atmpS1312 - 3;
      _M0L6bufferS266[_M0L6_2atmpS1311] = _M0L6d1__loS263;
      _M0L6_2atmpS1314 = _M0L12digit__startS267 + _M0L6offsetS257;
      _M0L6_2atmpS1313 = _M0L6_2atmpS1314 - 2;
      _M0L6bufferS266[_M0L6_2atmpS1313] = _M0L6d2__hiS264;
      _M0L6_2atmpS1316 = _M0L12digit__startS267 + _M0L6offsetS257;
      _M0L6_2atmpS1315 = _M0L6_2atmpS1316 - 1;
      _M0L6bufferS266[_M0L6_2atmpS1315] = _M0L6d2__loS265;
      _M0L6_2atmpS1317 = _M0L6offsetS257 - 4;
      _M0L3numS256 = _M0L1tS258;
      _M0L6offsetS257 = _M0L6_2atmpS1317;
      continue;
    } else {
      int32_t _M0L6_2atmpS1348 = *(int32_t*)&_M0L3numS256;
      int32_t _M0L9remainingS269 = _M0L6_2atmpS1348;
      int32_t _M0L6offsetS270 = _M0L6offsetS257;
      while (1) {
        if (_M0L9remainingS269 >= 100) {
          int32_t _M0L1tS271 = _M0L9remainingS269 / 100;
          int32_t _M0L1dS272 = _M0L9remainingS269 % 100;
          int32_t _M0L6_2atmpS1335 = _M0L1dS272 / 10;
          int32_t _M0L6_2atmpS1334 = 48 + _M0L6_2atmpS1335;
          int32_t _M0L5d__hiS273 = (uint16_t)_M0L6_2atmpS1334;
          int32_t _M0L6_2atmpS1333 = _M0L1dS272 % 10;
          int32_t _M0L6_2atmpS1332 = 48 + _M0L6_2atmpS1333;
          int32_t _M0L5d__loS274 = (uint16_t)_M0L6_2atmpS1332;
          int32_t _M0L6_2atmpS1328 = _M0L12digit__startS267 + _M0L6offsetS270;
          int32_t _M0L6_2atmpS1327 = _M0L6_2atmpS1328 - 2;
          int32_t _M0L6_2atmpS1330;
          int32_t _M0L6_2atmpS1329;
          int32_t _M0L6_2atmpS1331;
          _M0L6bufferS266[_M0L6_2atmpS1327] = _M0L5d__hiS273;
          _M0L6_2atmpS1330 = _M0L12digit__startS267 + _M0L6offsetS270;
          _M0L6_2atmpS1329 = _M0L6_2atmpS1330 - 1;
          _M0L6bufferS266[_M0L6_2atmpS1329] = _M0L5d__loS274;
          _M0L6_2atmpS1331 = _M0L6offsetS270 - 2;
          _M0L9remainingS269 = _M0L1tS271;
          _M0L6offsetS270 = _M0L6_2atmpS1331;
          continue;
        } else if (_M0L9remainingS269 >= 10) {
          int32_t _M0L6_2atmpS1343 = _M0L9remainingS269 / 10;
          int32_t _M0L6_2atmpS1342 = 48 + _M0L6_2atmpS1343;
          int32_t _M0L5d__hiS276 = (uint16_t)_M0L6_2atmpS1342;
          int32_t _M0L6_2atmpS1341 = _M0L9remainingS269 % 10;
          int32_t _M0L6_2atmpS1340 = 48 + _M0L6_2atmpS1341;
          int32_t _M0L5d__loS277 = (uint16_t)_M0L6_2atmpS1340;
          int32_t _M0L6_2atmpS1337 = _M0L12digit__startS267 + _M0L6offsetS270;
          int32_t _M0L6_2atmpS1336 = _M0L6_2atmpS1337 - 2;
          int32_t _M0L6_2atmpS1339;
          int32_t _M0L6_2atmpS1338;
          _M0L6bufferS266[_M0L6_2atmpS1336] = _M0L5d__hiS276;
          _M0L6_2atmpS1339 = _M0L12digit__startS267 + _M0L6offsetS270;
          _M0L6_2atmpS1338 = _M0L6_2atmpS1339 - 1;
          _M0L6bufferS266[_M0L6_2atmpS1338] = _M0L5d__loS277;
        } else {
          int32_t _M0L6_2atmpS1347 = _M0L12digit__startS267 + _M0L6offsetS270;
          int32_t _M0L6_2atmpS1344 = _M0L6_2atmpS1347 - 1;
          int32_t _M0L6_2atmpS1346 = 48 + _M0L9remainingS269;
          int32_t _M0L6_2atmpS1345 = (uint16_t)_M0L6_2atmpS1346;
          _M0L6bufferS266[_M0L6_2atmpS1344] = _M0L6_2atmpS1345;
        }
        break;
      }
    }
    break;
  }
  return 0;
}

int32_t _M0FPB24int__to__string__generic(
  uint16_t* _M0L6bufferS246,
  uint32_t _M0L3numS250,
  int32_t _M0L12digit__startS247,
  int32_t _M0L10total__lenS249,
  int32_t _M0L5radixS240
) {
  uint32_t _M0L4baseS239;
  int32_t _M0L6_2atmpS1294;
  int32_t _M0L6_2atmpS1293;
  #line 57 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L4baseS239 = *(uint32_t*)&_M0L5radixS240;
  _M0L6_2atmpS1294 = _M0L5radixS240 - 1;
  _M0L6_2atmpS1293 = _M0L5radixS240 & _M0L6_2atmpS1294;
  if (_M0L6_2atmpS1293 == 0) {
    int32_t _M0L5shiftS241;
    uint32_t _M0L4maskS242;
    int32_t _M0L6_2atmpS1301;
    int32_t _M0L6offsetS243;
    uint32_t _M0L1nS244;
    #line 68 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0L5shiftS241 = moonbit_ctz32(_M0L5radixS240);
    _M0L4maskS242 = _M0L4baseS239 - 1u;
    _M0L6_2atmpS1301 = _M0L10total__lenS249 - _M0L12digit__startS247;
    _M0L6offsetS243 = _M0L6_2atmpS1301;
    _M0L1nS244 = _M0L3numS250;
    while (1) {
      if (_M0L1nS244 > 0u) {
        uint32_t _M0L6_2atmpS1300 = _M0L1nS244 & _M0L4maskS242;
        int32_t _M0L5digitS245 = *(int32_t*)&_M0L6_2atmpS1300;
        int32_t _M0L6_2atmpS1297 = _M0L12digit__startS247 + _M0L6offsetS243;
        int32_t _M0L6_2atmpS1295 = _M0L6_2atmpS1297 - 1;
        int32_t _M0L6_2atmpS1296 =
          ((moonbit_string_t)moonbit_string_literal_24.data)[_M0L5digitS245];
        int32_t _M0L6_2atmpS1298;
        uint32_t _M0L6_2atmpS1299;
        _M0L6bufferS246[_M0L6_2atmpS1295] = _M0L6_2atmpS1296;
        _M0L6_2atmpS1298 = _M0L6offsetS243 - 1;
        _M0L6_2atmpS1299 = _M0L1nS244 >> (_M0L5shiftS241 & 31);
        _M0L6offsetS243 = _M0L6_2atmpS1298;
        _M0L1nS244 = _M0L6_2atmpS1299;
        continue;
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1308 = _M0L10total__lenS249 - _M0L12digit__startS247;
    int32_t _M0L6offsetS251 = _M0L6_2atmpS1308;
    uint32_t _M0L1nS252 = _M0L3numS250;
    while (1) {
      if (_M0L1nS252 > 0u) {
        uint32_t _M0L1qS253 = _M0L1nS252 / _M0L4baseS239;
        uint32_t _M0L6_2atmpS1307 = _M0L1qS253 * _M0L4baseS239;
        uint32_t _M0L6_2atmpS1306 = _M0L1nS252 - _M0L6_2atmpS1307;
        int32_t _M0L5digitS254 = *(int32_t*)&_M0L6_2atmpS1306;
        int32_t _M0L6_2atmpS1304 = _M0L12digit__startS247 + _M0L6offsetS251;
        int32_t _M0L6_2atmpS1302 = _M0L6_2atmpS1304 - 1;
        int32_t _M0L6_2atmpS1303 =
          ((moonbit_string_t)moonbit_string_literal_24.data)[_M0L5digitS254];
        int32_t _M0L6_2atmpS1305;
        _M0L6bufferS246[_M0L6_2atmpS1302] = _M0L6_2atmpS1303;
        _M0L6_2atmpS1305 = _M0L6offsetS251 - 1;
        _M0L6offsetS251 = _M0L6_2atmpS1305;
        _M0L1nS252 = _M0L1qS253;
        continue;
      }
      break;
    }
  }
  return 0;
}

int32_t _M0FPB20int__to__string__hex(
  uint16_t* _M0L6bufferS233,
  uint32_t _M0L3numS238,
  int32_t _M0L12digit__startS234,
  int32_t _M0L10total__lenS237
) {
  int32_t _M0L6_2atmpS1292;
  int32_t _M0L6offsetS228;
  uint32_t _M0L1nS229;
  #line 29 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L6_2atmpS1292 = _M0L10total__lenS237 - _M0L12digit__startS234;
  _M0L6offsetS228 = _M0L6_2atmpS1292;
  _M0L1nS229 = _M0L3numS238;
  while (1) {
    if (_M0L6offsetS228 >= 2) {
      uint32_t _M0L6_2atmpS1289 = _M0L1nS229 & 255u;
      int32_t _M0L9byte__valS230 = *(int32_t*)&_M0L6_2atmpS1289;
      int32_t _M0L2hiS231 = _M0L9byte__valS230 / 16;
      int32_t _M0L2loS232 = _M0L9byte__valS230 % 16;
      int32_t _M0L6_2atmpS1283 = _M0L12digit__startS234 + _M0L6offsetS228;
      int32_t _M0L6_2atmpS1281 = _M0L6_2atmpS1283 - 2;
      int32_t _M0L6_2atmpS1282 =
        ((moonbit_string_t)moonbit_string_literal_24.data)[_M0L2hiS231];
      int32_t _M0L6_2atmpS1286;
      int32_t _M0L6_2atmpS1284;
      int32_t _M0L6_2atmpS1285;
      int32_t _M0L6_2atmpS1287;
      uint32_t _M0L6_2atmpS1288;
      _M0L6bufferS233[_M0L6_2atmpS1281] = _M0L6_2atmpS1282;
      _M0L6_2atmpS1286 = _M0L12digit__startS234 + _M0L6offsetS228;
      _M0L6_2atmpS1284 = _M0L6_2atmpS1286 - 1;
      _M0L6_2atmpS1285
      = ((moonbit_string_t)moonbit_string_literal_24.data)[
        _M0L2loS232
      ];
      _M0L6bufferS233[_M0L6_2atmpS1284] = _M0L6_2atmpS1285;
      _M0L6_2atmpS1287 = _M0L6offsetS228 - 2;
      _M0L6_2atmpS1288 = _M0L1nS229 >> 8;
      _M0L6offsetS228 = _M0L6_2atmpS1287;
      _M0L1nS229 = _M0L6_2atmpS1288;
      continue;
    } else if (_M0L6offsetS228 == 1) {
      uint32_t _M0L6_2atmpS1291 = _M0L1nS229 & 15u;
      int32_t _M0L6nibbleS236 = *(int32_t*)&_M0L6_2atmpS1291;
      int32_t _M0L6_2atmpS1290 =
        ((moonbit_string_t)moonbit_string_literal_24.data)[_M0L6nibbleS236];
      _M0L6bufferS233[_M0L12digit__startS234] = _M0L6_2atmpS1290;
    }
    break;
  }
  return 0;
}

moonbit_string_t _M0IP016_24default__implPB4Show10to__stringGRPB7FailureE(
  void* _M0L4selfS227
) {
  struct _M0TPB13StringBuilder* _M0L6loggerS226;
  struct _M0TPB6Logger _M0L6_2atmpS1280;
  moonbit_string_t _result_2309;
  #line 171 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  #line 172 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6loggerS226 = _M0MPB13StringBuilder21StringBuilder_2einner(0);
  moonbit_incref_cycle_free(_M0L6loggerS226);
  _M0L6_2atmpS1280
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L6loggerS226
  };
  #line 173 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0IPB7FailurePB4Show6output(_M0L4selfS227, _M0L6_2atmpS1280);
  if (_M0L6_2atmpS1280.$1) {
    moonbit_decref(_M0L6_2atmpS1280.$1);
  }
  #line 174 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _result_2309 = _M0MPB13StringBuilder10to__string(_M0L6loggerS226);
  moonbit_decref_cycle_free(_M0L6loggerS226);
  return _result_2309;
}

int32_t _M0IP016_24default__implPB4Show6outputGsE(
  moonbit_string_t _M0L4selfS221,
  struct _M0TPB6Logger _M0L6loggerS220
) {
  moonbit_string_t _M0L6_2atmpS1277;
  #line 165 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS1277 = _M0IPC16string6StringPB4Show10to__string(_M0L4selfS221);
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6loggerS220.$0->$method_0(_M0L6loggerS220.$1, _M0L6_2atmpS1277);
  moonbit_decref_cycle_free(_M0L6_2atmpS1277);
  return 0;
}

int32_t _M0IP016_24default__implPB4Show6outputGiE(
  int32_t _M0L4selfS223,
  struct _M0TPB6Logger _M0L6loggerS222
) {
  moonbit_string_t _M0L6_2atmpS1278;
  #line 165 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS1278 = _M0IPC13int3IntPB4Show10to__string(_M0L4selfS223);
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6loggerS222.$0->$method_0(_M0L6loggerS222.$1, _M0L6_2atmpS1278);
  moonbit_decref_cycle_free(_M0L6_2atmpS1278);
  return 0;
}

int32_t _M0IP016_24default__implPB4Show6outputGmE(
  uint64_t _M0L4selfS225,
  struct _M0TPB6Logger _M0L6loggerS224
) {
  moonbit_string_t _M0L6_2atmpS1279;
  #line 165 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS1279 = _M0IPC16uint646UInt64PB4Show10to__string(_M0L4selfS225);
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6loggerS224.$0->$method_0(_M0L6loggerS224.$1, _M0L6_2atmpS1279);
  moonbit_decref_cycle_free(_M0L6_2atmpS1279);
  return 0;
}

int32_t _M0MPC16string10StringView13start__offset(
  struct _M0TPC16string10StringView _M0L4selfS219
) {
  #line 99 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
  return _M0L4selfS219.$1;
}

moonbit_string_t _M0MPC16string10StringView4data(
  struct _M0TPC16string10StringView _M0L4selfS218
) {
  moonbit_string_t _M0L8_2afieldS2198;
  #line 92 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
  _M0L8_2afieldS2198 = _M0L4selfS218.$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS2198);
  return _M0L8_2afieldS2198;
}

int32_t _M0IP016_24default__implPB6Logger16write__substringGRPB13StringBuilderE(
  struct _M0TPB13StringBuilder* _M0L4selfS214,
  moonbit_string_t _M0L5valueS215,
  int32_t _M0L5startS216,
  int32_t _M0L3lenS217
) {
  int32_t _M0L6_2atmpS1276;
  int64_t _M0L6_2atmpS1275;
  struct _M0TPC16string10StringView _M0L6_2atmpS1274;
  #line 128 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS1276 = _M0L5startS216 + _M0L3lenS217;
  _M0L6_2atmpS1275 = (int64_t)_M0L6_2atmpS1276;
  #line 129 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS1274
  = _M0MPC16string6String21clamped__view_2einner(_M0L5valueS215, _M0L5startS216, _M0L6_2atmpS1275);
  #line 129 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0IPB13StringBuilderPB6Logger11write__view(_M0L4selfS214, _M0L6_2atmpS1274);
  moonbit_decref_cycle_free(_M0L6_2atmpS1274.$0);
  return 0;
}

struct _M0TPC16string10StringView _M0MPC16string6String21clamped__view_2einner(
  moonbit_string_t _M0L4selfS207,
  int32_t _M0L5startS209,
  int64_t _M0L3endS211
) {
  int32_t _M0L3lenS206;
  int32_t _M0Lm2loS208;
  int32_t _M0Lm2hiS210;
  int32_t _M0L6_2atmpS1258;
  int32_t _if__result_2310;
  int32_t _M0L6_2atmpS1266;
  int32_t _if__result_2311;
  int32_t _M0L6_2atmpS1268;
  int32_t _M0L6_2atmpS1269;
  #line 698 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
  _M0L3lenS206 = Moonbit_array_length(_M0L4selfS207);
  if (_M0L5startS209 < 0) {
    _M0Lm2loS208 = 0;
  } else if (_M0L5startS209 > _M0L3lenS206) {
    _M0Lm2loS208 = _M0L3lenS206;
  } else {
    _M0Lm2loS208 = _M0L5startS209;
  }
  if (_M0L3endS211 == 4294967296ll) {
    _M0Lm2hiS210 = _M0L3lenS206;
  } else {
    int64_t _M0L7_2aSomeS212 = _M0L3endS211;
    int32_t _M0L4_2aeS213 = (int32_t)_M0L7_2aSomeS212;
    if (_M0L4_2aeS213 < 0) {
      _M0Lm2hiS210 = 0;
    } else if (_M0L4_2aeS213 > _M0L3lenS206) {
      _M0Lm2hiS210 = _M0L3lenS206;
    } else {
      _M0Lm2hiS210 = _M0L4_2aeS213;
    }
  }
  _M0L6_2atmpS1258 = _M0Lm2loS208;
  if (_M0L6_2atmpS1258 > 0) {
    int32_t _M0L6_2atmpS1257 = _M0Lm2loS208;
    if (_M0L6_2atmpS1257 < _M0L3lenS206) {
      int32_t _M0L6_2atmpS1256 = _M0Lm2loS208;
      int32_t _M0L6_2atmpS1255 = _M0L4selfS207[_M0L6_2atmpS1256];
      #line 712 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
      if (_M0MPC16uint166UInt1623is__trailing__surrogate(_M0L6_2atmpS1255)) {
        int32_t _M0L6_2atmpS1254 = _M0Lm2loS208;
        int32_t _M0L6_2atmpS1253 = _M0L6_2atmpS1254 - 1;
        int32_t _M0L6_2atmpS1252 = _M0L4selfS207[_M0L6_2atmpS1253];
        #line 713 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
        _if__result_2310
        = _M0MPC16uint166UInt1622is__leading__surrogate(_M0L6_2atmpS1252);
      } else {
        _if__result_2310 = 0;
      }
    } else {
      _if__result_2310 = 0;
    }
  } else {
    _if__result_2310 = 0;
  }
  if (_if__result_2310) {
    int32_t _M0L6_2atmpS1259 = _M0Lm2loS208;
    _M0Lm2loS208 = _M0L6_2atmpS1259 + 1;
  }
  _M0L6_2atmpS1266 = _M0Lm2hiS210;
  if (_M0L6_2atmpS1266 > 0) {
    int32_t _M0L6_2atmpS1265 = _M0Lm2hiS210;
    if (_M0L6_2atmpS1265 < _M0L3lenS206) {
      int32_t _M0L6_2atmpS1264 = _M0Lm2hiS210;
      int32_t _M0L6_2atmpS1263 = _M0L4selfS207[_M0L6_2atmpS1264];
      #line 718 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
      if (_M0MPC16uint166UInt1623is__trailing__surrogate(_M0L6_2atmpS1263)) {
        int32_t _M0L6_2atmpS1262 = _M0Lm2hiS210;
        int32_t _M0L6_2atmpS1261 = _M0L6_2atmpS1262 - 1;
        int32_t _M0L6_2atmpS1260 = _M0L4selfS207[_M0L6_2atmpS1261];
        #line 719 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
        _if__result_2311
        = _M0MPC16uint166UInt1622is__leading__surrogate(_M0L6_2atmpS1260);
      } else {
        _if__result_2311 = 0;
      }
    } else {
      _if__result_2311 = 0;
    }
  } else {
    _if__result_2311 = 0;
  }
  if (_if__result_2311) {
    int32_t _M0L6_2atmpS1267 = _M0Lm2hiS210;
    _M0Lm2hiS210 = _M0L6_2atmpS1267 - 1;
  }
  _M0L6_2atmpS1268 = _M0Lm2loS208;
  _M0L6_2atmpS1269 = _M0Lm2hiS210;
  if (_M0L6_2atmpS1268 >= _M0L6_2atmpS1269) {
    int32_t _M0L6_2atmpS1270 = _M0Lm2loS208;
    int32_t _M0L6_2atmpS1271 = _M0Lm2loS208;
    moonbit_incref_cycle_free(_M0L4selfS207);
    return (struct _M0TPC16string10StringView){.$0 = _M0L4selfS207,
                                                 .$1 = _M0L6_2atmpS1270,
                                                 .$2 = _M0L6_2atmpS1271};
  } else {
    int32_t _M0L6_2atmpS1272 = _M0Lm2loS208;
    int32_t _M0L6_2atmpS1273 = _M0Lm2hiS210;
    moonbit_incref_cycle_free(_M0L4selfS207);
    return (struct _M0TPC16string10StringView){.$0 = _M0L4selfS207,
                                                 .$1 = _M0L6_2atmpS1272,
                                                 .$2 = _M0L6_2atmpS1273};
  }
}

int32_t _M0IP016_24default__implPB6Logger5writeGRPB13StringBuilderE(
  struct _M0TPB13StringBuilder* _M0L4selfS205,
  struct _M0TPB4Show _M0L4showS204
) {
  struct _M0TPB6Logger _M0L6_2atmpS1251;
  #line 122 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  moonbit_incref_cycle_free(_M0L4selfS205);
  _M0L6_2atmpS1251
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS205
  };
  #line 123 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L4showS204.$0->$method_0(_M0L4showS204.$1, _M0L6_2atmpS1251);
  if (_M0L6_2atmpS1251.$1) {
    moonbit_decref(_M0L6_2atmpS1251.$1);
  }
  return 0;
}

int32_t _M0IP016_24default__implPB6Logger28write__string__interpolationGRPB13StringBuilderE(
  struct _M0TPB13StringBuilder* _M0L4selfS203,
  struct _M0TPB4Show _M0L4showS202
) {
  struct _M0TPB6Logger _M0L6_2atmpS1250;
  #line 117 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  moonbit_incref_cycle_free(_M0L4selfS203);
  _M0L6_2atmpS1250
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS203
  };
  #line 118 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L4showS202.$0->$method_0(_M0L4showS202.$1, _M0L6_2atmpS1250);
  if (_M0L6_2atmpS1250.$1) {
    moonbit_decref(_M0L6_2atmpS1250.$1);
  }
  return 0;
}

uint64_t _M0MPC13int3Int10to__uint64(int32_t _M0L4selfS201) {
  int64_t _M0L6_2atmpS1249;
  #line 989 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS1249 = (int64_t)_M0L4selfS201;
  return *(uint64_t*)&_M0L6_2atmpS1249;
}

int32_t _M0IPC16uint166UInt16PB7Default7default() {
  #line 201 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uint16_char.mbt"
  return 0;
}

moonbit_string_t _M0MPC16string6String14escape_2einner(
  moonbit_string_t _M0L4selfS199,
  int32_t _M0L5quoteS200
) {
  struct _M0TPB13StringBuilder* _M0L3bufS198;
  int32_t _M0L6_2atmpS1248;
  struct _M0TPC16string10StringView _M0L6_2atmpS1246;
  struct _M0TPB6Logger _M0L6_2atmpS1247;
  moonbit_string_t _result_2312;
  #line 110 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  #line 111 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L3bufS198 = _M0MPB13StringBuilder21StringBuilder_2einner(0);
  _M0L6_2atmpS1248 = Moonbit_array_length(_M0L4selfS199);
  moonbit_incref_cycle_free(_M0L4selfS199);
  _M0L6_2atmpS1246
  = (struct _M0TPC16string10StringView){
    .$0 = _M0L4selfS199, .$1 = 0, .$2 = _M0L6_2atmpS1248
  };
  moonbit_incref_cycle_free(_M0L3bufS198);
  _M0L6_2atmpS1247
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L3bufS198
  };
  #line 112 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0MPC16string10StringView18escape__to_2einner(_M0L6_2atmpS1246, _M0L6_2atmpS1247, _M0L5quoteS200);
  moonbit_decref_cycle_free(_M0L6_2atmpS1246.$0);
  if (_M0L6_2atmpS1247.$1) {
    moonbit_decref(_M0L6_2atmpS1247.$1);
  }
  #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _result_2312 = _M0MPB13StringBuilder10to__string(_M0L3bufS198);
  moonbit_decref_cycle_free(_M0L3bufS198);
  return _result_2312;
}

int32_t _M0MPC16string10StringView18escape__to_2einner(
  struct _M0TPC16string10StringView _M0L4selfS190,
  struct _M0TPB6Logger _M0L6loggerS188,
  int32_t _M0L5quoteS187
) {
  int32_t _M0L3endS1244;
  int32_t _M0L5startS1245;
  int32_t _M0L3lenS189;
  struct _M0TURPC16string10StringViewRPB6LoggerE* _M0L6_2aenvS191;
  int32_t _M0L1iS192;
  int32_t _M0L3segS193;
  #line 144 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  if (_M0L5quoteS187) {
    #line 150 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6loggerS188.$0->$method_3(_M0L6loggerS188.$1, 34);
  }
  _M0L3endS1244 = _M0L4selfS190.$2;
  _M0L5startS1245 = _M0L4selfS190.$1;
  _M0L3lenS189 = _M0L3endS1244 - _M0L5startS1245;
  moonbit_incref_cycle_free(_M0L4selfS190.$0);
  if (_M0L6loggerS188.$1) {
    moonbit_incref(_M0L6loggerS188.$1);
  }
  _M0L6_2aenvS191
  = (struct _M0TURPC16string10StringViewRPB6LoggerE*)moonbit_malloc(sizeof(struct _M0TURPC16string10StringViewRPB6LoggerE));
  Moonbit_object_header(_M0L6_2aenvS191)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 63, 0);
  _M0L6_2aenvS191->$0 = _M0L4selfS190;
  _M0L6_2aenvS191->$1 = _M0L6loggerS188;
  _M0L1iS192 = 0;
  _M0L3segS193 = 0;
  _2afor_194:;
  while (1) {
    moonbit_string_t _M0L3strS1241;
    int32_t _M0L5startS1243;
    int32_t _M0L6_2atmpS1242;
    int32_t _M0L4codeS195;
    int32_t _M0L1cS197;
    int32_t _M0L6_2atmpS1225;
    int32_t _M0L6_2atmpS1226;
    int32_t _M0L6_2atmpS1227;
    if (_M0L1iS192 >= _M0L3lenS189) {
      #line 160 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
      _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS191, _M0L3segS193, _M0L1iS192);
      moonbit_decref_cycle_free(_M0L6_2aenvS191);
      break;
    }
    _M0L3strS1241 = _M0L4selfS190.$0;
    _M0L5startS1243 = _M0L4selfS190.$1;
    _M0L6_2atmpS1242 = _M0L5startS1243 + _M0L1iS192;
    _M0L4codeS195 = _M0L3strS1241[_M0L6_2atmpS1242];
    switch (_M0L4codeS195) {
      case 34: {
        _M0L1cS197 = _M0L4codeS195;
        goto join_196;
        break;
      }
      
      case 92: {
        _M0L1cS197 = _M0L4codeS195;
        goto join_196;
        break;
      }
      
      case 10: {
        int32_t _M0L6_2atmpS1228;
        int32_t _M0L6_2atmpS1229;
        #line 172 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS191, _M0L3segS193, _M0L1iS192);
        #line 173 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0L6loggerS188.$0->$method_0(_M0L6loggerS188.$1, (moonbit_string_t)moonbit_string_literal_25.data);
        _M0L6_2atmpS1228 = _M0L1iS192 + 1;
        _M0L6_2atmpS1229 = _M0L1iS192 + 1;
        _M0L1iS192 = _M0L6_2atmpS1228;
        _M0L3segS193 = _M0L6_2atmpS1229;
        goto _2afor_194;
        break;
      }
      
      case 13: {
        int32_t _M0L6_2atmpS1230;
        int32_t _M0L6_2atmpS1231;
        #line 177 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS191, _M0L3segS193, _M0L1iS192);
        #line 178 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0L6loggerS188.$0->$method_0(_M0L6loggerS188.$1, (moonbit_string_t)moonbit_string_literal_26.data);
        _M0L6_2atmpS1230 = _M0L1iS192 + 1;
        _M0L6_2atmpS1231 = _M0L1iS192 + 1;
        _M0L1iS192 = _M0L6_2atmpS1230;
        _M0L3segS193 = _M0L6_2atmpS1231;
        goto _2afor_194;
        break;
      }
      
      case 8: {
        int32_t _M0L6_2atmpS1232;
        int32_t _M0L6_2atmpS1233;
        #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS191, _M0L3segS193, _M0L1iS192);
        #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0L6loggerS188.$0->$method_0(_M0L6loggerS188.$1, (moonbit_string_t)moonbit_string_literal_27.data);
        _M0L6_2atmpS1232 = _M0L1iS192 + 1;
        _M0L6_2atmpS1233 = _M0L1iS192 + 1;
        _M0L1iS192 = _M0L6_2atmpS1232;
        _M0L3segS193 = _M0L6_2atmpS1233;
        goto _2afor_194;
        break;
      }
      
      case 9: {
        int32_t _M0L6_2atmpS1234;
        int32_t _M0L6_2atmpS1235;
        #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS191, _M0L3segS193, _M0L1iS192);
        #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0L6loggerS188.$0->$method_0(_M0L6loggerS188.$1, (moonbit_string_t)moonbit_string_literal_28.data);
        _M0L6_2atmpS1234 = _M0L1iS192 + 1;
        _M0L6_2atmpS1235 = _M0L1iS192 + 1;
        _M0L1iS192 = _M0L6_2atmpS1234;
        _M0L3segS193 = _M0L6_2atmpS1235;
        goto _2afor_194;
        break;
      }
      default: {
        if (_M0L4codeS195 < 32) {
          int32_t _M0L6_2atmpS1237;
          moonbit_string_t _M0L6_2atmpS1236;
          int32_t _M0L6_2atmpS1238;
          int32_t _M0L6_2atmpS1239;
          #line 193 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS191, _M0L3segS193, _M0L1iS192);
          #line 194 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0L6loggerS188.$0->$method_0(_M0L6loggerS188.$1, (moonbit_string_t)moonbit_string_literal_29.data);
          _M0L6_2atmpS1237 = _M0L4codeS195 & 0xff;
          #line 194 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0L6_2atmpS1236 = _M0MPC14byte4Byte7to__hex(_M0L6_2atmpS1237);
          #line 194 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0L6loggerS188.$0->$method_0(_M0L6loggerS188.$1, _M0L6_2atmpS1236);
          moonbit_decref_cycle_free(_M0L6_2atmpS1236);
          #line 194 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0L6loggerS188.$0->$method_0(_M0L6loggerS188.$1, (moonbit_string_t)moonbit_string_literal_6.data);
          _M0L6_2atmpS1238 = _M0L1iS192 + 1;
          _M0L6_2atmpS1239 = _M0L1iS192 + 1;
          _M0L1iS192 = _M0L6_2atmpS1238;
          _M0L3segS193 = _M0L6_2atmpS1239;
          goto _2afor_194;
        } else {
          int32_t _M0L6_2atmpS1240 = _M0L1iS192 + 1;
          int32_t _tmp_2315 = _M0L3segS193;
          _M0L1iS192 = _M0L6_2atmpS1240;
          _M0L3segS193 = _tmp_2315;
          goto _2afor_194;
        }
        break;
      }
    }
    goto joinlet_2314;
    join_196:;
    #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS191, _M0L3segS193, _M0L1iS192);
    #line 167 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6loggerS188.$0->$method_3(_M0L6loggerS188.$1, 92);
    #line 168 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS1225 = _M0MPC16uint166UInt1616unsafe__to__char(_M0L1cS197);
    #line 168 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6loggerS188.$0->$method_3(_M0L6loggerS188.$1, _M0L6_2atmpS1225);
    _M0L6_2atmpS1226 = _M0L1iS192 + 1;
    _M0L6_2atmpS1227 = _M0L1iS192 + 1;
    _M0L1iS192 = _M0L6_2atmpS1226;
    _M0L3segS193 = _M0L6_2atmpS1227;
    continue;
    joinlet_2314:;
    break;
  }
  if (_M0L5quoteS187) {
    #line 202 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6loggerS188.$0->$method_3(_M0L6loggerS188.$1, 34);
  }
  return 0;
}

int32_t _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(
  struct _M0TURPC16string10StringViewRPB6LoggerE* _M0L6_2aenvS183,
  int32_t _M0L3segS186,
  int32_t _M0L1iS185
) {
  struct _M0TPB6Logger _M0L6loggerS182;
  struct _M0TPC16string10StringView _M0L4selfS184;
  #line 153 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L6loggerS182 = _M0L6_2aenvS183->$1;
  _M0L4selfS184 = _M0L6_2aenvS183->$0;
  if (_M0L1iS185 > _M0L3segS186) {
    int64_t _M0L6_2atmpS1224 = (int64_t)_M0L1iS185;
    struct _M0TPC16string10StringView _M0L6_2atmpS1223;
    #line 155 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS1223
    = _M0MPC16string10StringView21clamped__view_2einner(_M0L4selfS184, _M0L3segS186, _M0L6_2atmpS1224);
    #line 155 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6loggerS182.$0->$method_2(_M0L6loggerS182.$1, _M0L6_2atmpS1223);
    moonbit_decref_cycle_free(_M0L6_2atmpS1223.$0);
  }
  return 0;
}

struct _M0TPC16string10StringView _M0MPC16string10StringView21clamped__view_2einner(
  struct _M0TPC16string10StringView _M0L4selfS173,
  int32_t _M0L5startS175,
  int64_t _M0L3endS177
) {
  int32_t _M0L3endS1221;
  int32_t _M0L5startS1222;
  int32_t _M0L3lenS172;
  int32_t _M0Lm2loS174;
  int32_t _M0Lm2hiS176;
  moonbit_string_t _M0L3strS180;
  int32_t _M0L4baseS181;
  int32_t _M0L6_2atmpS1199;
  int32_t _if__result_2316;
  int32_t _M0L6_2atmpS1209;
  int32_t _if__result_2317;
  int32_t _M0L6_2atmpS1211;
  int32_t _M0L6_2atmpS1212;
  #line 748 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
  _M0L3endS1221 = _M0L4selfS173.$2;
  _M0L5startS1222 = _M0L4selfS173.$1;
  _M0L3lenS172 = _M0L3endS1221 - _M0L5startS1222;
  if (_M0L5startS175 < 0) {
    _M0Lm2loS174 = 0;
  } else if (_M0L5startS175 > _M0L3lenS172) {
    _M0Lm2loS174 = _M0L3lenS172;
  } else {
    _M0Lm2loS174 = _M0L5startS175;
  }
  if (_M0L3endS177 == 4294967296ll) {
    _M0Lm2hiS176 = _M0L3lenS172;
  } else {
    int64_t _M0L7_2aSomeS178 = _M0L3endS177;
    int32_t _M0L4_2aeS179 = (int32_t)_M0L7_2aSomeS178;
    if (_M0L4_2aeS179 < 0) {
      _M0Lm2hiS176 = 0;
    } else if (_M0L4_2aeS179 > _M0L3lenS172) {
      _M0Lm2hiS176 = _M0L3lenS172;
    } else {
      _M0Lm2hiS176 = _M0L4_2aeS179;
    }
  }
  _M0L3strS180 = _M0L4selfS173.$0;
  _M0L4baseS181 = _M0L4selfS173.$1;
  _M0L6_2atmpS1199 = _M0Lm2loS174;
  if (_M0L6_2atmpS1199 > 0) {
    int32_t _M0L6_2atmpS1198 = _M0Lm2loS174;
    if (_M0L6_2atmpS1198 < _M0L3lenS172) {
      int32_t _M0L6_2atmpS1197 = _M0Lm2loS174;
      int32_t _M0L6_2atmpS1196 = _M0L4baseS181 + _M0L6_2atmpS1197;
      int32_t _M0L6_2atmpS1195 = _M0L3strS180[_M0L6_2atmpS1196];
      #line 764 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
      if (_M0MPC16uint166UInt1623is__trailing__surrogate(_M0L6_2atmpS1195)) {
        int32_t _M0L6_2atmpS1194 = _M0Lm2loS174;
        int32_t _M0L6_2atmpS1193 = _M0L4baseS181 + _M0L6_2atmpS1194;
        int32_t _M0L6_2atmpS1192 = _M0L6_2atmpS1193 - 1;
        int32_t _M0L6_2atmpS1191 = _M0L3strS180[_M0L6_2atmpS1192];
        #line 765 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
        _if__result_2316
        = _M0MPC16uint166UInt1622is__leading__surrogate(_M0L6_2atmpS1191);
      } else {
        _if__result_2316 = 0;
      }
    } else {
      _if__result_2316 = 0;
    }
  } else {
    _if__result_2316 = 0;
  }
  if (_if__result_2316) {
    int32_t _M0L6_2atmpS1200 = _M0Lm2loS174;
    _M0Lm2loS174 = _M0L6_2atmpS1200 + 1;
  }
  _M0L6_2atmpS1209 = _M0Lm2hiS176;
  if (_M0L6_2atmpS1209 > 0) {
    int32_t _M0L6_2atmpS1208 = _M0Lm2hiS176;
    if (_M0L6_2atmpS1208 < _M0L3lenS172) {
      int32_t _M0L6_2atmpS1207 = _M0Lm2hiS176;
      int32_t _M0L6_2atmpS1206 = _M0L4baseS181 + _M0L6_2atmpS1207;
      int32_t _M0L6_2atmpS1205 = _M0L3strS180[_M0L6_2atmpS1206];
      #line 770 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
      if (_M0MPC16uint166UInt1623is__trailing__surrogate(_M0L6_2atmpS1205)) {
        int32_t _M0L6_2atmpS1204 = _M0Lm2hiS176;
        int32_t _M0L6_2atmpS1203 = _M0L4baseS181 + _M0L6_2atmpS1204;
        int32_t _M0L6_2atmpS1202 = _M0L6_2atmpS1203 - 1;
        int32_t _M0L6_2atmpS1201 = _M0L3strS180[_M0L6_2atmpS1202];
        #line 771 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
        _if__result_2317
        = _M0MPC16uint166UInt1622is__leading__surrogate(_M0L6_2atmpS1201);
      } else {
        _if__result_2317 = 0;
      }
    } else {
      _if__result_2317 = 0;
    }
  } else {
    _if__result_2317 = 0;
  }
  if (_if__result_2317) {
    int32_t _M0L6_2atmpS1210 = _M0Lm2hiS176;
    _M0Lm2hiS176 = _M0L6_2atmpS1210 - 1;
  }
  _M0L6_2atmpS1211 = _M0Lm2loS174;
  _M0L6_2atmpS1212 = _M0Lm2hiS176;
  if (_M0L6_2atmpS1211 >= _M0L6_2atmpS1212) {
    int32_t _M0L6_2atmpS1216 = _M0Lm2loS174;
    int32_t _M0L6_2atmpS1213 = _M0L4baseS181 + _M0L6_2atmpS1216;
    int32_t _M0L6_2atmpS1215 = _M0Lm2loS174;
    int32_t _M0L6_2atmpS1214 = _M0L4baseS181 + _M0L6_2atmpS1215;
    moonbit_incref_cycle_free(_M0L3strS180);
    return (struct _M0TPC16string10StringView){.$0 = _M0L3strS180,
                                                 .$1 = _M0L6_2atmpS1213,
                                                 .$2 = _M0L6_2atmpS1214};
  } else {
    int32_t _M0L6_2atmpS1220 = _M0Lm2loS174;
    int32_t _M0L6_2atmpS1217 = _M0L4baseS181 + _M0L6_2atmpS1220;
    int32_t _M0L6_2atmpS1219 = _M0Lm2hiS176;
    int32_t _M0L6_2atmpS1218 = _M0L4baseS181 + _M0L6_2atmpS1219;
    moonbit_incref_cycle_free(_M0L3strS180);
    return (struct _M0TPC16string10StringView){.$0 = _M0L3strS180,
                                                 .$1 = _M0L6_2atmpS1217,
                                                 .$2 = _M0L6_2atmpS1218};
  }
}

moonbit_string_t _M0MPC14byte4Byte7to__hex(int32_t _M0L1bS171) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS170;
  int32_t _M0L6_2atmpS1188;
  int32_t _M0L6_2atmpS1187;
  int32_t _M0L6_2atmpS1190;
  int32_t _M0L6_2atmpS1189;
  struct _M0TPB13StringBuilder* _M0L6_2atmpS1186;
  moonbit_string_t _result_2318;
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L7_2aselfS170 = _M0MPB13StringBuilder21StringBuilder_2einner(0);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L6_2atmpS1188 = _M0IPC14byte4BytePB3Div3div(_M0L1bS171, 16);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L6_2atmpS1187
  = _M0MPC14byte4Byte7to__hexN14to__hex__digitS4391(_M0L6_2atmpS1188);
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0IPB13StringBuilderPB6Logger11write__char(_M0L7_2aselfS170, _M0L6_2atmpS1187);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L6_2atmpS1190 = _M0IPC14byte4BytePB3Mod3mod(_M0L1bS171, 16);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L6_2atmpS1189
  = _M0MPC14byte4Byte7to__hexN14to__hex__digitS4391(_M0L6_2atmpS1190);
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0IPB13StringBuilderPB6Logger11write__char(_M0L7_2aselfS170, _M0L6_2atmpS1189);
  _M0L6_2atmpS1186 = _M0L7_2aselfS170;
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _result_2318 = _M0MPB13StringBuilder10to__string(_M0L6_2atmpS1186);
  moonbit_decref_cycle_free(_M0L6_2atmpS1186);
  return _result_2318;
}

int32_t _M0MPC14byte4Byte7to__hexN14to__hex__digitS4391(int32_t _M0L1iS169) {
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  if (_M0L1iS169 < 10) {
    int32_t _M0L6_2atmpS1183;
    #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS1183 = _M0IPC14byte4BytePB3Add3add(_M0L1iS169, 48);
    #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    return _M0MPC14byte4Byte8to__char(_M0L6_2atmpS1183);
  } else {
    int32_t _M0L6_2atmpS1185;
    int32_t _M0L6_2atmpS1184;
    #line 79 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS1185 = _M0IPC14byte4BytePB3Add3add(_M0L1iS169, 97);
    #line 79 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS1184 = _M0IPC14byte4BytePB3Sub3sub(_M0L6_2atmpS1185, 10);
    #line 79 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    return _M0MPC14byte4Byte8to__char(_M0L6_2atmpS1184);
  }
}

int32_t _M0IPC14byte4BytePB3Sub3sub(
  int32_t _M0L4selfS167,
  int32_t _M0L4thatS168
) {
  int32_t _M0L6_2atmpS1181;
  int32_t _M0L6_2atmpS1182;
  int32_t _M0L6_2atmpS1180;
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\byte.mbt"
  _M0L6_2atmpS1181 = (int32_t)_M0L4selfS167;
  _M0L6_2atmpS1182 = (int32_t)_M0L4thatS168;
  _M0L6_2atmpS1180 = _M0L6_2atmpS1181 - _M0L6_2atmpS1182;
  return _M0L6_2atmpS1180 & 0xff;
}

int32_t _M0IPC14byte4BytePB3Mod3mod(
  int32_t _M0L4selfS165,
  int32_t _M0L4thatS166
) {
  int32_t _M0L6_2atmpS1178;
  int32_t _M0L6_2atmpS1179;
  int32_t _M0L6_2atmpS1177;
  #line 80 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\byte.mbt"
  _M0L6_2atmpS1178 = (int32_t)_M0L4selfS165;
  _M0L6_2atmpS1179 = (int32_t)_M0L4thatS166;
  _M0L6_2atmpS1177 = _M0L6_2atmpS1178 % _M0L6_2atmpS1179;
  return _M0L6_2atmpS1177 & 0xff;
}

int32_t _M0IPC14byte4BytePB3Div3div(
  int32_t _M0L4selfS163,
  int32_t _M0L4thatS164
) {
  int32_t _M0L6_2atmpS1175;
  int32_t _M0L6_2atmpS1176;
  int32_t _M0L6_2atmpS1174;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\byte.mbt"
  _M0L6_2atmpS1175 = (int32_t)_M0L4selfS163;
  _M0L6_2atmpS1176 = (int32_t)_M0L4thatS164;
  _M0L6_2atmpS1174 = _M0L6_2atmpS1175 / _M0L6_2atmpS1176;
  return _M0L6_2atmpS1174 & 0xff;
}

int32_t _M0IPC14byte4BytePB3Add3add(
  int32_t _M0L4selfS161,
  int32_t _M0L4thatS162
) {
  int32_t _M0L6_2atmpS1172;
  int32_t _M0L6_2atmpS1173;
  int32_t _M0L6_2atmpS1171;
  #line 119 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\byte.mbt"
  _M0L6_2atmpS1172 = (int32_t)_M0L4selfS161;
  _M0L6_2atmpS1173 = (int32_t)_M0L4thatS162;
  _M0L6_2atmpS1171 = _M0L6_2atmpS1172 + _M0L6_2atmpS1173;
  return _M0L6_2atmpS1171 & 0xff;
}

int32_t _M0MPC16uint166UInt1616unsafe__to__char(int32_t _M0L4selfS160) {
  int32_t _M0L6_2atmpS1170;
  #line 81 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uint16_char.mbt"
  _M0L6_2atmpS1170 = (int32_t)_M0L4selfS160;
  return _M0L6_2atmpS1170;
}

int32_t _M0MPC16uint166UInt1623is__trailing__surrogate(int32_t _M0L4selfS159) {
  #line 58 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uint16_char.mbt"
  return _M0L4selfS159 >= 56320 && _M0L4selfS159 <= 57343;
}

int32_t _M0MPC16uint166UInt1622is__leading__surrogate(int32_t _M0L4selfS158) {
  #line 28 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uint16_char.mbt"
  return _M0L4selfS158 >= 55296 && _M0L4selfS158 <= 56319;
}

int32_t _M0IPB13StringBuilderPB6Logger13write__string(
  struct _M0TPB13StringBuilder* _M0L4selfS157,
  moonbit_string_t _M0L3strS155
) {
  int32_t _M0L8str__lenS154;
  int32_t _M0L3lenS1169;
  int32_t _M0L8requiredS156;
  uint16_t* _M0L4dataS1164;
  int32_t _M0L6_2atmpS1163;
  int32_t _if__result_2319;
  uint16_t* _M0L4dataS1165;
  int32_t _M0L3lenS1166;
  int32_t _M0L3lenS1168;
  int32_t _M0L6_2atmpS1167;
  #line 105 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L8str__lenS154 = Moonbit_array_length(_M0L3strS155);
  if (_M0L8str__lenS154 == 0) {
    return 0;
  }
  _M0L3lenS1169 = _M0L4selfS157->$1;
  _M0L8requiredS156 = _M0L3lenS1169 + _M0L8str__lenS154;
  _M0L4dataS1164 = _M0L4selfS157->$0;
  _M0L6_2atmpS1163 = Moonbit_array_length(_M0L4dataS1164);
  if (_M0L8requiredS156 > _M0L6_2atmpS1163) {
    _if__result_2319 = 1;
  } else {
    int32_t _M0L3lenS1162 = _M0L4selfS157->$1;
    _if__result_2319 = _M0L8requiredS156 < _M0L3lenS1162;
  }
  if (_if__result_2319) {
    #line 112 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0MPB13StringBuilder4grow(_M0L4selfS157, _M0L8requiredS156);
  }
  _M0L4dataS1165 = _M0L4selfS157->$0;
  _M0L3lenS1166 = _M0L4selfS157->$1;
  moonbit_incref_cycle_free(_M0L4dataS1165);
  #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0MPC15array10FixedArray26unsafe__blit__from__string(_M0L4dataS1165, _M0L3lenS1166, _M0L3strS155, 0, _M0L8str__lenS154);
  moonbit_decref_cycle_free(_M0L4dataS1165);
  _M0L3lenS1168 = _M0L4selfS157->$1;
  _M0L6_2atmpS1167 = _M0L3lenS1168 + _M0L8str__lenS154;
  _M0L4selfS157->$1 = _M0L6_2atmpS1167;
  return 0;
}

int32_t _M0MPC15array10FixedArray26unsafe__blit__from__string(
  uint16_t* _M0L4selfS150,
  int32_t _M0L11dst__offsetS153,
  moonbit_string_t _M0L3strS151,
  int32_t _M0L11str__offsetS146,
  int32_t _M0L3lenS147
) {
  int32_t _M0L16end__str__offsetS145;
  int32_t _M0L1iS148;
  int32_t _M0L1jS149;
  #line 90 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L16end__str__offsetS145 = _M0L11str__offsetS146 + _M0L3lenS147;
  _M0L1iS148 = _M0L11str__offsetS146;
  _M0L1jS149 = _M0L11dst__offsetS153;
  while (1) {
    if (_M0L1iS148 < _M0L16end__str__offsetS145) {
      int32_t _M0L6_2atmpS1159 = _M0L3strS151[_M0L1iS148];
      int32_t _M0L6_2atmpS1160;
      int32_t _M0L6_2atmpS1161;
      _M0L4selfS150[_M0L1jS149] = _M0L6_2atmpS1159;
      _M0L6_2atmpS1160 = _M0L1iS148 + 1;
      _M0L6_2atmpS1161 = _M0L1jS149 + 1;
      _M0L1iS148 = _M0L6_2atmpS1160;
      _M0L1jS149 = _M0L6_2atmpS1161;
      continue;
    }
    break;
  }
  return 0;
}

int32_t _M0IPB13StringBuilderPB6Logger11write__char(
  struct _M0TPB13StringBuilder* _M0L4selfS143,
  int32_t _M0L2chS142
) {
  uint32_t _M0L4codeS141;
  #line 120 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  #line 121 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L4codeS141 = _M0MPC14char4Char8to__uint(_M0L2chS142);
  if (_M0L4codeS141 <= 65535u) {
    int32_t _M0L3lenS1130 = _M0L4selfS143->$1;
    uint16_t* _M0L4dataS1132 = _M0L4selfS143->$0;
    int32_t _M0L6_2atmpS1131 = Moonbit_array_length(_M0L4dataS1132);
    uint16_t* _M0L4dataS1135;
    int32_t _M0L3lenS1136;
    int32_t _M0L6_2atmpS1137;
    int32_t _M0L3lenS1139;
    int32_t _M0L6_2atmpS1138;
    if (_M0L3lenS1130 >= _M0L6_2atmpS1131) {
      int32_t _M0L3lenS1134 = _M0L4selfS143->$1;
      int32_t _M0L6_2atmpS1133 = _M0L3lenS1134 + 1;
      #line 124 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0MPB13StringBuilder4grow(_M0L4selfS143, _M0L6_2atmpS1133);
    }
    _M0L4dataS1135 = _M0L4selfS143->$0;
    _M0L3lenS1136 = _M0L4selfS143->$1;
    moonbit_incref_cycle_free(_M0L4dataS1135);
    #line 126 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0L6_2atmpS1137 = _M0MPC14uint4UInt10to__uint16(_M0L4codeS141);
    if (
      _M0L3lenS1136 < 0
      || _M0L3lenS1136 >= Moonbit_array_length(_M0L4dataS1135)
    ) {
      #line 126 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      moonbit_panic();
    }
    _M0L4dataS1135[_M0L3lenS1136] = _M0L6_2atmpS1137;
    moonbit_decref_cycle_free(_M0L4dataS1135);
    _M0L3lenS1139 = _M0L4selfS143->$1;
    _M0L6_2atmpS1138 = _M0L3lenS1139 + 1;
    _M0L4selfS143->$1 = _M0L6_2atmpS1138;
  } else if (_M0L4codeS141 <= 1114111u) {
    uint16_t* _M0L4dataS1143 = _M0L4selfS143->$0;
    int32_t _M0L6_2atmpS1141 = Moonbit_array_length(_M0L4dataS1143);
    int32_t _M0L3lenS1142 = _M0L4selfS143->$1;
    int32_t _M0L6_2atmpS1140 = _M0L6_2atmpS1141 - _M0L3lenS1142;
    uint32_t _M0L4codeS144;
    uint16_t* _M0L4dataS1146;
    int32_t _M0L3lenS1147;
    uint32_t _M0L6_2atmpS1150;
    uint32_t _M0L6_2atmpS1149;
    int32_t _M0L6_2atmpS1148;
    uint16_t* _M0L4dataS1151;
    int32_t _M0L3lenS1156;
    int32_t _M0L6_2atmpS1152;
    uint32_t _M0L6_2atmpS1155;
    uint32_t _M0L6_2atmpS1154;
    int32_t _M0L6_2atmpS1153;
    int32_t _M0L3lenS1158;
    int32_t _M0L6_2atmpS1157;
    if (_M0L6_2atmpS1140 < 2) {
      int32_t _M0L3lenS1145 = _M0L4selfS143->$1;
      int32_t _M0L6_2atmpS1144 = _M0L3lenS1145 + 2;
      #line 130 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0MPB13StringBuilder4grow(_M0L4selfS143, _M0L6_2atmpS1144);
    }
    _M0L4codeS144 = _M0L4codeS141 - 65536u;
    _M0L4dataS1146 = _M0L4selfS143->$0;
    _M0L3lenS1147 = _M0L4selfS143->$1;
    _M0L6_2atmpS1150 = _M0L4codeS144 >> 10;
    _M0L6_2atmpS1149 = 55296u + _M0L6_2atmpS1150;
    moonbit_incref_cycle_free(_M0L4dataS1146);
    #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0L6_2atmpS1148 = _M0MPC14uint4UInt10to__uint16(_M0L6_2atmpS1149);
    if (
      _M0L3lenS1147 < 0
      || _M0L3lenS1147 >= Moonbit_array_length(_M0L4dataS1146)
    ) {
      #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      moonbit_panic();
    }
    _M0L4dataS1146[_M0L3lenS1147] = _M0L6_2atmpS1148;
    moonbit_decref_cycle_free(_M0L4dataS1146);
    _M0L4dataS1151 = _M0L4selfS143->$0;
    _M0L3lenS1156 = _M0L4selfS143->$1;
    _M0L6_2atmpS1152 = _M0L3lenS1156 + 1;
    _M0L6_2atmpS1155 = _M0L4codeS144 & 1023u;
    _M0L6_2atmpS1154 = 56320u + _M0L6_2atmpS1155;
    moonbit_incref_cycle_free(_M0L4dataS1151);
    #line 134 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0L6_2atmpS1153 = _M0MPC14uint4UInt10to__uint16(_M0L6_2atmpS1154);
    if (
      _M0L6_2atmpS1152 < 0
      || _M0L6_2atmpS1152 >= Moonbit_array_length(_M0L4dataS1151)
    ) {
      #line 134 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      moonbit_panic();
    }
    _M0L4dataS1151[_M0L6_2atmpS1152] = _M0L6_2atmpS1153;
    moonbit_decref_cycle_free(_M0L4dataS1151);
    _M0L3lenS1158 = _M0L4selfS143->$1;
    _M0L6_2atmpS1157 = _M0L3lenS1158 + 2;
    _M0L4selfS143->$1 = _M0L6_2atmpS1157;
  } else {
    #line 137 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_30.data);
  }
  return 0;
}

int32_t _M0MPB13StringBuilder4grow(
  struct _M0TPB13StringBuilder* _M0L4selfS138,
  int32_t _M0L8requiredS139
) {
  uint16_t* _M0L4dataS1129;
  int32_t _M0L6_2atmpS1127;
  int32_t _M0L3lenS1128;
  int32_t _M0L13new__capacityS137;
  uint16_t* _M0L4dataS1124;
  int32_t _M0L6_2atmpS1125;
  int32_t _M0L3lenS1126;
  uint16_t* _M0L9new__dataS140;
  uint16_t* _M0L6_2aoldS2199;
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L4dataS1129 = _M0L4selfS138->$0;
  _M0L6_2atmpS1127 = Moonbit_array_length(_M0L4dataS1129);
  _M0L3lenS1128 = _M0L4selfS138->$1;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L13new__capacityS137
  = _M0FPB31stringbuilder__growth__capacity(_M0L6_2atmpS1127, _M0L3lenS1128, _M0L8requiredS139);
  _M0L4dataS1124 = _M0L4selfS138->$0;
  moonbit_incref_cycle_free(_M0L4dataS1124);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L6_2atmpS1125 = _M0IPC16uint166UInt16PB7Default7default();
  _M0L3lenS1126 = _M0L4selfS138->$1;
  #line 80 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L9new__dataS140
  = _M0MPC15array10FixedArray23make__and__blit_2einnerGkE(_M0L4dataS1124, _M0L13new__capacityS137, _M0L6_2atmpS1125, _M0L3lenS1126, 0, 0);
  _M0L6_2aoldS2199 = _M0L4selfS138->$0;
  moonbit_decref_cycle_free(_M0L6_2aoldS2199);
  _M0L4selfS138->$0 = _M0L9new__dataS140;
  return 0;
}

int32_t _M0FPB31stringbuilder__growth__capacity(
  int32_t _M0L7currentS136,
  int32_t _M0L3lenS132,
  int32_t _M0L8requiredS131
) {
  int32_t _M0L5spaceS133;
  #line 48 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  if (_M0L8requiredS131 < _M0L3lenS132) {
    #line 55 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_31.data);
  }
  _M0L5spaceS133 = _M0L7currentS136;
  while (1) {
    if (_M0L5spaceS133 < _M0L8requiredS131) {
      int32_t _M0L4nextS134 = _M0L5spaceS133 * 2;
      if (_M0L4nextS134 <= _M0L5spaceS133) {
        return _M0L8requiredS131;
      }
      _M0L5spaceS133 = _M0L4nextS134;
      continue;
    } else {
      return _M0L5spaceS133;
    }
    break;
  }
}

int32_t _M0MPC14uint4UInt10to__uint16(uint32_t _M0L4selfS130) {
  int32_t _M0L6_2atmpS1123;
  #line 2785 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS1123 = *(int32_t*)&_M0L4selfS130;
  return (uint16_t)_M0L6_2atmpS1123;
}

uint32_t _M0MPC14char4Char8to__uint(int32_t _M0L4selfS129) {
  int32_t _M0L6_2atmpS1122;
  #line 1335 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS1122 = _M0L4selfS129;
  return *(uint32_t*)&_M0L6_2atmpS1122;
}

moonbit_string_t _M0MPB13StringBuilder10to__string(
  struct _M0TPB13StringBuilder* _M0L4selfS127
) {
  int32_t _M0L3lenS1113;
  #line 181 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L3lenS1113 = _M0L4selfS127->$1;
  if (_M0L3lenS1113 == 0) {
    return (moonbit_string_t)moonbit_string_literal_0.data;
  } else {
    int32_t _M0L3lenS1114 = _M0L4selfS127->$1;
    uint16_t* _M0L4dataS1116 = _M0L4selfS127->$0;
    int32_t _M0L6_2atmpS1115 = Moonbit_array_length(_M0L4dataS1116);
    if (_M0L3lenS1114 == _M0L6_2atmpS1115) {
      uint16_t* _M0L4dataS1117 = _M0L4selfS127->$0;
      moonbit_incref_cycle_free(_M0L4dataS1117);
      return _M0L4dataS1117;
    } else {
      uint16_t* _M0L4dataS1118 = _M0L4selfS127->$0;
      int32_t _M0L3lenS1119 = _M0L4selfS127->$1;
      int32_t _M0L6_2atmpS1120;
      int32_t _M0L3lenS1121;
      uint16_t* _M0L4dataS128;
      moonbit_incref_cycle_free(_M0L4dataS1118);
      #line 190 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0L6_2atmpS1120 = _M0IPC16uint166UInt16PB7Default7default();
      _M0L3lenS1121 = _M0L4selfS127->$1;
      #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0L4dataS128
      = _M0MPC15array10FixedArray23make__and__blit_2einnerGkE(_M0L4dataS1118, _M0L3lenS1119, _M0L6_2atmpS1120, _M0L3lenS1121, 0, 0);
      return _M0L4dataS128;
    }
  }
}

uint16_t* _M0MPC15array10FixedArray23make__and__blit_2einnerGkE(
  uint16_t* _M0L3srcS124,
  int32_t _M0L13allocate__lenS120,
  int32_t _M0L4initS125,
  int32_t _M0L3lenS121,
  int32_t _M0L11src__offsetS122,
  int32_t _M0L11dst__offsetS123
) {
  int32_t _if__result_2322;
  #line 97 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
  if (_M0L13allocate__lenS120 >= 0) {
    if (_M0L3lenS121 >= 0) {
      if (_M0L11src__offsetS122 >= 0) {
        if (_M0L11dst__offsetS123 >= 0) {
          int32_t _M0L6_2atmpS1109 = _M0L11src__offsetS122 + _M0L3lenS121;
          int32_t _M0L6_2atmpS1110 = Moonbit_array_length(_M0L3srcS124);
          if (_M0L6_2atmpS1109 <= _M0L6_2atmpS1110) {
            int32_t _M0L6_2atmpS1108 = _M0L11dst__offsetS123 + _M0L3lenS121;
            _if__result_2322 = _M0L6_2atmpS1108 <= _M0L13allocate__lenS120;
          } else {
            _if__result_2322 = 0;
          }
        } else {
          _if__result_2322 = 0;
        }
      } else {
        _if__result_2322 = 0;
      }
    } else {
      _if__result_2322 = 0;
    }
  } else {
    _if__result_2322 = 0;
  }
  if (_if__result_2322) {
    #line 116 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    return _M0MPC15array10FixedArray23unsafe__make__and__blitGkE(_M0L3srcS124, _M0L13allocate__lenS120, _M0L4initS125, _M0L11src__offsetS122, _M0L11dst__offsetS123, _M0L3lenS121);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS126;
    int32_t _M0L6_2atmpS1112;
    moonbit_string_t _M0L6_2atmpS1111;
    uint16_t* _result_2323;
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0L18_2astring__builderS126
    = _M0MPB13StringBuilder21StringBuilder_2einner(89);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS126, (moonbit_string_t)moonbit_string_literal_32.data);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS126, _M0L13allocate__lenS120);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS126, (moonbit_string_t)moonbit_string_literal_33.data);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS126, _M0L11src__offsetS122);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS126, (moonbit_string_t)moonbit_string_literal_34.data);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS126, _M0L11dst__offsetS123);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS126, (moonbit_string_t)moonbit_string_literal_35.data);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS126, _M0L3lenS121);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS126, (moonbit_string_t)moonbit_string_literal_36.data);
    _M0L6_2atmpS1112 = Moonbit_array_length(_M0L3srcS124);
    moonbit_decref_cycle_free(_M0L3srcS124);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS126, _M0L6_2atmpS1112);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0L6_2atmpS1111
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS126);
    moonbit_decref_cycle_free(_M0L18_2astring__builderS126);
    #line 112 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _result_2323 = _M0FPC15abort5abortGAkE(_M0L6_2atmpS1111);
    moonbit_decref_cycle_free(_M0L6_2atmpS1111);
    return _result_2323;
  }
}

uint16_t* _M0MPC15array10FixedArray23unsafe__make__and__blitGkE(
  uint16_t* _M0L3srcS117,
  int32_t _M0L13allocate__lenS114,
  int32_t _M0L4initS115,
  int32_t _M0L11src__offsetS118,
  int32_t _M0L11dst__offsetS116,
  int32_t _M0L9blit__lenS119
) {
  uint16_t* _M0L3dstS113;
  #line 79 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
  _M0L3dstS113
  = (uint16_t*)moonbit_make_string(_M0L13allocate__lenS114, _M0L4initS115);
  moonbit_incref_cycle_free(_M0L3dstS113);
  #line 90 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
  moonbit_unsafe_val_array_blit(_M0L3dstS113, _M0L11dst__offsetS116, _M0L3srcS117, _M0L11src__offsetS118, _M0L9blit__lenS119, sizeof(uint16_t));
  return _M0L3dstS113;
}

struct _M0TPB13StringBuilder* _M0MPB13StringBuilder21StringBuilder_2einner(
  int32_t _M0L10size__hintS111
) {
  int32_t _M0L7initialS110;
  uint16_t* _M0L4dataS112;
  struct _M0TPB13StringBuilder* _block_2324;
  #line 32 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  if (_M0L10size__hintS111 < 1) {
    _M0L7initialS110 = 1;
  } else {
    int32_t _M0L6_2atmpS1107 = _M0L10size__hintS111 + 1;
    _M0L7initialS110 = _M0L6_2atmpS1107 / 2;
  }
  _M0L4dataS112 = (uint16_t*)moonbit_make_string(_M0L7initialS110, 0);
  _block_2324
  = (struct _M0TPB13StringBuilder*)moonbit_malloc(sizeof(struct _M0TPB13StringBuilder));
  Moonbit_object_header(_block_2324)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 68, 0);
  _block_2324->$0 = _M0L4dataS112;
  _block_2324->$1 = 0;
  return _block_2324;
}

int32_t _M0MPC14byte4Byte8to__char(int32_t _M0L4selfS109) {
  int32_t _M0L6_2atmpS1106;
  #line 1950 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS1106 = (int32_t)_M0L4selfS109;
  return _M0L6_2atmpS1106;
}

moonbit_string_t* _M0MPB18UninitializedArray23make__and__blit_2einnerGsE(
  moonbit_string_t* _M0L3srcS95,
  int32_t _M0L13allocate__lenS91,
  int32_t _M0L3lenS92,
  int32_t _M0L11src__offsetS93,
  int32_t _M0L11dst__offsetS94
) {
  int32_t _if__result_2325;
  #line 201 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  if (_M0L13allocate__lenS91 >= 0) {
    if (_M0L3lenS92 >= 0) {
      if (_M0L11src__offsetS93 >= 0) {
        if (_M0L11dst__offsetS94 >= 0) {
          int32_t _M0L6_2atmpS1092 = _M0L11src__offsetS93 + _M0L3lenS92;
          int32_t _M0L6_2atmpS1093;
          #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
          _M0L6_2atmpS1093
          = _M0MPB18UninitializedArray6lengthGsE(_M0L3srcS95);
          if (_M0L6_2atmpS1092 <= _M0L6_2atmpS1093) {
            int32_t _M0L6_2atmpS1091 = _M0L11dst__offsetS94 + _M0L3lenS92;
            _if__result_2325 = _M0L6_2atmpS1091 <= _M0L13allocate__lenS91;
          } else {
            _if__result_2325 = 0;
          }
        } else {
          _if__result_2325 = 0;
        }
      } else {
        _if__result_2325 = 0;
      }
    } else {
      _if__result_2325 = 0;
    }
  } else {
    _if__result_2325 = 0;
  }
  if (_if__result_2325) {
    #line 219 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    return (moonbit_string_t*)moonbit_make_ref_array_with_blit(_M0L13allocate__lenS91, (moonbit_string_t)moonbit_string_literal_0.data, _M0L3srcS95, _M0L11src__offsetS93, _M0L11dst__offsetS94, _M0L3lenS92);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS96;
    int32_t _M0L6_2atmpS1095;
    moonbit_string_t _M0L6_2atmpS1094;
    moonbit_string_t* _result_2326;
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L18_2astring__builderS96
    = _M0MPB13StringBuilder21StringBuilder_2einner(89);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS96, (moonbit_string_t)moonbit_string_literal_32.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS96, _M0L13allocate__lenS91);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS96, (moonbit_string_t)moonbit_string_literal_33.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS96, _M0L11src__offsetS93);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS96, (moonbit_string_t)moonbit_string_literal_34.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS96, _M0L11dst__offsetS94);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS96, (moonbit_string_t)moonbit_string_literal_35.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS96, _M0L3lenS92);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS96, (moonbit_string_t)moonbit_string_literal_36.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS1095 = _M0MPB18UninitializedArray6lengthGsE(_M0L3srcS95);
    moonbit_decref_cycle_free(_M0L3srcS95);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS96, _M0L6_2atmpS1095);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS1094
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS96);
    moonbit_decref_cycle_free(_M0L18_2astring__builderS96);
    #line 215 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _result_2326
    = _M0FPC15abort5abortGRPB18UninitializedArrayGsEE(_M0L6_2atmpS1094);
    moonbit_decref_cycle_free(_M0L6_2atmpS1094);
    return _result_2326;
  }
}

struct _M0TUsiE** _M0MPB18UninitializedArray23make__and__blit_2einnerGUsiEE(
  struct _M0TUsiE** _M0L3srcS101,
  int32_t _M0L13allocate__lenS97,
  int32_t _M0L3lenS98,
  int32_t _M0L11src__offsetS99,
  int32_t _M0L11dst__offsetS100
) {
  int32_t _if__result_2327;
  #line 201 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  if (_M0L13allocate__lenS97 >= 0) {
    if (_M0L3lenS98 >= 0) {
      if (_M0L11src__offsetS99 >= 0) {
        if (_M0L11dst__offsetS100 >= 0) {
          int32_t _M0L6_2atmpS1097 = _M0L11src__offsetS99 + _M0L3lenS98;
          int32_t _M0L6_2atmpS1098;
          #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
          _M0L6_2atmpS1098
          = _M0MPB18UninitializedArray6lengthGUsiEE(_M0L3srcS101);
          if (_M0L6_2atmpS1097 <= _M0L6_2atmpS1098) {
            int32_t _M0L6_2atmpS1096 = _M0L11dst__offsetS100 + _M0L3lenS98;
            _if__result_2327 = _M0L6_2atmpS1096 <= _M0L13allocate__lenS97;
          } else {
            _if__result_2327 = 0;
          }
        } else {
          _if__result_2327 = 0;
        }
      } else {
        _if__result_2327 = 0;
      }
    } else {
      _if__result_2327 = 0;
    }
  } else {
    _if__result_2327 = 0;
  }
  if (_if__result_2327) {
    #line 219 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    return (struct _M0TUsiE**)moonbit_make_ref_array_with_blit(_M0L13allocate__lenS97, 0, _M0L3srcS101, _M0L11src__offsetS99, _M0L11dst__offsetS100, _M0L3lenS98);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS102;
    int32_t _M0L6_2atmpS1100;
    moonbit_string_t _M0L6_2atmpS1099;
    struct _M0TUsiE** _result_2328;
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L18_2astring__builderS102
    = _M0MPB13StringBuilder21StringBuilder_2einner(89);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS102, (moonbit_string_t)moonbit_string_literal_32.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS102, _M0L13allocate__lenS97);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS102, (moonbit_string_t)moonbit_string_literal_33.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS102, _M0L11src__offsetS99);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS102, (moonbit_string_t)moonbit_string_literal_34.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS102, _M0L11dst__offsetS100);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS102, (moonbit_string_t)moonbit_string_literal_35.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS102, _M0L3lenS98);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS102, (moonbit_string_t)moonbit_string_literal_36.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS1100 = _M0MPB18UninitializedArray6lengthGUsiEE(_M0L3srcS101);
    moonbit_decref_cycle_free(_M0L3srcS101);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS102, _M0L6_2atmpS1100);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS1099
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS102);
    moonbit_decref_cycle_free(_M0L18_2astring__builderS102);
    #line 215 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _result_2328
    = _M0FPC15abort5abortGRPB18UninitializedArrayGUsiEEE(_M0L6_2atmpS1099);
    moonbit_decref_cycle_free(_M0L6_2atmpS1099);
    return _result_2328;
  }
}

float* _M0MPB18UninitializedArray23make__and__blit_2einnerGfE(
  float* _M0L3srcS107,
  int32_t _M0L13allocate__lenS103,
  int32_t _M0L3lenS104,
  int32_t _M0L11src__offsetS105,
  int32_t _M0L11dst__offsetS106
) {
  int32_t _if__result_2329;
  #line 201 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  if (_M0L13allocate__lenS103 >= 0) {
    if (_M0L3lenS104 >= 0) {
      if (_M0L11src__offsetS105 >= 0) {
        if (_M0L11dst__offsetS106 >= 0) {
          int32_t _M0L6_2atmpS1102 = _M0L11src__offsetS105 + _M0L3lenS104;
          int32_t _M0L6_2atmpS1103;
          #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
          _M0L6_2atmpS1103
          = _M0MPB18UninitializedArray6lengthGfE(_M0L3srcS107);
          if (_M0L6_2atmpS1102 <= _M0L6_2atmpS1103) {
            int32_t _M0L6_2atmpS1101 = _M0L11dst__offsetS106 + _M0L3lenS104;
            _if__result_2329 = _M0L6_2atmpS1101 <= _M0L13allocate__lenS103;
          } else {
            _if__result_2329 = 0;
          }
        } else {
          _if__result_2329 = 0;
        }
      } else {
        _if__result_2329 = 0;
      }
    } else {
      _if__result_2329 = 0;
    }
  } else {
    _if__result_2329 = 0;
  }
  if (_if__result_2329) {
    #line 219 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    return _M0MPB18UninitializedArray23unsafe__make__and__blitGfE(_M0L3srcS107, _M0L13allocate__lenS103, _M0L11src__offsetS105, _M0L11dst__offsetS106, _M0L3lenS104);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS108;
    int32_t _M0L6_2atmpS1105;
    moonbit_string_t _M0L6_2atmpS1104;
    float* _result_2330;
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L18_2astring__builderS108
    = _M0MPB13StringBuilder21StringBuilder_2einner(89);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS108, (moonbit_string_t)moonbit_string_literal_32.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS108, _M0L13allocate__lenS103);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS108, (moonbit_string_t)moonbit_string_literal_33.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS108, _M0L11src__offsetS105);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS108, (moonbit_string_t)moonbit_string_literal_34.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS108, _M0L11dst__offsetS106);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS108, (moonbit_string_t)moonbit_string_literal_35.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS108, _M0L3lenS104);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS108, (moonbit_string_t)moonbit_string_literal_36.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS1105 = _M0MPB18UninitializedArray6lengthGfE(_M0L3srcS107);
    moonbit_decref_cycle_free(_M0L3srcS107);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS108, _M0L6_2atmpS1105);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS1104
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS108);
    moonbit_decref_cycle_free(_M0L18_2astring__builderS108);
    #line 215 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _result_2330
    = _M0FPC15abort5abortGRPB18UninitializedArrayGfEE(_M0L6_2atmpS1104);
    moonbit_decref_cycle_free(_M0L6_2atmpS1104);
    return _result_2330;
  }
}

int32_t _M0MPB13StringBuilder13write__objectGsE(
  struct _M0TPB13StringBuilder* _M0L4selfS86,
  moonbit_string_t _M0L3objS85
) {
  struct _M0TPB6Logger _M0L6_2atmpS1088;
  #line 17 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  moonbit_incref_cycle_free(_M0L4selfS86);
  _M0L6_2atmpS1088
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS86
  };
  #line 23 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  _M0IP016_24default__implPB4Show6outputGsE(_M0L3objS85, _M0L6_2atmpS1088);
  if (_M0L6_2atmpS1088.$1) {
    moonbit_decref(_M0L6_2atmpS1088.$1);
  }
  return 0;
}

int32_t _M0MPB13StringBuilder13write__objectGiE(
  struct _M0TPB13StringBuilder* _M0L4selfS88,
  int32_t _M0L3objS87
) {
  struct _M0TPB6Logger _M0L6_2atmpS1089;
  #line 17 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  moonbit_incref_cycle_free(_M0L4selfS88);
  _M0L6_2atmpS1089
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS88
  };
  #line 23 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  _M0IP016_24default__implPB4Show6outputGiE(_M0L3objS87, _M0L6_2atmpS1089);
  if (_M0L6_2atmpS1089.$1) {
    moonbit_decref(_M0L6_2atmpS1089.$1);
  }
  return 0;
}

int32_t _M0MPB13StringBuilder13write__objectGmE(
  struct _M0TPB13StringBuilder* _M0L4selfS90,
  uint64_t _M0L3objS89
) {
  struct _M0TPB6Logger _M0L6_2atmpS1090;
  #line 17 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  moonbit_incref_cycle_free(_M0L4selfS90);
  _M0L6_2atmpS1090
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS90
  };
  #line 23 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  _M0IP016_24default__implPB4Show6outputGmE(_M0L3objS89, _M0L6_2atmpS1090);
  if (_M0L6_2atmpS1090.$1) {
    moonbit_decref(_M0L6_2atmpS1090.$1);
  }
  return 0;
}

moonbit_string_t* _M0MPB18UninitializedArray23unsafe__make__and__blitGsE(
  moonbit_string_t* _M0L3srcS70,
  int32_t _M0L13allocate__lenS68,
  int32_t _M0L11src__offsetS71,
  int32_t _M0L11dst__offsetS69,
  int32_t _M0L9blit__lenS72
) {
  moonbit_string_t* _M0L3dstS67;
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  _M0L3dstS67
  = (moonbit_string_t*)moonbit_make_ref_array(_M0L13allocate__lenS68, (moonbit_string_t)moonbit_string_literal_0.data);
  #line 176 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  _M0MPB18UninitializedArray12unsafe__blitGsE(_M0L3dstS67, _M0L11dst__offsetS69, _M0L3srcS70, _M0L11src__offsetS71, _M0L9blit__lenS72);
  moonbit_decref_cycle_free(_M0L3srcS70);
  return _M0L3dstS67;
}

struct _M0TUsiE** _M0MPB18UninitializedArray23unsafe__make__and__blitGUsiEE(
  struct _M0TUsiE** _M0L3srcS76,
  int32_t _M0L13allocate__lenS74,
  int32_t _M0L11src__offsetS77,
  int32_t _M0L11dst__offsetS75,
  int32_t _M0L9blit__lenS78
) {
  struct _M0TUsiE** _M0L3dstS73;
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  _M0L3dstS73
  = (struct _M0TUsiE**)moonbit_make_ref_array(_M0L13allocate__lenS74, 0);
  #line 176 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  _M0MPB18UninitializedArray12unsafe__blitGUsiEE(_M0L3dstS73, _M0L11dst__offsetS75, _M0L3srcS76, _M0L11src__offsetS77, _M0L9blit__lenS78);
  moonbit_decref_cycle_free(_M0L3srcS76);
  return _M0L3dstS73;
}

float* _M0MPB18UninitializedArray23unsafe__make__and__blitGfE(
  float* _M0L3srcS82,
  int32_t _M0L13allocate__lenS80,
  int32_t _M0L11src__offsetS83,
  int32_t _M0L11dst__offsetS81,
  int32_t _M0L9blit__lenS84
) {
  float* _M0L3dstS79;
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  _M0L3dstS79 = (float*)moonbit_make_float_array_raw(_M0L13allocate__lenS80);
  #line 176 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  _M0MPB18UninitializedArray12unsafe__blitGfE(_M0L3dstS79, _M0L11dst__offsetS81, _M0L3srcS82, _M0L11src__offsetS83, _M0L9blit__lenS84);
  moonbit_decref_cycle_free(_M0L3srcS82);
  return _M0L3dstS79;
}

int32_t _M0MPB18UninitializedArray12unsafe__blitGsE(
  moonbit_string_t* _M0L3dstS52,
  int32_t _M0L11dst__offsetS53,
  moonbit_string_t* _M0L3srcS54,
  int32_t _M0L11src__offsetS55,
  int32_t _M0L3lenS56
) {
  #line 152 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  moonbit_incref_cycle_free(_M0L3srcS54);
  moonbit_incref_cycle_free(_M0L3dstS52);
  #line 161 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  moonbit_unsafe_ref_array_blit(_M0L3dstS52, _M0L11dst__offsetS53, _M0L3srcS54, _M0L11src__offsetS55, _M0L3lenS56);
  return 0;
}

int32_t _M0MPB18UninitializedArray12unsafe__blitGUsiEE(
  struct _M0TUsiE** _M0L3dstS57,
  int32_t _M0L11dst__offsetS58,
  struct _M0TUsiE** _M0L3srcS59,
  int32_t _M0L11src__offsetS60,
  int32_t _M0L3lenS61
) {
  #line 152 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  moonbit_incref_cycle_free(_M0L3srcS59);
  moonbit_incref_cycle_free(_M0L3dstS57);
  #line 161 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  moonbit_unsafe_ref_array_blit(_M0L3dstS57, _M0L11dst__offsetS58, _M0L3srcS59, _M0L11src__offsetS60, _M0L3lenS61);
  return 0;
}

int32_t _M0MPB18UninitializedArray12unsafe__blitGfE(
  float* _M0L3dstS62,
  int32_t _M0L11dst__offsetS63,
  float* _M0L3srcS64,
  int32_t _M0L11src__offsetS65,
  int32_t _M0L3lenS66
) {
  #line 152 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  moonbit_incref_cycle_free(_M0L3srcS64);
  moonbit_incref_cycle_free(_M0L3dstS62);
  #line 161 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  moonbit_unsafe_val_array_blit(_M0L3dstS62, _M0L11dst__offsetS63, _M0L3srcS64, _M0L11src__offsetS65, _M0L3lenS66, sizeof(float));
  return 0;
}

int32_t _M0MPC15array10FixedArray12unsafe__blitGkE(
  uint16_t* _M0L3dstS16,
  int32_t _M0L11dst__offsetS18,
  uint16_t* _M0L3srcS17,
  int32_t _M0L11src__offsetS19,
  int32_t _M0L3lenS21
) {
  #line 38 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
  if (
    _M0L3dstS16 == _M0L3srcS17 && _M0L11dst__offsetS18 < _M0L11src__offsetS19
  ) {
    int32_t _M0L1iS20 = 0;
    while (1) {
      if (_M0L1iS20 < _M0L3lenS21) {
        int32_t _M0L6_2atmpS1052 = _M0L11dst__offsetS18 + _M0L1iS20;
        int32_t _M0L6_2atmpS1054 = _M0L11src__offsetS19 + _M0L1iS20;
        int32_t _M0L6_2atmpS1053;
        int32_t _M0L6_2atmpS1055;
        if (
          _M0L6_2atmpS1054 < 0
          || _M0L6_2atmpS1054 >= Moonbit_array_length(_M0L3srcS17)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1053 = (int32_t)_M0L3srcS17[_M0L6_2atmpS1054];
        if (
          _M0L6_2atmpS1052 < 0
          || _M0L6_2atmpS1052 >= Moonbit_array_length(_M0L3dstS16)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS16[_M0L6_2atmpS1052] = _M0L6_2atmpS1053;
        _M0L6_2atmpS1055 = _M0L1iS20 + 1;
        _M0L1iS20 = _M0L6_2atmpS1055;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS17);
        moonbit_decref_cycle_free(_M0L3dstS16);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1060 = _M0L3lenS21 - 1;
    int32_t _M0L1iS23 = _M0L6_2atmpS1060;
    while (1) {
      if (_M0L1iS23 >= 0) {
        int32_t _M0L6_2atmpS1056 = _M0L11dst__offsetS18 + _M0L1iS23;
        int32_t _M0L6_2atmpS1058 = _M0L11src__offsetS19 + _M0L1iS23;
        int32_t _M0L6_2atmpS1057;
        int32_t _M0L6_2atmpS1059;
        if (
          _M0L6_2atmpS1058 < 0
          || _M0L6_2atmpS1058 >= Moonbit_array_length(_M0L3srcS17)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1057 = (int32_t)_M0L3srcS17[_M0L6_2atmpS1058];
        if (
          _M0L6_2atmpS1056 < 0
          || _M0L6_2atmpS1056 >= Moonbit_array_length(_M0L3dstS16)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS16[_M0L6_2atmpS1056] = _M0L6_2atmpS1057;
        _M0L6_2atmpS1059 = _M0L1iS23 - 1;
        _M0L1iS23 = _M0L6_2atmpS1059;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS17);
        moonbit_decref_cycle_free(_M0L3dstS16);
      }
      break;
    }
  }
  return 0;
}

int32_t _M0MPC15array10FixedArray12unsafe__blitGRPB17UnsafeMaybeUninitGsEE(
  moonbit_string_t* _M0L3dstS25,
  int32_t _M0L11dst__offsetS27,
  moonbit_string_t* _M0L3srcS26,
  int32_t _M0L11src__offsetS28,
  int32_t _M0L3lenS30
) {
  #line 38 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
  if (
    _M0L3dstS25 == _M0L3srcS26 && _M0L11dst__offsetS27 < _M0L11src__offsetS28
  ) {
    int32_t _M0L1iS29 = 0;
    while (1) {
      if (_M0L1iS29 < _M0L3lenS30) {
        int32_t _M0L6_2atmpS1061 = _M0L11dst__offsetS27 + _M0L1iS29;
        int32_t _M0L6_2atmpS1063 = _M0L11src__offsetS28 + _M0L1iS29;
        moonbit_string_t _M0L6_2atmpS1062;
        moonbit_string_t _M0L6_2aoldS2200;
        int32_t _M0L6_2atmpS1064;
        if (
          _M0L6_2atmpS1063 < 0
          || _M0L6_2atmpS1063 >= Moonbit_array_length(_M0L3srcS26)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1062 = (moonbit_string_t)_M0L3srcS26[_M0L6_2atmpS1063];
        if (
          _M0L6_2atmpS1061 < 0
          || _M0L6_2atmpS1061 >= Moonbit_array_length(_M0L3dstS25)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2aoldS2200 = (moonbit_string_t)_M0L3dstS25[_M0L6_2atmpS1061];
        moonbit_incref_cycle_free(_M0L6_2atmpS1062);
        moonbit_decref_cycle_free(_M0L6_2aoldS2200);
        _M0L3dstS25[_M0L6_2atmpS1061] = _M0L6_2atmpS1062;
        _M0L6_2atmpS1064 = _M0L1iS29 + 1;
        _M0L1iS29 = _M0L6_2atmpS1064;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS26);
        moonbit_decref_cycle_free(_M0L3dstS25);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1069 = _M0L3lenS30 - 1;
    int32_t _M0L1iS32 = _M0L6_2atmpS1069;
    while (1) {
      if (_M0L1iS32 >= 0) {
        int32_t _M0L6_2atmpS1065 = _M0L11dst__offsetS27 + _M0L1iS32;
        int32_t _M0L6_2atmpS1067 = _M0L11src__offsetS28 + _M0L1iS32;
        moonbit_string_t _M0L6_2atmpS1066;
        moonbit_string_t _M0L6_2aoldS2201;
        int32_t _M0L6_2atmpS1068;
        if (
          _M0L6_2atmpS1067 < 0
          || _M0L6_2atmpS1067 >= Moonbit_array_length(_M0L3srcS26)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1066 = (moonbit_string_t)_M0L3srcS26[_M0L6_2atmpS1067];
        if (
          _M0L6_2atmpS1065 < 0
          || _M0L6_2atmpS1065 >= Moonbit_array_length(_M0L3dstS25)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2aoldS2201 = (moonbit_string_t)_M0L3dstS25[_M0L6_2atmpS1065];
        moonbit_incref_cycle_free(_M0L6_2atmpS1066);
        moonbit_decref_cycle_free(_M0L6_2aoldS2201);
        _M0L3dstS25[_M0L6_2atmpS1065] = _M0L6_2atmpS1066;
        _M0L6_2atmpS1068 = _M0L1iS32 - 1;
        _M0L1iS32 = _M0L6_2atmpS1068;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS26);
        moonbit_decref_cycle_free(_M0L3dstS25);
      }
      break;
    }
  }
  return 0;
}

int32_t _M0MPC15array10FixedArray12unsafe__blitGRPB17UnsafeMaybeUninitGUsiEEE(
  struct _M0TUsiE** _M0L3dstS34,
  int32_t _M0L11dst__offsetS36,
  struct _M0TUsiE** _M0L3srcS35,
  int32_t _M0L11src__offsetS37,
  int32_t _M0L3lenS39
) {
  #line 38 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
  if (
    _M0L3dstS34 == _M0L3srcS35 && _M0L11dst__offsetS36 < _M0L11src__offsetS37
  ) {
    int32_t _M0L1iS38 = 0;
    while (1) {
      if (_M0L1iS38 < _M0L3lenS39) {
        int32_t _M0L6_2atmpS1070 = _M0L11dst__offsetS36 + _M0L1iS38;
        int32_t _M0L6_2atmpS1072 = _M0L11src__offsetS37 + _M0L1iS38;
        struct _M0TUsiE* _M0L6_2atmpS1071;
        struct _M0TUsiE* _M0L6_2aoldS2202;
        int32_t _M0L6_2atmpS1073;
        if (
          _M0L6_2atmpS1072 < 0
          || _M0L6_2atmpS1072 >= Moonbit_array_length(_M0L3srcS35)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1071 = (struct _M0TUsiE*)_M0L3srcS35[_M0L6_2atmpS1072];
        if (
          _M0L6_2atmpS1070 < 0
          || _M0L6_2atmpS1070 >= Moonbit_array_length(_M0L3dstS34)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2aoldS2202 = (struct _M0TUsiE*)_M0L3dstS34[_M0L6_2atmpS1070];
        if (_M0L6_2atmpS1071) {
          moonbit_incref_cycle_free(_M0L6_2atmpS1071);
        }
        if (_M0L6_2aoldS2202) {
          moonbit_decref_cycle_free(_M0L6_2aoldS2202);
        }
        _M0L3dstS34[_M0L6_2atmpS1070] = _M0L6_2atmpS1071;
        _M0L6_2atmpS1073 = _M0L1iS38 + 1;
        _M0L1iS38 = _M0L6_2atmpS1073;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS35);
        moonbit_decref_cycle_free(_M0L3dstS34);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1078 = _M0L3lenS39 - 1;
    int32_t _M0L1iS41 = _M0L6_2atmpS1078;
    while (1) {
      if (_M0L1iS41 >= 0) {
        int32_t _M0L6_2atmpS1074 = _M0L11dst__offsetS36 + _M0L1iS41;
        int32_t _M0L6_2atmpS1076 = _M0L11src__offsetS37 + _M0L1iS41;
        struct _M0TUsiE* _M0L6_2atmpS1075;
        struct _M0TUsiE* _M0L6_2aoldS2203;
        int32_t _M0L6_2atmpS1077;
        if (
          _M0L6_2atmpS1076 < 0
          || _M0L6_2atmpS1076 >= Moonbit_array_length(_M0L3srcS35)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1075 = (struct _M0TUsiE*)_M0L3srcS35[_M0L6_2atmpS1076];
        if (
          _M0L6_2atmpS1074 < 0
          || _M0L6_2atmpS1074 >= Moonbit_array_length(_M0L3dstS34)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2aoldS2203 = (struct _M0TUsiE*)_M0L3dstS34[_M0L6_2atmpS1074];
        if (_M0L6_2atmpS1075) {
          moonbit_incref_cycle_free(_M0L6_2atmpS1075);
        }
        if (_M0L6_2aoldS2203) {
          moonbit_decref_cycle_free(_M0L6_2aoldS2203);
        }
        _M0L3dstS34[_M0L6_2atmpS1074] = _M0L6_2atmpS1075;
        _M0L6_2atmpS1077 = _M0L1iS41 - 1;
        _M0L1iS41 = _M0L6_2atmpS1077;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS35);
        moonbit_decref_cycle_free(_M0L3dstS34);
      }
      break;
    }
  }
  return 0;
}

int32_t _M0MPC15array10FixedArray12unsafe__blitGRPB17UnsafeMaybeUninitGfEE(
  float* _M0L3dstS43,
  int32_t _M0L11dst__offsetS45,
  float* _M0L3srcS44,
  int32_t _M0L11src__offsetS46,
  int32_t _M0L3lenS48
) {
  #line 38 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
  if (
    _M0L3dstS43 == _M0L3srcS44 && _M0L11dst__offsetS45 < _M0L11src__offsetS46
  ) {
    int32_t _M0L1iS47 = 0;
    while (1) {
      if (_M0L1iS47 < _M0L3lenS48) {
        int32_t _M0L6_2atmpS1079 = _M0L11dst__offsetS45 + _M0L1iS47;
        int32_t _M0L6_2atmpS1081 = _M0L11src__offsetS46 + _M0L1iS47;
        float _M0L6_2atmpS1080;
        int32_t _M0L6_2atmpS1082;
        if (
          _M0L6_2atmpS1081 < 0
          || _M0L6_2atmpS1081 >= Moonbit_array_length(_M0L3srcS44)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1080 = (float)_M0L3srcS44[_M0L6_2atmpS1081];
        if (
          _M0L6_2atmpS1079 < 0
          || _M0L6_2atmpS1079 >= Moonbit_array_length(_M0L3dstS43)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS43[_M0L6_2atmpS1079] = _M0L6_2atmpS1080;
        _M0L6_2atmpS1082 = _M0L1iS47 + 1;
        _M0L1iS47 = _M0L6_2atmpS1082;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS44);
        moonbit_decref_cycle_free(_M0L3dstS43);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1087 = _M0L3lenS48 - 1;
    int32_t _M0L1iS50 = _M0L6_2atmpS1087;
    while (1) {
      if (_M0L1iS50 >= 0) {
        int32_t _M0L6_2atmpS1083 = _M0L11dst__offsetS45 + _M0L1iS50;
        int32_t _M0L6_2atmpS1085 = _M0L11src__offsetS46 + _M0L1iS50;
        float _M0L6_2atmpS1084;
        int32_t _M0L6_2atmpS1086;
        if (
          _M0L6_2atmpS1085 < 0
          || _M0L6_2atmpS1085 >= Moonbit_array_length(_M0L3srcS44)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1084 = (float)_M0L3srcS44[_M0L6_2atmpS1085];
        if (
          _M0L6_2atmpS1083 < 0
          || _M0L6_2atmpS1083 >= Moonbit_array_length(_M0L3dstS43)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS43[_M0L6_2atmpS1083] = _M0L6_2atmpS1084;
        _M0L6_2atmpS1086 = _M0L1iS50 - 1;
        _M0L1iS50 = _M0L6_2atmpS1086;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS44);
        moonbit_decref_cycle_free(_M0L3dstS43);
      }
      break;
    }
  }
  return 0;
}

int32_t _M0MPB18UninitializedArray6lengthGsE(moonbit_string_t* _M0L4selfS13) {
  #line 146 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  return Moonbit_array_length(_M0L4selfS13);
}

int32_t _M0MPB18UninitializedArray6lengthGUsiEE(
  struct _M0TUsiE** _M0L4selfS14
) {
  #line 146 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  return Moonbit_array_length(_M0L4selfS14);
}

int32_t _M0MPB18UninitializedArray6lengthGfE(float* _M0L4selfS15) {
  #line 146 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  return Moonbit_array_length(_M0L4selfS15);
}

int32_t _M0IPB7FailurePB4Show6output(
  void* _M0L10_2ax__6387S9,
  struct _M0TPB6Logger _M0L10_2ax__6388S12
) {
  struct _M0DTPC15error5Error48moonbitlang_2fcore_2fbuiltin_2eFailure_2eFailure* _M0L10_2aFailureS10;
  moonbit_string_t _M0L15_2a_2aarg__6389S11;
  #line 39 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\failure.mbt"
  _M0L10_2aFailureS10
  = (struct _M0DTPC15error5Error48moonbitlang_2fcore_2fbuiltin_2eFailure_2eFailure*)_M0L10_2ax__6387S9;
  _M0L15_2a_2aarg__6389S11 = _M0L10_2aFailureS10->$0;
  #line 39 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\failure.mbt"
  _M0L10_2ax__6388S12.$0->$method_0(_M0L10_2ax__6388S12.$1, (moonbit_string_t)moonbit_string_literal_37.data);
  #line 39 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\failure.mbt"
  _M0MPB6Logger13write__objectGsE(_M0L10_2ax__6388S12, _M0L15_2a_2aarg__6389S11);
  #line 39 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\failure.mbt"
  _M0L10_2ax__6388S12.$0->$method_0(_M0L10_2ax__6388S12.$1, (moonbit_string_t)moonbit_string_literal_38.data);
  return 0;
}

int32_t _M0MPB6Logger13write__objectGsE(
  struct _M0TPB6Logger _M0L4selfS8,
  moonbit_string_t _M0L3objS7
) {
  #line 179 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  #line 180 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0IP016_24default__implPB4Show6outputGsE(_M0L3objS7, _M0L4selfS8);
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

moonbit_string_t* _M0FPC15abort5abortGRPB18UninitializedArrayGsEE(
  moonbit_string_t _M0L3msgS3
) {
  #line 47 "C:\\Users\\31379\\.moon\\lib\\core\\abort\\abort.mbt"
  #line 49 "C:\\Users\\31379\\.moon\\lib\\core\\abort\\abort.mbt"
  moonbit_println(_M0L3msgS3);
  #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\abort\\abort.mbt"
  moonbit_panic();
}

struct _M0TUsiE** _M0FPC15abort5abortGRPB18UninitializedArrayGUsiEEE(
  moonbit_string_t _M0L3msgS4
) {
  #line 47 "C:\\Users\\31379\\.moon\\lib\\core\\abort\\abort.mbt"
  #line 49 "C:\\Users\\31379\\.moon\\lib\\core\\abort\\abort.mbt"
  moonbit_println(_M0L3msgS4);
  #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\abort\\abort.mbt"
  moonbit_panic();
}

float* _M0FPC15abort5abortGRPB18UninitializedArrayGfEE(
  moonbit_string_t _M0L3msgS5
) {
  #line 47 "C:\\Users\\31379\\.moon\\lib\\core\\abort\\abort.mbt"
  #line 49 "C:\\Users\\31379\\.moon\\lib\\core\\abort\\abort.mbt"
  moonbit_println(_M0L3msgS5);
  #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\abort\\abort.mbt"
  moonbit_panic();
}

int32_t _M0FPC15abort5abortGiE(moonbit_string_t _M0L3msgS6) {
  #line 47 "C:\\Users\\31379\\.moon\\lib\\core\\abort\\abort.mbt"
  #line 49 "C:\\Users\\31379\\.moon\\lib\\core\\abort\\abort.mbt"
  moonbit_println(_M0L3msgS6);
  #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\abort\\abort.mbt"
  moonbit_panic();
}

moonbit_string_t _M0FP15Error10to__string(void* _M0L4_2aeS1023) {
  switch (Moonbit_object_tag(_M0L4_2aeS1023)) {
    case 2: {
      return (moonbit_string_t)moonbit_string_literal_39.data;
      break;
    }
    
    case 0: {
      return _M0IP016_24default__implPB4Show10to__stringGRPB7FailureE(_M0L4_2aeS1023);
      break;
    }
    
    case 3: {
      return (moonbit_string_t)moonbit_string_literal_40.data;
      break;
    }
    
    case 1: {
      return (moonbit_string_t)moonbit_string_literal_41.data;
      break;
    }
    default: {
      return (moonbit_string_t)moonbit_string_literal_42.data;
      break;
    }
  }
}

int32_t _M0IP016_24default__implPB6Logger61write_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLoggerGRPB13StringBuilderE(
  void* _M0L11_2aobj__ptrS1047,
  struct _M0TPB4Show _M0L8_2aparamS1046
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS1045 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS1047;
  _M0IP016_24default__implPB6Logger5writeGRPB13StringBuilderE(_M0L7_2aselfS1045, _M0L8_2aparamS1046);
  return 0;
}

int32_t _M0IP016_24default__implPB6Logger84write__string__interpolation_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLoggerGRPB13StringBuilderE(
  void* _M0L11_2aobj__ptrS1044,
  struct _M0TPB4Show _M0L8_2aparamS1043
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS1042 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS1044;
  _M0IP016_24default__implPB6Logger28write__string__interpolationGRPB13StringBuilderE(_M0L7_2aselfS1042, _M0L8_2aparamS1043);
  return 0;
}

int32_t _M0IPB13StringBuilderPB6Logger67write__char_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLogger(
  void* _M0L11_2aobj__ptrS1041,
  int32_t _M0L8_2aparamS1040
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS1039 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS1041;
  _M0IPB13StringBuilderPB6Logger11write__char(_M0L7_2aselfS1039, _M0L8_2aparamS1040);
  return 0;
}

int32_t _M0IPB13StringBuilderPB6Logger67write__view_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLogger(
  void* _M0L11_2aobj__ptrS1038,
  struct _M0TPC16string10StringView _M0L8_2aparamS1037
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS1036 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS1038;
  _M0IPB13StringBuilderPB6Logger11write__view(_M0L7_2aselfS1036, _M0L8_2aparamS1037);
  return 0;
}

int32_t _M0IP016_24default__implPB6Logger72write__substring_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLoggerGRPB13StringBuilderE(
  void* _M0L11_2aobj__ptrS1035,
  moonbit_string_t _M0L8_2aparamS1032,
  int32_t _M0L8_2aparamS1033,
  int32_t _M0L8_2aparamS1034
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS1031 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS1035;
  _M0IP016_24default__implPB6Logger16write__substringGRPB13StringBuilderE(_M0L7_2aselfS1031, _M0L8_2aparamS1032, _M0L8_2aparamS1033, _M0L8_2aparamS1034);
  return 0;
}

int32_t _M0IPB13StringBuilderPB6Logger69write__string_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLogger(
  void* _M0L11_2aobj__ptrS1030,
  moonbit_string_t _M0L8_2aparamS1029
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS1028 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS1030;
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L7_2aselfS1028, _M0L8_2aparamS1029);
  return 0;
}

void moonbit_init() {
  moonbit_layout_table = moonbit_layout_table_data;
}

int main(int argc, char** argv) {
  struct _M0TWWuEuWRPC15error5ErrorEuEOuQRPC15error5Error** _M0L6_2atmpS1051;
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12async__testsS1016;
  struct _M0TPB5ArrayGUsiEE* _M0L7_2abindS1017;
  int32_t _M0L7_2abindS1018;
  struct _M0TUsiE** _M0L7_2abindS1019;
  int32_t _M0L6_2acntS2208;
  int32_t _M0L2__S1020;
  moonbit_runtime_init(argc, argv);
  moonbit_init();
  _M0L6_2atmpS1051
  = (struct _M0TWWuEuWRPC15error5ErrorEuEOuQRPC15error5Error**)moonbit_empty_ref_array;
  _M0L12async__testsS1016
  = (struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE));
  Moonbit_object_header(_M0L12async__testsS1016)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 71, 0);
  _M0L12async__testsS1016->$0 = _M0L6_2atmpS1051;
  _M0L12async__testsS1016->$1 = 0;
  #line 447 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\adex\\__generated_driver_for_blackbox_test.mbt"
  _M0L7_2abindS1017
  = _M0FP46RiantR8snn__mbt8examples20adex__blackbox__test52moonbit__test__driver__internal__native__parse__args();
  _M0L7_2abindS1018 = _M0L7_2abindS1017->$1;
  _M0L7_2abindS1019 = _M0L7_2abindS1017->$0;
  _M0L6_2acntS2208
  = Moonbit_rc_count(Moonbit_object_header(_M0L7_2abindS1017));
  if (_M0L6_2acntS2208 > 1) {
    int32_t _M0L11_2anew__cntS2209 = _M0L6_2acntS2208 - 1;
    Moonbit_set_rc_count(Moonbit_object_header(_M0L7_2abindS1017), _M0L11_2anew__cntS2209);
    moonbit_incref_cycle_free(_M0L7_2abindS1019);
  } else if (_M0L6_2acntS2208 == 1) {
    #line 447 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\adex\\__generated_driver_for_blackbox_test.mbt"
    moonbit_free(_M0L7_2abindS1017);
  }
  _M0L2__S1020 = 0;
  while (1) {
    if (_M0L2__S1020 < _M0L7_2abindS1018) {
      struct _M0TUsiE* _M0L3argS1021 =
        (struct _M0TUsiE*)_M0L7_2abindS1019[_M0L2__S1020];
      moonbit_string_t _M0L6_2atmpS1048 = _M0L3argS1021->$0;
      int32_t _M0L6_2atmpS1049 = _M0L3argS1021->$1;
      int32_t _M0L6_2atmpS1050;
      moonbit_incref_cycle_free(_M0L6_2atmpS1048);
      #line 448 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\adex\\__generated_driver_for_blackbox_test.mbt"
      _M0FP46RiantR8snn__mbt8examples20adex__blackbox__test44moonbit__test__driver__internal__do__execute(_M0L12async__testsS1016, _M0L6_2atmpS1048, _M0L6_2atmpS1049);
      moonbit_decref_cycle_free(_M0L6_2atmpS1048);
      _M0L6_2atmpS1050 = _M0L2__S1020 + 1;
      _M0L2__S1020 = _M0L6_2atmpS1050;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L7_2abindS1019);
    }
    break;
  }
  #line 450 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\adex\\__generated_driver_for_blackbox_test.mbt"
  _M0IP016_24default__implP46RiantR8snn__mbt8examples20adex__blackbox__test28MoonBit__Async__Test__Driver17run__async__testsGRP46RiantR8snn__mbt8examples20adex__blackbox__test34MoonBit__Async__Test__Driver__ImplE(_M0L12async__testsS1016);
  moonbit_decref_cycle_free(_M0L12async__testsS1016);
  moonbit_flush_cycles();
  return 0;
}